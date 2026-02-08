#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-4.0.2}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COSMOCC_DIR="$ROOT/_toolchains/cosmocc"
INSTALL_DIR="$ROOT/_toolchains/apelink"

"$ROOT/get-cosmocc.sh" "$VERSION" >/dev/null

src=""
if [[ -f "$COSMOCC_DIR/bin/apelink" ]]; then
  src="$COSMOCC_DIR/bin/apelink"
elif [[ -f "$COSMOCC_DIR/bin/apelink.exe" ]]; then
  src="$COSMOCC_DIR/bin/apelink.exe"
fi

if [[ -z "$src" ]]; then
  echo "error: apelink not found under $COSMOCC_DIR/bin" >&2
  exit 2
fi

rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

cp -f "$src" "$INSTALL_DIR/"
echo -n "$VERSION" > "$INSTALL_DIR/VERSION.txt"

echo "Done. apelink installed."
echo "- Dir: $INSTALL_DIR"
