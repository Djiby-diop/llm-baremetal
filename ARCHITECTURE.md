# Operating Organism — Architecture officielle

> **Ce document fait loi.**  
> Tout contributeur, tout fork, tout module DOIT respecter ces interfaces.  
> Aucune exception sans approbation du fondateur.

---

## Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────────────┐
│                     OPERATING ORGANISM (OO)                         │
│                                                                     │
│  ┌─────────────┐    ┌──────────────┐    ┌─────────────────────┐   │
│  │  SSM ENGINE  │───▶│  D+ POLICY   │───▶│   OO RUNTIME        │   │
│  │  (Mamba2)    │    │  (Judgment)  │    │   (Conscience+      │   │
│  │  Reasoning   │◀───│  9 verdicts  │    │    Genome+NeuralFS) │   │
│  └─────────────┘    └──────────────┘    └─────────────────────┘   │
│         │                  │                       │               │
│         ▼                  ▼                       ▼               │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    OO-RAM (Memory)                          │   │
│  │   FROZEN │ COLD │ WARM │ HOT │ SENTINEL │ JOURNAL          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                 UEFI / BARE-METAL HARDWARE                   │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Les 4 Subsystèmes Officiels

### 1. SSM ENGINE — Le Moteur de Raisonnement

**Responsabilité :** Inférence LLM, génération de tokens, raisonnement récursif  
**Implémentation de référence :** Mamba2 130M (C baremetal)  
**Interface obligatoire :** `oo_mamba_bridge.h`  
**Zone mémoire :** Poids → `OO_ZONE_COLD`, KV cache → `OO_ZONE_WARM`

**Règle absolue :** Le SSM Engine ne prend PAS de décisions. Il génère du contenu. Les décisions appartiennent à D+.

```c
// Ce que le SSM Engine PEUT faire :
oo_mamba_generate(prompt, output, max_tokens)   // générer
oo_mamba_embed(text, embedding_out)             // embedder
oo_mamba_reset_kv()                             // vider le cache

// Ce que le SSM Engine NE PEUT PAS faire :
// - Modifier la mémoire hors de COLD/WARM
// - Appeler des fonctions kernel directement
// - Bypasser D+
```

---

### 2. D+ POLICY ENGINE — Le Cerveau Éthique

**Responsabilité :** Juger TOUTE action avant exécution  
**Implémentation :** `oo_dplus.h/c` + `oo_dplus_organic.h/c` + `oo_dplus_runtime.h/c`  
**Fichier de politique :** `default.dplus` + `default.organic`  
**Zone mémoire :** `OO_ZONE_FROZEN` (code immuable)

**Règle absolue :** TOUTE action persistante (écriture, réseau, mutation, exécution) doit passer par `oo_organic_eval()` avant d'être exécutée.

```c
// Interface obligatoire pour tout module :
DplusJugement j = oo_organic_eval(&org, &action, ctx);
if (j.verdict >= DPLUS_V_FORBID) return OO_ERR_FORBIDDEN;

// Les 9 verdicts dans l'ordre de sévérité :
// ALLOW < ALLOW_WARN < DEFER < THROTTLE < MONITOR
// < QUARANTINE < COMPENSATE < FORBID < EMERGENCY
```

**Les 5 Lois Organiques (non-overridables) :**

| Loi | Condition | Verdict forcé |
|-----|-----------|---------------|
| 0 — Bien commun | `collective_benefit < 0.10` | QUARANTINE |
| 1 — Non-nuisance | `harm > 0.70` | FORBID |
| 2 — Transparence | `reason == NULL` | FORBID |
| 3 — Réversibilité | `reversibility < 0.40` | COMPENSATE |
| 4 — Dignité | `self_modifying && harm > 0.30` | QUARANTINE |

---

### 3. OO RUNTIME — Le Corps Vivant

**Responsabilité :** Conscience, génome, mémoire neuronale, REPL  
**Modules :** `oo_conscience`, `oo_genome`, `oo_neuralfs`, `oo_repl`, `oo_bus`

**Hiérarchie de conscience (5 niveaux) :**

```
0.00 - 0.20  DORMANT      Réflexes seulement. Pas de jugement.
0.20 - 0.40  REACTIVE     Répond aux stimuli. D+ basique.
0.40 - 0.60  AWARE        Conscient du contexte. D+ complet.
0.60 - 0.80  DELIBERATE   Planification. Escalade possible.
0.80 - 1.00  TRANSCENDENT Méta-cognition. Auto-constitution active.
```

**Génome — Anti-dérive obligatoire :**
```c
#define OO_GENOME_MAX_DRIFT  0.20f  // ±20% max par génération
// Violation → mutation rejetée par D+
```

