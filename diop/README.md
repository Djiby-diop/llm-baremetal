# DIOP — Distributed Intelligence Operating Platform

> DIOP est une plateforme d'intelligence distribuée capable de comprendre une intention, planifier un travail, déléguer l'exécution à des workers spécialisés, mémoriser les décisions, puis améliorer ses futures exécutions avec validation humaine.

---

## Vision

DIOP n'est pas un simple chatbot ni un modèle unique.

Le projet vise à construire un système capable de :

* transformer une demande en plan exécutable
* produire architecture, code, analyse et refactorisation
* conserver une mémoire projet durable
* apprendre de ses succès, erreurs et validations
* rester sous contrôle humain à chaque étape critique

En pratique, DIOP agit comme un atelier logiciel orchestré par une intelligence centrale.

Mais sa vocation finale est plus large : devenir le centre cognitif du projet OO, capable de faire evoluer le systeme lui-meme, d'ecrire son propre environnement avec approbation humaine, de personnaliser l'experience de chaque utilisateur, et de faire emerger une intelligence locale specifique a cette personne.

Dans cette vision :

* DIOP ne sert pas seulement a generer du code, il sert a faire evoluer tout le projet
* l'humain garde le dernier mot sur les changements critiques, les permissions et les validations
* chaque utilisateur peut decrire son metier, son style de travail, son bureau ideal, ses outils et ses habitudes
* DIOP traduit ensuite ces preferences en systeme reel : interface, workflows, assistants, automatisations, outils et comportements
* DIOP doit aussi pouvoir collaborer avec d'autres intelligences externes quand c'est utile, tout en gardant un noyau local autonome
* a terme, DIOP doit pouvoir creer un "double operatoire" local pour chaque utilisateur : une intelligence entrainee sur ses habitudes, son contexte, ses objectifs et ses modes d'action

Ce double n'est pas concu comme une copie decorative. Il doit devenir un assistant specialise capable de :

* assister l'utilisateur dans son domaine
* apprendre son langage de travail
* reprendre certaines taches avec son accord
* preparer, simuler ou prolonger son activite quand il n'est pas disponible
* rester aligne sur son profil, son role, ses contraintes et ses intentions

---

## Objectifs

### Objectif principal

Construire un noyau d'orchestration IA qui coordonne plusieurs capacités spécialisées pour produire des systèmes logiciels de manière structurée.

### Objectifs secondaires

* standardiser la décomposition des tâches
* isoler les responsabilités par worker
* introduire une mémoire exploitable entre sessions
* rendre l'évolution mesurable et contrôlée
* permettre une intégration progressive avec l'écosystème OO

---

## Positionnement

DIOP se situe entre :

* un orchestrateur multi-agents
* une usine logicielle assistée par IA
* une couche de raisonnement et de coordination au-dessus d'outils d'exécution

Le système doit pouvoir piloter des tâches allant de la simple génération de code jusqu'à la conception complète d'un produit logiciel.

---

## Piliers du système

### 1. Core Orchestrator

Le Core reçoit l'intention utilisateur, produit une représentation structurée de la demande, planifie les étapes et pilote les workers.

### 2. Worker Layer

Chaque worker remplit une fonction précise. Cela évite qu'un seul modèle essaie de tout faire en même temps.

### 3. Memory Layer

La mémoire permet de conserver le contexte projet, les patterns utiles, les décisions prises et les erreurs rencontrées.

### 4. Validation Layer

Les points sensibles restent soumis à validation humaine pour préserver sécurité, cohérence et alignement.

### 5. Evolution Layer

Les résultats, feedbacks et métriques servent à améliorer les stratégies d'orchestration et de sélection des workers.

---

## Workers initiaux

### Analysis Worker

Responsable de :

* reformuler le besoin
* extraire les contraintes
* identifier les risques
* produire un brief exploitable

### Architecture Worker

Responsable de :

* proposer une structure système
* définir modules, interfaces et flux
* recommander les choix techniques
* expliciter les compromis

