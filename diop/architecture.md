# DIOP — System Architecture

> Architecture technique cible du projet DIOP, pensée pour passer d'une vision conceptuelle à une implémentation modulaire.

---

## 1. Vue d'ensemble

DIOP est un système d'intelligence distribuée organisé autour d'un orchestrateur central et d'un ensemble de sous-systèmes spécialisés.

Les cinq blocs majeurs sont :

* `Core` pour comprendre, planifier et piloter
* `Workers` pour exécuter les capacités spécialisées
* `Memory` pour conserver l'historique utile
* `Validation` pour garder l'humain dans la boucle
* `Evolution` pour améliorer les futures exécutions

L'objectif n'est pas seulement de produire une réponse, mais de produire une réponse traçable, améliorable et réutilisable.

Dans la cible OO, DIOP devient plus qu'un orchestrateur logiciel : il devient le centre evolutif du projet.

Cela signifie qu'il doit pouvoir :

* comprendre un utilisateur, son metier, ses preferences et son mode de travail
* concevoir et faire evoluer l'environnement qui lui est destine
* coder des parties du systeme avec approbation humaine
* apprendre en continu a partir des interactions, validations, erreurs et habitudes observees
* faire emerger un assistant personnel local specifique a chaque utilisateur

---

## 2. Architecture logique globale

```text
User
  |
  v
Intent Parser
  |
  v
Task Planner
  |
  v
Dispatcher -----> Worker Layer
  |                  |-- Analysis Worker
  |                  |-- Architecture Worker
  |                  |-- Code Worker
  |                  |-- Refactor Worker
  |                  `-- Science Worker
  |
  v
Aggregator
  |
  v
Validation Layer
  |
  +------> Memory Layer
  |
  `------> Evolution Layer
  |
  v
Final Output
```

---

## 2.1 Architecture cible etendue

Au-dela du pipeline logiciel de base, la cible finale ajoute quatre couches structurantes :

* `Runtime Gateway` pour exposer le noyau local et ses capacites d'inference
* `User Profile Layer` pour representer le metier, les preferences et les habitudes d'un utilisateur
* `Personal Twin Layer` pour faire emerger une intelligence locale propre a cet utilisateur
* `System Evolution Layer` pour permettre a DIOP de proposer, preparer et ecrire des evolutions du systeme sous validation humaine

Vue simplifiee :

```text
User
  |
  v
Profile Capture -----> Personal Twin Memory
  |                         |
  v                         v
DIOP Core -----------> Runtime Gateway -----------> Local Model Runtime
  |                         |
  |                         +------> External Intelligence Connectors
  |
  +------> Project Memory
  +------> Validation Layer
  +------> Evolution Layer
  |
  `------> System Writer (human-approved)
