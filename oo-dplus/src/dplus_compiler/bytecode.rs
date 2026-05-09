// src/dplus_compiler/bytecode.rs
//! D++ Bytecode: Intermediate Representation

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
    pub entrypoint: String,
}

impl BytecodeModule {
    pub fn new(entrypoint: &str) -> Self {
        BytecodeModule {
            functions: std::collections::HashMap::new(),
            entrypoint: entrypoint.to_string(),
        }
    }

    pub fn add_function(&mut self, func: BytecodeFunction) {
        self.functions.insert(func.name.clone(), func);
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

    #[test]
    fn test_bytecode_module() {
        let module = BytecodeModule::new("main");
        assert_eq!(module.entrypoint, "main");
    }
}
