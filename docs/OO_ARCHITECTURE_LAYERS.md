# OO Architecture — Spécification complète

> Source of truth pour l'architecture de l'Operating Organism.
> Toutes les décisions de design majeures sont documentées ici.

---

## Vue d'ensemble

OO est organisé en 7 couches verticales. Chaque couche a une responsabilité unique,
une interface définie, et un repo dédié (ou un sous-dossier de `oo-system`).

```
┌─────────────────────────────────────────────────────────┐
│  7. Interface Layer   CLI · API · Bridge hardware        │
├─────────────────────────────────────────────────────────┤
│  6. Meta Layer        OO modifie OO (reflect+patch)      │
├─────────────────────────────────────────────────────────┤
│  5. Evolution Layer   D+ policy · mutation · adaptation  │
├─────────────────────────────────────────────────────────┤
│  4. Research Layer    Prototypes · expériences           │
├─────────────────────────────────────────────────────────┤
│  3. Simulation Layer  Mondes simulés · agents · test     │
├─────────────────────────────────────────────────────────┤
│  2. Execution Kernel  Agents · scheduling · mémoire      │
├─────────────────────────────────────────────────────────┤
│  1. Cognitive Core    UEFI kernel · LLM · Mamba SSM      │
└─────────────────────────────────────────────────────────┘
          ↕ OO Message Bus (shared/oo-proto)
```

Les couches communiquent uniquement via le **OO Message Bus** (défini dans `shared/oo-proto`).
Aucun appel direct entre couches non-adjacentes.

---

## Couche 1 — Cognitive Core (`llm-baremetal`)

**Rôle :** Cerveau bare-metal de l'organisme.

