# OO Repository Guide
## Organisation Justine-Style (Monorepo)

> Philosophie : **Un seul repo, un seul Makefile, zéro dépendances externes, tout compilable depuis zéro.**
>
> Inspiré de : Justine Tunney (llamafile, cosmopolitan libc) — "monorepo radical, self-contained, no packaging hell"

---

## Structure Officielle du Monorepo `llm-baremetal`

```
llm-baremetal/
│
├── engine/                    ← Moteurs d'inférence (C, freestanding)
│   ├── llama2/                ← God file C + splits progressifs
│   │   ├── llama2_efi_final.c ← God file (en cours de split)
│   │   ├── soma_mind.c        ← [SPLIT] llmk_mind_* — politique + halting
│   │   ├── soma_loader.c      ← [SPLIT] chargement modèles OOSS/GGUF/Mamba
│   │   ├── soma_inference.c   ← [SPLIT] boucle d'inférence + toutes les phases
│   │   ├── soma_repl.c        ← [SPLIT] REPL + commandes /cmd
│   │   └── soma_boot.c        ← [SPLIT] EFI_MAIN + init mémoire
│   ├── djiblas/               ← BLAS custom (AVX2, matmul)
│   ├── gguf/                  ← GGUF loader
│   └── ssm/                   ← Mamba SSM bare-metal
│
├── OS-G (Operating System Genesis)/  ← Rust kernel (no_std, UEFI)
│   └── src/                   ← Warden + D+ + Sentinel + NeuralSoma (Rust)
│
├── oo-kernel-rust/            ← Nouveau Rust god file (phases A-Z en Rust)
│   └── src/                   ← Parallèle au C, plus sûr, plus avancé
│
├── olympe/                    ← Hermes bus polyglot + pillar tests
│   └── core_hermes/           ← hermes_bus.c/h — IPC bus C
│
├── oo-sim/                    ← Simulateur QEMU (scripts, scénarios)
│   ├── core/                  ← Scripts de boot QEMU
│   └── scenarios/             ← Scénarios de test automatisés
│
├── oo-lab/                    ← Laboratoire d'entraînement Python
│   ├── notebooks/             ← Jupyter, expériences
│   ├── datasets/              ← Datasets générés (plans, math, soma)
│   └── models/                ← Checkpoints PyTorch
│
├── oo-dplus/                  ← D+ Policy System (standalone)
│   ├── core/                  ← D+ VM, parser, validator en C
│   └── tools/                 ← dplus_check, dplus_weaver (wrappers Rust)
│
├── oo-hal/                    ← Hardware Abstraction Layer
├── oo-crypto/                 ← Cryptographie freestanding
├── oo-multicore/              ← SMP bare-metal
├── oo-ipc/                    ← IPC + bridge Syrin
├── oo-net/                    ← Stack réseau bare-metal
├── oo-storage/                ← FAT32/NVMe bare-metal
├── oo-metadriver/             ← Drivers générés par LLM
│
├── *-engine/                  ← Moteurs cognitifs (limbion, chronion, etc.)
│   └── core/                  ← engine.h + engine.c par moteur
│
├── oo-model-repo/             ← Pipeline d'entraînement Python (oo-model)
│   ├── scripts/               ← train_*.py, generate_*.py
│   ├── soma_dataset/          ← Datasets JSONL générés
│   └── models/                ← Binaires OOSS exportés
│
├── oo-guard/ rust-guard/      ← Safety layer Rust (warden mémoire)
├── oo-bus/                    ← Bus Hermes (objets compilés)
├── oo-modules/                ← Registre des modules
├── oo-warden/                 ← Warden C (sentinel, D+)
│
├── tools/                     ← Scripts de build + outils dev
├── tests/                     ← Tests automatisés QEMU
├── docs/                      ← Architecture + contrats d'interface
│
├── Makefile                   ← UN SEUL Makefile, construit tout
├── build.ps1                  ← Windows PowerShell build wrapper
└── README.md                  ← README principal

```

---

## Règles Justine-Style

### 1. Monorepo total
Tout ce qui touche au kernel bare-metal **reste dans ce repo**.
- `oo-sim/` ← **intégré** (scripts QEMU sont des tests du kernel)
- `oo-lab/` ← **intégré** (datasets et modèles sont des artifacts du kernel)
- `oo-dplus/` ← **intégré** (D+ est une composante du kernel)
- `oo-host/` (Batterfyl/Syrin) ← **repo séparé** (équipe externe, Python, cloud)
- `llm-baremetal` (HuggingFace) ← **séparé** (modèles binaires, trop lourds pour git)

### 2. Un seul Makefile
```makefile
make all        # tout compiler (C + Rust + tests)
make kernel     # juste le kernel C
make osg        # juste OS-G Rust
make sim        # boot QEMU
make test       # tests automatisés
make lab        # entraînement Python
make clean      # nettoyage total
```

### 3. Zéro dépendances externes
- Pas de npm, pas de pip dans le build principal
- Les deps Python (`torch`, etc.) sont isolées dans `oo-lab/`
- Le kernel C compile avec `gcc -ffreestanding` uniquement
- Le kernel Rust compile avec `cargo build --target x86_64-unknown-uefi`

### 4. Headers séparés des sources
Chaque sous-système expose une interface `.h` propre :
```
oo-crypto/core/oo_crypto.h      ← interface publique
oo-crypto/core/oo_crypto.c      ← implémentation
```
Jamais d'`#include "../../other_system/internal.h"` — les interfaces sont explicites.

### 5. Un README par sous-système
Chaque dossier `*-engine/` et `oo-*/` doit avoir un `README.md` minimal :
- Rôle du sous-système (1 phrase)
- Fichiers clés
- API principale (5-10 fonctions)
- Dépendances

---

## Repos Séparés (ce qui SORT du monorepo)

| Repo | Raison | URL |
|------|--------|-----|
| `oo-host` (Syrin/Batterfyl) | Équipe externe, Python/cloud, cycle release différent | github.com/Batterfyl/syrin |
| `llm-baremetal` (HuggingFace) | Modèles binaires (>1GB), incompatibles avec git | huggingface.co/djibydiop/llm-baremetal |

---

## Convention de Nommage

| Catégorie | Préfixe | Exemple |
|-----------|---------|---------|
| Kernel subsystem | `oo-` | `oo-hal`, `oo-crypto` |
| Cognitive engine | `*ion-engine` | `limbion-engine`, `chronion-engine` |
| Rust kernel | `oo-kernel-rust` | `oo-kernel-rust/src/` |
| Python lab | `oo-lab` | `oo-lab/notebooks/` |
| Simulator | `oo-sim` | `oo-sim/scenarios/` |
| Tools | `tools/` | `tools/make_boot_gif.py` |

---

## Roadmap d'intégration

```
Phase actuelle:  God file C (25K lignes) + modules compilés séparément
→ Court terme:   God file splité en 5 modules C
→ Moyen terme:   OS-G Rust intégré au Makefile, compile en parallèle
→ Long terme:    Rust remplace progressivement C (sécurité mémoire)
```

*Guide maintenu par l'équipe OO. Dernière mise à jour: 2026-04-08.*
