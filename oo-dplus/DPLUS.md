# D+ (D‑Plus) — Esquisse d’un langage “mosaïque” pour OS atomique

## 1) Intention
D+ vise un but : **réduire l’espace des erreurs** en combinant plusieurs “facettes” (bas niveau, haut niveau, logique, policies) dans un même tissu, sans perdre la cohérence.

## 2) Le langage mosaïque (multi-facettes)
Un même artefact D+ peut contenir plusieurs zones :
- **SPEED** : primitives bas niveau (style asm/IR)
- **LOGIC** : logique haut niveau (style Rust)
- **LAW** : règles/politiques (style eBPF/verifier)
- **PROOF** : contrats/invariants (style logique formelle, assertions)

D+ n’est pas “un seul parseur” : c’est une **Unité de Traduction Universelle** qui compile chaque facette vers une IR commune.

## 3) Superpouvoir D+ : Consensus contre l’erreur
Pour une opération critique, D+ peut exiger une validation croisée :
- 2 ou 3 formulations indépendantes (ex: LAW + PROOF + LOGIC)
- la décision n’est exécutée que si elles convergent.

**Idée ajoutée : Divergence = signal**
- Si divergence, l’instruction bascule en mode “safe fallback” + log + quarantine.

## 4) Tissu de code (au lieu de fichiers séparés)
D+ peut être modélisé comme un **graphe** :
- nœuds = fonctions/lois/preuves
- arêtes = dépendances/permissions

L’ordre n’est pas “top→bottom” mais “déploiement par besoins”.

## 5) Exemple de pseudo-syntaxe (illustrative)
> (c’est une idée de notation, pas une syntaxe figée)

Deux manières de penser la “mosaïque” :

1) **Tags compacts** (proches du MVP)
- `[LAW] allow(mem.allocate) if size < quota && ttl < 2s`
- `[PROOF] invariant: no_capability ⇒ no_memory_access`
- `[SPEED] memcpy_avx2(dst, src, n)`

2) **Blocs organiques** (style “tissu multimodal”, proche de ta vision)

```dplus
[SOMA:C] {
	// muscle : électrons bruts
	register_t weights = load_raw_model(0xBFAD);
}

[PROTECT:RUST] {
	// armure : frontières mémoire
	let safe_slice = validate_memory(weights)?;
}

[FLEET:JAVA] {
	// conscience : synchronisation / distribution
	FleetManager.syncWeights(safe_slice);
}

[JUDGE:WARDEN] {
	// tribunal : si l'intention est pure, on manifeste
	if (intent == CREATION) { manifest(); }
}
```

## 6) MVP D+ (implémenté dans ce dossier)

### 6.1 Format de module (texte)
Le MVP D+ est un **document texte** avec des sections.

Il supporte deux syntaxes équivalentes :

1) **Headers** (style minimal)
- `@@TAG`

2) **Tissu / Fabric** (style vision)
- `[TAG] { ... }` (blocs avec accolades ; accolades imbriquées supportées par comptage)

Le parseur accepte **n’importe quel tag** :

- `@@LAW`
- `@@PROOF`
- `@@SPEED`
- `@@LOGIC`
- `@@LANG:python`
- `@@GPU:ptx`
- `@@<TOUT_AUTRE_TAG>`

Les tags peuvent être simples (`LAW`) ou “namespacés” (`SOMA:C`, `PROTECT:RUST`, `FLEET:JAVA`, `JUDGE:WARDEN`, etc.).

Le but est de pouvoir mettre **des milliers de sous-langages** (ou dialectes) dans un seul fichier, via des headers arbitraires.

#### Mapping “vision ↔ MVP”
Les deux formes représentent la même idée : un fichier D+ est une mosaïque de sections taggées.

- `@@SOMA:C` … est la forme simple.
- `[SOMA:C] { ... }` est la forme “tissu”, plus naturelle pour écrire du polyglotte.

Le MVP ne compile pas encore les langages contenus dans chaque bloc : il sait surtout **découper**, puis appliquer des règles strictes sur `LAW/PROOF`.

Tout ce qui suit un header appartient à la section jusqu’au prochain header.

Exemple :

```
@@LAW
allow mem.allocate op:7 if bytes < quota

@@PROOF
invariant op:7: no_capability => no_access

@@LANG:python
def helper(x):
    return x + 1

@@SPEED
mov rax, rbx
```

### 6.2 Identifiants d’opérations (consensus)
Pour activer le “vote” LAW↔PROOF, le MVP utilise un marqueur simple :

- `op:<nombre>`

