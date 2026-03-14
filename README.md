# llm-baremetal

UEFI x86_64 bare-metal LLM chat REPL (GNU-EFI). Boots from USB.

By Djiby Diop

## Architectural role

`llm-baremetal` is the **sovereign runtime** of the larger Operating Organism vision.
It is meant to be preserved and evolved as the bare-metal / survival / recovery pillar of the system, not replaced.

See [OO_GLOBAL_ARCHITECTURE.md](OO_GLOBAL_ARCHITECTURE.md) for the global direction.

## Build (Windows + WSL)

### Model weights (not in git)

Model weights (`.gguf` / legacy `.bin`) are intentionally not tracked in git.
Download them from Hugging Face (or any direct URL) into `models/`.

Windows:

```powershell
./scripts/get-weights.ps1 -Url "https://huggingface.co/<org>/<repo>/resolve/main/<file>.gguf" -OutName "<file>.gguf"
```

Linux:

```bash
./scripts/get-weights.sh "https://huggingface.co/<org>/<repo>/resolve/main/<file>.gguf" "<file>.gguf"
```

Then pass the model path to the build.

1) Ensure `tokenizer.bin` is present (this repo includes it by default).
2) Download a model file into `models/` (see above).
   - Supported today for inference: `.bin` (llama2.c export)
   - Supported today for inference: `.gguf` (F16/F32 + common quant types like Q4/Q5/Q8; see below)
   - You can also use a base name without extension (the image builder will copy `.bin` and/or `.gguf` if present)
3) Build + create boot image:

```powershell
./build.ps1
```

Example (base name):

```powershell
./build.ps1 -ModelBin models/stories110M

# or explicit file
./build.ps1 -ModelBin models/my-model.gguf
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

Note: some UEFI FAT drivers can be unreliable with long filenames. If you hit "file not found / open failed" issues, prefer an 8.3-compatible filename (e.g. `STORIES11.GGU`) or use the FAT 8.3 alias (e.g. `STORIE~1.GGU`) when setting `model=` in `repl.cfg`.

Boot the USB on an x86_64 UEFI machine, then select/load your model from the REPL.

## Recommended conversational setup (8GB RAM)

On an 8GB machine, "conversational" works best with a **small instruct/chat GGUF model** rather than a large 7B model.

Recommended target:

- Size: ~0.5B-1B parameters
- Format: `.gguf`
- Quantization: prefer variants that are supported by the current GGUF inferencer: `Q4_0/Q4_1/Q5_0/Q5_1/Q8_0` (avoid `Q4_K_*` / `Q5_K_*` for now)

Suggested first-run settings:

- Keep context small at first (e.g. 256-512) to avoid running out of RAM (KV cache grows with context).
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

Host -> sovereign handoff smoke:

```powershell
./test-qemu-handoff.ps1
```

Validate everything (recommended after pulling updates):

```powershell
./validate.ps1
```

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


