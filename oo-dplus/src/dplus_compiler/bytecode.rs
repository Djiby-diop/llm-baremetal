// src/dplus_compiler/bytecode.rs
//! D++ Bytecode: Intermediate Representation

use super::polyglot::{EmbeddedLanguage, ForeignBlock};

#[derive(Debug, Clone, PartialEq)]
pub enum Bytecode {
    // Stack operations
    LoadConst(f64),
    LoadStr(String),
    LoadBool(bool),
    LoadArg(usize),
    LoadLocal(usize),
    StoreLocal(usize),

    // Arithmetic
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,

    // Logic
    And,
    Or,
    Not,

    // Comparison
    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,

    // Control flow
    Jump(usize),           // PC offset
    JumpIfFalse(usize),
    JumpIfTrue(usize),
    Call(String, usize),   // function name, arg count
    Return,

    // Consensus
    ConsensusVote,
    MergeVerdicts,

    // Special
    Nop,
    Halt,
    Panic(String),
}

#[derive(Debug, Clone)]
pub struct BytecodeFunction {
    pub name: String,
    pub arity: usize,
    pub locals: usize,
    pub code: Vec<Bytecode>,
}

#[derive(Debug, Clone)]
pub struct BytecodeModule {
    pub functions: std::collections::HashMap<String, BytecodeFunction>,
    pub foreign_blocks: Vec<ForeignBlock>,
    pub entrypoint: String,
}

impl BytecodeModule {
    pub fn new(entrypoint: &str) -> Self {
        BytecodeModule {
            functions: std::collections::HashMap::new(),
            foreign_blocks: Vec::new(),
            entrypoint: entrypoint.to_string(),
        }
    }

    pub fn add_function(&mut self, func: BytecodeFunction) {
        self.functions.insert(func.name.clone(), func);
    }

    pub fn add_foreign_block(&mut self, block: ForeignBlock) {
        self.foreign_blocks.push(block);
    }

    pub fn foreign_blocks_for(
        &self,
        language: super::polyglot::EmbeddedLanguage,
    ) -> impl Iterator<Item = &ForeignBlock> {
        self.foreign_blocks
            .iter()
            .filter(move |block| block.language == language)
    }

    pub fn has_foreign_blocks(&self) -> bool {
        !self.foreign_blocks.is_empty()
    }

    pub fn serialize(&self) -> Vec<u8> {
        // Compact binary format (DPP2), preserving foreign blocks.
        // Functions are still not serialized in this phase.
        let mut out = Vec::new();
        out.extend_from_slice(b"DPP2");

        write_u16(&mut out, self.entrypoint.len() as u16);
        out.extend_from_slice(self.entrypoint.as_bytes());

        write_u32(&mut out, self.foreign_blocks.len() as u32);
        for block in &self.foreign_blocks {
            out.push(language_to_u8(block.language));
            write_u32(&mut out, block.code.len() as u32);
            out.extend_from_slice(block.code.as_bytes());
        }

        out
    }

    pub fn deserialize(data: &[u8]) -> Result<Self, String> {
        if data.len() >= 4 && &data[0..4] == b"DPP2" {
            let mut cursor = 4usize;

            let entry_len = read_u16(data, &mut cursor)? as usize;
            let entry_bytes = read_bytes(data, &mut cursor, entry_len)?;
            let entrypoint = std::str::from_utf8(entry_bytes)
                .map_err(|e| format!("Invalid utf8 entrypoint: {}", e))?;

            let foreign_count = read_u32(data, &mut cursor)? as usize;
            let mut module = BytecodeModule::new(entrypoint);

            for _ in 0..foreign_count {
                let lang_tag = read_u8(data, &mut cursor)?;
                let language = u8_to_language(lang_tag)?;
                let code_len = read_u32(data, &mut cursor)? as usize;
                let code_bytes = read_bytes(data, &mut cursor, code_len)?;
                let code = String::from_utf8(code_bytes.to_vec())
                    .map_err(|e| format!("Invalid utf8 code payload: {}", e))?;
                let block = ForeignBlock::new(language, code)
                    .map_err(|e| format!("Invalid foreign block: {}", e))?;
                module.add_foreign_block(block);
            }

            return Ok(module);
        }

        deserialize_legacy_dpp1(data)
    }
}

