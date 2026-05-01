# OO Prime Runbook (v0.4)

## Prerequisites

- Python environment configured for repository.
- Optional: `oo-sim/oo-sim.exe` built for auto simulation.

## Standard Operations

Dry-run governance cycle:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --dry-run
```

Dry-run with automatic oo-sim proof refresh:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --dry-run --auto-simulate
```

Apply constrained system-wide actions with automatic simulation:

```powershell
python -m oo_prime.cli --root .. --cycles 1 --apply --auto-simulate
```

CI strict mode (all guards):

```powershell
python -m oo_prime.cli --root .. --cycles 1 --strict-all --explain
```

Strict health gate only:

```powershell
python -m oo_prime.cli --root .. --strict-health-min-score 0.50 --strict-health-max-drop 0.20
```

## Artifacts

- Cycle report: `oo-bot/reports/latest.json`
- Runtime log: `oo-bot/logs/oo-prime.log`
- Simulation proof: `oo-sim/reports/oo-prime-sim-ok.json`
- Applied actions ledger: `OO_PRIME_ACTIONS.md`
- v0.2 system plan artifact: `OO_PRIME_SYSTEM_ACTION_PLAN.md`
- Optional OS-G reminder ledger: `OS-G (Operating System Genesis)/OO_PRIME_ACTIONS.md`

## Troubleshooting

Simulation proof fails with `oo-sim.exe missing`:

```powershell
cd ../oo-sim
./build.ps1
```

Then rerun OO Prime with `--auto-simulate`.

## Verification

Run tests:

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

Exit codes used by strict modes:

- `2`: strict simulation gate failed
- `3`: strict apply failed (no applied actions)
- `4`: strict risk failed (risk-blocked decisions)
- `5`: strict health failed (average below threshold or drop too high)
