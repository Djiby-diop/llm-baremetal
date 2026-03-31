---
name: "🔧 New Module"
about: "Proposer un nouveau module OO (driver, extension, pilier)"
title: "[MODULE] "
labels: ["new-module"]
---

## Nom du module
`oo_<nom>` — 

## Catégorie
- [ ] Driver hardware (`oo-drivers/`)
- [ ] Extension cognitive (`oo_*.h/c`)
- [ ] Module -ion (`oo-modules/`)
- [ ] Pilier Olympe (`olympe/pillars/`)
- [ ] Autre

## Description
<!-- En une phrase : que fait ce module ? -->

## Checklist obligatoire (CONTRIBUTING.md)

### D+ Policy
- [ ] Toutes les actions passent par `oo_organic_eval()`
- [ ] Règles D+ ajoutées dans `default.dplus`

### Mémoire
- [ ] Allocation dans la bonne zone (`OO_ZONE_*`)
- [ ] Pas de `malloc()` — pool statique
- [ ] Freestanding compatible

### Tests
- [ ] `test_<nom>()` ajouté dans `test_oo_hwsim.c`
- [ ] Tous les tests passent : `make -f tools/Makefile.oo-hwsim test`

### Architecture
- [ ] Respecte les interfaces de `ARCHITECTURE.md`
- [ ] Ne viole aucune des 5 Lois Organiques
- [ ] Convention de nommage : `oo_module_fonction()` / `OO_MACRO`

## Interface publique proposée
```c
// oo_<nom>.h
int  oo_<nom>_init(...);
void oo_<nom>_tick(...);
void oo_<nom>_shutdown(...);
```

## Zone mémoire utilisée
- [ ] FROZEN (immuable — Warden only)
- [ ] COLD (poids/données read-only)
- [ ] WARM (état de travail évictable)
- [ ] HOT (conscience/stack — effacé chaque cycle)
- [ ] JOURNAL (persistance)