```

---

## 3. Core Orchestrator

Le `Core` est le cerveau de coordination. Il ne produit pas nécessairement tout le contenu lui-même ; il décide surtout comment le travail doit être structuré et exécuté.

### Responsabilités

* interpréter la demande initiale
* produire un objectif structuré
* découper le travail en sous-tâches
* sélectionner les workers appropriés
* séquencer ou paralléliser l'exécution
* agréger les résultats
* envoyer les sorties en validation
* orchestrer la personnalisation utilisateur
* décider quand solliciter un moteur local, un moteur externe ou un worker interne
* piloter l'evolution du systeme tout en respectant les approbations humaines

### Sous-composants

#### 3.1 Intent Parser

Transforme une entrée libre en représentation exploitable.

Exemples de champs extraits :

* intention principale
* domaine du problème
* contraintes techniques
* niveau de risque
* niveau d'ambiguïté
* type de livrable attendu

#### 3.2 Task Planner

Convertit l'intention en plan exécutable.

Sorties typiques :

* liste de sous-tâches
* dépendances
* ordre recommandé
* mode d'exécution
* critères de validation

#### 3.3 Dispatcher

Attribue chaque tâche au meilleur worker disponible selon :

* le type de tâche
* les capacités du worker
* le coût estimé
* la qualité historique
* le mode actif Solar ou Lunar

#### 3.4 Aggregator

Fusionne les sorties workers en un résultat cohérent.

Il doit :

* résoudre les conflits de réponses
* produire une synthèse lisible
* rattacher les artefacts
* signaler les risques restants

---

## 4. Worker Layer

Les workers doivent rester interchangeables. Chacun suit un contrat commun d'entrée et de sortie.

### 4.1 Contrat d'entrée

Chaque worker reçoit :

* un `task_id`
* un `task_type`
* un contexte métier
* des contraintes
* des critères de réussite
* un niveau d'autonomie

### 4.2 Contrat de sortie

Chaque worker renvoie :

* un statut
* un résumé
* des artefacts
* des risques
* des recommandations
* une demande éventuelle de validation humaine

### 4.3 Workers initiaux

#### Analysis Worker

Usage :

* clarification des besoins
* extraction des règles métier
* détection de contradictions

Entrées typiques :

* brief initial
* documents projet
* contraintes produit

Sorties typiques :

* problem statement
* liste de besoins
* hypothèses explicites
* zones d'incertitude

#### Architecture Worker

Usage :

* conception système
* définition des modules
* contrats d'interface
* stratégie de déploiement

Sorties typiques :

* architecture logique
* architecture technique
* composants
* flux de données
* décisions structurantes

#### Code Worker

Usage :

* génération de code
* création de services
* endpoints API
* interfaces utilisateur
* scripts et automatisations

Sorties typiques :

* fichiers source
* tests
* documentation technique

#### Refactor Worker

Usage :

* amélioration de structure
* réduction de complexité
* optimisation de performance
* alignement avec conventions

#### Science Worker

Usage :

* cas expérimentaux
* recherche avancée
* prototypes non standards
* exploration d'algorithmes

---

## 5. Memory Architecture

La mémoire est un composant de premier ordre. Elle ne doit pas être un simple dépôt de logs, mais une base de connaissance réutilisable.

### 5.1 Types de mémoire

#### Project Memory

Contient :

* contexte du projet
* historique des tâches
* décisions majeures
* livrables associés

#### Pattern Memory

Contient :

* solutions réutilisables
* templates efficaces
* architectures fréquentes
* stratégies de résolution

#### Error Memory

Contient :

* erreurs passées
* causes probables
* remédiations validées
* conditions de réapparition

#### Decision Memory

Contient :

* choix structurants
* raisons du choix
* alternatives écartées
* validations humaines reçues

#### User Profile Memory

Contient :

* métier ou rôle de l'utilisateur
* préférences d'interface et de bureau
* habitudes de travail
* contraintes personnelles ou métier
* outils favoris

#### Personal Twin Memory

Contient :

* comportements observés et validés
* styles de réponse préférés
* décisions récurrentes
* routines apprises
* limites explicites imposées par l'utilisateur

### 5.2 Stockage recommandé

Approche hybride :

* documents JSON ou Markdown pour la traçabilité lisible
* base structurée pour requêtes fiables
* index vectoriel pour rappel sémantique

### 5.3 Règles mémoire

* ne stocker que l'utile
* distinguer faits, hypothèses et décisions
* versionner les changements majeurs
* lier les mémoires aux tâches sources
* séparer la mémoire projet de la mémoire personnelle
* ne jamais fusionner un profil utilisateur sans consentement explicite
* permettre l'oubli, l'effacement et la réinitialisation d'un profil ou d'un double

---

## 6. Validation Layer

Le système doit intégrer une validation explicite avant toute action sensible ou clôture finale à impact élevé.

### Rôle

* revue des sorties
* contrôle des risques
* arbitrage des conflits
* injection de feedback humain

### Cas où la validation est obligatoire

* décision d'architecture majeure
* suppression ou modification importante de code
* action irréversible
* réponse à forte incertitude
* changement de stratégie d'évolution
* écriture ou réécriture du système OO lui-même
* modification d'un profil utilisateur ou de son double opératoire
* délégation d'une action autonome au double personnel

### Sortie de validation

La validation doit pouvoir produire :

* `approved`
* `approved_with_changes`
* `rejected`
* `needs_more_analysis`

---

## 7. Evolution Engine

Le moteur d'évolution ne doit pas auto-modifier le système sans garde-fous. Son rôle initial est d'observer, mesurer et recommander.

### Fonctions

* calculer des scores par worker
* mesurer la qualité des plans
* détecter les patterns gagnants
* recommander des améliorations de routage
* proposer des ajustements de stratégie

### Sources de signal

* validations humaines
* taux de réutilisation
* taux d'erreur
* corrections post-génération
* temps de résolution

### Boucle d'évolution

1. exécution d'une tâche
2. collecte des sorties
3. validation humaine
4. scoring
5. stockage en mémoire
6. mise à jour des préférences d'orchestration

Dans la cible finale, cette boucle d'évolution alimente aussi :

* l'évolution du profil utilisateur
* l'entraînement progressif du double personnel
* les propositions d'évolution du système OO

---

## 7.1 Personal Twin Layer

Le `Personal Twin Layer` est la couche qui permet a DIOP de faire emerger une intelligence locale propre a un utilisateur donne.

### Objectif

Former une intelligence specialisee qui :

* comprend le contexte individuel de l'utilisateur
* apprend son style de travail
* assiste ses activites quotidiennes
* peut reprendre certaines taches avec son accord
* reste sous supervision du noyau DIOP et de l'humain

### Capacites attendues

* personnalisation du bureau et des workflows
* assistance metier specialisee
* memorisation des preferences individuelles
* adaptation continue a partir des validations
* delegation encadree de certaines routines

### Exemples

* un architecte demande un bureau centre sur ses plans, ses outils et ses revues
* un entrepreneur demande un espace de travail oriente strategie, suivi, priorites et execution
* un comptable demande un environnement structure autour des chiffres, verifications et ecritures
* un joueur peut disposer d'un assistant local qui analyse, aide, prepare ou s'entraine selon ses preferences

---

## 7.2 System Writer Layer

Le `System Writer Layer` est la couche par laquelle DIOP peut proposer, preparer et ecrire des changements dans le systeme OO.

### Rôle

* transformer une intention d'evolution en taches techniques
* ecrire ou modifier du code systeme
* proposer des migrations d'architecture
* garder chaque changement lisible, traçable et validable

### Contraintes

* aucune écriture critique sans approbation humaine
* chaque modification doit pouvoir être revue
* le système doit conserver un historique des changements proposés, refusés et approuvés

---

## 8. Dual Mode Engine

Le système fonctionne avec deux profils d'exécution complémentaires.

### 8.1 Solar Mode

Utilisé quand on cherche :

* rapidité
* exploration
* diversité de solutions
* génération créative

Caractéristiques :

* plans plus larges
* plus de variantes
* seuil de filtrage plus souple

### 8.2 Lunar Mode

Utilisé quand on cherche :

* précision
* réduction du risque
* cohérence forte
* consolidation technique

Caractéristiques :

* plans plus stricts
* validation plus fréquente
* décisions plus conservatrices

### 8.3 Politique de bascule

Le Core peut changer de mode selon :

* criticité de la tâche
* niveau d'incertitude
* phase du cycle projet
* instructions utilisateur

---

## 9. Flux de données

### Flux standard

1. l'utilisateur soumet une demande
2. le parser extrait l'intention
3. le planner construit les tâches
4. le dispatcher assigne les workers
5. les workers produisent leurs sorties
6. l'aggregator fusionne les résultats
7. la validation humaine approuve ou corrige
8. la mémoire persiste le contexte
9. l'évolution met à jour les signaux d'amélioration

### Flux de correction

1. un résultat est rejeté
2. la cause est identifiée
3. une nouvelle tâche est créée
4. la mémoire d'erreur est mise à jour
5. une nouvelle exécution est lancée

---

## 10. Modèle de données minimal

### Task

```json
{
  "id": "task_002",
  "parent_id": "task_001",
  "type": "architecture_design",
  "goal": "Design auth service",
  "context": {
    "project": "diop",
    "stack": "python"
  },
  "constraints": [
    "must be modular",
    "must support validation"
  ],
  "mode": "lunar",
  "status": "pending"
}
```

### WorkerCapability

```json
{
  "worker": "architecture",
  "handles": [
    "architecture_design",
    "system_modeling",
    "module_definition"
  ],
  "quality_score": 0.84,
  "cost_score": 0.40
}
```

### ExecutionResult

```json
{
  "task_id": "task_002",
  "worker": "architecture",
  "status": "completed",
  "summary": "Service boundaries defined",
  "artifacts": [
    {
      "type": "document",
      "name": "auth-architecture"
    }
  ],
  "risks": [
    "identity provider not chosen"
  ],
  "needs_validation": true
}
```

---

## 11. Structure d'implémentation recommandée

```text
diop/
├── core/
│   ├── orchestrator/
│   │   ├── service
│   │   ├── policies
│   │   └── state
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
│   ├── repository/
│   ├── indexing/
│   ├── retrieval/
│   └── schemas/
├── validation/
│   ├── policies/
│   ├── review/
│   └── feedback/
├── evolution/
│   ├── scoring/
│   ├── evaluation/
│   └── strategy/
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

