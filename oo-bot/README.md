# oo-bot

OO Prime v0.4 is a governance bot for the Operating Organism ecosystem.
It observes module health, proposes safe actions, and writes auditable reports.

## Scope

This first version focuses on:

- observation of OO module folders
- deterministic governance decisions
- oo-sim simulation gate for sensitive decisions
- security guardrails before any decision is accepted
- constrained apply mode (system-wide allowlist, OS-G reminder retained)
- generated system action plan (docs/tests/policy + reminders)
- module-level scaffold actions (README/tests)
- safe-apply and confirmed-apply execution modes
- apply delta reporting (created/updated/noop)
- persistent inter-cycle memory and effectiveness scoring
- per-cycle risk budget enforcement
- target cooldown between repeated interventions
- action-specific cooldown policy (for example plan generation)
- anti-repetition guard for noop/blocked/skipped proposals
- explicit reporting of deferred decisions caused by governance memory
- structured logs and JSON reports

When apply mode is enabled, OO Prime operates at system scope, keeps an explicit OS-G reminder heartbeat, and materializes a v0.2 system action plan file.

It never executes destructive actions. Apply mode only writes allowlisted action records.

`--apply-mode safe` is create-only (no overwrite).
`--apply-mode confirmed` allows updates when necessary.

## Documentation

- `ARCHITECTURE.md`
- `RUNBOOK.md`

## Folder structure

```text
oo-bot/
  config/
    policy.json
  oo_prime/
    __init__.py
    cli.py
    config.py
    engine.py
    executor.py
    governance.py
    observer.py
    planner.py
    state.py
    security.py
    simulation.py
    types.py
  tests/
    test_oo_prime.py
  reports/
  logs/
  botvison.md
```

## Quick start

From this folder:

```powershell
cd llm-baremetal/oo-bot
python -m oo_prime.cli --root .. --cycles 1 --dry-run
```

Generated outputs:

- `reports/latest.json`
- `logs/oo-prime.log`
- `../OO_PRIME_ACTIONS.md` (apply mode)
- `../OO_PRIME_SYSTEM_ACTION_PLAN.md` (v0.2 apply mode)
- `../OS-G (Operating System Genesis)/OO_PRIME_ACTIONS.md` (OS-G reminder target actions)

## Example commands

Dry run, no side effects:

```powershell
python -m oo_prime.cli --root .. --cycles 2 --dry-run
```

Generate simulation proof (example format expected by OO Prime):

```json
{
  "status": "ok",
  "generated_at": "2026-04-09T12:00:00+00:00"
}
```

Place it at:

```text
../oo-sim/reports/oo-prime-sim-ok.json
```

Run with explicit proof file:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --simulation-proof ../oo-sim/reports/oo-prime-sim-ok.json
```

Auto-run oo-sim before each cycle and refresh proof automatically:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --auto-simulate
```

Auto-run oo-sim with explicit scenario:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --auto-simulate --simulation-scenario ../oo-sim/scenarios/oo-prime-gate.json
```

Apply allowlisted decisions at system scope:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --apply --apply-mode safe
```

Apply with auto simulation and generate v0.2 system plan:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --apply --auto-simulate --apply-mode safe
```

Confirmed apply mode (allows updates):

```powershell
python -m oo_prime.cli --root .. --cycles 1 --apply --auto-simulate --apply-mode confirmed
```

Custom policy:

```powershell
python -m oo_prime.cli --root .. --policy config/policy.json
```

Custom persistent state path:

```powershell
python -m oo_prime.cli --root .. --state state/oo-prime-state.json
```

Override deferred reason cap for one run:

```powershell
python -m oo_prime.cli --root .. --max-deferred-reasons-in-log 3
```

Override action cooldowns for one run (repeatable):

```powershell
python -m oo_prime.cli --root .. --action-cooldown propose_system_action_plan=4 --action-cooldown propose_docs=1
```

Override repetition and target cooldown for one run:

```powershell
python -m oo_prime.cli --root .. --target-cooldown-cycles 2 --repetition-threshold 3
```

Show compact explanation for the last cycle:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --explain
```

Fail fast when simulation gate is not approved:

```powershell
python -m oo_prime.cli --root .. --strict-simulation-gate
```

Fail fast when apply mode results in zero applied actions:

```powershell
python -m oo_prime.cli --root .. --apply --strict-apply
```

Fail fast when risk budget blocks at least one decision:

```powershell
python -m oo_prime.cli --root .. --strict-risk
```

Fail fast on health thresholds:

```powershell
python -m oo_prime.cli --root .. --strict-health-min-score 0.50 --strict-health-max-drop 0.20
```

Enable all strict checks at once:

```powershell
python -m oo_prime.cli --root .. --strict-all
```

Cooldown and anti-repetition are controlled in `config/policy.json` with:

- `target_cooldown_cycles`
- `action_cooldown_by_action`
- `repetition_threshold`
- `repetition_statuses`
- `max_deferred_reasons_in_log`

Reports now include a `health_trend` snapshot to track observed score drift across cycles.

Run tests:

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

## Roadmap

- v0.1: observe + decide + secure + log
- v0.2: auto simulation + system docs/tests/policy plan generation + OS-G reminder
- v0.3-A: module scaffolds + safe/confirmed apply + delta report
- v0.3-B: persistent memory + effectiveness scoring + risk budget
- v0.3-C+: cooldown/action overrides + explain mode + health trend snapshot
- v0.4: strict-all + strict health guards for CI gating
