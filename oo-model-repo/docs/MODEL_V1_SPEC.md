# OO Model v1 Spec

## Nom de travail

`oo-v1-15m`

## Positionnement

Petit modèle spécialisé pour l'exécution OO.

Pas un assistant grand public.
Pas un benchmark model.
Un modèle système.

## Objectifs produit

Le modèle doit savoir :

- interpréter une commande OO
- résumer un état ou un journal
- diagnostiquer un boot ou un incident
- proposer une action suivante sûre
- rester cohérent avec mémoire externe et handoff
- répondre brièvement et de manière opératoire

## Taille cible

### V1 minimum

- 15M paramètres
- séquence 512 à 1024
- vocabulaire 8k à 16k

### V1 plus confortable

- 30M à 60M paramètres
- séquence 1024
- quantisation finale Q8 ou inférieure si acceptable

## Architecture recommandée

Pour v1 :

- decoder transformer compact
- RoPE ou positionnel simple stable
- tokenizer sentencepiece/BPE
- mémoire longue externalisée hors modèle
- pas de MoE, pas de complexité exotique en v1

## Corpus cible

Le corpus doit être centré sur :

1. `boot_recovery`
2. `operator_command`
3. `journal_memory`
4. `policy_safety`
5. `system_reasoning`

## Format de sortie attendu

Réponses :

- courtes
- techniques
- structurées
- justifiées si incertitude
- sans invention d'état système non observé

## Critères de réussite

Le modèle v1 est validé s'il :

- fonctionne sur une batterie OO dédiée
- améliore les réponses opérationnelles par rapport au bootstrap model
- reste intégrable dans `llm-baremetal`
- supporte quantisation et chargement réel
- ne casse pas la cohérence host ↔ sovereign
