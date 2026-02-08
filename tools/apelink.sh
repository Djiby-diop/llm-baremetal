#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$ROOT/_toolchains/apelink"

if [[ ! -d "$TOOL_DIR" ]]; then
  "$ROOT/get-apelink.sh" >/dev/null
fi

apelink=""
if [[ -f "$TOOL_DIR/apelink" ]]; then
  apelink="$TOOL_DIR/apelink"
elif [[ -f "$TOOL_DIR/apelink.exe" ]]; then
  apelink="$TOOL_DIR/apelink.exe"
fi

if [[ -z "$apelink" ]]; then
  echo "error: apelink not found under $TOOL_DIR" >&2
  exit 2
fi

exec "$apelink" "$@"
