---
name: "🐛 Bug Report"
about: "Signaler un bug (crash, verdict incorrect, zone mémoire corrompue)"
title: "[BUG] "
labels: ["bug"]
---

## Module concerné
- [ ] `oo_dplus` / `oo_dplus_organic` / `oo_dplus_runtime`
- [ ] `oo_genome` / `oo_conscience`
- [ ] `oo_neuralfs` / `oo_repl` / `oo_bus`
- [ ] `oo-ram` / `oo_cpuid` / `oo_mamba_bridge`
- [ ] `oo_ssm_infer` — SSM kernel
- [ ] Boot / UEFI firmware

## Environnement
```
OS/Plateforme :
Compilateur   : gcc / clang
Mode          : Host WSL / QEMU-UEFI / Hardware
```

## Comportement attendu vs observé
<!-- attendu → observé -->

## Reproduction
```bash
make -f tools/Makefile.oo-hwsim test
# Output :
```

## Loi Organique violée ?
- [ ] LOI 0 — Bien commun
- [ ] LOI 1 — Non-nuisance
- [ ] LOI 2 — Transparence
- [ ] LOI 3 — Réversibilité
- [ ] LOI 4 — Dignité
- [ ] Aucune — bug technique
