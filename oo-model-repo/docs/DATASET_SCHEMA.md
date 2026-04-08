# OO Dataset Schema

## Format principal

Le format principal est `jsonl`.

Chaque ligne représente un exemple autonome.

## Schéma minimal

```json
{
  "id": "boot-000001",
  "family": "boot_recovery",
  "source": "qemu-log",
  "input": "boot failed after loading GGUF header",
  "target": "Check GGUF header size, validate aligned reads, retry with model summary.",
  "context": {
    "repo": "llm-baremetal",
    "component": "engine/llama2",
    "mode": "boot",
    "risk": "medium"
  },
  "tags": ["boot", "diagnostic", "gguf"],
  "quality": 0.92
}
```

## Métadonnées recommandées

Le champ `context` peut contenir, quand disponible :

- `source_path`
- `mode`
- `component`
- `risk`
- `event_index`

Un manifest global des sources est écrit dans `data/raw/source_manifest.json`.

## Familles

### `boot_recovery`

- logs de boot
- crashs
- séquences de reprise
- échecs de chargement modèle

### `operator_command`

- commande utilisateur
- état connu
- réponse/action attendue

### `journal_memory`

- journal brut
- résumé opératoire
- continuity snapshots
- handoff host ↔ sovereign

### `policy_safety`

- entrée risquée
- décision de refus/autorisation
- justification

### `system_reasoning`

- diagnostic
- triage
- planification courte
- next action

## Règles de qualité

- pas de doublons évidents
- pas de données contradictoires non marquées
- séparer clairement faits observés et inférences
- noter le niveau de confiance
- garder les réponses courtes et actionnables

## Splits

- `train.jsonl`
- `valid.jsonl`
- `test.jsonl`
- `eval_oo.jsonl`

Le corpus brut consolidé est écrit dans `data/raw/extracted_corpus.jsonl` avant le split déterministe.

## Evaluation examples

Le split `eval_oo.jsonl` doit couvrir :

- boot failure
- policy refusal
- journal summarization
- continuity explanation
- command understanding
- safe next-step proposal
