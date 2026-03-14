# OO Global Architecture — llm-baremetal as Sovereign Runtime

## Intent

This document defines how `llm-baremetal` fits into the larger goal of building an **Operating Organism (OO)** that can:

- live on top of modern operating systems,
- preserve its identity and memory,
- survive faults and degraded environments,
- and eventually operate in a more autonomous bare-metal mode.

The key rule is simple:

> `llm-baremetal` is **not** a throwaway prototype.
> It is the **sovereign runtime pillar** of the future OO.

---

## 1. Strategic position

The OO is split into **two execution habitats**:

### A. Host Habitat
Runs on Windows, macOS, Linux.

Purpose:
- rich integration with files, network, UI, processes, voice, sensors
- daily operation
- long-duration memory and orchestration
- rapid iteration

### B. Sovereign Habitat
Runs through `llm-baremetal`.

Purpose:
- autonomous boot
- survival and recovery mode
- deterministic safety-first execution
- proof of continuity without dependency on a host OS

The same organism should eventually be able to inhabit both.

---

## 2. Role of llm-baremetal

`llm-baremetal` is the **Soma + Survival Kernel** of the OO.

It is responsible for:

- UEFI boot and minimal sovereignty
- deterministic REPL execution
- model loading / no-model degraded operation
- persistent state and journaling
- safe-first policy enforcement
- recovery, fallback, and degraded modes
- proving the organism can still function when the host habitat is absent

In the global architecture, `llm-baremetal` is the place where the OO proves:

- continuity,
- resilience,
- bounded autonomy,
- and recoverability.

---

## 3. Non-negotiable preservation rules

The following are preserved and evolved, not discarded:

### Stable pillar
- `llama2_efi_final.c`
- current build chain
- current QEMU/autorun proofs
- current OO state / journal / policy logic

### Rule
No future host-side work should invalidate the sovereign runtime.

### Consequence
The host runtime is an **extension**, not a replacement.

---

## 4. Functional layering

## Layer 1 — Execution Body
Current primary implementation in `llm-baremetal`.

Examples:
- UEFI boot
- framebuffer / text UI
- keyboard input
- filesystem access
- model loading
- inference paths
- no-model REPL

## Layer 2 — Homeostasis
Current partial implementation in `llm-baremetal`.

Examples:
- SAFE / DEGRADED / NORMAL modes
- RAM-aware reductions
- fallback behavior
- autorun safety
- persistence and recovery
- OO policy gates

## Layer 3 — Organism Memory
Current partial implementation in `llm-baremetal`, to be expanded in host runtime.

Examples:
- `OOSTATE.BIN`
- `OORECOV.BIN`
- `OOJOUR.LOG`
- `OOCONSULT.LOG`

## Layer 4 — Policy / Will
Current deterministic policy is already grounded in `llm-baremetal` and OS-G.

Examples:
- `/oo*` gates
- strict D+ style policy artifacts
- enforce/deny-by-default flows
- safety-first arbitration

## Layer 5 — Advisory Cognition
The LLM may advise, but not rule.

Rule:
- LLM suggests
- policy decides
- runtime executes
- journal records

This rule remains valid in both host and sovereign habitats.

---

## 5. What llm-baremetal is not

`llm-baremetal` is **not** required to become a full desktop OS before the OO is credible.

It does **not** need, yet:
- a full windowing system,
- a complete driver ecosystem,
- a modern multi-user OS abstraction,
- parity with Windows/macOS features.

Its mission is different:
- prove organism continuity,
- prove survival,
- prove safe autonomy,
- prove recoverable behavior.

---

## 6. Immediate architecture direction

### Keep
- monolithic runtime if it helps move fast
- current test harnesses
- current UEFI proofs
- no-model degraded path
- OO command path

### Improve gradually
- extract subsystems only when stable enough
- document invariants before refactoring
- preserve end-to-end QEMU gates after every meaningful change

### Do not do
- rewrite `llama2_efi_final.c` from scratch
- split code purely for aesthetics
- replace deterministic logic with LLM-driven control

---

## 7. Relationship with a future OO Host Runtime

A future host runtime will likely own:
- richer memory
- background services
- higher-level planning
- cross-platform adapters
- richer UI / cockpit
- external integrations

But `llm-baremetal` remains the authoritative place for:
- sovereign boot
- minimal identity continuity
- survival mode
- deterministic recovery
- final safety envelope

In other words:

- Host runtime = **daily life**
- Bare-metal runtime = **survival truth**

The first practical host-side plan is defined in [OO_HOST_RUNTIME_V0.md](OO_HOST_RUNTIME_V0.md).

---

## 8. Near-term workstreams for llm-baremetal

### Workstream A — Stabilize the sovereign core
- keep QEMU smoke green
- preserve no-model boot path
- strengthen recovery and journaling
- keep policy artifacts deterministic

### Workstream B — Make the organism state more explicit
- document invariants of state/journal files
- define identity fields
- define recovery transitions
- define what survives reboot vs what is ephemeral

### Workstream C — Prepare the bridge to host habitats
- define a shared organism identity model
- define a shared journal/event schema
- define portable policy semantics
- define what a host runtime must preserve when handing off to sovereign mode

This bridge model is specified in [OO_SHARED_IDENTITY_AND_JOURNAL.md](OO_SHARED_IDENTITY_AND_JOURNAL.md).

---

## 9. Definition of success

`llm-baremetal` succeeds if it can prove the OO has:

1. **Identity continuity**
2. **Persistent memory**
3. **Safe degraded operation**
4. **Deterministic recovery**
5. **Policy-bounded action**
6. **A viable no-model survival mode**

That is enough to make it foundational.

---

## 10. Current decision

Effective immediately, the project direction is:

- preserve `llm-baremetal` as a first-class pillar,
- continue improving it as the sovereign runtime,
- build future host-side components around it,
- and never treat `llama2_efi_final.c` as disposable work.

For the concrete runtime guarantees that must remain true while evolving this pillar, see [OO_SOVEREIGN_RUNTIME_INVARIANTS.md](OO_SOVEREIGN_RUNTIME_INVARIANTS.md).
