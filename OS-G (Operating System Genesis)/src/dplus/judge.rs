#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
pub struct LawMemAllocate {
    pub op_id: Option<u32>,
    pub bytes: Option<u64>,
    pub ttl_ms: Option<u64>,
    pub sandbox: Option<bool>,
}

#[inline]
fn ascii_upper(b: u8) -> u8 {
    if (b'a'..=b'z').contains(&b) {
        b - 32
    } else {
        b
    }
}

fn contains_ignore_ascii_case(haystack: &str, needle: &str) -> bool {
    let h = haystack.as_bytes();
    let n = needle.as_bytes();
    if n.is_empty() {
        return true;
    }
    if n.len() > h.len() {
        return false;
    }
    for i in 0..=(h.len() - n.len()) {
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

fn strip_line_comment(line: &str) -> &str {
    let trimmed = line.trim_start();
    if trimmed.starts_with('#') || trimmed.starts_with(';') {
        return "";
    }

    // We only need a line-level heuristic here (no allocations, no multi-line state).
    // Truncate at the earliest comment marker.
    let mut cut = line.len();
    if let Some(pos) = line.find("//") {
        cut = core::cmp::min(cut, pos);
    }
    if let Some(pos) = line.find("/*") {
        cut = core::cmp::min(cut, pos);
    }
    &line[..cut]
}

fn parse_u64_digits(mut s: &str) -> Option<u64> {
    s = s.trim_start();
    let mut v: u64 = 0;
    let mut any = false;
    for c in s.chars() {
        if c.is_ascii_digit() {
            any = true;
            v = v.saturating_mul(10).saturating_add((c as u64) - ('0' as u64));
        } else {
            break;
        }
    }
    any.then_some(v)
}

fn parse_op_id(line: &str) -> Option<u32> {
    let b = line.as_bytes();

    #[inline]
    fn is_ident(x: u8) -> bool {
        x.is_ascii_alphanumeric() || x == b'_'
    }

    #[inline]
    fn is_ws(x: u8) -> bool {
        x == b' ' || x == b'\t' || x == b'\r'
    }

    let mut i = 0usize;
    while i + 1 < b.len() {
        let c0 = b[i];
        let c1 = b[i + 1];
        if (c0 == b'o' || c0 == b'O') && (c1 == b'p' || c1 == b'P') && (i == 0 || !is_ident(b[i - 1])) {
            let mut j = i + 2;
            while j < b.len() && is_ws(b[j]) {
                j += 1;
            }
            if j < b.len() && b[j] == b':' {
                j += 1;
                while j < b.len() && is_ws(b[j]) {
                    j += 1;
                }
                let mut v: u32 = 0;
                let mut any = false;
                while j < b.len() {
                    let d = b[j];
                    if d.is_ascii_digit() {
                        any = true;
                        v = v.saturating_mul(10).saturating_add((d - b'0') as u32);
                        j += 1;
                    } else {
                        break;
                    }
                }
                if any {
                    return Some(v);
                }
            }
        }
        i += 1;
    }

    None
}

fn parse_key_le_value(line: &str, key: &str) -> Option<u64> {
    // Parse patterns like: `key <= 123` or `key<=123`, ascii case-insensitive.
    // We keep this intentionally permissive and heuristic.
    let h = line.as_bytes();
    let k = key.as_bytes();
    if k.is_empty() || k.len() > h.len() {
        return None;
    }

    for i in 0..=(h.len() - k.len()) {
        let mut ok = true;
        for j in 0..k.len() {
            if ascii_upper(h[i + j]) != ascii_upper(k[j]) {
                ok = false;
                break;
            }
        }
        if !ok {
            continue;
        }

        let mut p = i + k.len();
        while p < h.len() && (h[p] == b' ' || h[p] == b'\t') {
            p += 1;
        }

        if p >= h.len() || h[p] != b'<' {
            continue;
        }
        p += 1;
        while p < h.len() && (h[p] == b' ' || h[p] == b'\t') {
            p += 1;
        }
        if p >= h.len() || h[p] != b'=' {
            continue;
        }
        p += 1;

        let after = &line[p..];
        return parse_u64_digits(after);
    }

    None
}

pub fn extract_mem_allocate_rules(law_body: &str, out: &mut [LawMemAllocate]) -> usize {
    let res = extract_mem_allocate_rules_ex(law_body, out);
    res.n
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct LawSentinelRule {
    pub violation_threshold: u32,
    pub action_quarantine: bool,
}

impl Default for LawSentinelRule {
    fn default() -> Self {
        Self {
            violation_threshold: 3,
            action_quarantine: true,
        }
    }
}

pub fn extract_sentinel_rules(law_body: &str, out: &mut [LawSentinelRule]) -> usize {
    let mut n = 0usize;
    for raw_line in law_body.lines() {
        let line = strip_line_comment(raw_line).trim();
        if line.is_empty() {
            continue;
        }
        
        // syntax: rule behavior.access_denied count<=3
        // or:     allow behavior.access_denied count<=3  (implies >3 is bad)
        if !contains_ignore_ascii_case(line, "behavior.access_denied") {
            continue;
        }

        let mut r = LawSentinelRule::default();

        // If rule specifies "count<=N", we take that as the threshold.
        // e.g. count<=5 means 6 is quarantine.
        if let Some(val) = parse_key_le_value(line, "count") {
             r.violation_threshold = val as u32;
        }

        // Optional: verify action. For now default is quarantine.
        // If someone wanted to disable quarantine they'd need syntax we don't support yet.

        if n < out.len() {
            out[n] = r;
            n += 1;
        } else {
            break; // buffer full
        }
    }
    n
}

#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
pub(crate) struct ExtractRulesResult {
    pub n: usize,
    pub truncated: bool,
}

pub(crate) fn extract_mem_allocate_rules_ex(law_body: &str, out: &mut [LawMemAllocate]) -> ExtractRulesResult {
    let mut n = 0usize;
    let mut truncated = false;

    for raw_line in law_body.lines() {
        let line = strip_line_comment(raw_line).trim();
        if line.is_empty() {
            continue;
        }

        // Heuristic: treat lines mentioning mem.allocate as memory intents.
        if !contains_ignore_ascii_case(line, "mem.allocate") {
            continue;
        }

        let mut r = LawMemAllocate::default();
        r.op_id = parse_op_id(line);
        if r.op_id.is_none() {
            // Avoid producing ambiguous/wildcard rules.
            continue;
        }
        r.bytes = parse_key_le_value(line, "bytes");
        r.ttl_ms = parse_key_le_value(line, "ttl_ms");

        // Sandbox/zone hint (heuristic MVP)
        if contains_ignore_ascii_case(line, "sandbox=true")
            || contains_ignore_ascii_case(line, "sandbox = true")
        {
            r.sandbox = Some(true);
        } else if contains_ignore_ascii_case(line, "sandbox=false")
            || contains_ignore_ascii_case(line, "sandbox = false")
        {
            r.sandbox = Some(false);
        } else if contains_ignore_ascii_case(line, "zone==sandbox")
            || contains_ignore_ascii_case(line, "zone == sandbox")
        {
            r.sandbox = Some(true);
        } else if contains_ignore_ascii_case(line, "zone!=sandbox")
            || contains_ignore_ascii_case(line, "zone != sandbox")
        {
            r.sandbox = Some(false);
        } else if contains_ignore_ascii_case(line, "zone==normal")
            || contains_ignore_ascii_case(line, "zone == normal")
        {
            r.sandbox = Some(false);
        }

        if n < out.len() {
            out[n] = r;
            n += 1;
        } else {
            truncated = true;
        }
    }

    ExtractRulesResult { n, truncated }
}

#[cfg(test)]
mod tests {
    extern crate std;

    use super::*;

    #[test]
    fn extracts_rules_and_parses_key_values() {
        let s = "allow mem.allocate op:7 bytes<=8192 ttl_ms <= 2000 zone == sandbox\n";
        let mut out = [LawMemAllocate::default(); 4];
        let n = extract_mem_allocate_rules(s, &mut out);
        assert_eq!(n, 1);
        assert_eq!(out[0].op_id, Some(7));
        assert_eq!(out[0].bytes, Some(8192));
        assert_eq!(out[0].ttl_ms, Some(2000));
        assert_eq!(out[0].sandbox, Some(true));
    }

    #[test]
    fn parses_op_with_spaces_and_uppercase() {
        let s = "allow mem.allocate OP : 42 bytes<=1 ttl_ms<=2 zone==normal\n";
        let mut out = [LawMemAllocate::default(); 4];
        let n = extract_mem_allocate_rules(s, &mut out);
        assert_eq!(n, 1);
        assert_eq!(out[0].op_id, Some(42));
        assert_eq!(out[0].bytes, Some(1));
        assert_eq!(out[0].ttl_ms, Some(2));
        assert_eq!(out[0].sandbox, Some(false));
    }

    #[test]
    fn ignores_line_comments_and_non_allocate_lines() {
        let s = "// allow mem.allocate op:1 bytes<=1\npermit mem.read op:2\nallow mem.allocate op:3 bytes<=4\n";
        let mut out = [LawMemAllocate::default(); 4];
        let n = extract_mem_allocate_rules(s, &mut out);
        assert_eq!(n, 1);
        assert_eq!(out[0].op_id, Some(3));
    }

    #[test]
    fn truncates_block_comments_on_line() {
        let s = "allow mem.allocate op:8 bytes<=1 zone==normal /* op:998 ignored */\n";
        let mut out = [LawMemAllocate::default(); 4];
        let n = extract_mem_allocate_rules(s, &mut out);
        assert_eq!(n, 1);
        assert_eq!(out[0].op_id, Some(8));
        assert_eq!(out[0].bytes, Some(1));
        assert_eq!(out[0].sandbox, Some(false));
    }

    #[test]
    fn ignores_allocate_lines_without_op_id() {
        let s = "allow mem.allocate bytes<=8192 ttl_ms<=2000 zone==sandbox\n";
        let mut out = [LawMemAllocate::default(); 4];
        let n = extract_mem_allocate_rules(s, &mut out);
        assert_eq!(n, 0);
    }
}
