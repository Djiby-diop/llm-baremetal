#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN="$ROOT/_toolchains/cosmocc"

if [[ -f "$TOOLCHAIN/bin/cosmocc" ]]; then
  exec "$TOOLCHAIN/bin/cosmocc" "$@"
fi

if [[ -f "$TOOLCHAIN/bin/cosmocc.exe" ]]; then
  exec "$TOOLCHAIN/bin/cosmocc.exe" "$@"
fi

echo "cosmocc not installed. Run: $ROOT/get-cosmocc.sh" >&2
exit 1
