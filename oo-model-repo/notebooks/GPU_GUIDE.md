# Guide GPU — OO-Native Training

## Ton setup actuel
- CPU: Intel HD Graphics 620 (pas de NVIDIA GPU)
- Pour l'entraînement: **Google Colab (gratuit)** ou **Kaggle (gratuit)**

---

## Option A — Google Colab (recommandé)

1. **Uploader oo-model sur Google Drive**
   - Compresser `C:\Users\djibi\OneDrive\Bureau\baremetal\oo-model`
   - Uploader sur `Mon Drive > oo-model`

2. **Ouvrir le notebook**
   - Aller sur https://colab.research.google.com
   - `Fichier > Importer le notebook > Google Drive > oo-model/notebooks/train_colab.ipynb`

3. **Activer le GPU**
   - `Runtime > Changer le type de runtime > GPU T4`

4. **Exécuter toutes les cellules** (Run All)
   - ~30 min pour 5000 steps sur T4

5. **Récupérer les fichiers**
   - `checkpoints/oo-native-v1/oo_native_v1.pt`  — modèle complet
   - `checkpoints/oo-native-v1/oo_native_v1.bin` — format bare-metal
   - `checkpoints/oo-native-v1/tokenizer.json`   — tokenizer OO

---

## Option B — Kaggle

1. Créer un dataset Kaggle avec le dossier `oo-model`
2. Créer un nouveau notebook Kaggle
3. Monter le dataset + exécuter les mêmes commandes
4. GPU P100 disponible gratuitement (30h/semaine)

---

## Après l'entraînement

```bash
# Copier le binaire dans llm-baremetal
cp oo_native_v1.bin ../llm-baremetal/models/

# Mettre à jour repl.cfg
echo "model=oo_native_v1.bin" >> ../llm-baremetal/repl.cfg

# Rebuild EFI
cd ../llm-baremetal && make all
```

---

## Estimation temps / ressources

| Config       | Params | Steps | T4 (~)  | P100 (~) |
|--------------|--------|-------|---------|----------|
| dry-run      | 4.6M   | 20    | 2 min   | 1 min    |
| full small   | 4.6M   | 5000  | 25 min  | 15 min   |
| full 16M     | 16M    | 5000  | 60 min  | 35 min   |

---

## Extension dataset (recommandé avant full training)

Le dataset actuel = 24 samples (trop petit).
Avant de lancer le full training, ajouter des données dans `scripts/build_dataset.py`:
- Plus d'exemples système OO (docs, code du projet)
- Extraits de `llm-baremetal` / `oo-host` / `oo-system`
- Exemples de raisonnement D+ / sentinel / pressure

Cible minimum : **500+ samples** pour un modèle cohérent.
