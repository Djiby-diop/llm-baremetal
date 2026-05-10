// src/dplus_compiler/bytecode.rs
//! D++ Bytecode: Intermediate Representation

use super::polyglot::ForeignBlock;

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
        // Simple serialization: for now, just output a placeholder
        // In reality, this would be binary format
        format!("D++ {:?}", self.entrypoint).into_bytes()
    }

    pub fn deserialize(data: &[u8]) -> Result<Self, String> {
        // Placeholder deserialization
        Ok(BytecodeModule::new("main"))
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
}
