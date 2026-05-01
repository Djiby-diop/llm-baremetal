# OO Prime Architecture (v0.2)

## Purpose

OO Prime is a governance orchestrator for the repository.
It observes health signals, builds safe decisions, validates simulation gates, and can apply only constrained actions at system scope.

## Pipeline

1. Observe modules from repository root.
2. Generate governance decisions sorted by severity.
3. Inject v0.2 system action plan decision in apply-priority mode and keep OS-G as reminder target.
4. Run or verify oo-sim simulation proof.
5. Apply security guardrails (blocked actions, simulation requirements, protected targets).
6. Optionally apply allowlisted system actions.
7. Emit report JSON and JSONL operational logs.

## Components

- `oo_prime/observer.py`: computes module health from README/tests/policy signals.
- `oo_prime/governance.py`: creates candidate decisions and v0.2 system plan decision.
- `oo_prime/planner.py`: builds concrete docs/tests/policy checklist for the whole system with reminder targets.
- `oo_prime/simulation.py`: runs oo-sim (auto mode) and validates proof freshness.
- `oo_prime/security.py`: blocks unsafe decisions and enforces simulation requirements.
- `oo_prime/executor.py`: applies only allowlisted target-scoped actions.
- `oo_prime/engine.py`: cycle orchestrator and report/log producer.
- `oo_prime/cli.py`: command-line interface and mode flags.

## Safety Contract

- Destructive actions are not allowlisted by default.
- Apply scope is system-wide when no target prefixes are configured.
- OS-G remains a reminder target for governance heartbeat and planning context.
- `stabilize_module` requires a valid simulation proof.
- Protected targets cannot receive direct stabilize actions; they are downgraded to advisory.

## v0.2 Delta

- Added generated system plan artifact: `OO_PRIME_SYSTEM_ACTION_PLAN.md`.
- Kept optional OS-G action artifacts for reminder-target decisions.
- Added auto simulation refresh before each cycle with `--auto-simulate`.
- Added scenario-driven simulation gate via `oo-sim/scenarios/oo-prime-gate.json`.