### Code Worker

Responsable de :

* générer du code
* créer des services, API, interfaces ou scripts
* respecter les contrats définis par l'architecture
* proposer des tests associés

### Refactor Worker

Responsable de :

* améliorer la lisibilité
* réduire la dette technique
* optimiser performances et structure
* harmoniser les conventions

### Science Worker

Responsable de :

* explorer des solutions expérimentales
* simuler des approches avancées
* soutenir les cas non standards
* servir de laboratoire d'innovation

---

## Modes d'intelligence

### Solar Mode

Mode orienté exploration :

* génération rapide
* ouverture aux variantes
* créativité
* expansion de solution

### Lunar Mode

Mode orienté profondeur :

* audit
* validation
* consolidation
* optimisation

Le Core doit pouvoir passer de l'un à l'autre selon :

* la complexité
* le niveau de risque
* l'étape du pipeline
* la préférence utilisateur

---

## Cas d'usage visés

* concevoir une architecture backend ou full-stack
* produire un plan d'implémentation détaillé
* générer un module logiciel complet
* analyser et refactoriser un code existant
* construire une base de connaissance technique projet
* itérer sur un système en conservant l'historique décisionnel
* construire un environnement de travail personnalise pour un profil metier donne
* faire emerger un assistant local specialise pour un utilisateur precis
* faire cooperer un noyau local avec des intelligences externes quand une tache l'exige
* assister un utilisateur pendant ses activites professionnelles, creatives ou ludiques

---

## MVP recommandé

Le MVP de DIOP devrait rester simple et testable.

### Fonctionnalités MVP

* une entrée utilisateur unique
* un orchestrateur central
* trois workers minimum : analysis, architecture, code
* un format de tâche standardisé
* une mémoire locale persistante
* une validation humaine avant sortie finale
* une journalisation complète des décisions

### Ce que le MVP ne doit pas encore faire

* auto-créer de nouveaux workers
* auto-modifier ses propres règles sans validation
* dépendre d'une infrastructure distribuée complexe
* promettre une autonomie totale

---

## Architecture logique

Le fonctionnement cible suit ce pipeline :

1. réception de la demande utilisateur
2. parsing de l'intention
3. génération d'un plan de tâches
4. choix des workers
5. exécution des sous-tâches
6. agrégation des sorties
7. validation humaine
8. enregistrement mémoire
9. mise à jour des signaux d'évolution

Pour plus de détails, voir [architecture.md](C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\diop\architecture.md).

---

## Structure projet proposée

```text
diop/
├── README.md
├── architecture.md
├── core/
│   ├── orchestrator/
│   ├── planner/
│   ├── dispatcher/
│   ├── aggregator/
│   └── contracts/
├── workers/
│   ├── analysis/
│   ├── architecture/
│   ├── code/
│   ├── refactor/
│   └── science/
├── memory/
│   ├── project/
│   ├── patterns/
│   ├── errors/
│   └── decisions/
├── evolution/
│   ├── evaluator/
│   ├── scorer/
│   └── optimizer/
├── validation/
│   └── human_interface/
├── modes/
│   ├── solar/
│   └── lunar/
└── integration/
    ├── oo/
    ├── compiler/
    ├── lab/
    └── cpu/
```

---

## Contrats de base

Pour rester modulaire, chaque composant doit échanger via des structures simples.

### Exemple de Task

```json
{
  "id": "task_001",
  "goal": "Create authentication API",
  "type": "code_generation",
  "context": {
    "stack": "nodejs",
    "constraints": ["jwt", "postgres"]
  },
  "priority": "high",
  "requested_by": "core"
}
```

### Exemple de Worker Result

```json
{
  "task_id": "task_001",
  "worker": "code",
  "status": "completed",
  "artifacts": [
    {
      "type": "source_code",
      "path": "services/auth/index.ts"
    }
  ],
  "summary": "Authentication service generated",
  "risks": [
    "Token rotation not implemented"
  ],
  "needs_validation": true
}
```

