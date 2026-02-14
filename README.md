# llm-baremetal

UEFI x86_64 bare-metal LLM chat REPL (GNU-EFI). Boots from USB.

By Djiby Diop

## Build (Windows + WSL)

1) Put `tokenizer.bin` and a model file in this folder.
   - Supported today for inference: `.bin` (llama2.c export)
   - Supported today for inference: `.gguf` (F16/F32 + common quant types like Q4/Q5/Q8; see below)
   - You can also use a base name without extension (the image builder will copy `.bin` and/or `.gguf` if present)
2) Build + create boot image:

```powershell
./build.ps1
```

Example (base name):

```powershell
./build.ps1 -ModelBin stories110M
```

## Build (Linux)

Prereqs (Ubuntu/Debian):

```bash
sudo apt-get update
sudo apt-get install -y build-essential gnu-efi mtools parted dosfstools grub-pc-bin
```

Then:

```bash
cd llm-baremetal
make clean
make repl

# Build an image with a bundled model:
# MODEL=stories110M ./create-boot-mtools.sh

# Or build a small image without embedding weights (copy your model later):
NO_MODEL=1 ./create-boot-mtools.sh
```

## Prebuilt image (x86_64)

GitHub Releases provides a prebuilt **x86_64 no-model** boot image.
It intentionally does **not** bundle any model weights, and it does **not** hardcode a model path.

Download these assets from the latest Release:

- `llm-baremetal-boot-nomodel-x86_64.img.xz`
- `SHA256SUMS.txt`

Verify + extract (Linux):

```bash
sha256sum -c SHA256SUMS.txt
xz -d llm-baremetal-boot-nomodel-x86_64.img.xz
```

Flash to a USB drive (Linux, replace `/dev/sdX`):

```bash
sudo dd if=llm-baremetal-boot-nomodel-x86_64.img of=/dev/sdX bs=4M conv=fsync status=progress
```

Copy your model to the USB EFI/FAT partition:

- Copy your model file (`.gguf` or legacy `.bin`) to the root of the FAT partition (or create a `models/` folder and put it there).
- `tokenizer.bin` is already included in the Release image.

Note: some UEFI FAT drivers can be unreliable with long filenames. If you hit “file not found / open failed” issues, prefer an 8.3-compatible filename (e.g. `STORIES11.GGU`) or use the FAT 8.3 alias (e.g. `STORIE~1.GGU`) when setting `model=` in `repl.cfg`.

Boot the USB on an x86_64 UEFI machine, then select/load your model from the REPL.

## Recommended conversational setup (8GB RAM)

On an 8GB machine, “conversational” works best with a **small instruct/chat GGUF model** rather than a large 7B model.

Recommended target:

- Size: ~0.5B–1B parameters
- Format: `.gguf`
- Quantization: prefer variants that are supported by the current GGUF inferencer: `Q4_0/Q4_1/Q5_0/Q5_1/Q8_0` (avoid `Q4_K_*` / `Q5_K_*` for now)

Suggested first-run settings:

- Keep context small at first (e.g. 256–512) to avoid running out of RAM (KV cache grows with context).
- If your model is Q8_0 and you want lower RAM usage, enable `gguf_q8_blob=1` (default in the Release image).

Useful REPL commands:

- `/models` to list `.gguf`/`.bin` found in the root and `models\\`
- `/model_info <file>` to inspect a model before loading
- `/cfg` to confirm effective `repl.cfg` settings

### Flashing from Windows

- Use Rufus: select the `.img` (or extract from `.img.xz` first), partition scheme **GPT**, target **UEFI (non CSM)**.

## Run (QEMU)

```powershell
./run.ps1 -Preflight -Gui
```

### M6 workflow (release-ready)

Use this sequence as the default operator path:

```powershell
./preflight-host.ps1
./build.ps1 -ModelBin stories110M.bin
./run.ps1 -Preflight -Gui
```

For headless runs:

```powershell
./run.ps1 -Preflight
```

### M6.1 final verification (one command)

Run the full operator flow in one command:

```powershell
./m6-verify.ps1 -ModelBin stories110M.bin -Gui
```

Fast check (preflight + build only):

```powershell
./m6-verify.ps1 -ModelBin stories110M.bin -SkipRun
```

Release checklist:

- Preflight says `READY`.
- Build produces a fresh boot image.
- QEMU starts from `run.ps1 -Preflight` (GUI or headless).

### M6.2 release package check

Validate release artifacts and SHA256 integrity:

```powershell
./m6-package-check.ps1
```

Rebuild artifacts before checking:

```powershell
./m6-package-check.ps1 -Rebuild
```

### M6.3 release upload prep

Prepare a clean GitHub Release upload bundle (`.img.xz` + `SHA256SUMS.txt` + notes):

```powershell
./m6-release-prep.ps1 -Rebuild
```

Optional: include raw `.img` in the upload bundle:

```powershell
./m6-release-prep.ps1 -Rebuild -IncludeRawImage
```

### M8 reliability pass (A1/A3/B1/B2/B3)

Static checks only (fast):

```powershell
./m8-reliability.ps1 -SkipPreflight -SkipBuild
```

End-to-end runtime checks (build + QEMU autorun scenario):

```powershell
./m8-reliability.ps1 -RunQemu -Accel tcg -TimeoutSec 150
```

Runtime checks with explicit startup latency budgets:

```powershell
./m8-reliability.ps1 -RunQemu -Accel tcg -TimeoutSec 150 -MaxModelSelectMs 5000 -MaxModelPrepareMs 30000
```

The runtime log is written to `artifacts/m8/m8-qemu-serial.log`.

