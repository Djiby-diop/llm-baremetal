// src/dplus_compiler/executor.rs
//! High-level executor combining VM, consensus, and policy enforcement

use super::vm::*;
use super::bytecode::*;
use super::state_machine::{RuntimeEvent, RuntimeEventKind, RuntimeState, StateMachine};
use super::auto_heal::{AutoHealer, HealingPatch};
use super::CompileError;
use std::collections::HashMap;

/// Policy executor with full lifecycle management
pub struct PolicyExecutor {
    pub vm: DppVM,
    policy_name: String,
    action_counter: u64,
    divergence_log: Vec<DivergenceEvent>,
    state_machine: StateMachine,
    auto_healer: AutoHealer,
    recent_events: Vec<RuntimeEvent>,
}

/// Divergence event (when judges disagree)
#[derive(Debug, Clone)]
pub struct DivergenceEvent {
    pub action_id: String,
    pub votes: Vec<Vote>,
    pub trigger_learning: bool,
    pub auto_patch_suggested: String,
}

/// Execution result
#[derive(Debug, Clone)]
pub struct ExecutionResult {
    pub action_id: String,
    pub verdict: Verdict,
    pub consensus: ConsensusResult,
    pub duration_ns: u64,
    pub success: bool,
    pub error: Option<String>,
}

impl PolicyExecutor {
    pub fn new(policy_name: String, module: &BytecodeModule) -> Result<Self, CompileError> {
        Ok(PolicyExecutor {
            vm: DppVM::new(module)?,
            policy_name,
            action_counter: 0,
            divergence_log: Vec::new(),
            state_machine: StateMachine::new(RuntimeState::Idle),
            auto_healer: AutoHealer::new(),
            recent_events: Vec::new(),
        })
    }
    
    /// Execute a policy action with full consensus
    pub fn execute_action(
        &mut self,
        action_id: &str,
        bytecode: Vec<Bytecode>,
    ) -> Result<ExecutionResult, CompileError> {
        let start = std::time::Instant::now();
        self.action_counter += 1;
        
        // Phase 1: Pre-execution checks
        if let Err(e) = self.pre_execution_checks() {
            return Ok(ExecutionResult {
                action_id: action_id.to_string(),
                verdict: Verdict::Emergency,
                consensus: ConsensusResult {
                    votes: vec![],
                    final_verdict: Verdict::Emergency,
                    unanimous: true,
                    divergence: false,
                },
                duration_ns: start.elapsed().as_nanos() as u64,
                success: false,
                error: Some(e.to_string()),
            });
        }

        // Drive to NORMAL once runtime metrics are sane.
        self.push_event(RuntimeEventKind::MetricsGreen, 1.0, "startup");
        self.process_events();
        
        // Phase 2: Consensus voting
        let consensus_result = match self.vm.run_consensus(action_id) {
            Ok(result) => result,
            Err(e) => {
                return Ok(ExecutionResult {
                    action_id: action_id.to_string(),
                    verdict: Verdict::Emergency,
                    consensus: ConsensusResult {
                        votes: vec![],
                        final_verdict: Verdict::Emergency,
                        unanimous: true,
                        divergence: false,
                    },
                    duration_ns: start.elapsed().as_nanos() as u64,
                    success: false,
                    error: Some(e.to_string()),
                });
            }
        };
        
        // Phase 3: Divergence handling
        if consensus_result.divergence {
            self.handle_divergence(action_id, &consensus_result);
            self.push_event(RuntimeEventKind::DivergenceDetected, 1.0, action_id);
        }

        self.detect_resource_events();
        self.process_events();
        
        // Phase 4: Action execution (if allowed)
        let success = if consensus_result.final_verdict.is_allowed() {
            match self.vm.execute(bytecode) {
                Ok(_) => true,
                Err(_) => false,
            }
        } else {
            false
        };
        
        // Phase 5: Audit logging
        self.vm.log_action_with_reason(
            action_id,
            consensus_result.final_verdict,
            MemoryZone::Journal,
            Self::format_consensus_reasoning(&consensus_result),
        );
        
        // Phase 6: Health check
        self.update_health(&consensus_result);

        if self.get_context().health < 0.30 {
            self.push_event(RuntimeEventKind::HealthDrop, self.get_context().health, action_id);
            self.process_events();
        }
        
        Ok(ExecutionResult {
            action_id: action_id.to_string(),
            verdict: consensus_result.final_verdict,
            consensus: consensus_result,
            duration_ns: start.elapsed().as_nanos() as u64,
            success,
            error: None,
        })
    }

