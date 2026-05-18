# Public Scope Policy

This document defines the intended public scope for `llm-baremetal`.

## Objective

Keep this repository focused on bare-metal runtime and prototype execution only.

## In Scope

- UEFI boot/runtime code
- inference runtime and low-level support code
- build, flash, smoke-test, and validation scripts
- legal/security/release documentation

## Out of Scope (should be moved to private or separate repos)

- private strategy/governance plans
- multi-repo orchestration documents
- broad ecosystem components not required to build/test baremetal runtime
- local debug dumps and transient artifacts

## Scope Check Command

Run:

```powershell
./scripts/public-scope-check.ps1
```

For blocking mode:

```powershell
./scripts/public-scope-check.ps1 -Strict
```

## Notes

- Non-strict mode reports candidates for cleanup.
- Strict mode fails when out-of-scope markers are detected.
