# llm-baremetal

Public bare-metal prototype for UEFI x86_64 inference.

This repository is intentionally limited to a clean baremetal scope: firmware/runtime code, build scripts, smoke tests, and release-safe docs.

## Scope

Included in this public repository:

- UEFI boot/runtime code
- bare-metal inference prototype code
- build/flash/test scripts
- legal/security/release documentation

Excluded from this public repository intent:

- private planning and internal governance content
- broad multi-repo orchestration material
- unrelated experiments not needed for baremetal build/test

## Quick Start

Windows:

```powershell
./build.ps1
./test-qemu-smoke.ps1
```

Linux:

```bash
make clean
make repl
```

## Public Release Gates

Before publication, run:

```powershell
./scripts/public-preflight.ps1
```

Main references:

- [docs/PUBLIC_RELEASE_PLAYBOOK.md](docs/PUBLIC_RELEASE_PLAYBOOK.md)
- [docs/THIRD_PARTY_LICENSE_AUDIT.md](docs/THIRD_PARTY_LICENSE_AUDIT.md)
- [docs/TRANSLATIONS.md](docs/TRANSLATIONS.md)
- [docs/SECURITY.md](docs/SECURITY.md)
- [docs/PUBLIC_SCOPE.md](docs/PUBLIC_SCOPE.md)

## Model Files

Model weights are not tracked in git. Keep model binaries in `models/` locally.

## Contribution Rule

Keep changes minimal, auditable, and directly relevant to baremetal runtime quality.
