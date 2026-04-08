use core::fmt;

use super::{DPlusModule, SectionKind, SectionTag};

#[derive(Copy, Clone, Eq, PartialEq, Default)]
pub struct MeritReasons(u32);

impl MeritReasons {
    pub const SOMA: Self = Self(1 << 0);
    pub const GPU: Self = Self(1 << 1);
    pub const PROTECT: Self = Self(1 << 2);
    pub const PROOF: Self = Self(1 << 3);

    pub const fn empty() -> Self {
        Self(0)
    }

    pub const fn bits(self) -> u32 {
        self.0
    }

    pub const fn from_bits_truncate(bits: u32) -> Self {
        // Keep only known bits.
        Self(bits & (Self::SOMA.0 | Self::GPU.0 | Self::PROTECT.0 | Self::PROOF.0))
    }

    pub const fn contains(self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }

    pub fn insert(&mut self, other: Self) {
        self.0 |= other.0;
    }
}

#[derive(Copy, Clone, Eq, PartialEq)]
pub struct MeritProfile {
    pub score_0_100: u8,
    pub default_sandbox: bool,
    pub bytes_cap: Option<u64>,
    pub ttl_cap_ms: Option<u64>,
    pub reasons: MeritReasons,
}

impl fmt::Debug for MeritProfile {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("MeritProfile")
            .field("score_0_100", &self.score_0_100)
            .field("default_sandbox", &self.default_sandbox)
            .field("bytes_cap", &self.bytes_cap)
            .field("ttl_cap_ms", &self.ttl_cap_ms)
            .field("reasons_bits", &self.reasons.bits())
            .finish()
    }
}

pub fn format_reasons_csv(reasons: MeritReasons, out: &mut [u8]) -> &str {
    // Writes ASCII like: "SOMA,GPU,PROOF".
    // If out is too small, output is truncated.
    let mut n = 0usize;
    let mut first = true;

    fn push_str(dst: &mut [u8], n: &mut usize, s: &str) {
        let bytes = s.as_bytes();
        let max = dst.len().saturating_sub(*n);
        let take = bytes.len().min(max);
        if take > 0 {
            dst[*n..*n + take].copy_from_slice(&bytes[..take]);
            *n += take;
        }
    }

    fn push_sep(dst: &mut [u8], n: &mut usize, first: &mut bool) {
        if *first {
            *first = false;
        } else {
            push_str(dst, n, ",");
        }
    }

    if reasons.contains(MeritReasons::SOMA) {
        push_sep(out, &mut n, &mut first);
        push_str(out, &mut n, "SOMA");
    }
    if reasons.contains(MeritReasons::GPU) {
        push_sep(out, &mut n, &mut first);
        push_str(out, &mut n, "GPU");
    }
    if reasons.contains(MeritReasons::PROTECT) {
        push_sep(out, &mut n, &mut first);
        push_str(out, &mut n, "PROTECT");
    }
    if reasons.contains(MeritReasons::PROOF) {
        push_sep(out, &mut n, &mut first);
        push_str(out, &mut n, "PROOF");
    }

    if n == 0 {
        // Avoid empty string for UX.
        push_str(out, &mut n, "NONE");
    }

    core::str::from_utf8(&out[..n]).unwrap_or("<utf8>")
}

#[inline]
fn ascii_upper(b: u8) -> u8 {
    if (b'a'..=b'z').contains(&b) {
        b - 32
    } else {
        b
    }
}

#[inline]
fn eq_ignore_ascii_case(a: &str, b: &str) -> bool {
    if a.len() != b.len() {
        return false;
    }
    a.as_bytes()
        .iter()
        .zip(b.as_bytes().iter())
        .all(|(&x, &y)| ascii_upper(x) == ascii_upper(y))
}

#[inline]
fn starts_with_ignore_ascii_case(s: &str, prefix: &str) -> bool {
    if s.len() < prefix.len() {
        return false;
    }
    s.as_bytes()
        .iter()
        .take(prefix.len())
        .zip(prefix.as_bytes().iter())
        .all(|(&x, &y)| ascii_upper(x) == ascii_upper(y))
}

