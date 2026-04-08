# oo-dplus — D+ Policy Engine for the Operating Organism

> **Couche 5 — Evolution Layer** | [oo-system architecture](https://github.com/Djiby-diop/oo-system)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![oo-dplus CI](https://github.com/Djiby-diop/oo-dplus/actions/workflows/oo-dplus-ci.yml/badge.svg)](https://github.com/Djiby-diop/oo-dplus/actions/workflows/oo-dplus-ci.yml)

**oo-dplus** is the decision and policy engine of the [Operating Organism](https://github.com/Djiby-diop/oo-system).

Every action in the OO passes through D+. It is the **ethical core** — not a firewall, but a judge.

## What is D+?

D+ (Decision Plus) is a policy runtime that evaluates *intentions*, not just instructions:

```
Action --> D+ Engine --> Verdict (ALLOW / FORBID / QUARANTINE / THROTTLE / ...)
```

Unlike a traditional OS permission system, D+ reasons about:
- **harm** (0.0-1.0): How dangerous is this action?
- **benefit** (0.0-1.0): What is the expected value?
- **reversibility**: Can we undo this?
- **intent**: What is the declared purpose?

## Architecture

```
oo-dplus/
|-- src/
|   |-- dplus/        -- Core verdict engine (judge, merit, verifier, ops)
|   |-- warden.rs     -- Sovereign enforcement layer
|   |-- sentinel.rs   -- Anomaly detection
|   |-- cortex.rs     -- Reasoning cortex
|   \-- journal.rs    -- Audit log
|-- examples/         -- Sample .dplus policy files
|-- policy.dplus      -- Default OO policy
\-- SPEC.md           -- Full specification
```

## Verdicts

| Verdict | Meaning |
|---------|---------|
| ALLOW | Execute |
| THROTTLE | Execute slowly |
| QUARANTINE | Isolate |
| FORBID | Block |
| EMERGENCY | Full stop |

## Quick host-side checks

On Windows/host development, the fastest path is to use the `std` feature for
the CLI tools:

```bash
cargo test
cargo run --features std --bin dplus_check -- policy-strict.dplus
cargo run --features std --bin dplus_judge -- policy-strict.dplus
pwsh ./test-smoke.ps1
pwsh ./test-smoke.ps1 -Configuration release
```

Notes:

- `policy-strict.dplus` is the positive smoke path and should pass `dplus_check`.
- `policy.dplus` is intentionally useful as a negative verifier case because it
	lacks matching proof entries for strict law/proof validation.
- `test-smoke.ps1` accepts `-Configuration debug|release`, `-SkipBuild`,
	`-PositivePolicy`, and `-NegativePolicy` so the same script can validate
	custom policy files without editing the repo script.

## Related

- [llm-baremetal](https://github.com/Djiby-diop/llm-baremetal) -- Bare-metal UEFI kernel
- [oo-host](https://github.com/Djiby-diop/oo-host) -- Host runtime

*Djiby Diop -- Operating Organism Project*
