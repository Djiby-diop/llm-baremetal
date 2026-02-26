use std::alloc::{alloc, dealloc, Layout};
use std::fs;

use osg_memory_warden::dplus::{
    apply_caps, compute_merit_profile, extract_mem_allocate_rules, format_reasons_csv, parse,
    LawMemAllocate, DPlusSection, SectionKind, SectionTag,
};
use osg_memory_warden::{MemIntent, MemoryWarden, Rights};

struct AlignedMem {
    ptr: *mut u8,
    size: usize,
    layout: Layout,
}

impl AlignedMem {
    fn new(size: usize, align: usize) -> Self {
        let layout = Layout::from_size_align(size, align).expect("layout");
        let ptr = unsafe { alloc(layout) };
        if ptr.is_null() {
            panic!("alloc failed");
        }
        Self { ptr, size, layout }
    }

    fn base(&self) -> usize {
        self.ptr as usize
    }
}

impl Drop for AlignedMem {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { dealloc(self.ptr, self.layout) };
            self.ptr = core::ptr::null_mut();
        }
    }
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_merit <file.dplus>");
        std::process::exit(2);
    });

    let src = fs::read_to_string(&path).unwrap_or_else(|e| {
        eprintln!("read failed: {e}");
        std::process::exit(2);
    });

    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 256];

    let module = parse(&src, &mut scratch).unwrap_or_else(|e| {
        eprintln!("parse error: {e}");
        std::process::exit(1);
    });

    let profile = compute_merit_profile(&module);
    let mut merit_buf = [0u8; 64];
    let reasons = format_reasons_csv(profile.reasons, &mut merit_buf);
    println!(
        "MERIT score={} default_sandbox={} bytes_cap={:?} ttl_cap_ms={:?} reasons={}",
        profile.score_0_100,
        profile.default_sandbox,
        profile.bytes_cap,
        profile.ttl_cap_ms,
        reasons
    );

    // Set up deterministic host simulation.
    const OWNER: u32 = 1;
    const PAGE_ALIGN: usize = 4096;

    let normal_mem = AlignedMem::new(4 * 1024 * 1024, PAGE_ALIGN);
    let sandbox_mem = AlignedMem::new(2 * 1024 * 1024, PAGE_ALIGN);

    const BITMAP_WORDS: usize = 64;
    const MAX_CAPS: usize = 128;
    const MAX_CELLS: usize = 16;

    let mut warden = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
    unsafe {
        warden.init_zones(
            normal_mem.base(),
            normal_mem.size,
            sandbox_mem.base(),
            sandbox_mem.size,
        )
    };

    warden.set_quota(OWNER, 256 * 1024).expect("quota");

    // Evaluate LAW rules with merit defaults.
    let mut any = false;
    let mut rules = [LawMemAllocate::default(); 64];
    for sec in module.sections {
        if sec.tag.kind() != Some(SectionKind::Law) {
            continue;
        }

        let n = extract_mem_allocate_rules(sec.body, &mut rules);
        for rule in &rules[..n] {
            any = true;
            let raw_bytes = rule.bytes.unwrap_or(4096);
            let raw_ttl = rule.ttl_ms.unwrap_or(0);
            let sandbox = rule.sandbox.unwrap_or(profile.default_sandbox);
            let label = rule.op_id.unwrap_or(0);

            let (bytes, ttl_ms) = apply_caps(raw_bytes, raw_ttl, profile);

            let mut intent = MemIntent::new(OWNER, bytes);
            intent.ttl_ticks = ttl_ms; // host demo: 1 tick == 1ms
            intent.sandbox = sandbox;
            intent.label = label;
            intent.rights = Rights::R.union(Rights::W);

            match warden.allocate(intent) {
                Ok(cap) => {
                    let meta = warden.cap_meta(cap).expect("meta");
                    println!(
                        "GRANTED op={:?} raw_bytes={} raw_ttl_ms={} -> bytes={} ttl_ms={} sandbox={} zone={:?} cap=0x{:08x}",
                        rule.op_id,
                        raw_bytes,
                        raw_ttl,
                        bytes,
                        ttl_ms,
                        sandbox,
                        meta.zone,
                        cap.raw()
                    );
                    let _ = warden.free(cap);
                }
                Err(e) => {
                    println!(
                        "DENIED op={:?} raw_bytes={} raw_ttl_ms={} -> bytes={} ttl_ms={} sandbox={} err={:?}",
                        rule.op_id, raw_bytes, raw_ttl, bytes, ttl_ms, sandbox, e
                    );
                }
            }
        }
    }

    if !any {
        println!("NO_RULES: no LAW lines containing mem.allocate");
    }
}
