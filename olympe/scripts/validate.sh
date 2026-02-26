#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"

mkdir -p "$BUILD_DIR"

need() {
  local c="$1"
  command -v "$c" >/dev/null 2>&1 || { echo "MISSING toolchain: $c"; exit 1; }
}

echo "[validate] core toolchains"
need gcc
need g++
need make

echo "[validate] p01 zig"
need zig
zig test "$ROOT/pillars/p01/p01.zig"

echo "[validate] p02 lua"
need lua
lua "$ROOT/pillars/p02/p02.lua"

echo "[validate] p03 rust"
need rustc
rustc --test "$ROOT/pillars/p03/p03.rs" -o "$BUILD_DIR/p03_tests"
"$BUILD_DIR/p03_tests"

echo "[validate] p04 zig"
zig test "$ROOT/pillars/p04/p04.zig"

echo "[validate] p05 c++"
g++ -std=c++2b -Wall -Wextra -Werror -c "$ROOT/pillars/p05/p05.cpp" -o "$BUILD_DIR/p05.o"

echo "[validate] p06 nasm"
need nasm
nasm -f elf64 "$ROOT/pillars/p06/p06.asm" -o "$BUILD_DIR/p06.o"

echo "[validate] p07 gforth"
need gforth
gforth "$ROOT/pillars/p07/p07.fs" -e bye

echo "[validate] p08 mojo (stub)"
if command -v mojo >/dev/null 2>&1; then
  mojo test "$ROOT/pillars/p08/p08.mojo"
else
  if [[ "${OLYMPE_ALLOW_MOJO_STUB:-0}" == "1" ]]; then
    echo "SKIP: mojo not installed (OLYMPE_ALLOW_MOJO_STUB=1)"
  else
    echo "MISSING toolchain: mojo (needed for p08). Set OLYMPE_ALLOW_MOJO_STUB=1 to skip."
    exit 1
  fi
fi

echo "[validate] p09 verilator"
need verilator
verilator --lint-only "$ROOT/pillars/p09/p09.v"

echo "[validate] p10 rust"
rustc --test "$ROOT/pillars/p10/p10.rs" -o "$BUILD_DIR/p10_tests"
"$BUILD_DIR/p10_tests"

echo "[validate] OK"
