use std::alloc::{alloc, dealloc, Layout};
use std::fs;

use osg_memory_warden::dplus::{
    apply_caps, compute_merit_profile, extract_mem_allocate_rules, format_reasons_csv, parse,
    LawMemAllocate, DPlusSection, MeritReasons, SectionKind, SectionTag,
};
use osg_memory_warden::sentinel::{Sentinel, SentinelState};
use osg_memory_warden::{EventKind, MemIntent, MemoryWarden, Rights, Zone};

fn fnv1a64_step(mut h: u64, b: u8) -> u64 {
    const PRIME: u64 = 0x100000001b3;
    h ^= b as u64;
    h.wrapping_mul(PRIME)
}

fn fnv1a64(bytes: &[u8]) -> u64 {
    const OFFSET_BASIS: u64 = 0xcbf29ce484222325;
    let mut h = OFFSET_BASIS;
    for &b in bytes {
        h = fnv1a64_step(h, b);
    }
    h
}

fn fnv1a64_u64(h: u64, v: u64) -> u64 {
    let mut h = h;
    for b in v.to_le_bytes() {
        h = fnv1a64_step(h, b);
    }
    h
}

fn fnv1a64_u32(h: u64, v: u32) -> u64 {
    let mut h = h;
    for b in v.to_le_bytes() {
        h = fnv1a64_step(h, b);
    }
    h
}

fn fnv1a64_u8(h: u64, v: u8) -> u64 {
    fnv1a64_step(h, v)
}

struct AlignedMem {
    ptr: *mut u8,
    size: usize,
    layout: Layout,
}