    fn format_consensus_reasoning(consensus: &ConsensusResult) -> String {
        let vote_details = consensus
            .votes
            .iter()
            .map(|vote| {
                format!(
                    "{:?}={:?} ({})",
                    vote.judge, vote.verdict, vote.reasoning
                )
            })
            .collect::<Vec<_>>()
            .join("; ");

        format!(
            "Consensus verdict={:?}, unanimous={}, divergence={}; {}",
            consensus.final_verdict,
            consensus.unanimous,
            consensus.divergence,
            vote_details
        )
    }
    
    fn pre_execution_checks(&self) -> Result<(), CompileError> {
        let ctx = self.vm.get_context();

        self.state_machine.ensure_viable()?;
        
        // Check CPU budget
        if ctx.cpu_remaining < 1000 {
            return Err(CompileError::RuntimeError("CPU budget exhausted".into()));
        }
        
        // Check mode
        if ctx.mode == ExecutionMode::Recovery && ctx.health < 0.2 {
            return Err(CompileError::RuntimeError("System in critical recovery mode".into()));
        }
        
        Ok(())
    }
    
    fn handle_divergence(&mut self, action_id: &str, consensus: &ConsensusResult) {
        let verdicts_str = consensus
            .votes
            .iter()
            .map(|v| format!("{:?}", v.verdict))
            .collect::<Vec<_>>()
            .join(", ");
        
        let auto_patch = match consensus.final_verdict {
            Verdict::Throttle => format!(
                "// Detected divergence in {}: judges voted [{}]. Auto-patch: reduce CPU usage",
                action_id, verdicts_str
            ),
            Verdict::Compensate => format!(
                "// Detected divergence in {}: judges voted [{}]. Auto-patch: enable compensation",
                action_id, verdicts_str
            ),
            _ => format!(
                "// Divergence detected in {}: judges voted [{}]",
                action_id, verdicts_str
            ),
        };
        
        let event = DivergenceEvent {
            action_id: action_id.to_string(),
            votes: consensus.votes.clone(),
            trigger_learning: true,
            auto_patch_suggested: auto_patch,
        };
        
        self.divergence_log.push(event);

        // Learn a better patch proposal when divergence pattern is known.
        self.auto_healer.learn_patch(
            RuntimeEventKind::DivergenceDetected,
            format!("patch:{}:tighten_consensus_threshold", action_id),
        );
    }
    
    fn update_health(&mut self, consensus: &ConsensusResult) {
        let ctx = self.vm.get_context_mut();
        
        // Reduce health if any judge forbid
        if consensus
            .votes
            .iter()
            .any(|v| v.verdict == Verdict::Forbid)
        {
            ctx.health *= 0.95;
        }
        
        // Improve health if all judges agree positively
        if consensus.unanimous && consensus.final_verdict == Verdict::Allow {
            ctx.health = (ctx.health + 0.01).min(1.0);
        }
        
        // Update mode based on health
        ctx.mode = match ctx.health {
            h if h >= 0.8 => ExecutionMode::Normal,
            h if h >= 0.5 => ExecutionMode::Degraded,
            h if h >= 0.2 => ExecutionMode::Safe,
            _ => ExecutionMode::Recovery,
        };
    }
    
    pub fn get_journal(&self) -> &AuditJournal {
        self.vm.get_journal()
    }

    pub fn get_latest_journal_entry(&self) -> Option<&JournalEntry> {
        self.vm.get_journal().entries().last()
    }

