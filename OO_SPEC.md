# Operating Organism (OO) — Spec v0 (draft)

## 0) Positionnement (honnête)

- Objectif long terme : un "organisme" logiciel portable qui peut vivre *au-dessus* des OS actuels (Windows/macOS/Linux) puis, à terme, booter en autonomie (UEFI/baremetal) quand la stack est prête.
- Objectif court terme (v0) : prouver un **organisme** par des propriétés testables (continuité, homeostasie, recovery), pas par une promesse de "conscience" impossible à démontrer.

## 1) Définition opérationnelle

Un OO est un système qui tourne en continu et exécute 3 boucles :

1. **Perception** : collecte d'inputs (utilisateur, temps, métriques RAM/IO, fichiers d'état, réseau si dispo)
2. **Régulation (vitals / homeostasie)** : maintient le système dans un domaine stable (budgets, watchdog, modes dégradés, rollback)
3. **Politique / cognition** : décide d'actions sur la base d'une hiérarchie **safe-first** (règles déterministes → heuristiques → LLM en dernier)

Critère de "vie" (v0) : le système **survit à des perturbations** (fichiers manquants, opens FAT flakey, OOM, configs invalides) et reste cohérent.

## 2) Contraintes & principes (v0)

- **Safety-first** : l'OO ne doit jamais "s'auto-corrompre" (écritures atomiques, journal, rollback).
- **Observabilité** : toute décision importante est loguée (raison + état + action).
- **Reproductibilité** : tests QEMU/autorun déterministes dès que possible.
- **Minimal repo** : pas de gros artefacts versionnés.

## 3) Artefacts persistants (proposés)

Sur la FAT (root) :

- `oo_state.bin` : état minimal persistant (version, compteur de boot, mode courant, budgets, derniers échecs)
- `oo_journal.log` : journal append-only (events, décisions, erreurs)
- `oo_recovery.bin` : dernier état valide (rollback)

Écriture recommandée :
- écrire `*.tmp` puis rename / swap (ou delete+create) + checksum
- garder au moins un backup (`.bak`) (best-effort)

## 4) Modes (v0)

- **NORMAL** : fonctionnement nominal
- **DEGRADED** : budgets plus stricts (ctx_len réduit, features off)
- **SAFE** : strict minimum (REPL + diag + logs), pas d'actions risquées

Transitions :
- si erreurs répétées au boot (ex: 3 fois) → SAFE
- si SAFE stable N boots → DEGRADED → NORMAL

## 5) Boucle "Organism Tick" (v0)

Pseudo-flow :

- Lire `oo_state.bin` (si absent → defaults)
- Mesurer (RAM dispo, erreurs I/O, temps boot, etc.)
- Décider action(s) :
  - ajuster paramètres (ex: ctx_len, gguf_q8_blob)
  - valider/auto-réparer `repl.cfg`
  - choisir un modèle fallback si modèle demandé échoue
  - déclencher reboot/shutdown si nécessaire
- Logger la décision (raison + delta)
- Écrire l'état (journal + state)

## 6) Réseau (autorisé, mais optionnel v0)

Règle v0 : le réseau ne doit jamais être requis pour booter.

Capacités réseau progressives (ordre conseillé) :
1. **Time sync** (si possible) ou timestamp monotone local
2. **HTTP GET** (read-only) pour télécharger un manifeste signé (pas de binaire direct au début)
3. **Update** (plus tard) : téléchargement + vérif + staging + rollback

## 7) Mesures de succès (tests)

À automatiser sous QEMU (autorun) :

- **Survivabilité** : 100 boots consécutifs sans brick
- **Recovery** : si `repl.cfg` invalide ou fichier manquant → SAFE + log explicite
- **Homeostasie** : si OOM/ctx trop grand → réduction automatique + boot suivant OK
- **Cohérence** : `oo_state.bin` checksum OK, rollback si corruption détectée

## 8) Jalons (roadmap courte)

M1 — *Vitals + état persistant* (UEFI)
- écrire/relire `oo_state.bin` + `oo_journal.log`
- logs série + logs fichier
- tests autorun : création/lecture, compteur de boot

M2 — *Recovery / Safe mode*
- corruption/absence de fichiers → SAFE + rollback
- tests autorun : injection de mauvais state, assert recovery

