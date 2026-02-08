#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-4.0.2}"
SHA256="${2:-85b8c37a406d862e656ad4ec14be9f6ce474c1b436b9615e91a55208aced3f44}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$ROOT/_toolchains/cosmocc"
ZIP_NAME="cosmocc-$VERSION.zip"
URL="https://cosmo.zip/pub/cosmocc/$ZIP_NAME"
TMP="${TMPDIR:-/tmp}/$ZIP_NAME"

installed_version=""
if [[ -f "$INSTALL_DIR/VERSION.txt" ]]; then
  installed_version="$(tr -d '\r\n' < "$INSTALL_DIR/VERSION.txt" || true)"
fi

if [[ "$installed_version" == "$VERSION" ]]; then
  echo "cosmocc already installed: v$VERSION ($INSTALL_DIR)"
  exit 0
fi

if command -v curl >/dev/null 2>&1; then
  echo "Downloading $URL"
  curl -fL --retry 3 -o "$TMP" "$URL"
elif command -v wget >/dev/null 2>&1; then
  echo "Downloading $URL"
  wget -O "$TMP" "$URL"
else
  echo "error: need curl or wget" >&2
  exit 2
fi

if ! command -v sha256sum >/dev/null 2>&1; then
  echo "error: need sha256sum" >&2
  exit 2
fi

echo "$SHA256  $TMP" | sha256sum -c -

rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

if ! command -v unzip >/dev/null 2>&1; then
  echo "error: need unzip" >&2
  exit 2
fi

unzip -q "$TMP" -d "$INSTALL_DIR"

# Normalize layout (some zips contain a single top-level directory)
if [[ ! -d "$INSTALL_DIR/bin" ]]; then
  topdir="$(find "$INSTALL_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1 || true)"
  if [[ -n "$topdir" && -d "$topdir/bin" ]]; then
    shopt -s dotglob
    mv "$topdir"/* "$INSTALL_DIR/"
    rmdir "$topdir"
  fi
fi

echo -n "$VERSION" > "$INSTALL_DIR/VERSION.txt"

echo "Done. cosmocc toolchain installed."
echo "- Bin dir: $INSTALL_DIR/bin"
echo "- Wrapper: $ROOT/cosmocc.sh"