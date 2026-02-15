# Ameliorations apportees (M6 -> M16)

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

## M13.1 - Reason IDs natifs depuis le runtime OO

- Le moteur OO (`llama2_efi_final.c`) émet maintenant des `reason_id=...` explicites dans les marqueurs policy/auto-apply.
- `m13-explainability.ps1` priorise ces reason IDs natifs pour `reason_code` (avec fallback rétrocompatible).
- Résultat: traçabilité plus fiable entre décision runtime et artefacts d'explicabilité.

## M14 - Couverture reason_id et parité log/journal

- Le runtime enrichit les marqueurs `OO confidence` et `OO plan` avec `reason_id=...`.
- Le journal OO embarque aussi des `reason_id` sur événements confidence/plan/auto-apply.
- Nouveau script `m14-explainability-coverage.ps1`:
  - vérifie la couverture `reason_id` dans les marqueurs runtime,
  - vérifie la parité optionnelle log/journal quand `OOJOUR.LOG` est disponible,
  - persiste `artifacts/m14/coverage-state.json` et `artifacts/m14/history.jsonl`.

## M14.1 - Extraction runtime de OOJOUR pour parité stricte

- Nouveau script `m14-extract-oojournal.ps1`:
  - extrait `OOJOUR.LOG` depuis l'image boot (`llm-baremetal-boot*.img`) via WSL+mtools,
  - écrit `artifacts/m14/extract-state.json` et `artifacts/m14/extract-history.jsonl`.
- `m8-reliability.ps1` supporte désormais la parité stricte avec `-M14RequireJournalParity`.
- CI runtime active ce mode strict et publie les artefacts `OOJOUR.LOG` + état/historique d'extraction.

## M15 - Guardrails de drift des reason_id

- Nouveau script `m15-reasonid-drift.ps1`:
  - mesure la distribution des `reason_id` sur le run courant,
  - compare à une baseline récente issue de l'historique M13,
  - déclenche alertes/anomalies sur dérive forte ou profils inconnus dominants.
- Intégration en fin de pipeline runtime M8 avec mode gate (`-FailOnDrift`).
- Artefacts produits:
  - `artifacts/m15/drift-state.json`,
  - `artifacts/m15/history.jsonl`.

## M15.1 - Export dashboard SLO reason_id

- Nouveau script `m15-slo-dashboard.ps1`:
  - agrège les runs M15 récents pour un snapshot SLO compact (pass rate drift, part moyenne `AUTO_APPLY_UNKNOWN`),
  - construit un comparatif week-over-week des `reason_id` à partir de l'historique M13,
  - produit des snapshots hebdomadaires compacts pour revue de régression.
- Artefacts produits:
  - `artifacts/m15/dashboard-state.json`,
  - `artifacts/m15/dashboard.md`,
  - `artifacts/m15/dashboard-history.jsonl`.
- Intégration dans `m8-reliability.ps1` après M15 et publication CI des artefacts dashboard.

## M16 - Interface publique unifiée pour les guardrails

- Nouveau script public `reliability.ps1`:
  - point d'entrée unique pour tous les checks de fiabilité (statique + runtime),
  - délègue à l'orchestrateur interne (M8-M15.1) masqué dans `.ops/milestones/`,
  - réduit la surface visible du repo en cachant la granularité milestone interne.
- Refactor des scripts M8-M15.1:
  - déplacés dans `.ops/milestones/`,
  - chemins corrigés pour fonctionner depuis le sous-dossier,
  - workflow CI mis à jour pour utiliser `reliability.ps1`.
- Résultat: repo plus discret, interface opérateur simplifiée, maintenance pipeline conservée.

## M16.1 - Métriques runtime exportables (JSON)

- Structure `LlmkRuntimeMetrics` instrumentant le runtime:
  - capture cycles TSC (prefill/decode) avec `__rdtsc()`,
  - compteurs tokens préfill/décodage avec phase-awareness,
  - compteurs opérationnels: appels transformer, resets KV cache, générations complétées,
  - placeholder pour violations sentinel (intégration future).
- Commande REPL `/metrics`:
  - export JSON complet de `g_metrics` vers `LLMK_METRICS.LOG`,
  - helper `llmk_u64_to_str()` pour conversion sans `sprintf` (bare-metal UEFI),
  - utilise infrastructure fichier existante (`llmk_open_binary_file`, `llmk_file_write_bytes`).
- Initialisation automatique: `llmk_metrics_reset()` appelé au démarrage REPL (après `repl_ready` marker).
- Résultat: métriques de performance accessibles en runtime pour analyse SLO, détection dérives, et debugging.

## M16.2 - Agrégation métriques + détection dérive performance

- Script `m16-extract-metrics.ps1`:
  - extraction automatique de `LLMK_METRICS.LOG` depuis image bootable (via WSL + mtools),
  - validation JSON et stockage timestampé dans `artifacts/m16/raw/`,
  - intégration dans pipeline M8 pour collecte post-run.
- Script `m16-metrics-aggregate.ps1`:
  - parse tous les fichiers métriques raw collectés,
  - calcul statistiques agrégées: moyenne, P50, P95, P99 pour cycles/token (prefill/decode),
  - détection dérive performance vs baseline configurable (seuil %, ex: +20%),
  - persistance historique dans `artifacts/m16/metrics-history.jsonl`,
  - génération dashboard Markdown avec résumé stats + alertes dérive.
- Intégration M8/reliability:
  - flags `-M16ExtractMetrics` active extraction + agrégation post-runtime,
  - `-M16UpdateBaseline` met à jour baseline de référence,
  - `-M16RejectOnDrift` bloque pipeline si dérive détectée (gate CI),
  - paramètres exposés via `reliability.ps1` wrapper public.