fn language_to_u8(language: EmbeddedLanguage) -> u8 {
    match language {
        EmbeddedLanguage::Python => 1,
        EmbeddedLanguage::Rust => 2,
        EmbeddedLanguage::Prolog => 3,
        EmbeddedLanguage::CudaKernel => 4,
        EmbeddedLanguage::OpenClKernel => 5,
        EmbeddedLanguage::AsmX86_64 => 6,
    }
}

fn u8_to_language(tag: u8) -> Result<EmbeddedLanguage, String> {
    match tag {
        1 => Ok(EmbeddedLanguage::Python),
        2 => Ok(EmbeddedLanguage::Rust),
        3 => Ok(EmbeddedLanguage::Prolog),
        4 => Ok(EmbeddedLanguage::CudaKernel),
        5 => Ok(EmbeddedLanguage::OpenClKernel),
        6 => Ok(EmbeddedLanguage::AsmX86_64),
        _ => Err(format!("Unknown embedded language tag: {}", tag)),
    }
}

fn write_u16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn write_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn read_u8(data: &[u8], cursor: &mut usize) -> Result<u8, String> {
    if *cursor >= data.len() {
        return Err("Unexpected EOF while reading u8".into());
    }
    let v = data[*cursor];
    *cursor += 1;
    Ok(v)
}

fn read_u16(data: &[u8], cursor: &mut usize) -> Result<u16, String> {
    let bytes = read_bytes(data, cursor, 2)?;
    Ok(u16::from_le_bytes([bytes[0], bytes[1]]))
}

fn read_u32(data: &[u8], cursor: &mut usize) -> Result<u32, String> {
    let bytes = read_bytes(data, cursor, 4)?;
    Ok(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

fn read_bytes<'a>(data: &'a [u8], cursor: &mut usize, len: usize) -> Result<&'a [u8], String> {
    let end = cursor.saturating_add(len);
    if end > data.len() {
        return Err(format!(
            "Unexpected EOF while reading {} bytes at offset {}",
            len, *cursor
        ));
    }
    let slice = &data[*cursor..end];
    *cursor = end;
    Ok(slice)
}

fn deserialize_legacy_dpp1(data: &[u8]) -> Result<BytecodeModule, String> {
    let text = std::str::from_utf8(data).map_err(|e| format!("Invalid utf8 payload: {}", e))?;
    let mut lines = text.lines();

    let header = lines.next().ok_or_else(|| "Missing bytecode header".to_string())?;
    if header != "DPP1" {
        return Err(format!("Unsupported bytecode header: {}", header));
    }

    let entrypoint_line = lines
        .next()
        .ok_or_else(|| "Missing entrypoint line".to_string())?;
    let entrypoint = entrypoint_line
        .strip_prefix("entrypoint:")
        .ok_or_else(|| "Malformed entrypoint line".to_string())?;

    let foreign_count_line = lines
        .next()
        .ok_or_else(|| "Missing foreign_blocks line".to_string())?;
    let expected_foreign = foreign_count_line
        .strip_prefix("foreign_blocks:")
        .ok_or_else(|| "Malformed foreign_blocks line".to_string())?
        .parse::<usize>()
        .map_err(|e| format!("Invalid foreign block count: {}", e))?;

    let mut module = BytecodeModule::new(entrypoint);
    for line in lines {
        if line.trim().is_empty() {
            continue;
        }

        let rest = line
            .strip_prefix("fb|")
            .ok_or_else(|| format!("Malformed foreign block line: {}", line))?;
        let mut parts = rest.splitn(2, '|');
        let lang_raw = parts
            .next()
            .ok_or_else(|| format!("Missing language in line: {}", line))?;
        let code_hex = parts
            .next()
            .ok_or_else(|| format!("Missing payload in line: {}", line))?;

        let lang = EmbeddedLanguage::parse(lang_raw)
            .ok_or_else(|| format!("Unknown embedded language: {}", lang_raw))?;
        let code_bytes = hex_decode(code_hex)?;
        let code = String::from_utf8(code_bytes)
            .map_err(|e| format!("Invalid utf8 code payload: {}", e))?;
        let block = ForeignBlock::new(lang, code)
            .map_err(|e| format!("Invalid foreign block: {}", e))?;
        module.add_foreign_block(block);
    }

    if module.foreign_blocks.len() != expected_foreign {
        return Err(format!(
            "Foreign block count mismatch: expected {}, got {}",
            expected_foreign,
            module.foreign_blocks.len()
        ));
    }

    Ok(module)
}

