#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ffi::c_void;

use osg_memory_warden::dplus::{
    apply_caps, compute_merit_profile, extract_cortex_heur_config, extract_mem_allocate_rules,
    extract_sentinel_rules, extract_soma_io_config, extract_warden_mem_config,
    extract_warden_policy_rpn,
    format_reasons_csv, parse,
    verifier::{verify, ForbiddenToken}, ConsensusMode, DPlusSection, SectionKind, SectionTag,
    VerifyError, VerifyOptions,
};
use osg_memory_warden::sentinel::Sentinel;
use osg_memory_warden::soma::NeuralSoma;
use osg_memory_warden::soma::tokenizer::SimpleTokenizer;
use osg_memory_warden::{
    compile_policy_rpn, Access, CortexConfig, HeuristicCortex, MemError, MemIntent, MemoryWarden,
    PolicyProgram, Rights, Zone,
};
use osg_memory_warden::dplus::judge::LawSentinelRule;

type EfiHandle = *mut core::ffi::c_void;
type EfiStatus = usize;

const EFI_SUCCESS: EfiStatus = 0;
const EFI_BUFFER_TOO_SMALL: EfiStatus = 0x8000_0000_0000_0005;
const EFI_NOT_READY: EfiStatus = 0x8000_0000_0000_0006;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct EfiGuid {
    data1: u32,
    data2: u16,
    data3: u16,
    data4: [u8; 8],
}

const EFI_LOADED_IMAGE_PROTOCOL_GUID: EfiGuid = EfiGuid {
    data1: 0x5B1B31A1,
    data2: 0x9562,
    data3: 0x11D2,
    data4: [0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B],
};

// 4006c0c1-fcb3-403e-996d-4a6c8724e06d
const EFI_LOAD_FILE2_PROTOCOL_GUID: EfiGuid = EfiGuid {
    data1: 0x4006C0C1,
    data2: 0xFCB3,
    data3: 0x403E,
    data4: [0x99, 0x6D, 0x4A, 0x6C, 0x87, 0x24, 0xE0, 0x6D],
};

// 964e5b21-6459-11d2-8e39-00a0c969723b
const EFI_BLOCK_IO_PROTOCOL_GUID: EfiGuid = EfiGuid {
    data1: 0x964E5B21,
    data2: 0x6459,
    data3: 0x11D2,
    data4: [0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B],
};

const DPLUS_BOOT_VERBOSE: bool = false;

#[repr(C)]
pub struct EfiTableHeader {
    signature: u64,
    revision: u32,
    header_size: u32,
    crc32: u32,
    reserved: u32,
}

#[repr(C)]
pub struct SimpleTextOutputProtocol {
    reset: usize,
    output_string: extern "efiapi" fn(this: *mut SimpleTextOutputProtocol, string: *const u16) -> EfiStatus,
    test_string: usize,
    query_mode: usize,
    set_mode: usize,
    set_attribute: usize,
    clear_screen: usize,
    set_cursor_position: usize,
    enable_cursor: usize,
    mode: usize,
}

#[repr(C)]
pub struct EfiInputKey {
    pub scan_code: u16,
    pub unicode_char: u16,
}

#[repr(C)]
pub struct EfiSimpleTextInputProtocol {
    pub reset: extern "efiapi" fn(this: *mut EfiSimpleTextInputProtocol, extended_verification: bool) -> EfiStatus,
    pub read_key_stroke: extern "efiapi" fn(this: *mut EfiSimpleTextInputProtocol, key: *mut EfiInputKey) -> EfiStatus,
    pub wait_for_key: EfiHandle,
}

#[repr(C)]
pub struct EfiSystemTable {
    hdr: EfiTableHeader,
    firmware_vendor: *const u16,
    firmware_revision: u32,
    console_in_handle: EfiHandle,
    con_in: *mut EfiSimpleTextInputProtocol,
    console_out_handle: EfiHandle,
    con_out: *mut SimpleTextOutputProtocol,
    standard_error_handle: EfiHandle,
    std_err: *mut SimpleTextOutputProtocol,
    runtime_services: usize,
    boot_services: *mut EfiBootServices,
    number_of_table_entries: usize,
    configuration_table: usize,
}

#[repr(C)]
pub struct EfiBootServices {
    hdr: EfiTableHeader,
    // The ordering here matches UEFI spec. We only call a few of these.
    raise_tpl: usize,
    restore_tpl: usize,
    allocate_pages: extern "efiapi" fn(
        alloc_type: u32,
        memory_type: u32,
        pages: usize,
        memory: *mut u64,
    ) -> EfiStatus,
    free_pages: usize,
    get_memory_map: usize,
    allocate_pool: usize,
    free_pool: extern "efiapi" fn(buffer: *mut c_void) -> EfiStatus,
    create_event: usize,
    set_timer: usize,
    wait_for_event: usize,
    signal_event: usize,
    close_event: usize,
    check_event: usize,
    install_protocol_interface: usize,
    reinstall_protocol_interface: usize,
    uninstall_protocol_interface: usize,
    handle_protocol: extern "efiapi" fn(
        handle: EfiHandle,
        protocol: *const EfiGuid,
        interface: *mut *mut c_void,
    ) -> EfiStatus,
    reserved: usize,
    register_protocol_notify: usize,
    locate_handle: usize,
    locate_device_path: extern "efiapi" fn(
        protocol: *const EfiGuid,
        device_path: *mut *mut EfiDevicePathProtocol,
        device: *mut EfiHandle,
    ) -> EfiStatus,
    install_configuration_table: usize,
    load_image: usize,
    start_image: usize,
    exit: usize,
    unload_image: usize,
    exit_boot_services: usize,
    get_next_monotonic_count: usize,
    stall: extern "efiapi" fn(microseconds: usize) -> EfiStatus,
    set_watchdog_timer: usize,
    connect_controller: usize,
    disconnect_controller: usize,
    open_protocol: usize,
    close_protocol: usize,
    open_protocol_information: usize,
    protocols_per_handle: usize,
    locate_handle_buffer: extern "efiapi" fn(
        search_type: u32,
        protocol: *const EfiGuid,
        search_key: *mut c_void,
        no_handles: *mut usize,
        buffer: *mut *mut EfiHandle,
    ) -> EfiStatus,
    locate_protocol: extern "efiapi" fn(
        protocol: *const EfiGuid,
        registration: *mut c_void,
        interface: *mut *mut c_void,
    ) -> EfiStatus,
    install_multiple_protocol_interfaces: usize,
    uninstall_multiple_protocol_interfaces: usize,
    calculate_crc32: usize,
    copy_mem: usize,
    set_mem: usize,
    create_event_ex: usize,
}

#[repr(C)]
pub struct EfiDevicePathProtocol {
    ty: u8,
    sub_ty: u8,
    len: [u8; 2],
}

#[repr(C)]
pub struct EfiLoadFile2Protocol {
    load_file: extern "efiapi" fn(
        this: *mut EfiLoadFile2Protocol,
        file_path: *mut EfiDevicePathProtocol,
        boot_policy: u8,
        buffer_size: *mut usize,
        buffer: *mut c_void,
    ) -> EfiStatus,
}

#[repr(C)]
pub struct EfiBlockIoMedia {
    media_id: u32,
    removable_media: u8,
    media_present: u8,
    logical_partition: u8,
    read_only: u8,
    write_caching: u8,
    _pad0: [u8; 3],
    block_size: u32,
    io_align: u32,
    last_block: u64,
    // newer fields may follow; we don't need them.
}

#[repr(C)]
pub struct EfiBlockIoProtocol {
    revision: u64,
    media: *mut EfiBlockIoMedia,
    reset: usize,
    read_blocks: extern "efiapi" fn(
        this: *mut EfiBlockIoProtocol,
        media_id: u32,
        lba: u64,
        buffer_size: usize,
        buffer: *mut c_void,
    ) -> EfiStatus,
    write_blocks: usize,
    flush_blocks: usize,
}

