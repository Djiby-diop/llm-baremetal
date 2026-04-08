# oo-lab — OO Training Laboratory

Environnement Python pour entraîner et exporter les modèles OO.

## Structure

```
oo-lab/
├── notebooks/   — Jupyter notebooks d'expériences
├── datasets/    — Datasets JSONL générés (symlink → oo-model-repo/soma_dataset/)
├── models/      — Checkpoints PyTorch exportés
└── README.md
```

## Scripts principaux (dans oo-model-repo/scripts/)

| Script | Rôle | Commande |
|--------|------|---------|
| `train_soma_cortex.py` | Entraîne le cortex SomaMind | `python3 train_soma_cortex.py` |
| `train_soma_homeostatic.py` | Fine-tune homéostatique | `python3 train_soma_homeostatic.py` |
| `generate_soma_plans_dataset.py` | Génère dataset plans (378 ex) | `python3 generate_soma_plans_dataset.py` |
| `generate_math_dataset.py` | Dataset maths pour halt head | `python3 generate_math_dataset.py` |

## Modèles produits

| Fichier | Taille | Description |
|---------|--------|-------------|
| `cortex_oo.bin` | 14.9 MB | Cortex SomaMind (OOSS v1) |
| `cortex_oo_homeostatic.bin` | 14.9 MB | Version homéostatique |
| `oo_v3.bin` | 2794 MB | Mamba 2.8B (sur HuggingFace) |

## Entraînement halt head (PRIORITÉ)

Le halt head est mal calibré pour les maths (`42*37` → réponse fausse, halt_prob=0.0).
Pour corriger :

```bash
cd oo-model-repo/scripts
python3 generate_math_dataset.py      # génère dataset math
python3 train_halt_calibration.py     # fine-tune halt head
```

Envoyer `soma_dataset/plans.jsonl` + math dataset à Batterfyl pour Mamba3 fine-tune.

## Pré-requis

```bash
pip install torch transformers datasets  # Python 3.10+
```

## Connexion HuggingFace

```bash
huggingface-cli login  # token: hf_UDdAr...
huggingface-cli upload djibydiop/llm-baremetal oo_v3.bin
```