**Responsabilités :**
- Boot UEFI (firmware, pas d'OS)
- Memory zones (COLD/WARM/HOT)
- LLM inference (llama2, GGUF)
- Mamba SSM inference (O(1) mémoire/token)
- D+ policy gate (sécurité)
- REPL interactif (`llmk>`)
- Journal/persistence des états cognitifs

**Contraintes :**
- Tout le code C est `-ffreestanding` (zéro libc)
- Zéro allocateur dynamique en runtime
- Toutes les décisions passent par D+ sentinel

**Interface exposée :**
- Serial UART → OO Message Bus
- COLD zone : poids LLM + Mamba (binaires GGUF/MAMB)
- Journal : fichiers FAT32 (`oo-journal-*.txt`)

---

## Couche 2 — Execution Kernel (`oo-host`)

**Rôle :** Runtime hôte — exécute les agents, orchestre les décisions.

**Responsabilités :**
- Agent lifecycle (spawn, pause, kill)
- Task graph execution (dépendances entre agents)
- Resource allocation intelligente (pas uniform round-robin)
- Pont avec la couche Cognitive (lit le serial, parse les journaux)
- `oo-bot` : assistant CLI GitHub

**Interface exposée :**
- `oo-host exec <agent>` — lancer un agent
- `oo-host status` — état de tous les agents
- OO Message Bus (socket Unix)

---

## Couche 3 — Simulation Layer (`oo-sim`)

**Rôle :** Bac à sable — tester des comportements avant déploiement réel.

**Responsabilités :**
- Mondes simulés (marché, réseau d'agents, physique légère)
- Entraînement de comportements (reinforcement léger)
- Test de politiques D+ en simulation avant bare-metal
- Génération de datasets pour oo-lab

**Interface exposée :**
- `oo-sim run <world.toml>` — lancer une simulation
- Résultats → OO Message Bus → oo-dplus (feedback)

Boucle hote actuelle :
- [scripts/future-cycle.ps1](scripts/future-cycle.ps1) relie aujourd'hui `oo-dplus`
    + `oo-sim` + `oo-lab` + `oo-host` dans une boucle `policy -> futures -> bench -> feedback -> reports`.

---

## Couche 4 — Research Layer (`oo-lab`)

**Rôle :** Incubateur — prototypes non stabilisés, recherche pure.

**Responsabilités :**
- Expériences non-bloquantes (échec acceptable)
- Nouveaux algorithmes avant intégration dans les couches stables
- Notebooks, scripts, micro-benchmarks

**Interface exposée :**
- Output → `oo-lab/artifacts/` → pipeline vers oo-dplus ou oo-sim

---

## Couche 5 — Evolution Layer (`oo-dplus`)

**Rôle :** Ce qui rend OO vivant — il mute, il adapte, il améliore.

**Responsabilités :**
- D+ policy DSL : règles de décision auditables
- Policy mutation : ajuster les règles selon les résultats
- Module injection : proposer de nouveaux modules
- Merit system : évaluer les agents selon performances
- Warden : garde-fou contre les mutations dangereuses

**Interface exposée :**
- `dplus_check <policy.dplus>` — valider une policy
- `dplus_judge <event.json>` — évaluer un événement
- OO Message Bus → événements de mutation

---

## Couche 6 — Meta Layer (`oo-system/meta`)

**Rôle :** OO modifie OO. La couche la plus critique.

**Responsabilités :**
- `reflect` : lire sa propre structure (repos, fichiers, dépendances)
- `patch` : générer des diffs sur lui-même (contrôlé par D+)
- `evolve` : boucle `simulate → learn → modify → redeploy`
- Toute modification passe par le warden D+

**Contraintes de sécurité :**
- Aucune modification sans approbation D+ (confidence ≥ 0.9)
- Journal immuable de toutes les modifications auto
- Mode `SAFE` : propose seulement, n'applique pas

---

## Couche 7 — Interface Layer (`oo-system/interface`)

**Rôle :** Pont entre OO et le monde réel.

**Responsabilités :**
- CLI `oo` en C : commandes pour interagir avec tout l'écosystème
- API HTTP légère : interroger l'état de OO depuis l'extérieur
- Bridge C : communication série entre bare-metal et host
- UI future : dashboard web minimal

**Commandes CLI cibles :**
```
oo status          # état de tous les repos + CI
oo think <text>    # envoyer une pensée au Cognitive Core
oo journal         # afficher le journal OO
oo evolve propose  # proposer une évolution via D+
oo meta reflect    # afficher la structure de OO
```

En attendant la CLI unifiée, [scripts/future-cycle.ps1](scripts/future-cycle.ps1)
sert de premier "meta reflex" opérable sur Windows pour exécuter une boucle
prospective complète sans toucher au bare-metal.

---

## OO Message Bus

Protocole de communication entre toutes les couches.
Défini dans `shared/oo-proto`.

```
OOMessage {
    version: u8,        // version du protocole
    from:    OOLayer,   // couche source
    to:      OOLayer,   // couche destination (ou BROADCAST)
    kind:    OOEvent,   // type d'événement
    seq:     u64,       // numéro de séquence global
    ts:      u64,       // timestamp (ns depuis boot ou UNIX)
    payload: [u8],      // contenu (JSON ou binaire selon kind)
}

OOLayer  = COGNITIVE | KERNEL | SIM | LAB | EVOLVE | META | INTERFACE
OOEvent  = THINK | ACT | OBSERVE | EVOLVE | PATCH | QUERY | RESPONSE | JOURNAL | ALARM
```

**Transport :**
- Bare-metal : region mémoire WARM (offset fixe, zéro copie)
- Host : socket Unix, pipe nommé, ou pont CLI natif C
- Inter-machine (Phase 5) : TCP + authentification HMAC

---

## Principes fondamentaux

1. **Immunité avant fonctionnalité** — chaque nouvelle feature passe par D+
2. **Zéro dépendance externe en couche 1** — bare-metal = freestanding only
3. **Journaux immuables** — tout événement important est loggé
4. **Interfaces étroites** — les couches communiquent via messages, pas d'appels directs
5. **Évolution contrôlée** — la couche Meta ne peut modifier que ce que D+ approuve

---

Voir les specs détaillées dans `docs/layers/`.
