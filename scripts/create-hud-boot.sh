#!/bin/bash
# create-hud-boot.sh — Build llm-baremetal-hud.img with the new OO HUD v2 dashboard
#
# This creates a standalone 100MB bootable USB image that boots directly to
# the OO HUD v2 interface (futuristic elegant dashboard) without the LLM kernel.
#
# Usage (from llm-baremetal repo root):
#   bash scripts/create-hud-boot.sh
#
# Output:
#   llm-baremetal-hud.img  (100MB, bootable, USB-ready)
#
# To flash to USB:
#   sudo dd if=llm-baremetal-hud.img of=/dev/sdX bs=4M status=progress
#   (Or use Rufus on Windows in DD mode)

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DISPLAY_DIR="$(dirname "$REPO_ROOT")/desktop_display"
IMAGE="llm-baremetal-hud.img"
IMAGE_MIB=100

echo "=================================================="
echo "  OO HUD v2 Boot Image Builder"
echo "=================================================="
echo ""

# ── Step 1: Build oo_hud_v3.efi ────────────────────────────────────────────────
echo "[1/5] Building oo_hud_v3.efi..."
if [ ! -d "$DISPLAY_DIR" ]; then
    echo "ERROR: desktop_display not found at: $DISPLAY_DIR"
    exit 1
fi
(cd "$DISPLAY_DIR" && make clean 2>/dev/null || true && make)
HUD_EFI="$DISPLAY_DIR/oo_hud_v3.efi"
if [ ! -f "$HUD_EFI" ]; then
    echo "ERROR: Build failed — oo_hud_v3.efi not found"
    exit 1
fi
HUD_SIZE=$(stat -c %s "$HUD_EFI")
echo "  [OK] oo_hud_v3.efi built: $((HUD_SIZE / 1024)) KB"

# Also copy to artifacts/efi/ for reference
mkdir -p "$REPO_ROOT/artifacts/efi"
cp "$HUD_EFI" "$REPO_ROOT/artifacts/efi/oo_hud_v3.efi"
echo "  [OK] Copied to artifacts/efi/oo_hud_v3.efi"

# ── Step 2: Create blank image ─────────────────────────────────────────────────
echo ""
echo "[2/5] Creating ${IMAGE_MIB}MB blank image..."
cd "$REPO_ROOT"
rm -f "$IMAGE" 2>/dev/null || true
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMAGE_MIB status=progress
echo "  [OK] Blank image created"

# ── Step 3: GPT partition table + FAT32 ───────────────────────────────────────
echo ""
echo "[3/5] Partitioning (GPT + FAT32)..."
parted "$IMAGE" --script mklabel gpt
parted "$IMAGE" --script mkpart primary fat32 1MiB 100%
parted "$IMAGE" --script set 1 boot on
parted "$IMAGE" --script set 1 esp on
# Hybrid MBR for legacy BIOS compatibility
dd if=/usr/lib/grub/i386-pc/boot.img of="$IMAGE" bs=440 count=1 conv=notrunc 2>/dev/null \
    || echo "  (grub hybrid MBR: skipped)"
echo "  [OK] GPT + ESP partition created"

# ── Step 4: Format FAT32 + copy files ─────────────────────────────────────────
echo ""
echo "[4/5] Formatting FAT32 and copying files..."
OFFSET_BYTES=$((1024 * 1024))
MTOOLSRC_TMP="$(mktemp)"
echo "mtools_skip_check=1" > "$MTOOLSRC_TMP"
echo "drive z: file=\"${PWD}/${IMAGE}\" offset=${OFFSET_BYTES}" >> "$MTOOLSRC_TMP"
export MTOOLSRC="$MTOOLSRC_TMP"

mformat -F -v OOHUD z:

# EFI directory structure
mmd z:/EFI
mmd z:/EFI/BOOT
mcopy "$HUD_EFI" z:/EFI/BOOT/BOOTX64.EFI
echo "  [OK] Copied EFI/BOOT/BOOTX64.EFI (OO HUD v2)"

# Convenience copy at root
mcopy "$HUD_EFI" z:/OO_HUD.EFI
echo "  [OK] Copied OO_HUD.EFI"

# startup.nsh — auto-boot in UEFI shell (fallback)
printf "echo -off\r\nFS0:\r\nFS0:\\EFI\\BOOT\\BOOTX64.EFI\r\necho.\r\necho OO HUD returned.\r\n" > /tmp/startup.nsh
mcopy /tmp/startup.nsh z:/startup.nsh
rm -f /tmp/startup.nsh
echo "  [OK] Copied startup.nsh"

# README on the partition (visible when USB is mounted on Windows)
cat > /tmp/OO_README.TXT << 'EOF'
================================================
  OPERATING ORGANISM — Boot USB
  OO HUD v2 — Futuristic Dashboard Interface
================================================

Boot this USB on any x64 UEFI machine to see
the OO bare-metal intelligence interface.

No OS required. Boots directly from UEFI.

Controls:
  Arrow keys  — Move cursor
  F1          — HELP
  ENTER       — Execute command
  ESC         — Back to desktop

Shell commands:
  STATUS      — System status
  ZONES       — Memory zone map
  NODES       — Node status
  BOOT_LOG    — Boot log
  CLEAR       — Clear shell
  REBOOT      — Warm reboot

github.com/Djiby-diop/llm-baremetal
EOF
mcopy /tmp/OO_README.TXT z:/OO_README.TXT
rm -f /tmp/OO_README.TXT
echo "  [OK] Copied OO_README.TXT"

rm -f "$MTOOLSRC_TMP"

# ── Step 5: Finalize ──────────────────────────────────────────────────────────
echo ""
echo "[5/5] Finalizing..."
IMG_FINAL_SIZE=$(ls -lh "$IMAGE" | awk '{print $5}')
echo ""
echo "=================================================="
echo "  OO HUD v2 Boot Image Ready!"
echo "=================================================="
echo ""
echo "  File: $REPO_ROOT/$IMAGE"
echo "  Size: $IMG_FINAL_SIZE"
echo ""
echo "  Flash to USB (Linux):"
echo "    sudo dd if=$IMAGE of=/dev/sdX bs=4M status=progress"
echo ""
echo "  Flash to USB (Windows - Rufus):"
echo "    1. Open Rufus"
echo "    2. Select USB drive"
echo "    3. SELECT -> $IMAGE"
echo "    4. Partition scheme: GPT"
echo "    5. Target system: UEFI (non CSM)"
echo "    6. START (DD image mode)"
echo ""
echo "  Test in QEMU:"
echo "    bash scripts/run-qemu.sh --image $IMAGE --mem 512"
echo ""
