// Pillar p03 (TRUTH-G) — Rust scaffold

pub fn truth_ok() -> bool { true }

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ok() {
        assert!(truth_ok());
    }
}
