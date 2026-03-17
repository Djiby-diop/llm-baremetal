# Host Runtime 90-Day Plan

Date: 2026-03-17
Status: execution draft
Purpose: turn the Operating Organism host runtime into a concrete first deliverable

## Objective

In 90 days, produce a host-side Operating Organism v0 that:
- keeps a persistent identity
- keeps a coherent append-only journal
- maintains durable goals
- applies deterministic policy
- supervises a few workers
- survives restarts without losing continuity

## Target shape of v0

A minimal but real runtime composed of:
- `oo-core` service/daemon
- persistent `identity.json`
- append-only `journal.jsonl`
- persistent `state.json`
- deterministic `policy` module
- simple `scheduler`
- `workers/` supervision loop
- operator CLI shell

## Suggested implementation stance

Keep v0 operationally simple.

Preferred constraints:
- one host language for orchestration
- simple files or SQLite
- append-only journal discipline
- explicit schemas
- no hidden magic
- no LLM direct actuation

A practical first stack can be:
- Python for speed of iteration
- SQLite or JSONL + snapshots for persistence
- small CLI operator shell
- later optional Rust rewrite for hardened parts

## Milestones

## Days 1–15 — Foundations

### Deliverables
- workspace/folder layout for host runtime
- identity format
- journal event schema
- state snapshot schema
- first operator CLI skeleton

### Required artifacts
- `oo-host/` repository or workspace folder
- `identity.json`
- `journal.jsonl`
- `state.json`
- `policy.json` or equivalent rules source

### Exit criteria
- runtime starts
- identity persists across restart
- journal accepts append-only events
- state can be loaded and saved cleanly

## Days 16–30 — Policy and scheduler

### Deliverables
- policy engine v0
- goal store
- simple scheduler loop
- deterministic action gating

### Minimal supported capabilities
- create goal
- list goals
- mark goal state
- choose next action deterministically
- block unsafe actions with explainable reasons

### Exit criteria
- goals persist across restart
- scheduler can progress a trivial multi-step objective
- every blocked action yields a journaled reason

## Days 31–45 — Worker supervision

### Deliverables
- worker registration model
- process/subtask supervision
- heartbeat model
- safe mode downgrade rules

### Minimal worker set
- clock/heartbeat worker
- filesystem watcher or polling worker
- optional network reachability worker

### Exit criteria
- worker death is detected
- runtime records anomaly in journal
- runtime downgrades posture when needed
- runtime remains alive after worker failure

## Days 46–60 — Operator shell and cockpit v0

### Deliverables
- operator CLI commands
- status summary view
- journal tail view
- goal status view
- policy/explain view

### Suggested commands
- `status`
- `goals`
- `goal add`
- `goal show`
- `journal tail`
- `policy explain`
- `safe on/off`
- `worker list`

### Exit criteria
- operator can understand current state in under one minute
- explanations are deterministic and journal-backed
- shell remains usable after restart and anomaly recovery

## Days 61–75 — Continuity and recovery

### Deliverables
- crash-safe startup sequence
- snapshot + replay strategy
- corruption detection
- rollback or safe-start path

### Required proofs
- restart without identity loss
- restart without journal inconsistency
- recover from partial state write
- continue in degraded mode if needed

### Exit criteria
- continuity proof documented
- crash tests reproducible
- recovery outcome visible in operator shell

## Days 76–90 — Sovereign bridge

### Deliverables
- shared identity fields with sovereign runtime
- shared journal event family
- continuity handoff schema
- first host ↔ sovereign compatibility note

### Minimal proof
- host can emit a continuity export
- sovereign side can understand the exported shape
- shared identifiers remain stable

### Exit criteria
- one organism identity spans both host and sovereign docs
- continuity bridge is explicit, not hand-wavy
- at least one end-to-end mock handoff is documented

## Proofs of life for v0

The host runtime should be considered credible only if it can pass these proofs.

### Continuity
- restart and retain same identity
- recover state and active goals

### Memory
- replay recent causal history
- expose prior failures and decisions

### Homeostasis
- degrade behavior under pressure or repeated faults
- reduce activity instead of collapsing silently

### Recovery
- detect damaged state
- restore a usable posture
- journal the recovery path

### Agency
- keep progressing a small multi-step objective without micromanagement

### Duality preparation
- use schemas that can later align with the sovereign runtime

## Suggested repository structure

```text
oo-host/
  README.md
  pyproject.toml
  oo_core/
    identity.py
    journal.py
    state.py
    policy.py
    scheduler.py
    workers.py
    continuity.py
    cli.py
  data/
    identity.json
    state.json
    journal.jsonl
  tests/
    test_identity.py
    test_journal.py
    test_policy.py
    test_scheduler.py
    test_recovery.py
```

## Non-goals for v0

Do not try to build yet:
- a desktop shell replacement
- a GUI-heavy system
- arbitrary model autonomy
- a full agent platform
- a human-like assistant persona layer

## Success statement after 90 days

At the end of the first 90 days, the project should be able to say:

> We have a host-side Operating Organism runtime that preserves identity,
> keeps causal memory, enforces policy, survives faults, and is ready to bridge
> to the sovereign bare-metal runtime.
