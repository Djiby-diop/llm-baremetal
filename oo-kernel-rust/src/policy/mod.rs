/*! oo-kernel-rust::policy — D+ Policy Engine in Rust
 *
 * Rust re-implementation of the D+ policy system.
 * Much safer than C: policy rules are enforced by the type system.
 *
 * Integration with OS-G:
 *   OS-G (Operating System Genesis) has a full D+ implementation in Rust.
 *   This module provides a simpler, embedded-first subset of D+ for
 *   use directly in the inference loop — no heap allocation required.
 *
 * D+ Embedded Rules (compile-time):
 *   - No write to WEIGHTS zone
 *   - Halt head threshold 0 < t < 1.0
 *   - Token budget: max 2048 per query
 *   - Swarm: max 4 peers
 */

#![no_std]

/// D+ rule — a single policy constraint
#[derive(Debug, Clone, Copy)]
pub enum DPlusRule {
    NoWriteWeights,
    HaltThresholdRange { min: f32, max: f32 },
    MaxTokenBudget(u32),
    MaxSwarmPeers(u8),
    NoMetaDriverUnsafe,
}

/// D+ verdict
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum DPlusVerdict {
    Allow,
    Deny,
    DenyWithReason(&'static str),
}

/// D+ policy checker (embedded subset)
pub struct DPlusEngine {
    pub enabled: bool,
    pub rules: [Option<DPlusRule>; 16],
    pub rule_count: usize,
    pub total_checks: u64,
    pub denied_count: u64,
}

impl DPlusEngine {
    pub const fn new() -> Self {
        Self {
            enabled: true,
            rules: [None; 16],
            rule_count: 0,
            total_checks: 0,
            denied_count: 0,
        }
    }

    pub fn add_rule(&mut self, rule: DPlusRule) -> bool {
        if self.rule_count >= 16 { return false; }
        self.rules[self.rule_count] = Some(rule);
        self.rule_count += 1;
        true
    }

    /// Check if a memory write is allowed
    pub fn check_write(&mut self, addr: u64, weights_base: u64, weights_size: u64) -> DPlusVerdict {
        self.total_checks += 1;
        if !self.enabled { return DPlusVerdict::Allow; }

        for i in 0..self.rule_count {
            if let Some(DPlusRule::NoWriteWeights) = self.rules[i] {
                if addr >= weights_base && addr < weights_base + weights_size {
                    self.denied_count += 1;
                    return DPlusVerdict::DenyWithReason("write to WEIGHTS zone forbidden by D+");
                }
            }
        }
        DPlusVerdict::Allow
    }

    /// Check if halt threshold is valid
    pub fn check_halt_threshold(&mut self, t: f32) -> DPlusVerdict {
        self.total_checks += 1;
        for i in 0..self.rule_count {
            if let Some(DPlusRule::HaltThresholdRange { min, max }) = self.rules[i] {
                if t < min || t > max {
                    self.denied_count += 1;
                    return DPlusVerdict::DenyWithReason("halt threshold out of D+ range");
                }
            }
        }
        DPlusVerdict::Allow
    }

    /// Check token budget
    pub fn check_budget(&mut self, tokens_used: u32) -> DPlusVerdict {
        self.total_checks += 1;
        for i in 0..self.rule_count {
            if let Some(DPlusRule::MaxTokenBudget(max)) = self.rules[i] {
                if tokens_used >= max {
                    self.denied_count += 1;
                    return DPlusVerdict::DenyWithReason("token budget exceeded (D+ MaxTokenBudget)");
                }
            }
        }
        DPlusVerdict::Allow
    }
}

/// Default OO policy — mirrors policy.dplus in binary
pub fn default_oo_policy() -> DPlusEngine {
    let mut e = DPlusEngine::new();
    e.add_rule(DPlusRule::NoWriteWeights);
    e.add_rule(DPlusRule::HaltThresholdRange { min: 0.5, max: 0.99 });
    e.add_rule(DPlusRule::MaxTokenBudget(2048));
    e.add_rule(DPlusRule::MaxSwarmPeers(4));
    e.add_rule(DPlusRule::NoMetaDriverUnsafe);
    e
}