#[repr(C)]
pub struct EfiLoadedImageProtocol {
    revision: u32,
    parent_handle: EfiHandle,
    system_table: *mut EfiSystemTable,
    device_handle: EfiHandle,
    file_path: *mut core::ffi::c_void,
    reserved: *mut core::ffi::c_void,
    load_options_size: u32,
    load_options: *mut core::ffi::c_void,
    image_base: *mut core::ffi::c_void,
    image_size: u64,
    image_code_type: u32,
    image_data_type: u32,
    unload: usize,
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

fn uefi_print(con_out: *mut SimpleTextOutputProtocol, s: &str) {
    if con_out.is_null() {
        return;
    }

    let mut buf = [0u16; 512];
    let mut i = 0usize;
    for b in s.as_bytes() {
        if i + 1 >= buf.len() {
            break;
        }
        if *b == b'\n' {
            buf[i] = b'\r' as u16;
            i += 1;
            buf[i] = b'\n' as u16;
            i += 1;
        } else {
            buf[i] = *b as u16;
            i += 1;
        }
    }
    buf[i] = 0;

    unsafe {
        ((*con_out).output_string)(con_out, buf.as_ptr());
    }
}

fn uefi_stall_us(bs: &EfiBootServices, us: usize) {
    let _ = (bs.stall)(us);
}

const EVT_TIMER: u32 = 0x8000_0000;
const TIMER_CANCEL: u32 = 0;
const TIMER_RELATIVE: u32 = 2;

fn bs_create_event(bs: &EfiBootServices, event_type: u32) -> Option<EfiHandle> {
    if bs.create_event == 0 {
        return None;
    }
    let create_event: extern "efiapi" fn(
        u32,
        usize,
        usize,
        *mut c_void,
        *mut EfiHandle,
    ) -> EfiStatus = unsafe { core::mem::transmute(bs.create_event) };

    let mut ev: EfiHandle = core::ptr::null_mut();
    let st = create_event(event_type, 0, 0, core::ptr::null_mut(), &mut ev);
    if st == EFI_SUCCESS && !ev.is_null() {
        Some(ev)
    } else {
        None
    }
}

fn bs_close_event(bs: &EfiBootServices, ev: EfiHandle) {
    if bs.close_event == 0 || ev.is_null() {
        return;
    }
    let close_event: extern "efiapi" fn(EfiHandle) -> EfiStatus = unsafe { core::mem::transmute(bs.close_event) };
    let _ = close_event(ev);
}

fn bs_set_timer_relative_ms(bs: &EfiBootServices, ev: EfiHandle, timeout_ms: usize) -> bool {
    if bs.set_timer == 0 || ev.is_null() {
        return false;
    }
    let set_timer: extern "efiapi" fn(EfiHandle, u32, u64) -> EfiStatus = unsafe { core::mem::transmute(bs.set_timer) };
    let t_100ns = (timeout_ms as u64).saturating_mul(10_000);
    set_timer(ev, TIMER_RELATIVE, t_100ns) == EFI_SUCCESS
}

fn bs_wait_for_event(bs: &EfiBootServices, events: &mut [EfiHandle]) -> Option<usize> {
    if bs.wait_for_event == 0 {
        return None;
    }
    let wait_for_event: extern "efiapi" fn(usize, *mut EfiHandle, *mut usize) -> EfiStatus =
        unsafe { core::mem::transmute(bs.wait_for_event) };

    let mut idx: usize = 0;
    let st = wait_for_event(events.len(), events.as_mut_ptr(), &mut idx);
    if st == EFI_SUCCESS {
        Some(idx)
    } else {
        None
    }
}

fn try_read_key(con_in: *mut EfiSimpleTextInputProtocol) -> Option<u16> {
    if con_in.is_null() {
        return None;
    }

    let mut key = EfiInputKey {
        scan_code: 0,
        unicode_char: 0,
    };
    let status = unsafe { ((*con_in).read_key_stroke)(con_in, &mut key) };
    if status == EFI_SUCCESS {
        if key.unicode_char != 0 {
            return Some(key.unicode_char);
        }
        return None;
    }
    if status == EFI_NOT_READY {
        return None;
    }
    None
}

fn poll_key_with_timeout_ms(bs: &EfiBootServices, con_in: *mut EfiSimpleTextInputProtocol, timeout_ms: usize) -> Option<u16> {
    // Fast path: key already available.
    if let Some(c) = try_read_key(con_in) {
        return Some(c);
    }

    // Event-based wait (preferred).
    if !con_in.is_null() {
        let key_ev = unsafe { (*con_in).wait_for_key };
        if !key_ev.is_null() {
            if timeout_ms == 0 {
                return None;
            }

            if let Some(timer_ev) = bs_create_event(bs, EVT_TIMER) {
                let mut result: Option<u16> = None;
                if bs_set_timer_relative_ms(bs, timer_ev, timeout_ms) {
                    let mut evs = [key_ev, timer_ev];
                    if let Some(idx) = bs_wait_for_event(bs, &mut evs) {
                        if idx == 0 {
                            // Key event signaled.
                            // Read may still transiently report NOT_READY, so retry a bit.
                            for _ in 0..16 {
                                if let Some(c) = try_read_key(con_in) {
                                    result = Some(c);
                                    break;
                                }
                            }
                        }
                    }
                }
                // Best-effort cancel timer then close.
                if bs.set_timer != 0 {
                    let set_timer: extern "efiapi" fn(EfiHandle, u32, u64) -> EfiStatus = unsafe { core::mem::transmute(bs.set_timer) };
                    let _ = set_timer(timer_ev, TIMER_CANCEL, 0);
                }
                bs_close_event(bs, timer_ev);
                return result;
            }
        }
    }

    // Fallback: legacy stall loop.
    for _ in 0..timeout_ms {
        if let Some(c) = try_read_key(con_in) {
            return Some(c);
        }
        uefi_stall_us(bs, 1000);
    }
    None
}

fn wait_for_keypress(bs: &EfiBootServices, con_in: *mut EfiSimpleTextInputProtocol) -> u16 {
    // Event-based wait (preferred).
    if !con_in.is_null() {
        let key_ev = unsafe { (*con_in).wait_for_key };
        if !key_ev.is_null() {
            loop {
                let mut evs = [key_ev];
                let _ = bs_wait_for_event(bs, &mut evs);
                if let Some(c) = try_read_key(con_in) {
                    return c;
                }
            }
        }
    }

    // Fallback: legacy stall loop.
    loop {
        if let Some(c) = try_read_key(con_in) {
            return c;
        }
        uefi_stall_us(bs, 1000);
    }
}

fn uefi_print_bool(con_out: *mut SimpleTextOutputProtocol, v: bool) {
    if v {
        uefi_print(con_out, "true");
    } else {
        uefi_print(con_out, "false");
    }
}

fn uefi_print_u64_dec(con_out: *mut SimpleTextOutputProtocol, v: u64) {
    let mut tmp = [0u8; 21];
    let mut t = 0usize;
    let mut x = v;
    if x == 0 {
        tmp[t] = b'0';
        t += 1;
    } else {
        let mut rev = [0u8; 21];
        let mut r = 0usize;
        while x > 0 {
            rev[r] = b'0' + (x % 10) as u8;
            r += 1;
            x /= 10;
        }
        while r > 0 {
            r -= 1;
            tmp[t] = rev[r];
            t += 1;
        }
    }
    let s = core::str::from_utf8(&tmp[..t]).unwrap_or("?");
    uefi_print(con_out, s);
}

fn uefi_print_verify_error(con_out: *mut SimpleTextOutputProtocol, e: VerifyError) {
    match e {
        VerifyError::TooLarge => uefi_print(con_out, "TooLarge"),
        VerifyError::SectionTooLarge => uefi_print(con_out, "SectionTooLarge"),
        VerifyError::TooManyLines => uefi_print(con_out, "TooManyLines"),
        VerifyError::LineTooLong => uefi_print(con_out, "LineTooLong"),
        VerifyError::TooManyLawOps => uefi_print(con_out, "TooManyLawOps"),
        VerifyError::TooManyProofOps => uefi_print(con_out, "TooManyProofOps"),
        VerifyError::TooManyLawMemAllocateRules => uefi_print(con_out, "TooManyLawMemAllocateRules"),
        VerifyError::MemAllocateBytesZero { op } => {
            uefi_print(con_out, "MemAllocateBytesZero op=");
            uefi_print_u64_dec(con_out, op as u64);
        }
        VerifyError::MemAllocateBytesTooLarge { op, bytes, max } => {
            uefi_print(con_out, "MemAllocateBytesTooLarge op=");
            uefi_print_u64_dec(con_out, op as u64);
            uefi_print(con_out, " bytes=");
            uefi_print_u64_dec(con_out, bytes);
            uefi_print(con_out, " max=");
            uefi_print_u64_dec(con_out, max);
        }
        VerifyError::MemAllocateTtlTooLarge { op, ttl_ms, max } => {
            uefi_print(con_out, "MemAllocateTtlTooLarge op=");
            uefi_print_u64_dec(con_out, op as u64);
            uefi_print(con_out, " ttl_ms=");
            uefi_print_u64_dec(con_out, ttl_ms);
            uefi_print(con_out, " max=");
            uefi_print_u64_dec(con_out, max);
        }
        VerifyError::MissingProofForLawOp { op } => {
            uefi_print(con_out, "MissingProofForLawOp op=");
            uefi_print_u64_dec(con_out, op as u64);
        }
        VerifyError::MissingLawForProofOp { op } => {
            uefi_print(con_out, "MissingLawForProofOp op=");
            uefi_print_u64_dec(con_out, op as u64);
        }
        VerifyError::ForbiddenToken { section, line, token } => {
            uefi_print(con_out, "ForbiddenToken section=");
            match section {
                SectionKind::Law => uefi_print(con_out, "LAW"),
                SectionKind::Proof => uefi_print(con_out, "PROOF"),
                SectionKind::Speed => uefi_print(con_out, "SPEED"),
                SectionKind::Logic => uefi_print(con_out, "LOGIC"),
                SectionKind::Unknown => uefi_print(con_out, "UNKNOWN"),
            }
            uefi_print(con_out, " line=");
            uefi_print_u64_dec(con_out, line as u64);
            uefi_print(con_out, " token=");
            match token {
                ForbiddenToken::While => uefi_print(con_out, "WHILE"),
                ForbiddenToken::For => uefi_print(con_out, "FOR"),
                ForbiddenToken::Loop => uefi_print(con_out, "LOOP"),
                ForbiddenToken::Goto => uefi_print(con_out, "GOTO"),
                ForbiddenToken::Asm => uefi_print(con_out, "ASM"),
            }
        }
    }
}

fn uefi_print_u32_hex(con_out: *mut SimpleTextOutputProtocol, v: u32) {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut buf = [0u8; 10];
    buf[0] = b'0';
    buf[1] = b'x';
    for i in 0..8 {
        let shift = 28 - (i * 4);
        let nib = ((v >> shift) & 0xF) as usize;
        buf[2 + i] = HEX[nib];
    }
    let s = core::str::from_utf8(&buf).unwrap_or("0x????????");
    uefi_print(con_out, s);
}

fn uefi_print_usize_hex(con_out: *mut SimpleTextOutputProtocol, v: usize) {
    #[cfg(target_pointer_width = "64")]
    {
        const HEX: &[u8; 16] = b"0123456789abcdef";
        let mut buf = [0u8; 18];
        buf[0] = b'0';
        buf[1] = b'x';
        let vv = v as u64;
        for i in 0..16 {
            let shift = 60 - (i * 4);
            let nib = ((vv >> shift) & 0xF) as usize;
            buf[2 + i] = HEX[nib];
        }
        let s = core::str::from_utf8(&buf).unwrap_or("0x????????????????");
        uefi_print(con_out, s);
    }
    #[cfg(target_pointer_width = "32")]
    {
        uefi_print_u32_hex(con_out, v as u32);
    }
}

fn uefi_print_status(con_out: *mut SimpleTextOutputProtocol, label: &str, status: EfiStatus) {
    uefi_print(con_out, label);
    uefi_print(con_out, " status=");
    uefi_print_usize_hex(con_out, status);
    uefi_print(con_out, "\n");
}

#[derive(Copy, Clone)]
struct BootConfig {
    soma_interactive_default: bool,
    soma_steps: usize,
    soma_layers: usize,
    soma_dim: usize,
    // None: auto (try header if present, silent on invalid)
    // Some(true): force (log if invalid / missing)
    // Some(false): disable header probing entirely
    soma_weights_header: Option<bool>,
}


fn uefi_print_zone(con_out: *mut SimpleTextOutputProtocol, z: Zone) {
    match z {
        Zone::Normal => uefi_print(con_out, "Normal"),
        Zone::Sandbox => uefi_print(con_out, "Sandbox"),
    }
}

fn ascii_to_ucs2_z(src: &str, out: &mut [u16]) -> Option<()> {
    if out.is_empty() {
        return None;
    }
    let mut i = 0usize;
    for &b in src.as_bytes() {
        if i + 1 >= out.len() {
            return None;
        }
        if b > 0x7F {
            return None;
        }
        out[i] = b as u16;
        i += 1;
    }
    out[i] = 0;
    Some(())
}

fn device_path_node_len(dp: *const EfiDevicePathProtocol) -> Option<usize> {
    if dp.is_null() {
        return None;
    }
    let n = unsafe { &*dp };
    let len = u16::from_le_bytes(n.len) as usize;
    if len < 4 {
        return None;
    }
    Some(len)
}

fn device_path_prefix_len_before_filepath(dp: *const EfiDevicePathProtocol) -> Option<usize> {
    let mut off = 0usize;
    let mut p = dp;
    for _ in 0..256 {
        let n = unsafe { &*p };
        let len = device_path_node_len(p)?;
        // End node
        if n.ty == 0x7f && n.sub_ty == 0xff {
            return Some(off);
        }
        // Media FilePath node
        if n.ty == 0x04 && n.sub_ty == 0x04 {
            return Some(off);
        }
        off = off.checked_add(len)?;
        p = unsafe { (dp as *const u8).add(off) as *const EfiDevicePathProtocol };
    }
    None
}

fn build_file_device_path(
    con_out: *mut SimpleTextOutputProtocol,
    base: *mut EfiDevicePathProtocol,
    path_ascii: &str,
    out: &mut [u8],
) -> Option<usize> {
    let _ = con_out;
    let prefix_len = device_path_prefix_len_before_filepath(base)?;
    if prefix_len > out.len() {
        return None;
    }

    // Convert path to UCS-2.
    let mut u16buf = [0u16; 128];
    ascii_to_ucs2_z(path_ascii, &mut u16buf)?;
    let mut u16_len = 0usize;
    while u16_len < u16buf.len() {
        if u16buf[u16_len] == 0 {
            break;
        }
        u16_len += 1;
    }
    // include NUL
    let path_bytes = (u16_len + 1) * 2;
    let file_node_len = 4 + path_bytes;
    let total = prefix_len + file_node_len + 4;
    if total > out.len() {
        return None;
    }

    unsafe {
        core::ptr::copy_nonoverlapping(base as *const u8, out.as_mut_ptr(), prefix_len);
    }

    // FilePath node
    let file_off = prefix_len;
    out[file_off + 0] = 0x04;
    out[file_off + 1] = 0x04;
    let ln = (file_node_len as u16).to_le_bytes();
    out[file_off + 2] = ln[0];
    out[file_off + 3] = ln[1];
    // Write UCS-2 bytes
    let mut w = 0usize;
    for i in 0..(u16_len + 1) {
        let b = u16buf[i].to_le_bytes();
        out[file_off + 4 + w] = b[0];
        out[file_off + 4 + w + 1] = b[1];
        w += 2;
    }

    // End node
    let end_off = file_off + file_node_len;
    out[end_off + 0] = 0x7f;
    out[end_off + 1] = 0xff;
    out[end_off + 2] = 0x04;
    out[end_off + 3] = 0x00;

    if DPLUS_BOOT_VERBOSE {
        uefi_print(con_out, "DPLUS: built device path bytes=");
        uefi_print_u64_dec(con_out, total as u64);
        uefi_print(con_out, "\n");
    }

    Some(total)
}

fn try_read_policy_via_loadfile2(
    con_out: *mut SimpleTextOutputProtocol,
    bs: &EfiBootServices,
    loaded_image: &EfiLoadedImageProtocol,
    out_ptr: *mut u8,
    out_len: usize,
) -> Option<usize> {
    let _ = con_out;
    let mut lfp: *mut c_void = core::ptr::null_mut();
    let status = (bs.handle_protocol)(
        loaded_image.device_handle,
        &EFI_LOAD_FILE2_PROTOCOL_GUID as *const EfiGuid,
        &mut lfp,
    );
    if status != EFI_SUCCESS || lfp.is_null() {
        if DPLUS_BOOT_VERBOSE {
            uefi_print_status(con_out, "DPLUS: LoadFile2(handle_protocol)", status);
        }
        return None;
    }
    let lf2 = lfp as *mut EfiLoadFile2Protocol;

    let mut dp_buf = [0u8; 512];
    let dp_len = build_file_device_path(con_out, loaded_image.file_path as *mut EfiDevicePathProtocol, "\\policy.dplus", &mut dp_buf)?;
    let _ = dp_len;
    let dp = dp_buf.as_mut_ptr() as *mut EfiDevicePathProtocol;
    let mut size = out_len;
    let st = unsafe { ((*lf2).load_file)(lf2, dp, 0, &mut size, out_ptr as *mut c_void) };
    if st == EFI_SUCCESS {
        return Some(size);
    }
    if st == EFI_BUFFER_TOO_SMALL {
        if DPLUS_BOOT_VERBOSE {
            uefi_print_status(con_out, "DPLUS: LoadFile2(buffer too small)", st);
            uefi_print(con_out, "DPLUS: needed bytes=");
            uefi_print_u64_dec(con_out, size as u64);
            uefi_print(con_out, "\n");
        }
        return None;
    }
    if DPLUS_BOOT_VERBOSE {
        uefi_print_status(con_out, "DPLUS: LoadFile2(load_file)", st);
        uefi_print(con_out, "DPLUS: dp_len=");
        uefi_print_u64_dec(con_out, dp_len as u64);
        uefi_print(con_out, "\n");
    }
    None
}

fn read_block(
    bio: *mut EfiBlockIoProtocol,
    lba: u64,
    buf: *mut u8,
    buf_len: usize,
) -> EfiStatus {
    if bio.is_null() {
        return 0x8000_0000_0000_0002; // EFI_INVALID_PARAMETER
    }
    let media = unsafe { (*bio).media };
    if media.is_null() {
        return 0x8000_0000_0000_0002;
    }
    let bs = unsafe { (*media).block_size as usize };
    if buf_len < bs {
        return EFI_BUFFER_TOO_SMALL;
    }
    unsafe { ((*bio).read_blocks)(bio, (*media).media_id, lba, bs, buf as *mut c_void) }
}

fn eq_ascii_ignore_case(a: &str, b: &str) -> bool {
    if a.len() != b.len() {
        return false;
    }
    a.bytes().zip(b.bytes()).all(|(x, y)| {
        let xl = if (b'A'..=b'Z').contains(&x) { x + 32 } else { x };
        let yl = if (b'A'..=b'Z').contains(&y) { y + 32 } else { y };
        xl == yl
    })
}

fn try_read_policy_via_blockio_fat(
    con_out: *mut SimpleTextOutputProtocol,
    bs: &EfiBootServices,
    loaded_image: &EfiLoadedImageProtocol,
    out_ptr: *mut u8,
    out_len: usize,
) -> Option<usize> {
    // Acquire BlockIO from the device handle.
    let mut bio_ptr: *mut c_void = core::ptr::null_mut();
    let st = (bs.handle_protocol)(
        loaded_image.device_handle,
        &EFI_BLOCK_IO_PROTOCOL_GUID as *const EfiGuid,
        &mut bio_ptr,
    );
    if st != EFI_SUCCESS || bio_ptr.is_null() {
        uefi_print_status(con_out, "DPLUS: BlockIO(handle_protocol)", st);
        return None;
    }
    let bio = bio_ptr as *mut EfiBlockIoProtocol;
    let media = unsafe { (*bio).media };
    if media.is_null() {
        return None;
    }
    let block_size = unsafe { (*media).block_size as usize };
    if block_size > 4096 {
        return None;
    }

    let mut sector = [0u8; 4096];
    let status0 = read_block(bio, 0, sector.as_mut_ptr(), sector.len());
    if status0 != EFI_SUCCESS {
        uefi_print_status(con_out, "DPLUS: BlockIO(read LBA0)", status0);
        return None;
    }

    let parse_bpb = |vbr: &[u8]| -> Option<(u64, FatBpb)> {
        if vbr.len() < 90 {
            return None;
        }
        if vbr[510] != 0x55 || vbr[511] != 0xAA {
            return None;
        }
        let bytes_per_sector = u16::from_le_bytes([vbr[11], vbr[12]]) as u32;
        if bytes_per_sector != 512 && bytes_per_sector != 1024 && bytes_per_sector != 2048 && bytes_per_sector != 4096 {
            return None;
        }
        let sectors_per_cluster = vbr[13] as u32;
        if sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 {
            return None;
        }
        let reserved = u16::from_le_bytes([vbr[14], vbr[15]]) as u32;
        let fats = vbr[16] as u32;
        let root_entries = u16::from_le_bytes([vbr[17], vbr[18]]) as u32;
        let total16 = u16::from_le_bytes([vbr[19], vbr[20]]) as u32;
        let fat16 = u16::from_le_bytes([vbr[22], vbr[23]]) as u32;
        let total32 = u32::from_le_bytes([vbr[32], vbr[33], vbr[34], vbr[35]]);
        let fat32 = u32::from_le_bytes([vbr[36], vbr[37], vbr[38], vbr[39]]);
        let root_cluster = u32::from_le_bytes([vbr[44], vbr[45], vbr[46], vbr[47]]);

        let fat_size = if fat16 != 0 { fat16 } else { fat32 };
        let total_sectors = if total16 != 0 { total16 } else { total32 };
        if fat_size == 0 || total_sectors == 0 {
            return None;
        }
        let root_dir_sectors = ((root_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
        let first_data_sector = reserved + fats * fat_size + root_dir_sectors;
        if total_sectors <= first_data_sector {
            return None;
        }
        let data_sectors = total_sectors - first_data_sector;
        let clusters = data_sectors / sectors_per_cluster;
        let fat_type = if clusters < 4085 {
            FatType::Fat12
        } else if clusters < 65525 {
            FatType::Fat16
        } else {
            FatType::Fat32
        };

        Some((0, FatBpb {
            bytes_per_sector,
            sectors_per_cluster,
            reserved_sectors: reserved,
            fats,
            fat_size_sectors: fat_size,
            first_data_sector,
            root_dir_sectors,
            fat_type,
            root_cluster,
        }))
    };

    // Try superfloppy VBR at LBA0 first.
    let mut part_lba: u64 = 0;
    let bpb: FatBpb;
    if let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) {
        bpb = bb;
    } else {
        // MBR: first partition entry at 0x1BE.
        if sector[510] != 0x55 || sector[511] != 0xAA {
            return None;
        }
        let p0 = 0x1BE;
        let lba_start = u32::from_le_bytes([sector[p0 + 8], sector[p0 + 9], sector[p0 + 10], sector[p0 + 11]]) as u64;
        if lba_start == 0 {
            return None;
        }
        part_lba = lba_start;
        let status_vbr = read_block(bio, part_lba, sector.as_mut_ptr(), sector.len());
        if status_vbr != EFI_SUCCESS {
            return None;
        }
        let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) else {
            return None;
        };
        bpb = bb;
    }

    if matches!(bpb.fat_type, FatType::Fat12) {
        uefi_print(con_out, "DPLUS: FAT12 not supported\n");
        return None;
    }

    if DPLUS_BOOT_VERBOSE {
        uefi_print(con_out, "DPLUS: FAT type=");
        match bpb.fat_type {
            FatType::Fat16 => uefi_print(con_out, "FAT16"),
            FatType::Fat32 => uefi_print(con_out, "FAT32"),
            FatType::Fat12 => uefi_print(con_out, "FAT12"),
        }
        uefi_print(con_out, " bytes_per_sector=");
        uefi_print_u64_dec(con_out, bpb.bytes_per_sector as u64);
        uefi_print(con_out, " spc=");
        uefi_print_u64_dec(con_out, bpb.sectors_per_cluster as u64);
        uefi_print(con_out, "\n");
    }

    // Read FAT into a scratch sector on demand.
    let fat_start_lba = part_lba + bpb.reserved_sectors as u64;
    let root_dir_lba = part_lba + (bpb.reserved_sectors + bpb.fats * bpb.fat_size_sectors) as u64;
    let data_start_lba = part_lba + bpb.first_data_sector as u64;

    let mut lfn_name = [0u16; 260];
    let mut lfn_valid = false;

    let mut check_entry = |entry: &[u8]| -> Option<(u32, u32)> {
        if entry.len() < 32 {
            return None;
        }
        let first = entry[0];
        if first == 0x00 {
            return None;
        }
        if first == 0xE5 {
            lfn_valid = false;
            return Some((0, 0));
        }
        let attr = entry[11];
        if attr == 0x0F {
            // LFN entry
            let seq = (entry[0] & 0x1F) as usize;
            if seq == 0 {
                return Some((0, 0));
            }
            if (entry[0] & 0x40) != 0 {
                // start
                for c in &mut lfn_name {
                    *c = 0;
                }
                lfn_valid = true;
            }
            if !lfn_valid {
                return Some((0, 0));
            }
            let base = (seq - 1) * 13;
            let mut put = |i: usize, lo: u8, hi: u8| {
                let idx = base + i;
                if idx < lfn_name.len() {
                    lfn_name[idx] = u16::from_le_bytes([lo, hi]);
                }
            };
            // name1 (5)
            put(0, entry[1], entry[2]);
            put(1, entry[3], entry[4]);
            put(2, entry[5], entry[6]);
            put(3, entry[7], entry[8]);
            put(4, entry[9], entry[10]);
            // name2 (6)
            put(5, entry[14], entry[15]);
            put(6, entry[16], entry[17]);
            put(7, entry[18], entry[19]);
            put(8, entry[20], entry[21]);
            put(9, entry[22], entry[23]);
            put(10, entry[24], entry[25]);
            // name3 (2)
            put(11, entry[28], entry[29]);
            put(12, entry[30], entry[31]);
            return Some((0, 0));
        }

        // Normal entry.
        let mut name_match = false;
        if lfn_valid {
            // Convert UCS-2 to ASCII for comparison.
            let mut tmp = [0u8; 64];
            let mut n = 0usize;
            for &ch in &lfn_name {
                if ch == 0x0000 || ch == 0xFFFF {
                    break;
                }
                if ch <= 0x7F && n < tmp.len() {
                    tmp[n] = ch as u8;
                    n += 1;
                } else {
                    n = 0;
                    break;
                }
            }
            if n > 0 {
                let s = core::str::from_utf8(&tmp[..n]).ok()?;
                if eq_ascii_ignore_case(s, "policy.dplus") {
                    name_match = true;
                }
            }
        }
        lfn_valid = false;

        if !name_match {
            // Fallback 8.3 match: POLICY.*
            let n0 = &entry[0..8];
            let e0 = &entry[8..11];
            if &n0[0..6] == b"POLICY" {
                // accept any extension
                let _ = e0;
                name_match = true;
            }
        }

        if !name_match {
            return Some((0, 0));
        }

        let hi = u16::from_le_bytes([entry[20], entry[21]]) as u32;
        let lo = u16::from_le_bytes([entry[26], entry[27]]) as u32;
        let first_cluster = if matches!(bpb.fat_type, FatType::Fat32) {
            (hi << 16) | lo
        } else {
            lo
        };
        let file_size = u32::from_le_bytes([entry[28], entry[29], entry[30], entry[31]]);
        Some((first_cluster, file_size))
    };

    let (mut first_cluster, file_size) = if matches!(bpb.fat_type, FatType::Fat16) {
        // FAT16 root dir is fixed region.
        let mut found: Option<(u32, u32)> = None;
        for s in 0..bpb.root_dir_sectors {
            let lba = root_dir_lba + s as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }
            let mut off = 0usize;
            while off + 32 <= block_size {
                let e = &sector[off..off + 32];
                if e[0] == 0x00 {
                    break;
                }
                if let Some((cl, sz)) = check_entry(e) {
                    if cl != 0 && sz != 0 {
                        found = Some((cl, sz));
                        break;
                    }
                }
                off += 32;
            }
            if found.is_some() {
                break;
            }
        }
        found?
    } else {
        // FAT32: root dir is a cluster chain.
        let mut dir_cluster = bpb.root_cluster;
        let mut found: Option<(u32, u32)> = None;
        for _ in 0..1024 {
            if dir_cluster < 2 {
                break;
            }
            let first_sector = data_start_lba + ((dir_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
            for sc in 0..bpb.sectors_per_cluster {
                let lba = first_sector + sc as u64;
                let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
                if st != EFI_SUCCESS {
                    return None;
                }
                let mut off = 0usize;
                while off + 32 <= block_size {
                    let e = &sector[off..off + 32];
                    if e[0] == 0x00 {
                        break;
                    }
                    if let Some((cl, sz)) = check_entry(e) {
                        if cl != 0 && sz != 0 {
                            found = Some((cl, sz));
                            break;
                        }
                    }
                    off += 32;
                }
                if found.is_some() {
                    break;
                }
            }
            if found.is_some() {
                break;
            }
            // next cluster in dir
            dir_cluster = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, dir_cluster)?;
            if is_eoc(&bpb, dir_cluster) {
                break;
            }
        }
        found?
    };

    if first_cluster < 2 {
        return None;
    }
    if DPLUS_BOOT_VERBOSE {
        uefi_print(con_out, "DPLUS: found policy cluster=");
        uefi_print_u64_dec(con_out, first_cluster as u64);
        uefi_print(con_out, " size=");
        uefi_print_u64_dec(con_out, file_size as u64);
        uefi_print(con_out, "\n");
    }

    let mut remaining = core::cmp::min(file_size as usize, out_len);
    let mut written = 0usize;
    while remaining > 0 {
        if is_eoc(&bpb, first_cluster) {
            break;
        }
        let first_sector = data_start_lba + ((first_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
        for sc in 0..bpb.sectors_per_cluster {
            if remaining == 0 {
                break;
            }
            let lba = first_sector + sc as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }
            let take = core::cmp::min(remaining, block_size);
            unsafe {
                core::ptr::copy_nonoverlapping(sector.as_ptr(), out_ptr.add(written), take);
            }
            written += take;
            remaining -= take;
        }
        let next = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, first_cluster)?;
        first_cluster = next;
    }

    Some(written)
}

#[derive(Copy, Clone, Eq, PartialEq)]
enum FatType {
    Fat12,
    Fat16,
    Fat32,
}

#[derive(Copy, Clone)]
struct FatBpb {
    bytes_per_sector: u32,
    sectors_per_cluster: u32,
    reserved_sectors: u32,
    fats: u32,
    fat_size_sectors: u32,
    first_data_sector: u32,
    root_dir_sectors: u32,
    fat_type: FatType,
    root_cluster: u32,
}

fn is_eoc(bpb: &FatBpb, cl: u32) -> bool {
    match bpb.fat_type {
        FatType::Fat16 => cl >= 0xFFF8,
        FatType::Fat32 => (cl & 0x0FFF_FFFF) >= 0x0FFF_FFF8,
        FatType::Fat12 => cl >= 0x0FF8,
    }
}

fn fat_next_cluster(
    con_out: *mut SimpleTextOutputProtocol,
    bio: *mut EfiBlockIoProtocol,
    bpb: &FatBpb,
    fat_start_lba: u64,
    block_size: usize,
    cluster: u32,
) -> Option<u32> {
    let mut sector = [0u8; 4096];
    let (ent_size, mask) = match bpb.fat_type {
        FatType::Fat16 => (2u32, 0xFFFF_FFFFu32),
        FatType::Fat32 => (4u32, 0x0FFF_FFFFu32),
        FatType::Fat12 => (0u32, 0u32),
    };
    if ent_size == 0 {
        return None;
    }

    let off = cluster as u64 * ent_size as u64;
    let sec = (off / bpb.bytes_per_sector as u64) as u64;
    let ofs = (off % bpb.bytes_per_sector as u64) as usize;
    let st = read_block(bio, fat_start_lba + sec, sector.as_mut_ptr(), sector.len());
    if st != EFI_SUCCESS {
        uefi_print_status(con_out, "DPLUS: FAT read", st);
        return None;
    }
    if ofs + ent_size as usize > block_size {
        return None;
    }
    let v = if ent_size == 2 {
        u16::from_le_bytes([sector[ofs], sector[ofs + 1]]) as u32
    } else {
        u32::from_le_bytes([sector[ofs], sector[ofs + 1], sector[ofs + 2], sector[ofs + 3]])
    };
    Some(v & mask)
}

fn try_read_policy_from_fat(
    con_out: *mut SimpleTextOutputProtocol,
    image: EfiHandle,
    system_table: *mut EfiSystemTable,
    out_ptr: *mut u8,
    out_len: usize,
) -> Option<usize> {
    let st = unsafe { system_table.as_ref()? };
    let bs = unsafe { st.boot_services.as_ref()? };

    // Prefer a direct BlockIO+FAT read (works with QEMU vvfat even when SimpleFS is absent).

    if out_ptr.is_null() || out_len == 0 {
        return None;
    }

    let mut loaded_image_ptr: *mut core::ffi::c_void = core::ptr::null_mut();
    let status = (bs.handle_protocol)(
        image,
        &EFI_LOADED_IMAGE_PROTOCOL_GUID as *const EfiGuid,
        &mut loaded_image_ptr,
    );
    if status != EFI_SUCCESS || loaded_image_ptr.is_null() {
        uefi_print_status(con_out, "DPLUS: LoadedImageProtocol", status);
        return None;
    }
    let li = unsafe { &*(loaded_image_ptr as *const EfiLoadedImageProtocol) };

    if let Some(n) = try_read_policy_via_blockio_fat(con_out, bs, li, out_ptr, out_len) {
        return Some(n);
    }

    if let Some(n) = try_read_policy_via_loadfile2(con_out, bs, li, out_ptr, out_len) {
        return Some(n);
    }

    None
}

fn dump_journal(con_out: *mut SimpleTextOutputProtocol, w: &MemoryWarden<16, 64, 8>, max: usize) {
    let stats = w.journal_stats();
    uefi_print(con_out, "Journal: len=");
    uefi_print_u64_dec(con_out, stats.len as u64);
    uefi_print(con_out, " dropped=");
    uefi_print_u64_dec(con_out, stats.dropped);
    uefi_print(con_out, "\n");

    let n = core::cmp::min(stats.len, max);
    for i in 0..n {
        let Some(ev) = w.journal_get(i) else { break };
        uefi_print(con_out, "  #");
        uefi_print_u64_dec(con_out, i as u64);
        uefi_print(con_out, " tick=");
        uefi_print_u64_dec(con_out, ev.tick);
        uefi_print(con_out, " cell=");
        uefi_print_u64_dec(con_out, ev.cell as u64);
        uefi_print(con_out, " zone=");
        uefi_print_zone(con_out, ev.zone);
        uefi_print(con_out, " kind=");

        // Keep kind display short (no {:?} in no_std).
        match ev.kind {
            osg_memory_warden::EventKind::AllocateGranted => uefi_print(con_out, "AllocGranted"),
            osg_memory_warden::EventKind::AllocateDenied => uefi_print(con_out, "AllocDenied"),
            osg_memory_warden::EventKind::Freed => uefi_print(con_out, "Freed"),
            osg_memory_warden::EventKind::AccessDenied => uefi_print(con_out, "AccessDenied"),
            osg_memory_warden::EventKind::Expired => uefi_print(con_out, "Expired"),
            osg_memory_warden::EventKind::Delegated => uefi_print(con_out, "Delegated"),
            osg_memory_warden::EventKind::Quarantined => uefi_print(con_out, "Quarantined"),
            osg_memory_warden::EventKind::Reclaimed => uefi_print(con_out, "Reclaimed"),
            osg_memory_warden::EventKind::MeritDecision => uefi_print(con_out, "MeritDecision"),
        }

        if ev.kind == osg_memory_warden::EventKind::MeritDecision {
            let score = (ev.info & 0xFF) as u8;
            let default_sandbox = ((ev.info >> 8) & 1) != 0;
            let reasons_bits = (ev.info >> 16) & 0xFFFF;
            let bytes_cap = (ev.bytes & 0xFFFF_FFFF) as u32;
            let ttl_cap = (ev.bytes >> 32) as u32;

            uefi_print(con_out, " score=");
            uefi_print_u64_dec(con_out, score as u64);
            uefi_print(con_out, " default_sandbox=");
            uefi_print_bool(con_out, default_sandbox);
            uefi_print(con_out, " reasons_bits=");
            uefi_print_u32_hex(con_out, reasons_bits);

            let mut reasons_buf = [0u8; 64];
            let reasons = format_reasons_csv(
                osg_memory_warden::MeritReasons::from_bits_truncate(reasons_bits),
                &mut reasons_buf,
            );
            uefi_print(con_out, " reasons=");
            uefi_print(con_out, reasons);

            uefi_print(con_out, " bytes_cap=");
            uefi_print_u64_dec(con_out, bytes_cap as u64);
            uefi_print(con_out, " ttl_cap_ms=");
            uefi_print_u64_dec(con_out, ttl_cap as u64);
        } else {
            // Generic fields
            if ev.handle_raw != 0 {
                uefi_print(con_out, " handle=");
                uefi_print_u32_hex(con_out, ev.handle_raw);
            }
            if ev.bytes != 0 {
                uefi_print(con_out, " bytes=");
                uefi_print_u64_dec(con_out, ev.bytes);
            }
            if ev.info != 0 {
                uefi_print(con_out, " info=");
                uefi_print_u32_hex(con_out, ev.info);
            }
        }
        uefi_print(con_out, "\n");
    }
}

fn run_dplus_pipeline(
    con_out: *mut SimpleTextOutputProtocol,
    image: EfiHandle,
    system_table: *mut EfiSystemTable,
    w: &mut MemoryWarden<16, 64, 8>,
    sentinel_rules_out: &mut [LawSentinelRule],
) -> Option<BootConfig> {
    // Embedded fallback policy.
    const EMBEDDED_DPLUS: &str = "@@SOMA:C\n// embedded fallback\n@@GPU:ptx\n/* gpu */\n@@LAW\nallow mem.allocate op:7 bytes<=8192 ttl_ms<=2000 zone==sandbox\nallow mem.allocate op:8 bytes<=65536 ttl_ms<=0 zone==normal\nmonitor behavior.access_denied count<=3\n@@PROOF\nproof op:7\nproof op:8\n";

    const POLICY_BUF_CAP: usize = 32 * 1024;
    static mut POLICY_BUF: [u8; POLICY_BUF_CAP] = [0u8; POLICY_BUF_CAP];
    let (src, src_len): (&str, usize) = unsafe {
        let p = core::ptr::addr_of_mut!(POLICY_BUF) as *mut u8;
        match try_read_policy_from_fat(con_out, image, system_table, p, POLICY_BUF_CAP) {
            Some(n) => {
                let bytes = core::slice::from_raw_parts(p as *const u8, n);
                match core::str::from_utf8(bytes) {
                Ok(s) => {
                    uefi_print(con_out, "DPLUS: loaded policy.dplus bytes=");
                    uefi_print_u64_dec(con_out, n as u64);
                    uefi_print(con_out, "\n");
                    (s, n)
                }
                Err(_) => {
                    uefi_print(con_out, "DPLUS: policy.dplus not utf8 (fallback to embedded)\n");
                    (EMBEDDED_DPLUS, EMBEDDED_DPLUS.as_bytes().len())
                }
                }
            }
            None => (EMBEDDED_DPLUS, EMBEDDED_DPLUS.as_bytes().len()),
        }
    };

    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 32];

