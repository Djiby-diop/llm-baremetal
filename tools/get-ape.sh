#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-4.0.2}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COSMOS_DIR="$ROOT/_toolchains/cosmos"
INSTALL_DIR="$ROOT/_toolchains/ape"

"$ROOT/get-cosmos.sh" "$VERSION" >/dev/null

if [[ ! -d "$COSMOS_DIR/bin" ]]; then
  echo "error: cosmos bin dir not found: $COSMOS_DIR/bin" >&2
  exit 2
fi

rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

shopt -s nullglob
apes=("$COSMOS_DIR"/bin/ape-*)
if [[ ${#apes[@]} -eq 0 ]]; then
  echo "error: no ape-* artifacts found under $COSMOS_DIR/bin" >&2
  exit 2
fi

cp -f "${apes[@]}" "$INSTALL_DIR/"
echo -n "$VERSION" > "$INSTALL_DIR/VERSION.txt"

echo "Done. ape artifacts installed."
echo "- Dir: $INSTALL_DIR"
