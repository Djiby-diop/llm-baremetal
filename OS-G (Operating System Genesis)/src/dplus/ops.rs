pub fn for_each_op(section_body: &str, mut f: impl FnMut(u32)) {
    // MVP op-id format: occurrences of `op:<digits>`.
    // Heuristic:
    // - ignore anything after `//` on a line
    // - ignore `/* ... */` block comments (can span lines)
    // - ignore string literals delimited by `"` or `'` (handles `\\` escapes)
    // - ignore full-line `#...` / `;...` comments (after optional leading whitespace)
    let bytes = section_body.as_bytes();
    let mut i = 0usize;

    let mut at_line_start = true;
    let mut skip_line = false;
    let mut in_block_comment = false;
    let mut in_string: u8 = 0;
    let mut escape = false;

    #[inline]
    fn is_ident(b: u8) -> bool {
        b.is_ascii_alphanumeric() || b == b'_'
    }

    #[inline]
    fn is_ws(b: u8) -> bool {
        b == b' ' || b == b'\t' || b == b'\r'
    }

    while i < bytes.len() {
        let b = bytes[i];

        if skip_line {
            if b == b'\n' {
                skip_line = false;
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_block_comment {
            if b == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                in_block_comment = false;
                i += 2;
                continue;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_string != 0 {
            if escape {
                escape = false;
                i += 1;
                continue;
            }
            if b == b'\\' {
                escape = true;
                i += 1;
                continue;
            }
            if b == in_string {
                in_string = 0;
                i += 1;
                continue;
            }
            if b == b'\n' {
                // Strings in the embedded languages may be multi-line in theory,
                // but for MVP we still consider newline a line boundary.
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if at_line_start {
            if b == b' ' || b == b'\t' || b == b'\r' {
                i += 1;
                continue;
            }
            if b == b'#' || b == b';' {
                skip_line = true;
                i += 1;
                continue;
            }
            if b == b'\n' {
                i += 1;
                continue;
            }
            at_line_start = false;
        }

        // Comments
        if b == b'/' && i + 1 < bytes.len() {
            let n = bytes[i + 1];
            if n == b'/' {
                skip_line = true;
                i += 2;
                continue;
            }
            if n == b'*' {
                in_block_comment = true;
                i += 2;
                continue;
            }
        }

        // Strings
        if b == b'"' || b == b'\'' {
            in_string = b;
            escape = false;
            i += 1;
            continue;
        }

        // op:<digits> (MVP)
        // Accepts optional spaces around ':' and OP/op casing.
        // Avoids matching inside identifiers (e.g. "noop:7").
        if (b == b'o' || b == b'O')
            && i + 1 < bytes.len()
            && (bytes[i + 1] == b'p' || bytes[i + 1] == b'P')
            && (i == 0 || !is_ident(bytes[i - 1]))
        {
            let mut j = i + 2;
            while j < bytes.len() && is_ws(bytes[j]) {
                j += 1;
            }
            if j < bytes.len() && bytes[j] == b':' {
                j += 1;
                while j < bytes.len() && is_ws(bytes[j]) {
                    j += 1;
                }

                let mut v: u32 = 0;
                let mut any = false;
                while j < bytes.len() {
                    let d = bytes[j];
                    if d.is_ascii_digit() {
                        any = true;
                        v = v.saturating_mul(10).saturating_add((d - b'0') as u32);
                        j += 1;
                    } else {
                        break;
                    }
                }
                if any {
                    f(v);
                    i = j;
                    continue;
                }
            }
        }

        if b == b'\n' {
            at_line_start = true;
        }
        i += 1;
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct SomaIoConfig {
    pub interactive: Option<bool>,
    pub steps: Option<usize>,
    pub layers: Option<usize>,
    pub dim: Option<usize>,
    pub weights_header: Option<bool>,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct WardenMemConfig {
    pub rate_window_ticks: Option<u64>,
    pub rate_limit_bytes: Option<u64>,
}

/// Returns the first non-empty, non-comment line as the policy VM RPN program.
///
/// Intended for use with a `@@WARDEN:POLICY` section.
pub fn extract_warden_policy_rpn_line(section_body: &str) -> Option<&str> {
    for line in section_body.lines() {
        let t = line.trim();
        if t.is_empty() {
            continue;
        }
        if t.starts_with("//") || t.starts_with('#') || t.starts_with(';') {
            continue;
        }
        return Some(t);
    }
    None
}
/// Extract a policy VM RPN program from a `@@WARDEN:POLICY` section.
///
/// This supports multi-line programs by concatenating all non-empty lines,
/// while skipping full-line comments that start with `#` or `;`.
///
/// Notes:
/// - `compile_rpn()` already strips `//` and `/*` line comments.
/// - We avoid heap allocations by writing into `out_buf`.
/// - Returns `None` if the resulting program doesn't fit in `out_buf`.
pub fn extract_warden_policy_rpn<'a>(section_body: &str, out_buf: &'a mut [u8]) -> Option<&'a str> {
    let mut n = 0usize;

    for line in section_body.lines() {
        let t = line.trim();
        if t.is_empty() {
            continue;
        }
        if t.starts_with('#') || t.starts_with(';') {
            continue;
        }

        if n != 0 {
            if n >= out_buf.len() {
                return None;
            }
            out_buf[n] = b'\n';
            n += 1;
        }

        let b = t.as_bytes();
        if n + b.len() > out_buf.len() {
            return None;
        }
        out_buf[n..n + b.len()].copy_from_slice(b);
        n += b.len();
    }

    if n == 0 {
        return None;
    }
    core::str::from_utf8(&out_buf[..n]).ok()
}

pub fn extract_cortex_heur_config(section_body: &str) -> crate::cortex::CortexConfig {
    // MVP key=value format inside @@CORTEX:HEUR section.
    // Supported keys:
    // - enabled={1|0|true|false|on|off}
    // - prefetch_repeat=<digits>
    // Ignores the same comment/string constructs as for_each_op().
    let bytes = section_body.as_bytes();
    let mut i = 0usize;

    let mut at_line_start = true;
    let mut skip_line = false;
    let mut in_block_comment = false;
    let mut in_string: u8 = 0;
    let mut escape = false;

    #[inline]
    fn is_ident(b: u8) -> bool {
        b.is_ascii_alphanumeric() || b == b'_'
    }

    #[inline]
    fn is_ws(b: u8) -> bool {
        b == b' ' || b == b'\t' || b == b'\r'
    }

    #[inline]
    fn upper(v: u8) -> u8 {
        if (b'a'..=b'z').contains(&v) {
            v - 32
        } else {
            v
        }
    }

    #[inline]
    fn eq_ignore_ascii_case_at(h: &[u8], start: usize, needle: &[u8]) -> bool {
        if start + needle.len() > h.len() {
            return false;
        }
        h[start..start + needle.len()]
            .iter()
            .zip(needle.iter())
            .all(|(&a, &b)| upper(a) == upper(b))
    }

    fn parse_bool(h: &[u8], j: usize) -> Option<(bool, usize)> {
        // Accept 1/0, true/false, on/off
        let rest = &h[j..];
        if rest.starts_with(b"1") {
            return Some((true, j + 1));
        }
        if rest.starts_with(b"0") {
            return Some((false, j + 1));
        }
        if rest.len() >= 4 && eq_ignore_ascii_case_at(h, j, b"TRUE") {
            return Some((true, j + 4));
        }
        if rest.len() >= 5 && eq_ignore_ascii_case_at(h, j, b"FALSE") {
            return Some((false, j + 5));
        }
        if rest.len() >= 2 && eq_ignore_ascii_case_at(h, j, b"ON") {
            return Some((true, j + 2));
        }
        if rest.len() >= 3 && eq_ignore_ascii_case_at(h, j, b"OFF") {
            return Some((false, j + 3));
        }
        None
    }

    fn parse_u8(h: &[u8], j: usize) -> Option<(u8, usize)> {
        let mut v: u16 = 0;
        let mut any = false;
        let mut j = j;
        while j < h.len() {
            let c = h[j];
            if !(b'0'..=b'9').contains(&c) {
                break;
            }
            any = true;
            v = v.saturating_mul(10).saturating_add((c - b'0') as u16);
            j += 1;
        }
        if any {
            Some((core::cmp::min(v, u8::MAX as u16) as u8, j))
        } else {
            None
        }
    }

    let mut out = crate::cortex::CortexConfig::default();

    while i < bytes.len() {
        let b = bytes[i];

        if skip_line {
            if b == b'\n' {
                skip_line = false;
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_block_comment {
            if b == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                in_block_comment = false;
                i += 2;
                continue;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_string != 0 {
            if escape {
                escape = false;
                i += 1;
                continue;
            }
            if b == b'\\' {
                escape = true;
                i += 1;
                continue;
            }
            if b == in_string {
                in_string = 0;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        // Line comments
        if b == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
            skip_line = true;
            i += 2;
            continue;
        }
        if at_line_start {
            let mut j = i;
            while j < bytes.len() && is_ws(bytes[j]) {
                j += 1;
            }
            if j < bytes.len() && (bytes[j] == b'#' || bytes[j] == b';') {
                skip_line = true;
                i = j + 1;
                continue;
            }
        }

        // Block comment start
        if b == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'*' {
            in_block_comment = true;
            i += 2;
            continue;
        }

        // Strings
        if b == b'\'' || b == b'"' {
            in_string = b;
            i += 1;
            continue;
        }

        // Key parse
        if is_ident(b) {
            if i > 0 && is_ident(bytes[i - 1]) {
                // inside identifier
                i += 1;
                continue;
            }

            let key = if eq_ignore_ascii_case_at(bytes, i, b"ENABLED") {
                1
            } else if eq_ignore_ascii_case_at(bytes, i, b"PREFETCH_REPEAT") {
                2
            } else {
                0
            };

            if key != 0 {
                let key_len = if key == 1 { 7 } else { 15 };
                let end = i + key_len;
                if end < bytes.len() && is_ident(bytes[end]) {
                    i += 1;
                    continue;
                }

                let mut j = end;
                while j < bytes.len() && is_ws(bytes[j]) {
                    j += 1;
                }
                if j < bytes.len() && bytes[j] == b'=' {
                    j += 1;
                    while j < bytes.len() && is_ws(bytes[j]) {
                        j += 1;
                    }

                    match key {
                        1 => {
                            if let Some((v, nj)) = parse_bool(bytes, j) {
                                out.enabled = v;
                                i = nj;
                                continue;
                            }
                        }
                        2 => {
                            if let Some((v, nj)) = parse_u8(bytes, j) {
                                out.prefetch_repeat = if v == 0 { 1 } else { v };
                                i = nj;
                                continue;
                            }
                        }
                        _ => {}
                    }
                }
            }
        }

        if b == b'\n' {
            at_line_start = true;
        } else {
            at_line_start = false;
        }
        i += 1;
    }

    out
}

pub fn extract_soma_io_config(section_body: &str) -> SomaIoConfig {
    // MVP key=value format inside @@SOMA:IO section.
    // Supported keys:
    // - interactive={1|0|true|false|on|off}
    // - steps=<digits>
    // - layers=<digits>
    // - dim=<digits>
    // - weights_header={1|0|true|false|on|off}
    // Ignores the same comment/string constructs as for_each_op().
    let bytes = section_body.as_bytes();
    let mut i = 0usize;

    let mut at_line_start = true;
    let mut skip_line = false;
    let mut in_block_comment = false;
    let mut in_string: u8 = 0;
    let mut escape = false;

    #[inline]
    fn is_ident(b: u8) -> bool {
        b.is_ascii_alphanumeric() || b == b'_'
    }

    #[inline]
    fn is_ws(b: u8) -> bool {
        b == b' ' || b == b'\t' || b == b'\r'
    }

    #[inline]
    fn upper(v: u8) -> u8 {
        if (b'a'..=b'z').contains(&v) {
            v - 32
        } else {
            v
        }
    }

    #[inline]
    fn eq_ignore_ascii_case_at(h: &[u8], start: usize, needle: &[u8]) -> bool {
        if start + needle.len() > h.len() {
            return false;
        }
        h[start..start + needle.len()]
            .iter()
            .zip(needle.iter())
            .all(|(&a, &b)| upper(a) == upper(b))
    }

    fn parse_usize(h: &[u8], j: usize) -> Option<(usize, usize)> {
        let mut v: usize = 0;
        let mut any = false;
        let mut j = j;
        while j < h.len() {
            let c = h[j];
            if !(b'0'..=b'9').contains(&c) {
                break;
            }
            any = true;
            v = v.saturating_mul(10).saturating_add((c - b'0') as usize);
            j += 1;
        }
        if any {
            Some((v, j))
        } else {
            None
        }
    }

    fn parse_bool(h: &[u8], j: usize) -> Option<(bool, usize)> {
        if j >= h.len() {
            return None;
        }

        // 0/1 quick path
        if h[j] == b'0' {
            return Some((false, j + 1));
        }
        if h[j] == b'1' {
            return Some((true, j + 1));
        }

        const TRUE1: &[u8] = b"TRUE";
        const TRUE2: &[u8] = b"ON";
        const FALSE1: &[u8] = b"FALSE";
        const FALSE2: &[u8] = b"OFF";
        if eq_ignore_ascii_case_at(h, j, TRUE1) {
            return Some((true, j + TRUE1.len()));
        }
        if eq_ignore_ascii_case_at(h, j, TRUE2) {
            return Some((true, j + TRUE2.len()));
        }
        if eq_ignore_ascii_case_at(h, j, FALSE1) {
            return Some((false, j + FALSE1.len()));
        }
        if eq_ignore_ascii_case_at(h, j, FALSE2) {
            return Some((false, j + FALSE2.len()));
        }

        None
    }

    let mut out = SomaIoConfig::default();

    while i < bytes.len() {
        let b = bytes[i];

        if skip_line {
            if b == b'\n' {
                skip_line = false;
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_block_comment {
            if b == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                in_block_comment = false;
                i += 2;
                continue;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_string != 0 {
            if escape {
                escape = false;
                i += 1;
                continue;
            }
            if b == b'\\' {
                escape = true;
                i += 1;
                continue;
            }
            if b == in_string {
                in_string = 0;
                i += 1;
                continue;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if at_line_start {
            if b == b' ' || b == b'\t' || b == b'\r' {
                i += 1;
                continue;
            }
            if b == b'#' || b == b';' {
                skip_line = true;
                i += 1;
                continue;
            }
            if b == b'\n' {
                i += 1;
                continue;
            }
            at_line_start = false;
        }

        // Comments
        if b == b'/' && i + 1 < bytes.len() {
            let n = bytes[i + 1];
            if n == b'/' {
                skip_line = true;
                i += 2;
                continue;
            }
            if n == b'*' {
                in_block_comment = true;
                i += 2;
                continue;
            }
        }

        // Strings
        if b == b'"' || b == b'\'' {
            in_string = b;
            escape = false;
            i += 1;
            continue;
        }

        // key = value
        // Avoid matching inside identifiers.
        if is_ident(b) && (i == 0 || !is_ident(bytes[i.saturating_sub(1)])) {
            const KEY_INTERACTIVE: &[u8] = b"INTERACTIVE";
            const KEY_STEPS: &[u8] = b"STEPS";
            const KEY_LAYERS: &[u8] = b"LAYERS";
            const KEY_DIM: &[u8] = b"DIM";
            const KEY_WEIGHTS_HEADER: &[u8] = b"WEIGHTS_HEADER";

            let (kind, key_len) = if eq_ignore_ascii_case_at(bytes, i, KEY_INTERACTIVE)
                && (i + KEY_INTERACTIVE.len() >= bytes.len() || !is_ident(bytes[i + KEY_INTERACTIVE.len()]))
            {
                (0u8, KEY_INTERACTIVE.len())
            } else if eq_ignore_ascii_case_at(bytes, i, KEY_STEPS)
                && (i + KEY_STEPS.len() >= bytes.len() || !is_ident(bytes[i + KEY_STEPS.len()]))
            {
                (1u8, KEY_STEPS.len())
            } else if eq_ignore_ascii_case_at(bytes, i, KEY_LAYERS)
                && (i + KEY_LAYERS.len() >= bytes.len() || !is_ident(bytes[i + KEY_LAYERS.len()]))
            {
                (2u8, KEY_LAYERS.len())
            } else if eq_ignore_ascii_case_at(bytes, i, KEY_DIM)
                && (i + KEY_DIM.len() >= bytes.len() || !is_ident(bytes[i + KEY_DIM.len()]))
            {
                (3u8, KEY_DIM.len())
            } else if eq_ignore_ascii_case_at(bytes, i, KEY_WEIGHTS_HEADER)
                && (i + KEY_WEIGHTS_HEADER.len() >= bytes.len()
                    || !is_ident(bytes[i + KEY_WEIGHTS_HEADER.len()]))
            {
                (4u8, KEY_WEIGHTS_HEADER.len())
            } else {
                (255u8, 0usize)
            };

            if kind != 255 {
                let mut j = i + key_len;
                while j < bytes.len() && is_ws(bytes[j]) {
                    j += 1;
                }
                if j < bytes.len() && bytes[j] == b'=' {
                    j += 1;
                    while j < bytes.len() && is_ws(bytes[j]) {
                        j += 1;
                    }

                    match kind {
                        0 => {
                            if let Some((v, nj)) = parse_bool(bytes, j) {
                                out.interactive = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        1 => {
                            if let Some((v, nj)) = parse_usize(bytes, j) {
                                out.steps = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        2 => {
                            if let Some((v, nj)) = parse_usize(bytes, j) {
                                out.layers = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        3 => {
                            if let Some((v, nj)) = parse_usize(bytes, j) {
                                out.dim = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        4 => {
                            if let Some((v, nj)) = parse_bool(bytes, j) {
                                out.weights_header = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        _ => {}
                    }
                }
            }
        }

        if b == b'\n' {
            at_line_start = true;
        }
        i += 1;
    }

    out
}

pub fn extract_warden_mem_config(section_body: &str) -> WardenMemConfig {
    // MVP key=value format inside @@WARDEN:MEM section.
    // Supported keys:
    // - rate_window_ticks=<digits>
    // - rate_limit_bytes=<digits>
    // Ignores the same comment/string constructs as for_each_op().
    let bytes = section_body.as_bytes();
    let mut i = 0usize;

    let mut at_line_start = true;
    let mut skip_line = false;
    let mut in_block_comment = false;
    let mut in_string: u8 = 0;
    let mut escape = false;

    #[inline]
    fn is_ident(b: u8) -> bool {
        b.is_ascii_alphanumeric() || b == b'_'
    }

    #[inline]
    fn is_ws(b: u8) -> bool {
        b == b' ' || b == b'\t' || b == b'\r'
    }

    #[inline]
    fn upper(v: u8) -> u8 {
        if (b'a'..=b'z').contains(&v) {
            v - 32
        } else {
            v
        }
    }

    #[inline]
    fn eq_ignore_ascii_case_at(h: &[u8], start: usize, needle: &[u8]) -> bool {
        if start + needle.len() > h.len() {
            return false;
        }
        h[start..start + needle.len()]
            .iter()
            .zip(needle.iter())
            .all(|(&a, &b)| upper(a) == upper(b))
    }

    fn parse_u64(h: &[u8], j: usize) -> Option<(u64, usize)> {
        let mut v: u64 = 0;
        let mut any = false;
        let mut j = j;
        while j < h.len() {
            let c = h[j];
            if !(b'0'..=b'9').contains(&c) {
                break;
            }
            any = true;
            v = v.saturating_mul(10).saturating_add((c - b'0') as u64);
            j += 1;
        }
        if any {
            Some((v, j))
        } else {
            None
        }
    }

    let mut out = WardenMemConfig::default();

    while i < bytes.len() {
        let b = bytes[i];

        if skip_line {
            if b == b'\n' {
                skip_line = false;
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_block_comment {
            if b == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                in_block_comment = false;
                i += 2;
                continue;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if in_string != 0 {
            if escape {
                escape = false;
                i += 1;
                continue;
            }
            if b == b'\\' {
                escape = true;
                i += 1;
                continue;
            }
            if b == in_string {
                in_string = 0;
                i += 1;
                continue;
            }
            if b == b'\n' {
                at_line_start = true;
            }
            i += 1;
            continue;
        }

        if at_line_start {
            if b == b' ' || b == b'\t' || b == b'\r' {
                i += 1;
                continue;
            }
            if b == b'#' || b == b';' {
                skip_line = true;
                i += 1;
                continue;
            }
            if b == b'\n' {
                i += 1;
                continue;
            }
            at_line_start = false;
        }

        // Comments
        if b == b'/' && i + 1 < bytes.len() {
            let n = bytes[i + 1];
            if n == b'/' {
                skip_line = true;
                i += 2;
                continue;
            }
            if n == b'*' {
                in_block_comment = true;
                i += 2;
                continue;
            }
        }

        // Strings
        if b == b'"' || b == b'\'' {
            in_string = b;
            escape = false;
            i += 1;
            continue;
        }

        // key = value
        // Avoid matching inside identifiers.
        if is_ident(b) && (i == 0 || !is_ident(bytes[i.saturating_sub(1)])) {
            const KEY_RATE_WINDOW_TICKS: &[u8] = b"RATE_WINDOW_TICKS";
            const KEY_RATE_LIMIT_BYTES: &[u8] = b"RATE_LIMIT_BYTES";

            let (kind, key_len) = if eq_ignore_ascii_case_at(bytes, i, KEY_RATE_WINDOW_TICKS)
                && (i + KEY_RATE_WINDOW_TICKS.len() >= bytes.len()
                    || !is_ident(bytes[i + KEY_RATE_WINDOW_TICKS.len()]))
            {
                (0u8, KEY_RATE_WINDOW_TICKS.len())
            } else if eq_ignore_ascii_case_at(bytes, i, KEY_RATE_LIMIT_BYTES)
                && (i + KEY_RATE_LIMIT_BYTES.len() >= bytes.len()
                    || !is_ident(bytes[i + KEY_RATE_LIMIT_BYTES.len()]))
            {
                (1u8, KEY_RATE_LIMIT_BYTES.len())
            } else {
                (255u8, 0usize)
            };

            if kind != 255 {
                let mut j = i + key_len;
                while j < bytes.len() && is_ws(bytes[j]) {
                    j += 1;
                }
                if j < bytes.len() && bytes[j] == b'=' {
                    j += 1;
                    while j < bytes.len() && is_ws(bytes[j]) {
                        j += 1;
                    }

                    match kind {
                        0 => {
                            if let Some((v, nj)) = parse_u64(bytes, j) {
                                out.rate_window_ticks = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        1 => {
                            if let Some((v, nj)) = parse_u64(bytes, j) {
                                out.rate_limit_bytes = Some(v);
                                i = nj;
                                continue;
                            }
                        }
                        _ => {}
                    }
                }
            }
        }

        if b == b'\n' {
            at_line_start = true;
        }
        i += 1;
    }

    out
}

#[cfg(test)]
mod tests {
    extern crate std;

    use super::for_each_op;
    use super::extract_warden_policy_rpn;
    use super::extract_soma_io_config;
    use super::extract_warden_mem_config;
    use std::vec::Vec;

    fn collect_ops(s: &str) -> Vec<u32> {
        let mut out = Vec::new();
        for_each_op(s, |op| out.push(op));
        out
    }

    #[test]
    fn ignores_line_comments() {
        let s = "op:1\n// op:2\nop:3\n";
        assert_eq!(collect_ops(s), vec![1, 3]);
    }

    #[test]
    fn ignores_full_line_hash_semicolon_comments() {
        let s = "  # op:2\n; op:9\nop:1\n";
        assert_eq!(collect_ops(s), vec![1]);
    }

    #[test]
    fn ignores_block_comments_single_line() {
        let s = "op:1 /* op:2 */ op:3";
        assert_eq!(collect_ops(s), vec![1, 3]);
    }

    #[test]
    fn ignores_block_comments_multi_line() {
        let s = "op:1 /*\nop:2\n*/ op:3";
        assert_eq!(collect_ops(s), vec![1, 3]);
    }

    #[test]
    fn ignores_double_quoted_strings() {
        let s = "op:1 \"op:2\" op:3";
        assert_eq!(collect_ops(s), vec![1, 3]);
    }

    #[test]
    fn extract_warden_policy_rpn_multiline_concatenates() {
        let body = r#"
        # comment line
          ; another comment

        ctx.intent.bytes push 4096 gt
        ctx.intent.sandboxed push 0 eq and

        "#;

        let mut buf = [0u8; 256];
        let rpn = extract_warden_policy_rpn(body, &mut buf).expect("expected policy");
        assert_eq!(rpn, "ctx.intent.bytes push 4096 gt\nctx.intent.sandboxed push 0 eq and");
    }

    #[test]
    fn ignores_single_quoted_strings() {
        let s = "op:1 'op:2' op:3";
        assert_eq!(collect_ops(s), vec![1, 3]);
    }

    #[test]
    fn handles_escaped_quotes_in_strings() {
        let s = "op:1 \"x \\\"op:2\\\" y\" op:3";
        assert_eq!(collect_ops(s), vec![1, 3]);
    }

    #[test]
    fn accepts_spaces_around_colon() {
        let s = "op : 7\nproof op:\t8\n";
        assert_eq!(collect_ops(s), vec![7, 8]);
    }

    #[test]
    fn accepts_uppercase_op() {
        let s = "OP:9\n";
        assert_eq!(collect_ops(s), vec![9]);
    }

    #[test]
    fn does_not_match_inside_identifiers() {
        let s = "noop:7 coop:8 op:9";
        assert_eq!(collect_ops(s), vec![9]);
    }

    #[test]
    fn soma_io_parses_basic_kv() {
        let s = "interactive=1\nsteps=42\nlayers=7\ndim=128\nweights_header=1\n";
        let cfg = extract_soma_io_config(s);
        assert_eq!(cfg.interactive, Some(true));
        assert_eq!(cfg.steps, Some(42));
        assert_eq!(cfg.layers, Some(7));
        assert_eq!(cfg.dim, Some(128));
        assert_eq!(cfg.weights_header, Some(true));
    }

    #[test]
    fn soma_io_allows_layers_zero() {
        let s = "layers=0\n";
        let cfg = extract_soma_io_config(s);
        assert_eq!(cfg.layers, Some(0));
    }

    #[test]
    fn soma_io_parses_bool_variants() {
        let s = "interactive=true\n";
        assert_eq!(extract_soma_io_config(s).interactive, Some(true));
        let s = "interactive=on\n";
        assert_eq!(extract_soma_io_config(s).interactive, Some(true));
        let s = "interactive=0\n";
        assert_eq!(extract_soma_io_config(s).interactive, Some(false));
        let s = "interactive=off\n";
        assert_eq!(extract_soma_io_config(s).interactive, Some(false));

        let s = "weights_header=1\n";
        assert_eq!(extract_soma_io_config(s).weights_header, Some(true));
        let s = "weights_header=false\n";
        assert_eq!(extract_soma_io_config(s).weights_header, Some(false));
    }

    #[test]
    fn soma_io_ignores_comments_and_strings() {
        let s = "// interactive=1\nsteps=10\n/* layers=9 */\n\"interactive=0\"\n";
        let cfg = extract_soma_io_config(s);
        assert_eq!(cfg.interactive, None);
        assert_eq!(cfg.steps, Some(10));
        assert_eq!(cfg.layers, None);
    }

    #[test]
    fn soma_io_does_not_match_inside_identifiers() {
        let s = "xinteractive=1\nsteps=3\n";
        let cfg = extract_soma_io_config(s);
        assert_eq!(cfg.interactive, None);
        assert_eq!(cfg.steps, Some(3));
    }

    #[test]
    fn warden_mem_parses_basic_kv() {
        let s = "rate_window_ticks=10\nrate_limit_bytes=4096\n";
        let cfg = extract_warden_mem_config(s);
        assert_eq!(cfg.rate_window_ticks, Some(10));
        assert_eq!(cfg.rate_limit_bytes, Some(4096));
    }

    #[test]
    fn warden_mem_ignores_comments_and_strings() {
        let s = "rate_window_ticks=10\n// rate_limit_bytes=123\nrate_limit_bytes=4096\n'x rate_window_ticks=999'\n";
        let cfg = extract_warden_mem_config(s);
        assert_eq!(cfg.rate_window_ticks, Some(10));
        assert_eq!(cfg.rate_limit_bytes, Some(4096));
    }
}
