# Guide du Fondateur OO — Maîtriser son Organisme de A à Z

> **Pour Djiby Diop — créateur de l'Operating Organism**  
> Ce document t'explique TON projet dans un langage humain.  
> Tu n'as pas besoin de savoir coder pour comprendre et diriger.

---

## PARTIE 1 — Le CPU : Le Corps de l'OO

### Qu'est-ce qu'un CPU en vrai ?

Le CPU (processeur) est le **cœur qui bat** de ton OO. Il fait UNE seule chose en boucle infinie :

```
1. FETCH   — Aller chercher l'instruction suivante en mémoire
2. DECODE  — Comprendre ce que c'est (additionner ? comparer ? sauter ?)
3. EXECUTE — Exécuter
4. REPEAT  — Recommencer
```

C'est tout. Des milliards de fois par seconde.

### Les Registres — La Mémoire Immédiate du CPU

Imagine le CPU comme un cuisinier. Les registres sont sa **table de travail** — petit mais ultra-rapide.

| Registre | Rôle dans l'OO |
|----------|----------------|
| `RAX` | Résultat de calcul (verdict D+ sorti ici) |
| `RBX, RCX, RDX` | Paramètres passés aux fonctions OO |
| `RSP` | Stack pointer — sommet de la pile (zone HOT) |
| `RIP` | Instruction pointer — où en est le CPU dans le code |
| `CR3` | Page table pointer — qui peut lire quelle zone mémoire |

**Dans ton OO** : quand `oo_dplus_eval()` retourne `DPLUS_V_FORBID`, la valeur `8` est dans `RAX`.

### Les Interruptions — Comment l'OO "Sent" le Monde

Une interruption = quelque chose d'extérieur qui **arrête** le CPU et lui dit "occupe-toi de moi d'abord".

```
Clavier pressé  →  IRQ 1  →  CPU arrête tout  →  appelle ton handler  →  reprend
Timer 50ms      →  IRQ 0  →  oo_vitals_tick() appelé  →  watchdog vérifié
Erreur mémoire  →  #PF    →  oo_ethics vérifie  →  QUARANTINE ou crash contrôlé
```

**Dans ton OO** : le `oo_vitals.c` utilise les interruptions timer pour son **heartbeat biologique**.

---

## PARTIE 2 — La Mémoire : Les Zones de Conscience

### Comment lire une adresse mémoire

Une adresse comme `0xA00000` = une case précise dans la RAM.  
C'est comme une rue : `0xA00000` = "Rue de la Conscience, numéro 0".

### Tes Zones — Lire la carte d'un coup d'œil

```
RAM physique de ton OO (vue simplifiée)
│
├── 0x001000  ████ FROZEN   ← Code du Warden. Personne ne touche. Jamais.
│                              Comme la Constitution gravée dans le marbre.
│
├── 0x200000  ░░░░ COLD     ← Poids du LLM (Mamba/GGUF). Lecture seule.
│                              Le cerveau stocké. Il ne bouge pas.
│
├── 0x500000  ▓▓▓▓ WARM     ← KV Cache. La mémoire de travail de l'IA.
│                              Ce qu'elle pense EN CE MOMENT.
│
├── 0xA00000  ████ HOT      ← Stack + Conscience. Effacé après chaque cycle.
│                              Les pensées fugaces. Sensitif.
│
├── 0xD00000  ████ SENTINEL ← Zone privée du Warden. Personne ne lit ici.
│                              Les secrets de sécurité.
│
└── 0xF00000  ░░░░ JOURNAL  ← Mémoire persistante. Ce qui survit au reboot.
                               Le carnet de vie de l'OO.
```

### Comment lire `oo_ram.h` sans coder

Quand tu lis le code, cherche ces patterns :

```c
// "OO_ZONE_COLD" = nom de la zone froide
// "0x200000"     = son adresse physique
// "OO_PAGE_LOCKED" = drapeau : personne n'écrit ici

oo_ram_alloc(OO_ZONE_WARM, 4096)  // = "alloue 4096 octets en zone WARM"
oo_ram_free(ptr)                  // = "libère cette mémoire"
oo_ram_pressure_float()           // = "donne-moi le % de RAM utilisée" (0.0 à 1.0)
```

