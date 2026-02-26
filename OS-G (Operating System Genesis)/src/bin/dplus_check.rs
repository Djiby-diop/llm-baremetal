use std::fs;

use osg_memory_warden::dplus::{parse, verifier::verify, ConsensusMode, DPlusSection, SectionKind, SectionTag, VerifyOptions};

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_check <file.dplus>");
        std::process::exit(2);
    });

    let src = fs::read_to_string(&path).unwrap_or_else(|e| {
        eprintln!("read failed: {e}");
        std::process::exit(2);
    });

    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 64];

    let module = parse(&src, &mut scratch).unwrap_or_else(|e| {
        eprintln!("parse error: {e}");
        std::process::exit(1);
    });

    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::LawAndProof;

    verify(&module, opts).unwrap_or_else(|e| {
        eprintln!("verify error: {e:?}");
        std::process::exit(1);
    });

    println!("OK: sections={} bytes={}", module.sections.len(), module.source.len());
}
