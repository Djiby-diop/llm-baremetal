// src/dplus_compiler/ast.rs
//! D+ AST (Abstract Syntax Tree)

use std::collections::HashMap;
use super::polyglot::ForeignBlock;

#[derive(Debug, Clone, PartialEq)]
pub enum Expr {
    Number(f64),
    String(String),
    Bool(bool),
    Ident(String),
    Binary {
        left: Box<Expr>,
        op: BinOp,
        right: Box<Expr>,
    },
    Unary {
        op: UnaryOp,
        expr: Box<Expr>,
    },
    Call {
        func: String,
        args: Vec<Expr>,
    },
    IfExpr {
        condition: Box<Expr>,
        then_branch: Box<Expr>,
        else_branch: Option<Box<Expr>>,
    },
    Match {
        expr: Box<Expr>,
        cases: Vec<(Pattern, Expr)>,
    },
}

#[derive(Debug, Clone, PartialEq)]
pub enum Pattern {
    Literal(Expr),
    Wildcard,
}

#[derive(Debug, Clone, PartialEq)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    And,
    Or,
    Eq,
    Neq,
    Lt,
    Le,
    Gt,
    Ge,
    Imply,
}

#[derive(Debug, Clone, PartialEq)]
pub enum UnaryOp {
    Not,
    Neg,
}

#[derive(Debug, Clone)]
pub enum Stmt {
    Let {
        name: String,
        value: Expr,
    },
    Return(Option<Expr>),
    Expr(Expr),
}

#[derive(Debug, Clone)]
pub enum SectionKind {
    Genome,
    Rights,
    Duties,
    Verdicts,
    Law,
    Proof,
    Judge,
    Heal,
    Emergency,
}

#[derive(Debug, Clone)]
pub struct Rule {
    pub head: String,
    pub params: Vec<String>,
    pub body: Vec<Expr>,
}

#[derive(Debug, Clone)]
pub struct Invariant {
    pub name: Option<String>,
    pub formula: String,
}

#[derive(Debug, Clone)]
pub enum Section {
    Genome {
        props: HashMap<String, String>,
    },
    Rights {
        rights: Vec<(String, String)>,
    },
    Duties {
        duties: Vec<(String, String)>,
    },
    Verdicts {
        verdicts: Vec<String>,
    },
    Law {
        rules: Vec<Rule>,
    },
    Proof {
        invariants: Vec<Invariant>,
    },
    Judge {
        body: String, // Rust code block
    },
    Heal {
        body: String, // Rust code block
    },
    Emergency {
        body: String, // Rust code block
    },
    Polyglot {
        block: ForeignBlock,
    },
}

#[derive(Debug, Clone)]
pub struct DplusModule {
    pub sections: Vec<Section>,
    pub metadata: HashMap<String, String>,
}