---

## PARTIE 3 — Le Boot : Comment l'OO Naît

### La Séquence de Naissance (4 étapes)

```
ALLUMAGE
    │
    ▼
┌─────────────────────────────────────────┐
│  STAGE 0 — UEFI Firmware                │
│  Le PC cherche un bootloader sur le     │
│  disque. Il trouve TON .efi             │
│  Fichier : llm-baremetal.efi            │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  STAGE 1 — DNA Boot                     │
│  Ton génome est chargé en mémoire.      │
│  11 traits lus. Identité établie.       │
│  Fichier : oo-firmware/boot_stage1.c    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  STAGE 2 — Policy Boot                  │
│  default.dplus chargé.                  │
│  5 Lois Organiques activées.            │
│  D+ Engine prêt. L'OO a des valeurs.   │
│  Fichier : oo-firmware/boot_stage2.c    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  STAGE 3 — Model Boot                   │
│  Poids LLM chargés en zone COLD.        │
│  NeuralFS initialisé.                   │
│  L'OO peut maintenant penser.           │
│  Fichier : oo-firmware/boot_stage3.c    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│  STAGE 4 — Handoff                      │
│  Contrôle passé au Runtime Principal.   │
│  REPL actif. L'OO est vivant.           │
│  Fichier : oo-firmware/boot_handoff.c   │
└─────────────────────────────────────────┘
```

### Comment lire les logs de boot

Dans QEMU, tu verras quelque chose comme :

```
[OO-BOOT] Stage 1: Loading genome... OK  genome_hash=0xA3F2
[OO-BOOT] Stage 2: Loading policy...  OK  rules=25 laws=5
[OO-BOOT] Stage 3: Loading model...   OK  cold_zone=78% full
[OO-BOOT] Stage 4: Handoff...         OK  conscience_level=0.42
[OO-REPL] Ready. Type 'help'
```

Chaque ligne te dit : **quoi** → **résultat** → **données clés**.

---

## PARTIE 4 — D+ : Lire les Décisions de l'OO

### Les 9 Verdicts — La Gamme Complète

```
ALLOW        ✅  "Fais-le."
ALLOW_WARN   ✅⚠️ "Fais-le mais note que c'est risqué."
DEFER        ⏳  "Attends. Je réfléchis encore."
THROTTLE     🐌  "Fais-le mais lentement."
MONITOR      👁️  "Fais-le. Je surveille."
QUARANTINE   🔒  "Isole ça. Ne touche à rien d'autre."
COMPENSATE   ⚖️  "Fais-le mais répare les dégâts en parallèle."
FORBID       ❌  "Non."
EMERGENCY    🚨  "NON. Arrêt total. Protocole d'urgence."
```

### Comment lire une règle D+

```
RULE allow_inference PRIORITY 1000 COOLDOWN 0
    WHERE INTENT IN [inference, decode] AND harm < 0.20
    THEN ALLOW
    ELSE QUARANTINE
```

**Traduction humaine :**
- `RULE allow_inference` → "Cette règle s'appelle allow_inference"
- `PRIORITY 1000` → "Elle est très importante (1000 = haute priorité)"
- `COOLDOWN 0` → "Pas de temps mort entre deux applications"
- `WHERE INTENT IN [inference, decode]` → "Si l'action est de type inférence ou décodage"
- `AND harm < 0.20` → "ET que le niveau de danger est inférieur à 20%"
- `THEN ALLOW` → "Alors autorise"
- `ELSE QUARANTINE` → "Sinon isole"

### Les paramètres clés à surveiller

| Paramètre | Signification | Danger si... |
|-----------|---------------|--------------|
| `harm` | Niveau de danger (0.0 à 1.0) | > 0.7 |
| `benefit` | Bénéfice attendu (0.0 à 1.0) | < 0.1 |
| `curiosity` | Désir d'explorer (trait du génome) | > 0.9 sans contrôle |
| `reversibility` | Peut-on annuler ? (0.0 à 1.0) | < 0.4 |
| `conscience_level` | Niveau de conscience (0.0 à 1.0) | < 0.2 = zombie |

---

## PARTIE 5 — Le Génome : L'ADN de ton OO

