#!/bin/bash
IMG="/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llm-baremetal-boot.img"
CFG=$(mktemp)
echo 'mtools_skip_check=1' > "$CFG"
echo "drive z: file=\"$IMG\" offset=1048576" >> "$CFG"
export MTOOLSRC="$CFG"
mdel z:/llmk-autorun.txt 2>&1 || true
mdel z:/llmk-autorun-decode-test.txt 2>&1 || true
echo "Remaining autorun files:"
mdir z:/ 2>&1 | grep -i autorun || echo "(none)"
rm "$CFG"
echo "done"