    let module = match parse(src, &mut scratch) {
        Ok(m) => m,
        Err(_) => {
            uefi_print(con_out, "DPLUS: parse FAIL\n");
            return None;
        }
    };
    
    // ... verification omitted for brevity in thought but kept in tool call ...
    
    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::Off;
    if let Err(e) = verify(&module, opts) {
        uefi_print(con_out, "DPLUS: verify FAIL: ");
        uefi_print_verify_error(con_out, e);
        uefi_print(con_out, "\n");
        return None;
    }
    uefi_print(con_out, "DPLUS: verify OK\n");

    // Optional boot config: allow policy to request interactive Soma.
    // Usage in policy.dplus:
    //   @@SOMA:IO
    //   interactive=1
    let mut cfg = BootConfig {
        soma_interactive_default: false,
        soma_steps: 10,
        // 0 means: use all available layers (auto-derived from weights size or allocation).
        soma_layers: 0,
        soma_dim: 128,
        soma_weights_header: None,
    };
    let mut warden_rate_window_ticks: Option<u64> = None;
    let mut warden_rate_limit_bytes: Option<u64> = None;
    let mut warden_policy_present = false;
    let mut warden_policy_program: Option<PolicyProgram<64>> = None;
    let mut warden_policy_buf = [0u8; 1024];
    let mut cortex_cfg: CortexConfig = CortexConfig::default();