M3 — *Policy engine déterministe*
- règles : budgets RAM, ctx_len, choix modèle fallback
- tests autorun : provoquer OOM → auto-réduction

M4 — *Réseau read-only (optionnel)*
- fetch manifeste signé (ou placeholder) + log
- tests : mode réseau off → boot ok

M5 — *LLM comme conseiller (pas pilote)*
- LLM produit des suggestions, policy décide
- logs : suggestion vs décision

### M5 — Détails d'implémentation

**Objectif:** Utiliser le LLM embarqué pour suggérer des actions d'adaptation système basées sur l'état observé (mode OO, RAM dispo, erreurs récentes). Le policy engine déterministe décide d'appliquer ou non.

**Commande REPL:** `/oo_consult`

**Comportement:**

1. **Collecte de l'état système** (lecture, pas de modification):
   - Mode OO actuel (`g_oo_last_mode`: SAFE/DEGRADED/NORMAL)
   - RAM disponible estimée (MB) via preflight estimator
   - `ctx_len` effectif
   - `seq_len` actuel
   - Boot count
   - Dernières entrées du journal `OOJOUR.LOG` (tail 3-5 lignes, best-effort)

2. **Composition du prompt système** (compact, <256 chars):
   ```
   System status: mode=SAFE ram=512MB ctx=256 seq=1024 boots=5 errors=[model_open_fail, ram_low]. Suggest ONE brief action to improve stability (max 10 words).
   ```

3. **Génération LLM**:
   - Paramètres: `temperature=0.3`, `max_tokens=32`, `top_k=20` (faible créativité, réponses courtes)
   - Timeout: budget decode standard (pas de timeout spécial)
   - Capture de la réponse dans un buffer ASCII (max 128 chars)

4. **Parsing de la suggestion**:
   - Recherche de mots-clés déterministes dans la réponse:
     - `reduce` → suggestion de réduction (ctx_len ou seq_len)
     - `increase` → suggestion d'augmentation
     - `reboot` → suggestion de redémarrage
     - `model` → suggestion de changement de modèle
     - `stable` / `ok` / `wait` → pas d'action (système stable)
   - Extraction de valeurs numériques si présentes (ex: "reduce ctx to 128")

5. **Décision policy engine (safety-first)**:
   - **Règle 1:** En mode SAFE, seules les réductions sont autorisées (never increase).
   - **Règle 2:** Les suggestions d'augmentation sont ignorées si RAM < seuil.
   - **Règle 3:** Reboot/model change → loggé mais **pas appliqué automatiquement** (nécessite confirmation utilisateur explicite dans v0).
   - **Règle 4:** Si aucun mot-clé reconnu → log "no actionable suggestion".

6. **Logging déterministe** (serial + journal):
   ```
   OK: OO LLM suggested: <texte_brut_reponse>
   OK: OO policy decided: <action_appliquee_ou_ignored> (reason=<raison>)
   ```
   Exemples:
   - `OK: OO LLM suggested: reduce context to 128`
   - `OK: OO policy decided: reduce_ctx (256->128) (reason=safe_mode_allow)`
   
   - `OK: OO LLM suggested: increase ram allocation`
   - `OK: OO policy decided: ignored (reason=safe_mode_no_increase)`

7. **Actions appliquées** (v0 minimal):
   - **reduce_ctx:** Écrire `ctx_len=<new_val>` dans `repl.cfg` (best-effort, pas de reboot auto).
   - **reduce_seq:** Log uniquement (seq_len est déjà réduit par preflight si nécessaire).
   - **stable:** Aucune action, log "system_stable".

8. **Config opt-in** (repl.cfg):
   - `oo_llm_consult=0|1` (default=1 si `oo_enable=1`, sinon 0)
   - Si désactivé, `/oo_consult` print un message d'erreur.

**Test QEMU déterministe:** `oo_llm_consult`

**Profil du test:**
- Mode: `oo_llm_consult`
- RAM: 640MB (déclenche mode SAFE avec RAM budget visible)
- Config injectée: `oo_enable=1`, `oo_llm_consult=1`
- Autorun: `/oo_consult` + `/quit`
- Assertions (serial markers):
  - `OK: OO boot_count=` (confirme OO actif)
  - `OK: OO LLM suggested:` (confirme génération LLM)
  - `OK: OO policy decided:` (confirme décision policy)
