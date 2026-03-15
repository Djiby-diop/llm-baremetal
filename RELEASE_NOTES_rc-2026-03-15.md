# Release Notes — rc-2026-03-15

Date: 2026-03-15
Tag: `rc-2026-03-15`
Status: draft for GitHub Release publication

## Highlights

- End-to-end host ↔ sovereign handoff flow validated.
- `oo-host` operator tooling hardened with explicit tests, CI checks, and artifact rendering.
- `llm-baremetal` validation now treats handoff + `sync-check` as a stable release gate when a sibling `oo-host` workspace is present.
- Relative `-OoHostRoot` overrides are now resolved robustly for sibling-clone workflows.

## What is included in this candidate

- x86_64 UEFI no-model boot image flow
- OS-G host tests and policy verification
- OS-G UEFI/QEMU smoke validation
- host → sovereign receipt extraction via `OOHANDOFF.TXT`
- aligned host/export/receipt verification via `oo-bot sync-check`

## Validation summary

- `./validate.ps1` passes
- `./validate.ps1 -OoHostRoot ..\oo-host` passes
- OS-G smoke reports `RESULT: PASS`
- handoff smoke reports `PASS`
- `oo-bot sync-check` reports `verdict               : aligned`

## Suggested release assets

- `llm-baremetal-boot-nomodel-x86_64.img.xz`
- `SHA256SUMS.txt`
- optional operator artifact bundle generated from `oo-host handoff-pack`

## Operator guidance

- This candidate does not bundle model weights.
- Keep `oo-host` adjacent to `llm-baremetal` when running the full release gate locally.
- Treat [RELEASE_CANDIDATE.md](RELEASE_CANDIDATE.md) as the short status page and this document as the publishable release draft.

## Known constraints

- Hardware virtualization is still reported disabled on the current validation host, so QEMU validation is running in a compatible non-accelerated path.
- Real hardware USB boot remains a separate confirmation step after image publication.