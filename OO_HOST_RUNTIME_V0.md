# OO Host Runtime v0

## Purpose

This document defines the first practical host-side runtime for the Operating Organism.

It is intended to run on:
- Windows
- macOS
- Linux

It does **not** replace `llm-baremetal`.
It complements it.

- Host runtime = daily life
- Sovereign runtime = survival truth

---

## 1. Mission of v0

The host runtime v0 is not a full operating system.
It is a persistent organism process that can:

- keep a stable identity,
- preserve memory,
- manage goals,
- supervise background activity,
- apply policy-bounded actions,
- and bridge toward sovereign mode.

The first success criterion is simple:

> the organism should run continuously on a host OS, survive restarts/failures, and preserve meaningful continuity.

---

## 2. v0 scope

## Included in v0
- organism identity
- persistent state store
- append-only journal
- goal scheduler
- policy engine
- operator shell / control plane
- basic worker supervision
- host adapters for files, clock, process, network
- bridge-ready export/import model for sovereign runtime

## Excluded from v0
- full desktop shell replacement
- custom kernel
- broad driver model
- full autonomous self-modification
- unrestricted LLM control
- deep GUI requirements

---

## 3. High-level architecture

The host runtime v0 should be split into the following modules.

## A. Core organism service
The central long-lived process.

Responsibilities:
- load identity
- load state
- recover if state is damaged
- start workers
- enforce policy
- expose operator interface
- flush journal

Suggested name:
- `oo-core`

## B. Memory service
Responsible for durable organism state.

Responsibilities:
- load/store organism identity
- load/store continuity markers
- load/store active goals
- store summaries and durable notes
- snapshot and rollback

## C. Journal service
Responsible for causal evidence.

Responsibilities:
- append events
- rotate journal safely
- expose recent tails
- export compact summaries for sovereign handoff

## D. Scheduler / will engine
Responsible for selecting what the organism does next.

Responsibilities:
- rank goals
- assign work windows
- limit concurrency
- defer unsafe actions
- respect policy and homeostasis state

## E. Policy engine
Responsible for final decision authority.

Responsibilities:
- allow / deny / observe / enforce
- constrain autonomous actions
- gate LLM suggestions
- handle safe-mode rules

## F. Adapters
Responsible for environment access.

Adapters include:
- filesystem adapter
- clock adapter
- process adapter
- network adapter
- UI/notification adapter

## G. Operator interface
A shell or control plane for human supervision.

Capabilities:
- inspect state
- inspect journal
- inspect goals
- inspect policy
- trigger manual actions
- request export to sovereign package

---

## 4. Identity model

The host runtime must implement the shared model defined in [OO_SHARED_IDENTITY_AND_JOURNAL.md](OO_SHARED_IDENTITY_AND_JOURNAL.md).

Minimum required fields:
- `organism_id`
- `genesis_id`
- `runtime_habitat=host_windows|host_macos|host_linux`
- `runtime_instance_id`
- `boot_or_start_count`
- `continuity_epoch`
- `last_clean_shutdown`
- `last_recovery_reason`

---

## 5. Persistent storage model

v0 should use a simple, robust persistence layer.

## Recommended layout

### Identity file
- `organism_identity.json`

### State file
- `organism_state.json`

### Journal
- `organism_journal.jsonl`

### Recovery snapshot
- `organism_recovery.json`

### Optional notes/memory summaries
- `organism_memory/`

## Persistence rules
- write temp file first
- fsync/flush where available
- rename atomically when possible
- keep at least one recovery snapshot
- never silently discard state corruption

---

## 6. Journal model

The host runtime should emit events compatible with [OO_SHARED_IDENTITY_AND_JOURNAL.md](OO_SHARED_IDENTITY_AND_JOURNAL.md).

Recommended format for v0:
- JSON Lines (`.jsonl`)

Why:
- easy append
- easy parse
- easy filter
- easy export to compact sovereign summaries

---

## 7. Goal model

