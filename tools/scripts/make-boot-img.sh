#!/bin/bash
# make-boot-img.sh — Build a bootable GPT/FAT32 disk image for OO
# Usage (from WSL): bash tools/scripts/make-boot-img.sh
# Usage (from PowerShell): wsl -e bash tools/scripts/make-boot-img.sh
#
# Contents of image:
#   EFI/BOOT/BOOTX64.EFI  — llama2.efi (built by Makefile)
#   models/               — OO native models (cortex_oo, diop_model, diop_architect)
#   tokenizer.bin         — BPE tokenizer
#
# Model priority (first found):
#   1. oo-model-repo/models/cortex_oo.bin  (15MB — OO native, preferred)
#   2. diop/engine/model/diop_architect.bin  (6MB — compact OO model)
#   3. diop/engine/model/diop_model.bin    (79MB — full DIOP model)

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="/tmp/oo-boot.img"
IMG_SIZE_MB=512
OFFSET=1048576   # 2048 sectors × 512 bytes = 1MB

EFI_FILE="$REPO_ROOT/llama2.efi"
TOKENIZER="$REPO_ROOT/tokenizer.bin"

# ── Sélection automatique du modèle OO natif ──────────────────────────────────
MODEL_FILE=""
for candidate in \
    "$REPO_ROOT/oo-model-repo/models/cortex_oo.bin" \
    "$REPO_ROOT/diop/engine/model/diop_architect.bin" \
    "$REPO_ROOT/diop/engine/model/diop_model.bin"; do
    if [ -f "$candidate" ]; then
        MODEL_FILE="$candidate"
        echo "  Model: $(basename $MODEL_FILE)  ($(du -sh "$MODEL_FILE" | cut -f1))"
        break
    fi
done
[ -n "$MODEL_FILE" ] || { echo "ERROR: No OO native model found (cortex_oo.bin / diop_architect.bin / diop_model.bin)"; exit 1; }
MODEL_NAME="$(basename "$MODEL_FILE")"

# Windows destination (adjust if needed)
WIN_DEST=$(echo "$REPO_ROOT" | sed 's|/mnt/c/|C:/|')
DEST_IMG="$REPO_ROOT/llm-baremetal-boot.img"

echo "=== OO Boot Image Builder ==="
echo "Repo:  $REPO_ROOT"
echo "Image: $IMG (${IMG_SIZE_MB}MB)"

# Validate inputs
[ -f "$EFI_FILE" ]    || { echo "ERROR: $EFI_FILE not found — run 'make' first"; exit 1; }
[ -f "$TOKENIZER" ]   || { echo "WARNING: $TOKENIZER not found — skipping"; TOKENIZER=""; }

# 1. Zero disk
echo "--- Creating blank image ---"
dd if=/dev/zero of="$IMG" bs=1M count="$IMG_SIZE_MB" status=progress

# 2. GPT + EFI System Partition
echo "--- Partitioning (GPT + ESP) ---"
sgdisk --clear \
       --new=1:2048:0 \
       --typecode=1:EF00 \
       --change-name=1:EFI \
       "$IMG"

# 3. FAT32 filesystem
echo "--- Formatting FAT32 ---"
mformat -i "${IMG}@@${OFFSET}" -F -v OO ::

# 4. Directory structure
echo "--- Creating directories ---"
mmd -i "${IMG}@@${OFFSET}" ::/EFI ::/EFI/BOOT ::/models

# 5. Copy files
echo "--- Copying EFI binary ($(du -sh "$EFI_FILE" | cut -f1)) ---"
mcopy -i "${IMG}@@${OFFSET}" "$EFI_FILE" ::/EFI/BOOT/BOOTX64.EFI

echo "--- Copying model: $MODEL_NAME ($(du -sh "$MODEL_FILE" | cut -f1)) ---"
mcopy -i "${IMG}@@${OFFSET}" "$MODEL_FILE" ::/models/

# Also copy all other available OO models
for extra in \
    "$REPO_ROOT/oo-model-repo/models/cortex_oo_homeostatic.bin" \
    "$REPO_ROOT/diop/engine/model/diop_warden.bin"; do
    if [ -f "$extra" ] && [ "$extra" != "$MODEL_FILE" ]; then
        echo "--- Copying extra model: $(basename $extra) ($(du -sh "$extra" | cut -f1)) ---"
        mcopy -i "${IMG}@@${OFFSET}" "$extra" ::/models/ 2>/dev/null || true
    fi
done

if [ -n "$TOKENIZER" ]; then
echo "--- Copying tokenizer ($(du -sh "$TOKENIZER" | cut -f1)) ---"
mcopy -i "${IMG}@@${OFFSET}" "$TOKENIZER" ::/tokenizer.bin
fi

# 6. Verify
echo ""
echo "=== Image contents ==="
mdir -i "${IMG}@@${OFFSET}" ::/EFI/BOOT/
mdir -i "${IMG}@@${OFFSET}" ::/models/
echo ""

# 7. Copy to repo
echo "--- Copying to $DEST_IMG ---"
cp "$IMG" "$DEST_IMG"

echo ""
echo "✓ Boot image ready: $DEST_IMG"
echo "  Launch:  .\\boot-oo.ps1 -Interactive   (graphical)"
echo "           .\\boot-oo.ps1                (headless, shows UART log)"
