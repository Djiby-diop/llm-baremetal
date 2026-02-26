#!/usr/bin/env bash
set -euo pipefail

APT="apt-get"
if [[ "$(id -u)" != "0" ]]; then
  APT="sudo apt-get"
fi

$APT update

install_zig_from_tarball() {
  if command -v zig >/dev/null 2>&1; then
    return 0
  fi

  local version="${ZIG_VERSION:-0.12.0}"
  local arch
  arch="$(uname -m)"

  local platform
  case "$arch" in
    x86_64) platform="zig-linux-x86_64" ;;
    aarch64|arm64) platform="zig-linux-aarch64" ;;
    *)
      echo "ERROR: unsupported arch for Zig tarball: $arch" >&2
      return 1
      ;;
  esac

  local url="https://ziglang.org/download/${version}/${platform}-${version}.tar.xz"
  local tmp="/tmp/${platform}-${version}.tar.xz"

  echo "[toolchains] Installing Zig ${version} from ${url}"
  curl -fsSL "$url" -o "$tmp"

  rm -rf "/opt/zig-${version}"
  mkdir -p "/opt/zig-${version}"
  tar -xJf "$tmp" -C "/opt/zig-${version}" --strip-components=1

  ln -sf "/opt/zig-${version}/zig" /usr/local/bin/zig
  zig version
}

ensure_lua_command() {
  if command -v lua >/dev/null 2>&1; then
    return 0
  fi
  if command -v lua5.4 >/dev/null 2>&1; then
    ln -sf "$(command -v lua5.4)" /usr/local/bin/lua
    return 0
  fi
}

# NOTE: Mojo toolchain is not installed here.
$APT install -y --no-install-recommends \
  build-essential make \
  clang \
  nasm \
  rustc cargo \
  lua5.4 luajit \
  gforth \
  verilator \
  curl ca-certificates xz-utils

ensure_lua_command

# Zig isn't packaged on all distros; install from an official tarball if needed.
install_zig_from_tarball

echo "OK: installed toolchains (except Mojo)."