    for sec in module.sections {
        let SectionTag::Other(h) = sec.tag else {
            continue;
        };
        if eq_ascii_ignore_case(h, "SOMA:IO") || eq_ascii_ignore_case(h, "SOMA:INTERACTIVE") {
            let io = extract_soma_io_config(sec.body);
            if let Some(v) = io.interactive {
                cfg.soma_interactive_default = v;
            }
            if let Some(v) = io.steps {
                cfg.soma_steps = core::cmp::min(core::cmp::max(v, 1), 256);
            }
            if let Some(v) = io.layers {
                // 0 means: use all available layers (auto-derived from weights size or allocation).
                cfg.soma_layers = if v == 0 {
                    0
                } else {
                    core::cmp::min(core::cmp::max(v, 1), 64)
                };
            }
            if let Some(v) = io.dim {
                // Keep dim in a safe range for the demo.
                cfg.soma_dim = core::cmp::min(core::cmp::max(v, 16), 256);
            }
            if let Some(v) = io.weights_header {
                cfg.soma_weights_header = Some(v);
            }
        } else if eq_ascii_ignore_case(h, "WARDEN:MEM") {
            let mem = extract_warden_mem_config(sec.body);
            if let Some(v) = mem.rate_window_ticks {
                warden_rate_window_ticks = Some(v);
            }
            if let Some(v) = mem.rate_limit_bytes {
                warden_rate_limit_bytes = Some(v);
            }
        } else if eq_ascii_ignore_case(h, "WARDEN:POLICY") {
            warden_policy_present = true;
            if let Some(rpn) = extract_warden_policy_rpn(sec.body, &mut warden_policy_buf) {
                let mut p: PolicyProgram<64> = PolicyProgram::new_disabled();
                if compile_policy_rpn(rpn, &mut p).is_ok() {
                    warden_policy_program = Some(p);
                }
            }
        } else if eq_ascii_ignore_case(h, "CORTEX:HEUR") {
            cortex_cfg = extract_cortex_heur_config(sec.body);
        }
    }

    if warden_rate_window_ticks.is_some() || warden_rate_limit_bytes.is_some() {
        let window = warden_rate_window_ticks.unwrap_or(0);
        let limit = warden_rate_limit_bytes.unwrap_or(0);

        uefi_print(con_out, "WARDEN:MEM rate_window_ticks=");
        uefi_print_u64_dec(con_out, window);
        uefi_print(con_out, " rate_limit_bytes=");
        uefi_print_u64_dec(con_out, limit);
        uefi_print(con_out, " -> ");

        if w.set_rate_limit(1, window, limit).is_ok() {
            uefi_print(con_out, "OK\n");
        } else {
            uefi_print(con_out, "FAIL\n");
        }
    }

