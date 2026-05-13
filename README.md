# llm-baremetal — Operating Organism (Public Prototype)

> **A bare-metal UEFI x86_64 research engine combining LLM inference, bio-inspired organ modules, and autonomous decision-making — running entirely without an OS.**

---

## What is this?

**llm-baremetal** is the public prototype of the **Operating Organism (OO)** project — an experimental research system that runs large language model inference directly on hardware, without Linux, Windows, or any traditional OS.

The full project is developed privately. This repository demonstrates the core architectural concepts.

---

## Core Concepts

### 1. Zone Memory Allocator
Custom memory management without malloc/libc. Three zones:
- **ZONE_BOOT** — early UEFI scratch memory
- **ZONE_MODEL** — dedicated region for model weights (large, contiguous)
- **ZONE_INFERENCE** — KV-cache and activation buffers (fast, reusable)

### 2. UEFI Bare-Metal Boot
The system boots directly via UEFI (`.efi` application), takes over the CPU, installs its own GDT/IDT, and runs without any OS layer.

### 3. LLM Inference Engine
Transformer-based inference (Llama-2 architecture) running on bare hardware:
- Custom BLAS kernels (SSE2 / AVX2)
- GGUF model format support
- BPE tokenizer
- Mamba/SSM hybrid blocks

### 4. D+ Policy Engine
A policy system that gates all system actions. Every inference output, memory write, or external action passes through a D+ decision gate before execution.

### 5. Warden / Sentinel
Hardware-level safety layer. Monitors all subsystems, enforces isolation between zones, and can halt execution if invariants are violated.

### 6. Bio-Inspired Organ Modules
The system is structured as a biological organism with specialized organs:
- **soma** — central inference cortex
- **warden** — immune/safety system
- **memorion** — episodic memory
- **evolvion** — self-improvement engine
- **orchestrion** — task scheduler

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                  UEFI EFI Application                │
│                   (llama2.efi)                       │
├──────────────┬──────────────┬───────────────────────┤
│  Zone Alloc  │  D+ Policy   │  Warden / Sentinel    │
│  (3 zones)   │  (gate all)  │  (safety invariants)  │
├──────────────┴──────────────┴───────────────────────┤
│              LLM Inference Engine                     │
│   (Llama2 / Mamba / GGUF / AVX2 BLAS kernels)       │
├─────────────────────────────────────────────────────┤
│                Organ Bus (IPC)                        │
│  soma │ memory │ evolvion │ orchestrion │ ...        │
├─────────────────────────────────────────────────────┤
│          Bare-Metal Hardware (x86_64 UEFI)           │
└─────────────────────────────────────────────────────┘
```

---

## Build (Prototype)

**Requirements:** `gcc-efi`, `gnu-efi` headers, cross-compiler targeting `x86_64-pe`

```bash
# Ubuntu / Debian
sudo apt install gnu-efi gcc make

# Build the EFI application
make

# Run in QEMU (UEFI)
./run-qemu.ps1      # Windows
# or
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -drive file=llm-baremetal-boot.img,format=raw
```

---

## Zone Memory API (Sketch)

```c
// Initialize memory zones from UEFI memory map
llmk_zones_init(mmap, mmap_size, desc_size);

// Allocate from a specific zone
void *weights = llmk_zone_alloc(ZONE_MODEL, model_size);
void *kvcache  = llmk_zone_alloc(ZONE_INFERENCE, kv_size);

// Reset inference zone between requests (O(1))
llmk_zone_reset(ZONE_INFERENCE);
```

---

## D+ Policy Gate (Sketch)

```c
// Every action requires a policy decision
DPlusDecision d = dplus_evaluate(&ctx, ACTION_INFERENCE_OUTPUT, &payload);
if (d.verdict == DPLUS_ALLOW) {
    emit_token(payload.token);
} else {
    warden_log_violation(&d);
}
```

---

## Roadmap

| Version | Goal |
|---------|------|
| v0.1 | Stable UEFI boot + zone allocator + basic REPL |
| v0.2 | LLM inference (Llama-2 7B GGUF) integrated |
| v0.3 | D+ policy engine + warden active |
| v0.4 | Organ bus + bio-modules |
| v0.5 | Network oracle queries (bare-metal TLS) |
| v1.0 | Distributed intelligence across nodes |

---

## Research Vision

The Operating Organism is not an OS in the traditional sense. It is an **autonomous intelligent agent** that:
1. Runs its own inference without a host OS
2. Makes decisions via a structured policy engine (D+)
3. Evolves its own weights via LoRA self-improvement
4. Communicates across nodes via a bare-metal network stack
5. Protects itself via hardware-level warden/sentinel

This is early-stage systems research. The full implementation is maintained privately.

---

## License

Apache 2.0 — See [LICENSE](LICENSE)

**Author:** Djiby Diop  
**Contact:** See AUTHORS

---

> *"An operating system that thinks."*
