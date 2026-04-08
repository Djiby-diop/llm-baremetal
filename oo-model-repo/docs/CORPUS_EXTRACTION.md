# OO Corpus Extraction Workflow

## But

Construire un premier corpus réellement ancré dans l'écosystème OO, sans inventer de données externes.

## Sources actuelles

### `llm-baremetal`

- logs QEMU à la racine du workspace
- scripts `llmk-autorun-*.txt`
- validation `tests/test-qemu-handoff.ps1`
- receipt exemple `docs/OOHANDOFF.TXT`
- specs `docs/OO_SPEC.md` et `docs/COMMANDES.md` pour les formats `OOCONSULT.LOG` / `OOJOUR.LOG`
- contrat de validation `scripts/validate-real-hw-oo-artifacts.ps1`

### `oo-host`

- `data/organism_journal.jsonl`
- `data/sovereign_export.json`
- `data/organism_state.json`
- `data/organism_recovery.json`
- `data/handoff-status.md`
- `data/handoff-pack/*`

### `oo-system`

- `interface/cli/src/oo_cli.c`

## Sorties

Commande :

```bash
python scripts/prepare_dataset.py --input data/raw --output data/processed
```

Fichiers générés :

- `data/raw/extracted_corpus.jsonl`
- `data/raw/source_manifest.json`
- `data/processed/train.jsonl`
- `data/processed/valid.jsonl`
- `data/processed/test.jsonl`
- `data/processed/eval_oo.jsonl`
- `data/processed/manifest.json`

## Principes de construction

- extraction déterministe depuis des fichiers réels
- déduplication par contenu utile
- séparation explicite `eval_oo`
- familles limitées au schéma OO
- réponses courtes, actionnables, orientées système

## Limites actuelles

- le corpus reflète surtout la continuité, le handoff, la journalisation et les commandes
- les logs boot riches restent peu nombreux tant que de nouveaux runs QEMU ne sont pas ajoutés
- aucun `OOCONSULT.LOG` / `OOJOUR.LOG` brut n'était présent dans le workspace au moment de l'extraction; les exemples log proviennent donc des specs et contrats de validation
- la partie policy est encore dominée par `observe`, pas par des cas d'enforcement variés

## Étape suivante

- enrichir les sources avec de nouveaux artifacts de boot/recovery réels
- ajouter des journaux `OOCONSULT.LOG` et `OOJOUR.LOG` exportés depuis des runs récents
- construire une batterie `eval_oo` plus dure sur explication et triage