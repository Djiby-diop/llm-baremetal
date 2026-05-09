// src/dplus_compiler/vm.rs
//! D++ Bytecode Virtual Machine - Stack-based executor with consensus

use super::bytecode::*;
use super::CompileError;
use std::collections::HashMap;

/// Memory zone types
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum MemoryZone {
    Frozen,    // Immutable (laws, genome)
    Cold,      // Read-heavy (model weights)
    Warm,      // Frequently accessed (KV cache, state)
    Hot,       // Real-time (current action, verdict)
    Sentinel,  // Guarded boundaries
    Journal,   // Audit log
}

/// Execution verdict (9 levels)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    Allow,      // ✅ Execute
    AllowWarn,  // ✅ Execute + warn
    Defer,      // ⏳ Report
    Throttle,   // ⚠️ Slow down
    Monitor,    // 👁️ Observe
    Quarantine, // 🔒 Isolate
    Compensate, // 🔧 Correct + execute
    Forbid,     // ❌ Block
    Emergency,  // 🚨 Stop
}

impl Verdict {
    pub fn severity(&self) -> u8 {
        match self {
            Verdict::Allow => 0,
            Verdict::AllowWarn => 1,
            Verdict::Defer => 2,
            Verdict::Throttle => 3,
            Verdict::Monitor => 4,
            Verdict::Quarantine => 5,
            Verdict::Compensate => 6,
            Verdict::Forbid => 7,
            Verdict::Emergency => 8,
        }
    }

    pub fn is_allowed(&self) -> bool {
        matches!(self, Verdict::Allow | Verdict::AllowWarn | Verdict::Compensate)
    }
}

/// Judge types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum JudgeType {
    Law,        // Prolog-like rule evaluation
    Proof,      // Formal invariant verification
    Cortex,     // Heuristic reasoning
}

/// Vote from a single judge
#[derive(Debug, Clone)]
pub struct Vote {
    pub judge: JudgeType,
    pub verdict: Verdict,
    pub reasoning: String,
}

/// Consensus result
#[derive(Debug, Clone)]
pub struct ConsensusResult {
    pub votes: Vec<Vote>,
    pub final_verdict: Verdict,
    pub unanimous: bool,
    pub divergence: bool,
}

impl ConsensusResult {
    pub fn new(votes: Vec<Vote>) -> Self {
        let verdicts: Vec<_> = votes.iter().map(|v| v.verdict).collect();
        
        // Check unanimity
        let unanimous = verdicts.windows(2).all(|w| w[0] == w[1]);
        
        // Check divergence (if not unanimous)
        let divergence = !unanimous;
        
        // Merge verdicts (majority voting on severity)
        let avg_severity = verdicts.iter().map(|v| v.severity() as f32).sum::<f32>() / verdicts.len() as f32;
        let final_verdict = match avg_severity as u8 {
            0 => Verdict::Allow,
            1 => Verdict::AllowWarn,
            2 => Verdict::Defer,
            3 => Verdict::Throttle,
            4 => Verdict::Monitor,
            5 => Verdict::Quarantine,
            6 => Verdict::Compensate,
            7 => Verdict::Forbid,
            _ => Verdict::Emergency,
        };
        
        ConsensusResult {
            votes,
            final_verdict,
            unanimous,
            divergence,
        }
    }
}

/// Stack-based virtual machine
pub struct DppVM {
    /// Program counter
    pc: usize,
    
    /// Value stack
    stack: Vec<f64>,
    
    /// Local variables
    locals: Vec<f64>,
    
    /// Bytecode being executed
    bytecode: Vec<Bytecode>,
    
    /// Function table
    functions: HashMap<String, BytecodeFunction>,
    
    /// Execution context (quotas, zones, state)
    context: ExecutionContext,
    
    /// Memory allocations per zone
    zone_usage: HashMap<MemoryZone, u64>,
    
    /// Judges (law, proof, cortex)
    judges: Vec<JudgeType>,
    
    /// Audit journal
    journal: AuditJournal,
}

/// Execution context with quotas and state
#[derive(Debug, Clone)]
pub struct ExecutionContext {
    pub cpu_budget: u64,
    pub cpu_remaining: u64,
    pub memory_quota: u64,
    pub memory_used: u64,
    pub thermal_state: u8, // 0-100
    pub health: f32,       // 0.0-1.0
    pub mode: ExecutionMode,
}

/// Execution mode
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExecutionMode {
    Normal,
    Degraded,
    Safe,
    Recovery,
}

