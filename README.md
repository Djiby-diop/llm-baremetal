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
./run.ps1 -Gui
```

### QEMU autorun tests

Tip: you can also run autorun tests from the repo root using the wrapper `./test-qemu-autorun.ps1 ...` (so you don’t need `cd .\\llm-baremetal`, which can be noisy if you’re already in that folder).

OO M1 smoke (persistence: writes `OOSTATE.BIN` + appends `OOJOUR.LOG`, validates across 2 boots):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_smoke -Accel tcg -TimeoutSec 600 -SkipInspect
```

OO M2 recovery (corrupts `OOSTATE.BIN` between boots, asserts SAFE rollback + `event=recover`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_recovery -Accel tcg -TimeoutSec 600 -SkipInspect
```

OO M3 homeostasis proof (clamps effective context length in SAFE/DEGRADED, checks for serial marker):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_ctx_clamp -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```

OO M3 homeostasis proof (reach DEGRADED then clamp with DEGRADED cap; 3 boots without snapshot):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_ctx_clamp_degraded -Accel tcg -TimeoutSec 900 -SkipInspect -SkipBuild
```

OO M3 policy proof (invalid model in repl.cfg triggers fallback + OO marker):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_model_fallback -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```

OO M3 policy proof (RAM budget preflight: low-RAM SAFE zone minimum marker; defaults to `-MemMB 640` unless overridden):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_ram_preflight -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```

OO M3 policy proof (RAM preflight reduces seq_len under tight RAM; uses `oo_min_total_mb=0` and defaults to `-MemMB 620`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_ram_preflight_seq -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```
OO M5 LLM advisor proof (LLM suggests system adaptations, policy engine decides; runs in SAFE mode with 640MB RAM):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\test-qemu-autorun.ps1 -Mode oo_llm_consult -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```
Note (VS Code → PowerShell): if VS Code formats a path into a Markdown-ish link like `.[test-qemu-autorun.ps1](http://...)`, don’t paste that into the terminal. Use a normal path like `.\test-qemu-autorun.ps1 ...`.

Optional (bootstrap pinned tool wrappers before a build/test run):

```powershell
./test-qemu-autorun.ps1 -BootstrapToolchains
./bench-matrix.ps1 -BootstrapToolchains
```

## Notes

- Model weights are intentionally not tracked in git; use GitHub Releases or your own files.
- Optional config: copy `repl.cfg.example` → `repl.cfg` (not committed) and rebuild.