## 12. Séquence d'implémentation conseillée

### Étape 1

Définir les contrats communs :

* task
* result
* validation decision
* memory record

### Étape 2

Créer un orchestrateur minimal capable de :

* accepter une demande
* générer un plan simple
* appeler un worker
* retourner un résultat agrégé

### Étape 3

Ajouter la persistance :

* journal des tâches
* journal des sorties
* journal des validations

### Étape 4

Introduire plusieurs workers et le dispatcher.

### Étape 5

Ajouter scoring, feedbacks et premières règles d'évolution.

---

## 13. Principes de conception

* séparer coordination et exécution
* privilégier des contrats simples
* rendre chaque décision observable
* rendre chaque sortie retraçable
* limiter l'autonomie tant que les métriques sont faibles
* concevoir pour extension plutôt que sur-optimisation précoce

---

## 14. Risques à maîtriser

### Risque d'orchestrateur surchargé

Si le Core prend toutes les décisions fines, il devient un goulot d'étranglement.

Réponse :

* garder des politiques simples
* déléguer l'exécution réelle aux workers

### Risque de mémoire inutile

Une mémoire trop volumineuse sans structure devient rapidement inexploitable.

Réponse :

* stocker moins mais mieux
* taguer et scorer les entrées

### Risque d'évolution non contrôlée

Un moteur d'évolution sans validation peut dégrader la qualité.

Réponse :

* bloquer les changements stratégiques derrière validation humaine

### Risque de workers redondants

Des rôles mal séparés créent des duplications.

Réponse :

* définir clairement les capacités et contrats

---

## 15. Conclusion

DIOP doit être conçu comme un système d'orchestration intelligent, pas comme un modèle isolé.

Sa force vient de :

* la clarté des responsabilités
* la mémoire persistante
* la validation humaine
* l'amélioration incrémentale

Cette architecture donne une base suffisamment claire pour démarrer un vrai développement du projet.
