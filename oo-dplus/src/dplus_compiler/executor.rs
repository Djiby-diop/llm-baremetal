// src/dplus_compiler/executor.rs
//! High-level executor combining VM, consensus, and policy enforcement

use super::vm::*;
use super::bytecode::*;
use super::CompileError;
use std::collections::HashMap;

/// Policy executor with full lifecycle management
pub struct PolicyExecutor {
    pub vm: DppVM,
    policy_name: String,
    action_counter: u64,
    divergence_log: Vec<DivergenceEvent>,
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
        }
        
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
        self.vm.log_action(action_id, consensus_result.final_verdict, MemoryZone::Journal);
        
        // Phase 6: Health check
        self.update_health(&consensus_result);
        
        Ok(ExecutionResult {
            action_id: action_id.to_string(),
            verdict: consensus_result.final_verdict,
            consensus: consensus_result,
            duration_ns: start.elapsed().as_nanos() as u64,
            success,
            error: None,
        })
    }
    
    fn pre_execution_checks(&self) -> Result<(), CompileError> {
        let ctx = self.vm.get_context();
        
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
    
    pub fn get_divergences(&self) -> &[DivergenceEvent] {
        &self.divergence_log
    }
    
    pub fn get_context(&self) -> &ExecutionContext {
        self.vm.get_context()
    }
    
    pub fn get_stats(&self) -> ExecutorStats {
        let journal = self.vm.get_journal();
        let ctx = self.vm.get_context();
        
        let verdicts: HashMap<&str, usize> = journal
            .entries()
            .iter()
            .fold(HashMap::new(), |mut acc, entry| {
                let key = format!("{:?}", entry.verdict);
                let counter = acc.entry(&key).or_insert(0);
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
}
