//! Phase 3: Auto-healing and patch suggestion layer.

use super::state_machine::RuntimeEventKind;
use super::vm::ExecutionContext;
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct HealingPatch {
    pub trigger: RuntimeEventKind,
    pub summary: String,
    pub applied: bool,
}

#[derive(Debug, Default, Clone)]
pub struct AutoHealer {
    learned: HashMap<RuntimeEventKind, String>,
    history: Vec<HealingPatch>,
}

impl AutoHealer {
    pub fn new() -> Self {
        let mut learned = HashMap::new();
        learned.insert(RuntimeEventKind::CpuSpike, "reduce_cpu_budget_10_percent".into());
        learned.insert(RuntimeEventKind::MemoryPressure, "trigger_gc_and_trim_warm_zone".into());
        learned.insert(RuntimeEventKind::DivergenceDetected, "apply_safe_mode_guardrails".into());
        learned.insert(RuntimeEventKind::HealthDrop, "increase_sampling_and_free_hot_cache".into());
        Self {
            learned,
            history: Vec::new(),
        }
    }

    pub fn learn_patch(&mut self, trigger: RuntimeEventKind, patch: String) {
        self.learned.insert(trigger, patch);
    }

    pub fn suggest_patch(&self, trigger: RuntimeEventKind) -> Option<&str> {
        self.learned.get(&trigger).map(|s| s.as_str())
    }

    pub fn apply_patch(&mut self, trigger: RuntimeEventKind, ctx: &mut ExecutionContext) -> Option<HealingPatch> {
        let patch = self.suggest_patch(trigger)?.to_string();

        match trigger {
            RuntimeEventKind::CpuSpike => {
                ctx.cpu_remaining = (ctx.cpu_remaining as f32 * 0.90) as u64;
            }
            RuntimeEventKind::MemoryPressure => {
                ctx.memory_used = (ctx.memory_used as f32 * 0.85) as u64;
            }
            RuntimeEventKind::DivergenceDetected => {
                ctx.health = (ctx.health * 0.98).max(0.0);
            }
            RuntimeEventKind::HealthDrop => {
                ctx.health = (ctx.health + 0.03).min(1.0);
            }
            RuntimeEventKind::LatencyReduced | RuntimeEventKind::MetricsGreen => {}
        }

        let record = HealingPatch {
            trigger,
            summary: patch,
            applied: true,
        };
        self.history.push(record.clone());
        Some(record)
    }

    pub fn history(&self) -> &[HealingPatch] {
        &self.history
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::dplus_compiler::vm::{ExecutionContext, ExecutionMode};

    #[test]
    fn patch_changes_context() {
        let mut healer = AutoHealer::new();
        let mut ctx = ExecutionContext {
            cpu_budget: 1_000_000,
            cpu_remaining: 500_000,
            memory_quota: 10 * 1024 * 1024,
            memory_used: 8 * 1024 * 1024,
            thermal_state: 70,
            health: 0.7,
            mode: ExecutionMode::Normal,
        };

        let before = ctx.cpu_remaining;
        let patch = healer.apply_patch(RuntimeEventKind::CpuSpike, &mut ctx);
        assert!(patch.is_some());
        assert!(ctx.cpu_remaining < before);
    }
}
