#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$ROOT/_toolchains/ape"

if [[ ! -d "$TOOL_DIR" ]]; then
  "$ROOT/get-ape.sh" >/dev/null
fi

arch="$(uname -m)"
case "$arch" in
  x86_64) ape="$TOOL_DIR/ape-x86_64.elf" ;;
  aarch64) ape="$TOOL_DIR/ape-aarch64.elf" ;;
  arm64) ape="$TOOL_DIR/ape-arm64.elf" ;;
  *)
    echo "error: unsupported arch: $arch" >&2
    exit 2
    ;;
esac

if [[ ! -f "$ape" ]]; then
  if [[ "$arch" == "arm64" && -f "$TOOL_DIR/ape-aarch64.elf" ]]; then
    ape="$TOOL_DIR/ape-aarch64.elf"
  else
    echo "error: ape artifact not found: $ape" >&2
    exit 2
  fi
fi

exec "$ape" "$@"