/// Audit journal entry
#[derive(Debug, Clone)]
pub struct JournalEntry {
    pub timestamp: u64,
    pub action_id: String,
    pub verdict: Verdict,
    pub zone: MemoryZone,
    pub reasoning: String,
}

/// Audit journal
#[derive(Debug, Clone)]
pub struct AuditJournal {
    entries: Vec<JournalEntry>,
    max_size: usize,
}

impl AuditJournal {
    pub fn new(max_size: usize) -> Self {
        AuditJournal {
            entries: Vec::new(),
            max_size,
        }
    }
    
    pub fn log(&mut self, entry: JournalEntry) {
        if self.entries.len() >= self.max_size {
            self.entries.remove(0); // FIFO eviction
        }
        self.entries.push(entry);
    }
    
    pub fn entries(&self) -> &[JournalEntry] {
        &self.entries
    }
}

impl DppVM {
    pub fn new(module: &BytecodeModule) -> Result<Self, CompileError> {
        Ok(DppVM {
            pc: 0,
            stack: Vec::with_capacity(1024),
            locals: Vec::with_capacity(256),
            bytecode: Vec::new(),
            functions: module.functions.clone(),
            context: ExecutionContext {
                cpu_budget: 1_000_000,
                cpu_remaining: 1_000_000,
                memory_quota: 10 * 1024 * 1024, // 10MB
                memory_used: 0,
                thermal_state: 50,
                health: 1.0,
                mode: ExecutionMode::Normal,
            },
            zone_usage: HashMap::new(),
            judges: vec![JudgeType::Law, JudgeType::Proof, JudgeType::Cortex],
            journal: AuditJournal::new(10000),
        })
    }
    
    pub fn push(&mut self, value: f64) {
        self.stack.push(value);
    }
    
    pub fn pop(&mut self) -> Result<f64, CompileError> {
        self.stack.pop()
            .ok_or_else(|| CompileError::RuntimeError("Stack underflow".into()))
    }
    
    pub fn execute(&mut self, bytecode: Vec<Bytecode>) -> Result<Verdict, CompileError> {
        self.bytecode = bytecode;
        self.pc = 0;
        
        while self.pc < self.bytecode.len() {
            let instr = self.bytecode[self.pc].clone();
            self.pc += 1;
            
            match instr {
                Bytecode::LoadConst(v) => self.push(v),
                
                Bytecode::LoadBool(b) => self.push(if b { 1.0 } else { 0.0 }),
                
                Bytecode::Add => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(a + b);
                }
                
                Bytecode::Sub => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(a - b);
                }
                
                Bytecode::Mul => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(a * b);
                }
                
