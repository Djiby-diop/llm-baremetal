# llm-baremetal

Public bare-metal prototype for UEFI x86_64 inference and boot-time experimentation.
This repository is intentionally kept narrow: firmware/runtime code, build scripts, tests, and release-safe documentation only.

## Scope

What stays in this public repo:

- UEFI and bare-metal runtime code
- model-loading and inference prototype code
- build, flash, and smoke-test scripts
- minimal public documentation and release gates

What belongs in the private repo:

- organism-wide strategy, multi-organ governance, and long-horizon plans
- private coordination docs and internal worktrees
- broader OO modules that are not needed to understand or build the baremetal prototype

## Public release readiness

Before publishing a change, run the public gates:

- Playbook: [docs/PUBLIC_RELEASE_PLAYBOOK.md](docs/PUBLIC_RELEASE_PLAYBOOK.md)
- Third-party audit template: [docs/THIRD_PARTY_LICENSE_AUDIT.md](docs/THIRD_PARTY_LICENSE_AUDIT.md)
- Translation policy: [docs/TRANSLATIONS.md](docs/TRANSLATIONS.md)
- Preflight script: `./scripts/public-preflight.ps1`

## Build and test

Use the existing platform scripts for the narrow public lane:

```powershell
./build.ps1
./test-qemu-smoke.ps1
```

For Linux builds, use the Makefile path documented in the repository scripts.

## Model files

Model weights are intentionally not tracked in git. Keep them in `models/` locally and do not commit them.

## Documentation

- Release process: [docs/PUBLIC_RELEASE_PLAYBOOK.md](docs/PUBLIC_RELEASE_PLAYBOOK.md)
- Security notes: [docs/SECURITY.md](docs/SECURITY.md)
- License audit: [docs/THIRD_PARTY_LICENSE_AUDIT.md](docs/THIRD_PARTY_LICENSE_AUDIT.md)
- Translation policy: [docs/TRANSLATIONS.md](docs/TRANSLATIONS.md)

## Contribution boundaries

- Keep changes additive, minimal, and auditable.
- Avoid broad refactors across unrelated subsystems.
- Preserve reproducibility and ASCII-safe docs where possible.

- Use Rufus: select the `.img` (or extract from `.img.xz` first), partition scheme **GPT**, target **UEFI (non CSM)**.

## Run (QEMU)

```powershell
./run.ps1 -Preflight -Gui
```

Host -> sovereign handoff smoke:

```powershell
./test-qemu-handoff.ps1

# optional if oo-host is not in the default sibling path
./test-qemu-handoff.ps1 -OoHostRoot ..\oo-host
```

This smoke flow also extracts OOHANDOFF.TXT beside the repo so [oo-host/sync-check](../oo-host/README.md) can verify the aligned host/export/receipt state.

Model-backed OO consult smoke in QEMU:

```powershell
./test-qemu-autorun.ps1 -Mode oo_consult_smoke -ModelBin stories15M.q8_0.gguf -SkipPrebuild
```

This validates `/oo_consult`, `/oo_log`, and `OOCONSULT.LOG` creation with a small bundled model before moving to real hardware.

No-model OO outcome / adaptation learning smoke in QEMU:

```powershell
./test-qemu-autorun.ps1 -Mode oo_outcome_smoke -Accel tcg -SkipPrebuild
```

This validates the consult -> persist -> reboot-verified outcome -> learned reselection loop, including `/oo_outcome`, `/oo_explain boot`, recent confirmed history, and operator-facing summaries persisted in `OOCONSULT.LOG`.

For faster iteration, use the unified QEMU wrapper [run-qemu-oo-validation.ps1](run-qemu-oo-validation.ps1):

```powershell
# run one focused lane
./run-qemu-oo-validation.ps1 -Mode consult -ModelBin stories15M.q8_0.gguf -Accel tcg -SkipPrebuild
./run-qemu-oo-validation.ps1 -Mode reboot -Accel tcg
./run-qemu-oo-validation.ps1 -Mode handoff -Accel tcg

# or run the core QEMU matrix end to end
./run-qemu-oo-validation.ps1 -Mode all-core -ModelBin stories15M.q8_0.gguf -Accel tcg -SkipPrebuild
```

The wrapper keeps QEMU as the primary iteration loop for no-model smoke, reboot continuity, host -> sovereign handoff, and model-backed OO consult so hardware reboots are reserved for larger milestones only.

For a real UEFI/USB handoff check, copy `sovereign_export.json` from the host runtime onto the FAT root of the USB image, then run [llmk-autorun-real-hw-handoff-smoke.txt](llmk-autorun-real-hw-handoff-smoke.txt) with `/autorun llmk-autorun-real-hw-handoff-smoke.txt`.