pub fn compute_merit_profile(module: &DPlusModule<'_>) -> MeritProfile {
    // Heuristic MVP:
    // - SOMA:* and GPU:* raise risk ("muscle")
    // - PROTECT:* lowers risk ("armor")
    // - PROOF lowers risk (declared invariants)
    // Derived policy:
    // - low score => default sandbox + caps on bytes/ttl

    let mut risk: i32 = 0;
    let mut has_proof = false;
    let mut has_protect = false;
    let mut reasons = MeritReasons::empty();

    for sec in module.sections {
        match sec.tag.kind() {
            Some(SectionKind::Proof) => has_proof = true,
            _ => {
                if let SectionTag::Other(h) = sec.tag {
                    if starts_with_ignore_ascii_case(h, "SOMA:") || eq_ignore_ascii_case(h, "SOMA") {
                        reasons.insert(MeritReasons::SOMA);
                        risk += 3;
                    }
                    if starts_with_ignore_ascii_case(h, "GPU:") {
                        reasons.insert(MeritReasons::GPU);
                        risk += 2;
                    }
                    if eq_ignore_ascii_case(h, "PROTECT:RUST") || starts_with_ignore_ascii_case(h, "PROTECT:") {
                        has_protect = true;
                        reasons.insert(MeritReasons::PROTECT);
                        risk -= 2;
                    }
                }
            }
        }
    }

    if has_proof {
        reasons.insert(MeritReasons::PROOF);
        risk -= 2;
    }

    // Map risk -> score.
    let score: i32 = if risk <= 0 {
        90
    } else if risk <= 2 {
        70
    } else if risk <= 4 {
        50
    } else {
        25
    };

    // Policy derived from score.
    let (default_sandbox, bytes_cap, ttl_cap_ms): (bool, Option<u64>, Option<u64>) = if score >= 70 {
        (false, None::<u64>, None::<u64>)
    } else if score >= 50 {
        (true, Some(128u64 * 1024u64), Some(2000u64))
    } else {
        (true, Some(64u64 * 1024u64), Some(500u64))
    };

    // If protection is present, relax slightly.
    let (default_sandbox, bytes_cap, ttl_cap_ms) = if has_protect && score < 70 {
        (
            default_sandbox,
            bytes_cap.map(|b| b.saturating_mul(2)),
            ttl_cap_ms.map(|t| t.saturating_mul(2)),
        )
    } else {
        (default_sandbox, bytes_cap, ttl_cap_ms)
    };

    MeritProfile {
        score_0_100: score as u8,
        default_sandbox,
        bytes_cap,
        ttl_cap_ms,
        reasons,
    }
}

pub fn apply_caps(bytes: u64, ttl_ms: u64, profile: MeritProfile) -> (u64, u64) {
    let bytes = profile.bytes_cap.map(|cap| bytes.min(cap)).unwrap_or(bytes);
    let ttl_ms = match profile.ttl_cap_ms {
        Some(cap) => {
            if ttl_ms == 0 {
                // ttl_ms=0 means "no expiry" at the warden level.
                // Treat it as an unbounded request and clamp to the cap when one exists.
                cap
            } else {
                ttl_ms.min(cap)
            }
        }
        None => ttl_ms,
    };
    (bytes, ttl_ms)
}

#[cfg(test)]
mod tests {
    extern crate std;

    use super::*;

    #[test]
    fn ttl_zero_is_clamped_when_cap_exists() {
        let profile = MeritProfile {
            score_0_100: 50,
            default_sandbox: true,
            bytes_cap: None,
            ttl_cap_ms: Some(2000),
            reasons: MeritReasons::empty(),
        };

        let (_bytes, ttl) = apply_caps(123, 0, profile);
        assert_eq!(ttl, 2000);
    }

    #[test]
    fn ttl_zero_stays_infinite_without_cap() {
        let profile = MeritProfile {
            score_0_100: 90,
            default_sandbox: false,
            bytes_cap: None,
            ttl_cap_ms: None,
            reasons: MeritReasons::empty(),
        };

        let (_bytes, ttl) = apply_caps(123, 0, profile);
        assert_eq!(ttl, 0);
    }
}