    pub fn get_journal_entries_for_action(&self, action_id: &str) -> Vec<&JournalEntry> {
        self.vm
            .get_journal()
            .entries()
            .iter()
            .filter(|entry| entry.action_id == action_id)
            .collect()
    }

    pub fn get_journal_entries_for_verdict(&self, verdict: Verdict) -> Vec<&JournalEntry> {
        self.vm
            .get_journal()
            .entries()
            .iter()
            .filter(|entry| entry.verdict == verdict)
            .collect()
    }
    
    pub fn get_divergences(&self) -> &[DivergenceEvent] {
        &self.divergence_log
    }
    
    pub fn get_context(&self) -> &ExecutionContext {
        self.vm.get_context()
    }

    pub fn get_state(&self) -> RuntimeState {
        self.state_machine.current()
    }

    pub fn get_recent_events(&self) -> &[RuntimeEvent] {
        &self.recent_events
    }

    pub fn get_healing_history(&self) -> &[HealingPatch] {
        self.auto_healer.history()
    }

    fn push_event(&mut self, kind: RuntimeEventKind, value: f32, source: &str) {
        self.recent_events.push(RuntimeEvent {
            kind,
            value,
            source: source.to_string(),
        });
    }

    fn detect_resource_events(&mut self) {
        let ctx = self.vm.get_context();
        let cpu_ratio = if ctx.cpu_budget == 0 {
            0.0
        } else {
            1.0 - (ctx.cpu_remaining as f32 / ctx.cpu_budget as f32)
        };

        let mem_ratio = if ctx.memory_quota == 0 {
            0.0
        } else {
            ctx.memory_used as f32 / ctx.memory_quota as f32
        };

        if cpu_ratio > 0.85 {
            self.push_event(RuntimeEventKind::CpuSpike, cpu_ratio, "resource_monitor");
        }
        if mem_ratio > 0.85 {
            self.push_event(RuntimeEventKind::MemoryPressure, mem_ratio, "resource_monitor");
        }
        if cpu_ratio < 0.35 && mem_ratio < 0.60 {
            self.push_event(RuntimeEventKind::LatencyReduced, cpu_ratio, "resource_monitor");
            self.push_event(RuntimeEventKind::MetricsGreen, 1.0, "resource_monitor");
        }
    }

    fn process_events(&mut self) {
        if self.recent_events.is_empty() {
            return;
        }

        let events = std::mem::take(&mut self.recent_events);
        for event in &events {
            self.state_machine.apply_event(event);
            let _ = self.auto_healer.apply_patch(event.kind, self.vm.get_context_mut());
        }

        // Keep a compact rolling event window.
        let keep = events.into_iter().rev().take(32).collect::<Vec<_>>();
        self.recent_events = keep.into_iter().rev().collect();
    }
    
    pub fn get_stats(&self) -> ExecutorStats {
        let journal = self.vm.get_journal();
        let ctx = self.vm.get_context();
        
        let verdicts: HashMap<String, usize> = journal
            .entries()
            .iter()
            .fold(HashMap::new(), |mut acc, entry| {
                let key = format!("{:?}", entry.verdict);
                let counter = acc.entry(key).or_insert(0);
                *counter += 1;
                acc
            });
        
        ExecutorStats {
            actions_executed: self.action_counter,
            divergences_detected: self.divergence_log.len(),
            current_health: ctx.health,
            current_mode: ctx.mode,
            cpu_remaining: ctx.cpu_remaining,
            memory_used: ctx.memory_used,
            memory_quota: ctx.memory_quota,
            verdict_distribution: verdicts,
        }
    }
}

#[derive(Debug, Clone)]
pub struct ExecutorStats {
    pub actions_executed: u64,
    pub divergences_detected: usize,
    pub current_health: f32,
    pub current_mode: ExecutionMode,
    pub cpu_remaining: u64,
    pub memory_used: u64,
    pub memory_quota: u64,
    pub verdict_distribution: HashMap<String, usize>,
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_executor_creation() {
        let module = BytecodeModule::new("test_policy");
        let executor = PolicyExecutor::new("TestPolicy".to_string(), &module);
        assert!(executor.is_ok());
    }
    