fn decode_merit_decision(ev: osg_memory_warden::Event) -> Option<(u8, bool, u32, u32, u32)> {
    if ev.kind != EventKind::MeritDecision {
        return None;
    }

    let score = (ev.info & 0xFF) as u8;
    let default_sandbox = ((ev.info >> 8) & 1) != 0;
    let reasons_bits = (ev.info >> 16) & 0xFFFF;
    let bytes_cap = (ev.bytes & 0xFFFF_FFFF) as u32;
    let ttl_cap_ms = (ev.bytes >> 32) as u32;
    Some((score, default_sandbox, reasons_bits, bytes_cap, ttl_cap_ms))
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

fn run_once(source: &str, dump_journal: bool, dump_label: &str, simulate_sentinel: bool) -> u64 {
    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 256];

    let module = parse(source, &mut scratch).expect("parse");
    let merit = compute_merit_profile(&module);

    // Deterministic host simulation setup.
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

    // Persist merit decision into Warden journal so the replay fingerprint includes it.
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

    let mut rules = [LawMemAllocate::default(); 64];
    for sec in module.sections {
        if sec.tag.kind() != Some(SectionKind::Law) {
            continue;
        }
        let n = extract_mem_allocate_rules(sec.body, &mut rules);
        for rule in &rules[..n] {
            let raw_bytes = rule.bytes.unwrap_or(4096);
            let raw_ttl_ticks = rule.ttl_ms.unwrap_or(0);
            let sandbox = rule.sandbox.unwrap_or(merit.default_sandbox);
            let label = rule.op_id.unwrap_or(0);

            let (bytes, ttl_ticks) = apply_caps(raw_bytes, raw_ttl_ticks, merit);

            let mut intent = MemIntent::new(OWNER, bytes);
            intent.ttl_ticks = ttl_ticks; // 1 tick == 1ms
            intent.sandbox = sandbox;
            intent.label = label;
            intent.rights = Rights::R.union(Rights::W);

            if let Ok(cap) = warden.allocate(intent) {
                // Deterministic: free right away.
                let _ = warden.free(cap);
            }
        }
    }

    if simulate_sentinel {
        // Simulate a misbehaving actor (Cell 999)
        const BAD_ACTOR: u32 = 999;
        let mut intent = MemIntent::new(BAD_ACTOR, 4096);
        intent.sandbox = true;
        if let Ok(cap) = warden.allocate(intent) {
            // Get range
            if let Ok((start, _end)) = warden.cap_range(cap) {
                 // Generate violations (3 needed for sentinel trigger)
                 for _ in 0..3 {
                     // We just "check_access" out of bounds to trigger the event.
                     let _ = warden.check_access(cap, osg_memory_warden::Access::Read, start + 8192, 1);
                 }
            }
            // Run Sentinel
            let mut state = SentinelState::new();
            Sentinel::run(&mut warden, &mut state, &[]);
            
            // Verify quarantine? The fingerprint will capture the "Quarantined" event.
        }
    }

    if dump_journal {
        println!("JOURNAL_DUMP {}:", dump_label);
        let stats = warden.journal_stats();
        println!("  stats len={} dropped={} capacity={}", stats.len, stats.dropped, stats.capacity);
        for i in 0..stats.len {
            if let Some(ev) = warden.journal_get(i) {
                if let Some((score, default_sandbox, reasons_bits, bytes_cap, ttl_cap_ms)) =
                    decode_merit_decision(ev)
                {
                    let mut buf = [0u8; 64];
                    let reasons = format_reasons_csv(
                        MeritReasons::from_bits_truncate(reasons_bits),
                        &mut buf,
                    );
                    println!(
                        "  #{:03} tick={} cell={} kind=MeritDecision zone={:?} score={} default_sandbox={} reasons_bits=0x{:04x} reasons={} bytes_cap={} ttl_cap_ms={} info=0x{:08x}",
                        i,
                        ev.tick,
                        ev.cell,
                        ev.zone,
                        score,
                        default_sandbox,
                        reasons_bits,
                        reasons,
                        bytes_cap,
                        ttl_cap_ms,
                        ev.info
                    );
                } else {
                    println!(
                        "  #{:03} tick={} cell={} kind={:?} zone={:?} handle_raw=0x{:08x} bytes={} info=0x{:08x}",
                        i,
                        ev.tick,
                        ev.cell,
                        ev.kind,
                        ev.zone,
                        ev.handle_raw,
                        ev.bytes,
                        ev.info
                    );
                }
            }
        }
    }

    // Fingerprint journal deterministically.
    let stats = warden.journal_stats();
    let mut h = fnv1a64(b"OSG_JOURNAL_FNV1A64_V0");
    h = fnv1a64_u64(h, stats.len as u64);
    h = fnv1a64_u64(h, stats.dropped);

    for i in 0..stats.len {
        if let Some(ev) = warden.journal_get(i) {
            h = fnv1a64_u64(h, ev.tick);
            h = fnv1a64_u32(h, ev.cell);
            h = fnv1a64_u8(h, ev.kind as u8);
            h = fnv1a64_u8(h, ev.zone as u8);
            h = fnv1a64_u32(h, ev.handle_raw);
            h = fnv1a64_u64(h, ev.bytes);
            h = fnv1a64_u32(h, ev.info);
        }
    }

    h
}

fn main() {
    let mut args = std::env::args().skip(1);
    let mut dump = false;
    let mut simulate_sentinel = false;
    let mut path: Option<String> = None;

    while let Some(a) = args.next() {
        if a == "--dump-journal" {
            dump = true;
        } else if a == "--simulate-sentinel" {
            simulate_sentinel = true;
        } else if a.starts_with('-') {
            eprintln!("unknown flag: {a}");
            std::process::exit(2);
        } else {
            path = Some(a);
            break;
        }
    }

    let path = path.unwrap_or_else(|| {
        eprintln!("usage: dplus_replay [--dump-journal] <file.dplus>");
        std::process::exit(2);
    });

    let src = fs::read_to_string(&path).unwrap_or_else(|e| {
        eprintln!("read failed: {e}");
        std::process::exit(2);
    });

    let h1 = run_once(&src, dump, "run1", simulate_sentinel);
    let h2 = run_once(&src, false, "run2", simulate_sentinel);


    println!("REPLAY fingerprint_run1=0x{:016x}", h1);
    println!("REPLAY fingerprint_run2=0x{:016x}", h2);

    if h1 == h2 {
        println!("REPLAY RESULT: PASS (deterministic)");
    } else {
        println!("REPLAY RESULT: FAIL (non-deterministic)");
        std::process::exit(1);
    }
}
