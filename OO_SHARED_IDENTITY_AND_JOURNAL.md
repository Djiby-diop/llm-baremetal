# OO Shared Identity and Journal Model

## Purpose

This document defines the **shared organism model** that should remain compatible between:

- the future OO Host Runtime (Windows / macOS / Linux), and
- the current sovereign runtime in `llm-baremetal`.

The goal is not to force both habitats to store data identically.
The goal is to make them preserve the **same organism identity**, the **same core continuity markers**, and a **compatible event model**.

---

## 1. Core principle

The organism may inhabit multiple execution environments, but it should still be recognized as the **same organism** if these remain consistent:

1. organism identity
2. continuity counters / epochs
3. stable policy intent
4. causal journal semantics
5. recovery lineage

This is the bridge between host life and sovereign life.

---

## 2. Identity model

Each organism should have a minimal persistent identity.

## Required fields

### `organism_id`
A stable unique identifier for the organism across habitats.

Properties:
- generated once
- never silently rotated
- copied or migrated explicitly
- survives reboot
- survives host ↔ sovereign transitions when continuity is intended

### `genesis_id`
Identifier of the original genesis event or founding lineage.

Purpose:
- distinguish "same organism evolved" from "new organism cloned"
- track lineage through migrations or future replication events

### `runtime_habitat`
Current habitat of execution.

Suggested values:
- `host_windows`
- `host_macos`
- `host_linux`
- `sovereign_uefi`
- `sovereign_baremetal`

### `runtime_instance_id`
Ephemeral identifier of the current running instance.

Properties:
- changes at each fresh runtime start
- not used as organism identity
- useful for causal logs and debugging

---

## 3. Continuity model

Identity alone is not enough. The organism also needs continuity markers.

## Required continuity fields

### `boot_or_start_count`
Monotonic counter of starts in the current habitat family.

### `continuity_epoch`
Monotonic epoch increased when a major recovery or state reset happens.

Purpose:
- preserve continuity while acknowledging discontinuities
- allow the organism to say: "I am still me, but I crossed a repair boundary"

### `last_clean_shutdown`
Boolean or status marker indicating whether the previous shutdown was clean.

### `last_recovery_reason`
Short machine-readable reason if recovery was needed.

Examples:
- `state_missing`
- `state_corrupt`
- `policy_crc_fail`
- `model_open_fail`
- `oom_guard`
- `manual_safe_mode`

---

## 4. Habitat-specific vs shared state

Not all state should be shared.

## Shared state

The following should be portable across habitats when possible:
- organism identity
- continuity epoch
- stable goals / intentions
- important policy preferences
- high-level journal events
- durable notes or memory summaries

## Sovereign-local state

The following may remain specific to `llm-baremetal`:
- UEFI-specific boot timings
- FAT file open fallbacks / 8.3 path details
- model loading quirks specific to sovereign runtime
- minimal safe-mode artifacts
- low-level recovery buffers

## Host-local state

The following may remain specific to host runtimes:
- process supervision metadata
- OS adapter handles and tokens
- window/UI session state
- background service topology
- host-specific permissions and paths

---

## 5. Journal model

A shared journal does **not** require identical file formats.
It requires compatible meaning.

## Journal event shape

Each important event should be representable with at least these fields:

- `event_id` — unique within journal stream
- `ts` — timestamp or monotonic time marker
- `organism_id`
- `runtime_habitat`
- `runtime_instance_id`
- `kind`
- `severity`
- `summary`
- `reason`
- `action`
- `result`
- `continuity_epoch`

## Recommended event kinds

- `boot`
- `startup`
- `shutdown`
- `recovery`
- `policy_decision`
- `policy_denial`
- `journal_rotation`
- `state_load`
- `state_save`
- `model_select`
- `model_load`
- `model_fail`
- `consult`
- `consult_decision`
- `goal_create`
- `goal_update`
- `goal_complete`
- `safe_mode_enter`
- `degraded_mode_enter`
- `normal_mode_enter`
- `integrity_fail`

---

## 6. Journal levels

Suggested severity levels:
- `debug`
- `info`
- `notice`
- `warn`
- `error`
- `critical`

Rule:
- recovery, integrity, and policy enforcement events should never be below `warn` when they materially affect behavior.

---

## 7. Causality rule

The organism journal should explain:

- what happened,
- why it happened,
- what was decided,
- what changed.

This means important events should not be pure print markers.
They should carry causal meaning.

### Example
Instead of only:
- `entered SAFE`

Prefer:
- `kind=recovery severity=warn summary="entered SAFE" reason="state_corrupt" action="rollback_to_recovery" result="safe_boot_ok"`

---

## 8. Policy portability

The shared model should preserve policy meaning across habitats.

At minimum, both host and sovereign habitats should understand the same concepts:
- allow
- deny
- observe
- enforce
- safe-first
- deny-by-default
- advisory-only LLM

The exact policy engine implementation may differ.
The semantics must remain compatible.

---

## 9. Recovery lineage

Recovery should preserve lineage, not erase it.

When recovery happens, the shared model should preserve:
- same `organism_id`
- same `genesis_id`
- incremented `continuity_epoch`
- explicit recovery event in journal
- optional pointer to previous valid snapshot lineage

This is how the OO remains the same organism after repair.

---

## 10. Migration rules

## Host → Sovereign
When moving into sovereign runtime, preserve if possible:
- `organism_id`
- `genesis_id`
- continuity epoch
- high-level goals
- minimal durable journal summary
- policy posture

## Sovereign → Host
When moving back into host runtime, preserve if possible:
- updated continuity epoch
- sovereign recovery history
- final policy state
- important no-model / degraded history markers

---

## 11. Minimum sovereign subset

Even if the full host identity model is richer, `llm-baremetal` only needs a minimal subset to remain compatible.

Minimum required subset for sovereign runtime:
- `organism_id`
- `genesis_id` or equivalent lineage marker
- `boot_or_start_count`
- `continuity_epoch`
- `last_recovery_reason`
- causal journal with key recovery/policy events

This is enough to make sovereign continuity meaningful.

---

## 12. Anti-patterns to avoid

Do not:
- treat each boot as a new organism by default
- silently reset identity after corruption
- mix ephemeral runtime IDs with organism identity
- let host-side convenience override sovereign safety semantics
- allow journal formats to diverge so far that event meaning is lost

---

## 13. Recommended next implementation steps

### In `llm-baremetal`
- define a minimal identity structure for sovereign persistence
- make continuity epoch explicit
- make recovery reasons machine-readable where possible
- keep journal markers aligned with the shared event model

### In future host runtime
- adopt the same identity fields
- store richer journal details while preserving shared event semantics
- support import/export or translation into sovereign-compatible summaries

---

## 14. Current decision

The Operating Organism will evolve as a multi-habitat system.

Therefore:
- identity must be portable,
- continuity must be explainable,
- recovery must preserve lineage,
- and journals must remain semantically compatible across host and sovereign runtimes.
