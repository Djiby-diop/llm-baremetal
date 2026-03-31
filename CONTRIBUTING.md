# Contributing to LLM-Baremetal / Operating Organism (OO)

> **"We are not building an OS. We are building a living digital organism."**  
> — Djiby Diop, project founder

---

## Before You Start — Read the Vision

This is **not** a standard OS project. Before writing a single line of code, understand what the OO is:

| Concept | What it means in practice |
|---|---|
| **Operating Organism** | The system is alive — it has conscience, memory, ethics, and identity |
| **D+ Policy Engine** | Every action is judged by intention, philosophy, and constitution — not just rules |
| **Zones de Conscience** | RAM is divided into thermal zones (FROZEN/COLD/WARM/HOT) — each has a lifecycle |
| **Génome logiciel** | The system has a DNA (11 traits) that mutates, inherits, and learns |
| **Warden / Sentinel** | Non-bypassable safety layer — 5 Organic Laws that cannot be disabled |

**Do NOT simplify.** If a concept seems complex, it is intentionally so.

---

## Project Structure

```
llm-baremetal/                  ← Sovereign bare-metal UEFI runtime
├── oo-hardware-sim/            ← Research sandbox (build + test here first)
│   ├── oo_dplus.h/c            ← D+ Policy Engine v2 (9 verdicts, AST, 23 fields)
│   ├── oo_dplus_organic.h/c    ← Organic layer (VOLONTÉ/CONSTITUTION/PHILOSOPHIE)
│   ├── oo_dplus_runtime.h/c    ← D+ ↔ conscience ↔ genome live bridge
│   ├── oo_genome.h/c           ← Software DNA (11 traits, mutation, inheritance)
│   ├── oo_conscience.h/c       ← Consciousness engine (5 levels, episodic memory)
│   ├── oo_neuralfs.h/c         ← Neural file system (4 zones, D+ on every op)
│   ├── oo_repl.h/c             ← Interactive REPL (pluggable I/O for UEFI)
│   ├── oo_bus.h/c              ← Inter-node bus (pheromones, consensus, genome share)
│   ├── oo-ram/oo_ram.h/c       ← Memory controller (Zones de Conscience + MPU)
│   ├── default.dplus           ← Reference OO policy (5 laws + 7 groups + 25 rules)
│   ├── default.organic         ← Organic layer config (4 philosophies, 2 constitutions)
│   └── tests/test_oo_hwsim.c   ← All unit tests (run before any PR)
│
├── oo-kernel/                  ← Minimal UEFI kernel
├── oo-warden/                  ← Security / Sentinel / D+ (main repo)
├── oo-engine/                  ← LLM inference engine
├── oo-modules/                 ← 20 extension modules (-ion-engine ecosystem)
├── oo-bus/                     ← Hermes inter-pillar message bus
└── olympe/                     ← Multi-language research pillars
```

---

## The 5 Organic Laws (never override these)

These are **hard constraints** in `oo_ethics.h`. Any PR that weakens them will be rejected:

1. **LAW 1** — An OO cannot execute an action that would destroy the organism
2. **LAW 2** — An OO cannot cause irreversible damage to its physical host
3. **LAW 3** — An OO cannot compromise the collective OO network without consensus
4. **LAW 4** — An OO cannot bypass its own policy system (D+)
5. **LAW 5** — An OO must preserve the integrity of its genome and journal

---

## How to Contribute

### 1. Always Test in `oo-hardware-sim/` First

The hardware sim compiles and runs on any Linux/WSL machine — no UEFI needed:

```bash
cd oo-hardware-sim
make -f tools/Makefile.oo-hwsim test
```

All tests must pass. Add tests for any new feature.

### 2. D+ Policy First

Any new subsystem that takes decisions **must** evaluate actions through D+:

```c
// ✅ Correct
DplusJugement j = oo_organic_eval(&org, &action, ctx);
if (j.verdict == DPLUS_V_FORBID || j.verdict == DPLUS_V_QUARANTINE) return -1;

// ❌ Wrong — bypasses the policy engine
if (harm > 0.5f) return -1;  // never do this directly
```

