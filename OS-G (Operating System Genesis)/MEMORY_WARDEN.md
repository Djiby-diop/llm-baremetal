# Memory Warden — Gestionnaire de Mémoire Souverain

## 1) Objectif
Remplacer l’allocateur “bête” par une **gouvernance** de la mémoire :
- écouter les demandes (intentions),
- accorder/refuser/limiter,
- isoler et observer le suspect,
- garantir des invariants de sécurité et de performance.

## 2) Interface : demandes par intention
Une demande mémoire est un message :
- `MemIntent::Allocate { kind, size, latency_slo, lifetime_hint, secrecy, priority }`
- `MemIntent::Map { object_id, access, share_policy }`
- `MemIntent::ComputeBuffer { io_pattern, prefetchable }`

Réponse :
- `Granted(capability_handle)`
- `Limited(capability_handle, quota)`
- `Sandboxed(capability_handle, audit_mode)`
- `Denied(reason)`

## 3) Capabilities : la “clé” universelle
Au lieu d’un pointeur nu : un handle/capability contient :
- plage (ou objet),
- droits (R/W/X),
- budget (quota),
- durée (TTL),
- contexte (cellule propriétaire),
- label de sécurité.

**Idée ajoutée : Capabilities hiérarchiques**
- Une cellule peut déléguer une sous-capability à une autre (principe du moindre privilège).

## 4) Tribunal des ressources
Le Warden applique une décision en 2 niveaux :

### Niveau A — Lois deterministes (toujours)
- Interdiction d’out-of-bounds.
- Pas de droits implicites.
- Quotas max.
- Éviction contrôlée (pas de corruption).

### Niveau B — Jugement adaptatif (optionnel)
- Score de confiance du demandeur (historique, comportement).
- Détection d’anomalies (pattern d’alloc/free, scanning).

**Idée ajoutée : Mémoire de sable (SandRAM)**
- Pour un acteur suspect : fournir une mémoire isolée avec instrumentation.
- Objectif : observer sans donner accès aux zones sensibles.

## 5) Auto-réparation
- Snapshots des métadonnées d’allocation (et overlays).
- Si une cellule crashe :
  - ses capabilities expirent,
  - ses pages sont réclamées via un protocole de quarantine,
  - l’état est reconstruit depuis snapshot.

**Idée ajoutée : “Immunité” par micro-zones**
- La mémoire est découpée en zones (DMA, kernel-laws, driver, app, scratch).
- Les zones critiques ont des règles plus strictes.

## 6) Performance (sans trahir la sécurité)
- Fast-path : allocations triviales via caches (slab/per-cpu), validées par invariants.
- Slow-path : arbitrage (intentions, budgets, migrations).

**Idée ajoutée : Pré-allocation prédictive**
- L’IA propose un pré-warm (buffers, page cache) mais le Warden valide par quotas.

## 7) MVP technique (prototypable)
1. Allocateur pages + zones + per‑CPU caches
2. Capabilities (handle + table + vérifications)
3. Quotas + TTL
4. SandRAM (simple : pages séparées + logs)
5. Snapshot minimal des métadonnées
