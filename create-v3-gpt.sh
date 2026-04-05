#!/bin/bash
set -e

echo "========================================"
echo " OO USB v3 Image ? GPT + ESP (UEFI)"
echo "========================================"

IMG="/mnt/c/Temp/oo_usb_v3_gpt.img"
EFI="/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2.efi"
MODEL="/mnt/c/Temp/oo_v3.bin"
REPO="/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal"

# Check files
for f in "$EFI" "$MODEL"; do
    if [ ! -f "$f" ]; then echo "MISSING: $f"; exit 1; fi
done

MODEL_MB=$(( $(stat -c %s "$MODEL") / 1024 / 1024 ))
echo "[1/6] Model: ${MODEL_MB} MB"

# Image size: model + 100MB slack
IMG_MB=$(( MODEL_MB + 150 ))
echo "[2/6] Creating ${IMG_MB} MB image..."
dd if=/dev/zero of="$IMG" bs=1M count=$IMG_MB status=progress

# GPT partition table
echo ""
echo "[3/6] GPT partition table..."
parted "$IMG" --script mklabel gpt
parted "$IMG" --script mkpart primary fat32 1MiB 100%
parted "$IMG" --script set 1 boot on
parted "$IMG" --script set 1 esp on

# MBR bootcode for compat
dd if=/usr/lib/grub/i386-pc/boot.img of="$IMG" bs=440 count=1 conv=notrunc 2>/dev/null || true

# Format FAT32 at partition offset (1MiB = 1048576 bytes)
echo "[4/6] Formatting FAT32..."
OFFSET=1048576
MTRC=$(mktemp)
echo "mtools_skip_check=1" > "$MTRC"
echo "drive z: file=\"${IMG}\" offset=${OFFSET}" >> "$MTRC"
export MTOOLSRC="$MTRC"
mformat -F -v OOBIOS z:

# Copy files
echo ""
echo "[5/6] Copying files..."
mmd z:/EFI
mmd z:/EFI/BOOT
mcopy "$EFI" z:/EFI/BOOT/BOOTX64.EFI
echo "  [OK] BOOTX64.EFI ($(( $(stat -c %s "$EFI") / 1024 / 1024 )) MB)"

mcopy "$MODEL" z:/oo_v3.bin
echo "  [OK] oo_v3.bin (${MODEL_MB} MB)"

# Tokenizer ? check for GPT-NeoX first, then fallback
if [ -f "${REPO}/gpt_neox_tokenizer.bin" ]; then
    mcopy "${REPO}/gpt_neox_tokenizer.bin" z:/gpt_neox_tokenizer.bin
    echo "  [OK] gpt_neox_tokenizer.bin"
else
    # Extract from old image if possible
    OLD="/mnt/c/Temp/oo_usb_v3.img"
    OLDRC=$(mktemp)
    echo "mtools_skip_check=1" > "$OLDRC"
    echo "drive y: file=\"${OLD}\"" >> "$OLDRC"
    MTOOLSRC_BAK="$MTOOLSRC"
    export MTOOLSRC="$OLDRC"
    mcopy "y:/gpt_neox_tokenizer.bin" /tmp/gpt_neox_tokenizer.bin 2>/dev/null && {
        export MTOOLSRC="$MTRC"
        mcopy /tmp/gpt_neox_tokenizer.bin z:/gpt_neox_tokenizer.bin
        echo "  [OK] gpt_neox_tokenizer.bin (extracted from old image)"
        rm -f /tmp/gpt_neox_tokenizer.bin
    } || {
        export MTOOLSRC="$MTRC"
        echo "  [WARN] No GPT-NeoX tokenizer found"
    }
    rm -f "$OLDRC"
    export MTOOLSRC="$MTRC"
fi

# Regular tokenizer too (fallback)
if [ -f "${REPO}/tokenizer.bin" ]; then
    mcopy "${REPO}/tokenizer.bin" z:/tokenizer.bin
    echo "  [OK] tokenizer.bin (fallback)"
fi

# repl.cfg
cat > /tmp/repl.cfg << 'REPLEOF'
autorun_autostart=1
autorun_file=llmk-autorun.txt
max_new_tokens=64
temperature=0.7
REPLEOF
mcopy /tmp/repl.cfg z:/repl.cfg
echo "  [OK] repl.cfg"

# autorun
cat > /tmp/autorun.txt << 'AREOF'
/ssm_load oo_v3.bin
/ssm_infer The future of AI is
AREOF
mcopy /tmp/autorun.txt z:/llmk-autorun.txt
echo "  [OK] llmk-autorun.txt"

# startup.nsh
printf "echo -off\r\nFS0:\r\nFS0:\\EFI\\BOOT\\BOOTX64.EFI\r\necho.\r\necho BOOTX64.EFI returned.\r\npause\r\n" > /tmp/startup.nsh
mcopy /tmp/startup.nsh z:/startup.nsh
echo "  [OK] startup.nsh"

# Splash
if [ -f "${REPO}/splash.bmp" ]; then
    mcopy "${REPO}/splash.bmp" z:/splash.bmp
    echo "  [OK] splash.bmp"
fi

# Policy
if [ -f "${REPO}/policy.dplus" ]; then
    mcopy "${REPO}/policy.dplus" z:/policy.dplus
    echo "  [OK] policy.dplus"
fi
if [ -f "${REPO}/OOPOLICY.BIN" ]; then
    mcopy "${REPO}/OOPOLICY.BIN" z:/OOPOLICY.BIN
    echo "  [OK] OOPOLICY.BIN"
fi

# Cleanup
rm -f /tmp/repl.cfg /tmp/autorun.txt /tmp/startup.nsh "$MTRC"

echo ""
echo "[6/6] Verifying..."
MTRC2=$(mktemp)
echo "mtools_skip_check=1" > "$MTRC2"
echo "drive z: file=\"${IMG}\" offset=${OFFSET}" >> "$MTRC2"
export MTOOLSRC="$MTRC2"
mdir z:/
echo ""
fdisk -l "$IMG"
rm -f "$MTRC2"

echo ""
echo "========================================"
echo " IMAGE READY: $IMG"
echo " Size: $(ls -lh "$IMG" | awk '{print $5}')"
echo "========================================"
echo ""
echo "Flash with Rufus:"
echo "  1. Select USB drive"
echo "  2. SELECT ? $(echo $IMG | sed 's|/mnt/c|C:|; s|/|\\|g')"
echo "  3. Partition: GPT"
echo "  4. Target: UEFI (non CSM)"
echo "  5. START"