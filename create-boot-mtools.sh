#!/bin/bash
# create-boot-mtools.sh — Build minimal FAT32 boot image for QEMU testing
# Usage: ./create-boot-mtools.sh [output.img]
set -e

REPO="$(cd "$(dirname "$0")" && pwd)"
IMG="${1:-${REPO}/llm-baremetal-boot.img}"
EFI="${REPO}/llama2.efi"
IMG_MB=256

if [ ! -f "$EFI" ]; then
    echo "ERROR: llama2.efi not found. Run 'make' first." >&2
    exit 1
fi

echo "[1/4] Creating ${IMG_MB} MB FAT32 image: $IMG"
dd if=/dev/zero of="$IMG" bs=1M count=$IMG_MB status=none

echo "[2/4] Formatting FAT32..."
MTRC=$(mktemp)
echo "mtools_skip_check=1"      > "$MTRC"
echo "drive z: file=\"${IMG}\"" >> "$MTRC"
export MTOOLSRC="$MTRC"
mformat -F -v OOBIOS z:

echo "[3/4] Copying files..."
mmd z:/EFI
mmd z:/EFI/BOOT
mcopy "$EFI" z:/EFI/BOOT/BOOTX64.EFI
echo "  [OK] BOOTX64.EFI ($(( $(stat -c %s "$EFI") / 1024 / 1024 )) MB)"

# startup.nsh — auto-launches EFI on UEFI shell startup
printf "echo -off\r\nFS0:\r\nFS0:\\EFI\\BOOT\\BOOTX64.EFI\r\n" > /tmp/oo_startup.nsh
mcopy /tmp/oo_startup.nsh z:/startup.nsh
echo "  [OK] startup.nsh"

# repl.cfg — no-model REPL config
cat > /tmp/oo_repl.cfg << 'EOF'
autorun_autostart=0
max_new_tokens=64
temperature=0.7
EOF
mcopy /tmp/oo_repl.cfg z:/repl.cfg
echo "  [OK] repl.cfg"

# autorun for smoke test — exercises all Phase 4-6 subsystems
cat > /tmp/oo_autorun.txt << 'EOF'
/version
/diop_info
/fed_status
/mbedtls_status
/mmu_status
/sched_status
/gpu_status
/sc_status
/irq_status
/thermal_status
/lora_status
/evol_status
/organ_status
/usb_status
/shutdown
EOF
mcopy /tmp/oo_autorun.txt z:/llmk-autorun.txt
echo "  [OK] llmk-autorun.txt (smoke test autorun)"

# Optional: policy and tokenizer
[ -f "${REPO}/policy.dplus"          ] && mcopy "${REPO}/policy.dplus"          z:/policy.dplus   && echo "  [OK] policy.dplus"
[ -f "${REPO}/OOPOLICY.BIN"          ] && mcopy "${REPO}/OOPOLICY.BIN"          z:/OOPOLICY.BIN   && echo "  [OK] OOPOLICY.BIN"
[ -f "${REPO}/tokenizer.bin"         ] && mcopy "${REPO}/tokenizer.bin"         z:/tokenizer.bin  && echo "  [OK] tokenizer.bin"
[ -f "${REPO}/gpt_neox_tokenizer.bin"] && mcopy "${REPO}/gpt_neox_tokenizer.bin" z:/gpt_neox_tokenizer.bin && echo "  [OK] gpt_neox_tokenizer.bin"

rm -f "$MTRC" /tmp/oo_startup.nsh /tmp/oo_repl.cfg /tmp/oo_autorun.txt

echo "[4/4] Verifying..."
MTRC2=$(mktemp)
echo "mtools_skip_check=1"      > "$MTRC2"
echo "drive z: file=\"${IMG}\"" >> "$MTRC2"
export MTOOLSRC="$MTRC2"
mdir z:/EFI/BOOT/
rm -f "$MTRC2"

echo ""
echo "=== Boot image ready: $IMG ($(ls -lh "$IMG" | awk '{print $5}')) ==="
