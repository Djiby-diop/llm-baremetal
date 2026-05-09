// src/dplus_compiler/lexer.rs
//! D+ Lexer: Text → Tokens

use super::CompileError;

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    // Literals
    Identifier(String),
    Number(f64),
    String(String),
    Symbol(char),

    // Keywords
    KwLet,
    KwIf,
    KwThen,
    KwElse,
    KwFn,
    KwPub,
    KwReturn,
    KwTrue,
    KwFalse,
    KwMatch,
    KwCase,
    KwWhen,
    KwRequire,
    KwEnsure,
    KwInvariant,
    KwProof,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equal,
    EqualEqual,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
    Not,
    Question,
    Arrow,      // ->
    FatArrow,   // =>
    Colonequal, // :=
    Imply,      // :-

    // Delimiters
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Semicolon,
    Comma,
    Dot,
    Colon,
    At,
    Hash,

    // Sections
    SectionGenome,
    SectionRights,
    SectionDuties,
    SectionVerdicts,
    SectionLaw,
    SectionProof,
    SectionJudge,
    SectionHeal,
    SectionEmergency,

    // Special
    Eof,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub kind: TokenKind,
    pub line: usize,
    pub col: usize,
}

pub struct Lexer {
    input: Vec<char>,
    pos: usize,
    line: usize,
    col: usize,
}

impl Lexer {
    pub fn new(input: &str) -> Self {
        Lexer {
            input: input.chars().collect(),
            pos: 0,
            line: 1,
            col: 1,
        }
    }

    fn current_char(&self) -> Option<char> {
        if self.pos < self.input.len() {
            Some(self.input[self.pos])
        } else {
            None
        }
    }

    fn peek_char(&self, offset: usize) -> Option<char> {
        if self.pos + offset < self.input.len() {
            Some(self.input[self.pos + offset])
        } else {
            None
        }
    }

    fn advance(&mut self) {
        if let Some(ch) = self.current_char() {
            if ch == '\n' {
                self.line += 1;
                self.col = 1;
            } else {
                self.col += 1;
            }
            self.pos += 1;
        }
    }

    fn skip_whitespace(&mut self) {
        while let Some(ch) = self.current_char() {
            if ch.is_whitespace() {
                self.advance();
            } else {
                break;
            }
        }
    }

    fn skip_comment(&mut self) {
        if self.current_char() == Some('#') {
            while let Some(ch) = self.current_char() {
                if ch == '\n' {
                    break;
                }
                self.advance();
            }
        }
    }

    fn read_identifier(&mut self) -> String {
        let mut result = String::new();
        while let Some(ch) = self.current_char() {
            if ch.is_alphanumeric() || ch == '_' {
                result.push(ch);
                self.advance();
            } else {
                break;
            }
        }
        result
    }

    fn read_number(&mut self) -> f64 {
        let mut num_str = String::new();
        while let Some(ch) = self.current_char() {
            if ch.is_ascii_digit() || ch == '.' {
                num_str.push(ch);
                self.advance();
            } else {
                break;
            }
        }
        num_str.parse().unwrap_or(0.0)
    }

    fn read_string(&mut self, quote: char) -> Result<String, CompileError> {
        let mut result = String::new();
        self.advance(); // skip opening quote
        
        while let Some(ch) = self.current_char() {
            if ch == quote {
                self.advance();
                return Ok(result);
            } else if ch == '\\' {
                self.advance();
                match self.current_char() {
                    Some('n') => result.push('\n'),
                    Some('t') => result.push('\t'),
                    Some('r') => result.push('\r'),
                    Some('\\') => result.push('\\'),
                    Some(c) => result.push(c),
                    None => return Err(CompileError::LexerError("Unterminated string".into())),
                }
                self.advance();
            } else {
                result.push(ch);
                self.advance();
            }
        }
        Err(CompileError::LexerError("Unterminated string".into()))
    }

    fn keyword_or_identifier(&self, ident: &str) -> TokenKind {
        match ident {
            "let" => TokenKind::KwLet,
            "if" => TokenKind::KwIf,
            "then" => TokenKind::KwThen,
            "else" => TokenKind::KwElse,
            "fn" => TokenKind::KwFn,
            "pub" => TokenKind::KwPub,
            "return" => TokenKind::KwReturn,
            "true" => TokenKind::KwTrue,
            "false" => TokenKind::KwFalse,
            "match" => TokenKind::KwMatch,
            "case" => TokenKind::KwCase,
            "when" => TokenKind::KwWhen,
            "require" => TokenKind::KwRequire,
            "ensure" => TokenKind::KwEnsure,
            "invariant" => TokenKind::KwInvariant,
            "proof" => TokenKind::KwProof,
            _ => TokenKind::Identifier(ident.to_string()),
        }
    }

    fn section_tag(&self, tag: &str) -> Option<TokenKind> {
        match tag {
            "GENOME" => Some(TokenKind::SectionGenome),
            "RIGHTS" => Some(TokenKind::SectionRights),
            "DUTIES" => Some(TokenKind::SectionDuties),
            "VERDICTS" => Some(TokenKind::SectionVerdicts),
            "LAW" => Some(TokenKind::SectionLaw),
            "PROOF" => Some(TokenKind::SectionProof),
            "JUDGE" => Some(TokenKind::SectionJudge),
            "HEAL" => Some(TokenKind::SectionHeal),
            "EMERGENCY" => Some(TokenKind::SectionEmergency),
            _ => None,
        }
    }

