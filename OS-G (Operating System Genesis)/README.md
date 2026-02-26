# OS-G (Operating System Genesis)

OS-G n’est pas “un OS de plus”. C’est une vision de kernel **gouverneur** : il ne se contente pas d’exécuter, il **juge**.

Dans un OS classique, le kernel agit comme un secrétaire : il obéit à des instructions (“écris à telle adresse”).
Dans OS-G, le kernel devient le **Warden** : il reçoit des **intentions** (“je veux produire X sous contraintes Y”), puis arbitre, transforme, isole, régénère.

Le but de ce dossier est de rendre cette vision **testable** et **progressive** : on garde le futurisme, mais on ancre chaque concept dans des invariants, du code, et des tests.

Ce dossier contient la spec initiale (concept → architecture) et une roadmap de prototype.

Important : ce dossier forme un **kernel unique**. Le crate Rust `osg-memory-warden` est le *noyau OS-G* :
- le **core `no_std`** contient le Warden + D+ (parse/verify/merit/judge) et tourne en UEFI/QEMU,
- les binaires `dplus_*` sont des **outils host** (feature `std`) construits à partir des mêmes sources du kernel.

## Idée centrale : Kernel “Gouverneur” (Warden)
Le Warden est l’organe souverain : il applique la loi fondamentale du système.

Ses responsabilités (version “concrète”, codable) :
- écouter des **intentions** (vœux) émises par des cellules/apps,
- arbitrer (accorder / refuser / limiter / sandboxer),
- fournir des garanties (capabilities, quotas, TTL, snapshots/rollback),
- isoler les fautes et permettre la régénération.

Sa promesse (version “vision”) : passer de l’**instruction** à l’**intention**, et de la RAM comme espace vide à la RAM comme **société** (ressources attribuées selon mérite et risque).

## Architecture horizontale (micro‑cellules)
OS-G vise une architecture “nuage de particules” :
- chaque cœur (ou groupe de cœurs) peut héberger une **micro‑cellule** (mini‑kernel + runtime),
- une cellule est remplaçable ; une panne locale ne doit pas contaminer l’organisme.

Cette idée est décrite dans la spec, et le prototype actuel (Memory Warden) est conçu pour être instanciable par cellule.

## Anatomie OS-G (concept → code)
OS-G se pense comme un organisme :

- **Warden (Souverain)** : kernel-gouverneur et tribunal (implémentation partielle : Memory Warden).
- **D+ (Le Verbe)** : format/“langage d’intention” mosaïque qui peut contenir des milliers de sous‑langages dans un seul artefact (implémentation MVP : parser + verifier + consensus LAW↔PROOF).
- **Spine (Réflexe / barrière)** : couche Rust/hyperviseur qui empêche les gestes physiques dangereux (futur).
- **Cortex (Instinct / ordonnanceur IA)** : ordonnanceur orienté intentions, IA qui propose, lois vérifiées qui disposent (futur).
- **Akasha (Âme / stockage atomique)** : objets immuables + overlays + régénération (futur).
- **Telepathic Link (flotte)** : continuité multi‑machines, délégation de calcul (futur).

Ce dossier commence volontairement par les “os” de l’organisme : mémoire + intentions + vérification.

## Documents
- [SPEC.md](SPEC.md) — architecture globale : cellules, bus d’intentions, lois vérifiables, OS invisible.
- [MEMORY_WARDEN.md](MEMORY_WARDEN.md) — design “concret” du Warden côté mémoire (capabilities, quotas, TTL, sandbox, snapshots).
- [DPLUS.md](DPLUS.md) — D+ MVP : sections taggées (arbitraires) + consensus minimal LAW↔PROOF.
- [ROADMAP.md](ROADMAP.md) — phases de prototypage (ancrer la vision dans des invariants testables).
- [PROTOTYPE.md](PROTOTYPE.md) — comment tester (host + QEMU/UEFI).

## Ce qui est déjà réel (dans ce dossier)
- Un crate Rust `no_std` avec un **Memory Warden** (capabilities, quotas, TTL, zones, délégation, snapshots, journal).
- Un binaire **UEFI/QEMU** qui exécute un smoke-test kernel et affiche `PASS/FAIL`.
- Un MVP **D+** dans le kernel : parseur sans allocation + vérificateur + consensus LAW↔PROOF + méritocratie + juge (et le kernel le fait tourner en UEFI/QEMU).
- Des outils host du kernel (feature `std`) : `dplus_check`, `dplus_weaver`, `dplus_graph`, `dplus_judge`, `dplus_replay`, etc.
- Un exemple D+ polyglotte prêt à valider : `examples/genesis.dplus`.

## Non-objectifs (pour rester réalisable)
- Répliquer Linux immédiatement.
- Dépendre d’un cloud obligatoire.
- Faire de l’IA non-vérifiable dans la boucle critique : l’IA propose, **les lois vérifiées disposent**.
