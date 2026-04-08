use std::alloc::{alloc, dealloc, Layout};
use std::fs;

use osg_memory_warden::dplus::{
    apply_caps, compute_merit_profile, extract_mem_allocate_rules, format_reasons_csv, parse,
    LawMemAllocate, DPlusSection, SectionKind, SectionTag,
};
use osg_memory_warden::{MemIntent, MemoryWarden, Rights, Zone};

struct AlignedMem {
    ptr: *mut u8,
    size: usize,
    layout: Layout,
}

impl AlignedMem {
    fn new(size: usize, align: usize) -> Self {
        let layout = Layout::from_size_align(size, align).expect("layout");
        // SAFETY: layout is valid.
        let ptr = unsafe { alloc(layout) };
        if ptr.is_null() {
            panic!("alloc failed");
        }
        Self {
            ptr,
            size,
            layout,
        }
    }

    fn base(&self) -> usize {
        self.ptr as usize
    }
}

impl Drop for AlignedMem {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            // SAFETY: ptr was allocated with the same layout.
            unsafe { dealloc(self.ptr, self.layout) };
            self.ptr = core::ptr::null_mut();
        }
    }
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_judge <file.dplus>");
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

    let merit = compute_merit_profile(&module);
    let mut merit_buf = [0u8; 64];
    let reasons = format_reasons_csv(merit.reasons, &mut merit_buf);
    println!(
        "MERIT score={} default_sandbox={} bytes_cap={:?} ttl_cap_ms={:?} reasons={}",
        merit.score_0_100,
        merit.default_sandbox,
        merit.bytes_cap,
        merit.ttl_cap_ms,
        reasons
    );

    // Host simulation assumptions:
    // - We interpret 1 tick == 1ms for ttl_ms.
    // - We use cell id 1.
    const OWNER: u32 = 1;

    // Create normal + sandbox memory regions.
    const PAGE_ALIGN: usize = 4096;
    let normal_mem = AlignedMem::new(4 * 1024 * 1024, PAGE_ALIGN);
    let sandbox_mem = AlignedMem::new(2 * 1024 * 1024, PAGE_ALIGN);

    const BITMAP_WORDS: usize = 64; // 4096 pages max per zone if fully used
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

    // Simple quota for the demo.
    warden.set_quota(OWNER, 256 * 1024).expect("quota");

    // Persist merit decision into Warden journal (used by replay/fingerprints).
    warden.journal_merit_decision(
        OWNER,
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

    // Extract LAW rules.
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
            let raw_ttl_ticks = rule.ttl_ms.unwrap_or(0);
            let sandbox = rule.sandbox.unwrap_or(merit.default_sandbox);
            let label = rule.op_id.unwrap_or(0);

            let (bytes, ttl_ticks) = apply_caps(raw_bytes, raw_ttl_ticks, merit);

            let mut intent = MemIntent::new(OWNER, bytes);
            intent.ttl_ticks = ttl_ticks; // 1 tick == 1ms (host demo)
            intent.sandbox = sandbox;
            intent.label = label;
            intent.rights = Rights::R.union(Rights::W);

            let verdict = warden.allocate(intent);
            match verdict {
                Ok(cap) => {
                    let meta = warden.cap_meta(cap).expect("meta");
                    println!(
                        "GRANTED op={:?} bytes={} ttl_ms={} sandbox={} cap=0x{:08x} zone={:?} expires_at={}",
                        rule.op_id,
                        bytes,
                        ttl_ticks,
                        sandbox,
                        cap.raw(),
                        meta.zone,
                        meta.expires_at
                    );
                    // Keep the demo deterministic by freeing immediately.
                    warden.free(cap).expect("free");
                }
                Err(e) => {
                    println!(
                        "DENIED op={:?} bytes={} ttl_ms={} sandbox={} err={:?}",
                        rule.op_id, bytes, ttl_ticks, sandbox, e
                    );
                }
            }
        }
    }

    if !any {
        println!("NO_RULES: no LAW lines containing mem.allocate");
    }
}
