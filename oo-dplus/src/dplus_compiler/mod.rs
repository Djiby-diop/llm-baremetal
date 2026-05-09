// src/dplus_compiler/mod.rs
//! D+ v2 Compiler: Lexer → Parser → AST → Bytecode

pub mod lexer;
pub mod parser;
pub mod ast;
pub mod bytecode;
pub mod compiler;

pub use lexer::Lexer;
pub use parser::Parser;
pub use ast::*;
pub use bytecode::*;
pub use compiler::Compiler;

#[derive(Debug, Clone, PartialEq)]
pub enum CompileError {
    LexerError(String),
    ParseError(String),
    TypeError(String),
    RuntimeError(String),
}

impl std::fmt::Display for CompileError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            CompileError::LexerError(msg) => write!(f, "Lexer error: {}", msg),
            CompileError::ParseError(msg) => write!(f, "Parse error: {}", msg),
            CompileError::TypeError(msg) => write!(f, "Type error: {}", msg),
            CompileError::RuntimeError(msg) => write!(f, "Runtime error: {}", msg),
        }
    }
}

impl std::error::Error for CompileError {}