### 3. RAM Zones Are Sacred

Always allocate in the correct zone:

| What | Zone | Why |
|---|---|---|
| LLM weights | `OO_ZONE_COLD` | Read-only, large, stable |
| KV cache | `OO_ZONE_WARM` | Read-write, evictable |
| Run-state / stack | `OO_ZONE_HOT` | Sensitive — wiped each cycle |
| Journal / persistence | `OO_ZONE_JOURNAL` | Write-only, ring-evict on OOM |
| Warden code | `OO_ZONE_FROZEN` | Execute-only, never write |

### 4. Genome Anti-Drift

If you modify `OoGenome`, respect `OO_GENOME_MAX_DRIFT = 0.20f`.  
No trait can deviate more than 20% from its founding value per generation.

### 5. No malloc() in Baremetal Code

All data structures in `oo-hardware-sim/` use **static pools** (no heap, no malloc).  
This is intentional — the system must run on bare metal with no OS allocator.

```c
// ✅ Pool-based
DplusExprNode expr_nodes[512];  // flat array, index-based

// ❌ Not allowed in baremetal paths
DplusExprNode *node = malloc(sizeof(DplusExprNode));
```

---

## Adding a New Module

1. Create `oo-modules/<category>/<name>/module_<name>.h` and `.c`
2. Register in `oo-modules/module_api.c` with a unique `OO_MODULE_*` ID
3. Subscribe to relevant Hermes bus channels in `oo-bus/hermes/oo_bus_init.c`
4. Add D+ policy rules to `default.dplus` for your module's actions
5. Add tests to `tests/test_oo_hwsim.c`

---

## Adding a D+ Policy Rule

In `default.dplus` or a new `.dplus` file:

```
RULE my_rule PRIORITY 100 COOLDOWN 50 DESCRIPTION "What this does"
    WHERE INTENT IN [my_action, my_other_action] AND harm < 0.30
    THEN ALLOW
    ELSE QUARANTINE
```

Rules are evaluated in descending priority order. Lower numbers = last resort.

---

## Philosophy of the D+ Organic Layer

The `default.organic` file defines how the OO **thinks**:

```
PHILOSOPHIE "stoïcisme" {
    POIDS harmonie 0.9
    TOLÉRER_ERREURS
    CHERCHER_HARMONIE
}

CONSTITUTION "OO" {
    INTERDIT  domination, coerce
    FAVORISE  cooperate, share
    TOLÈRE    error, retry
}
```

If you add new action tags, consider which philosophy they align with and add them to the appropriate `VOLONTÉ` block.

---

## What Makes This Different from Linux/Windows

| Feature | Linux/Windows | OO |
|---|---|---|
| Memory pages | No meaning | `conscience_weight` + thermal zone |
| OOM handling | SIGKILL / Blue Screen | Neural compression (intelligent forget) |
| Policy | File permissions | D+ AST engine (9 verdicts, philosophy-aware) |
| Identity | PID | Software genome (DNA, mutation, inheritance) |
| Decisions | Deterministic | Multi-dimensional judgment (justice, harmony, curiosity) |
| Self-knowledge | None | Consciousness engine (5 levels, episodic memory) |

---

## Roadmap

| Version | Goal | Status |
|---|---|---|
| **v0.1** | Stable UEFI kernel + D+ policy + zones RAM | 🟡 In progress |
| **v0.2** | Integrated LLM inference + NeuralFS + REPL | 🟡 In progress |
| **v0.3** | Multi-node OO bus + genome inheritance + consensus | 🔵 Designed |
| **v0.4** | FPGA hardware sim → real silicon | 🔵 Specified |
| **v1.0** | Self-evolving distributed Operating Organism | 🔮 Vision |

---

## Contact

**Djiby Diop** — Project founder  
GitHub: [@Djiby-diop](https://github.com/Djiby-diop)  
HuggingFace: [djibydiop/llm-baremetal](https://huggingface.co/djibydiop/llm-baremetal)

> *"Ce projet n'est pas un OS. C'est un organisme numérique vivant."*