To stage that file from the sibling host workspace, use [llm-baremetal/prepare-real-hw-handoff.ps1](prepare-real-hw-handoff.ps1). It refreshes `oo-host/data/sovereign_export.json`, can copy both the export and the real-hardware handoff autorun script onto a mounted FAT/USB root, and can also build a dedicated `llm-baremetal-boot-real-hw-handoff.img` image with the export already injected.

For the next milestone — model-backed sovereign chat on a real machine — use [prepare-real-hw-chat.ps1](prepare-real-hw-chat.ps1). It generates a dedicated `llm-baremetal-boot-real-hw-chat.img` with a bundled model, a generated `repl.cfg`, and conversational defaults already set:

```powershell
./prepare-real-hw-chat.ps1 -ModelBin stories110M.bin

# optional: boot straight into a tiny chat smoke
./prepare-real-hw-chat.ps1 -ModelBin stories110M.bin -AutoSmoke
```

The helper keeps the image interactive by default. With `-AutoSmoke`, it points `autorun_file` at [llmk-autorun-real-hw-model-chat-smoke.txt](llmk-autorun-real-hw-model-chat-smoke.txt) so the machine can prove model load + first response automatically.

To continue the OO path with a real model, the same helper also supports `-AutoOoConsultSmoke`. That enables `oo_enable=1`, `oo_llm_consult=1`, and boots into [llmk-autorun-real-hw-oo-consult-smoke.txt](llmk-autorun-real-hw-oo-consult-smoke.txt) to prove model-backed `/oo_consult` plus `OOCONSULT.LOG` creation:

```powershell
./prepare-real-hw-chat.ps1 -ModelBin stories110M.bin -AutoOoConsultSmoke
```

For an interactive real-hardware OO image without autorun or auto-shutdown, use `-EnableOoConsult` instead. This keeps the boot in the REPL while pre-enabling `oo_enable=1` and `oo_llm_consult=1`:

```powershell
./prepare-real-hw-chat.ps1 -ModelBin stories110M.bin -EnableOoConsult -OutImagePath ..\llm-baremetal-boot-real-hw-oo-consult-interactive.img
```

Validated demo image:

```powershell
./prepare-real-hw-chat.ps1 -ModelBin stories110M.bin -EnableOoConsult -SkipPrebuild -CtxLen 256 -MaxTokens 96 -Temperature 0.75 -TopP 0.95 -TopK 80 -RepeatPenalty 1.15 -OutImagePath ..\llm-baremetal-boot-demo-stories110M.img
```

This produces a clean interactive USB/demo image with the bundled `stories110M.bin` model, conversational defaults, OO consult enabled, and no autorun shutdown path. After boot, a short live demo can be:

- `/cfg`
- `/diag`
- `hi`
- `/oo_status`
- `/oo_consult`
- `/oo_explain`

Published demo artifacts on Hugging Face now include both the raw and compressed forms:

- `llm-baremetal-boot-demo-stories110M.img`
- `llm-baremetal-boot-demo-stories110M.img.xz`
- `SHA256SUMS-demo-stories110M.txt`
- `SHA256SUMS-demo-stories110M-xz.txt`

After the real-machine run, collect the produced OO artifacts from the mounted FAT partition or from an image copy with [collect-real-hw-oo-artifacts.ps1](collect-real-hw-oo-artifacts.ps1):

```powershell
./collect-real-hw-oo-artifacts.ps1 -UsbRoot E:\

# or directly from an image file
./collect-real-hw-oo-artifacts.ps1 -ImagePath .\llm-baremetal-boot-real-hw-chat.img
```

It gathers `OOCONSULT.LOG`, `OOJOUR.LOG`, `OOSTATE.BIN`, `OORECOV.BIN`, `OOHANDOFF.TXT`, and `llmk-diag.txt` into a timestamped folder under `artifacts/` and writes a small summary file for review.

Then validate the collected folder with [validate-real-hw-oo-artifacts.ps1](validate-real-hw-oo-artifacts.ps1):

```powershell
./validate-real-hw-oo-artifacts.ps1

# explicit folder also works
./validate-real-hw-oo-artifacts.ps1 -ArtifactsDir .\artifacts\real-hw-oo-20260316-012323
```

By default it expects `OOSTATE.BIN`, `OORECOV.BIN`, `OOJOUR.LOG`, and a consult trace in `OOCONSULT.LOG`. Optional stricter checks are available with `-RequireDiag` and `-RequireHandoff`.

If you want a single entrypoint for the whole real-machine consult milestone, use [run-real-hw-oo-consult-validation.ps1](run-real-hw-oo-consult-validation.ps1):

