# OO Operating Organism — Complete Topology Map (Phase Z)

## Architecture Overview

```
╔══════════════════════════════════════════════════════════════════════════════════╗
║                     OO OPERATING ORGANISM — FULL TOPOLOGY                       ║
║                     Phase Z Topology Map (May 2026)                              ║
╚══════════════════════════════════════════════════════════════════════════════════╝

┌─────────────────────────────────────────────────────────────────────────────────┐
│  LAYER 0: BARE METAL (EFI / x86-64)                                             │
│                                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │  oo-kernel   │  │  oo-warden   │  │   engine/    │  │  oo-modules/     │   │
│  │  (C, UEFI)   │  │  (sentinel,  │  │  (LLM infer  │  │  12 engines      │   │
│  │              │  │   D+ policy) │  │   llama2/ssm)│  │  (C modules)     │   │
│  │  boot phases │  │              │  │              │  │                  │   │
│  │  1-7        │  │  OoPressure  │  │  SplitbrainC │  │  ghost/evolvion  │   │
│  │  REPL        │  │  LlmkSentin.│  │  HebbianTab. │  │  dreamion/morph  │   │
│  │  /display    │  │  DplusPolicy│  │  KvPersist   │  │  conscience/symp │   │
│  │  /soma_state │  │              │  │              │  │  metabion/neural │   │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘   │
│         │                 │                  │                   │             │
│         └─────────────────┴──────────────────┴───────────────────┘             │
│                                    │                                            │
│                           ┌────────▼────────┐                                  │
│                           │   OoBootCtx     │   Master boot context            │
│                           │  (all phases)   │   Shared by all kernel modules   │
│                           └────────┬────────┘                                  │
│                                    │                                            │
│                           ┌────────▼────────┐                                  │
│                           │   oo-bus/hermes │   Hermes kernel-side bus         │
│                           │  (C, channels)  │   OO_CH_COLLECTIVION_SYNC etc.   │
│                           └────────┬────────┘                                  │
│                                    │ UART / serial (COM1)                      │
└────────────────────────────────────┼────────────────────────────────────────────┘
                                     │
                         ┌───────────▼───────────┐
                         │    UART BRIDGE        │
                         │  oo-kernel-rust FFI   │
                         │  [oo-event] JSON lines│
                         │  Protocol:            │
                         │  IN:  {"kind":"swarm  │
                         │        _event",...}   │
                         │  OUT: {"kind":"state  │
                         │        _transition",.}│
                         └───────────┬───────────┘
                                     │
┌────────────────────────────────────┼────────────────────────────────────────────┐
│  LAYER 1: HOST RUNTIME (Rust)       │                                            │
│                                     │                                            │
│                           ┌─────────▼─────────┐                                 │
│                           │    oo-host        │   Rust binary                   │
│                           │  (main runtime)   │   215 tests                     │
│                           │                   │                                 │
│  ┌─────────────────────────────────────────────────────────────────┐            │
│  │                    oo-host modules                              │            │
│  │                                                                 │            │
│  │  bus.rs          — File-based JSONL bus (inbox/outbox/bcast)   │            │
│  │  diop_bridge.rs  — DIOP HTTP gateway (raw TcpStream, no reqwest)│            │
│  │  diop_model.rs   — DIOP trained model registry + inference     │            │
│  │  swarm.rs        — Multi-instance swarm coordinator            │            │
│  │  governor.rs     — Goal governor (priority, scheduling)        │            │
│  │  uart_bus.rs     — UART serial bridge to kernel                │            │
│  │  serial.rs       — COM1 raw serial I/O                         │            │
│  │  oo_event_bridge.rs — [oo-event] → BusMessage translation      │            │
│  │  state.rs        — Persistent RuntimeState                     │            │
│  │  policy.rs       — D+ policy evaluation (Rust side)            │            │
│  │  training.rs     — Training journal management                 │            │
│  │  vitals.rs       — PMU / system vitals reporting               │            │
│  └─────────────────────────────────────────────────────────────────┘            │
│                                                                                 │
│  CLI Commands:                                                                  │
│    status | goal | bus | consensus | governor | swarm                           │
│    diop {run|status|ping|react}                                                 │
│    diop-model {status|broadcast|infer|llama|watch}    ← Phase X NEW            │
│    self-schedule | publish | fat32 | kv                                         │
│                                                                                 │
│                      ┌────────────────┐                                         │
│                      │  OO FILE BUS   │   Flat JSONL files                     │
│                      │                │   bus/inbox/<id>.jsonl                  │
│                      │  MsgKind:      │   bus/outbox/<id>.jsonl                 │
│                      │  heartbeat     │   bus/broadcast.jsonl                   │
│                      │  goal_sync     │                                         │
│                      │  swarm_event   │   NEW (Phase X):                        │
│                      │  diops_event   │   inference_result                      │
│                      │  state_trans.  │   diops_model_status                   │
│                      │  dplus_verdict │                                         │
│                      │  warden_alert  │                                         │
│                      │  uart_event    │                                         │
│                      └───────┬────────┘                                         │
└──────────────────────────────┼──────────────────────────────────────────────────┘
                               │
        ┌──────────────────────┼──────────────────────┐
        │                      │                      │
        ▼                      ▼                      ▼
┌───────────────┐   ┌─────────────────┐   ┌──────────────────────┐
│  LAYER 2a     │   │  LAYER 2b       │   │  LAYER 2c            │
│  oo-bot       │   │  DIOP           │   │  desktop_display     │
│  (Python)     │   │  (Python)       │   │  (C, UEFI app)       │
│  47 tests     │   │                 │   │                      │
│               │   │  DIOPOrchest.   │   │  oo_hud_final.efi    │
│  BotBusState  │   │  9 Workers:     │   │  SOMA HUD            │
│  diop_worker  │   │  analysis       │   │  OO sphere render    │
│  diop_last_   │   │  architecture   │   │  color ← node_state  │
│  kind         │   │  code           │   │                      │
│  diop_model_  │   │  refactor       │   │  SOMA bridge:        │
│  count        │   │  science        │   │  oo_soma_bridge.h    │
│  diop_last_   │   │  baremetal      │   │  UART JSON in/out    │
│  infer_summ.  │   │  compiler       │   │  state: ACTIVE →     │
│               │   │  warden         │   │  SYNCING → DEGRADED  │
│  Handlers:    │   │  executor       │   │  ISOLATED → EMERGENCY│
│  goal_sync    │   │                 │   │                      │
│  heartbeat    │   │  6 Adapters:    │   │  Peer count: from    │
│  swarm_event  │   │  mock           │   │  oo_repl_notify_     │
│  diops_event  │   │  local          │   │  bus_event()         │
│  infer_result │   │  native(C FFI)  │   │                      │
│  model_status │   │  swarm          │   │                      │
│               │   │  trained(PT)    │   │                      │
│  bus_bridge   │   │  llama_cpp      │   │                      │
│  .py          │   │                 │   │                      │
└───────┬───────┘   │  3 Models:      │   └──────────────────────┘
        │           │  diop_model     │
        │           │  (83MB, 8L/8H)  │
        │           │  diop_warden    │
        │           │  (83MB)         │
        │           │  diop_architect │
        │           │  (6MB, 4L/4H)   │
        │           │                 │
        │           │  Gateway API:   │
        │           │  GET /api/health│
        │           │  GET /api/runtime│
        │           │  POST /api/genr.│
        │           │  (http://127.0  │
        │           │  .0.1:8000)     │
        │           └────────┬────────┘
        │                    │
        └────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────────────────────────┐
│  LAYER 3: TRAINING / DATASET PIPELINE                                            │
│                                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐    │
│  │  djib/dataset/                          oo-model-repo/data/             │    │
│  │    djibion_base.jsonl  (144 samples)      engine_training/              │    │
│  │    djibion_v2.jsonl    (161 samples)        oo_native_concepts.jsonl    │    │
│  │    djibion_v3.jsonl    (40 samples)         (63 samples, 12 engines)    │    │
│  │                                            governor_concepts.jsonl      │    │
│  │  diop/engine/model/                        warden_bus_events.jsonl      │    │
│  │    train.jsonl  (104 KB)                   self_introspection.jsonl     │    │
│  │    diop_model.pt  (83 MB)                  halt_calibration.jsonl       │    │
│  │    diop_architect.pt  (6 MB)               code_domain.jsonl            │    │
│  │    diop_warden.pt  (83 MB)                 swarm_coordination.jsonl     │    │
│  │                                                                         │    │
│  │  → generate_mamba3_dataset.py → data/processed/mamba3_training.jsonl   │    │
│  │    365 samples (shuffled, seed=42)                                      │    │
│  │    Domain: OO_META 67% / MATH 16% / SELF 7% / REASONING 4%             │    │
│  │                                                                         │    │
│  │  → hf_push_dataset.py → HuggingFace djibydiop/llm-baremetal            │    │
│  └─────────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## Interface Table

| Interface | From | To | Protocol | Format |
|-----------|------|----|----------|--------|
| UART serial | kernel (COM1) | oo-host uart_bus | raw bytes | `[oo-event] JSON\n` |
| SOMA UART | oo_soma_bridge | desktop_display | COM1 JSON | `{"kind":"swarm_event",...}` |
| OO file bus | oo-host | oo-bot | JSONL files | `BusMessage` schema |
| OO file bus | oo-host | oo-host (swarm) | JSONL files | `BusMessage` schema |
| DIOP gateway | oo-host diop_bridge | DIOP HTTP | HTTP/1.0 raw | `/api/generate POST` |
| DIOP trained | oo-host diop_model | Python subprocess | stdout | `SUMMARY: <text>` |
| Hermes bus | oo-kernel modules | oo-kernel modules | C structs | `hermes_msg_t` |
| D+ gate | kernel → Rust | D+ policy engine | FFI | `oo-dplus` crate |
| llama-cli | oo-host diop_model | WinGet binary | subprocess | `--chatml --model` |
| KV persist | kernel | EFI volume | binary | `OO_KVC.BIN` |
| Training journal | kernel | EFI volume | JSONL | `OO_TRAIN.JSONL` |
| neuralfs | kernel | EFI volume | binary | `OO_NEURALFS.BIN` |
| GGUF model | engine/llama2 | RAM (ARENA_WEIGHTS) | binary | llama2.c format |
| OOSI v3 model | engine/llama2+ssm | RAM (mapped) | binary | magic "OOS3", zero-copy |

---

## OOSI v3 Boot Path (Phase Z)

```
efi_main()
    │
    ├─ llmk_detect_model_format() ──► magic "OOS3" → LLMK_MODEL_FMT_OOSI3
    │
    │   (skip .bin header parse — goto oosi3_boot_done)
    │
    └─ oosi3_boot_done:
           │
           ▼
        llmk_repl()   ← REPL starts immediately
           │
           │  user types: /think <prompt>
           ▼
        soma_repl.c → llmk_oo_infer_think()
           │
           ├─ g_v3_ready==1 → oosi_v3_generate()
           │     │
           │     ├─ oosi_v3_forward_one() × N tokens
           │     └─ OosiV3HaltHead → adaptive loop halt
           │
           └─ output chars → Print()
