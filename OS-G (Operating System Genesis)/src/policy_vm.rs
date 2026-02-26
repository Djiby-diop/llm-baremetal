use crate::types::{CellId, MemIntent};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum PolicyError {
    ProgramTooLong,
    InvalidOpcode,
    StackUnderflow,
    StackOverflow,
    MissingTerminalAllow,
    BadImmediate,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum PolicyVerdict {
    Allow,
    Deny,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct PolicyOutcome {
    pub verdict: PolicyVerdict,
    pub force_sandbox: bool,
}

impl PolicyOutcome {
    pub const fn allow() -> Self {
        Self {
            verdict: PolicyVerdict::Allow,
            force_sandbox: false,
        }
    }

    pub const fn deny() -> Self {
        Self {
            verdict: PolicyVerdict::Deny,
            force_sandbox: false,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Op {
    PushImm = 0x01,
    LoadBytes = 0x02,
    LoadTtl = 0x03,
    LoadCell = 0x04,

    Gt = 0x10,
    Le = 0x11,
    Eq = 0x12,

    And = 0x20,
    Or = 0x21,
    Not = 0x22,

    SandboxIfTrue = 0x30,
    DenyIfTrue = 0x31,

    Allow = 0x40,
}

impl Op {
    fn from_u8(v: u8) -> Option<Self> {
        match v {
            0x01 => Some(Op::PushImm),
            0x02 => Some(Op::LoadBytes),
            0x03 => Some(Op::LoadTtl),
            0x04 => Some(Op::LoadCell),
            0x10 => Some(Op::Gt),
            0x11 => Some(Op::Le),
            0x12 => Some(Op::Eq),
            0x20 => Some(Op::And),
            0x21 => Some(Op::Or),
            0x22 => Some(Op::Not),
            0x30 => Some(Op::SandboxIfTrue),
            0x31 => Some(Op::DenyIfTrue),
            0x40 => Some(Op::Allow),
            _ => None,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Insn {
    pub op: u8,
    pub imm: u64,
}

impl Insn {
    pub const fn new(op: Op, imm: u64) -> Self {
        Self {
            op: op as u8,
            imm,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct PolicyProgram<const N: usize> {
    enabled: bool,
    len: usize,
    insns: [Insn; N],
}

impl<const N: usize> PolicyProgram<N> {
    pub const fn new_disabled() -> Self {
        Self {
            enabled: false,
            len: 0,
            insns: [Insn { op: 0, imm: 0 }; N],
        }
    }

    pub fn clear(&mut self) {
        self.enabled = false;
        self.len = 0;
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    pub fn load_from_insns(&mut self, insns: &[Insn]) -> Result<(), PolicyError> {
        if insns.len() > N {
            return Err(PolicyError::ProgramTooLong);
        }

        for i in 0..insns.len() {
            self.insns[i] = insns[i];
        }
        self.len = insns.len();
        self.enabled = self.len != 0;

        self.verify()?;
        Ok(())
    }

    pub fn insns(&self) -> &[Insn] {
        &self.insns[..self.len]
    }

    pub fn verify(&self) -> Result<(), PolicyError> {
        if !self.enabled {
            return Ok(());
        }
        if self.len == 0 {
            return Err(PolicyError::ProgramTooLong);
        }
        if self.insns[self.len - 1].op != (Op::Allow as u8) {
            return Err(PolicyError::MissingTerminalAllow);
        }

        // Stack depth verifier: ensures no underflow and bounded maximum.
        // We treat bool/int as u64.
        let mut depth: i32 = 0;
        let mut max_depth: i32 = 0;

        for i in 0..self.len {
            let insn = self.insns[i];
            let op = Op::from_u8(insn.op).ok_or(PolicyError::InvalidOpcode)?;
            match op {
                Op::PushImm => {
                    depth += 1;
                }
                Op::LoadBytes | Op::LoadTtl | Op::LoadCell => {
                    depth += 1;
                }
                Op::Gt | Op::Le | Op::Eq | Op::And | Op::Or => {
                    depth -= 1; // consumes two, produces one
                }
                Op::Not => {
                    // consumes one, produces one
                }
                Op::SandboxIfTrue | Op::DenyIfTrue => {
                    depth -= 1; // consumes one, produces nothing
                }
                Op::Allow => {
                    // terminal; allow extra stack items but keep bounded checks
                }
            }

            if depth < 0 {
                return Err(PolicyError::StackUnderflow);
            }
            if depth > 32 {
                return Err(PolicyError::StackOverflow);
            }
            max_depth = core::cmp::max(max_depth, depth);
        }

        // A non-empty, sensible program should never require huge stack.
        let _ = max_depth;
        Ok(())
    }

    pub fn eval(&self, intent: &MemIntent) -> Result<PolicyOutcome, PolicyError> {
        if !self.enabled {
            return Ok(PolicyOutcome::allow());
        }
        self.verify()?;

        // Small fixed stack.
        let mut stack = [0u64; 32];
        let mut sp: usize = 0;

        #[inline]
        fn push(stack: &mut [u64; 32], sp: &mut usize, v: u64) -> Result<(), PolicyError> {
            if *sp >= 32 {
                return Err(PolicyError::StackOverflow);
            }
            stack[*sp] = v;
            *sp += 1;
            Ok(())
        }

        #[inline]
        fn pop(sp: &mut usize) -> Result<usize, PolicyError> {
            if *sp == 0 {
                return Err(PolicyError::StackUnderflow);
            }
            *sp -= 1;
            Ok(*sp)
        }

        let mut force_sandbox = false;

        for insn in self.insns() {
            let op = Op::from_u8(insn.op).ok_or(PolicyError::InvalidOpcode)?;
            match op {
                Op::PushImm => {
                    push(&mut stack, &mut sp, insn.imm)?;
                }
                Op::LoadBytes => {
                    push(&mut stack, &mut sp, intent.bytes)?;
                }
                Op::LoadTtl => {
                    push(&mut stack, &mut sp, intent.ttl_ticks)?;
                }
                Op::LoadCell => {
                    push(&mut stack, &mut sp, intent.owner as u64)?;
                }
                Op::Gt | Op::Le | Op::Eq | Op::And | Op::Or => {
                    let bi = pop(&mut sp)?;
                    let ai = pop(&mut sp)?;
                    let a = stack[ai];
                    let b = stack[bi];
                    let out = match op {
                        Op::Gt => (a > b) as u64,
                        Op::Le => (a <= b) as u64,
                        Op::Eq => (a == b) as u64,
                        Op::And => ((a != 0) && (b != 0)) as u64,
                        Op::Or => ((a != 0) || (b != 0)) as u64,
                        _ => 0,
                    };
                    push(&mut stack, &mut sp, out)?;
                }
                Op::Not => {
                    let ai = pop(&mut sp)?;
                    let a = stack[ai];
                    push(&mut stack, &mut sp, (a == 0) as u64)?;
                }
                Op::SandboxIfTrue => {
                    let ai = pop(&mut sp)?;
                    let a = stack[ai];
                    if a != 0 {
                        force_sandbox = true;
                    }
                }
                Op::DenyIfTrue => {
                    let ai = pop(&mut sp)?;
                    let a = stack[ai];
                    if a != 0 {
                        return Ok(PolicyOutcome {
                            verdict: PolicyVerdict::Deny,
                            force_sandbox,
                        });
                    }
                }
                Op::Allow => {
                    return Ok(PolicyOutcome {
                        verdict: PolicyVerdict::Allow,
                        force_sandbox,
                    });
                }
            }
        }

        // Verify enforces terminal Allow.
        Ok(PolicyOutcome {
            verdict: PolicyVerdict::Allow,
            force_sandbox,
        })
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum CompileError {
    TooManyInsns,
    UnknownToken,
    InvalidNumber,
    PolicyError(PolicyError),
}

impl From<PolicyError> for CompileError {
    fn from(e: PolicyError) -> Self {
        CompileError::PolicyError(e)
    }
}

#[inline]
fn is_ws(b: u8) -> bool {
    b == b' ' || b == b'\t' || b == b'\r' || b == b'\n'
}

fn strip_line_comment(line: &str) -> &str {
    let trimmed = line.trim_start();
    if trimmed.starts_with('#') || trimmed.starts_with(';') {
        return "";
    }

    let mut cut = line.len();
    if let Some(pos) = line.find("//") {
        cut = core::cmp::min(cut, pos);
    }
    if let Some(pos) = line.find("/*") {
        cut = core::cmp::min(cut, pos);
    }
    &line[..cut]
}

fn parse_u64_token(tok: &str) -> Option<u64> {
    let t = tok.trim();
    if t.is_empty() {
        return None;
    }
    let mut v: u64 = 0;
    let mut any = false;
    for c in t.chars() {
        if c.is_ascii_digit() {
            any = true;
            v = v.saturating_mul(10).saturating_add((c as u64) - ('0' as u64));
        } else {
            return None;
        }
    }
    any.then_some(v)
}

/// Compile a minimal RPN policy program.
///
/// Tokens (whitespace-separated):
/// - `bytes`, `ttl`, `cell`
/// - integer literals
/// - operators: `>`, `<=`, `==`, `&&`, `||`, `!`
/// - actions: `deny_if_true`, `sandbox_if_true`, `allow`
///
/// Example (deny allocations > 262144 bytes):
/// `bytes 262144 > deny_if_true allow`
pub fn compile_rpn<const N: usize>(source: &str, out: &mut PolicyProgram<N>) -> Result<(), CompileError> {
    let mut insns: [Insn; N] = [Insn { op: 0, imm: 0 }; N];
    let mut n = 0usize;

    for raw_line in source.lines() {
        let line = strip_line_comment(raw_line);
        let bytes = line.as_bytes();
        let mut i = 0usize;
        while i < bytes.len() {
            while i < bytes.len() && is_ws(bytes[i]) {
                i += 1;
            }
            if i >= bytes.len() {
                break;
            }
            let start = i;
            while i < bytes.len() && !is_ws(bytes[i]) {
                i += 1;
            }
            let tok = &line[start..i];
            if tok.is_empty() {
                continue;
            }

            let (op, imm) = match tok {
                "bytes" | "BYTES" => (Op::LoadBytes, 0),
                "ttl" | "TTL" | "ttl_ticks" | "TTL_TICKS" => (Op::LoadTtl, 0),
                "cell" | "CELL" | "owner" | "OWNER" => (Op::LoadCell, 0),
                ">" => (Op::Gt, 0),
                "<=" => (Op::Le, 0),
                "==" => (Op::Eq, 0),
                "&&" => (Op::And, 0),
                "||" => (Op::Or, 0),
                "!" => (Op::Not, 0),
                "deny_if_true" | "DENY_IF_TRUE" | "deny" | "DENY" => (Op::DenyIfTrue, 0),
                "sandbox_if_true" | "SANDBOX_IF_TRUE" | "sandbox" | "SANDBOX" => {
                    (Op::SandboxIfTrue, 0)
                }
                "allow" | "ALLOW" => (Op::Allow, 0),
                _ => {
                    if let Some(v) = parse_u64_token(tok) {
                        (Op::PushImm, v)
                    } else {
                        return Err(CompileError::UnknownToken);
                    }
                }
            };

            if n >= N {
                return Err(CompileError::TooManyInsns);
            }
            insns[n] = Insn::new(op, imm);
            n += 1;
        }
    }

    out.load_from_insns(&insns[..n])?;
    Ok(())
}

pub fn policy_cell_id(intent: &MemIntent) -> CellId {
    intent.owner
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn policy_denies_large_allocations() {
        let mut p: PolicyProgram<32> = PolicyProgram::new_disabled();
        compile_rpn("bytes 4096 > deny_if_true allow", &mut p).unwrap();
        assert!(p.is_enabled());

        let mut small = MemIntent::new(1, 1024);
        let out_small = p.eval(&small).unwrap();
        assert_eq!(out_small.verdict, PolicyVerdict::Allow);

        small.bytes = 8192;
        let out_big = p.eval(&small).unwrap();
        assert_eq!(out_big.verdict, PolicyVerdict::Deny);
    }

    #[test]
    fn policy_can_force_sandbox() {
        let mut p: PolicyProgram<32> = PolicyProgram::new_disabled();
        compile_rpn("bytes 4096 > sandbox_if_true allow", &mut p).unwrap();

        let intent = MemIntent::new(1, 8192);
        let out = p.eval(&intent).unwrap();
        assert_eq!(out.verdict, PolicyVerdict::Allow);
        assert!(out.force_sandbox);
    }

    #[test]
    fn verifier_rejects_missing_allow() {
        let mut p: PolicyProgram<32> = PolicyProgram::new_disabled();
        let err = compile_rpn("bytes 1 > deny_if_true", &mut p).unwrap_err();
        match err {
            CompileError::PolicyError(PolicyError::MissingTerminalAllow) => {}
            _ => panic!("unexpected err: {err:?}"),
        }
    }
}
