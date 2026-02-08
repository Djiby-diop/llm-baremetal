#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$ROOT/_toolchains/redbean"

if [[ ! -d "$TOOL_DIR" ]]; then
  "$ROOT/get-redbean.sh" >/dev/null
fi

if [[ ! -f "$TOOL_DIR/redbean" ]]; then
  echo "error: redbean not found: $TOOL_DIR/redbean" >&2
  exit 2
fi

exec "$TOOL_DIR/redbean" "$@"