```


---

## Data Flow: Inference Request → Bus → Bot → DIOP → Back

```
User types in REPL
      │
      ▼
 oo-kernel REPL
  /soma_state
      │ derives OoNodeState from
      │ pressure + sentinel + splitbrain
      │
      ▼
  oo_repl_notify_bus_event(peer_count)
      │ updates g_soma_peer_count
      │
      ├─── serial UART ──► oo-host uart_bus.rs
      │                         │
      │                         ▼
      │                   [oo-event] JSON parsed
      │                   → BusMessage(kind=uart_event)
      │                   → broadcast to bus
      │
      │                         │
      │                         ▼
      │                   oo-bot bus_bridge.py
      │                   react_to_messages()
      │                         │
      │              ┌──────────┴──────────┐
      │              │                     │
      │        swarm_event          diops_event
      │        handler              handler
      │              │                     │
      │         update state         check kind:
      │         node_state           warden_alert?
      │         quorum               → demote to observe
      │                              worker_result?
      │                              → log
      │
      ▼
 oo-host diop-model infer
  --goal "check zone pressure"
  --model diop_warden
      │
      ▼
 Python subprocess:
   TrainedModelAdapter(diop_warden)
   .generate(req)
      │
      ▼  (PyTorch → C FFI → rule fallback)
   GenerationResponse(summary=...)
      │
      ▼
 BusMessage(kind=inference_result,
   payload="model=diop_warden status=ok summary=...")
 → broadcast to OO file bus
      │
      ▼
 oo-bot _handle_inference_result()
   → update diop_last_infer_summary
   → log warden audit result
