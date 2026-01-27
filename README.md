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

## Run (QEMU)

```powershell
./run.ps1 -Gui
```

## Notes

- Model weights are intentionally not tracked in git; use GitHub Releases or your own files.
- Optional config: copy `repl.cfg.example` → `repl.cfg` (not committed) and rebuild.

