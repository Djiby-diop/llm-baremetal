#!/usr/bin/env python3
"""
generate_engine_datasets.py — OO Per-Engine Training Dataset Generator

Generates structured JSONL training data for all OO engines.
Merges halt_calibration + oo_native_concepts + self_introspection +
code_domain + swarm_coordination into a single shuffled train split.

Usage:
    python generate_engine_datasets.py --output soma_dataset/ --seed 42
"""
import argparse
import json
import os
import random
from pathlib import Path

ENGINE_DATA_DIR = Path(__file__).parent.parent / "data" / "engine_training"

SOURCES = [
    ("halt_calibration.jsonl",    3),   # weight 3x — critical
    ("oo_native_concepts.jsonl",  2),
    ("self_introspection.jsonl",  2),
    ("code_domain.jsonl",         1),
    ("swarm_coordination.jsonl",  1),
]


def load_weighted(sources: list[tuple[str, int]], data_dir: Path) -> list[dict]:
    rows = []
    for filename, weight in sources:
        path = data_dir / filename
        if not path.exists():
            print(f"  [warn] missing: {path}")
            continue
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                obj = json.loads(line)
                for _ in range(weight):
                    rows.append(obj)
    return rows


def to_train_format(obj: dict) -> dict:
    """Convert engine dataset entry to soma_dataset train.jsonl format."""
    instruction = obj.get("instruction", "")
    response = obj.get("response", "")
    domain = obj.get("domain", "UNKNOWN")
    halt_prob = obj.get("halt_prob", None)

    # Build text field (SomaMind training format)
    text = f"[{domain}] {instruction}\n{response}"

    out = {
        "text": text,
        "instruction": instruction,
        "response": response,
        "domain": domain,
    }
    if halt_prob is not None:
        out["halt_prob"] = halt_prob
    if "engine" in obj:
        out["engine"] = obj["engine"]
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="soma_dataset", help="Output directory")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--val-ratio", type=float, default=0.1)
    args = parser.parse_args()

    rng = random.Random(args.seed)

    print(f"[engine_datasets] Loading from {ENGINE_DATA_DIR}")
    rows = load_weighted(SOURCES, ENGINE_DATA_DIR)
    print(f"[engine_datasets] {len(rows)} weighted samples loaded")

    rng.shuffle(rows)
    converted = [to_train_format(r) for r in rows]

    n_val = max(1, int(len(converted) * args.val_ratio))
    val_rows = converted[:n_val]
    train_rows = converted[n_val:]

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    train_path = out_dir / "engine_train.jsonl"
    val_path = out_dir / "engine_val.jsonl"

    with open(train_path, "w") as f:
        for row in train_rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    with open(val_path, "w") as f:
        for row in val_rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    print(f"[engine_datasets] train: {len(train_rows)} → {train_path}")
    print(f"[engine_datasets] val:   {len(val_rows)} → {val_path}")

    # Stats by domain
    from collections import Counter
    domain_counts = Counter(r.get("domain", "?") for r in train_rows)
    halt_rows = [r for r in train_rows if "halt_prob" in r]
    print(f"[engine_datasets] halt_prob samples: {len(halt_rows)}")
    print(f"[engine_datasets] domain breakdown:")
    for domain, count in sorted(domain_counts.items(), key=lambda x: -x[1]):
        print(f"  {domain:20s} {count}")


if __name__ == "__main__":
    main()
