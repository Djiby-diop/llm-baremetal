# Operating Organism — Roadmap

## v0.1 — Stable Kernel (current)
- [x] UEFI bare-metal boot (EFI application)
- [x] Zone memory allocator (BOOT / MODEL / INFERENCE)
- [x] Basic REPL (keyboard input, UART output)
- [x] D+ policy engine (binary policy file)
- [x] Warden / Sentinel safety layer

## v0.2 — Integrated LLM
- [x] GGUF model loader (Llama-2 architecture)
- [x] AVX2 BLAS kernels (djiblas)
- [x] BPE tokenizer
- [x] Mamba/SSM hybrid blocks
- [ ] Quantized inference (Q4_K_M)

## v0.3 — Distributed Intelligence
- [ ] Bare-metal Ethernet (UDP multicast)
- [ ] Federation protocol (peer discovery + weight sharing)
- [ ] WASM module loader (hot-loadable extensions)
- [ ] LoRA self-improvement (on-device fine-tuning)

## v0.4 — Full Organism
- [ ] Organ bus (bio-inspired IPC between modules)
- [ ] NVMe storage driver (persistent journal)
- [ ] MMU + page tables (full CPU takeover)
- [ ] LAPIC preemption (cooperative scheduler)

## v1.0 — Research Release
- [ ] Distributed multi-node inference
- [ ] Public paper
