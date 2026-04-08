/*! oo_uefi_entry.rs — OO Rust Kernel UEFI Entry Point
 *
 * This is the `efi_main` equivalent for the Rust kernel.
 * Mirrors `EFI_STATUS EFIAPI efi_main()` from llama2_efi_final.c
 * but written in safe(r) Rust.
 *
 * Build: cargo build --target x86_64-unknown-uefi --features uefi --release
 * Output: target/x86_64-unknown-uefi/release/oo_uefi.efi
 * Deploy: copy to EFI partition as EFI/BOOT/OO_RUST.EFI
 */

#![no_std]
#![no_main]

use core::panic::PanicInfo;
use oo_kernel::boot::{run_boot_sequence, OO_PHASES};
use oo_kernel::memory::arena;
use oo_kernel::policy::default_oo_policy;

type EfiHandle = *mut core::ffi::c_void;
type EfiStatus = usize;
const EFI_SUCCESS: EfiStatus = 0;

#[unsafe(no_mangle)]
pub extern "efiapi" fn efi_main(
    _image_handle: EfiHandle,
    _system_table: *mut core::ffi::c_void,
) -> EfiStatus {
    /* Phase A: memory zones (placeholder — real impl uses AllocatePages) */
    let base: u64 = 0x0010_0000;  /* 1MB — typical EFI load address */
    let total = (2794 + 32 + 20 + 4) * 1024 * 1024u64;
    if !arena().init(base, total) {
        return 0x8000_0000_0000_000E;  /* EFI_OUT_OF_RESOURCES */
    }

    /* Run boot phases A-Z */
    let _completed = run_boot_sequence();

    /* D+ policy init */
    let _policy = default_oo_policy();

    /* TODO: enter REPL loop */
    /* TODO: call SSM inference */

    EFI_SUCCESS
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    /* Bare-metal panic: halt the CPU */
    loop {
        unsafe { core::arch::asm!("hlt", options(nomem, nostack)); }
    }
}
