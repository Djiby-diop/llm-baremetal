use std::fs;

use osg_memory_warden::dplus::{
    parse,
    verifier::verify,
    ConsensusMode,
    DPlusSection,
    SectionKind,
    SectionTag,
    VerifyOptions,
};

const OUT_NAME: &str = "OOPOLICY.BIN";
const MAGIC: [u8; 4] = *b"OOPL";
const VERSION: u8 = 1;
const MAX_RULES: usize = 32;
const RULE_CAP: usize = 64; // includes trailing NUL
const HEADER_LEN: usize = 16;

fn trim_ascii(s: &str) -> &str {
    let s = s.trim_matches(|c: char| c == ' ' || c == '\t' || c == '\r' || c == '\n');
    s
}

fn strip_utf8_bom(s: &str) -> &str {
    if s.as_bytes().starts_with(&[0xEF, 0xBB, 0xBF]) {
        &s[3..]
    } else {
        s
    }
}

fn strip_inline_comment(mut line: &str) -> &str {
    // Best-effort: match firmware behavior (#) plus common comment prefixes.
    // We keep it conservative: only cut on '#' which is used in D+ examples.
    if let Some(i) = line.find('#') {
        line = &line[..i];
    }
    trim_ascii(line)
}

fn starts_with_ci(s: &str, prefix: &str) -> bool {
    s.len() >= prefix.len() && s[..prefix.len()].eq_ignore_ascii_case(prefix)
}

fn parse_mode(val: &str) -> Option<bool> {
    let v = val.trim();
    if v.eq_ignore_ascii_case("allow") || v.eq_ignore_ascii_case("allow_by_default") {
        Some(true)
    } else if v.eq_ignore_ascii_case("deny") || v.eq_ignore_ascii_case("deny_by_default") {
        Some(false)
    } else {
        None
    }
}

fn normalize_rule(val: &str) -> Option<String> {
    let v = trim_ascii(val);
    if v.is_empty() {
        return None;
    }
    // Only gate /oo* commands.
    if !v.to_ascii_lowercase().starts_with("/oo") {
        return None;
    }
    // Cap to RULE_CAP-1 bytes.
    let mut out = v.to_ascii_lowercase();
    if out.as_bytes().len() >= RULE_CAP {
        out.truncate(RULE_CAP - 1);
    }
    Some(out)
}

fn push_rule(out: &mut Vec<String>, val: &str) {
    if out.len() >= MAX_RULES {
        return;
    }
    if let Some(r) = normalize_rule(val) {
        if !r.is_empty() {
            out.push(r);
        }
    }
}

fn extract_rules(src: &str) -> (bool, Vec<String>, Vec<String>) {
    let mut allow_by_default = false;
    let mut allow: Vec<String> = Vec::new();
    let mut deny: Vec<String> = Vec::new();

    // 1) Parse D+ sections and extract allow/deny only from @@LAW blocks.
    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 64];

    if let Ok(module) = parse(src, &mut scratch) {
        for sec in module.sections {
            if sec.tag.kind() != Some(SectionKind::Law) {
                continue;
            }

            for raw_line in sec.body.lines() {
                let line = strip_inline_comment(trim_ascii(raw_line));
                if line.is_empty() {
                    continue;
                }

                if starts_with_ci(line, "allow") {
                    let rest = trim_ascii(&line[5..]);
                    let tok = rest.split_whitespace().next().unwrap_or("");
                    push_rule(&mut allow, tok);
                } else if starts_with_ci(line, "deny") {
                    let rest = trim_ascii(&line[4..]);
                    let tok = rest.split_whitespace().next().unwrap_or("");
                    push_rule(&mut deny, tok);
                }
            }
        }
    }

    // 2) Legacy helpers supported by the firmware (mode= / allow= / deny= / @@ALLOW / @@DENY)
    for raw_line in src.lines() {
        let line0 = trim_ascii(raw_line);
        if line0.is_empty() {
            continue;
        }
        if line0.starts_with('#') || line0.starts_with(';') || line0.starts_with("//") {
            continue;
        }

        let line = strip_inline_comment(line0);
        if line.is_empty() {
            continue;
        }

        if starts_with_ci(line, "@@allow") {
            push_rule(&mut allow, trim_ascii(&line[7..]));
            continue;
        }
        if starts_with_ci(line, "@@deny") {
            push_rule(&mut deny, trim_ascii(&line[6..]));
            continue;
        }

        if let Some(eq) = line.find('=') {
            let key = trim_ascii(&line[..eq]);
            let val = trim_ascii(&line[eq + 1..]);
            if key.is_empty() || val.is_empty() {
                continue;
            }

            if starts_with_ci(key, "mode") {
                if let Some(m) = parse_mode(val) {
                    allow_by_default = m;
                }
            } else if starts_with_ci(key, "allow") {
                push_rule(&mut allow, val);
            } else if starts_with_ci(key, "deny") {
                push_rule(&mut deny, val);
            }
        }
    }

    (allow_by_default, allow, deny)
}

