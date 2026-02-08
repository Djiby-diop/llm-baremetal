# Suite des améliorations (roadmap)

**Contexte actuel**
- UI simplifiée : interface.h avec overlay **off** par défaut (clean boot)
- Release `boot-tinyllama-2026-02-08` publiée avec TinyLlama 1.1B Q8_0
- Cosmopolitan toolchain bootstrap intégré (tools/) mais non utilisé encore
- Harness QEMU autorun stable + matrix bench pour validation

---

## 1. Stabilité & diagnostics

### 1.1 Mode diagnostic boot
- **But** : vérifier rapidement l'environnement avant d'exécuter un modèle.
- **Commande REPL** : `/diag` ou flag autorun `diag=1`
- **Affiche** :
  - Résolution GOP (pixels, framebuffer)
  - Mémoire disponible (avant/après chargement modèle)
  - Build-id (timestamp)
  - Chemins modèles détectés (root + models\\)
  - CPU capabilities (AVX2, FMA si détectable)

### 1.2 Screenshot QEMU (option)
- Capturer un screenshot depuis QEMU pendant autorun (`screendump`)
- Utile pour CI/documentation (montre l'interface réelle)

### 1.3 Smoke boot automatisé
- Script qui démarre QEMU headless et vérifie arrivée au prompt (timeout 60s)
- Valide que l'image boot sans crash

---

## 2. UX / Interface

### 2.1 Overlay configurable (repl.cfg)
- Déjà implémenté : `overlay=0/1` dans repl.cfg
- Ajouter un mode "minimal" : juste barre de progression, pas de warp stars
- Éviter que l'overlay écrase le texte ConOut

### 2.2 Alias FAT 8.3 visibles
- Quand un long nom est copié (tinyllama-1.1b-chat-v1.0.Q8_0.gguf → TINYLL~1.GGU), afficher clairement l'alias court
- Commande `/models` : montrer les deux noms (long + 8.3 si différent)

### 2.3 Splash optionnel
- Actuellement : `llm2.png` → `splash.bmp` (1024×1024 24-bit BMP)
- Ajouter flag `show_splash=0` dans repl.cfg pour skip (boot plus rapide)

---

## 3. Performance & matmul

### 3.1 Optimisation AVX2 actuelle
- Déjà utilisé : djiblas_avx2.c (FMA + vectorisé)
- Benchmark : runs avec `bench-matrix.ps1` documentent tok/s

### 3.2 Profiling granulaire
- Ajouter des markers au niveau attention/FFN
- Identifier si matmul reste le bottleneck ou si c'est I/O (copy from/to KV cache)

### 3.3 Support Q4_K / Q5_K (GGUF)
- Actuellement supporté : Q4_0/Q4_1/Q5_0/Q5_1/Q8_0
- K-variants nécessitent dequant + lookup complexe (peut attendre)

---

## 4. Modèles & formats

### 4.1 Multi-modèles runtime
- Permettre de charger plusieurs modèles sans reboot
- Commande `/load <file>` décharge l'ancien et charge le nouveau

### 4.2 Tokenizer externe (optionnel)
- Actuellement : tokenizer.bin obligatoire
- Support pour tokenizers alternatifs (e.g. Llama3 tokenizer)

### 4.3 Format .safetensors (futur)
- Support Rust/safetensors si besoin
- Mais probablement overkill pour baremetal

---

## 5. Test & CI

### 5.1 Matrix automatisé (GitHub Actions)
- Tourner `bench-matrix.ps1` en CI WSL
- Publish artefacts : logs + tok/s metrics

### 5.2 Regression suite
- Smoke tests pour modes : smoke, ram, gguf_smoke, gen, q8bench
- Fail si crash ou mauvais tok/s (threshold)

### 5.3 USB réel (optionnel)
- Si test sur hardware physique, documenter les résultats

---

## 6. Documentation & ecosystem

### 6.1 Quickstart amélioré
- README : ajouter section troubleshooting (FAT 8.3, model not found, no output)
- Vidéo ou GIF : démo boot QEMU → chat

### 6.2 Exemples repl.cfg
- Profils prédéfinis : "tiny" (256 ctx), "medium" (1024 ctx), "debug" (overlay=1, verbose=1)

### 6.3 Blog post / article
- "Running a 1B LLM on bare-metal UEFI x86_64"
- Couvre : llama2.c, GGUF, AVX2 matmul, zones allocator, OO runtime

---

## Priorités

**High** :
- Mode diag
- Smoke boot automatisé
- Alias FAT 8.3 visibles

**Medium** :
- Multi-modèles runtime
- Matrix CI automation
- Quickstart amélioré

**Low** :
- K-variants GGUF (seulement si demandé)
- Safetensors support
- Screenshot QEMU

---

**Date** : 2026-02-08  
**Build** : `f3d53a2` (ui: revert to simple interface.h)  
**Next** : tests hardware sur Release `boot-tinyllama-2026-02-08`
