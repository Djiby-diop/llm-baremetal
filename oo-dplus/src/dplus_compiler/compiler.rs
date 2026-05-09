// src/dplus_compiler/compiler.rs
//! D+ Compiler: AST → Bytecode

use super::ast::*;
use super::bytecode::*;
use super::CompileError;
use std::collections::HashMap;

pub struct Compiler {
    functions: HashMap<String, BytecodeFunction>,
    current_function: Option<String>,
    bytecode: Vec<Bytecode>,
    local_count: usize,
}

impl Compiler {
    pub fn new() -> Self {
        Compiler {
            functions: HashMap::new(),
            current_function: None,
            bytecode: Vec::new(),
            local_count: 0,
        }
    }

    pub fn compile(&mut self, module: &DplusModule) -> Result<BytecodeModule, CompileError> {
        let mut bc_module = BytecodeModule::new("main");

        for section in &module.sections {
            match section {
                Section::Genome { .. } => {
                    // Genome sections don't produce bytecode
                }
                Section::Rights { rights } => {
                    self.compile_rights(rights)?;
                }
                Section::Duties { duties } => {
                    self.compile_duties(duties)?;
                }
                Section::Verdicts { verdicts } => {
                    self.compile_verdicts(verdicts)?;
                }
                Section::Law { rules } => {
                    self.compile_law(rules)?;
                }
                Section::Proof { invariants } => {
                    self.compile_proof(invariants)?;
                }
                Section::Judge { body } => {
                    self.compile_judge(body)?;
                }
                Section::Heal { body } => {
                    self.compile_heal(body)?;
                }
                Section::Emergency { body } => {
                    self.compile_emergency(body)?;
                }
            }
        }

        // Finalize bytecode module
        for (name, func) in &self.functions {
            bc_module.add_function(func.clone());
        }

        Ok(bc_module)
    }

    fn compile_rights(&mut self, rights: &[(String, String)]) -> Result<(), CompileError> {
        for (name, _desc) in rights {
            let func = BytecodeFunction {
                name: format!("right_{}", name),
                arity: 1,
                locals: 1,
                code: vec![Bytecode::Return],
            };
            self.functions.insert(func.name.clone(), func);
        }
        Ok(())
    }

    fn compile_duties(&mut self, duties: &[(String, String)]) -> Result<(), CompileError> {
        for (name, _desc) in duties {
            let func = BytecodeFunction {
                name: format!("duty_{}", name),
                arity: 1,
                locals: 1,
                code: vec![Bytecode::Return],
            };
            self.functions.insert(func.name.clone(), func);
        }
        Ok(())
    }

    fn compile_verdicts(&mut self, verdicts: &[String]) -> Result<(), CompileError> {
        for verdict in verdicts {
            let func = BytecodeFunction {
                name: format!("verdict_{}", verdict),
                arity: 0,
                locals: 0,
                code: vec![Bytecode::Return],
            };
            self.functions.insert(func.name.clone(), func);
        }
        Ok(())
    }

    fn compile_law(&mut self, rules: &[Rule]) -> Result<(), CompileError> {
        for rule in rules {
            let mut code = Vec::new();
            
            // For now, emit simple return true for each rule
            code.push(Bytecode::LoadBool(true));
            code.push(Bytecode::Return);

            let func = BytecodeFunction {
                name: format!("law_{}", rule.head),
                arity: rule.params.len(),
                locals: rule.params.len(),
                code,
            };
            self.functions.insert(func.name.clone(), func);
        }
        Ok(())
    }

    fn compile_proof(&mut self, invariants: &[Invariant]) -> Result<(), CompileError> {
        for (idx, _inv) in invariants.iter().enumerate() {
            let code = vec![Bytecode::LoadBool(true), Bytecode::Return];
            let func = BytecodeFunction {
                name: format!("proof_{}", idx),
                arity: 0,
                locals: 0,
                code,
            };
            self.functions.insert(func.name.clone(), func);
        }
        Ok(())
    }

    fn compile_judge(&mut self, _body: &str) -> Result<(), CompileError> {
        // Judge section: compile from Rust to bytecode would require
        // an actual Rust compiler integration; for now, stub it
        let func = BytecodeFunction {
            name: "judge".to_string(),
            arity: 1,
            locals: 1,
            code: vec![Bytecode::ConsensusVote, Bytecode::Return],
        };
        self.functions.insert(func.name.clone(), func);
        Ok(())
    }

    fn compile_heal(&mut self, _body: &str) -> Result<(), CompileError> {
        let func = BytecodeFunction {
            name: "heal".to_string(),
            arity: 0,
            locals: 1,
            code: vec![Bytecode::Nop, Bytecode::Return],
        };
        self.functions.insert(func.name.clone(), func);
        Ok(())
    }

    fn compile_emergency(&mut self, _body: &str) -> Result<(), CompileError> {
        let func = BytecodeFunction {
            name: "emergency".to_string(),
            arity: 0,
            locals: 0,
            code: vec![Bytecode::Halt],
        };
        self.functions.insert(func.name.clone(), func);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compiler_empty() {
        let mut compiler = Compiler::new();
        let module = DplusModule {
            sections: Vec::new(),
            metadata: HashMap::new(),
        };
        let result = compiler.compile(&module);
        assert!(result.is_ok());
    }
}
