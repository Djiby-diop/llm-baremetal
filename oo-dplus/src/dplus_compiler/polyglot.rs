//! Phase 4 foundation: Polyglot block model for D+.
//!
//! This module is intentionally parser-agnostic for now. It defines
//! validated representations that the parser/codegen can plug into next.

use super::CompileError;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum EmbeddedLanguage {
    Python,
    Rust,
    Prolog,
    CudaKernel,
    OpenClKernel,
    AsmX86_64,
}

impl EmbeddedLanguage {
    pub fn as_str(self) -> &'static str {
        match self {
            EmbeddedLanguage::Python => "python",
            EmbeddedLanguage::Rust => "rust",
            EmbeddedLanguage::Prolog => "prolog",
            EmbeddedLanguage::CudaKernel => "cuda_kernel",
            EmbeddedLanguage::OpenClKernel => "opencl_kernel",
            EmbeddedLanguage::AsmX86_64 => "asm_x86_64",
        }
    }

    pub fn parse(raw: &str) -> Option<Self> {
        match raw.trim().to_ascii_lowercase().as_str() {
            "python" => Some(EmbeddedLanguage::Python),
            "rust" => Some(EmbeddedLanguage::Rust),
            "prolog" => Some(EmbeddedLanguage::Prolog),
            "cuda_kernel" => Some(EmbeddedLanguage::CudaKernel),
            "opencl_kernel" => Some(EmbeddedLanguage::OpenClKernel),
            "asm_x86_64" => Some(EmbeddedLanguage::AsmX86_64),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ForeignBlock {
    pub language: EmbeddedLanguage,
    pub code: String,
}

impl ForeignBlock {
    pub fn new(language: EmbeddedLanguage, code: impl Into<String>) -> Result<Self, CompileError> {
        let block = Self {
            language,
            code: code.into(),
        };
        block.validate()?;
        Ok(block)
    }

    fn contains_forbidden_fragment(lowered: &str) -> Option<&'static str> {
        const FORBIDDEN_FRAGMENTS: [&str; 10] = [
            "import os",
            "os.system",
            "subprocess",
            "__import__",
            "eval(",
            "exec(",
            "std::process",
            "command::new(",
            "socket",
            "syscall",
        ];

        FORBIDDEN_FRAGMENTS
            .iter()
            .find(|fragment| lowered.contains(**fragment))
            .copied()
    }

    pub fn validate(&self) -> Result<(), CompileError> {
        let trimmed = self.code.trim();
        if trimmed.is_empty() {
            return Err(CompileError::ParseError(format!(
                "empty foreign code block for {}",
                self.language.as_str()
            )));
        }

        let lowered = trimmed.to_ascii_lowercase();
        if let Some(fragment) = Self::contains_forbidden_fragment(&lowered) {
            return Err(CompileError::TypeError(format!(
                "foreign block for {} contains forbidden fragment: {}",
                self.language.as_str(),
                fragment
            )));
        }

        // Keep this minimal and safe for now. Deeper validation is backend-specific.
        if trimmed.len() > 64 * 1024 {
            return Err(CompileError::TypeError(format!(
                "foreign block for {} exceeds 64 KiB",
                self.language.as_str()
            )));
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn language_parse_roundtrip() {
        let langs = [
            EmbeddedLanguage::Python,
            EmbeddedLanguage::Rust,
            EmbeddedLanguage::Prolog,
            EmbeddedLanguage::CudaKernel,
            EmbeddedLanguage::OpenClKernel,
            EmbeddedLanguage::AsmX86_64,
        ];

        for lang in langs {
            let parsed = EmbeddedLanguage::parse(lang.as_str());
            assert_eq!(parsed, Some(lang));
        }
    }

    #[test]
    fn rejects_empty_block() {
        let err = ForeignBlock::new(EmbeddedLanguage::Python, "   ");
        assert!(err.is_err());
    }

    #[test]
    fn accepts_valid_block() {
        let ok = ForeignBlock::new(EmbeddedLanguage::Prolog, "can_allocate(X) :- X > 0.");
        assert!(ok.is_ok());
    }

    #[test]
    fn rejects_obvious_escape_hatch() {
        let err = ForeignBlock::new(
            EmbeddedLanguage::Python,
            "import os\nos.system('id')",
        );
        assert!(err.is_err());
    }
}