```

---

## Bus Message Kinds Reference

| Kind | Direction | Purpose | Phase |
|------|-----------|---------|-------|
| `heartbeat` | oo-host → all | Instance alive signal | Core |
| `goal_sync` | oo-host → all | Share active goal | Core |
| `patch_vote` | oo-host ↔ oo-host | Distributed D+ vote | O |
| `dplus_verdict` | kernel → oo-host | D+ gate decision | J |
| `warden_alert` | kernel → oo-host | Sentinel pressure alert | K |
| `uart_event` | oo-host → all | Raw kernel serial event | M |
| `swarm_event` | oo-host → all | Swarm node state change | O/R |
| `diops_event` | oo-host → all | DIOP gateway worker result | T |
| `state_transition` | desktop_display ↔ oo-host | SOMA HUD state change | S |
| `inference_result` | oo-host → all | DIOP trained model output | **X** |
| `diops_model_status` | oo-host → all | DIOP model registry info | **X** |

---

## REPL Command Reference (Phase V)

| Command | What it shows |
|---------|---------------|
| `/dna` | HW DNA fingerprint |
| `/dplus` | D+ policy rules + eval stats |
| `/pressure` | Memory pressure % + pain level |
| `/zones` | Arena A/B/C usage breakdown |
| `/pmu` | CPU temp + stress (PMU MSR) |
| `/splitbrain` | Dual-brain phase + divergences |
| `/kvc` | KV cache snapshot info |
| `/train` | Training journal stats |
| `/hebbian` | Top-10 hot tensors |
| `/soma_state` | **OoNodeState** derived from vitals + peers |
| `/display` | **Full OO HUD** (node, mem, CPU, D+, inference, boot) |
| `/bus_status` | Kernel-side peer count + bus health |
| `/oo_ext_help` | All command list |

---

## Repository Map

```
baremetal/
├── llm-baremetal/              ← MAIN MONOREPO (git)
│   ├── oo-kernel/              ← Bare-metal UEFI kernel (C)
│   │   ├── boot/               ← OoBootCtx, phase init
│   │   ├── repl/               ← REPL command router (Phase V)
│   │   ├── memory/             ← Zone allocator, arenas
│   │   ├── pressure/           ← OoPressureSignal
│   │   └── kvc/                ← KV cache persistence
│   ├── oo-warden/              ← Sentinel + D+ policy (C)
│   ├── engine/                 ← LLM inference (llama2, SSM, GGUF)
│   ├── oo-modules/             ← 12 oo-engines (C)
│   │   ├── ghost-engine/       ← Covert channel (LED timing)
│   │   ├── conscience-engine/  ← Thermal precision control
│   │   ├── evolvion-engine/    ← Driver probe + LLM code gen
│   │   ├── dreamion-engine/    ← Idle memory consolidation
│   │   ├── morphion-engine/    ← HW-adaptive module loading
│   │   ├── symbion-engine/     ← LLM-observed hardware annotation
│   │   ├── synaption-engine/   ← Access-pattern arena repack
│   │   ├── metabion-engine/    ← Sampling metabolism control
│   │   ├── neuralfs-engine/    ← 128-dim vector store
│   │   ├── cellion-engine/     ← Wasm config hot-load
│   │   ├── collectivion-engine/← KV cache swarm broadcast
│   │   └── compatibilion-engine/← Feature negotiation
│   ├── oo-bus/hermes/          ← Hermes kernel bus (C)
│   ├── oo-kernel-rust/         ← Rust FFI bridge to kernel
│   ├── oo-bot/                 ← Autonomous bot agent (Python)
│   │   └── oo_prime/           ← bus_bridge, engine, cli
│   ├── diop/                   ← Distributed AI orchestration (Python)
│   │   ├── adapters/           ← 6 generation backends
│   │   ├── engine/             ← Trained model + trainer
│   │   │   └── model/          ← .pt / .bin checkpoints
│   │   ├── workers/            ← 9 specialized task workers
│   │   ├── core/               ← Orchestrator, planner, dispatcher
│   │   └── memory/             ← Episodic + distilled knowledge
│   ├── djib/                   ← llama.cpp + Djibion dataset
│   │   └── dataset/            ← djibion_base/v2/v3.jsonl (345 samples)
│   ├── oo-model-repo/          ← Training pipeline
│   │   ├── data/engine_training/ ← Domain JSONL datasets
│   │   └── data/processed/     ← mamba3_training.jsonl (365 samples)
│   ├── oo-sim/                 ← Simulation scenarios (Python)
│   ├── oo-lab/                 ← Experimental modules
│   ├── tools/                  ← Build + bench scripts
│   └── tests/                  ← Integration tests (Phase W, Y)
│
├── oo-host/                    ← Rust host runtime (separate repo)
│   └── src/                    ← 40+ modules, 215 tests
│
├── oo-dplus/                   ← D+ policy engine (Rust crate)
├── desktop_display/            ← SOMA HUD UEFI app (C, external)
└── oo-model/                   ← Mamba SSM model (Python)
```

---

## Version Milestones

| Version | Status | Description |
|---------|--------|-------------|
| v0.1 | ✅ Done | Stable kernel: boot phases 1-7, REPL, memory, D+, sentinel |
| v0.2 | ✅ Done | Integrated LLM: GGUF loading, inference, KV cache, splitbrain |
| v0.3 | ✅ Done | Distributed intelligence: oo-host, OO bus, swarm, oo-bot |
| v0.4 | ✅ Done | DIOP integration: gateway bridge, Djibion dataset, LlamaCpp adapter |
| v0.5 | ✅ Done | SOMA HUD: desktop_display, oo_soma_bridge, visual node state |
| v0.6 | ✅ Done | DIOP native model: 3 trained models, inference_result bus event |
| v0.7 | 🚧 Next | QEMU live integration test; oo-model Mamba 2.8B training |
| v1.0 | 🔮 Future | Full autonomous OO: self-modifying, swarm-learning, zero-touch |
