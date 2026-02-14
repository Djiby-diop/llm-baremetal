# Ameliorations apportees (M6 -> M13)

Ce document synthétise les améliorations livrées dans `llm-baremetal` pendant la phase d'industrialisation.

## M6 - Durcissement opérateur/release

- Flux opérateur unifié: `preflight-host.ps1` -> `build.ps1` -> `run.ps1 -Preflight`.
- Vérification one-shot: `m6-verify.ps1`.
- Contrôle package release + checksums: `m6-package-check.ps1`.
- Préparation bundle release: `m6-release-prep.ps1`.

## A1 - Startup path

- Marqueurs timing startup: `[obs][startup] model_select_ms=... model_prepare_ms=...`.
- Réduction des opérations redondantes au démarrage (GGUF).
- Cache résumé modèle pour `/model_info`.

## A2 - Observabilité runtime

- Marqueurs structurés pour le diagnostic runtime et OO.
- Signaux plus déterministes dans les logs série.

## A3 - UX modèle / diagnostics

- `/models` amélioré: `size + type + name + summary`.
- Diagnostics `model=` plus explicites (cause + hint + fallback).
- Meilleure lisibilité en mode no-model.

## B1 - Confiance OO

- Score de confiance OO avec seuil configurable.
- Mode log-only puis gate enforceable (`oo_conf_gate`, `oo_conf_threshold`).

## B2 - Boucle outcome

- Persistance des outcomes OO (`OOOUTCOME.LOG`) avec expected/observed.
- Réinjection du feedback historique dans le score OO.

## B3 - Plan multi-etapes borne

- Plan OO borné par boot (`oo_plan_enable`, `oo_plan_max_actions`).
- Checkpoint rollback explicite avant auto-apply.
- Hard-stop sur échec de vérification auto-apply.

## M8 / M8.1 - Fiabilité et CI

- Script de fiabilité end-to-end: `m8-reliability.ps1` (statique + runtime autorun).
- Workflow GitHub Actions: `.github/workflows/m8-reliability.yml`.
- Job statique sur push/PR + runtime optionnel sur runner self-hosted.

## M9 / M9.1 - Guardrails de régression

- `m9-guardrails.ps1`: validation marqueurs + budgets latence startup.
- Historique de runs: `artifacts/m9/history.jsonl`.
- Drift check configurable (fenêtre + seuils pour `model_select_ms` / `model_prepare_ms`).

## M10 - Guardrails qualité OO

- `m10-quality-guardrails.ps1`:
  - calcule ratio d'actions harmful (échecs auto-apply),
  - détecte streak d'échecs consécutifs,
  - applique auto-quarantine optionnelle (`oo_auto_apply=0`) si seuils dépassés,
  - écrit l'état de quarantaine dans `artifacts/m10/quarantine-state.json`.

## M10.1 - Seuils adaptatifs qualité

- Ajuste automatiquement les seuils M10 selon:
  - la classe de modèle détectée (`tiny`, `medium`, `large`),
  - le tier RAM (`low`, `mid`, `high`).
- Persiste les seuils effectifs appliqués dans `artifacts/m10/quarantine-state.json`.
- Permet désactivation explicite via `-NoAdaptiveThresholds`.

## M11 - Self-healing de quarantaine (release + canary)

- Nouveau script `m11-self-heal.ps1`:
  - déclenche auto-unquarantine après un streak stable configurable,
  - active un mode canary borné (`CanaryBoots`) avec seuil de confiance renforcé,
  - rollback automatique vers quarantaine si régression en canary.
- Persistance d'état et historique:
  - `artifacts/m11/release-state.json`,
  - `artifacts/m11/release-history.jsonl`.
- Intégration runtime dans `m8-reliability.ps1` après M10.

## M11.1 - Couplage drift/qualité pour release

- Le release/canary M11 est désormais conditionné par deux fenêtres stables:
  - fenêtre M9 (`artifacts/m9/history.jsonl`, champ `pass`),
  - fenêtre M10 (`artifacts/m10/history.jsonl`, champ `quality_ok`).
- Ajout de l'historique M10 (`artifacts/m10/history.jsonl`) pour permettre la décision multi-run.
- Le streak M11 ne progresse que si la qualité courante + fenêtres M9/M10 sont simultanément stables.

## M12 - Curriculum de politique OO (phase + workload)

- Nouveau script `m12-policy-curriculum.ps1` qui ajuste `oo_conf_threshold` par:
  - phase de boot (`early`, `warm`, `steady`),
  - classe de workload (`latency_optimization`, `context_expansion`, `mixed`, `unknown`).
- Calcul du seuil effectif via base de phase + ajustement workload (borné), puis application dans `repl.cfg`.
- Persistance d'état et historique:
  - `artifacts/m12/curriculum-state.json`,
  - `artifacts/m12/history.jsonl`.

## M12.1 - Feedback outcomes sur la matrice curriculum

- M12 lit désormais les outcomes récents M10 (`artifacts/m10/history.jsonl`) pour dériver un score helpful/harmful.
- Ce score ajuste automatiquement la matrice avant calcul final:
  - seuils de phase (`early/warm/steady`),
  - cellule workload active (`latency_optimization|context_expansion|mixed|unknown`).
- Les métadonnées d'adaptation (fenêtre, score, direction, delta) sont persistées dans l'état/historique M12.

## M13 - Pack d'explicabilité policy

- Nouveau script `m13-explainability.ps1`:
  - extrait les événements auto-apply (`success`/`failed`) et leur reason code,
  - agrège des reason codes de synthèse multi-couches (M10/M11/M12),
  - persiste la provenance des seuils effectifs (qualité, self-heal, curriculum).
- Artefacts produits:
  - `artifacts/m13/explainability-state.json`,
  - `artifacts/m13/history.jsonl`.

## Résultat global

- Pipeline plus sûr, plus observable, et mieux automatisé.
- Détection plus rapide des régressions fonctionnelles et de performance.
- Mécanismes OO plus robustes face aux comportements dégradés.
