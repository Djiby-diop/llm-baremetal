// src/dplus_compiler/parser.rs
//! D+ Parser: Tokens -> AST

use super::ast::*;
use super::lexer::{Lexer, Token, TokenKind};
use super::polyglot::{EmbeddedLanguage, ForeignBlock};
use super::CompileError;
use std::collections::HashMap;

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, pos: 0 }
    }

    fn current_token(&self) -> &Token {
        self.tokens.get(self.pos).unwrap_or(&Token {
            kind: TokenKind::Eof,
            line: 0,
            col: 0,
        })
    }

    fn advance(&mut self) {
        if self.pos < self.tokens.len() {
            self.pos += 1;
        }
    }

    fn expect(&mut self, expected: TokenKind) -> Result<(), CompileError> {
        if self.current_token().kind == expected {
            self.advance();
            Ok(())
        } else {
            Err(CompileError::ParseError(format!(
                "Expected {:?}, got {:?}",
                expected, self.current_token().kind
            )))
        }
    }

    fn is_section_start(kind: &TokenKind) -> bool {
        matches!(
            kind,
            TokenKind::SectionGenome
                | TokenKind::SectionRights
                | TokenKind::SectionDuties
                | TokenKind::SectionVerdicts
                | TokenKind::SectionLaw
                | TokenKind::SectionProof
                | TokenKind::SectionJudge
                | TokenKind::SectionHeal
                | TokenKind::SectionEmergency
                | TokenKind::SectionLang
        )
    }

    pub fn parse_module(&mut self) -> Result<DplusModule, CompileError> {
        let mut sections = Vec::new();
        let metadata = HashMap::new();

        while self.current_token().kind != TokenKind::Eof {
            match &self.current_token().kind {
                TokenKind::SectionGenome => {
                    self.advance();
                    sections.push(self.parse_genome_section()?);
                }
                TokenKind::SectionRights => {
                    self.advance();
                    sections.push(self.parse_rights_section()?);
                }
                TokenKind::SectionDuties => {
                    self.advance();
                    sections.push(self.parse_duties_section()?);
                }
                TokenKind::SectionVerdicts => {
                    self.advance();
                    sections.push(self.parse_verdicts_section()?);
                }
                TokenKind::SectionLaw => {
                    self.advance();
                    sections.push(self.parse_law_section()?);
                }
                TokenKind::SectionProof => {
                    self.advance();
                    sections.push(self.parse_proof_section()?);
                }
                TokenKind::SectionJudge => {
                    self.advance();
                    sections.push(self.parse_judge_section()?);
                }
                TokenKind::SectionHeal => {
                    self.advance();
                    sections.push(self.parse_heal_section()?);
                }
                TokenKind::SectionEmergency => {
                    self.advance();
                    sections.push(self.parse_emergency_section()?);
                }
                TokenKind::SectionLang => {
                    self.advance();
                    sections.push(self.parse_lang_section()?);
                }
                _ => self.advance(),
            }
        }

        Ok(DplusModule { sections, metadata })
    }

    fn parse_genome_section(&mut self) -> Result<Section, CompileError> {
        let mut props = HashMap::new();

        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            if let TokenKind::Identifier(key) = &self.current_token().kind {
                let key = key.clone();
                self.advance();
                self.expect(TokenKind::Colon)?;

                if let TokenKind::String(value) = &self.current_token().kind {
                    props.insert(key, value.clone());
                    self.advance();
                }
                if self.current_token().kind == TokenKind::Semicolon {
                    self.advance();
                }
            } else {
                self.advance();
            }
        }

        Ok(Section::Genome { props })
    }

    fn parse_rights_section(&mut self) -> Result<Section, CompileError> {
        let mut rights = Vec::new();

        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            if let TokenKind::Identifier(name) = &self.current_token().kind {
                let name = name.clone();
                self.advance();
                self.expect(TokenKind::Equal)?;
                let desc = self.collect_raw_until_semicolon();
                rights.push((name, desc));
            } else {
                self.advance();
            }
        }

        Ok(Section::Rights { rights })
    }

    fn parse_duties_section(&mut self) -> Result<Section, CompileError> {
        let mut duties = Vec::new();

        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            if let TokenKind::Identifier(name) = &self.current_token().kind {
                let name = name.clone();
                self.advance();
                self.expect(TokenKind::Equal)?;
                let desc = self.collect_raw_until_semicolon();
                duties.push((name, desc));
            } else {
                self.advance();
            }
        }

        Ok(Section::Duties { duties })
    }

    fn parse_verdicts_section(&mut self) -> Result<Section, CompileError> {
        let mut verdicts = Vec::new();

        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            if let TokenKind::Identifier(verdict) = &self.current_token().kind {
                verdicts.push(verdict.clone());
            }
            self.advance();
        }

        Ok(Section::Verdicts { verdicts })
    }

    fn parse_law_section(&mut self) -> Result<Section, CompileError> {
        let mut rules = Vec::new();

        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            if let TokenKind::Identifier(head) = &self.current_token().kind {
                let head = head.clone();
                self.advance();

                let mut params = Vec::new();
                if self.current_token().kind == TokenKind::LeftParen {
                    self.advance();
                    while self.current_token().kind != TokenKind::RightParen
                        && self.current_token().kind != TokenKind::Eof
                    {
                        if let TokenKind::Identifier(param) = &self.current_token().kind {
                            params.push(param.clone());
                        }
                        self.advance();
                        if self.current_token().kind == TokenKind::Comma {
                            self.advance();
                        }
                    }
                    self.expect(TokenKind::RightParen)?;
                }

                if self.current_token().kind == TokenKind::Imply {
                    self.advance();
                }

                let body = Vec::new();
                while self.current_token().kind != TokenKind::Semicolon
                    && self.current_token().kind != TokenKind::Eof
                    && !Self::is_section_start(&self.current_token().kind)
                {
                    self.advance();
                }

                rules.push(Rule { head, params, body });
                if self.current_token().kind == TokenKind::Semicolon {
                    self.advance();
                }
            } else {
                self.advance();
            }
        }

        Ok(Section::Law { rules })
    }

    fn parse_proof_section(&mut self) -> Result<Section, CompileError> {
        let mut invariants = Vec::new();

        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            if self.current_token().kind == TokenKind::KwInvariant {
                self.advance();
                let formula = self.collect_raw_until_semicolon();
                invariants.push(Invariant { name: None, formula });
            } else {
                self.advance();
            }
        }

        Ok(Section::Proof { invariants })
    }

    fn parse_judge_section(&mut self) -> Result<Section, CompileError> {
        let body = self.collect_raw_tokens_until_section();
        Ok(Section::Judge { body })
    }

    fn parse_heal_section(&mut self) -> Result<Section, CompileError> {
        let body = self.collect_raw_tokens_until_section();
        Ok(Section::Heal { body })
    }

    fn parse_emergency_section(&mut self) -> Result<Section, CompileError> {
        let body = self.collect_raw_tokens_until_section();
        Ok(Section::Emergency { body })
    }

    fn parse_lang_section(&mut self) -> Result<Section, CompileError> {
        let lang_name = match &self.current_token().kind {
            TokenKind::Identifier(name) => {
                let v = name.clone();
                self.advance();
                v
            }
            _ => {
                return Err(CompileError::ParseError(
                    "Expected language identifier after @[LANG]".into(),
                ))
            }
        };

        let language = EmbeddedLanguage::parse(&lang_name).ok_or_else(|| {
            CompileError::ParseError(format!("Unsupported embedded language: {}", lang_name))
        })?;

        let code = self.collect_braced_raw()?;
        let block = ForeignBlock::new(language, code)?;
        Ok(Section::Polyglot { block })
    }

    fn token_to_source(token: &TokenKind) -> String {
        match token {
            TokenKind::Identifier(s) => s.clone(),
            TokenKind::Number(n) => n.to_string(),
            TokenKind::String(s) => format!("\"{}\"", s),
            TokenKind::Symbol(c) => c.to_string(),
            TokenKind::KwLet => "let".into(),
            TokenKind::KwIf => "if".into(),
            TokenKind::KwThen => "then".into(),
            TokenKind::KwElse => "else".into(),
            TokenKind::KwFn => "fn".into(),
            TokenKind::KwPub => "pub".into(),
            TokenKind::KwReturn => "return".into(),
            TokenKind::KwTrue => "true".into(),
            TokenKind::KwFalse => "false".into(),
            TokenKind::KwMatch => "match".into(),
            TokenKind::KwCase => "case".into(),
            TokenKind::KwWhen => "when".into(),
            TokenKind::KwRequire => "require".into(),
            TokenKind::KwEnsure => "ensure".into(),
            TokenKind::KwInvariant => "invariant".into(),
            TokenKind::KwProof => "proof".into(),
            TokenKind::Plus => "+".into(),
            TokenKind::Minus => "-".into(),
            TokenKind::Star => "*".into(),
            TokenKind::Slash => "/".into(),
            TokenKind::Percent => "%".into(),
            TokenKind::Equal => "=".into(),
            TokenKind::EqualEqual => "==".into(),
            TokenKind::NotEqual => "!=".into(),
            TokenKind::Less => "<".into(),
            TokenKind::LessEqual => "<=".into(),
            TokenKind::Greater => ">".into(),
            TokenKind::GreaterEqual => ">=".into(),
            TokenKind::And => "&&".into(),
            TokenKind::Or => "||".into(),
            TokenKind::Not => "!".into(),
            TokenKind::Question => "?".into(),
            TokenKind::Arrow => "->".into(),
            TokenKind::FatArrow => "=>".into(),
            TokenKind::Colonequal => ":=".into(),
            TokenKind::Imply => ":-".into(),
            TokenKind::LeftParen => "(".into(),
            TokenKind::RightParen => ")".into(),
            TokenKind::LeftBracket => "[".into(),
            TokenKind::RightBracket => "]".into(),
            TokenKind::LeftBrace => "{".into(),
            TokenKind::RightBrace => "}".into(),
            TokenKind::Semicolon => ";".into(),
            TokenKind::Comma => ",".into(),
            TokenKind::Dot => ".".into(),
            TokenKind::Colon => ":".into(),
            TokenKind::At => "@".into(),
            TokenKind::Hash => "#".into(),
            TokenKind::SectionGenome => "@[GENOME]".into(),
            TokenKind::SectionRights => "@[RIGHTS]".into(),
            TokenKind::SectionDuties => "@[DUTIES]".into(),
            TokenKind::SectionVerdicts => "@[VERDICTS]".into(),
            TokenKind::SectionLaw => "@[LAW]".into(),
            TokenKind::SectionProof => "@[PROOF]".into(),
            TokenKind::SectionJudge => "@[JUDGE]".into(),
            TokenKind::SectionHeal => "@[HEAL]".into(),
            TokenKind::SectionEmergency => "@[EMERGENCY]".into(),
            TokenKind::SectionLang => "@[LANG]".into(),
            TokenKind::Eof => String::new(),
        }
    }

    fn collect_braced_raw(&mut self) -> Result<String, CompileError> {
        self.expect(TokenKind::LeftBrace)?;
        let mut depth = 1usize;
        let mut out = String::new();

        while self.current_token().kind != TokenKind::Eof {
            let tk = self.current_token().kind.clone();
            match tk {
                TokenKind::LeftBrace => {
                    depth += 1;
                    out.push_str("{ ");
                    self.advance();
                }
                TokenKind::RightBrace => {
                    depth -= 1;
                    if depth == 0 {
                        self.advance();
                        return Ok(out.trim().to_string());
                    }
                    out.push_str("} ");
                    self.advance();
                }
                _ => {
                    out.push_str(&Self::token_to_source(&tk));
                    out.push(' ');
                    self.advance();
                }
            }
        }

        Err(CompileError::ParseError(
            "Unterminated LANG block; missing '}'".into(),
        ))
    }

    fn collect_raw_until_semicolon(&mut self) -> String {
        let mut result = String::new();
        while self.current_token().kind != TokenKind::Semicolon
            && self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            result.push_str(&Self::token_to_source(&self.current_token().kind));
            result.push(' ');
            self.advance();
        }

        if self.current_token().kind == TokenKind::Semicolon {
            self.advance();
        }

        result.trim().to_string()
    }

    fn collect_raw_tokens_until_section(&mut self) -> String {
        let mut result = String::new();
        while self.current_token().kind != TokenKind::Eof
            && !Self::is_section_start(&self.current_token().kind)
        {
            result.push_str(&Self::token_to_source(&self.current_token().kind));
            result.push(' ');
            self.advance();
        }
        result.trim().to_string()
    }
}

pub fn parse(input: &str) -> Result<DplusModule, CompileError> {
    let mut lexer = Lexer::new(input);
    let tokens = lexer.tokenize()?;
    let mut parser = Parser::new(tokens);
    parser.parse_module()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parser_empty() {
        let result = parse("");
        assert!(result.is_ok());
    }

    #[test]
    fn test_parser_lang_section() {
        let input = "@[LANG] python { print('hello'); x = 1 + 2; }";
        let result = parse(input).unwrap();
        assert_eq!(result.sections.len(), 1);

        match &result.sections[0] {
            Section::Polyglot { block } => {
                assert_eq!(block.language.as_str(), "python");
                assert!(block.code.contains("print"));
            }
            _ => panic!("Expected polyglot section"),
        }
    }
}