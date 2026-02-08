# Repère — LLM Baremetal (UEFI)

## Ce qu’on essaie de créer

Un binaire UEFI autonome (QEMU + vrai PC) qui boote de façon déterministe, charge un modèle (BIN ou GGUF), et expose un REPL minimal. L’objectif est la fiabilité et la reproductibilité (repo minimal, pas de gros binaires committés).

## Contraintes / règles du repo

- Repo minimal: pas de poids de modèles, images, toolchains ou artefacts lourds versionnés.
- Toolchains pin: téléchargés dans `tools/_toolchains/` (ignoré par git), avec vérification de checksum.
- Windows + WSL + mtools: éviter les écritures mtools sur `/mnt/c` (stager en WSL-local puis recopier).

## Ce qui est déjà fait (état actuel)

### A1 — Manual boot smoke (priorité absolue)

- Test “sans injection” en QEMU qui valide 3 marqueurs série:
  - `OK: Djibion boot`
  - `OK: Model loaded: ...`
  - `OK: Version: ...`
- Script: `test-qemu-manual-smoke.ps1`

### A2 — FAT 8.3 partout

- Côté host (PowerShell): résolution/rewriting du `model=` dans `repl.cfg` vers l’alias 8.3 (ex: `STORIE~1.BIN`).
  - Module partagé: `tools/fat83.ps1`
  - Utilisé par: `test-qemu-autorun.ps1` + builder
- Côté kernel (UEFI): fallback d’ouverture si le long filename échoue.
  - Log si fallback réussi: `[fat] open fallback ok (...) : <long> -> <alias>`

Preuve déterministe (tests):

- `fat83_force=1` dans `repl.cfg` force la préférence pour l’alias 8.3 quand il existe (utile sous QEMU/OVMF).
- Autorun dédié: `test-qemu-autorun.ps1 -Mode fat83_fallback` (injecte un long nom + `fat83_force=1` et assert le log `[fat] ...`).


### A3 — Logs actionnables


## Tests principaux (commandes)

- Astuce (cwd-safe): depuis la racine du repo, tu peux lancer l’autorun via le wrapper `./test-qemu-autorun.ps1 ...` (ça évite le warning si tu fais `cd .\\llm-baremetal` alors que tu es déjà dedans).

- Build + image:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -ModelBin stories110M.bin`
- Manual smoke (A1):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-manual-smoke.ps1 -TimeoutSec 120`
- Autorun RAM (régression):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode ram -Accel tcg -MemMB 1024 -SkipBuild -SkipInspect -TimeoutSec 120`
- OO M1 smoke (persistence: OOSTATE.BIN + OOJOUR.LOG, 2 boots sans snapshot):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_smoke -Accel tcg -TimeoutSec 600 -SkipInspect`
- OO M2 recovery (corruption state -> SAFE + rollback, 2 boots sans snapshot):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_recovery -Accel tcg -TimeoutSec 600 -SkipInspect`
- OO M3 homeostasis (preuve clamp ctx_len en SAFE/DEGRADED):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_ctx_clamp -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild`
- OO M3 homeostasis (preuve cap DEGRADED: 3 boots sans snapshot -> clamp mode=DEGRADED):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_ctx_clamp_degraded -Accel tcg -TimeoutSec 900 -SkipInspect -SkipBuild`
- OO M3 policy (model fallback: model invalide dans repl.cfg -> fallback + log OO):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_model_fallback -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild`
- OO M3 policy (RAM budget preflight: low-RAM SAFE zone minimum marker; defaults to `-MemMB 640` unless overridden):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_ram_preflight -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild`
- OO M3 policy (RAM preflight reduces seq_len under tight RAM; uses `oo_min_total_mb=0` and defaults to `-MemMB 620`):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_ram_preflight_seq -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild`
- OO M5 LLM advisor (LLM suggests system adaptations, policy engine decides; runs in SAFE mode with 640MB RAM):
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-qemu-autorun.ps1 -Mode oo_llm_consult -Accel tcg -TimeoutSec 600 -SkipInspect -SkipBuild`

⚠️ Note PowerShell / VS Code:

- Si VS Code transforme un chemin en lien Markdown du style `.[test-qemu-autorun.ps1](http://...)`, ne copie pas ça dans le terminal.
- Utilise le chemin PowerShell normal: `.\test-qemu-autorun.ps1 ...`

## Ce qui suit (next)

- Direction "OO" (Operating Organism): spec testable v0 + jalons
  - Voir: `OO_SPEC.md`
  - **M1** (persistence): ✅ implémenté + validé (`oo_smoke`)
  - **M2** (recovery): ✅ implémenté + validé (`oo_recovery`)
  - **M3** (homeostasis): ✅ implémenté + validé (`oo_ctx_clamp`, `oo_ctx_clamp_degraded`, `oo_model_fallback`, `oo_ram_preflight`, `oo_ram_preflight_seq`)
  - **M4** (réseau): optionnel, pas prioritaire
  - **M5** (LLM conseiller): ✅ implémenté + test `oo_llm_consult` prêt
    - Le LLM embarqué suggère des adaptations système (réduire/augmenter ctx, reboot, changer modèle, ou "stable")
    - Moteur de politique safety-first (SAFE=reductions only, DEGRADED/NORMAL=no increases if RAM<1GB)
    - Marqueurs déterministes: `OK: OO LLM suggested:` + `OK: OO policy decided:`
  - **M5.1** (multi-actions):  ✅ implémenté, validation limitée par modèle
    - Parser étendu : détecte plusieurs actions simultanées (ex: "reduce ctx AND seq")
    - Priorités : stable>reboot>reduce (reduce bloque increase)
    - Prompt adapté : "Suggest 1-3 brief actions" si `oo_multi_actions=1`
    - ⚠️ Test `oo_llm_multi` échoue avec stories110M (non instruction-tuned) — nécessite TinyLlama-Chat ou similaire
  - **M5.2** (auto-apply): ✅ implémenté, validation limitée par modèle
    - Flag `oo_auto_apply=0|1|2` (off / conservative / aggressive)
    - Mode 1: applique réductions automatiquement (reduce_ctx, reduce_seq)
    - Mode 2: applique réductions + increases (si RAM≥1GB en NORMAL)
    - Throttling: 1 auto-application par boot (évite spirales d'adaptation)
    - Marqueurs: `OK: OO auto-apply: <action> (old=X new=Y check=pass)`
    - ⚠️ Test `oo_auto_apply` nécessite TinyLlama-Chat — stories110M incompatible
- Preuve automatisée du fallback FAT83 (sans dépendre de la flakiness UEFI LFN):
  - Mode autorun dédié `-Mode fat83_fallback` qui injecte `model=<longname>` + `fat83_force=1` et vérifie le log `[fat] open fallback ok`.
- Éventuel: réduire encore la “surface” de fallback si besoin (couverture d’autres opens), ou ajuster la verbosité.