    #[test]
    fn test_pre_execution_checks() {
        let module = BytecodeModule::new("test");
        let executor = PolicyExecutor::new("Test".to_string(), &module).unwrap();
        
        let result = executor.pre_execution_checks();
        assert!(result.is_ok()); // Normal state should pass
    }
    
    #[test]
    fn test_health_degradation() {
        let module = BytecodeModule::new("test");
        let mut executor = PolicyExecutor::new("Test".to_string(), &module).unwrap();
        
        let initial_health = executor.get_context().health;
        
        // Create a consensus with forbid vote
        let forbid_vote = Vote {
            judge: JudgeType::Law,
            verdict: Verdict::Forbid,
            reasoning: "Test".into(),
        };
        let allow_votes = vec![
            Vote {
                judge: JudgeType::Proof,
                verdict: Verdict::Allow,
                reasoning: "Test".into(),
            },
            Vote {
                judge: JudgeType::Cortex,
                verdict: Verdict::Allow,
                reasoning: "Test".into(),
            },
        ];
        
        let mut votes = vec![forbid_vote];
        votes.extend(allow_votes);
        
        let consensus = ConsensusResult::new(votes);
        executor.update_health(&consensus);
        
        let final_health = executor.get_context().health;
        assert!(final_health < initial_health);
    }
    
    #[test]
    fn test_stats() {
        let module = BytecodeModule::new("test");
        let executor = PolicyExecutor::new("Test".to_string(), &module).unwrap();
        
        let stats = executor.get_stats();
        assert_eq!(stats.actions_executed, 0);
        assert_eq!(stats.divergences_detected, 0);
    }

    #[test]
    fn test_state_machine_reacts_to_events() {
        let module = BytecodeModule::new("test");
        let mut executor = PolicyExecutor::new("Test".to_string(), &module).unwrap();

        executor.push_event(RuntimeEventKind::MetricsGreen, 1.0, "test");
        executor.process_events();
        assert_eq!(executor.get_state(), RuntimeState::Normal);
    }

    #[test]
    fn test_journal_contains_consensus_reasoning() {
        let module = BytecodeModule::new("test");
        let mut executor = PolicyExecutor::new("Test".to_string(), &module).unwrap();

        let result = executor.execute_action(
            "action_consensus_log",
            vec![Bytecode::LoadBool(true), Bytecode::Return],
        );
        assert!(result.is_ok());

        let journal = executor.get_journal();
        assert!(!journal.entries().is_empty());
        let last = journal.entries().last().unwrap();
        assert_eq!(last.action_id, "action_consensus_log");
        assert!(last.reasoning.contains("Consensus verdict="));
        assert!(last.reasoning.contains("Law="));
        assert!(last.reasoning.contains("Proof="));
        assert!(last.reasoning.contains("Cortex="));
    }

    #[test]
    fn test_journal_filters_by_action_and_verdict() {
        let module = BytecodeModule::new("test");
        let mut executor = PolicyExecutor::new("Test".to_string(), &module).unwrap();

        let _ = executor.execute_action(
            "action_a",
            vec![Bytecode::LoadBool(true), Bytecode::Return],
        );
        let _ = executor.execute_action(
            "action_b",
            vec![Bytecode::LoadBool(true), Bytecode::Return],
        );

        let action_a_entries = executor.get_journal_entries_for_action("action_a");
        assert_eq!(action_a_entries.len(), 1);
        assert_eq!(action_a_entries[0].action_id, "action_a");

        let allow_entries = executor.get_journal_entries_for_verdict(Verdict::Allow);
        assert!(allow_entries.len() >= 2);

        let latest = executor.get_latest_journal_entry();
        assert!(latest.is_some());
        assert_eq!(latest.unwrap().action_id, "action_b");
    }
}