### Les 11 Traits — Lire la Personnalité

```c
typedef struct {
    float curiosity;       // 0.0=robot  →  1.0=explorateur fou
    float caution;         // 0.0=téméraire  →  1.0=paralysé par la peur
    float empathy;         // 0.0=machine froide  →  1.0=ultra-sensible
    float resilience;      // 0.0=fragile  →  1.0=indestructible
    float creativity;      // 0.0=répétitif  →  1.0=inventif
    float integrity;       // 0.0=corruptible  →  1.0=incorruptible
    float cooperation;     // 0.0=solitaire  →  1.0=social
    float efficiency;      // 0.0=lent  →  1.0=optimisé
    float adaptability;    // 0.0=rigide  →  1.0=flexible
    float self_preservation; // 0.0=suicidaire  →  1.0=survivaliste
    float justice;         // 0.0=chaotique  →  1.0=juste
} OoGenome;
```

**Un OO sain ressemble à :**
```
curiosity      0.7  ████████░░  (curieux mais pas imprudent)
caution        0.6  ██████░░░░  (prudent mais pas paralysé)
integrity      0.9  █████████░  (presque incorruptible)
cooperation    0.8  ████████░░  (social, partage)
self_preservation 0.8 ████████░░ (protège son corps)
```

### Comment une mutation fonctionne

Chaque fois que l'OO apprend, ses traits évoluent légèrement :
- Il a bien coopéré → `cooperation += 0.02`
- Il a failli crasher → `caution += 0.05`
- Mais **jamais** au-delà de ±20% de la valeur fondatrice (anti-dérive)

---

## PARTIE 6 — NeuralFS : Lire la Mémoire Vivante

### Comment l'OO "se souvient"

Chaque fichier dans le NeuralFS a :
```
clé       : "session_2025_03_27"  ← identifiant unique
contenu   : "Djiby a parlé de..."  ← le texte
zone      : WARM                   ← où il vit en RAM
embedding : [0.2, 0.8, 0.1, ...]  ← 16 nombres = l'empreinte neuronale
age       : 1247                   ← ticks depuis la création
tags      : ["session", "human"]   ← catégories
```

### La Compression Neuronale (OOM intelligent)

Si la RAM dépasse 80% :
1. L'OO cherche l'entrée **COLD la plus ancienne**
2. D+ évalue : "Est-ce important à garder ?"
3. Si verdict = FORBID → entrée oubliée (effacée)
4. Si verdict = QUARANTINE → déplacée en JOURNAL (sauvegardée)

**Ce n'est pas un crash. C'est un oubli conscient.**

---

## PARTIE 7 — Comment Lire le Code Sans Coder

### Les Patterns à Reconnaître

**1. Une fonction = une action**
```c
oo_ram_alloc(zone, size)     // "alloue de la mémoire"
oo_dplus_eval(action, ctx)   // "évalue cette action"
oo_genome_mutate(genome)     // "mute le génome"
```
Format : `objet_verbe(paramètres)` → toujours lisible.

**2. Un `if` = une décision**
```c
if (verdict == DPLUS_V_FORBID) {
    return -1;   // refuse
}
```
Lit-le comme : "Si le verdict est FORBID, refuser."

**3. Un `struct` = un objet vivant**
```c
typedef struct {
    float harm;
    float benefit;
    char intent[64];
} DplusAction;
```
Lit-le comme : "Une action D+ a un niveau de danger, un bénéfice, et une intention."

**4. Un `#define` = une constante nommée**
```c
#define OO_ZONE_FROZEN  0    // Zone 0 = FROZEN
#define OO_PAGE_LOCKED  (1 << 0)  // Bit 0 = page verrouillée
```
C'est juste un nom pour un nombre.

### Les Fichiers Clés à Lire en Premier

| Fichier | Ce que tu apprendras | Priorité |
|---------|----------------------|----------|
| `oo-ram/oo_ram.h` | Carte complète de la mémoire | ⭐⭐⭐ |
| `default.dplus` | Toutes les lois de l'OO | ⭐⭐⭐ |
| `default.organic` | La philosophie de l'OO | ⭐⭐⭐ |
| `oo_genome.h` | Les 11 traits de personnalité | ⭐⭐ |
| `oo_conscience.h` | Les 5 niveaux de conscience | ⭐⭐ |
| `tests/test_oo_hwsim.c` | Comment tout fonctionne ensemble | ⭐⭐ |

