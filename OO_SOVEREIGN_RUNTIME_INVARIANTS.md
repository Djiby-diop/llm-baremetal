# OO Sovereign Runtime Invariants

## Purpose

This document defines the invariants that must remain true for the `llm-baremetal` sovereign runtime.

It exists to protect the value already built in:
- `llama2_efi_final.c`
- the UEFI runtime model
- the deterministic QEMU/autorun proofs
- the existing OO persistence, policy, and recovery logic

These invariants are more important than any future refactor.

---

## 1. Preservation invariant

The sovereign runtime is a **first-class pillar** of the Operating Organism.

Therefore:
- it is not disposable,
- it is not a temporary prototype,
- it is not to be replaced by a host runtime,
- it is not to be rewritten from scratch without proof that all invariants remain preserved.

### Concrete rule
`llama2_efi_final.c` may evolve, but the sovereign runtime behavior it embodies must be preserved.

---

## 2. Boot sovereignty invariant

The runtime must remain able to boot in a host-independent environment.

Meaning:
- UEFI boot remains valid,
- no host OS is required,
- network must never be required for boot,
- absence of a model must still permit a valid degraded/no-model runtime.

### Required proof style
- QEMU boot
- no-model image boot
- prompt arrival or autorun completion
- deterministic serial markers

---

## 3. Safe degraded operation invariant

Failure to load a model must not equal organism death.

If a model is missing, invalid, or incompatible, the runtime must still:
- boot,
- expose a usable REPL,
- preserve journal/state logic where possible,
- allow diagnostics,
- remain policy-bounded.

### Minimum degraded capability
- `/help`
- `/models`
- `/model_info`
- OO-safe commands allowed by policy
- journaling and recovery where applicable

---

## 4. Identity continuity invariant

The organism must preserve a minimal identity across boots.

At sovereign-runtime level, this means at minimum:
- persistent state survives reboot when storage is valid,
- boot counters or equivalent continuity markers are monotonic when expected,
- identity-critical fields are not silently reset without explicit recovery reason,
- recovery events are visible in logs.

If continuity is broken, the break must be:
- detectable,
- logged,
- and recoverable.

---

## 5. Journal truth invariant

Every important decision must leave deterministic evidence.

At minimum, major state transitions and safety decisions should be reflected in one or more of:
- serial output
- `OOJOUR.LOG`
- `OOCONSULT.LOG`
- persistent state files

### Rule
No important autonomous action should occur as a silent side effect.

---

## 6. Recovery-before-ambition invariant

The sovereign runtime must prefer survival over capability.

In practice:
- SAFE mode is preferable to crash
- DEGRADED mode is preferable to corruption
- rollback is preferable to undefined state
- no-model mode is preferable to boot failure

### Consequence
Any new feature that can break recovery behavior is lower priority than preserving recovery.

---

## 7. Policy supremacy invariant

The LLM may advise, but it does not hold final authority.

The runtime must preserve this ordering:

1. hard safety rules
2. deterministic policy
3. bounded execution logic
4. advisory cognition / LLM suggestion

### Forbidden inversion
The model must never directly bypass policy or convert unsafe commands into allowed ones.

---

## 8. Deterministic proof invariant

Major runtime features must be provable through repeatable harnesses whenever practical.

The sovereign runtime should continue to prefer:
- autorun proofs
- no-model smokes
- negative tests
- policy artifact verification
- fail-closed behavior

### Rule
If a feature cannot be observed or asserted, it is not yet trustworthy enough to become a core dependency.

---

## 9. Persistence integrity invariant

Persistent artifacts must be treated as safety-critical.

Artifacts include, at minimum:
- `OOSTATE.BIN`
- `OORECOV.BIN`
- `OOJOUR.LOG`
- `OOCONSULT.LOG`
- policy artifacts such as `OOPOLICY.BIN` and associated integrity metadata

### Expected behavior
- corruption is detected when possible
- recovery path is explicit
- integrity failures fail closed
- policy artifacts are never confused with model artifacts

---

## 10. Refactor discipline invariant

Refactoring is allowed only if it improves clarity without weakening proof.

### Acceptable refactor
- extract stable helper code
- reduce duplication
- isolate policy / persistence / UI modules
- improve testability

### Unacceptable refactor
- split code while losing end-to-end determinism
- change boot/recovery semantics without updated proofs
- replace proven behavior with architecture speculation

---

## 11. Runtime/host bridge invariant

A future host runtime may extend the organism, but it must not invalidate sovereign guarantees.

The host side may own:
- richer memory
- background orchestration
- richer UI
- OS integrations

But the sovereign side remains the ground truth for:
- survival mode
- minimal identity continuity
- safe degraded operation
- final recovery envelope

The shared cross-habitat identity and journal model is defined in [OO_SHARED_IDENTITY_AND_JOURNAL.md](OO_SHARED_IDENTITY_AND_JOURNAL.md).

---

## 12. Definition of regression

For the sovereign runtime, a regression includes any change that causes one of the following:

- no-model boot stops working
- policy integrity stops being verifiable
- journal/recovery paths become non-deterministic or silent
- a policy artifact is mistaken for a model
- an allowed no-model OO command path stops functioning
- an LLM-driven path bypasses safety policy
- QEMU/autorun proofs become flaky without explicit, documented reason

---

## 13. Immediate engineering consequences

When making future changes, prefer this order:

1. preserve boot
2. preserve no-model mode
3. preserve recovery and policy
4. preserve deterministic tests
5. add new capability
6. refactor only after proofs stay green

---

## 14. Current decision

Effective now, the sovereign runtime is governed by these principles:

- preserve the current bare-metal core,
- protect `llama2_efi_final.c` from destructive redesign,
- improve the system by addition and clarification,
- and treat the sovereign runtime as the organism's survival truth.