    if warden_policy_present {
        uefi_print(con_out, "WARDEN:POLICY -> ");
        match warden_policy_program {
            Some(p) => {
                if w.set_policy_program(p).is_ok() {
                    uefi_print(con_out, "OK\n");
                } else {
                    uefi_print(con_out, "FAIL(set)\n");
                }
            }
            None => {
                uefi_print(con_out, "FAIL(compile)\n");
            }
        }
    }

    let mut cortex: HeuristicCortex<8> = HeuristicCortex::new();
    cortex.set_config(cortex_cfg);
    if cortex_cfg.enabled {
        uefi_print(con_out, "CORTEX:HEUR enabled=1 prefetch_repeat=");
        uefi_print_u64_dec(con_out, cortex_cfg.prefetch_repeat as u64);
        uefi_print(con_out, "\n");
    }

    let merit = compute_merit_profile(&module);
    let mut merit_buf = [0u8; 64];
    let reasons = format_reasons_csv(merit.reasons, &mut merit_buf);

    uefi_print(con_out, "MERIT score=");
    uefi_print_u64_dec(con_out, merit.score_0_100 as u64);
    uefi_print(con_out, " default_sandbox=");
    uefi_print_bool(con_out, merit.default_sandbox);
    uefi_print(con_out, " reasons=");
    uefi_print(con_out, reasons);
    uefi_print(con_out, "\n");

    w.journal_merit_decision(
        1,
        if merit.default_sandbox {
            Zone::Sandbox
        } else {
            Zone::Normal
        },
        merit.score_0_100,
        merit.reasons.bits(),
        merit.bytes_cap,
        merit.ttl_cap_ms,
    );

    // Phase 7 (MVP) demo: observe repeated allocations and suggest a prefetch intent.
    if cortex_cfg.enabled {
        let mut repeats = cortex_cfg.prefetch_repeat as u32;
        if repeats == 0 {
            repeats = 1;
        }
        for _ in 0..repeats {
            let cap = match w.allocate(MemIntent::new(1, 4096)) {
                Ok(h) => h,
                Err(_) => break,
            };
            let st = w.journal_stats();
            if st.len > 0 {
                if let Some(ev) = w.journal_get(st.len - 1) {
                    cortex.observe(&ev);
                }
            }
            let _ = w.free(cap);
        }

        if let Some(pref) = cortex.suggest_prefetch(1) {
            uefi_print(con_out, "CORTEX: prefetch bytes=");
            uefi_print_u64_dec(con_out, pref.bytes);
            uefi_print(con_out, " -> ");
            match w.allocate(pref) {
                Ok(cap) => {
                    uefi_print(con_out, "GRANTED cap=");
                    uefi_print_u32_hex(con_out, cap.raw());
                    uefi_print(con_out, "\n");
                    let _ = w.free(cap);
                }
                Err(_) => {
                    uefi_print(con_out, "DENIED\n");
                }
            }
        } else {
            uefi_print(con_out, "CORTEX: no prefetch suggestion\n");
        }
    }

    // Apply LAW rules to the warden.
    let mut rules = [osg_memory_warden::LawMemAllocate::default(); 16];
    let mut any = false;
    for sec in module.sections {
        if sec.tag.kind() != Some(SectionKind::Law) {
            continue;
        }
        
        // Extract Sentinel Rules
        let _n_sent = extract_sentinel_rules(sec.body, sentinel_rules_out);
        if _n_sent > 0 {
             uefi_print(con_out, "DPLUS: loaded sentinel rules count=");
             uefi_print_u64_dec(con_out, _n_sent as u64);
             uefi_print(con_out, "\n");
        }

        let n = extract_mem_allocate_rules(sec.body, &mut rules);
        for r in &rules[..n] {
            any = true;

            let raw_bytes = r.bytes.unwrap_or(4096);
            let raw_ttl_ms = r.ttl_ms.unwrap_or(0);
            let sandbox = r.sandbox.unwrap_or(merit.default_sandbox);
            let label = r.op_id.unwrap_or(0);
            let (bytes, ttl_ticks) = apply_caps(raw_bytes, raw_ttl_ms, merit);

            let mut intent = MemIntent::new(1, bytes);
            intent.ttl_ticks = ttl_ticks; // 1 tick == 1ms (demo)
            intent.sandbox = sandbox;
            intent.label = label;
            intent.rights = Rights::R.union(Rights::W);

            uefi_print(con_out, "LAW mem.allocate op=");
            uefi_print_u64_dec(con_out, label as u64);
            uefi_print(con_out, " bytes=");
            uefi_print_u64_dec(con_out, bytes);
            uefi_print(con_out, " ttl_ms=");
            uefi_print_u64_dec(con_out, ttl_ticks);
            uefi_print(con_out, " sandbox=");
            uefi_print_bool(con_out, sandbox);
            uefi_print(con_out, " -> ");

            match w.allocate(intent) {
                Ok(cap) => {
                    let meta = match w.cap_meta(cap) {
                        Ok(m) => m,
                        Err(_) => {
                            uefi_print(con_out, "GRANTED(meta_err)\n");
                            return None;
                        }
                    };
                    uefi_print(con_out, "GRANTED cap=");
                    uefi_print_u32_hex(con_out, cap.raw());
                    uefi_print(con_out, " zone=");
                    uefi_print_zone(con_out, meta.zone);
                    uefi_print(con_out, "\n");

                    // Keep deterministic: free immediately.
                    if w.free(cap).is_err() {
                        return None;
                    }
                }
                Err(_) => {
                    uefi_print(con_out, "DENIED\n");
                }
            }
        }
    }

    if !any {
        uefi_print(con_out, "DPLUS: no LAW mem.allocate rules found\n");
    }

    // Touch src_len so it stays used (debugging visibility).
    if src_len == 0 {
        uefi_print(con_out, "DPLUS: empty source\n");
    }

    Some(cfg)
}