Note MVP : l’extraction de `op:<id>` est un scan texte heuristique. Elle ignore ce qui suit `//` sur une ligne, ignore les lignes `#...` / `;...`, ignore aussi les commentaires bloc `/* ... */` et les chaînes (`"..."` / `'...'`), mais elle reste un parseur “simple” (pas encore une vraie grammaire/lexer).

Le vérificateur impose qu’un `op:<id>` présent dans `@@LAW` existe aussi dans `@@PROOF` (et inversement), quand le consensus est activé.

Note : le consensus MVP s’applique uniquement à `@@LAW` et `@@PROOF`. Les autres sections (`@@LANG:*`, `@@GPU:*`, etc.) sont traitées comme des blocs de contenu “opaques” dans cette première version.

### 6.3 Vérificateur (style BPF-like, minimal)
Le vérificateur du MVP (heuristique) impose :
- limites de taille (total + par section)
- limites de lignes / longueur de ligne
- rejet de tokens évidents de non-terminaison en `LAW/PROOF` (`while`, `for`, `loop`, `goto`, ...)

En plus, le MVP implémente une **validation bornée** (DoS hardening) sur certaines intentions de `@@LAW` quand elles sont exprimées de façon *déterministe* (voir ci-dessous).

Note : ces règles strictes ne sont appliquées qu’aux sections `@@LAW` et `@@PROOF`.

### 6.3.1 Grammaire MVP “fixée” (ce qui est réellement parsé)
Le MVP n’a pas (encore) une grammaire complète : il applique des scans textuels heuristiques, mais on peut quand même figer un sous-ensemble stable.

#### 6.3.1.a `op:<id>`
- Un identifiant d’opération est une occurrence de `op:<digits>` dans `@@LAW` ou `@@PROOF`.
- La casse est ignorée (`OP:7` OK).
- Les espaces autour de `:` sont autorisés (`OP : 7` OK).
- Le motif ne matche pas à l’intérieur d’identifiants (ex: `noop:7` ne compte pas).
- Le scan ignore :
	- `//` fin de ligne
	- lignes `#...` / `;...` (après espaces)
	- commentaires bloc `/* ... */` (peuvent être multi-lignes)
	- chaînes `"..."` et `'...'` (avec `\\` escapes)

#### 6.3.1.b Intention LAW: `mem.allocate`
Le MVP reconnaît des règles `mem.allocate` ligne par ligne dans `@@LAW`.

Une ligne est considérée comme une règle `mem.allocate` si, après un nettoyage simple des commentaires de ligne :
- elle contient `mem.allocate` (insensible à la casse), ET
- elle contient un `op:<id>` (obligatoire).

Champs optionnels (numériques uniquement) :
- `bytes<=<digits>` ou `bytes <= <digits>`
- `ttl_ms<=<digits>` ou `ttl_ms <= <digits>`

Notes importantes :
- Si `op:<id>` est absent, la règle est **ignorée** (pour éviter des règles ambiguës / wildcard).
- Si `bytes` / `ttl_ms` ne sont pas des nombres (ex: `bytes <= quota`), ces champs sont ignorés pour la vérification bornée.
- La détection de commentaires dans le parseur `mem.allocate` est **ligne‑niveau** (il tronque la ligne au premier `//` ou `/*`), contrairement au scan `op:` qui sait gérer `/* ... */` multi‑lignes.

Exemples reconnus :
```
@@LAW
allow mem.allocate op:7 bytes<=8192 ttl_ms<=2000 zone==sandbox
op:8 allow(mem.allocate) if bytes <= 65536 && ttl_ms <= 0
```

### 6.3.2 Bornes (verifier strict)
En mode strict (celui utilisé par défaut dans OS‑G) :
- maximum de règles `mem.allocate` parsées : 64 (au-delà ⇒ rejet)
- `bytes==0` ⇒ rejet
- `bytes` maximum : 16 MiB
- `ttl_ms` maximum : 3 600 000 (1h)

Note sur `ttl_ms==0` : c’est un cas “non expirant” (intention illimitée). Le vérificateur l’accepte, mais la **méritocratie/caps** peut le réduire si une `ttl_cap_ms` existe.

### 6.5 Extensions OS-G (implémentées)

En plus de `@@LAW` / `@@PROOF`, le noyau OS-G (UEFI demo + Warden) supporte des sections de configuration simples :

#### 6.5.1 `@@WARDEN:MEM`
Clés supportées (MVP):
- `rate_window_ticks=<digits>`
- `rate_limit_bytes=<digits>`

#### 6.5.2 `@@WARDEN:POLICY`
Contient un programme **policy VM** (stack VM) en notation RPN, sur la première ligne non-commentée.