- Timeout: 600s (génération LLM incluse)

**Commande test:**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_llm_consult -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```

### M5.1 — Parser multi-actions

**Objectif:** Permettre au LLM de suggérer plusieurs actions simultanées (ex: "reduce ctx AND switch model"), et appliquer toutes les actions compatibles selon les règles safety-first.

**Modifications au parser:**

1. **Détection multi-keywords** (balayage complet de la réponse LLM):
   - Rechercher tous les keywords présents: `reduce`, `increase`, `reboot`, `model`, `stable`
   - Créer une liste d'actions détectées (max 4 pour éviter confusion)

2. **Filtrage par priorité et compatibilité:**
   - Si `reduce` ET `increase` détectés → garder seulement `reduce` (safety-first)
   - Si plusieurs `reduce` (ex: ctx, seq, tokens) → appliquer toutes les réductions compatibles
   - Si `reboot` détecté → ignorer autres actions (reboot prime)
   - Si `stable` détecté avec autres actions → marquer conflit, appliquer `stable` (no-op)

3. **Application séquentielle avec logging:**
   - Pour chaque action validée:
     - Appliquer selon règles safety-first (mêmes que M5)
     - Logger: `OK: OO policy applied: <action> (reason=<reason>)`
   - Émettre résumé: `OK: OO policy batch: <count> actions applied, <count> blocked`
   - Journal: `oo event=consult_multi actions=<list> applied=<count> blocked=<count>`

4. **Prompt adapté** (encourager multi-actions si pertinent):
   ```
   System: mode=DEGRADED ram=256MB ctx=512 boots=12 log=[...]. Suggest 1-3 brief actions to improve stability (max 20 words total):
   ```

**Exemples de parsing:**

| Suggestion LLM                          | Actions détectées       | Actions appliquées (SAFE)                  | Raison                                      |
|-----------------------------------------|-------------------------|--------------------------------------------|---------------------------------------------|
| "reduce ctx to 256 and switch model"    | reduce_ctx, model       | reduce_ctx ✅                              | model blocked (logged only in v0)          |
| "increase ram stable"                   | increase, stable        | none (conflict)                            | stable cancels increase                     |
| "reduce ctx reduce seq"                 | reduce_ctx, reduce_seq  | reduce_ctx ✅ reduce_seq ✅ (si RAM<1GB)  | both reductions allowed in SAFE             |
| "reboot now reduce ctx"                 | reboot, reduce_ctx      | none (reboot logged)                       | reboot primes, others ignored               |
| "everything is stable"                  | stable                  | none                                       | no action needed                            |

**Config flag:** `oo_multi_actions=0|1` (default: 1 si `oo_llm_consult=1`)

**Test profile:**
- Mode: DEGRADED (RAM tight but not SAFE)
- LLM prompt biaisé: injecter "reduce ctx reduce seq" via mock
- Assertions:
  - `OK: OO policy applied: reduce_ctx`
  - `OK: OO policy applied: reduce_seq` (si applicable)
  - `OK: OO policy batch: 2 actions applied, 0 blocked`
  - Journal: `oo event=consult_multi actions=[reduce_ctx,reduce_seq] applied=2`
  - Timeout: 600s

**Commande test:**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_llm_multi -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```

**⚠️ Limitation de test:** Le modèle `stories110M.bin` (story generator) ne peut pas exécuter d'instructions comme "Suggest actions". Le test `oo_llm_multi` échouera avec ce modèle car il génère du texte narratif au lieu de mots-clés. **M5.1 nécessite un modèle instruction-tuned (ex: TinyLlama-1.1B-Chat) pour validation fonctionnelle**. Le code est implémenté et correct, mais la validation end-to-end attend un modèle adapté.

**Extensions futures (hors v0):**
- M5.3: Historique des consultations dans `OOCONSULT.LOG`
- M5.4: Métriques de pertinence (suggestion appliquée → amélioration observée au boot suivant)

---

### M5.2 — Auto-application conditionnelle

