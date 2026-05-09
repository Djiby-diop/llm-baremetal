# D+ v2 Implementation Roadmap

> Langage constitutionnel pour organismes intelligents vivants

---

## 🎯 Vision Globale

**D+ v2** est un **langage polymorphe compilable** pour encoder les **lois immuables** et **verdicts adaptatifs** d'un organisme autonome.

- **D+** = Langage de spécification (humain + formel)
- **D++** = Bytecode exécutable (VM-agnostic)
- **D+++** = Code natif (hardware-specific)

---

## 📋 Roadmap (Par Phase)

### **PHASE 1: Fondations (Semaines 1-4)**

#### ✅ Done
- [x] Lexer complet (tokens, keywords, sections)
- [x] Parser LL(1) → AST
- [x] AST structures (Expr, Stmt, Section)
- [x] Bytecode IR (opcodes, functions)
- [x] Basic Compiler (AST → bytecode)

#### 📌 In Progress
- [ ] **Test suite** (lexer, parser, compiler unit tests)
- [ ] **Error recovery** (better error messages)
- [ ] **Type inference** (Hindley-Milner or simpler)

#### Next
- [ ] **Symbol resolution** (variable scoping, function lookup)
- [ ] **Optimization pass** (constant folding, dead code)

---

### **PHASE 2: VM Execution (Weeks 5-8)**

#### Objectives
- [ ] **D++ Bytecode interpreter** (stack-based VM)
- [ ] **Consensus voter** (3-judge system)
- [ ] **Memory zones enforcer** (frozen/cold/warm/hot)
- [ ] **Journal writer** (audit log)
- [ ] **Exception handler** (trap, signal, recovery)

#### Key Components
```rust
pub struct DppVM {
    pc: u64,                    // program counter
    stack: Vec<u64>,            // value stack
    locals: Vec<u64>,           // local variables
    context: ExecutionContext,  // memory zones, quotas, etc.
    judges: Vec<Box<dyn Judge>>,// consensus judges
    journal: AuditJournal,      // audit log
}
```

#### Consensus System
```
Action A
  ├─→ Judge 1 (LAW) → Verdict::Allow
  ├─→ Judge 2 (PROOF) → Verdict::Allow
  ├─→ Judge 3 (CORTEX) → Verdict::Defer
  └─→ Consensus: Divergence → Escalate to Human
```

---

### **PHASE 3: Smart Policies (Weeks 9-12)**

#### Objectives
- [ ] **State machines** (NORMAL → DEGRADED → SAFE → RECOVERY)
- [ ] **Event system** (on_cpu_spike, on_memory_pressure, etc.)
- [ ] **Contract semantics** (require/ensure/revert)
- [ ] **Auto-healing** (propose_patch, apply_patch)

#### Example Smart Policy
```dplus
[POLICY]
  name: "adaptive_throttle"
  state { cpu_usage: f32, thermal: ThermalState }
  
  event on_thermal_spike {
    if thermal > 85°C {
      enable_throttle()
      notify_human()
    }
  }
  
  contract energy_efficiency {
    require: cpu_usage <= 0.85
    ensure: thermal < 85°C
    revert: rollback_config
  }
```

---

### **PHASE 4: Multi-Language Support (Weeks 13-16)**

#### Objectives
- [ ] **Embedded Python** (via Python AST)
- [ ] **Embedded Rust** (via procedural macro)
- [ ] **Embedded Prolog** (via mini Prolog engine)
- [ ] **GPU kernels** (CUDA/OpenCL snippets)
- [ ] **x86 assembly** (inline asm with constraints)

#### Example: Polyglot D+
```dplus
[LANG:python]
def verify(action_id):
    return law_db[action_id].valid

[LANG:rust]
pub fn judge(action: &Action) -> Verdict {
    if verify_law(action) {
        Verdict::Allow
    } else {
        Verdict::Forbid
    }
}

[LANG:prolog]
can_access(U, R) :- capability(U, R), \+ revoked(U).

[LANG:cuda_kernel]
__global__ void merge_verdicts(int* votes, int* result) {
    // ...
}
```

---

### **PHASE 5: Native Compilation (Weeks 17-20)**

#### Objectives
- [ ] **LLVM backend** (D++ → LLVM IR)
- [ ] **Cranelift backend** (alternative)
- [ ] **x86-64 codegen** (direct assembly)
- [ ] **JIT compiler** (runtime compilation)

#### Compilation Pipeline
```
D+ source (.dplus)
    ↓ [Lexer/Parser]
AST
    ↓ [Type check]
Type-checked AST
    ↓ [Compiler]
D++ Bytecode (.d++)
    ↓ [LLVM Backend]
LLVM IR
    ↓ [llc]
Native code (.o / .so)
    ↓ [Linker]
Executable (.elf / .dll)
```

---

### **PHASE 6: Testing & Verification (Weeks 21-24)**

#### Objectives
- [ ] **Unit tests** (lexer, parser, compiler, VM)
- [ ] **Integration tests** (end-to-end policy execution)
- [ ] **Property-based tests** (using quickcheck/proptest)
- [ ] **Formal verification** (Z3 / SMT solver)

#### Test Suite Structure
```
tests/
├── lexer/
│   ├── test_identifiers.rs
│   ├── test_strings.rs
│   ├── test_operators.rs
│   └── test_sections.rs
├── parser/
│   ├── test_law.rs
│   ├── test_proof.rs
│   ├── test_judge.rs
│   └── test_roundtrip.rs
├── compiler/
│   ├── test_bytecode_gen.rs
│   ├── test_optimization.rs
│   └── test_llvm_backend.rs
├── vm/
│   ├── test_stack_ops.rs
│   ├── test_consensus.rs
│   ├── test_zones.rs
│   └── test_journal.rs
└── integration/
    ├── test_adaptive_policy.rs
    ├── test_emergency_mode.rs
    └── test_healing.rs
```