    pub fn next_token(&mut self) -> Result<Token, CompileError> {
        self.skip_whitespace();

        // Skip comments
        while self.current_char() == Some('#') {
            self.skip_comment();
            self.skip_whitespace();
        }

        let line = self.line;
        let col = self.col;

        match self.current_char() {
            None => Ok(Token {
                kind: TokenKind::Eof,
                line,
                col,
            }),
            Some('@') => {
                self.advance();
                if self.current_char() == Some('[') {
                    self.advance();
                    let tag = self.read_identifier();
                    self.advance(); // skip ]
                    
                    if let Some(section) = self.section_tag(&tag) {
                        Ok(Token {
                            kind: section,
                            line,
                            col,
                        })
                    } else {
                        Ok(Token {
                            kind: TokenKind::Identifier(format!("[{}]", tag)),
                            line,
                            col,
                        })
                    }
                } else {
                    Ok(Token {
                        kind: TokenKind::At,
                        line,
                        col,
                    })
                }
            }
            Some('[') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::LeftBracket,
                    line,
                    col,
                })
            }
            Some(']') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::RightBracket,
                    line,
                    col,
                })
            }
            Some('(') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::LeftParen,
                    line,
                    col,
                })
            }
            Some(')') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::RightParen,
                    line,
                    col,
                })
            }
            Some('{') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::LeftBrace,
                    line,
                    col,
                })
            }
            Some('}') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::RightBrace,
                    line,
                    col,
                })
            }
            Some(';') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Semicolon,
                    line,
                    col,
                })
            }
            Some(',') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Comma,
                    line,
                    col,
                })
            }
            Some('.') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Dot,
                    line,
                    col,
                })
            }
            Some(':') => {
                self.advance();
                if self.current_char() == Some('-') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::Imply,
                        line,
                        col,
                    })
                } else if self.current_char() == Some('=') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::Colonequal,
                        line,
                        col,
                    })
                } else {
                    Ok(Token {
                        kind: TokenKind::Colon,
                        line,
                        col,
                    })
                }
            }
            Some('=') => {
                self.advance();
                if self.current_char() == Some('=') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::EqualEqual,
                        line,
                        col,
                    })
                } else if self.current_char() == Some('>') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::FatArrow,
                        line,
                        col,
                    })
                } else {
                    Ok(Token {
                        kind: TokenKind::Equal,
                        line,
                        col,
                    })
                }
            }
            Some('-') => {
                self.advance();
                if self.current_char() == Some('>') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::Arrow,
                        line,
                        col,
                    })
                } else {
                    Ok(Token {
                        kind: TokenKind::Minus,
                        line,
                        col,
                    })
                }
            }
            Some('+') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Plus,
                    line,
                    col,
                })
            }
            Some('*') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Star,
                    line,
                    col,
                })
            }
            Some('/') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Slash,
                    line,
                    col,
                })
            }
            Some('%') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Percent,
                    line,
                    col,
                })
            }
            Some('<') => {
                self.advance();
                if self.current_char() == Some('=') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::LessEqual,
                        line,
                        col,
                    })
                } else {
                    Ok(Token {
                        kind: TokenKind::Less,
                        line,
                        col,
                    })
                }
            }
            Some('>') => {
                self.advance();
                if self.current_char() == Some('=') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::GreaterEqual,
                        line,
                        col,
                    })
                } else {
                    Ok(Token {
                        kind: TokenKind::Greater,
                        line,
                        col,
                    })
                }
            }
            Some('!') => {
                self.advance();
                if self.current_char() == Some('=') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::NotEqual,
                        line,
                        col,
                    })
                } else {
                    Ok(Token {
                        kind: TokenKind::Not,
                        line,
                        col,
                    })
                }
            }
            Some('&') => {
                self.advance();
                if self.current_char() == Some('&') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::And,
                        line,
                        col,
                    })
                } else {
                    Err(CompileError::LexerError("Expected &&".into()))
                }
            }
            Some('|') => {
                self.advance();
                if self.current_char() == Some('|') {
                    self.advance();
                    Ok(Token {
                        kind: TokenKind::Or,
                        line,
                        col,
                    })
                } else {
                    Err(CompileError::LexerError("Expected ||".into()))
                }
            }
            Some('?') => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Question,
                    line,
                    col,
                })
            }
            Some('"') => {
                let string = self.read_string('"')?;
                Ok(Token {
                    kind: TokenKind::String(string),
                    line,
                    col,
                })
            }
            Some('\'') => {
                let string = self.read_string('\'')?;
                Ok(Token {
                    kind: TokenKind::String(string),
                    line,
                    col,
                })
            }
            Some(ch) if ch.is_ascii_digit() => {
                let num = self.read_number();
                Ok(Token {
                    kind: TokenKind::Number(num),
                    line,
                    col,
                })
            }
            Some(ch) if ch.is_alphabetic() || ch == '_' => {
                let ident = self.read_identifier();
                let kind = self.keyword_or_identifier(&ident);
                Ok(Token { kind, line, col })
            }
            Some(ch) => {
                self.advance();
                Ok(Token {
                    kind: TokenKind::Symbol(ch),
                    line,
                    col,
                })
            }
        }
    }

    pub fn tokenize(&mut self) -> Result<Vec<Token>, CompileError> {
        let mut tokens = Vec::new();
        loop {
            let token = self.next_token()?;
            let is_eof = token.kind == TokenKind::Eof;
            tokens.push(token);
            if is_eof {
                break;
            }
        }
        Ok(tokens)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lexer_simple() {
        let mut lexer = Lexer::new("let x = 42");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens.len(), 5); // let, x, =, 42, eof
    }

    #[test]
    fn test_lexer_law() {
        let mut lexer = Lexer::new("[LAW]");
        let tokens = lexer.tokenize().unwrap();
        assert!(matches!(tokens[0].kind, TokenKind::SectionLaw));
    }
}
