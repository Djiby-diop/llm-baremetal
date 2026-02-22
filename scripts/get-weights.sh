#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./scripts/get-weights.sh <url> [out_name] [sha256]

Example:
  ./scripts/get-weights.sh \
    "https://huggingface.co/<org>/<repo>/resolve/main/<file>.gguf" \
    "<file>.gguf"

Notes:
  - Downloads into ./models/
  - If sha256 is provided, validates it.
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" || $# -lt 1 ]]; then
  usage
  exit 0
fi

URL="$1"
OUT_NAME="${2:-}"
SHA256_EXPECTED="${3:-}"
DEST_DIR="models"

mkdir -p "$DEST_DIR"

if [[ -z "$OUT_NAME" ]]; then
  # Strip query params then take basename
  OUT_NAME="$(printf '%s' "$URL" | sed 's/[?#].*$//' | awk -F/ '{print $NF}')"
fi

if [[ -z "$OUT_NAME" ]]; then
  echo "ERROR: cannot infer out_name; pass it explicitly" >&2
  exit 1
fi

OUT_PATH="$DEST_DIR/$OUT_NAME"
TMP_PATH="$OUT_PATH.download"

if command -v curl >/dev/null 2>&1; then
  DL=(curl -L --fail --retry 3 --retry-delay 1 -o "$TMP_PATH" "$URL")
elif command -v wget >/dev/null 2>&1; then
  DL=(wget -O "$TMP_PATH" "$URL")
else
  echo "ERROR: need curl or wget" >&2
  exit 1
fi

echo "Downloading: $URL"
echo "To:          $OUT_PATH"

rm -f "$TMP_PATH"
"${DL[@]}"

if [[ ! -f "$TMP_PATH" ]]; then
  echo "ERROR: download did not produce expected file: $TMP_PATH" >&2
  exit 1
fi

if [[ -n "$SHA256_EXPECTED" ]]; then
  if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL="$(sha256sum "$TMP_PATH" | awk '{print $1}')"
  elif command -v shasum >/dev/null 2>&1; then
    ACTUAL="$(shasum -a 256 "$TMP_PATH" | awk '{print $1}')"
  else
    echo "ERROR: need sha256sum or shasum to verify sha256" >&2
    exit 1
  fi

  EXPECTED="$(printf '%s' "$SHA256_EXPECTED" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
  ACTUAL_LC="$(printf '%s' "$ACTUAL" | tr '[:upper:]' '[:lower:]')"
  if [[ "$ACTUAL_LC" != "$EXPECTED" ]]; then
    rm -f "$TMP_PATH"
    echo "ERROR: SHA256 mismatch (expected $EXPECTED, got $ACTUAL_LC)" >&2
    exit 1
  fi
  echo "[OK] SHA256 verified"
fi

mv -f "$TMP_PATH" "$OUT_PATH"
echo "[OK] Downloaded: $OUT_PATH"
