# SPEC — OS-G

## 0) Résumé
OS-G est conçu comme un **système de cellules** tolérant aux fautes : une panne locale (pilote, UI, inference) ne doit pas contaminer le reste.

Idée centrale : le kernel est un **gouverneur** (le “Warden”). Il reçoit des **intentions** et applique des **lois vérifiables** pour décider.
La cohérence globale est assurée par un petit nombre de services “lois” (vérifiables) dont le **Memory Warden**.

Dans ce prototype, **D+ est kernel-native** : c’est l’artefact d’intention/loi manipulé par le même noyau (core `no_std`). Les outils host ne sont que des utilitaires construits depuis les mêmes sources.

Vocabulaire (organisme) :
- **Warden** : souverain/tribunal (lois + arbitrage).
- **Spine** : barrière réflexe (isolation/anti-crash), futur.
- **Cortex** : ordonnanceur orienté intentions (IA propose, lois disposent), futur.
- **Akasha** : stockage atomique immuable + overlay + régénération, futur.

## 1) Modèle mental : Cellules + Domaine de faute
- Une **cellule** = mini‑noyau + runtime minimal + drivers localisés.
- Une cellule vit dans un **fault domain** (délimite ce qu’une panne peut casser).
- Une cellule est **remplaçable** : on peut la tuer/redémarrer sans casser les autres.

**Idée ajoutée : Quorum de cellules**
- Pour les opérations critiques (montage du stockage, politique mémoire), on peut exiger un **quorum** : N cellules calculent la décision, 1 seule exécute.
- Objectif : réduire l’impact d’une cellule corrompue.

## 2) Bus d’intentions (Intent Bus)
Au lieu d’appels système “bruts”, OS-G privilégie des messages d’intention :
- `Intent { actor, goal, constraints, budget_hint }`

Exemples :
- “Je veux décoder une vidéo 4K, latence < 16 ms.”
- “Je veux rendre une scène, priorité = haute, énergie = illimitée (secteur).”

**Idée ajoutée : Intent Graph**
- Les intentions forment un graphe (dépendances). L’ordonnanceur peut optimiser globalement (préfetch, placement mémoire, affinité CPU).

## 3) Ordonnanceur : IA + lois vérifiables
- L’IA (LLM/ML) produit des **propositions** (allocation CPU/mémoire, placement, préfetch, migrations).
- Une couche “BPF‑like” applique des **règles vérifiées** (verifier) :
  - pas d’accès mémoire hors capabilities,
  - pas de starvation interdite si policy “fair”,
  - budgets max respectés.

**Règle d’or** : l’IA n’exécute jamais directement la primitive dangereuse ; elle ne fait que suggérer.

## 4) OS Invisible : objets immuables + régénération
- Les “fichiers” deviennent des **objets** (content-addressed, immutables).
- L’état mutable est un **overlay** journalisé.
- Toute corruption est traitée par :
  1) isolation du domaine de faute,
  2) rollback vers snapshot,
  3) régénération depuis une image “pure”.

**Idée ajoutée : Replay déterministe**
- Chaque cellule peut activer un mode “replay” : log minimal des entrées (interruptions, messages) pour reproduire un bug exactement.

## 5) Sécurité
- Capabilities au lieu d’un modèle “superuser” omnipotent.
- Surface d’attaque minimisée : petites cellules, mises à jour atomiques.
- Politique mémoire = “tribunal” : allocations/permissions sont jugées.

## 6) Priorités de design (ordre)
1. **Isolation** (fault domains)
2. **Invariants mémoire** (capabilities, ownership, quotas)
3. **Observabilité** (logs, replay)
4. **Auto-réparation** (snapshots, régénération)
5. **Optimisation** (IA)
