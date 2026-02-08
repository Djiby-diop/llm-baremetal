#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-4.0.2}"
SHA256="${2:-e466106b18064e0c996ef64d261133af867bccd921ad14e54975d89aa17a8717}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$ROOT/_toolchains/cosmopolitan-src"
TGZ_NAME="cosmopolitan-$VERSION.tar.gz"
URL="https://justine.lol/cosmopolitan/$TGZ_NAME"
TMP="${TMPDIR:-/tmp}/$TGZ_NAME"

installed_version=""
if [[ -f "$INSTALL_DIR/VERSION.txt" ]]; then
  installed_version="$(tr -d '\r\n' < "$INSTALL_DIR/VERSION.txt" || true)"
fi

if [[ "$installed_version" == "$VERSION" ]]; then
  echo "cosmopolitan-src already installed: v$VERSION ($INSTALL_DIR)"
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

if ! command -v tar >/dev/null 2>&1; then
  echo "error: need tar" >&2
  exit 2
fi

echo "Extracting to $INSTALL_DIR"
tar -xf "$TMP" -C "$INSTALL_DIR"

# Normalize layout (tarball usually contains a single top-level directory)
topdir="$(find "$INSTALL_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1 || true)"
if [[ -n "$topdir" ]]; then
  filecount="$(find "$INSTALL_DIR" -mindepth 1 -maxdepth 1 -type f | wc -l | tr -d ' ')"
  dircount="$(find "$INSTALL_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
  if [[ "$filecount" == "0" && "$dircount" == "1" ]]; then
    shopt -s dotglob
    mv "$topdir"/* "$INSTALL_DIR/"
    rmdir "$topdir"
  fi
fi

echo -n "$VERSION" > "$INSTALL_DIR/VERSION.txt"

echo "Done. cosmopolitan sources extracted."
echo "- Root: $INSTALL_DIR"