### M9 regression guardrails

Parse an M8 runtime log and enforce marker/latency budgets:

```powershell
./m9-guardrails.ps1 -LogPath artifacts/m8/m8-qemu-serial.log -MaxModelSelectMs 2000 -MaxModelPrepareMs 12000 -RequireOoMarkers
```

M9.1 trend tracking is enabled by default:

- appends run history to `artifacts/m9/history.jsonl`
- checks drift versus recent runs (`-DriftWindow`, `-MaxSelectDriftPct`, `-MaxPrepareDriftPct`)
- can be disabled with `-NoDriftCheck` and/or `-NoHistoryWrite`

### M10 policy quality guardrails

Evaluate OO adaptation quality and trigger auto-quarantine when harmful behavior is detected:

```powershell
./m10-quality-guardrails.ps1 -LogPath artifacts/m8/m8-qemu-serial.log -MaxHarmfulRatioPct 40 -MaxConsecutiveFailures 2 -MinAutoApplySamples 2 -ApplyQuarantine
```

Outputs:

- quarantine state: `artifacts/m10/quarantine-state.json`
- M10 history: `artifacts/m10/history.jsonl`
- optional quarantine action on config: `oo_auto_apply=0` (+ `oo_conf_gate=1`)

M10.1 adaptive thresholds are enabled by default:

- model class inference from runtime log (`tiny`, `medium`, `large`)
- RAM tier inference (`low`, `mid`, `high`) from runtime memory / Zone B
- dynamic effective thresholds for harmful ratio, failure streak, and min samples
- disable if needed with `-NoAdaptiveThresholds`

### M11 self-healing quarantine release

Evaluate quarantine release readiness and drive canary recovery:

```powershell
./m11-self-heal.ps1 -LogPath artifacts/m8/m8-qemu-serial.log -ApplyRelease
```

Behavior:

- requires configurable stable release streak (`-StableStreakNeeded`)
- starts canary mode (`-CanaryBoots`) with stricter confidence gate (`-CanaryConfThreshold`)
- auto-rolls back to quarantine if canary quality degrades
- writes state/history to `artifacts/m11/release-state.json` and `artifacts/m11/release-history.jsonl`

M11.1 coupling (enabled by default):

- release/canary progression requires a stable M9 window (`artifacts/m9/history.jsonl`, `-M9StableWindow`)
- and a stable M10 quality window (`artifacts/m10/history.jsonl`, `-M10StableWindow`)
- both windows must be green in addition to current-run quality checks

### M12 policy curriculum (phase + workload)

Apply staged confidence thresholds from boot phase and workload class:

```powershell
./m12-policy-curriculum.ps1 -LogPath artifacts/m8/m8-qemu-serial.log -ApplyConfig
```

Behavior:

- infers boot phase from M9 run maturity (`early|warm|steady`)
- infers workload class from consult intents (`latency_optimization|context_expansion|mixed|unknown`)
- computes effective `oo_conf_threshold` from base phase threshold + workload adjustment
- writes state/history to `artifacts/m12/curriculum-state.json` and `artifacts/m12/history.jsonl`

M12.1 outcome feedback (enabled by default):

- reads recent M10 outcomes from `artifacts/m10/history.jsonl`
- computes helpful/harmful window score (`-OutcomeAdaptWindow`, `-OutcomeAdaptStep`)
- auto-tunes phase thresholds and active workload cell before final threshold computation
- can be disabled with `-NoOutcomeAdaptation`

### M13 explainability pack (reason codes + provenance)

Generate explainability artifacts for OO policy decisions:

```powershell
./m13-explainability.ps1 -LogPath artifacts/m8/m8-qemu-serial.log
```

Behavior:

- extracts per-run auto-apply events (`success`/`failed`) with reason codes
- aggregates reason-code context from M10/M11/M12 state outputs
- persists threshold provenance across guardrails/curriculum layers
- writes state/history to `artifacts/m13/explainability-state.json` and `artifacts/m13/history.jsonl`

M13.1 native reason IDs:

- runtime OO core now emits explicit `reason_id=...` tokens in policy/auto-apply markers
- `m13-explainability.ps1` consumes these native IDs as primary `reason_code` values (fallback kept for older logs)

### M14 explainability coverage (reason_id + parity)

Check runtime marker coverage and optional journal parity:

```powershell
./m14-explainability-coverage.ps1 -LogPath artifacts/m8/m8-qemu-serial.log -FailOnCoverageGap
```

Behavior:

- verifies `reason_id` coverage for `OO confidence`, `OO plan`, and `OO auto-apply` markers
- optionally checks parity against `OOJOUR.LOG` when available (`-RequireJournalParity`)
- writes state/history to `artifacts/m14/coverage-state.json` and `artifacts/m14/history.jsonl`

### M8.1 CI workflow

GitHub Actions workflow: `.github/workflows/m8-reliability.yml`

- On push/PR: runs M8 static pass (`m8-reliability.ps1 -SkipPreflight -SkipBuild`) on `windows-latest`.
- Manual dispatch: optional runtime pass on `self-hosted` Windows runner with QEMU/WSL.

Runtime dispatch inputs:

- `run_runtime=true`
- `timeout_sec` (default `180`)

### Synthese des ameliorations

Consultez `AMELIORATIONS_APPORTEES.md` pour la liste consolidée des améliorations livrées (M6 -> M14).

## Notes

- Model weights are intentionally not tracked in git; use GitHub Releases or your own files.
- Optional config: copy `repl.cfg.example` → `repl.cfg` (not committed) and rebuild.
- Next roadmap after M6.x: see `INNOVATIONS_NEXT.md`.