```powershell
# phase 1: prepare the real-hardware image
./run-real-hw-oo-consult-validation.ps1 -Phase prepare -ModelBin stories110M.bin

# phase 2: after the physical boot, collect + validate from the mounted USB FAT root
./run-real-hw-oo-consult-validation.ps1 -Phase collect -UsbRoot E:\
```

The `prepare` phase builds the image with `-AutoOoConsultSmoke`; the `collect` phase chains collection plus validation automatically.

For the real-machine host -> sovereign handoff milestone, use [run-real-hw-handoff-validation.ps1](run-real-hw-handoff-validation.ps1):

```powershell
# phase 1: refresh host export + build the dedicated handoff image
./run-real-hw-handoff-validation.ps1 -Phase prepare

# phase 2: after the physical boot, collect + validate from the mounted USB FAT root
./run-real-hw-handoff-validation.ps1 -Phase collect -UsbRoot E:\
```

The `prepare` phase refreshes `oo-host/data/sovereign_export.json` and builds `llm-baremetal-boot-real-hw-handoff.img`; the `collect` phase requires `OOHANDOFF.TXT`, allows a missing consult log, writes a handoff-focused validation report, and runs `oo-bot sync-check` when the sibling [oo-host](../oo-host/README.md) workspace is available.

For the real-machine reboot continuity milestone, use [run-real-hw-oo-reboot-validation.ps1](run-real-hw-oo-reboot-validation.ps1):

```powershell
# phase 1: build the dedicated reboot continuity image
./run-real-hw-oo-reboot-validation.ps1 -Phase prepare

# phase 2: after the physical reboot cycle, collect + validate from the mounted USB FAT root
./run-real-hw-oo-reboot-validation.ps1 -Phase collect -UsbRoot E:\
```

The `prepare` phase builds `llm-baremetal-boot-real-hw-oo-reboot.img` with `oo_enable=1` and the reboot smoke autorun; the firmware also makes a best-effort attempt to set UEFI `BootNext` to the current USB boot entry before resetting so the second boot returns to the USB device more reliably. The `collect` phase requires the `reboot_probe_arm` and `reboot_probe_verified` journal markers, allows a missing consult log, and writes a reboot-focused validation report.

The chained `collect` phase also writes `oo-real-validation-report.md` into the artifact folder so the real-machine milestone has a human-readable receipt with artifact sizes, consult decision, confidence fields, and parsed journal events.

The host runtime lives in the separate `oo-host` repository and is expected by default as a sibling clone beside this repo.

Validate everything (recommended after pulling updates):

```powershell
./validate.ps1

# explicit override also works with a relative sibling path
./validate.ps1 -OoHostRoot ..\oo-host
```

When the sibling [oo-host](../oo-host/README.md) workspace is present, validation also runs the handoff smoke plus `oo-bot sync-check` end to end. Relative `-OoHostRoot` overrides are resolved against the repo root first, so sibling-path invocations stay stable.

## Release candidate

The current release-candidate status is tracked in [RELEASE_CANDIDATE.md](RELEASE_CANDIDATE.md).

## OS-G (Operating System Genesis) — pillar

OS-G is included as a self-contained kernel-governor prototype (Memory Warden + D+ pipeline) under:

- `OS-G (Operating System Genesis)/`

Quick validation (UEFI/QEMU smoke test, prints `RESULT: PASS/FAIL`):

```powershell
./run-osg-smoke.ps1 -Profile release

# or via the main runner
./run.ps1 -OsgSmoke
```

Host-side tests/tools (requires `std` feature):

```powershell
cd 'OS-G (Operating System Genesis)'
cargo test --features std
```

## Notes

- Model weights are intentionally not tracked in git; use GitHub Releases or your own files.
- Optional config: copy `repl.cfg.example` -> `repl.cfg` (not committed) and rebuild.

Optional OO policy gate:

- If a file named `policy.dplus` exists on the FAT root, the firmware treats it as a D+ policy (OS-G style) and gates `/oo*` commands from it.
- Otherwise, it falls back to a simpler legacy file `oo-policy.dplus`.
- If neither file is present, behavior is unchanged.

Example `policy.dplus` (D+ style; deny-by-default; requires `@@LAW` + `@@PROOF`):

```text
@@LAW
allow /oo_list
allow /oo_new
allow /oo_note
deny /oo_exec*

@@PROOF
proof op:7
```

Legacy example `oo-policy.dplus` (best-effort):

```text
mode=deny_by_default
allow=/oo_list
allow=/oo_new
allow=/oo_note
deny=/oo_exec*
```



