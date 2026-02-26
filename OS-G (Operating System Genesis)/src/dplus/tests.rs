use super::{parse, ConsensusMode, SectionKind, VerifyOptions};
use crate::dplus::verifier::verify;

extern crate std;

#[test]
fn parse_sections() {
    let src = "@@LAW\nallow mem.allocate op:1\n@@PROOF\ninvariant op:1\n@@SPEED\nmov rax, rbx\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 8];
    let module = parse(src, &mut scratch).unwrap();
    assert_eq!(module.sections.len(), 3);
    assert_eq!(module.sections[0].tag.kind(), Some(SectionKind::Law));
    assert_eq!(module.sections[1].tag.kind(), Some(SectionKind::Proof));
    assert_eq!(module.sections[2].tag.kind(), Some(SectionKind::Speed));
}

#[test]
fn parse_arbitrary_language_sections() {
    let src = "@@LANG:python\nprint('hi')\n@@GPU:ptx\n// ptx\n@@LAW\nallow op:1\n@@PROOF\nprove op:1\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 16];
    let module = parse(src, &mut scratch).unwrap();
    assert_eq!(module.sections.len(), 4);
    match module.sections[0].tag {
        super::module::SectionTag::Other(h) => assert_eq!(h, "LANG:python"),
        _ => panic!("expected Other"),
    }
    match module.sections[1].tag {
        super::module::SectionTag::Other(h) => assert_eq!(h, "GPU:ptx"),
        _ => panic!("expected Other"),
    }
    assert_eq!(module.sections[2].tag.kind(), Some(SectionKind::Law));
    assert_eq!(module.sections[3].tag.kind(), Some(SectionKind::Proof));
}

#[test]
fn parse_fabric_block_sections_with_nested_braces() {
    let src = "[SOMA:C] {\nint f() {\n  if (x) {\n    return 1;\n  }\n  return 0;\n}\n}\n[LAW] {\nallow op:1\n}\n[PROOF] {\nprove op:1\n}\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 16];
    let module = parse(src, &mut scratch).unwrap();
    assert_eq!(module.sections.len(), 3);
    match module.sections[0].tag {
        super::module::SectionTag::Other(h) => assert_eq!(h, "SOMA:C"),
        _ => panic!("expected Other"),
    }
    assert!(module.sections[0].body.contains("int f()"));
    assert_eq!(module.sections[1].tag.kind(), Some(SectionKind::Law));
    assert_eq!(module.sections[2].tag.kind(), Some(SectionKind::Proof));
}

#[test]
fn parse_mixed_header_styles() {
    let src = "@@LAW\nallow op:7\n[PROOF] {\nprove op:7\n}\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 8];
    let module = parse(src, &mut scratch).unwrap();
    assert_eq!(module.sections.len(), 2);
    assert_eq!(module.sections[0].tag.kind(), Some(SectionKind::Law));
    assert_eq!(module.sections[1].tag.kind(), Some(SectionKind::Proof));
}

#[test]
fn verify_ok_with_consensus() {
    let src = "@@LAW\nallow mem.allocate op:7\n@@PROOF\nproof op:7\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 4];
    let module = parse(src, &mut scratch).unwrap();
    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::LawAndProof;
    verify(&module, opts).unwrap();
}

#[test]
fn verify_fails_missing_proof() {
    let src = "@@LAW\nallow mem.allocate op:9\n@@PROOF\nproof op:8\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 4];
    let module = parse(src, &mut scratch).unwrap();
    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::LawAndProof;
    let err = verify(&module, opts).err().unwrap();
    assert_eq!(err, super::VerifyError::MissingProofForLawOp { op: 9 });
}

#[test]
fn verify_rejects_forbidden_token_in_law() {
    let src = "@@LAW\nwhile true { op:1 }\n@@PROOF\nok op:1\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 4];
    let module = parse(src, &mut scratch).unwrap();
    let err = verify(&module, VerifyOptions::strict()).err().unwrap();
    assert_eq!(
        err,
        super::VerifyError::ForbiddenToken {
            section: SectionKind::Law,
            line: 1,
            token: crate::dplus::verifier::ForbiddenToken::While,
        }
    );
}

#[test]
fn verify_rejects_too_many_unique_law_ops() {
    // Build a LAW section with >256 unique ops.
    let mut src = String::from("@@LAW\n");
    for i in 0..300u32 {
        src.push_str("allow op:");
        src.push_str(&i.to_string());
        src.push('\n');
    }
    src.push_str("@@PROOF\n");
    src.push_str("proof op:0\n");

    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 64];
    let module = parse(&src, &mut scratch).unwrap();
    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::LawAndProof;
    let err = verify(&module, opts).err().unwrap();
    assert_eq!(err, super::VerifyError::TooManyLawOps);
}

#[test]
fn verify_rejects_too_many_unique_proof_ops() {
    let mut src = String::from("@@LAW\nallow mem.allocate op:0\n@@PROOF\n");
    for i in 0..300u32 {
        src.push_str("proof op:");
        src.push_str(&i.to_string());
        src.push('\n');
    }

    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 64];
    let module = parse(&src, &mut scratch).unwrap();
    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::LawAndProof;
    let err = verify(&module, opts).err().unwrap();
    assert_eq!(err, super::VerifyError::TooManyProofOps);
}

#[test]
fn verify_rejects_too_many_law_mem_allocate_rules() {
    let mut src = String::from("@@LAW\n");
    for i in 0..70u32 {
        src.push_str("allow mem.allocate op:");
        src.push_str(&i.to_string());
        src.push_str(" bytes<=1 ttl_ms<=1 zone==normal\n");
    }
    src.push_str("@@PROOF\nproof op:0\n");

    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 128];
    let module = parse(&src, &mut scratch).unwrap();

    let err = verify(&module, VerifyOptions::strict()).err().unwrap();
    assert_eq!(err, super::VerifyError::TooManyLawMemAllocateRules);
}

#[test]
fn verify_rejects_mem_allocate_bytes_too_large() {
    let src = "@@LAW\nallow mem.allocate op:7 bytes<=999999999 ttl_ms<=1\n@@PROOF\nproof op:7\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 16];
    let module = parse(src, &mut scratch).unwrap();

    let err = verify(&module, VerifyOptions::strict()).err().unwrap();
    assert_eq!(
        err,
        super::VerifyError::MemAllocateBytesTooLarge {
            op: 7,
            bytes: 999_999_999,
            max: 16 * 1024 * 1024,
        }
    );
}

#[test]
fn verify_rejects_mem_allocate_ttl_too_large() {
    let src = "@@LAW\nallow mem.allocate op:7 bytes<=1 ttl_ms<=999999999\n@@PROOF\nproof op:7\n";
    let mut scratch = [super::DPlusSection { tag: super::module::SectionTag::Known(SectionKind::Unknown), body: "" }; 16];
    let module = parse(src, &mut scratch).unwrap();

    let err = verify(&module, VerifyOptions::strict()).err().unwrap();
    assert_eq!(
        err,
        super::VerifyError::MemAllocateTtlTooLarge {
            op: 7,
            ttl_ms: 999_999_999,
            max: 3_600_000,
        }
    );
}
