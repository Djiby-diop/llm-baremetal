# Next Innovations — llm-baremetal + OO

This file defines the next practical innovation wave after M6.x hardening.

## Current sprint status
- ✅ A2 baseline implemented (structured observability markers in runtime + OO consult).
- ✅ B1 baseline implemented in log-only mode (confidence score + threshold logged).
- ✅ B1 threshold gating implemented behind config flags (`oo_conf_gate`, `oo_conf_threshold`), off by default.
- ✅ A1 baseline implemented: startup model path now avoids eager GGUF summary parse, adds startup timing markers, and caches GGUF summary once per session for `/model_info`.
- ✅ B2 baseline implemented: `OOOUTCOME.LOG` now persists `(action, expected, observed)` with pending/observed entries and confidence scoring now includes recent outcome feedback bias.
- ✅ B3 baseline implemented: bounded per-boot multi-step plan (`oo_plan_enable`, `oo_plan_max_actions`), explicit rollback checkpoint markers before auto-apply, and hard-stop on verify failures.
- ✅ A3 baseline implemented: `/models` now reports size+type+summary and startup `model=` path failures now emit explicit fallback diagnostics and recovery hints.
- ✅ M8 baseline implemented: `m8-reliability.ps1` now executes static + runtime reliability checks with targeted autorun scenarios for A1/A3/B1/B2/B3 and emits pass/fail markers.
- ▶️ Next coding target: M8.1 CI wiring (GitHub Action job for static pass + optional runtime gate on self-hosted runner).

## Track A — llm-baremetal core

### A1. Faster startup path (model open + metadata)
- Goal: reduce time-to-first-token on QEMU/TCG and real hardware.
- Scope:
  - cache GGUF metadata summary in RAM once per session,
  - skip redundant reopen/seek passes where safe,
  - expose startup timing markers in serial logs.
- Done when:
  - startup markers show measurable improvement,
  - no regression on `.bin` and `.gguf` loading.

### A2. Better runtime observability
- Goal: make troubleshooting deterministic from serial logs only.
- Scope:
  - standardized markers for load/start/decode/stop reasons,
  - explicit OOM guardrail markers,
  - single-line summary at generation end.
- Done when:
  - support/debug can identify failure root cause from one log excerpt.

### A3. Model selection UX hardening
- Goal: reduce model-path and FAT alias friction.
- Scope:
  - improve `/models` listing clarity (size/type/order),
  - reinforce `model=` fallback diagnostics,
  - preserve 8.3 fallback proofs in output markers.
- Done when:
  - wrong/missing model paths are self-explanatory in REPL logs.

## Track B — Operating Organism (OO)

### B1. M7 — Policy confidence scoring
- Goal: augment deterministic policy with confidence scoring before apply.
- Scope:
  - compute confidence score from vitals + last outcomes,
  - gate auto-apply by confidence threshold,
  - log score + decision reason in OO journals.
- Done when:
  - each OO decision includes score + threshold + applied/blocked.

### B2. M7.1 — Outcome feedback loop
- Goal: close the loop between applied actions and next-boot effect.
- Scope:
  - persist outcome tuples `(action, expected_effect, observed_effect)`,
  - mark actions as helpful/neutral/harmful,
  - decay old outcomes over time.
- Done when:
  - policy uses historical outcomes to prioritize safer actions.

### B3. M7.2 — Safe multi-step adaptation plan
- Goal: move from one-step decisions to bounded plan steps.
- Scope:
  - max N actions per boot window,
  - hard stop rules to prevent adaptation spirals,
  - explicit rollback checkpoints.
- Done when:
  - multi-step adaptation remains deterministic and bounded.

## Suggested execution order
1. A2 (observability) — immediate value and low risk.
2. A1 (startup performance) — measurable user impact.
3. B1 (confidence scoring) — first OO M7 building block.
4. B2 (feedback loop) — behavior quality over time.
5. B3 (safe plan) — advanced autonomy with strict safety bounds.

## First implementation candidate (next)
- Start with **A2 + B1** in one sprint:
  - add serial markers + decision score fields,
  - no behavior change initially (log-only mode),
  - then enable threshold gating behind a config flag.
