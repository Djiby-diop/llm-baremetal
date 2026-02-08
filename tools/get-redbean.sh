#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-4.0.2}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COSMOS_DIR="$ROOT/_toolchains/cosmos"
INSTALL_DIR="$ROOT/_toolchains/redbean"

"$ROOT/get-cosmos.sh" "$VERSION" >/dev/null

if [[ ! -f "$COSMOS_DIR/bin/redbean" ]]; then
  echo "error: redbean not found under $COSMOS_DIR/bin/redbean" >&2
  exit 2
fi

rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

cp -f "$COSMOS_DIR/bin/redbean" "$INSTALL_DIR/redbean"
echo -n "$VERSION" > "$INSTALL_DIR/VERSION.txt"

echo "Done. redbean installed."
echo "- Bin: $INSTALL_DIR/redbean"
