#!/usr/bin/env python3
"""
export_to_batterfyl.py — Phase T3: Sleep Learning → Batterfyl training bricks.

Two modes:
  --from-store   Read already-consolidated MemoryRecords from a JSON store and
                 convert to training bricks (no LLM call required).
  --run-cycle    Trigger a full SleepLearningEngine cycle (requires LLM adapter),
                 then convert and export the new records.

Output is appended (dedup by instruction) to:
  oo-model-repo/data/engine_training/sleep_distilled.jsonl

Usage:
  python -m diop.evolution.sleep_learning.export_to_batterfyl --from-store
  python -m diop.evolution.sleep_learning.export_to_batterfyl --run-cycle --adapter local
  python -m diop.evolution.sleep_learning.export_to_batterfyl --from-store --dry-run
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────────────────
_HERE       = Path(__file__).parent
_DIOP_ROOT  = _HERE.parent.parent.parent          # llm-baremetal/diop/../../  → llm-baremetal
_OUT_FILE   = _DIOP_ROOT / "oo-model-repo" / "data" / "engine_training" / "sleep_distilled.jsonl"
_STORE_FILE = _DIOP_ROOT / "djib" / "memory" / "records.json"


def _load_existing(path: Path) -> set[str]:
    """Return set of existing instruction strings (dedup key)."""
    seen: set[str] = set()
    if not path.exists():
        return seen
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
            key = obj.get("instruction", "")[:200].strip().lower()
            if key:
                seen.add(key)
        except json.JSONDecodeError:
            pass
    return seen


def _append_bricks(bricks: list[dict], out_path: Path, dry_run: bool) -> int:
    """Append new (deduplicated) bricks to the output JSONL file. Returns count added."""
    seen = _load_existing(out_path)
    new_bricks = []
    for b in bricks:
        key = b.get("instruction", "")[:200].strip().lower()
        if key and key not in seen:
            seen.add(key)
            new_bricks.append(b)

    if not new_bricks:
        print("[sleep-export] ✅ No new bricks to add (all already present).")
        return 0

    if dry_run:
        print(f"[sleep-export] 🔍 DRY RUN — would add {len(new_bricks)} bricks:")
        for b in new_bricks:
            print(f"  [{b['domain']}] halt={b['halt_prob']} | {b['instruction'][:80]}...")
        return len(new_bricks)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("a", encoding="utf-8") as f:
        for b in new_bricks:
            f.write(json.dumps(b, ensure_ascii=False) + "\n")

    print(f"[sleep-export] ✅ Appended {len(new_bricks)} new bricks → {out_path}")
    return len(new_bricks)


def from_store(store_path: Path, out_path: Path, dry_run: bool) -> int:
    from .to_training import memory_store_to_bricks
    bricks = memory_store_to_bricks(store_path)
    print(f"[sleep-export] 📦 Loaded {len(bricks)} bricks from {store_path}")
    return _append_bricks(bricks, out_path, dry_run)


def run_cycle(memory_root: Path, adapter_name: str, out_path: Path, dry_run: bool) -> int:
    from .engine import SleepLearningEngine
    from .to_training import record_to_training_brick

    engine  = SleepLearningEngine(memory_root=memory_root, adapter_name=adapter_name)
    records = engine.run_sleep_cycle()

    bricks = []
    for rec in records:
        brick = record_to_training_brick(rec)
        if brick:
            bricks.append(brick)

    print(f"[sleep-export] 🌙 Sleep cycle produced {len(bricks)} convertible bricks")
    return _append_bricks(bricks, out_path, dry_run)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export DIOP sleep-distilled rules to Batterfyl")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--from-store", action="store_true",
                      help="Read from JSON memory store (no LLM call)")
    mode.add_argument("--run-cycle", action="store_true",
                      help="Run a full SleepLearningEngine cycle (requires LLM adapter)")
    parser.add_argument("--store",   default=str(_STORE_FILE),
                        help=f"Path to JsonMemoryStore records.json (default: {_STORE_FILE})")
    parser.add_argument("--memory-root", default=str(_STORE_FILE.parent),
                        help="Memory root for SleepLearningEngine")
    parser.add_argument("--adapter", default="local",
                        help="Adapter name for SleepLearningEngine (default: local)")
    parser.add_argument("--out",     default=str(_OUT_FILE),
                        help=f"Output JSONL path (default: {_OUT_FILE})")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would be written without writing")
    args = parser.parse_args()

    out_path = Path(args.out)

    if args.from_store:
        count = from_store(Path(args.store), out_path, args.dry_run)
    else:
        count = run_cycle(Path(args.memory_root), args.adapter, out_path, args.dry_run)

    print(f"[sleep-export] Done. {count} new bricks exported.")
    sys.exit(0 if count >= 0 else 1)


if __name__ == "__main__":
    main()
