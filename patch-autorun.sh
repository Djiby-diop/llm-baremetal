#!/bin/bash
REPO=/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal
IMG="$REPO/llm-baremetal-boot.img"
MTRC=$(mktemp)
printf "mtools_skip_check=1\ndrive z: file=\"%s\"\n" "$IMG" > "$MTRC"
export MTOOLSRC="$MTRC"
printf "/version\n/diop_info\n/fed_status\n/irq_status\n/thermal_status\n/organ_status\n/lora_status\n/evol_status\n/shutdown\n" > /tmp/oo_autorun.txt
mcopy -o /tmp/oo_autorun.txt z:/llmk-autorun.txt
rm -f "$MTRC" /tmp/oo_autorun.txt
echo "Autorun patched (no /infer)"