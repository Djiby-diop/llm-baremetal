use std::fs;

use osg_memory_warden::dplus::{
    compute_merit_profile, for_each_op, format_reasons_csv, parse, DPlusSection, SectionKind,
    SectionTag,
};

fn fnv1a64(bytes: &[u8]) -> u64 {
    // Deterministic, dependency-free fingerprint.
    const OFFSET_BASIS: u64 = 0xcbf29ce484222325;
    const PRIME: u64 = 0x100000001b3;
    let mut h = OFFSET_BASIS;
    for &b in bytes {
        h ^= b as u64;
        h = h.wrapping_mul(PRIME);
    }
    h
}

fn extract_ops(section_body: &str, out: &mut Vec<u32>) {
    for_each_op(section_body, |op| out.push(op));
}

fn tag_display(tag: SectionTag<'_>) -> String {
    match tag {
        SectionTag::Known(k) => format!("{:?}", k),
        SectionTag::Other(h) => h.to_string(),
    }
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_weaver <file.dplus>");
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

    let mut intent: Option<&str> = None;
    let mut law_ops: Vec<u32> = Vec::new();
    let mut proof_ops: Vec<u32> = Vec::new();

    for sec in module.sections {
        match sec.tag.kind() {
            Some(SectionKind::Law) => extract_ops(sec.body, &mut law_ops),
            Some(SectionKind::Proof) => extract_ops(sec.body, &mut proof_ops),
            _ => {
                if let SectionTag::Other(h) = sec.tag {
                    if h.eq_ignore_ascii_case("INTENT") {
                        intent = Some(sec.body);
                    }
                }
            }
        }
    }

    law_ops.sort_unstable();
    law_ops.dedup();
    proof_ops.sort_unstable();
    proof_ops.dedup();

    println!("DPLUS_WEAVE_IR v0");
    println!("source_bytes={}", module.source.as_bytes().len());
    println!("sections={}", module.sections.len());
    println!("merit_score_0_100={}", merit.score_0_100);
    println!("merit_default_sandbox={}", merit.default_sandbox);
    println!("merit_bytes_cap={:?}", merit.bytes_cap);
    println!("merit_ttl_cap_ms={:?}", merit.ttl_cap_ms);
    println!("merit_reasons={}", reasons);

    if let Some(body) = intent {
        let trimmed = body.trim();
        let preview: String = trimmed.chars().take(240).collect();
        println!("intent_present=true");
        println!("intent_preview={}", preview.replace('\n', "\\n"));
    } else {
        println!("intent_present=false");
    }

    println!("law_ops={:?}", law_ops);
    println!("proof_ops={:?}", proof_ops);

    println!("blocks:");
    for sec in module.sections {
        let tag = tag_display(sec.tag);
        let kind = sec
            .tag
            .kind()
            .map(|k| format!("{:?}", k))
            .unwrap_or_else(|| "Opaque".to_string());
        let bytes = sec.body.as_bytes().len();
        let lines = sec.body.lines().count();
        let hash = fnv1a64(sec.body.as_bytes());
        println!("- tag={} kind={} bytes={} lines={} fnv1a64=0x{:016x}", tag, kind, bytes, lines, hash);
    }
}
