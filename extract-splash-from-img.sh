#!/usr/bin/env bash
set -euo pipefail

# Extract splash.bmp from a boot image created by create-boot-mtools.sh
# Usage:
#   ./extract-splash-from-img.sh [image] [out]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

IMG="${1:-llm-baremetal-boot.img}"
OUT="${2:-artifacts/splash-from-img.bmp}"

if [ ! -f "$IMG" ]; then
  echo "ERROR: image not found: $IMG" >&2
  exit 1
fi

OFF=$((1024 * 1024))
MT="$(mktemp)"
trap 'rm -f "$MT"' EXIT

echo "mtools_skip_check=1" > "$MT"
echo "drive z: file=\"$SCRIPT_DIR/$IMG\" offset=$OFF" >> "$MT"
export MTOOLSRC="$MT"

echo "[WSL] Root listing:" 
mdir -/ z:/

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

mcopy -o z:/splash.bmp "$OUT"

echo "[WSL] Extracted to: $OUT"