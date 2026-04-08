use core::fmt;

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

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SectionKind {
    Law,
    Proof,
    Speed,
    Logic,
    Unknown,
}

impl SectionKind {
    pub fn from_header(header: &str) -> SectionKind {
        let h = header.trim();
        if eq_ignore_ascii_case(h, "LAW") {
            SectionKind::Law
        } else if eq_ignore_ascii_case(h, "PROOF") {
            SectionKind::Proof
        } else if eq_ignore_ascii_case(h, "SPEED") {
            SectionKind::Speed
        } else if eq_ignore_ascii_case(h, "LOGIC") {
            SectionKind::Logic
        } else {
            SectionKind::Unknown
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SectionTag<'a> {
    Known(SectionKind),
    /// Arbitrary tag name (e.g. "LANG:python", "GPU:ptx", "FORMAL:z3").
    Other(&'a str),
}

impl<'a> SectionTag<'a> {
    pub fn from_header(header: &'a str) -> Self {
        let h = header.trim();
        if eq_ignore_ascii_case(h, "LAW") {
            SectionTag::Known(SectionKind::Law)
        } else if eq_ignore_ascii_case(h, "PROOF") {
            SectionTag::Known(SectionKind::Proof)
        } else if eq_ignore_ascii_case(h, "SPEED") {
            SectionTag::Known(SectionKind::Speed)
        } else if eq_ignore_ascii_case(h, "LOGIC") {
            SectionTag::Known(SectionKind::Logic)
        } else {
            SectionTag::Other(h)
        }
    }

    pub fn kind(self) -> Option<SectionKind> {
        match self {
            SectionTag::Known(k) => Some(k),
            SectionTag::Other(_) => None,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct DPlusSection<'a> {
    pub tag: SectionTag<'a>,
    pub body: &'a str,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct DPlusModule<'a> {
    pub source: &'a str,
    pub sections: &'a [DPlusSection<'a>],
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum ParseError {
    TooManySections,
    MissingHeader,
    UnclosedBlock,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ParseError::TooManySections => write!(f, "too many sections"),
            ParseError::MissingHeader => write!(f, "missing section header"),
            ParseError::UnclosedBlock => write!(f, "unclosed [TAG] {{ ... }} block"),
        }
    }
}

fn parse_block_header<'a>(trimmed_line: &'a str) -> Option<&'a str> {
    // Surface syntax (vision): `[TAG] { ... }`
    // We accept any TAG, including namespaced forms like `SOMA:C`.
    // Must contain `]` and be followed by `{` (after optional whitespace).
    if !trimmed_line.starts_with('[') {
        return None;
    }
    let close = trimmed_line.find(']')?;
    if close <= 1 {
        return None;
    }
    let tag = trimmed_line[1..close].trim();
    if tag.is_empty() {
        return None;
    }
    let after = trimmed_line[close + 1..].trim_start();
    if !after.starts_with('{') {
        return None;
    }
    Some(tag)
}

/// Parse a D+ document into sections.
///
/// Format (MVP):
/// - Section headers are lines starting with `@@`:
///   - `@@LAW`, `@@PROOF`, `@@SPEED`, `@@LOGIC`
/// - Everything until next header belongs to that section.
///
/// Parsing is allocation-free: caller provides `out_sections` scratch.
pub fn parse<'a>(source: &'a str, out_sections: &'a mut [DPlusSection<'a>]) -> Result<DPlusModule<'a>, ParseError> {
    let mut count = 0usize;
    let mut current_tag: Option<SectionTag<'a>> = None;
    let mut current_start: usize = 0;

    // When parsing `[TAG] { ... }` blocks, we count brace depth so inner C/Rust
    // braces don't prematurely terminate the section.
    let mut in_block = false;
    let mut brace_depth: i32 = 0;

    let bytes = source.as_bytes();
    let mut line_start = 0usize;
    let mut i = 0usize;

    while i <= bytes.len() {
        let at_end = i == bytes.len();
        let is_nl = !at_end && bytes[i] == b'\n';

        if at_end || is_nl {
            let line = &source[line_start..i];
            let trimmed = line.trim();

            if in_block {
                // Update brace depth and close the block when it reaches 0.
                // We operate on bytes: only ASCII braces matter here.
                if let Some(_tag) = current_tag {
                    for (pos, b) in line.as_bytes().iter().copied().enumerate() {
                        match b {
                            b'{' => brace_depth += 1,
                            b'}' => {
                                brace_depth -= 1;
                                if brace_depth == 0 {
                                    if count >= out_sections.len() {
                                        return Err(ParseError::TooManySections);
                                    }
                                    // Body ends right before this closing brace.
                                    out_sections[count] = DPlusSection {
                                        tag: current_tag.unwrap(),
                                        body: &source[current_start..(line_start + pos)],
                                    };
                                    count += 1;
                                    current_tag = None;
                                    in_block = false;
                                    break;
                                }
                            }
                            _ => {}
                        }
                    }
                }
            } else {
                if trimmed.starts_with("@@") {
                    // close previous section
                    if let Some(tag) = current_tag {
                        if count >= out_sections.len() {
                            return Err(ParseError::TooManySections);
                        }
                        out_sections[count] = DPlusSection {
                            tag,
                            body: &source[current_start..line_start],
                        };
                        count += 1;
                    }

                    let header = trimmed.trim_start_matches("@@").trim();
                    current_tag = Some(SectionTag::from_header(header));
                    current_start = i + if at_end { 0 } else { 1 };
                } else if let Some(tag_header) = parse_block_header(trimmed) {
                    // close previous section
                    if let Some(tag) = current_tag {
                        if count >= out_sections.len() {
                            return Err(ParseError::TooManySections);
                        }
                        out_sections[count] = DPlusSection {
                            tag,
                            body: &source[current_start..line_start],
                        };
                        count += 1;
                    }

                    current_tag = Some(SectionTag::from_header(tag_header));
                    current_start = i + if at_end { 0 } else { 1 };
                    in_block = true;
                    brace_depth = 1;
                }
            }

            line_start = i + 1;
        }

        i += 1;
    }

    if in_block {
        return Err(ParseError::UnclosedBlock);
    }

    // At least one section must have been produced.
    // Note: documents can end right after a `[TAG] { ... }` block, in which case
    // `current_tag` is None but `count > 0`.
    if count == 0 && current_tag.is_none() {
        return Err(ParseError::MissingHeader);
    }

    // close final section
    if let Some(tag) = current_tag {
        if count >= out_sections.len() {
            return Err(ParseError::TooManySections);
        }
        out_sections[count] = DPlusSection {
            tag,
            body: &source[current_start..source.len()],
        };
        count += 1;
    }

    Ok(DPlusModule {
        source,
        sections: &out_sections[..count],
    })
}
