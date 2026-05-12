#!/bin/bash
set -e
REPO=/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal
cd "$REPO"

IMG="$REPO/llm-baremetal-boot.img"
IMG_MB=512

echo "[1/4] Creating ${IMG_MB}MB image..."
dd if=/dev/zero of="$IMG" bs=1M count=$IMG_MB status=none

MTRC=$(mktemp)
printf "mtools_skip_check=1\ndrive z: file=\"%s\"\n" "$IMG" > "$MTRC"
export MTOOLSRC="$MTRC"

echo "[2/4] Formatting FAT32..."
mformat -F -v OOBIOS z:

echo "[3/4] Copying files..."
mmd z:/EFI; mmd z:/EFI/BOOT
mcopy "$REPO/llama2.efi" z:/EFI/BOOT/BOOTX64.EFI
echo "  [OK] BOOTX64.EFI ($(du -m "$REPO/llama2.efi" | cut -f1) MB)"

mcopy "$REPO/stories15M.bin" z:/stories15M.bin
echo "  [OK] stories15M.bin ($(du -m "$REPO/stories15M.bin" | cut -f1) MB)"

mcopy "$REPO/diop/engine/model/diop_model.bin" z:/diop_model.bin
echo "  [OK] diop_model.bin ($(du -m "$REPO/diop/engine/model/diop_model.bin" | cut -f1) MB)"

mcopy "$REPO/diop/engine/model/diop_architect.bin" z:/diop_architect.bin
echo "  [OK] diop_architect.bin"

[ -f "$REPO/tokenizer.bin" ] && mcopy "$REPO/tokenizer.bin" z:/tokenizer.bin && echo "  [OK] tokenizer.bin"
[ -f "$REPO/OOPOLICY.BIN"  ] && mcopy "$REPO/OOPOLICY.BIN"  z:/OOPOLICY.BIN  && echo "  [OK] OOPOLICY.BIN"

printf "model=stories15M.bin\ndiop_model=diop_model.bin\nautorun_autostart=1\nautorun_file=llmk-autorun.txt\nmax_new_tokens=32\ntemperature=0.8\n" > /tmp/oo_repl.cfg
mcopy /tmp/oo_repl.cfg z:/repl.cfg && echo "  [OK] repl.cfg"

printf "/version\n/diop_info\n/infer Once upon a time in a bare-metal world\n/thermal_status\n/organ_status\n/shutdown\n" > /tmp/oo_autorun.txt
mcopy /tmp/oo_autorun.txt z:/llmk-autorun.txt && echo "  [OK] llmk-autorun.txt"

printf "echo -off\r\nFS0:\r\nFS0:\\EFI\\BOOT\\BOOTX64.EFI\r\n" > /tmp/startup.nsh
mcopy /tmp/startup.nsh z:/startup.nsh && echo "  [OK] startup.nsh"

rm -f "$MTRC" /tmp/oo_repl.cfg /tmp/oo_autorun.txt /tmp/startup.nsh

echo "[4/4] Verify..."
MTRC2=$(mktemp)
printf "mtools_skip_check=1\ndrive z: file=\"%s\"\n" "$IMG" > "$MTRC2"
export MTOOLSRC="$MTRC2"
mdir z:/ | head -20
rm -f "$MTRC2"
echo "=== Image ready: $IMG ($(du -h "$IMG" | cut -f1)) ==="