fn build_blob(allow_by_default: bool, allow: &[String], deny: &[String]) -> Vec<u8> {
    let mut out = vec![0u8; HEADER_LEN + (MAX_RULES * RULE_CAP * 2)];

    // Header
    out[0..4].copy_from_slice(&MAGIC);
    out[4] = VERSION;
    out[5] = if allow_by_default { 1 } else { 0 };
    out[6] = allow.len().min(MAX_RULES) as u8;
    out[7] = deny.len().min(MAX_RULES) as u8;

    // out[8..16] reserved = 0

    let allow_off = HEADER_LEN;
    let deny_off = HEADER_LEN + (MAX_RULES * RULE_CAP);

    for (i, r) in allow.iter().take(MAX_RULES).enumerate() {
        let dst = allow_off + i * RULE_CAP;
        let bytes = r.as_bytes();
        let n = bytes.len().min(RULE_CAP - 1);
        out[dst..dst + n].copy_from_slice(&bytes[..n]);
        out[dst + n] = 0;
    }

    for (i, r) in deny.iter().take(MAX_RULES).enumerate() {
        let dst = deny_off + i * RULE_CAP;
        let bytes = r.as_bytes();
        let n = bytes.len().min(RULE_CAP - 1);
        out[dst..dst + n].copy_from_slice(&bytes[..n]);
        out[dst + n] = 0;
    }

    out
}

fn main() {
    let in_path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_compile_oo <policy.dplus> [out.bin]");
        std::process::exit(2);
    });
    let out_path = std::env::args().nth(2).unwrap_or_else(|| OUT_NAME.to_string());

    let src0 = fs::read_to_string(&in_path).unwrap_or_else(|e| {
        eprintln!("read failed: {e}");
        std::process::exit(2);
    });
    let src = strip_utf8_bom(&src0);

    // Parse + verify D+ strictly (LAW+PROOF consensus).
    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 64];
    let module = parse(src, &mut scratch).unwrap_or_else(|e| {
        eprintln!("parse error: {e}");
        std::process::exit(1);
    });

    let saw_law = module
        .sections
        .iter()
        .any(|s| s.tag.kind() == Some(SectionKind::Law));
    let saw_proof = module
        .sections
        .iter()
        .any(|s| s.tag.kind() == Some(SectionKind::Proof));
    if !saw_law || !saw_proof {
        eprintln!("verify error: missing required @@LAW/@@PROOF sections");
        std::process::exit(1);
    }

    let mut opts = VerifyOptions::strict();
    opts.consensus = ConsensusMode::LawAndProof;
    verify(&module, opts).unwrap_or_else(|e| {
        eprintln!("verify error: {e:?}");
        std::process::exit(1);
    });

    let (allow_by_default, allow, deny) = extract_rules(src);
    let blob = build_blob(allow_by_default, &allow, &deny);
    fs::write(&out_path, &blob).unwrap_or_else(|e| {
        eprintln!("write failed: {e}");
        std::process::exit(2);
    });

    println!(
        "OK: wrote {} (allow={} deny={} mode={})",
        out_path,
        allow.len().min(MAX_RULES),
        deny.len().min(MAX_RULES),
        if allow_by_default {
            "allow_by_default"
        } else {
            "deny_by_default"
        }
    );
}
