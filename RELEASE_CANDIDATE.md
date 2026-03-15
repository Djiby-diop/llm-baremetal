# Release Candidate

Date: 2026-03-15
Status: candidate ready
Scope: host ↔ sovereign handoff loop validated end to end

## Included validation

- `./validate.ps1`
- `./validate.ps1 -OoHostRoot ..\oo-host`
- OS-G host tests and `dplus_check`
- OS-G UEFI/QEMU smoke
- host → sovereign handoff smoke
- `oo-bot sync-check`

## Current expected result

- `RESULT: PASS` in the OS-G smoke flow
- `OOHANDOFF.TXT` extracted beside the repo
- `oo-bot sync-check` reports `verdict : aligned`
- no pending local changes in `llm-baremetal` or `oo-host`

## Operator note

If `oo-host` is present as a sibling repository, `validate.ps1` should be treated as the default release gate before producing a boot image or handing off a continuity receipt.

## Release publication

- Draft notes: [RELEASE_NOTES_rc-2026-03-15.md](RELEASE_NOTES_rc-2026-03-15.md)
- Tag: `rc-2026-03-15`