Exemple:
```
@@WARDEN:POLICY
bytes 1048576 > deny_if_true allow
```

#### 6.5.3 `@@CORTEX:HEUR`
Clés supportées (MVP):
- `enabled={1|0|true|false|on|off}`
- `prefetch_repeat=<digits>`

Note: le cortex MVP ne “décide” pas, il **propose** uniquement des intents; la policy/warden garde le contrôle.

### 6.4 Outils
- Tests : `cargo test --features std`
- CLI (hôte) : `cargo run --features std --bin dplus_check -- <fichier>`
- Weaver (IR canonique) : `cargo run --features std --bin dplus_weaver -- <fichier>`
- Graph (Mermaid) : `cargo run --features std --bin dplus_graph -- <fichier>`
- Judge (simulation Warden) : `cargo run --features std --bin dplus_judge -- <fichier>`
- Heal (snapshot/restore + quarantaine) : `cargo run --features std --bin dplus_heal -- <fichier>`
- Replay (fingerprint journal) : `cargo run --features std --bin dplus_replay -- <fichier>`
- Merit (score + jugement) : `cargo run --features std --bin dplus_merit -- <fichier>`

Exemple polyglotte (INTENT + LAW/PROOF + organes taggés) :
- `examples/genesis.dplus`
- Commande : `cargo run --features std --bin dplus_check -- examples/genesis.dplus`

Weave (IR) sur cet exemple :
- `cargo run --features std --bin dplus_weaver -- examples/genesis.dplus`

Note : l’IR inclut aussi `merit_*` (score + politique) calculés à partir des tags.

Graphe (Mermaid) sur l’exemple tissu :
- `cargo run --features std --bin dplus_graph -- examples/genesis_fabric.dplus`

Note : le graphe inclut un nœud `MERIT` (score + politique) calculé via la même heuristique que le juge.

Jugement (simulation Warden) :
- `examples/genesis_judge.dplus`
- `cargo run --features std --bin dplus_judge -- examples/genesis_judge.dplus`

Note : `dplus_judge` calcule aussi un **profil de mérite** à partir des tags (ex: `SOMA:*`, `GPU:*`, `PROTECT:*`, `PROOF`).
- Si la LAW ne force pas explicitement `sandbox/zone`, le juge peut router par défaut en **Sandbox** selon le score.
- Si le score est faible, il applique aussi des **caps** (clamp) sur `bytes` et `ttl_ms`.
- Le juge affiche une ligne `MERIT ...` au démarrage.
- Il expose aussi `reasons=...` (ex: `SOMA,GPU` / `PROTECT,PROOF`) pour expliquer le score.
- Il écrit aussi un événement `MeritDecision` dans le journal du Warden (utile pour les empreintes/replay).

Jugement avec SandRAM (Normal vs Sandbox) :
- `examples/genesis_judge_sandbox.dplus`
- `cargo run --features std --bin dplus_judge -- examples/genesis_judge_sandbox.dplus`

Auto-réparation (quarantaine → restore snapshot) :
- `examples/genesis_heal.dplus`
- `cargo run --features std --bin dplus_heal -- examples/genesis_heal.dplus`

Replay déterministe (même trace 2×) :
- `cargo run --features std --bin dplus_replay -- examples/genesis_judge_sandbox.dplus`
- Dump du journal (événements + MeritDecision) :
	- `cargo run --features std --bin dplus_replay -- --dump-journal examples/genesis_merit_low.dplus`
	- (le dump affiche `reasons_bits` + `reasons=...`)

Méritocratie (score bas vs score haut) :
- `examples/genesis_merit_low.dplus`
- `examples/genesis_merit_high.dplus`
- `cargo run --features std --bin dplus_merit -- examples/genesis_merit_low.dplus`
- `cargo run --features std --bin dplus_merit -- examples/genesis_merit_high.dplus`

Exemple polyglotte en syntaxe tissu (`[TAG] { ... }`) :
- `examples/genesis_fabric.dplus`
- Commande : `cargo run --features std --bin dplus_check -- examples/genesis_fabric.dplus`

Exemple “jugement” (échec volontaire du consensus LAW↔PROOF) :
- `examples/genesis_fail_consensus.dplus`
- Commande : `cargo run --features std --bin dplus_check -- examples/genesis_fail_consensus.dplus`

Exemple “jugement” (rejet d’un token interdit en `@@LAW`) :
- `examples/genesis_fail_forbidden_token.dplus`
- Commande : `cargo run --features std --bin dplus_check -- examples/genesis_fail_forbidden_token.dplus`

Code :
- `src/dplus/module.rs` (parser sans allocation)
- `src/dplus/verifier.rs` (verifier + consensus)
