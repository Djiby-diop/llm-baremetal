#!/usr/bin/env python3
"""
merge.py — Merge all djibion_*.jsonl into a single deduplicated JSONL.

Outputs:
  djib/dataset/djibion_full.jsonl          — full merged dataset (local)
  oo-model-repo/data/engine_training/djibion.jsonl  — copy for Batterfyl pipeline

Usage:
  python djib/dataset/merge.py
"""
import json
import sys
from pathlib import Path

DATASET_DIR = Path(__file__).parent
REPO_ROOT   = DATASET_DIR.parent.parent
OUT_LOCAL   = DATASET_DIR / "djibion_full.jsonl"
OUT_PIPELINE = REPO_ROOT / "oo-model-repo" / "data" / "engine_training" / "djibion.jsonl"

SOURCES = [
    DATASET_DIR / "djibion_base.jsonl",
    DATASET_DIR / "djibion_v2.jsonl",
    DATASET_DIR / "djibion_v3.jsonl",
    DATASET_DIR / "djibion_v4.jsonl",
]


def load_jsonl(path: Path) -> list[dict]:
    samples = []
    if not path.exists():
        print(f"[SKIP] {path.name} not found", file=sys.stderr)
        return samples
    with path.open(encoding="utf-8") as f:
        for i, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                samples.append(json.loads(line))
            except json.JSONDecodeError as e:
                print(f"[WARN] {path.name}:{i} JSON error: {e}", file=sys.stderr)
    return samples


def sample_key(obj: dict) -> str:
    """Dedup key: first user message content (normalized)."""
    msgs = obj.get("messages", [])
    for m in msgs:
        if m.get("role") == "user":
            return m.get("content", "")[:200].strip().lower()
    # instruction/response format
    return obj.get("instruction", "")[:200].strip().lower()


def to_single_line(obj: dict) -> str:
    """Ensure sample is a single-line JSON (JSONL format)."""
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":"))


def main():
    all_samples = []
    seen_keys: set[str] = set()
    total_raw = 0

    for src in SOURCES:
        samples = load_jsonl(src)
        total_raw += len(samples)
        added = 0
        for s in samples:
            k = sample_key(s)
            if k and k not in seen_keys:
                seen_keys.add(k)
                all_samples.append(s)
                added += 1
        print(f"  {src.name:<30} {len(samples):>4} loaded, {added:>4} added (dedup)")

    print(f"\n  Total raw: {total_raw}  →  Deduplicated: {len(all_samples)}")

    # Write both outputs
    for out_path in (OUT_LOCAL, OUT_PIPELINE):
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with out_path.open("w", encoding="utf-8") as f:
            for s in all_samples:
                f.write(to_single_line(s) + "\n")
        print(f"  Written → {out_path}")


if __name__ == "__main__":
    print("=== DjibionDataset Merge ===")
    main()
