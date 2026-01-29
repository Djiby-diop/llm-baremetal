# llm-baremetal

UEFI x86_64 bare-metal LLM chat REPL (GNU-EFI). Boots from USB.

Made in Senegal 🇸🇳 by Djiby Diop

## Build (Windows + WSL)

1) Put `tokenizer.bin` and a model file in this folder.
	- Supported today for inference: `.bin` (llama2.c export)
	- Supported today for inspection: `.gguf` (via `/model_info`)
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

Boot the USB on an x86_64 UEFI machine, then select/load your model from the REPL.

## Run (QEMU)

```powershell
./run.ps1 -Gui
```

## Notes

- Model weights are intentionally not tracked in git; use GitHub Releases or your own files.
- Optional config: copy `repl.cfg.example` → `repl.cfg` (not committed) and rebuild.