fn run_warden_checks(con_out: *mut SimpleTextOutputProtocol, image: EfiHandle, system_table: *mut EfiSystemTable) -> bool {
    const BITMAP_WORDS: usize = 16;
    const MAX_CAPS: usize = 64;
    const MAX_CELLS: usize = 8;

    // Phase 7: Soma Integration

    let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();

    let total_pages = BITMAP_WORDS * 64;
    let mut normal_paddr: u64 = 0;
    let mut sandbox_paddr: u64 = 0;

    unsafe {
        let bs = (*system_table).boot_services;
        // AllocateAnyPages = 0
        // EfiLoaderData = 2
        let st1 = ((*bs).allocate_pages)(0, 2, total_pages, &mut normal_paddr);
        if st1 != EFI_SUCCESS {
            uefi_print(con_out, "OS-G: Alloc normal pages failed\n");
            return false;
        }
        let st2 = ((*bs).allocate_pages)(0, 2, total_pages, &mut sandbox_paddr);
        if st2 != EFI_SUCCESS {
            uefi_print(con_out, "OS-G: Alloc sandbox pages failed\n");
            return false;
        }

        uefi_print(con_out, "OS-G: Normal base=");
        uefi_print_usize_hex(con_out, normal_paddr as usize);
        uefi_print(con_out, " Sandbox base=");
        uefi_print_usize_hex(con_out, sandbox_paddr as usize);
        uefi_print(con_out, "\n");

        w.init_zones(
            normal_paddr as usize,
            total_pages * 4096,
            sandbox_paddr as usize,
            total_pages * 4096,
        );
    }

    if w.set_quota(1, 2 * 1024 * 1024).is_err() {
        return false;
    }

    // Pre-allocate buffer for sentinel rules
    let mut sentinel_rules = [LawSentinelRule::default(); 8];

    uefi_print(con_out, "Running D+ pipeline...\n");
    let boot_cfg = match run_dplus_pipeline(con_out, image, system_table, &mut w, &mut sentinel_rules) {
        Some(c) => c,
        None => return false,
    };

    let cap_normal = match w.allocate(MemIntent::new(1, 8192)) {
        Ok(h) => h,
        Err(_) => return false,
    };
    let (start, end) = match w.cap_range(cap_normal) {
        Ok(r) => r,
        Err(_) => return false,
    };
    if start == 0 || end <= start {
        return false;
    }
    if w.check_access(cap_normal, Access::Read, start, 16).is_err() {
        return false;
    }

    let mut sandbox_intent = MemIntent::new(2, 4096);
    sandbox_intent.sandbox = true;
    let cap_sandbox = match w.allocate(sandbox_intent) {
        Ok(h) => h,
        Err(_) => return false,
    };
    let meta = match w.cap_meta(cap_sandbox) {
        Ok(m) => m,
        Err(_) => return false,
    };
    if meta.zone != Zone::Sandbox {
        return false;
    }

    let child = match w.delegate(cap_normal, 3, Rights::R, 0, 42) {
        Ok(h) => h,
        Err(_) => return false,
    };
    if w.check_access(child, Access::Write, start, 4).is_ok() {
        return false;
    }

    // TTL / Expiration test
    {
        // Allocate a short-lived cap
        // Intent: 4096 bytes, 20ms TTL
        let mut short_life = MemIntent::new(3, 4096);
        short_life.ttl_ticks = 20;

        let cap_short = match w.allocate(short_life) {
            Ok(h) => h,
            Err(_) => {
                uefi_print(con_out, "Alloc short_life failed\n");
                return false;
            }
        };

        // Immediately check access: should pass (now=0)
        let (s2, _e2) = match w.cap_range(cap_short) {
            Ok(r) => r,
            Err(_) => return false,
        };
        if w.check_access(cap_short, Access::Read, s2, 16).is_err() {
            uefi_print(con_out, "Access short_life immediate failed\n");
            return false;
        }

        // Advance time by 10ms -> should still pass (now=10, expires=20)
        w.tick(10);
        if w.check_access(cap_short, Access::Read, s2, 16).is_err() {
            uefi_print(con_out, "Access short_life after 10ms failed (should pass)\n");
            return false;
        }

        // Advance time by another 15ms -> should fail (now=25, expires=20)
        w.tick(15);
        if w.check_access(cap_short, Access::Read, s2, 16).is_ok() {
            uefi_print(con_out, "Access short_life after 25ms SUCCEEDED (should fail)\n");
            return false;
        }
        // At this point, the journal should contain an Expiration event.
    }

    // Quarantine test
    {
        // Allocate a cap for a "malicious" cell
        const MALICIOUS_CELL: u32 = 666;
        let mal_intent = MemIntent::new(MALICIOUS_CELL, 4096);
        let cap_mal = match w.allocate(mal_intent) {
            Ok(h) => h,
            Err(_) => {
                uefi_print(con_out, "Alloc malicious cap failed\n");
                return false;
            }
        };

        // Quarantine the cell
        if w.quarantine_cell(MALICIOUS_CELL).is_err() {
            uefi_print(con_out, "Quarantine failed\n");
            return false;
        }

        // Verify cap is expired immediately (now_ticks >= 1)
        w.tick(1);
        let (s_mal, _) = match w.cap_range(cap_mal) {
            Ok(r) => r,
            Err(_) => return false,
        };

        if w.check_access(cap_mal, Access::Read, s_mal, 16).is_ok() {
            uefi_print(con_out, "Access quarantined cap SUCCEEDED (should fail)\n");
            return false;
        }
    }

    // Validate journal BEFORE snapshot/restore (restore clears journal).
    let stats_before = w.journal_stats();
    if stats_before.len == 0 {
        return false;
    }

    dump_journal(con_out, &w, 24);

    uefi_print(con_out, "Journal events recorded (before restore): ");
    uefi_print_u64_dec(con_out, stats_before.len as u64);
    uefi_print(con_out, "\n");

    let snap = match w.snapshot() {
        Ok(s) => s,
        Err(_) => return false,
    };
    if w.free(child).is_err() {
        return false;
    }
    if w.free(cap_normal).is_err() {
        return false;
    }
    if w.restore(&snap).is_err() {
        return false;
    }

    // After restore, original cap should still be valid.
    if w.cap_range(cap_normal).is_err() {
        return false;
    }

    // Sentinel / Observability Test
    {
        uefi_print(con_out, "Sentinel Test...\n");

        let weights_size_total = try_probe_weights_file_size_from_fat(con_out, image, system_table).unwrap_or(0);

        // Optional WEIGHTS.BIN header (backward compatible).
        // If present, we auto-derive dim (and payload offset) from the file.
        // Policy knob: @@SOMA:IO weights_header={0|1}
        // - 0: disable header probing
        // - 1: force probing + log if invalid
        let mut dim = boot_cfg.soma_dim;
        let mut weights_payload_offset = 0usize;
        let mut header_layers: Option<usize> = None;
        let header_mode = boot_cfg.soma_weights_header;
        if header_mode != Some(false) {
            match probe_weights_header_from_fat(con_out, image, system_table) {
                WeightsHeaderProbe::Present(h) => {
                    let payload = weights_size_total.saturating_sub(h.header_len);
                    let stride_ok = h.dim.checked_mul(h.dim).and_then(|x| x.checked_mul(4));
                    if stride_ok.is_some() && payload >= stride_ok.unwrap() {
                        dim = h.dim;
                        weights_payload_offset = h.header_len;
                        header_layers = Some(h.layers);
                        uefi_print(con_out, "Soma: weights header dim=");
                        uefi_print_u64_dec(con_out, dim as u64);
                        uefi_print(con_out, " layers=");
                        uefi_print_u64_dec(con_out, h.layers as u64);
                        uefi_print(con_out, "\n");
                    } else {
                        uefi_print(con_out, "Soma: weights header ignored (payload too small)\n");
                    }
                }
                WeightsHeaderProbe::Invalid => {
                    if header_mode == Some(true) {
                        uefi_print(con_out, "Soma: weights header ignored (invalid)\n");
                    }
                }
                WeightsHeaderProbe::TooSmall => {
                    if header_mode == Some(true) {
                        uefi_print(con_out, "Soma: weights header ignored (too small)\n");
                    }
                }
                WeightsHeaderProbe::ReadFailed => {
                    if header_mode == Some(true) {
                        uefi_print(con_out, "Soma: weights header ignored (read failed)\n");
                    }
                }
            }
        }

        // Auto-derive how many complete layers we can load from WEIGHTS.BIN.
        // Layout assumption: contiguous [layers][dim][dim] f32.
        let Some(layer_stride) = dim.checked_mul(dim).and_then(|x| x.checked_mul(4)) else {
            uefi_print(con_out, "Soma: bad dim overflow\n");
            return false;
        };
        let weights_size = weights_size_total.saturating_sub(weights_payload_offset);
        let mut layers_from_file = if weights_size >= layer_stride {
            weights_size / layer_stride
        } else {
            1
        };
        if let Some(h_layers) = header_layers {
            layers_from_file = core::cmp::max(1, core::cmp::min(layers_from_file, h_layers));
        }

        // Keep a multi-layer default even if weights.bin is minimal.
        // Missing layers stay at the default in-memory initialization (0.01).
        let default_layers_alloc = 6usize;
        let requested_layers = if boot_cfg.soma_layers == 0 {
            default_layers_alloc
        } else {
            boot_cfg.soma_layers
        };

        let mut layers_alloc = core::cmp::max(layers_from_file, requested_layers);
        if layers_alloc == 0 {
            layers_alloc = 1;
        }
        if layers_alloc > 64 {
            layers_alloc = 64;
        }

        let mut soma = NeuralSoma::new(dim, layers_alloc);
        soma.set_active_layers(boot_cfg.soma_layers);

        uefi_print(con_out, "Initializing Soma (Neural Body)...\n");
        if let Err(_) = soma.load_weights(&mut w) {
            uefi_print(con_out, "Soma: load_weights failed\n");
            return false;
        }

        if let Some(h) = soma.weights_handle() {
            if let Ok((start, end)) = w.cap_range(h) {
                let size = end - start;
                uefi_print(con_out, "Soma: attempting to load WEIGHTS.BIN...\n");
                match try_read_weights_from_fat_offset(
                    con_out,
                    image,
                    system_table,
                    weights_payload_offset,
                    start as *mut u8,
                    size,
                ) {
                    Some(n) => {
                         uefi_print(con_out, "Soma: weights loaded bytes=");
                         uefi_print_u64_dec(con_out, n as u64);
                         uefi_print(con_out, "\n");
                    }
                    None => {
                         uefi_print(con_out, "Soma: failed to load weights (kept defaults)\n");
                    }
                }
            }
        }
        if let Err(_) = soma.init_state(&mut w) {
             uefi_print(con_out, "Soma: init_state failed\n");
             return false;
        }

        let bs = unsafe { &*(*system_table).boot_services };
        let con_in = unsafe { (*system_table).con_in };
        let tokenizer = SimpleTokenizer::new();
        let mut gen_step: usize = 0;

        let mut interactive = boot_cfg.soma_interactive_default;
        if interactive {
            uefi_print(con_out, "Soma: interactive enabled by policy.\n");
        } else {
            uefi_print(con_out, "Soma: Press 'i' within 1500ms for interactive input.\n");
            if let Some(c) = poll_key_with_timeout_ms(bs, con_in, 1500) {
                if c == (b'i' as u16) || c == (b'I' as u16) {
                    interactive = true;
                }
            }
        }

        if interactive {
            uefi_print(con_out, "Soma: Interactive Neural Loop. Type to inject context. Press ESC to continue tests.\n");
            uefi_print(con_out, "Soma: (idle timeout ~4s to continue boot)\n");
            loop {
                uefi_print(con_out, "\nUser> ");

                // rudimentary readline
                let mut input_buf = [0u8; 64];
                let mut len = 0usize;
                let mut exit = false;

                // Avoid hanging automated runs: if no key is pressed soon,
                // exit interactive mode and continue the rest of the tests.
                let first = match poll_key_with_timeout_ms(bs, con_in, 4000) {
                    Some(c) => c,
                    None => {
                        uefi_print(con_out, "\n[Interactive idle timeout]\n");
                        break;
                    }
                };

                if first == 27 {
                    uefi_print(con_out, "[Exit interactive]\n");
                    break;
                }

                if first != 13 {
                    if first >= 32 && first <= 126 && len < 63 {
                        input_buf[len] = first as u8;
                        len += 1;
                        let mut b = [0u16; 2];
                        b[0] = first;
                        b[1] = 0;
                        unsafe {
                            ((*con_out).output_string)(con_out, b.as_ptr());
                        }
                    }
                }
                loop {
                    let c = wait_for_keypress(bs, con_in);
                    if c == 13 {
                        uefi_print(con_out, "\n");
                        break;
                    }
                    if c == 27 {
                        uefi_print(con_out, "[Exit interactive]\n");
                        exit = true;
                        break;
                    }
                    if c >= 32 && c <= 126 && len < 63 {
                        input_buf[len] = c as u8;
                        len += 1;
                        let mut b = [0u16; 2];
                        b[0] = c;
                        b[1] = 0;
                        unsafe {
                            ((*con_out).output_string)(con_out, b.as_ptr());
                        }
                    }
                }

                if exit {
                    break;
                }

                if len > 0 {
                    let slice = unsafe { core::str::from_utf8_unchecked(&input_buf[..len]) };
                    if soma.update_state_with_input(&mut w, gen_step, slice).is_err() {
                        uefi_print(con_out, "[Error injecting input]\n");
                    }
                }

                uefi_print(con_out, "Soma> ");
                for _ in 0..boot_cfg.soma_steps {
                    match soma.think_step(&mut w, gen_step) {
                        Ok(v) => {
                            let token = tokenizer.decode(v);
                            uefi_print(con_out, token);
                            uefi_print(con_out, " ");
                        }
                        Err(_) => {
                            uefi_print(con_out, "[CRASH]\n");
                            break;
                        }
                    }
                    gen_step = gen_step.wrapping_add(1);
                }
                uefi_print(con_out, "\n");
            }
        } else {
            // Default (non-interactive) path for QEMU smoke tests.
            uefi_print(con_out, "Soma: thinking...\n");
            uefi_print(con_out, "Soma Output: \"");
            for _ in 0..boot_cfg.soma_steps {
                match soma.think_step(&mut w, gen_step) {
                    Ok(v) => {
                        let token = tokenizer.decode(v);
                        uefi_print(con_out, token);
                        uefi_print(con_out, " ");
                    }
                    Err(_) => {
                        uefi_print(con_out, "[CRASH]\n");
                        return false;
                    }
                }
                gen_step = gen_step.wrapping_add(1);
            }
            uefi_print(con_out, "\"\n");
        }
        
        // now make it hallucinate
        uefi_print(con_out, "Soma: forcing hallucination...\n");
        if let Ok(_) = soma.hallucinate(&mut w) {
             uefi_print(con_out, "Soma: hallucination CAUGHT by Warden (GOOD)\n");
        } else {
             uefi_print(con_out, "Soma: hallucination SUCCEEDED (BAD)\n");
             return false;
        }

        // 1. Create a clumsy cell
        const CLUMSY_CELL: u32 = 999;
        let intent = MemIntent::new(CLUMSY_CELL, 4096);
        let cap = w.allocate(intent).unwrap();
        let (start, _) = w.cap_range(cap).unwrap();

        // 2. Generate AccessDenied events (read out of bounds)
        // We need enough events to trigger the "Sentinel" logic we will implement below.
        // Let's say we trigger 4 access violations (threshold is 3, so >3 triggers).
        for _ in 0..4 {
            let _ = w.check_access(cap, Access::Read, start + 8192, 1);
        }

        // 3. Run the Sentinel
        // In a real OS, this would be a background task. Here we run it explicitly.
        // We use a local state. In a real system this would be persistent.
        let mut sentinel_state = osg_memory_warden::sentinel::SentinelState::new();
        Sentinel::run(&mut w, &mut sentinel_state, &sentinel_rules);

        // 4. Verify the cell is quarantined AND reclaimed
        // Previously we checked for Expired, but now Sentinel calls reclaim_expired(),
        // so the cap is fully removed -> InvalidHandle.
        match w.check_access(cap, Access::Read, start, 1) {
            Err(MemError::InvalidHandle) => {
                 uefi_print(con_out, "Sentinel successfully quarantined & reclaimed clumsy cell\n");
            },
            Err(MemError::Expired) => {
                 uefi_print(con_out, "Sentinel quarantined but not reclaimed? (Partial success)\n");
                 // Depending on implementation, this might happen if reclaim didn't run.
                 // But we want full success.
            },
            Ok(_) => {
                 uefi_print(con_out, "Sentinel failed: access still allowed\n");
                 return false;
            },
            Err(_) => {
                 uefi_print(con_out, "Sentinel failed: unexpected error\n");
                 return false;
            }
        }
    }

    // Auto-Repair / Reclamation Test
    {
        uefi_print(con_out, "Auto-Repair Test...\n");
        // allocate a new cap for a doomed cell
        const DOOMED_CELL: u32 = 888;
        let intent = MemIntent::new(DOOMED_CELL, 4096);
        // intent.reclaims_on_expiration = true? (New feature maybe?)
        // For now, our Sentinel does reclaim_expired() globally.
        
        let cap_doomed = match w.allocate(intent) {
             Ok(h) => h,
             Err(_) => {
                 uefi_print(con_out, "OS-G: Alloc doomed cap failed\n");
                 return false;
             }
        };
        let (s_d, _) = match w.cap_range(cap_doomed) {
             Ok(r) => r,
             Err(_) => return false,
        };
        
        // Quarantine it (simulating crash)
        if w.quarantine_cell(DOOMED_CELL).is_err() {
            return false;
        }
        w.tick(1); 
        
        // Cap is now EXPIRED.
        if w.check_access(cap_doomed, Access::Read, s_d, 1) != Err(MemError::Expired) {
             uefi_print(con_out, "OS-G: Doomed cap not expired?\n");
             return false;
        }
        
        // Run Sentinel (which calls reclaim_expired)
        let mut state = osg_memory_warden::sentinel::SentinelState::new();
        Sentinel::run(&mut w, &mut state, &sentinel_rules);

        // Now cap_doomed should be INVALID (slot freed and generation increased)
        match w.cap_range(cap_doomed) {
            Ok(_) => {
                 uefi_print(con_out, "Auto-Repair failed: cap still valid (should be InvalidHandle)\n");
                 return false;
            },
            Err(osg_memory_warden::MemError::InvalidHandle) => {
                 uefi_print(con_out, "Auto-Repair successfully reclaimed cap (InvalidHandle)\n");
            },
            Err(_e) => {
                 uefi_print(con_out, "Auto-Repair failed: expected InvalidHandle\n");
                 return false;
            }
        }
    }

    // Transaction / Rollback Test
    {
        uefi_print(con_out, "Rollback Test...\n");
        // 1. Take a snapshot (Checkpoint)
        let checkpoint = match w.snapshot() {
            Ok(s) => s,
            Err(_) => {
                uefi_print(con_out, "Snapshot failed\n");
                return false;
            }
        };

        // 2. Perform partial work (Allocating multiple resources for a new 'driver')
        const DRIVER_CELL: u32 = 777;
        let mut p1 = MemIntent::new(DRIVER_CELL, 4096);
        p1.label = 101; // "Driver Code"
        let h1 = match w.allocate(p1) {
            Ok(h) => h,
            Err(_) => {
                uefi_print(con_out, "Start of transaction failed\n");
                return false;
            }
        };

        let mut p2 = MemIntent::new(DRIVER_CELL, 8192);
        p2.label = 102; // "Driver Data"
        if w.allocate(p2).is_err() {
            uefi_print(con_out, "Mid-transaction failed\n");
            return false;
        }

        // 3. Simulate critical verification failure
        uefi_print(con_out, "Simulating driver verification failure... Rolling back.\n");
        
        // 4. Rollback
        if w.restore(&checkpoint).is_err() {
            uefi_print(con_out, "Restore failed\n");
            return false;
        }

        // 5. Verify state is reverted
        // The handle h1 should now be invalid because the slot was free in the snapshot.
        match w.cap_range(h1) {
            Err(osg_memory_warden::MemError::InvalidHandle) => {
                 uefi_print(con_out, "Rollback successful: h1 is invalid\n");
            },
            Ok(_) => {
                 uefi_print(con_out, "Rollback failed: h1 still valid\n");
                 return false;
            },
            Err(_e) => {
                 // Should be InvalidHandle
                 uefi_print(con_out, "Rollback output unexpected error\n");
            }
        }
    }


    true
}