- Résultat: visibilité long-terme sur performance runtime, détection automatique régressions, traçabilité CI.

## M17 - Intégration CI complète avec reporting automatique

- Script `m17-ci-metrics-report.ps1`:
  - génération rapport compact pour logs CI (texte + Markdown),
  - analyse dérive avec seuils warn/fail séparés (défaut: 15%/30%),
  - création automatique GitHub Actions job summary avec tableaux métriques,
  - gate optionnel sur dérive excessive (`-M17FailOnDrift`).
- Workflow GitHub Actions (`.github/workflows/m8-reliability.yml`):
  - step post-runtime: extraction automatique `LLMK_METRICS.LOG` depuis image bootable,
  - génération rapport M17 avec drift detection,
  - upload artifacts: métriques raw JSON + rapport texte pour traçabilité,
  - intégration job summary: tableaux performance visibles directement dans l'UI GitHub Actions.
- Intégration M8/reliability:
  - flag `-M17EnableCIReport` active génération rapport post-extraction,
  - détection automatique fichier métriques le plus récent si non spécifié,
  - seuils configurables via paramètres exposed dans `reliability.ps1`.
- Résultat: traçabilité continue performance runtime dans CI, alertes visuelles sur dérives, historique artifacts pour analyse régression.

## M18 - Auto-tuning runtime fermé (MVP)

- Boucle d'adaptation runtime dans `llama2_efi_final.c`:
  - ajuste dynamiquement `temperature`, `top_p`, `top_k`, `max_gen_tokens` après chaque tour,
  - pilotage par `decode cycles/token` (delta par tour basé sur métriques M16.1),
  - modes d'action: `tighten` (si dérive haute) et `relax` (si charge basse).
- Configuration via `repl.cfg`:
  - `autotune=0|1`,
  - `autotune_decode_cpt_hi`, `autotune_decode_cpt_lo`,
  - `autotune_step_top_k`, `autotune_step_max_tokens`, `autotune_step_temp_milli`.
- Garde-fous intégrés:
  - bornes minimales pour éviter une dégradation agressive (`min_top_k`, `min_max_gen_tokens`, `min_top_p`, `min_temp`),
  - relaxation limitée aux valeurs de base chargées en début de session.
- Observabilité opérateur:
  - commande REPL `/autotune_status` (seuils, steps, valeurs courantes, dernière action),
  - logs runtime `[m18] autotune=... decode_cpt=...` lors d’un ajustement effectif.
- Résultat: adaptation automatique légère sans refactor lourd, meilleure stabilité perfs sur charges variables.

## M18.1 - Guardrails temps réel + safe fallback mode

- Guardrail hard decode en boucle de génération:
  - compteur d'overruns decode surveillé en temps réel,
  - arrêt anticipé de génération (`stop_reason=budget_guard`) quand le seuil est dépassé,
  - seuil configurable via `repl.cfg` (`guardrails_decode_hard_stop_overruns`).
- Safe fallback post-trip (multi-turn borné):
  - active un mode dégradé pendant N tours (`guardrails_safe_turns`),
  - applique caps défensifs sur `top_k`, `max_gen_tokens`, `top_p`, `temperature`,
  - option de reset KV cache sur trip (`guardrails_reset_kv_on_trip=1`).
- Configuration `repl.cfg` (M18.1):
  - `guardrails` / `m181_guardrails` (on/off),
  - `guardrails_safe_top_k`, `guardrails_safe_max_tokens`,
  - `guardrails_safe_top_p_milli`, `guardrails_safe_temp_milli`.
- Observabilité:
  - nouvelle commande REPL `/guard_status` (état runtime, trip count, turns restants, caps),
  - logs explicites `[m18.1] hard-stop decode ...` et `[m18.1] safe-mode caps applied ...`.
- Résultat: comportement plus robuste sous dérive latence, avec réponse graduée (stop immédiat + mode sûr temporaire).

## M19 - Benchmark pack reproductible + matrice commit-to-commit

- Corpus benchmark figé ajouté: `.ops/benchmarks/m19-corpus.json` (cas courts multi-catégories).
- Nouveau script `m19-benchmark-pack.ps1`:
  - génère un pack déterministe (`benchmark-pack.json`) avec métadonnées (commit, seed, modèle, profil),
  - exporte les entrées d'exécution en JSONL (`benchmark-input.jsonl`),
  - publie un manifeste Markdown (`benchmark-manifest.md`) et un résumé optionnel si `results.jsonl` est présent.
- Nouveau script `m19-benchmark-compare.ps1`:
  - compare baseline vs courant sur cas communs (`case_id`),
  - calcule deltas par cas (latence ms, decode cycles/token),
  - produit une matrice Markdown (`artifacts/m19/compare/benchmark-compare.md`) + résumé JSON,
  - supporte gate régression configurable (`-RegressionThresholdPct`, `-FailOnRegression`).
- Intégration orchestrateur fiabilité (`reliability.ps1` + `.ops/milestones/m8-reliability.ps1`):
  - activation via `-M19EnableBenchmarkPack`,
  - chemins baseline/courant configurables (`-M19BaselineResultsPath`, `-M19CurrentResultsPath`),
  - gate optionnelle `-M19FailOnRegression`.
- Résultat: base standardisée pour comparer les performances entre commits avec artefacts exploitables en local/CI.

## Résultat global

- Pipeline plus sûr, plus observable, et mieux automatisé.
- Détection plus rapide des régressions fonctionnelles et de performance.
- Mécanismes OO plus robustes face aux comportements dégradés.
- Interface publique simplifiée (M16) avec complexité interne masquée.