---

### **PHASE 7: Documentation & Examples (Weeks 25-28)**

#### Objectives
- [ ] **Language reference** (D+ syntax, semantics)
- [ ] **API documentation** (Rust FFI, C bindings)
- [ ] **Tutorial** (hello-world policy to advanced contracts)
- [ ] **Best practices** (security, performance, debugging)

#### Example Policies
```
examples/
├── 01_hello_policy.dplus              # Simple allow/forbid
├── 02_memory_quota.dplus              # Memory budgets
├── 03_cpu_throttle.dplus              # Adaptive CPU
├── 04_consensus.dplus                 # Multi-judge voting
├── 05_emergency_mode.dplus            # Failure recovery
├── 06_smart_contract.dplus            # State machine
├── 07_polyglot.dplus                  # Python + Rust
└── 08_formal_verification.dplus       # Z3 contracts
```

---

### **PHASE 8: Deployment & Hardening (Weeks 29-32)**

#### Objectives
- [ ] **GitHub CI/CD** (automated testing, releases)
- [ ] **Security audit** (code review, fuzzing)
- [ ] **Performance benchmarks** (compilation speed, VM overhead)
- [ ] **Hardware support** (x86, ARM, RISC-V)

#### Hardening Checklist
- [ ] Buffer overflow protections
- [ ] Integer overflow checks
- [ ] Stack depth limits
- [ ] Recursion limits
- [ ] Sandbox escapeability

---

## 🏗️ Architecture Details

### Lexer
- **Input:** D+ source code (UTF-8)
- **Output:** Token stream
- **Complexity:** O(n) single pass
- **Features:** Comments, multi-line strings, unicode identifiers

### Parser
- **Input:** Token stream
- **Output:** AST
- **Algorithm:** LL(1) recursive descent
- **Error recovery:** Panic mode + token skipping

### Type Checker
- **Input:** AST
- **Output:** Annotated AST (with types)
- **Algorithm:** Hindley-Milner or simpler bidirectional
- **Polymorphism:** Limited (no rank-2 polymorphism)

### Compiler
- **Input:** Type-checked AST
- **Output:** D++ bytecode
- **Optimizations:**
  - Constant folding
  - Dead code elimination
  - Common sub-expression elimination
  - Tail call optimization

### VM
- **Architecture:** Stack-based (like JVM, WASM)
- **Memory model:** Zones (frozen/cold/warm/hot)
- **Concurrency:** Async support via fiber/green threads
- **Consensus:** 3-judge voting system

---

## 📊 Metrics & Goals

| Metric | Target |
|--------|--------|
| Compilation time (100 LOC) | < 10ms |
| VM execution (1M instructions) | < 100ms |
| Memory footprint | < 1MB per policy |
| Consensus latency | < 5ms |
| Code coverage | ≥ 95% |

---

## 🔗 Integration Points

### With oo-bot
- D+ policies as oo-bot rules
- Bytecode loaded by `--security-agent` mode
- Automatic policy distribution

### With llm-baremetal
- D+ verdicts drive SSM Engine permissions
- Memory zones enforced by kernel
- Journal written to persistent storage

### With oo-host
- D+ compiler CLI (dplus_cc)
- Policy verification tools
- Debugging & profiling

---

## 🚀 Quick Start Commands

```bash
# Compile D+ policy to bytecode
cargo run --bin dplus_cc -- policy.dplus -o policy.d++

# Verify policy syntax only
cargo run --bin dplus_cc -- policy.dplus --verify

# Compile to native code
cargo run --bin dplus_cc -- policy.dplus --native -o policy.so

# Run tests
cargo test --lib dplus_compiler

# Run integration tests
cargo test --test '*'

# Profile compilation
time cargo run --bin dplus_cc -- policy.dplus

# Debug with verbose output
cargo run --bin dplus_cc -- policy.dplus -v
```

---

## 📚 References

- **D+ Spec:** `DPLUS_v2_SPEC.md`
- **Existing D+ code:** `src/dplus/`
- **Policy examples:** `policies/`
- **Tests:** `src/dplus_compiler/*/tests.rs`

---

## 🎓 Learning Path

For developers wanting to understand D+:

1. Read `DPLUS_v2_SPEC.md` (conceptual)
2. Study `src/dplus_compiler/lexer.rs` (tokenization)
3. Study `src/dplus_compiler/parser.rs` (parsing)
4. Study `src/dplus_compiler/bytecode.rs` (IR)
5. Study `src/dplus_compiler/compiler.rs` (code generation)
6. Implement VM step (stack machine)
7. Implement consensus voter
8. Write policies in `policies/`

---

## ✨ Next Immediate Actions

**Today:**
1. [ ] Integrate dplus_compiler into Cargo build
2. [ ] Test lexer on real .dplus files
3. [ ] Add type checking pass

**This week:**
1. [ ] Complete parser for all section types
2. [ ] Implement basic VM skeleton
3. [ ] Write end-to-end test

**Next week:**
1. [ ] Consensus voting system
2. [ ] Memory zones enforcement
3. [ ] Journal audit log

---

> **Le code est une constitution, pas une dictatoure.**
> 
> — Djiby Diop, 2026

