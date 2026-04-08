use std::alloc::{alloc, dealloc, Layout};
use std::fs;

use osg_memory_warden::dplus::{parse, DPlusSection, SectionKind, SectionTag};
use osg_memory_warden::{Access, MemIntent, MemoryWarden, Rights};

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

#[derive(Copy, Clone, Debug, Default)]
struct Rule {
    op_id: Option<u32>,
    bytes: Option<u64>,
    ttl_ms: Option<u64>,
    sandbox: Option<bool>,
}

fn parse_u64_after(haystack: &str, needle: &str) -> Option<u64> {
    let pos = haystack.find(needle)?;
    let after = &haystack[pos + needle.len()..];
    let after = after.trim_start();
    let mut v: u64 = 0;
    let mut any = false;
    for c in after.chars() {
        if c.is_ascii_digit() {
            any = true;
            v = v
                .saturating_mul(10)
                .saturating_add((c as u64) - ('0' as u64));
        } else {
            break;
        }
    }
    any.then_some(v)
}

fn parse_u32_after(haystack: &str, needle: &str) -> Option<u32> {
    parse_u64_after(haystack, needle).and_then(|v| u32::try_from(v).ok())
}

fn extract_rules(law_body: &str) -> Vec<Rule> {
    let mut out = Vec::new();
    for line in law_body.lines() {
        let l = line.trim();
        if l.is_empty() {
            continue;
        }
        if !l.to_ascii_lowercase().contains("mem.allocate") {
            continue;
        }

        let mut r = Rule::default();
        r.op_id = parse_u32_after(l, "op:");
        r.bytes = parse_u64_after(l, "bytes<=").or_else(|| parse_u64_after(l, "bytes <= "));
        r.ttl_ms = parse_u64_after(l, "ttl_ms<=").or_else(|| parse_u64_after(l, "ttl_ms <= "));

        let low = l.to_ascii_lowercase();
        if low.contains("sandbox=true") || low.contains("sandbox = true") {
            r.sandbox = Some(true);
        } else if low.contains("sandbox=false") || low.contains("sandbox = false") {
            r.sandbox = Some(false);
        } else if low.contains("zone==sandbox") || low.contains("zone == sandbox") {
            r.sandbox = Some(true);
        } else if low.contains("zone!=sandbox") || low.contains("zone != sandbox") {
            r.sandbox = Some(false);
        } else if low.contains("zone==normal") || low.contains("zone == normal") {
            r.sandbox = Some(false);
        }

        out.push(r);
    }
    out
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_heal <file.dplus>");
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

    const OWNER: u32 = 1;
    const PAGE_ALIGN: usize = 4096;

    // Separate zones so we can demonstrate sandbox routing.
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

    let mut caps = Vec::new();
    let mut any = false;

    for sec in module.sections {
        if sec.tag.kind() != Some(SectionKind::Law) {
            continue;
        }
        let rules = extract_rules(sec.body);
        for rule in rules {
            any = true;
            let bytes = rule.bytes.unwrap_or(4096);
            let ttl_ticks = rule.ttl_ms.unwrap_or(0);
            let sandbox = rule.sandbox.unwrap_or(false);
            let label = rule.op_id.unwrap_or(0);

            let mut intent = MemIntent::new(OWNER, bytes);
            intent.ttl_ticks = ttl_ticks; // host demo: 1 tick == 1ms
            intent.sandbox = sandbox;
            intent.label = label;
            intent.rights = Rights::R.union(Rights::W);

            match warden.allocate(intent) {
                Ok(cap) => {
                    let (start, _) = warden.cap_range(cap).expect("cap_range");
                    warden
                        .check_access(cap, Access::Read, start, 1)
                        .expect("access");
                    println!(
                        "ALLOC ok op={:?} cap=0x{:08x} bytes={} ttl_ms={} sandbox={}",
                        rule.op_id,
                        cap.raw(),
                        bytes,
                        ttl_ticks,
                        sandbox
                    );
                    caps.push(cap);
                }
                Err(e) => {
                    println!(
                        "ALLOC denied op={:?} bytes={} ttl_ms={} sandbox={} err={:?}",
                        rule.op_id, bytes, ttl_ticks, sandbox, e
                    );
                }
            }
        }
    }

    if !any {
        println!("NO_RULES: no LAW lines containing mem.allocate");
        return;
    }

    // Snapshot the "pure" state.
    let snap = warden.snapshot().expect("snapshot");
    println!("SNAPSHOT ok caps={}", caps.len());

    // Simulate corruption: quarantine the cell and advance time.
    warden.quarantine_cell(OWNER).expect("quarantine");
    warden.tick(1);

    let mut denied_after_quarantine = 0usize;
    for &cap in &caps {
        let (start, _) = warden.cap_range(cap).expect("cap_range");
        if warden.check_access(cap, Access::Read, start, 1).is_err() {
            denied_after_quarantine += 1;
        }
    }
    println!("QUARANTINE applied denied_caps={}", denied_after_quarantine);

    // Heal: restore snapshot.
    warden.restore(&snap).expect("restore");

    let mut ok_after_restore = 0usize;
    for &cap in &caps {
        let (start, _) = warden.cap_range(cap).expect("cap_range");
        if warden.check_access(cap, Access::Read, start, 1).is_ok() {
            ok_after_restore += 1;
        }
    }

    println!("HEAL restore_ok_caps={}", ok_after_restore);
}