---

## Roadmap de développement

### Phase 1

Poser le socle :

* définir les contrats de données
* implémenter le Core minimal
* brancher les workers essentiels
* stocker les exécutions localement

### Phase 2

Ajouter la mémoire :

* persistance des tâches et sorties
* indexation des patterns utiles
* conservation des erreurs récurrentes
* réutilisation du contexte passé

### Phase 3

Introduire l'évaluation :

* scoring des sorties
* qualité par worker
* suivi des validations humaines
* optimisation des stratégies de routage

### Phase 4

Étendre l'écosystème :

* intégrations externes
* workers additionnels
* exécution parallèle
* scénarios OO avancés

### Phase 5

Personnalisation utilisateur :

* capture des preferences de bureau et de travail au premier demarrage
* generation d'environnements adaptes au profil utilisateur
* memoire personnelle separee de la memoire projet
* premiers assistants specialises par metier

### Phase 6

Double operatoire local :

* creation d'un profil cognitif utilisateur
* apprentissage progressif des habitudes, decisions et comportements
* delegation encadree de certaines taches a ce double
* simulation et assistance contextuelle dans le systeme OO

### Phase 7

Evolution du systeme par DIOP :

* DIOP devient capable de proposer des evolutions du systeme lui-meme
* le code systeme peut etre ecrit ou modifie par DIOP avec approbation humaine
* la personnalisation, l'orchestration et l'auto-evolution convergent dans un seul noyau

---

## Principes de conception

* modularité avant complexité
* interfaces stables entre composants
* observabilité native
* mémoire utile plutôt qu'accumulation brute
* validation humaine sur les décisions sensibles
* évolution pilotée par métriques et non par intuition seule

---

## État actuel

Le projet est à un stade de cadrage architectural.

La prochaine étape concrète consiste à transformer cette documentation en squelette d'implémentation :

* contrats communs
* orchestrateur minimal
* workers de base
* stockage local de la mémoire

---

## Stockage (C: vs D:)

DIOP peut écrire des artefacts "lourds" (registry de modèles, caches, etc.) dans un *data root* configurable.

Variables utiles :

* `DIOP_DATA_ROOT` : racine de données (recommandé pour déporter sur `D:\...`)
* `DIOP_PREFER_D=1` : si `DIOP_DATA_ROOT` n'est pas défini, tenter `D:\diop` (Windows)
* `DIOP_MODEL_DIR` : dossier où chercher les modèles locaux (GGUF/BIN)
* `DIOP_NATIVE_MODEL` : chemin direct vers un fichier modèle (prioritaire)
* `DIOP_NATIVE_MODEL_NAME` : nom d'un modèle enregistré via `diop models add`

Exemple (PowerShell) :

```powershell
$env:DIOP_DATA_ROOT = "D:\diop"
$env:DIOP_MODEL_DIR = "D:\diop\models"
python -m diop models add tinyllama-q4km "D:\diop\models\tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
python -m diop run "Hello" --adapter native
```

Note: dans un environnement restreint (ex: sandbox), DIOP peut stocker la registry localement dans le repo sous `llm-baremetal/.diop_data/`.

---

## Runtime Gateway

DIOP inclut un serveur HTTP local pour exposer son moteur d'inference, son registre de modeles et sa surface de chat sous une API stable, avec un backend remplaçable (mock aujourd'hui, native demain).

```powershell
python -m diop gateway serve --host 127.0.0.1 --port 11434 --adapter mock
```

Endpoints (v0.1) :

