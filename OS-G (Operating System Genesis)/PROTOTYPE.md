# Prototype — Kernel OS-G (Rust `no_std`)

Ce dossier contient un **kernel unique** sous forme d’un crate Rust `no_std` :

- allocateur pages **bitmap** (contigu)
- **capabilities** (handle + génération anti-stale)
- **quotas** par cellule (limite en octets)
- **rate limit** par cellule (limite d’allocations en octets par fenêtre de ticks)
- **TTL** (expiration)
- **D+** embarqué dans le kernel : parse/verify/consensus + mérite + juge (MVP)

## Fichiers

- `Cargo.toml`
- `src/alloc/bitmap.rs` — alloc pages bitmap
- `src/warden.rs` — gouvernance (cap table + quota + checks)
- `src/types.rs` — handles, droits, intent
- `src/dplus/*` — D+ (parser/verifier/merit/judge) *dans le core no_std*

## Tester sur machine hôte

Depuis ce dossier :

- `cargo test --features std`

## Tester sur QEMU (UEFI)

Un binaire UEFI minimal est fourni via `src/bin/osg_uefi.rs`. Il exécute un smoke-test kernel (Warden + pipeline D+) et affiche `PASS/FAIL`.

Depuis ce dossier :

- `./qemu-test.ps1 -Profile release`

### Générer WEIGHTS.BIN (optionnel)

Par défaut, le test tourne même si `weights.bin` est absent (poids simulés). Si tu veux un fichier cohérent avec `dim`/`layers`, tu peux le régénérer :

- `python ./generate_weights.py --dim 128 --layers 1 --out weights.bin`

Optionnel : inclure un header (dim/layers) reconnu par le loader UEFI :

- `python ./generate_weights.py --dim 64 --layers 6 --header --out weights.bin`

Tu peux aussi demander au harness QEMU de générer automatiquement `weights.bin` à partir de `@@SOMA:IO` :

- `./qemu-test.ps1 -Profile release -PolicySource policy-soma-dim64-layers6.dplus -AutoWeights`

Optionnel : écrire un petit header (dim/layers) dans `WEIGHTS.BIN` (supporté par le loader UEFI, et rétro-compatible avec l'ancien format brut) :

- `./qemu-test.ps1 -Profile release -PolicySource policy-soma-dim64-layers6.dplus -AutoWeights -WeightsHeader`

### Config SOMA (policy)

Le binaire UEFI charge une policy D+ depuis le disque (FAT) : `qemu-fs/policy.dplus`.

Pour tester facilement une policy alternative (ex: interactive), utilise :

- `./qemu-test.ps1 -Profile release -PolicySource policy-interactive.dplus`

Section boot optionnelle (dans la policy) :

```text
@@SOMA:IO
interactive=1
steps=32
layers=0
dim=128
weights_header=0
```

Notes :

- `steps`: clamp 1..256
- `layers`: 0..64 (0 = utiliser toutes les couches allouées)
- `dim`: clamp 16..256
- `weights_header`: 0/1 (0 = désactiver la détection header, 1 = forcer + log si invalide)

Section Warden optionnelle (dans la policy) :

```text
@@WARDEN:MEM
rate_window_ticks=10
rate_limit_bytes=4096
```

Notes :

- Le rate limit est **par cellule**; le smoke-test UEFI applique la section à la cellule `1`.
- Mettre `rate_window_ticks=0` ou `rate_limit_bytes=0` désactive le rate limit.

Précédence (host harness) :

- si `weights_header` est présent dans la policy, il override `-WeightsHeader` pour l’auto-génération.

Presets fournis :

- `policy-interactive.dplus`
- `policy-soma-dim64-layers6.dplus`
- `policy-soma-dim64-layers6-header.dplus`
- `policy-soma-dim64-layers6-noheader.dplus`
- `policy-soma-dim128-layers6.dplus`
- `policy-warden-rate-limit.dplus`

Test négatif (PASS attendu, mais header invalide ignoré) :

- `./qemu-test-bad-header.ps1 -Profile release`

Test négatif (FAIL attendu + message détaillé D+ dans le serial) :

- `./qemu-test-negative.ps1 -Profile release`

Notes :

- Le binaire UEFI est derrière la feature `uefi` (pour éviter de casser `cargo test`).
- Les outils host `dplus_*` sont derrière la feature `std`.

Options utiles :

- `-QemuPath` si QEMU n’est pas dans un chemin standard
- `-OvmfCode` si le firmware OVMF n’est pas trouvé automatiquement
- `-OvmfVars` pour fournir un template VARS alternatif

Notes :

- Le code compile en `no_std` par défaut (tests activent `std`).
- `MemoryWarden::init` est `unsafe` car il suppose une région RAM valide.
- Le paramètre const `BITMAP_WORDS` (au lieu de “max pages”) fixe la taille du bitmap : capacité = `BITMAP_WORDS * 64` pages.

## Intégration baremetal (plus tard)

- Brancher `init(base, bytes)` sur la carte mémoire UEFI/bootloader.
- Remplacer `tick()` par une source temps (TSC/APIC timer) ou un compteur monotone.
- Ajouter des zones séparées (normal vs SandRAM) via 2 allocateurs bitmap.