The host runtime must support explicit organism goals.

## Goal fields
- `goal_id`
- `title`
- `description`
- `status`
- `priority`
- `created_at`
- `updated_at`
- `deadline` (optional)
- `origin` (`operator`, `policy`, `llm_suggestion`, `recovery`)
- `safety_class`

## Goal states
- `pending`
- `ready`
- `doing`
- `blocked`
- `done`
- `aborted`

## Rule
No goal should transition automatically into action if policy would deny it.

---

## 8. Homeostasis model

The host runtime also needs modes, similar in spirit to the sovereign runtime.

Suggested modes:
- `normal`
- `degraded`
- `safe`

Triggers may include:
- repeated state load failure
- repeated worker crash
- persistent storage failure
- policy integrity issue
- repeated action denial escalation

Behavior by mode:
- `normal`: all safe features available
- `degraded`: reduced concurrency, reduced autonomy, no risky mutations
- `safe`: journal + diagnostics + manual operations only

---

## 9. LLM role in v0

The LLM remains advisory only.

Allowed uses:
- summarize state
- propose actions
- rewrite notes into compact memory
- classify journal events
- suggest plans

Forbidden uses:
- direct execution without policy
- silent mutation of durable state
- direct policy override
- unsafe shell/process execution

Rule:
- LLM suggests
- policy decides
- runtime executes
- journal records

---

## 10. Worker supervision

The host runtime should support bounded workers.

Examples:
- memory summarizer
- planner
- watcher
- sync/export task
- diagnosis task

Each worker should have:
- worker id
- last heartbeat
- state
- crash count
- restart count
- capability set

Worker failures must be journaled.
Repeated failures may degrade the organism mode.

---

## 11. Operator shell v0

A minimal shell is enough for the first version.

Suggested commands:
- `status`
- `identity`
- `journal tail`
- `goals list`
- `goal add`
- `goal done`
- `mode`
- `policy show`
- `policy set`
- `export sovereign`
- `import sovereign`
- `recover`
- `shutdown`

This shell should be deterministic and scriptable.

---

## 12. Bridge to sovereign runtime

The host runtime must eventually support export of a sovereign-compatible package.

v0 bridge output may include:
- compact identity file
- continuity markers
- durable memory summary
- recent high-value journal summary
- policy posture summary
- active high-priority goals summary

This export is not yet full migration.
It is the first bridge.

---

## 13. Recommended implementation style

## Language
Preferred choices:
- Rust
- or Python for a very early control-plane prototype

If choosing one strategic implementation language for v0:
- prefer **Rust** for the durable core,
- use Python only for auxiliary tooling if needed.

## Why Rust
- strong state modeling
- safer persistence logic
- better long-lived service discipline
- easier convergence with low-level/runtime-oriented thinking already present in the project

---

## 14. Suggested repository shape

Example future structure:

- `llm-baremetal/` → sovereign runtime
- `oo-host/` → host runtime v0
- `oo-spec/` or shared docs → identity/policy/journal models
- `tools/` → converters, exporters, validators

This keeps `llm-baremetal` preserved and clear.

---

## 15. First 30-day implementation target

A realistic first target for host runtime v0 is:

1. persistent organism identity
2. JSON state file
3. JSONL journal
4. shell with `status`, `goal add`, `journal tail`
5. basic policy engine
6. safe/degraded mode switching
7. recovery snapshot on startup failure
8. export summary compatible with sovereign runtime model

This is enough to create the first living host-side organism skeleton.

---

## 16. Success definition for v0

The host runtime v0 is successful if it can:

- restart without losing organism identity
- preserve continuity markers
- recover from damaged state with explicit journal evidence
- maintain a durable goal list
- constrain actions through policy
- remain stable for long-running sessions
- produce a compact sovereign-facing export

---

## 17. Current decision

The next major implementation track after architecture formalization should be:

- preserve and continue validating `llm-baremetal`, and
- begin a minimal host runtime v0 as the organism's daily-life habitat.
