
pub struct SimpleTokenizer {
    vocab: &'static [&'static str],
}

impl SimpleTokenizer {
    pub fn new() -> Self {
        Self {
            vocab: &[
                "_", "OS-G", "system", "boot", "init", "kernel", "Ok", 
                "loader", "memory", "warden", "secure", "AI", "soma", 
                "running", "task", "start", "stop", "panic", "error", 
                "warning", "info", "debug", "trace"
            ]
        }
    }

    pub fn decode(&self, logit: f32) -> &'static str {
        // Map float logit to a vocab index. 
        // We multiply by a factor to spread out small differences if any.
        // For consistent weights (all 0.02), the result is constant 2.56.
        // Index = 2 % len.
        let val = (logit * 100.0) as usize;
        let idx = val % self.vocab.len();
        self.vocab[idx]
    }
}