                Bytecode::Div => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    if b == 0.0 {
                        return Err(CompileError::RuntimeError("Division by zero".into()));
                    }
                    self.push(a / b);
                }
                
                Bytecode::CmpEq => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(if (a - b).abs() < 1e-10 { 1.0 } else { 0.0 });
                }
                
                Bytecode::CmpLt => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(if a < b { 1.0 } else { 0.0 });
                }
                
                Bytecode::CmpGt => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.push(if a > b { 1.0 } else { 0.0 });
                }
                
                Bytecode::Not => {
                    let a = self.pop()?;
                    self.push(if a == 0.0 { 1.0 } else { 0.0 });
                }
                
                Bytecode::Return => {
                    // Return top of stack as verdict
                    let verdict_val = self.pop()?;
                    let verdict = match verdict_val as u8 {
                        0 => Verdict::Allow,
                        1 => Verdict::AllowWarn,
                        2 => Verdict::Defer,
                        3 => Verdict::Throttle,
                        4 => Verdict::Monitor,
                        5 => Verdict::Quarantine,
                        6 => Verdict::Compensate,
                        7 => Verdict::Forbid,
                        _ => Verdict::Emergency,
                    };
                    return Ok(verdict);
                }
                
                Bytecode::ConsensusVote => {
                    // Trigger consensus voting
                    // For now, just pass through (full implementation in next phase)
                    self.push(0.0); // ALLOW verdict
                }
                
                Bytecode::Halt => return Ok(Verdict::Emergency),
                
                _ => {
                    return Err(CompileError::RuntimeError(format!(
                        "Unknown bytecode: {:?}",
                        instr
                    )))
                }
            }
        }
        
        Ok(Verdict::Allow)
    }
    
    pub fn evaluate_law(&self, _action_id: &str) -> Result<bool, CompileError> {
        // Stub: evaluate law rules from [LAW] section
        // Returns true if action passes all law checks
        Ok(true)
    }
    
    pub fn verify_proof(&self, _action_id: &str) -> Result<bool, CompileError> {
        // Stub: verify invariants from [PROOF] section
        // Returns true if all invariants hold
        Ok(true)
    }
    
    pub fn run_consensus(&mut self, _action_id: &str) -> Result<ConsensusResult, CompileError> {
        let mut votes = Vec::new();
        
        // Judge 1: Law evaluator
        let law_verdict = if self.evaluate_law(_action_id)? {
            Verdict::Allow
        } else {
            Verdict::Forbid
        };
        votes.push(Vote {
            judge: JudgeType::Law,
            verdict: law_verdict,
            reasoning: "Rule evaluation".into(),
        });
        
        // Judge 2: Proof verifier
        let proof_verdict = if self.verify_proof(_action_id)? {
            Verdict::Allow
        } else {
            Verdict::Quarantine
        };
        votes.push(Vote {
            judge: JudgeType::Proof,
            verdict: proof_verdict,
            reasoning: "Invariant check".into(),
        });
        
        // Judge 3: Cortex heuristic
        let cortex_verdict = if self.context.thermal_state < 80 {
            Verdict::Allow
        } else {
            Verdict::Throttle
        };
        votes.push(Vote {
            judge: JudgeType::Cortex,
            verdict: cortex_verdict,
            reasoning: "Heuristic analysis".into(),
        });
        
        Ok(ConsensusResult::new(votes))
    }
    
    pub fn allocate_memory(&mut self, bytes: u64, zone: MemoryZone) -> Result<(), CompileError> {
        let current = self.zone_usage.entry(zone).or_insert(0);
        
        if self.context.memory_used + bytes > self.context.memory_quota {
            return Err(CompileError::RuntimeError("Memory quota exceeded".into()));
        }
        
        if zone == MemoryZone::Frozen {
            return Err(CompileError::RuntimeError("Cannot allocate in frozen zone".into()));
        }
        
        *current += bytes;
        self.context.memory_used += bytes;
        
        Ok(())
    }
    
    pub fn deallocate_memory(&mut self, bytes: u64, zone: MemoryZone) -> Result<(), CompileError> {
        let current = self.zone_usage.entry(zone).or_insert(0);
        
        if *current < bytes {
            return Err(CompileError::RuntimeError("Deallocation underflow".into()));
        }
        
        *current -= bytes;
        self.context.memory_used -= bytes;
        
        Ok(())
    }
    
    pub fn log_action(&mut self, action_id: &str, verdict: Verdict, zone: MemoryZone) {
        let entry = JournalEntry {
            timestamp: 0, // Would be actual timestamp
            action_id: action_id.to_string(),
            verdict,
            zone,
            reasoning: format!("Verdict: {:?}", verdict),
        };
        self.journal.log(entry);
    }
    
    pub fn get_journal(&self) -> &AuditJournal {
        &self.journal
    }
    
    pub fn get_context(&self) -> &ExecutionContext {
        &self.context
    }
    
    pub fn get_context_mut(&mut self) -> &mut ExecutionContext {
        &mut self.context
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_vm_creation() {
        let module = BytecodeModule::new("main");
        let vm = DppVM::new(&module);
        assert!(vm.is_ok());
    }
    
    #[test]
    fn test_stack_operations() {
        let module = BytecodeModule::new("main");
        let mut vm = DppVM::new(&module).unwrap();
        
        vm.push(5.0);
        vm.push(3.0);
        
        let val = vm.pop().unwrap();
        assert_eq!(val, 3.0);
        
        let val = vm.pop().unwrap();
        assert_eq!(val, 5.0);
    }
    
    #[test]
    fn test_consensus() {
        let module = BytecodeModule::new("main");
        let mut vm = DppVM::new(&module).unwrap();
        
        let result = vm.run_consensus("test_action").unwrap();
        assert_eq!(result.votes.len(), 3);
        assert!(result.unanimous);
        assert_eq!(result.final_verdict, Verdict::Allow);
    }
    
    #[test]
    fn test_memory_allocation() {
        let module = BytecodeModule::new("main");
        let mut vm = DppVM::new(&module).unwrap();
        
        let result = vm.allocate_memory(1024, MemoryZone::Warm);
        assert!(result.is_ok());
        
        assert_eq!(vm.context.memory_used, 1024);
    }
}