* `GET /api/tags`
* `GET /api/health`
* `GET /api/ps`
* `GET /api/blueprints`
* `GET /api/runtime`
* `GET /api/profile`
* `GET /api/twin`
* `GET /api/session?id=<session-id>`
* `GET /api/system/proposals`
* `POST /api/generate`
* `POST /api/chat`
* `POST /api/show`
* `POST /api/create`
* `POST /api/load`
* `POST /api/unload`
* `POST /api/reset`
* `POST /api/pull` (stub local: "ensure registered")
* `POST /api/session/clear`
* `POST /api/boot/setup`
* `POST /api/profile/set`
* `POST /api/profile/clear`
* `POST /api/system/propose`
* `POST /api/system/patch`
* `POST /api/system/approve`
* `POST /api/system/reject`
* `POST /api/system/apply`
* `POST /api/delete`
* `POST /v1/chat/completions` (compat OpenAI minimal)

Les blueprints permettent de creer un modele logique DIOP a partir d'un modele de base, d'un system prompt et d'un template local.

```powershell
python -m diop models create planner-lab --from-model tinyllama-q4km --system "You are a planner."
python -m diop models show planner-lab
python -m diop models blueprints
python -m diop gateway doctor
python -m diop boot setup --role architecte --workspace-style atelier --focus plans
python -m diop profile set --role architecte --workspace-style atelier --focus plans
python -m diop profile twin
python -m diop system propose --title "Workspace boot setup" --goal "Prepare a personalized first boot flow"
python -m diop system patch proposal-123 --patch-file .\change.diff
python -m diop system approve proposal-123
python -m diop system apply proposal-123
```

DIOP commence aussi a porter une couche de profil utilisateur et de double local :

* `python -m diop boot setup --role ...`
* `python -m diop profile show`
* `python -m diop profile set --role ...`
* `python -m diop profile twin`

La commande `boot setup` represente le debut du flux "premier demarrage" : elle capture le role, le style de bureau, les axes de travail et les preferences, puis initialise le profil, seed le double local et cree une proposition systeme `pending` pour construire l'environnement personnalise sous validation humaine.

Quand un profil actif existe, le gateway injecte un bloc `[personal_context]` dans les prompts de generation et de chat. Ce bloc contient le role, le style de bureau, les axes de travail, les preferences et les premiers marqueurs du double local. Cela permet deja a DIOP de repondre comme noyau personnalise, meme avant que le moteur natif soit complet.

Le gateway peut aussi conserver un historique de conversation local avec `session`, pour permettre un chat continu cote DIOP.
Il maintient aussi un etat runtime local pour les modeles charges, visible via `GET /api/ps`, avec l'adapter actif, le statut et la residence memoire.
`GET /api/runtime` expose en plus les compteurs du pool et le diagnostic du moteur natif.
Chaque slot runtime conserve aussi une strategie de chargement et des resumes de preparation (`load_strategy`, `load_stage`, `inspect_summary`, `plan_summary`, `prepare_summary`).
`GET /api/health`, `POST /api/reset` et `POST /api/session/clear` servent de couche d'administration rapide du gateway.

La commande `system` sert de premier garde-fou pour l'evolution du projet par DIOP. Elle enregistre des propositions de changement avec objectif, resume, fichiers touches, niveau de risque et statut. Une proposition commence toujours en `pending`; l'humain peut ensuite la passer en `approved` ou `rejected`.

System Writer v2 ajoute un cycle de patch controle : DIOP peut attacher un diff unifie a une proposition, mais `system apply` refuse de l'ecrire tant que la proposition n'est pas approuvee. Le moteur d'application limite volontairement les patchs aux changements texte simples et bloque les chemins dangereux.

---

## Direction

Le but final de DIOP est de devenir une intelligence d'orchestration capable de :

* comprendre
* structurer
* produire
* retenir
* s'améliorer

sans perdre le contrôle humain ni la lisibilité du système.

A terme, DIOP doit aussi etre capable de :

* coder une partie croissante du systeme OO
* servir d'interface premiere entre l'utilisateur et son environnement numerique
* personnaliser le systeme selon le profil, le metier et le style de vie de l'utilisateur
* former une intelligence locale propre a chaque personne
* faire evoluer le projet sans casser la supervision humaine