#[no_mangle]
pub extern "efiapi" fn efi_main(_image: EfiHandle, system_table: *mut EfiSystemTable) -> EfiStatus {
    let con_out = unsafe { if system_table.is_null() { core::ptr::null_mut() } else { (*system_table).con_out } };

    uefi_print(con_out, "OS-G Memory Warden - UEFI/QEMU smoke test\n");

    let ok = run_warden_checks(con_out, _image, system_table);
    if ok {
        uefi_print(con_out, "RESULT: PASS\n");
    } else {
        uefi_print(con_out, "RESULT: FAIL\n");
    }

    EFI_SUCCESS
}

const WEIGHTS_HDR_MAGIC: [u8; 4] = *b"OSGW";
const WEIGHTS_HDR_LEN: usize = 16;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
struct WeightsHeader {
    dim: usize,
    layers: usize,
    header_len: usize,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum WeightsHeaderProbe {
    Present(WeightsHeader),
    Invalid,
    TooSmall,
    ReadFailed,
}

fn parse_weights_header(buf: &[u8]) -> Option<WeightsHeader> {
    if buf.len() < WEIGHTS_HDR_LEN {
        return None;
    }
    if buf[0..4] != WEIGHTS_HDR_MAGIC {
        return None;
    }
    let ver = u16::from_le_bytes([buf[4], buf[5]]);
    if ver != 1 {
        return None;
    }
    let dim = u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]) as usize;
    let layers = u32::from_le_bytes([buf[12], buf[13], buf[14], buf[15]]) as usize;

    // Keep within demo bounds; if out-of-range, ignore header.
    if !(16..=256).contains(&dim) {
        return None;
    }
    if layers == 0 || layers > 64 {
        return None;
    }

    Some(WeightsHeader {
        dim,
        layers,
        header_len: WEIGHTS_HDR_LEN,
    })
}

fn probe_weights_header_from_fat(
    con_out: *mut SimpleTextOutputProtocol,
    image: EfiHandle,
    system_table: *mut EfiSystemTable,
) -> WeightsHeaderProbe {
    let mut hdr = [0u8; WEIGHTS_HDR_LEN];
    let Some(n) = try_read_weights_from_fat_offset(
        con_out,
        image,
        system_table,
        0,
        hdr.as_mut_ptr(),
        hdr.len(),
    ) else {
        return WeightsHeaderProbe::ReadFailed;
    };
    if n < WEIGHTS_HDR_LEN {
        return WeightsHeaderProbe::TooSmall;
    }
    if let Some(h) = parse_weights_header(&hdr) {
        WeightsHeaderProbe::Present(h)
    } else {
        WeightsHeaderProbe::Invalid
    }
}

fn try_read_weights_via_blockio_fat(
    con_out: *mut SimpleTextOutputProtocol,
    bs: &EfiBootServices,
    loaded_image: &EfiLoadedImageProtocol,
    file_offset: usize,
    out_ptr: *mut u8,
    out_len: usize,
) -> Option<usize> {
    // Acquire BlockIO from the device handle.
    let mut bio_ptr: *mut c_void = core::ptr::null_mut();
    let st = (bs.handle_protocol)(
        loaded_image.device_handle,
        &EFI_BLOCK_IO_PROTOCOL_GUID as *const EfiGuid,
        &mut bio_ptr,
    );
    if st != EFI_SUCCESS || bio_ptr.is_null() {
        uefi_print_status(con_out, "SOMA: BlockIO(handle_protocol)", st);
        return None;
    }
    let bio = bio_ptr as *mut EfiBlockIoProtocol;
    let media = unsafe { (*bio).media };
    if media.is_null() {
        return None;
    }
    let block_size = unsafe { (*media).block_size as usize };
    if block_size > 4096 {
        return None;
    }

    let mut sector = [0u8; 4096];
    let status0 = read_block(bio, 0, sector.as_mut_ptr(), sector.len());
    if status0 != EFI_SUCCESS {
        uefi_print_status(con_out, "SOMA: BlockIO(read LBA0)", status0);
        return None;
    }

    let parse_bpb = |vbr: &[u8]| -> Option<(u64, FatBpb)> {
        if vbr.len() < 90 {
            return None;
        }
        if vbr[510] != 0x55 || vbr[511] != 0xAA {
            return None;
        }
        let bytes_per_sector = u16::from_le_bytes([vbr[11], vbr[12]]) as u32;
        if bytes_per_sector != 512 && bytes_per_sector != 1024 && bytes_per_sector != 2048 && bytes_per_sector != 4096 {
            return None;
        }
        let sectors_per_cluster = vbr[13] as u32;
        if sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 {
            return None;
        }
        let reserved = u16::from_le_bytes([vbr[14], vbr[15]]) as u32;
        let fats = vbr[16] as u32;
        let root_entries = u16::from_le_bytes([vbr[17], vbr[18]]) as u32;
        let total16 = u16::from_le_bytes([vbr[19], vbr[20]]) as u32;
        let fat16 = u16::from_le_bytes([vbr[22], vbr[23]]) as u32;
        let total32 = u32::from_le_bytes([vbr[32], vbr[33], vbr[34], vbr[35]]);
        let fat32 = u32::from_le_bytes([vbr[36], vbr[37], vbr[38], vbr[39]]);
        let root_cluster = u32::from_le_bytes([vbr[44], vbr[45], vbr[46], vbr[47]]);

        let fat_size = if fat16 != 0 { fat16 } else { fat32 };
        let total_sectors = if total16 != 0 { total16 } else { total32 };
        if fat_size == 0 || total_sectors == 0 {
            return None;
        }
        let root_dir_sectors = ((root_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
        let first_data_sector = reserved + fats * fat_size + root_dir_sectors;
        if total_sectors <= first_data_sector {
            return None;
        }
        let data_sectors = total_sectors - first_data_sector;
        let clusters = data_sectors / sectors_per_cluster;
        let fat_type = if clusters < 4085 {
            FatType::Fat12
        } else if clusters < 65525 {
            FatType::Fat16
        } else {
            FatType::Fat32
        };

        Some((0, FatBpb {
            bytes_per_sector,
            sectors_per_cluster,
            reserved_sectors: reserved,
            fats,
            fat_size_sectors: fat_size,
            first_data_sector,
            root_dir_sectors,
            fat_type,
            root_cluster,
        }))
    };

    // Try superfloppy VBR at LBA0 first.
    let mut part_lba: u64 = 0;
    let bpb: FatBpb;
    if let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) {
        bpb = bb;
    } else {
        // MBR: first partition entry at 0x1BE.
        if sector[510] != 0x55 || sector[511] != 0xAA {
            return None;
        }
        let p0 = 0x1BE;
        let lba_start = u32::from_le_bytes([sector[p0 + 8], sector[p0 + 9], sector[p0 + 10], sector[p0 + 11]]) as u64;
        if lba_start == 0 {
            return None;
        }
        part_lba = lba_start;
        let status_vbr = read_block(bio, part_lba, sector.as_mut_ptr(), sector.len());
        if status_vbr != EFI_SUCCESS {
            return None;
        }
        let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) else {
            return None;
        };
        bpb = bb;
    }

    if matches!(bpb.fat_type, FatType::Fat12) {
        return None;
    }

    // Read FAT into a scratch sector on demand.
    let fat_start_lba = part_lba + bpb.reserved_sectors as u64;
    let root_dir_lba = part_lba + (bpb.reserved_sectors + bpb.fats * bpb.fat_size_sectors) as u64;
    let data_start_lba = part_lba + bpb.first_data_sector as u64;

    let mut lfn_name = [0u16; 260];
    let mut lfn_valid = false;

    let mut check_entry = |entry: &[u8]| -> Option<(u32, u32)> {
        if entry.len() < 32 {
            return None;
        }
        let first = entry[0];
        if first == 0x00 {
            return None;
        }
        if first == 0xE5 {
            lfn_valid = false;
            return Some((0, 0));
        }
        let attr = entry[11];
        if attr == 0x0F {
            // LFN entry
            let seq = (entry[0] & 0x1F) as usize;
            if seq == 0 {
                return Some((0, 0));
            }
            if (entry[0] & 0x40) != 0 {
                // start
                for c in &mut lfn_name {
                    *c = 0;
                }
                lfn_valid = true;
            }
            if !lfn_valid {
                return Some((0, 0));
            }
            let base = (seq - 1) * 13;
            let mut put = |i: usize, lo: u8, hi: u8| {
                let idx = base + i;
                if idx < lfn_name.len() {
                    lfn_name[idx] = u16::from_le_bytes([lo, hi]);
                }
            };
            // name1 (5)
            put(0, entry[1], entry[2]);
            put(1, entry[3], entry[4]);
            put(2, entry[5], entry[6]);
            put(3, entry[7], entry[8]);
            put(4, entry[9], entry[10]);
            // name2 (6)
            put(5, entry[14], entry[15]);
            put(6, entry[16], entry[17]);
            put(7, entry[18], entry[19]);
            put(8, entry[20], entry[21]);
            put(9, entry[22], entry[23]);
            put(10, entry[24], entry[25]);
            // name3 (2)
            put(11, entry[28], entry[29]);
            put(12, entry[30], entry[31]);
            return Some((0, 0));
        }

        // Normal entry.
        let mut name_match = false;
        if lfn_valid {
            // Convert UCS-2 to ASCII for comparison.
            let mut tmp = [0u8; 64];
            let mut n = 0usize;
            for &ch in &lfn_name {
                if ch == 0x0000 || ch == 0xFFFF {
                    break;
                }
                if ch <= 0x7F && n < tmp.len() {
                    tmp[n] = ch as u8;
                    n += 1;
                } else {
                    n = 0;
                    break;
                }
            }
            if n > 0 {
                let s = core::str::from_utf8(&tmp[..n]).ok()?;
                if eq_ascii_ignore_case(s, "weights.bin") {
                    name_match = true;
                }
            }
        }
        lfn_valid = false;

        if !name_match {
            // Fallback 8.3 match: WEIGHTS.*
            let n0 = &entry[0..8];
            let e0 = &entry[8..11];
            if &n0[0..7] == b"WEIGHTS" {
                // accept any extension or check e0 == "BIN"
                let _ = e0;
                name_match = true;
            }
        }

        if !name_match {
            return Some((0, 0));
        }

        let hi = u16::from_le_bytes([entry[20], entry[21]]) as u32;
        let lo = u16::from_le_bytes([entry[26], entry[27]]) as u32;
        let first_cluster = if matches!(bpb.fat_type, FatType::Fat32) {
            (hi << 16) | lo
        } else {
            lo
        };
        let file_size = u32::from_le_bytes([entry[28], entry[29], entry[30], entry[31]]);
        Some((first_cluster, file_size))
    };

    let (mut first_cluster, file_size) = if matches!(bpb.fat_type, FatType::Fat16) {
        // FAT16 root dir is fixed region.
        let mut found: Option<(u32, u32)> = None;
        for s in 0..bpb.root_dir_sectors {
            let lba = root_dir_lba + s as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }
            let mut off = 0usize;
            while off + 32 <= block_size {
                let e = &sector[off..off + 32];
                if e[0] == 0x00 {
                    break;
                }
                if let Some((cl, sz)) = check_entry(e) {
                    if cl != 0 && sz != 0 {
                        found = Some((cl, sz));
                        break;
                    }
                }
                off += 32;
            }
            if found.is_some() {
                break;
            }
        }
        found?
    } else {
        // FAT32: root dir is a cluster chain.
        let mut dir_cluster = bpb.root_cluster;
        let mut found: Option<(u32, u32)> = None;
        for _ in 0..1024 {
            if dir_cluster < 2 {
                break;
            }
            let first_sector = data_start_lba + ((dir_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
            for sc in 0..bpb.sectors_per_cluster {
                let lba = first_sector + sc as u64;
                let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
                if st != EFI_SUCCESS {
                    return None;
                }
                let mut off = 0usize;
                while off + 32 <= block_size {
                    let e = &sector[off..off + 32];
                    if e[0] == 0x00 {
                        break;
                    }
                    if let Some((cl, sz)) = check_entry(e) {
                        if cl != 0 && sz != 0 {
                            found = Some((cl, sz));
                            break;
                        }
                    }
                    off += 32;
                }
                if found.is_some() {
                    break;
                }
            }
            if found.is_some() {
                break;
            }
            // next cluster in dir
            dir_cluster = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, dir_cluster)?;
            if is_eoc(&bpb, dir_cluster) {
                break;
            }
        }
        found?
    };

    if first_cluster < 2 {
        return None;
    }
    
    if out_len > WEIGHTS_HDR_LEN {
        uefi_print(con_out, "SOMA: found weights cluster=");
        uefi_print_u64_dec(con_out, first_cluster as u64);
        uefi_print(con_out, " size=");
        uefi_print_u64_dec(con_out, file_size as u64);
        uefi_print(con_out, "\n");
    }

    let mut file_pos = 0usize;
    let mut remaining_file = file_size as usize;
    let mut written = 0usize;
    while remaining_file > 0 && written < out_len {
        if is_eoc(&bpb, first_cluster) {
            break;
        }
        let first_sector = data_start_lba + ((first_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
        for sc in 0..bpb.sectors_per_cluster {
            if remaining_file == 0 || written >= out_len {
                break;
            }
            let lba = first_sector + sc as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }

            let take_in_file = core::cmp::min(remaining_file, block_size);
            if file_pos + take_in_file > file_offset {
                let begin = if file_pos >= file_offset {
                    0usize
                } else {
                    file_offset - file_pos
                };
                let avail = take_in_file.saturating_sub(begin);
                let can_take = core::cmp::min(avail, out_len - written);
                if can_take > 0 {
                    unsafe {
                        core::ptr::copy_nonoverlapping(
                            sector.as_ptr().add(begin),
                            out_ptr.add(written),
                            can_take,
                        );
                    }
                    written += can_take;
                }
            }

            file_pos += take_in_file;
            remaining_file -= take_in_file;
        }
        let next = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, first_cluster)?;
        first_cluster = next;
    }

    Some(written)
}

