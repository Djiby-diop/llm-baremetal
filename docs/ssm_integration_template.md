---
name: "🧠 SSM Engine Integration"
about: "Proposer ou contribuer un moteur d'inférence (Mamba2, Transformer, autre)"
title: "[SSM] "
labels: ["ssm-engine", "enhancement"]
---

## Moteur proposé
- [ ] Mamba2 130M
- [ ] Mamba2 2.7B
- [ ] Transformer (déconseillé pour baremetal)
- [ ] Autre : ___

## Interface implémentée
Le moteur implémente-t-il `oo_mamba_bridge.h` ?
- [ ] `oo_engine_init()`
- [ ] `oo_engine_generate()`
- [ ] `oo_engine_embed()`
- [ ] `oo_engine_set_speed()` (contrôle thermique)
- [ ] `oo_engine_verify_weights()` (intégrité CRC32)
- [ ] `oo_engine_shutdown()`

## Règles obligatoires (ARCHITECTURE.md)
- [ ] Poids dans `OO_ZONE_COLD`
- [ ] KV cache dans `OO_ZONE_WARM`
- [ ] Pas de malloc() — pools statiques uniquement
- [ ] CPUID vérifié avant AVX2 via `oo_cpuid.h`
- [ ] D+ appelé via `oo_organic_eval()` avant génération
- [ ] `-ffreestanding` compatible

## Détails techniques
```
Modèle        :
Paramètres    :
Taille weights:
Tokens/sec    : (scalar / SSE2 / AVX2)
CPUID requis  : SSE2 / AVX2 / AVX-512
```

## Fichiers fournis
- [ ] `oo_engine_generate_impl()` — implémentation réelle
- [ ] `oo_engine_embed_impl()` — embedding réel
- [ ] Tests dans `test_oo_hwsim.c`
- [ ] Script d'export des poids (`export_*.py`)

## Lien vers le fork / branch
<!-- URL GitHub -->
