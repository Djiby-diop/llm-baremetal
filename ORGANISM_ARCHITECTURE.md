# Organism Architecture

Date: 2026-03-17
Status: active master architecture note
Purpose: define the target shape of the Operating Organism across host and sovereign runtimes

## Core thesis

The project should not try to compete first as a generic desktop OS.
It should prove something stronger:

> a portable software organism that can live on top of existing operating systems,
> preserve its identity and memory over time,
> and continue in a sovereign bare-metal mode when needed.

This gives the project a practical sequence:

1. host runtime for continuous life
2. sovereign runtime for autonomy and recovery
3. hybrid continuity between both

## What an Operating Organism must prove

A credible Operating Organism is not just a kernel, not just a chatbot, and not just a desktop shell.
It must prove five layers working together.

### 1. Soma
Execution substrate:
- CPU execution
- memory
- file I/O
- display/input
- network
- storage
- adapters to environment

### 2. Homeostasis
Self-preservation and bounded adaptation:
- budgets
- watchdogs
- fault detection
- safe/degraded/normal modes
- rollback and recovery
- bounded auto-apply behavior

### 3. Living Memory
Continuity over time:
- stable identity
- append-only journal
- persistent state
- causal traces
- goals and outcomes
- reboot continuity

### 4. Policy / Will
Deterministic governance:
- permissions
- invariants
- priority and arbitration
- forbidden actions
- safe-first execution
- proof-oriented decision logging

### 5. Assisted Cognition
LLM as advisor, never as raw authority:
- suggestion generation
- explanation support
- plan proposal
- operator assistance
- policy-bounded execution only

## Strategic split

## Host runtime

The host runtime is the daily habitat.
It should run on Windows, macOS, and Linux.

Its role:
- long-lived identity
- continuous journal
- continuous objectives
- multi-process supervision
- richer perception and interaction
- operator cockpit
- long horizon adaptation

Windows/macOS/Linux are the biome, not the organism identity.

## Sovereign runtime

The sovereign runtime is the survival substrate.
In this repository, that role is held by `llm-baremetal`.

Its role:
- autonomous boot
- minimal but strong persistence
- recovery and safe mode
- continuity proof under constrained conditions
- bare-metal survival path
- strong deterministic policy enforcement

## Hybrid runtime

The final target is one organism with multiple habitats.

Shared across host and sovereign modes:
- identity
- persistent memory model
- journal semantics
- policy semantics
- continuity receipts
- outcome semantics

Different by habitat:
- available sensors and actuators
- UI richness
- process and network affordances
- storage capabilities
- resource ceiling

## Recommended architectural layers

### Layer A — OO Core
Portable, environment-independent logic.

Responsibilities:
- organism identity
- state model
- goal store
- journal model
- causal event schema
- scheduler
- policy engine
- health state
- continuity rules

This layer should be as OS-independent as possible.

### Layer B — Environment Adapters
One adapter per habitat.

Planned adapters:
- Windows
- macOS
- Linux
- UEFI / bare-metal

Each adapter should expose a small capability surface:
- clock
- files
- storage
- network
- process/jobs
- UI/output
- input/events
- notifications

### Layer C — Brain
Advisory reasoning only.

Responsibilities:
- summarization
- consult suggestions
- plan candidates
- explanation generation
- context-sensitive operator help

Rule:
- the brain proposes
- policy validates
- executor applies
- journal records

### Layer D — Body / Interface
Embodied operator-facing surface.

Responsibilities:
- REPL
- cockpit
- diagnostics
- overlays and dashboards
- device-specific rendering
- later: voice, robotics, richer peripherals

## Invariants that must never be broken

1. Identity continuity beats convenience.
2. Policy beats model output.
3. Recovery beats feature richness.
4. Journal beats hand-wavy explanation.
5. Safe-first beats aggressive autonomy.
6. Host and sovereign modes must converge on one logical organism.

## Current project state

### Already strong
- sovereign boot/runtime path
- persistence artifacts
- reboot continuity
- safe/degraded/normal homeostasis
- consult logging and explanation
- outcome-driven reselection
- real-hardware demo image validation

### Still missing as first-class product
- host runtime daemon/service
- persistent host identity lifecycle
- shared host/sovereign journal schema
- host scheduler and supervised workers
- operator cockpit outside bare-metal

## Near-term roadmap

### Phase 1 — Host runtime v0
Build a minimal hosted organism that can stay alive for days.

Must include:
- persistent identity
- append-only journal
- policy engine
- goal scheduler
- worker supervision
- safe mode fallback
- operator shell

### Phase 2 — Cockpit
Make state legible.

Must show:
- vital state
- goals
- journal tail
- decisions
- anomalies
- confidence level
- current policy posture

### Phase 3 — Sovereign proof hardening
Continue strengthening the bare-metal side.

Focus:
- continuity receipts
- recovery proofs
- bounded cognition
- no-model safe boot path
- artifact validation

### Phase 4 — Hybrid continuity
Unify host and sovereign semantics.

Must prove:
- one identity
- one logical journal
- one policy family
- one continuity chain
- controlled migration between habitats

## What should happen next

The next major engineering move should be the host runtime, not another large sovereign-only feature burst.

Recommended immediate deliverables:
1. `ORGANISM_ARCHITECTURE.md` as master doctrine
2. a concrete 90-day host runtime plan
3. a first host runtime scaffold with persistent identity and journal
4. a shared continuity schema usable by both host and sovereign environments

## Success criterion

The strongest claim is not:
- “we built another OS”

The strongest claim is:
- “we built a governed software organism that can live on existing systems and survive without them.”