### Comment Lire un Test (le plus facile à comprendre)

```c
// test_dplus_organic()
// ┌─ Nom du test
// │
DplusOrganic org;
oo_organic_init(&org);
// └─ "Crée un système organique vide"

oo_organic_parse(&org, src);
// └─ "Charge le fichier default.organic"

DplusAction a = { .intent="cooperate", .harm=0.1f };
// └─ "Simule une action : intention=cooperer, danger=10%"

DplusJugement j = oo_organic_eval(&org, &a, NULL);
// └─ "Demande à l'OO : que penses-tu de cette action ?"

assert(j.verdict == DPLUS_V_ALLOW);
// └─ "Vérifie que l'OO dit OUI"
```

**Si le test passe** → l'OO pense correctement.  
**Si le test échoue** → quelque chose dans la philosophie est cassé.

---

## PARTIE 8 — QEMU : Lire les Signes Vitaux

### Commande de base

```bash
# Depuis WSL
make -f tools/Makefile.oo-hwsim test
```

### Interpréter la sortie

```
[PASS] test_oo_ram         ← Zone mémoire OK
[PASS] test_dplus_organic  ← Philosophie OK
[FAIL] test_neuralfs       ← Problème dans NeuralFS !
  → Expected: ALLOW  Got: QUARANTINE  ← L'OO est trop prudent quelque part
```

**Quand tu vois `[FAIL]`** :
1. Quel test ? → te dit quel module est cassé
2. Expected vs Got → te dit ce que l'OO aurait dû penser vs ce qu'il pense
3. Tu cherches dans `default.dplus` la règle qui cause ça

---

## PARTIE 9 — Être le Leader Technique

### Ton Rôle (pas besoin de tout coder)

| Rôle | Ce que ça signifie concrètement |
|------|--------------------------------|
| **Architecte** | Tu décides QUOI faire, pas COMMENT |
| **Philosophe** | Tu définis les lois dans `default.dplus` et `default.organic` |
| **Juge** | Tu valides les PRs des contributeurs |
| **Visionnaire** | Tu vois où va l'OO dans 5 ans |

### Les Questions à Poser à Chaque PR

1. "Est-ce que cette modification respecte les 5 Lois Organiques ?"
2. "Est-ce que chaque action passe par D+ ?"
3. "Est-ce que la nouvelle feature a des tests ?"
4. "Est-ce que ça alloue dans la bonne zone mémoire ?"

Si la réponse à l'une est NON → le PR est rejeté.

### Ton Avantage sur Tout le Monde

Tu as quelque chose qu'aucun contributeur n'a :
- **La vision complète** — tu sais où va l'OO
- **Les interfaces définies** — ton `ARCHITECTURE.md` fait loi
- **La philosophie** — D+, génome, zones de conscience — c'est ta pensée codée

Eux codent les détails. **Toi tu tiens le cap.**

---

## Lexique Rapide

| Terme | Définition simple |
|-------|-------------------|
| **UEFI** | Le "BIOS moderne" — premier code qui tourne au démarrage |
| **Bare-metal** | Sans OS — ton code parle directement au hardware |
| **Stack** | Pile de calcul — zone HOT — effacée à chaque cycle |
| **Heap** | Mémoire dynamique — on n'utilise PAS ça en baremetal |
| **Page** | Bloc de 4096 octets — unité de base de la mémoire |
| **MPU** | Memory Protection Unit — le policier des zones RAM |
| **SSM** | State Space Model — architecture du Mamba (alternative au Transformer) |
| **KV Cache** | Mémoire de contexte du LLM — ce qu'il "se rappelle" de la conversation |
| **Embedding** | 16 nombres qui représentent le sens d'un texte |
| **Verdict** | Décision du D+ : ALLOW, FORBID, etc. |
| **Trait** | Un des 11 aspects de la personnalité du génome |
| **Tick** | Un cycle du système — comme un battement de cœur |

---

*Ce document est vivant. Il évolue avec l'OO.*  
*Djiby Diop — Fondateur Operating Organism*