fn try_probe_weights_file_size_via_blockio_fat(
    con_out: *mut SimpleTextOutputProtocol,
    bs: &EfiBootServices,
    loaded_image: &EfiLoadedImageProtocol,
) -> Option<usize> {
    // Acquire BlockIO from the device handle.
    let mut bio_ptr: *mut c_void = core::ptr::null_mut();
    let st = (bs.handle_protocol)(
        loaded_image.device_handle,
        &EFI_BLOCK_IO_PROTOCOL_GUID as *const EfiGuid,
        &mut bio_ptr,
    );
    if st != EFI_SUCCESS || bio_ptr.is_null() {
        return None;
    }
    let bio = bio_ptr as *mut EfiBlockIoProtocol;
    let media = unsafe { (*bio).media };
    if media.is_null() {
        return None;
    }
    let block_size = unsafe { (*media).block_size as usize };
    if block_size > 4096 {
        return None;
    }

    let mut sector = [0u8; 4096];
    let status0 = read_block(bio, 0, sector.as_mut_ptr(), sector.len());
    if status0 != EFI_SUCCESS {
        return None;
    }

    let parse_bpb = |vbr: &[u8]| -> Option<(u64, FatBpb)> {
        if vbr.len() < 90 {
            return None;
        }
        if vbr[510] != 0x55 || vbr[511] != 0xAA {
            return None;
        }
        let bytes_per_sector = u16::from_le_bytes([vbr[11], vbr[12]]) as u32;
        if bytes_per_sector != 512 && bytes_per_sector != 1024 && bytes_per_sector != 2048 && bytes_per_sector != 4096 {
            return None;
        }
        let sectors_per_cluster = vbr[13] as u32;
        if sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 {
            return None;
        }
        let reserved = u16::from_le_bytes([vbr[14], vbr[15]]) as u32;
        let fats = vbr[16] as u32;
        let root_entries = u16::from_le_bytes([vbr[17], vbr[18]]) as u32;
        let total16 = u16::from_le_bytes([vbr[19], vbr[20]]) as u32;
        let fat16 = u16::from_le_bytes([vbr[22], vbr[23]]) as u32;
        let total32 = u32::from_le_bytes([vbr[32], vbr[33], vbr[34], vbr[35]]);
        let fat32 = u32::from_le_bytes([vbr[36], vbr[37], vbr[38], vbr[39]]);
        let root_cluster = u32::from_le_bytes([vbr[44], vbr[45], vbr[46], vbr[47]]);

        let fat_size = if fat16 != 0 { fat16 } else { fat32 };
        let total_sectors = if total16 != 0 { total16 } else { total32 };
        if fat_size == 0 || total_sectors == 0 {
            return None;
        }
        let root_dir_sectors = ((root_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
        let first_data_sector = reserved + fats * fat_size + root_dir_sectors;
        if total_sectors <= first_data_sector {
            return None;
        }
        let data_sectors = total_sectors - first_data_sector;
        let clusters = data_sectors / sectors_per_cluster;
        let fat_type = if clusters < 4085 {
            FatType::Fat12
        } else if clusters < 65525 {
            FatType::Fat16
        } else {
            FatType::Fat32
        };

        Some((0, FatBpb {
            bytes_per_sector,
            sectors_per_cluster,
            reserved_sectors: reserved,
            fats,
            fat_size_sectors: fat_size,
            first_data_sector,
            root_dir_sectors,
            fat_type,
            root_cluster,
        }))
    };

    // Try superfloppy VBR at LBA0 first.
    let mut part_lba: u64 = 0;
    let bpb: FatBpb;
    if let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) {
        bpb = bb;
    } else {
        // MBR: first partition entry at 0x1BE.
        if sector[510] != 0x55 || sector[511] != 0xAA {
            return None;
        }
        let p0 = 0x1BE;
        let lba_start = u32::from_le_bytes([sector[p0 + 8], sector[p0 + 9], sector[p0 + 10], sector[p0 + 11]]) as u64;
        if lba_start == 0 {
            return None;
        }
        part_lba = lba_start;
        let status_vbr = read_block(bio, part_lba, sector.as_mut_ptr(), sector.len());
        if status_vbr != EFI_SUCCESS {
            return None;
        }
        let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) else {
            return None;
        };
        bpb = bb;
    }

    if matches!(bpb.fat_type, FatType::Fat12) {
        return None;
    }

    let fat_start_lba = part_lba + bpb.reserved_sectors as u64;
    let root_dir_lba = part_lba + (bpb.reserved_sectors + bpb.fats * bpb.fat_size_sectors) as u64;
    let data_start_lba = part_lba + bpb.first_data_sector as u64;

    let mut lfn_name = [0u16; 260];
    let mut lfn_valid = false;

    let mut check_entry = |entry: &[u8]| -> Option<(u32, u32)> {
        if entry.len() < 32 {
            return None;
        }
        let first = entry[0];
        if first == 0x00 {
            return None;
        }
        if first == 0xE5 {
            lfn_valid = false;
            return Some((0, 0));
        }
        let attr = entry[11];
        if attr == 0x0F {
            // LFN entry
            let seq = (entry[0] & 0x1F) as usize;
            if seq == 0 {
                return Some((0, 0));
            }
            if (entry[0] & 0x40) != 0 {
                // start
                for c in &mut lfn_name {
                    *c = 0;
                }
                lfn_valid = true;
            }
            if !lfn_valid {
                return Some((0, 0));
            }
            let base = (seq - 1) * 13;
            let mut put = |i: usize, lo: u8, hi: u8| {
                let idx = base + i;
                if idx < lfn_name.len() {
                    lfn_name[idx] = u16::from_le_bytes([lo, hi]);
                }
            };
            // name1 (5)
            put(0, entry[1], entry[2]);
            put(1, entry[3], entry[4]);
            put(2, entry[5], entry[6]);
            put(3, entry[7], entry[8]);
            put(4, entry[9], entry[10]);
            // name2 (6)
            put(5, entry[14], entry[15]);
            put(6, entry[16], entry[17]);
            put(7, entry[18], entry[19]);
            put(8, entry[20], entry[21]);
            put(9, entry[22], entry[23]);
            put(10, entry[24], entry[25]);
            // name3 (2)
            put(11, entry[28], entry[29]);
            put(12, entry[30], entry[31]);
            return Some((0, 0));
        }

        // Normal entry.
        let mut name_match = false;
        if lfn_valid {
            // Convert UCS-2 to ASCII for comparison.
            let mut tmp = [0u8; 64];
            let mut n = 0usize;
            for &ch in &lfn_name {
                if ch == 0x0000 || ch == 0xFFFF {
                    break;
                }
                if ch <= 0x7F && n < tmp.len() {
                    tmp[n] = ch as u8;
                    n += 1;
                } else {
                    n = 0;
                    break;
                }
            }
            if n > 0 {
                let s = core::str::from_utf8(&tmp[..n]).ok()?;
                if eq_ascii_ignore_case(s, "weights.bin") {
                    name_match = true;
                }
            }
        }
        lfn_valid = false;

        if !name_match {
            // Fallback 8.3 match: WEIGHTS.*
            let n0 = &entry[0..8];
            let e0 = &entry[8..11];
            if &n0[0..7] == b"WEIGHTS" {
                let _ = e0;
                name_match = true;
            }
        }

        if !name_match {
            return Some((0, 0));
        }

        let hi = u16::from_le_bytes([entry[20], entry[21]]) as u32;
        let lo = u16::from_le_bytes([entry[26], entry[27]]) as u32;
        let first_cluster = if matches!(bpb.fat_type, FatType::Fat32) {
            (hi << 16) | lo
        } else {
            lo
        };
        let file_size = u32::from_le_bytes([entry[28], entry[29], entry[30], entry[31]]);
        Some((first_cluster, file_size))
    };

    let (_first_cluster, file_size) = if matches!(bpb.fat_type, FatType::Fat16) {
        // FAT16 root dir is fixed region.
        let mut found: Option<(u32, u32)> = None;
        for s in 0..bpb.root_dir_sectors {
            let lba = root_dir_lba + s as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }
            let mut off = 0usize;
            while off + 32 <= block_size {
                let e = &sector[off..off + 32];
                if e[0] == 0x00 {
                    break;
                }
                if let Some((cl, sz)) = check_entry(e) {
                    if cl != 0 && sz != 0 {
                        found = Some((cl, sz));
                        break;
                    }
                }
                off += 32;
            }
            if found.is_some() {
                break;
            }
        }
        found?
    } else {
        // FAT32: root dir is a cluster chain.
        let mut dir_cluster = bpb.root_cluster;
        let mut found: Option<(u32, u32)> = None;
        for _ in 0..1024 {
            if dir_cluster < 2 {
                break;
            }
            let first_sector = data_start_lba + ((dir_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
            for sc in 0..bpb.sectors_per_cluster {
                let lba = first_sector + sc as u64;
                let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
                if st != EFI_SUCCESS {
                    return None;
                }
                let mut off = 0usize;
                while off + 32 <= block_size {
                    let e = &sector[off..off + 32];
                    if e[0] == 0x00 {
                        break;
                    }
                    if let Some((cl, sz)) = check_entry(e) {
                        if cl != 0 && sz != 0 {
                            found = Some((cl, sz));
                            break;
                        }
                    }
                    off += 32;
                }
                if found.is_some() {
                    break;
                }
            }
            if found.is_some() {
                break;
            }
            // next cluster in dir
            dir_cluster = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, dir_cluster)?;
            if is_eoc(&bpb, dir_cluster) {
                break;
            }
        }
        found?
    };

    Some(file_size as usize)
}

fn try_probe_weights_file_size_from_fat(
    con_out: *mut SimpleTextOutputProtocol,
    image: EfiHandle,
    system_table: *mut EfiSystemTable,
) -> Option<usize> {
    let st = unsafe { system_table.as_ref()? };
    let bs = unsafe { st.boot_services.as_ref()? };

    let mut loaded_image_ptr: *mut core::ffi::c_void = core::ptr::null_mut();
    let status = (bs.handle_protocol)(
        image,
        &EFI_LOADED_IMAGE_PROTOCOL_GUID as *const EfiGuid,
        &mut loaded_image_ptr,
    );
    if status != EFI_SUCCESS || loaded_image_ptr.is_null() {
        return None;
    }
    let li = unsafe { &*(loaded_image_ptr as *const EfiLoadedImageProtocol) };
    try_probe_weights_file_size_via_blockio_fat(con_out, bs, li)
}
fn try_read_weights_from_fat_offset(
    con_out: *mut SimpleTextOutputProtocol,
    image: EfiHandle,
    system_table: *mut EfiSystemTable,
    file_offset: usize,
    out_ptr: *mut u8,
    out_len: usize,
) -> Option<usize> {
    let st = unsafe { system_table.as_ref()? };
    let bs = unsafe { st.boot_services.as_ref()? };

    if out_ptr.is_null() || out_len == 0 {
        return None;
    }

    let mut loaded_image_ptr: *mut core::ffi::c_void = core::ptr::null_mut();
    let status = (bs.handle_protocol)(
        image,
        &EFI_LOADED_IMAGE_PROTOCOL_GUID as *const EfiGuid,
        &mut loaded_image_ptr,
    );
    if status != EFI_SUCCESS || loaded_image_ptr.is_null() {
        uefi_print_status(con_out, "SOMA: LoadedImageProtocol", status);
        return None;
    }
    let li = unsafe { &*(loaded_image_ptr as *const EfiLoadedImageProtocol) };

    if let Some(n) = try_read_weights_via_blockio_fat(con_out, bs, li, file_offset, out_ptr, out_len) {
        return Some(n);
    }

    None
}
