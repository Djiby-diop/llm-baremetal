// src/dplus_compiler/parser.rs
//! D+ Parser: Tokens → AST

use super::lexer::{Lexer, Token, TokenKind};
use super::ast::*;
use super::CompileError;
use std::collections::HashMap;

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Parser { tokens, pos: 0 }
    }

    fn current_token(&self) -> &Token {
        self.tokens.get(self.pos).unwrap_or(&Token {
            kind: TokenKind::Eof,
            line: 0,
            col: 0,
        })
    }

    fn peek_token(&self, offset: usize) -> &Token {
        self.tokens.get(self.pos + offset).unwrap_or(&Token {
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

    pub fn parse_module(&mut self) -> Result<DplusModule, CompileError> {
        let mut sections = Vec::new();
        let mut metadata = HashMap::new();

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
                _ => {
                    self.advance();
                }
            }
        }

        Ok(DplusModule {
            sections,
            metadata,
        })
    }

    fn parse_genome_section(&mut self) -> Result<Section, CompileError> {
        let mut props = HashMap::new();
        
        while self.current_token().kind != TokenKind::Eof
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
        {
            if let TokenKind::Identifier(key) = &self.current_token().kind {
                let key = key.clone();
                self.advance();
                self.expect(TokenKind::Colon)?;
                
                if let TokenKind::String(value) = &self.current_token().kind {
                    let value = value.clone();
                    props.insert(key, value);
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
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
        {
            if let TokenKind::Identifier(name) = &self.current_token().kind {
                let name = name.clone();
                self.advance();
                self.expect(TokenKind::Equal)?;
                
                let mut desc = String::new();
                while self.current_token().kind != TokenKind::Semicolon
                    && self.current_token().kind != TokenKind::Eof
                    && !matches!(
                        self.current_token().kind,
                        TokenKind::SectionGenome
                            | TokenKind::SectionRights
                            | TokenKind::SectionDuties
                            | TokenKind::SectionVerdicts
                            | TokenKind::SectionLaw
                            | TokenKind::SectionProof
                            | TokenKind::SectionJudge
                            | TokenKind::SectionHeal
                            | TokenKind::SectionEmergency
                    )
                {
                    desc.push_str(&format!("{:?} ", self.current_token().kind));
                    self.advance();
                }
                
                rights.push((name, desc));
                
                if self.current_token().kind == TokenKind::Semicolon {
                    self.advance();
                }
            } else {
                self.advance();
            }
        }

        Ok(Section::Rights { rights })
    }

    fn parse_duties_section(&mut self) -> Result<Section, CompileError> {
        let mut duties = Vec::new();
        
        while self.current_token().kind != TokenKind::Eof
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
        {
            if let TokenKind::Identifier(name) = &self.current_token().kind {
                let name = name.clone();
                self.advance();
                self.expect(TokenKind::Equal)?;
                
                let mut desc = String::new();
                while self.current_token().kind != TokenKind::Semicolon
                    && self.current_token().kind != TokenKind::Eof
                    && !matches!(
                        self.current_token().kind,
                        TokenKind::SectionGenome
                            | TokenKind::SectionRights
                            | TokenKind::SectionDuties
                            | TokenKind::SectionVerdicts
                            | TokenKind::SectionLaw
                            | TokenKind::SectionProof
                            | TokenKind::SectionJudge
                            | TokenKind::SectionHeal
                            | TokenKind::SectionEmergency
                    )
                {
                    desc.push_str(&format!("{:?} ", self.current_token().kind));
                    self.advance();
                }
                
                duties.push((name, desc));
                
                if self.current_token().kind == TokenKind::Semicolon {
                    self.advance();
                }
            } else {
                self.advance();
            }
        }

        Ok(Section::Duties { duties })
    }

    fn parse_verdicts_section(&mut self) -> Result<Section, CompileError> {
        let mut verdicts = Vec::new();
        
        while self.current_token().kind != TokenKind::Eof
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
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
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
        {
            if let TokenKind::Identifier(head) = &self.current_token().kind {
                let head = head.clone();
                self.advance();
                
                let mut params = Vec::new();
                if self.current_token().kind == TokenKind::LeftParen {
                    self.advance();
                    while self.current_token().kind != TokenKind::RightParen {
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
                
                let mut body = Vec::new();
                while self.current_token().kind != TokenKind::Semicolon
                    && self.current_token().kind != TokenKind::Eof
                    && !matches!(
                        self.current_token().kind,
                        TokenKind::SectionGenome
                            | TokenKind::SectionRights
                            | TokenKind::SectionDuties
                            | TokenKind::SectionVerdicts
                            | TokenKind::SectionLaw
                            | TokenKind::SectionProof
                            | TokenKind::SectionJudge
                            | TokenKind::SectionHeal
                            | TokenKind::SectionEmergency
                    )
                {
                    // For now, store raw tokens as expressions
                    // This would be expanded with full expression parsing
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
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
        {
            if self.current_token().kind == TokenKind::KwInvariant {
                self.advance();
                
                let mut formula = String::new();
                while self.current_token().kind != TokenKind::Semicolon
                    && self.current_token().kind != TokenKind::Eof
                    && !matches!(
                        self.current_token().kind,
                        TokenKind::SectionGenome
                            | TokenKind::SectionRights
                            | TokenKind::SectionDuties
                            | TokenKind::SectionVerdicts
                            | TokenKind::SectionLaw
                            | TokenKind::SectionProof
                            | TokenKind::SectionJudge
                            | TokenKind::SectionHeal
                            | TokenKind::SectionEmergency
                    )
                {
                    formula.push_str(&format!("{:?} ", self.current_token().kind));
                    self.advance();
                }
                
                invariants.push(Invariant { name: None, formula });
                
                if self.current_token().kind == TokenKind::Semicolon {
                    self.advance();
                }
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

    fn collect_raw_tokens_until_section(&mut self) -> String {
        let mut result = String::new();
        while self.current_token().kind != TokenKind::Eof
            && !matches!(
                self.current_token().kind,
                TokenKind::SectionGenome
                    | TokenKind::SectionRights
                    | TokenKind::SectionDuties
                    | TokenKind::SectionVerdicts
                    | TokenKind::SectionLaw
                    | TokenKind::SectionProof
                    | TokenKind::SectionJudge
                    | TokenKind::SectionHeal
                    | TokenKind::SectionEmergency
            )
        {
            result.push_str(&format!("{:?} ", self.current_token().kind));
            self.advance();
        }
        result
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
}