fn hex_decode(hex: &str) -> Result<Vec<u8>, String> {
    if !hex.len().is_multiple_of(2) {
        return Err("Hex payload must have an even length".to_string());
    }

    let mut out = Vec::with_capacity(hex.len() / 2);
    let bytes = hex.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let hi = hex_nibble(bytes[i]).ok_or_else(|| {
            format!("Invalid hex character '{}'", bytes[i] as char)
        })?;
        let lo = hex_nibble(bytes[i + 1]).ok_or_else(|| {
            format!("Invalid hex character '{}'", bytes[i + 1] as char)
        })?;
        out.push((hi << 4) | lo);
        i += 2;
    }

    Ok(out)
}

fn hex_nibble(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::dplus_compiler::EmbeddedLanguage;

    #[test]
    fn test_bytecode_module() {
        let module = BytecodeModule::new("main");
        assert_eq!(module.entrypoint, "main");
        assert!(module.foreign_blocks.is_empty());
        assert!(!module.has_foreign_blocks());
    }

    #[test]
    fn test_foreign_block_lookup() {
        let mut module = BytecodeModule::new("main");
        module.add_foreign_block(ForeignBlock::new(
            EmbeddedLanguage::Python,
            "print('hi')",
        ).unwrap());
        module.add_foreign_block(ForeignBlock::new(
            EmbeddedLanguage::Prolog,
            "can_allocate(X) :- X > 0.",
        ).unwrap());

        let python_blocks: Vec<_> = module
            .foreign_blocks_for(EmbeddedLanguage::Python)
            .collect();

        assert_eq!(python_blocks.len(), 1);
        assert_eq!(python_blocks[0].language, EmbeddedLanguage::Python);
    }

    #[test]
    fn test_serialize_deserialize_foreign_blocks_roundtrip() {
        let mut module = BytecodeModule::new("main");
        module.add_foreign_block(
            ForeignBlock::new(EmbeddedLanguage::Python, "print('hi')\nprint('again')").unwrap(),
        );
        module.add_foreign_block(
            ForeignBlock::new(EmbeddedLanguage::Prolog, "can_allocate(X) :- X > 0.").unwrap(),
        );

        let bytes = module.serialize();
        let restored = BytecodeModule::deserialize(&bytes).unwrap();

        assert_eq!(restored.entrypoint, "main");
        assert_eq!(restored.foreign_blocks.len(), 2);
        assert_eq!(restored.foreign_blocks[0].language, EmbeddedLanguage::Python);
        assert!(restored.foreign_blocks[0].code.contains("again"));
        assert_eq!(restored.foreign_blocks[1].language, EmbeddedLanguage::Prolog);
    }

    #[test]
    fn test_deserialize_legacy_dpp1_payload() {
        let legacy = b"DPP1\nentrypoint:main\nforeign_blocks:1\nfb|python|7072696e74282768692729\n";
        let restored = BytecodeModule::deserialize(legacy).unwrap();

        assert_eq!(restored.entrypoint, "main");
        assert_eq!(restored.foreign_blocks.len(), 1);
        assert_eq!(restored.foreign_blocks[0].language, EmbeddedLanguage::Python);
        assert_eq!(restored.foreign_blocks[0].code, "print('hi')");
    }
}