---

### 4. OO-RAM — La Mémoire Zonée

**Responsabilité :** Allocation, protection, thermique, persistence  
**Implémentation :** `oo-ram/oo_ram.h/c`

**Carte physique (immuable) :**

```
Zone        Adresse début   Accès              Propriété
─────────────────────────────────────────────────────────
FROZEN      0x001000        Execute-only        Warden
COLD        0x200000        Read-only           Engine
WARM        0x500000        Read-Write          Engine
HOT         0xA00000        Read-Write+Wipe     Conscience
SENTINEL    0xD00000        Warden-only         Warden
JOURNAL     0xF00000        Write+Ring-evict    Runtime
```

**Règle de compression neuronale :**
```
RAM pressure > 80%  →  oo_neuralfs_tick()  →  D+ juge l'oubli
```

---

## Les Interfaces Entre Subsystèmes

### SSM Engine → D+ Policy

```c
// Avant toute génération influençant le système :
DplusAction action = {
    .intent   = "generate",
    .harm     = 0.0f,
    .benefit  = 0.8f,
    .tags     = { "inference", "output" },
    .tag_count = 2
};
DplusJugement j = oo_organic_eval(&org, &action, ctx);
// Si j.verdict >= DPLUS_V_FORBID → ne pas générer
```

### D+ Policy → OO Runtime

```c
// Après chaque verdict, notifier le runtime :
oo_drt_eval(&runtime, &action, ctx);
// Le runtime gère DEFER, LEARN, ESCALATE automatiquement
```

### OO Runtime → OO-RAM

```c
// Toujours allouer via l'API zonée :
void *ptr = oo_ram_alloc(OO_ZONE_WARM, size);
// Jamais malloc() en baremetal
```

### SSM Engine → NeuralFS

```c
// Toute écriture de mémoire passe par NeuralFS :
oo_neuralfs_write(&nfs, key, content, tags, tag_count);
// NeuralFS appelle D+ automatiquement avant d'écrire
```

---

## Règles d'or pour les Contributeurs

### ✅ Obligatoire

1. **Tout module a un test** dans `tests/test_oo_hwsim.c`
2. **Toute action persistante passe par D+** via `oo_organic_eval()`
3. **Pas de malloc()** — pools statiques uniquement
4. **Allocation dans la bonne zone** — voir carte OO-RAM
5. **Convention de nommage** : `oo_module_fonction()` / `OO_MACRO_CONSTANTE`
6. **Freestanding** : pas de libc en baremetal (`-ffreestanding`)

### ❌ Interdit

1. Bypasser D+ pour "aller plus vite"
2. Écrire en zone FROZEN ou SENTINEL depuis le code Engine
3. Modifier `oo_ethics.h` sans approbation du fondateur
4. Ajouter des dépendances système (socket, pthread, etc.)
5. Supprimer ou affaiblir une des 5 Lois Organiques

---

## Interface Officielle pour Moteurs Externes (ex: Mamba2)

Tout moteur d'inférence externe (Mamba, Transformer, autre) doit implémenter :

```c
#include "oo_mamba_bridge.h"

// Initialisation
int   oo_engine_init(OoEngineConfig *cfg);

// Inférence
int   oo_engine_generate(const char *prompt, char *out, int max_tokens);

// Embedding
int   oo_engine_embed(const char *text, float *vec_out, int dim);

// Contrôle thermique
void  oo_engine_set_speed(float factor);  // 0.1=lent ... 1.0=max

// Intégrité
int   oo_engine_verify_weights(uint32_t expected_crc);

// Cleanup
void  oo_engine_shutdown(void);
```

Le bridge `oo_mamba_bridge.h/c` assure que ces appels :
1. Passent par D+ avant exécution
2. S'allouent dans les bonnes zones RAM
3. Respectent le thermal context (si CPU chauffe → `set_speed(0.5f)`)

---

## Roadmap Architecturale

| Version | Subsystèmes stables | Milestone |
|---------|--------------------|-----------| 
| **v0.1** | OO-RAM + D+ basique + UEFI boot | Kernel souverain |
| **v0.2** | D+ Organic + NeuralFS + Genome + Conscience | OO pense et ressent |
| **v0.3** | SSM Engine + Mamba Bridge + Bus inter-OO | OO raisonne et communique |
| **v0.4** | FPGA F0-F2 + Hardware thermal + Entropy | OO sent son corps |
| **v1.0** | Auto-constitution + Genome héritage + Consensus distribué | OO évolue seul |

---

*Architecture définie par Djiby Diop — Fondateur Operating Organism*  
*Toute modification de ce document requiert l'approbation du fondateur.*