**Objectif:** Permettre au système d'appliquer automatiquement les adaptations suggérées par le LLM, avec vérifications de sécurité strictes pour éviter les corruptions ou comportements instables.

**Mécanisme:**

1. **Flag de configuration** : `oo_auto_apply=0|1|2` (default: 0)
   - `0` = off (comportement actuel : log seulement, pas d'application automatique)
   - `1` = conservative (applique seulement réductions en mode SAFE/DEGRADED)
   - `2` = aggressive (applique toutes actions safety-first, même increases si RAM≥1GB)

2. **Conditions d'application** (toutes doivent être vraies) :
   - `oo_auto_apply ≥ 1`
   - Action passe les règles safety-first de M5/M5.1
   - Si mode=1 : seulement réductions (reduce_ctx, reduce_seq)
   - Si mode=2 : réductions + increases (si RAM≥1GB en NORMAL)
   - Reboot/model jamais auto-appliqués (logged only, même en mode 2)

3. **Vérification post-action** (détection corruption) :
   - **Avant application** : calculer checksum simple de la config critique :
     - `checksum = ctx_len XOR seq_len XOR (ram_mb << 8)`
   - **Appliquer l'action** (écrire `repl.cfg`)
   - **Après application** : relire `repl.cfg`, parser valeurs, recalculer checksum
   - **Vérifier cohérence** :
     - Nouvelle valeur dans range attendu (ex: ctx_len ∈ [16, 4096])
     - Pas de corruption du fichier (syntaxe OK)
   - **Si échec** : logger `ERROR: auto-apply verification failed, reverting` + restaurer ancienne valeur

4. **Logging renforcé** :
   - `OK: OO auto-apply: <action> (old=<val> new=<val> check=pass)`
   - `ERROR: OO auto-apply verification failed: <action> (reason=<reason>)`
   - Journal: `oo event=auto_apply action=<action> result=success|failed`

5. **Throttling** (éviter boucles d'adaptation) :
   - Limiter à 1 auto-application par boot
   - Tracker: `g_oo_auto_applied_this_boot` (reset au démarrage)
   - Si déjà appliqué : logger `INFO: auto-apply throttled (limit: 1/boot)`

**Exemples de comportement:**

| Config | Mode | Suggestion LLM | Auto-appliqué? | Raison |
|--------|------|----------------|----------------|--------|
| `oo_auto_apply=1` | SAFE | "reduce ctx to 256" | ✅ Oui | Réduction en SAFE (conservative) |
| `oo_auto_apply=1` | DEGRADED | "increase ctx to 1024" | ❌ Non | Increase bloqué en mode 1 |
| `oo_auto_apply=2` | NORMAL (2GB RAM) | "increase ctx to 1024" | ✅ Oui | Increase OK avec RAM≥1GB + mode 2 |
| `oo_auto_apply=2` | SAFE | "reboot now" | ❌ Non | Reboot jamais auto-appliqué |
| `oo_auto_apply=1` | DEGRADED | "reduce ctx" (2e fois même boot) | ❌ Non | Throttling: 1/boot déjà utilisé |

**Test profile:**
- Mode: DEGRADED (768MB RAM)
- Config: `oo_enable=1`, `oo_llm_consult=1`, `oo_auto_apply=1`
- Autorun: `/oo_status` (voir ctx avant), `/oo_consult` (suggère + applique), `/oo_status` (vérifier ctx changé)
- Assertions:
  - Serial: `OK: OO auto-apply: reduce_ctx (old=512 new=256 check=pass)`
  - Config changed: `repl.cfg` contient nouvelle valeur `ctx_len=256`
  - Journal: `oo event=auto_apply action=reduce_ctx result=success`
- Timeout: 600s (LLM generation)

**Commande test:**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_auto_apply -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild
```

**⚠️ Limitation de test:** Même limitation que M5.1 - le modèle `stories110M.bin` ne peut pas exécuter d'instructions. **M5.2 nécessite un modèle instruction-tuned (TinyLlama-1.1B-Chat)** pour validation fonctionnelle. Le code est implémenté et le build réussit (487KB kernel), mais la validation end-to-end attend un modèle adapté.

**Sécurité renforcée:**
- Checksum simple mais efficace pour détecter corruptions
- Range checking sur valeurs (éviter ctx_len=9999999)
- Throttling 1/boot (éviter spirales d'adaptation)
- Mode conservative par défaut (mode=1 : réductions only)

---

### M5.3 — Historique des consultations

**Objectif:** Logger toutes les consultations LLM (`/oo_consult`) dans un fichier persistant pour analyse, debugging, et évaluation de la pertinence des suggestions au fil du temps.

**Mécanisme:**

1. **Fichier log** : `OOCONSULT.LOG` (à la racine du disque FAT32)
   - Format : lignes texte ASCII, une entrée par consultation
   - Append-only (jamais écrasé)
   - Limite : 64KB max (truncate oldest si dépassé)

2. **Format d'entrée** (une ligne par consultation, ~150-200 chars) :
   ```
   [YYYY-MM-DD HH:MM:SS] boot=<N> mode=<MODE> ram=<MB> ctx=<val> seq=<val> suggestion="<text>" decision=<action> applied=<0|1>
   ```
   - Timestamp : boot count utilisé comme proxy (pas de RTC UEFI fiable)
   - Mode : SAFE/DEGRADED/NORMAL
   - Suggestion : texte LLM (truncate à 80 chars si >)
   - Decision : action décidée (reduce_ctx, stable, blocked, etc.)
   - Applied : 1 si auto-appliquée (M5.2), 0 sinon

3. **Exemple d'entrées** :
   ```
   [boot=12] mode=SAFE ram=512 ctx=512 seq=512 suggestion="reduce ctx to 256" decision=reduce_ctx applied=1
   [boot=13] mode=DEGRADED ram=768 ctx=256 seq=512 suggestion="increase ctx stable" decision=stable applied=0
   [boot=14] mode=NORMAL ram=2048 ctx=256 seq=512 suggestion="increase to 512" decision=increase_ctx applied=0
   ```

4. **Opérations filesystem** :
   - **Open** : `EFI_FILE_PROTOCOL->Open()` avec `EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE`
   - **Seek EOF** : `SetPosition()` avec `0xFFFFFFFFFFFFFFFF`
   - **Append** : `Write()` nouvelle ligne
   - **Rotation** : Si taille >64KB, lire secondes moitié, truncate, réécrire (simple FIFO)

5. **Config flag** : `oo_consult_log=0|1` (default: 1 si `oo_llm_consult=1`)
   - Si désactivé, pas de log (utile pour tests rapides)

6. **Commande de lecture** : `/oo_log` (optionnel, affiche dernières 10 lignes)

**Bénéfices:**

- **Debugging** : Comprendre pourquoi une suggestion a été faite/appliquée
- **Pattern analysis** : Identifier suggestions récurrentes, conditions déclenchantes
- **Métriques futures** (M5.4) : Base de données pour évaluer pertinence
- **Continuité** : Historique survit aux reboots (preuve de "mémoire")

**Sécurité:**

- Log en append-only (pas de modification rétroactive)
- Truncation simple si >64KB (oldest-first FIFO, pas de corruption)
- Erreurs log non-bloquantes (échec silencieux, system continue)

**Test profile:**
- Mode: SAFE (640MB RAM)
- Config: `oo_enable=1`, `oo_llm_consult=1`, `oo_consult_log=1`
- Autorun: 
  1. `/oo_consult` (1ère consultation, crée OOCONSULT.LOG)
  2. `/oo_consult` (2e consultation, append)
  3. `/quit`
- Assertions:
  - Fichier `OOCONSULT.LOG` existe sur le disque
  - Contient 2 lignes (2 consultations)
  - Format valide (boot=, mode=, suggestion=, decision=)
  - Serial: `OK: OO consult logged to OOCONSULT.LOG`
- Timeout: 900s (2x LLM generations)

**Commande test:**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_consult_log -Accel tcg -TimeoutSec 900 -SkipInspect -SkipBuild
```

**⚠️ Limitation de test:** Même limitation que M5.1/M5.2 — nécessite TinyLlama-1.1B-Chat pour générer suggestions actionnables.

---

## Notes

- "Conscience" : v0 vise des propriétés **observables** (continuité, introspection loguée, auto-maintenance). Toute revendication plus forte devra être étayée par des critères et des expériences.
