# OO-SomaMind Runtime Contract

> Contrat runtime bare-metal pour `OO-SomaMind V1`.

---

## 1. Règle V1

Le runtime bare-metal actuel exécute `OO-SomaMind V1` via un backbone `MAMB`.

Règle d'identité :

`OO-SomaMind` est le coeur interne natif d'OO.
Le backbone `MAMB` actuel est le support d'exécution de la V1, pas une redéfinition de l'identité du système.

En V1 :

- `MAMB` = format exécutable par le runtime SSM actuel ;
- `OOSS` = extension OO future ou sidecar optionnel.
- un modèle tiers supplémentaire = moteur attaché optionnel, distinct du coeur interne.

---

## 2. Interface existante

Le chemin runtime déjà supporté est :

- `/ssm_load <file.mamb>`
- `/ssm_infer <text>`
- `/ssm_reset`

Le squelette runtime OO expose aussi désormais :

- `/core_load <file.mamb>`
- `/oo_sidecar <file.ooss>`
- `/oo_sidecar_unload`
- `/mind_halt_probe [loop_pos]`
- `/mind_halt_decide [loop_pos] [threshold]`
- `/mind_halt_sweep [start] [end] [step] [threshold]`
- `/mind_halt_policy [threshold] [on|off]`
- `/mind_halt_policy_save`
- `/mind_halt_policy_load`
- `/mind_halt_policy_apply_saved`
- `/mind_halt_policy_reset`
- `/mind_halt_policy_diff`
- `/attach_load <file>`
- `/attach_unload`
- `/mind_status`

État actuel du sidecar `OOSS` :

- validation d'un header minimal ;
- conservation du blob validé en mémoire ;
- exposition d'un premier hook `HaltingHead` via vues mémoire sur le layout exporté ;
- arrêt anticipé best-effort des boucles de décodage actives quand `halt_prob >= threshold_configuré` ;
- politique runtime configurable (`on/off`, seuil) via `/mind_halt_policy` ;
- persistance explicite dans `repl.cfg` via `/mind_halt_policy_save` et rechargement via `/mind_halt_policy_load` ;
- ré-application explicite de la politique sauvegardée depuis `repl.cfg` via `/mind_halt_policy_apply_saved` ;
- stockage `repl.cfg` via `mind_halt_enabled` et `mind_halt_threshold` ;
- restauration explicite des valeurs runtime V1 via `/mind_halt_policy_reset` ;
- visualisation de la divergence éventuelle runtime vs `repl.cfg` via `/mind_status` ;
- affichage explicite de l'écart runtime vs persistant via `/mind_halt_policy_diff` ;
- pas encore de chargeur sémantique complet des poids/tables OO.

Le chargeur primaire est :

- `engine/ssm/mamba_weights_load()`

Le moteur d'inférence primaire est :

- `engine/ssm/ssm_infer.c`

---

## 3. Ce que le runtime attend

Le runtime V1 attend un backbone Mamba complet avec :

- `embed`
- `in_proj`
- `conv_weight`
- `conv_bias`
- `x_proj`
- `dt_proj_weight`
- `dt_proj_bias`
- `A_log`
- `D`
- `out_proj`
- `norm_weight`
- `final_norm`
- `lm_head`

Le header attendu est défini par `MambaFileHeader`.

---

## 4. Implication pour OO-SomaMind

Le script `oo-model/scripts/export_ssm_binary.py` ne suffit pas encore à lui seul pour alimenter ce runtime, car il produit un format `OOSS` partiel enrichi OO.

Pour l'intégration V1, `oo-model` doit produire un export `MAMB` compatible avec le chargeur bare-metal actuel.

Conséquence d'architecture :

- le runtime doit traiter `OO-SomaMind` comme le `core mind` ;
- les autres modèles doivent être traités plus tard comme `attach models` ;
- la disparition d'un modèle attaché ne doit pas détruire la continuité du coeur OO.

---

## 5. Évolution prévue

Étape suivante côté runtime :

- conserver `MAMB` comme contrat d'exécution stable ;
- ajouter plus tard un sidecar OO (`OOSS`) pour :
  - `HaltingHead`
  - budgets par domaine
  - métadonnées tool-use
  - paramètres OO-SomaMind.

Puis ajouter un plan runtime séparé pour :

- `core model`
- `attach model`
- arbitrage de priorité entre coeur interne et moteur externe.

Le sidecar `OOSS` est maintenant reconnu au niveau squelette par validation d'un header minimal (`magic`, `version`, `d_model`, `n_layer`, `vocab_size`, `halting_head_d_input`), maintien du blob en mémoire, et exposition d'un premier hook `HaltingHead`, sans chargement sémantique complet encore.

---

## 6. Loi runtime

Ne pas casser `MAMB` pour introduire `OO-SomaMind`.

Faire d'abord tourner le coeur.
Enrichir ensuite la cognition OO.
