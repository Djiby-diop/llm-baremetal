#!/usr/bin/env bash
# Create valid empty .a archives used by oo-subsystems cache fallback.
set -euo pipefail

OO_BUILD="../llm-baremetal.worktrees/copilot-worktree-2026-03-21T23-04-08/build/oo"
mkdir -p "$OO_BUILD"

for lib in liboo-kernel.a liboo-warden.a liboo-engine.a liboo-modules.a liboo-bus.a librust_guard.a; do
  target="$OO_BUILD/$lib"
  if [ ! -f "$target" ] || [ ! -s "$target" ]; then
    # A valid empty GNU ar archive is just the global header.
    printf '!<arch>\n' > "$target"
    echo "Created stub: $target"
  else
    echo "Already exists: $target ($(stat -c '%s' "$target") bytes)"
  fi
done
