use crate::dplus::module::{DPlusModule, SectionKind};
use crate::dplus::ops::for_each_op;
use crate::dplus::judge::{extract_mem_allocate_rules_ex, LawMemAllocate};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum ConsensusMode {
    Off,
    LawAndProof,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct VerifyOptions {
    pub max_total_bytes: usize,
    pub max_section_bytes: usize,
    pub max_lines_per_section: usize,
    pub max_line_len: usize,
    pub consensus: ConsensusMode,
    pub max_law_mem_allocate_rules: usize,
    pub max_mem_allocate_bytes: u64,
    pub max_mem_allocate_ttl_ms: u64,
}

impl VerifyOptions {
    pub const fn strict() -> Self {
        Self {
            max_total_bytes: 64 * 1024,
            max_section_bytes: 32 * 1024,
            max_lines_per_section: 2048,
            max_line_len: 512,
            consensus: ConsensusMode::LawAndProof,
            max_law_mem_allocate_rules: 64,
            max_mem_allocate_bytes: 16 * 1024 * 1024,
            max_mem_allocate_ttl_ms: 3_600_000,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum VerifyError {
    TooLarge,
    SectionTooLarge,
    TooManyLines,
    LineTooLong,
    TooManyLawOps,
    TooManyProofOps,
    TooManyLawMemAllocateRules,
    MemAllocateBytesZero { op: u32 },
    MemAllocateBytesTooLarge { op: u32, bytes: u64, max: u64 },
    MemAllocateTtlTooLarge { op: u32, ttl_ms: u64, max: u64 },
    MissingProofForLawOp { op: u32 },
    MissingLawForProofOp { op: u32 },
    ForbiddenToken {
        section: SectionKind,
        line: u16,
        token: ForbiddenToken,
    },
}

fn iter_lines(section_body: &str) -> impl Iterator<Item = &str> {
    section_body.lines()
}

fn line_len_ok(line: &str, max_len: usize) -> bool {
    line.as_bytes().len() <= max_len
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum ForbiddenToken {
    While,
    For,
    Loop,
    Goto,
    Asm,
}

fn find_forbidden_token(line: &str) -> Option<ForbiddenToken> {
    // MVP verifier: prohibit obvious unbounded constructs in LAW/PROOF.
    // (This is a heuristic placeholder for a real BPF-style verifier.)

    #[inline]
    fn ascii_upper(b: u8) -> u8 {
        if (b'a'..=b'z').contains(&b) {
            b - 32
        } else {
            b
        }
    }

    #[inline]
    fn is_ident(b: u8) -> bool {
        b.is_ascii_alphanumeric() || b == b'_'
    }

    fn contains_keyword_ignore_ascii_case(haystack: &str, needle: &str) -> bool {
        let h = haystack.as_bytes();
        let n = needle.as_bytes();
        if n.is_empty() {
            return true;
        }
        if n.len() > h.len() {
            return false;
        }

        for i in 0..=(h.len() - n.len()) {
            if i > 0 && is_ident(h[i - 1]) {
                continue;
            }
            if i + n.len() < h.len() && is_ident(h[i + n.len()]) {
                continue;
            }

            let mut ok = true;
            for j in 0..n.len() {
                if ascii_upper(h[i + j]) != ascii_upper(n[j]) {
                    ok = false;
                    break;
                }
            }
            if ok {
                return true;
            }
        }
        false
    }

    if contains_keyword_ignore_ascii_case(line, "WHILE") {
        return Some(ForbiddenToken::While);
    }
    if contains_keyword_ignore_ascii_case(line, "FOR") {
        return Some(ForbiddenToken::For);
    }
    if contains_keyword_ignore_ascii_case(line, "LOOP") {
        return Some(ForbiddenToken::Loop);
    }
    if contains_keyword_ignore_ascii_case(line, "GOTO") {
        return Some(ForbiddenToken::Goto);
    }
    if contains_keyword_ignore_ascii_case(line, "ASM") {
        return Some(ForbiddenToken::Asm);
    }
    None
}

fn append_ops_bounded(section_body: &str, out: &mut [u32], n: &mut usize) -> bool {
    // Appends ops into `out` starting at `*n`.
    // Keeps only unique op ids.
    // Returns true if unique ops were truncated (overflow).
    let mut overflow = false;
    for_each_op(section_body, |op| {
        if out[..*n].iter().any(|&x| x == op) {
            return;
        }
        if *n < out.len() {
            out[*n] = op;
            *n += 1;
        } else {
            overflow = true;
        }
    });
    overflow
}

fn sort_dedup_u32(slice: &mut [u32]) -> usize {
    if slice.is_empty() {
        return 0;
    }

    slice.sort_unstable();
    let mut w = 1usize;
    for i in 1..slice.len() {
        if slice[i] != slice[w - 1] {
            slice[w] = slice[i];
            w += 1;
        }
    }
    w
}

pub fn verify(module: &DPlusModule<'_>, opts: VerifyOptions) -> Result<(), VerifyError> {
    if module.source.as_bytes().len() > opts.max_total_bytes {
        return Err(VerifyError::TooLarge);
    }

    // section-local checks
    for sec in module.sections {
        let body = sec.body;
        if body.as_bytes().len() > opts.max_section_bytes {
            return Err(VerifyError::SectionTooLarge);
        }

        let mut lines = 0usize;
        for line in iter_lines(body) {
            lines += 1;
            if lines > opts.max_lines_per_section {
                return Err(VerifyError::TooManyLines);
            }
            if !line_len_ok(line, opts.max_line_len) {
                return Err(VerifyError::LineTooLong);
            }

            if matches!(sec.tag.kind(), Some(SectionKind::Law | SectionKind::Proof)) {
                if let Some(tok) = find_forbidden_token(line) {
                    let k = sec.tag.kind().unwrap_or(SectionKind::Unknown);
                    let line_u16 = core::cmp::min(lines, u16::MAX as usize) as u16;
                    return Err(VerifyError::ForbiddenToken {
                        section: k,
                        line: line_u16,
                        token: tok,
                    });
                }
            }
        }
    }

    // LAW value bounds (DoS hardening): validate mem.allocate rules without allocating.
    // We only validate what we can parse deterministically.
    const RULE_BUF_CAP: usize = 64;
    let cap = core::cmp::min(opts.max_law_mem_allocate_rules, RULE_BUF_CAP);
    if cap > 0 {
        let mut rules = [LawMemAllocate::default(); RULE_BUF_CAP];
        for sec in module.sections {
            if sec.tag.kind() != Some(SectionKind::Law) {
                continue;
            }

            let res = extract_mem_allocate_rules_ex(sec.body, &mut rules[..cap]);
            if res.truncated {
                return Err(VerifyError::TooManyLawMemAllocateRules);
            }

            for r in &rules[..res.n] {
                let op = r.op_id.unwrap_or(0);

                if let Some(bytes) = r.bytes {
                    if bytes == 0 {
                        return Err(VerifyError::MemAllocateBytesZero { op });
                    }
                    if bytes > opts.max_mem_allocate_bytes {
                        return Err(VerifyError::MemAllocateBytesTooLarge {
                            op,
                            bytes,
                            max: opts.max_mem_allocate_bytes,
                        });
                    }
                }

                if let Some(ttl_ms) = r.ttl_ms {
                    if ttl_ms != 0 && ttl_ms > opts.max_mem_allocate_ttl_ms {
                        return Err(VerifyError::MemAllocateTtlTooLarge {
                            op,
                            ttl_ms,
                            max: opts.max_mem_allocate_ttl_ms,
                        });
                    }
                }
            }
        }
    }

    // consensus checks
    if opts.consensus == ConsensusMode::LawAndProof {
        let mut law_ops = [0u32; 256];
        let mut proof_ops = [0u32; 256];
        let mut n_law = 0usize;
        let mut n_proof = 0usize;
        let mut overflow_law = false;
        let mut overflow_proof = false;

        for sec in module.sections {
            match sec.tag.kind() {
                Some(SectionKind::Law) => {
                    overflow_law |= append_ops_bounded(sec.body, &mut law_ops, &mut n_law);
                }
                Some(SectionKind::Proof) => {
                    overflow_proof |= append_ops_bounded(sec.body, &mut proof_ops, &mut n_proof);
                }
                _ => {}
            }
        }

        if overflow_law {
            return Err(VerifyError::TooManyLawOps);
        }
        if overflow_proof {
            return Err(VerifyError::TooManyProofOps);
        }

        let n_law_unique = sort_dedup_u32(&mut law_ops[..n_law]);
        let n_proof_unique = sort_dedup_u32(&mut proof_ops[..n_proof]);

        // Compare as sets (sorted unique). Keep error priority stable:
        // 1) LAW must be covered by PROOF
        // 2) PROOF must not introduce unknown ops

        // Pass 1: LAW ⊆ PROOF
        let mut i = 0usize;
        let mut j = 0usize;
        while i < n_law_unique && j < n_proof_unique {
            let a = law_ops[i];
            let b = proof_ops[j];
            if a == b {
                i += 1;
                j += 1;
            } else if a < b {
                return Err(VerifyError::MissingProofForLawOp { op: a });
            } else {
                // proof has an extra op (smaller than next LAW op); defer reporting to pass 2
                j += 1;
            }
        }
        if i < n_law_unique {
            return Err(VerifyError::MissingProofForLawOp { op: law_ops[i] });
        }

        // Pass 2: PROOF ⊆ LAW
        let mut i = 0usize;
        let mut j = 0usize;
        while j < n_proof_unique && i < n_law_unique {
            let a = law_ops[i];
            let b = proof_ops[j];
            if a == b {
                i += 1;
                j += 1;
            } else if b < a {
                return Err(VerifyError::MissingLawForProofOp { op: b });
            } else {
                i += 1;
            }
        }
        if j < n_proof_unique {
            return Err(VerifyError::MissingLawForProofOp { op: proof_ops[j] });
        }
    }

    Ok(())
}
