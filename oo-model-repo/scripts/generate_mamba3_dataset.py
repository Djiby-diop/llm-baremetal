#!/usr/bin/env python3
"""
generate_mamba3_dataset.py — Mamba3 2.8B Training Dataset Generator

Merges all OO training datasets with the weights defined in BATTERFYL_TRAINING_SPEC.md
and writes a single mamba3_training.jsonl ready for Batterfyl.

Dataset weights (multiplier = how many times a sample is repeated):
  halt_calibration.jsonl       3x  (CRITICAL — halt head discrimination)
  oo_native_concepts.jsonl     2x  (OO terminology + engine names)
  self_introspection.jsonl     2x  ([SELF] format for mirrorion)
  governor_concepts.jsonl      2x  (Governor/Bus/D+ wiring — Phase J/K/M)
  warden_bus_events.jsonl      1.5x → treated as 2x rounded (UART bridge, bus events)
  code_domain.jsonl            1x
  swarm_coordination.jsonl     1x

Output:
  data/processed/mamba3_training.jsonl  — merged, shuffled, with domain/halt_prob fields
  data/processed/mamba3_stats.json      — dataset statistics

Usage:
  python scripts/generate_mamba3_dataset.py
  python scripts/generate_mamba3_dataset.py --no-shuffle --dry-run
"""

import argparse
import json
import random
import sys
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT  = SCRIPT_DIR.parent
DATA_DIR   = REPO_ROOT / "data" / "engine_training"
OUT_DIR    = REPO_ROOT / "data" / "processed"

# (filename, weight, default_domain, default_halt_prob)
DATASETS = [
    ("halt_calibration.jsonl",    3,   "FACTUAL",   None),   # has own halt_prob
    ("oo_native_concepts.jsonl",  2,   "OO_META",   0.10),
    ("self_introspection.jsonl",  2,   "OO_META",   0.20),
    ("governor_concepts.jsonl",   2,   "OO_META",   0.10),
    ("warden_bus_events.jsonl",   2,   "OO_META",   0.10),   # 1.5x rounded up
    ("code_domain.jsonl",         1,   "CODE",      0.05),
    ("swarm_coordination.jsonl",  1,   "OO_META",   0.15),
]

REQUIRED_FIELDS = {"instruction", "response"}


def load_jsonl(path: Path) -> list[dict]:
    samples = []
    if not path.exists():
        print(f"[WARN] Missing: {path.name} — skipping", file=sys.stderr)
        return samples
    with path.open(encoding="utf-8") as f:
        for i, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                if not REQUIRED_FIELDS.issubset(obj.keys()):
                    print(f"[WARN] {path.name}:{i} missing required fields — skip", file=sys.stderr)
                    continue
                samples.append(obj)
            except json.JSONDecodeError as e:
                print(f"[WARN] {path.name}:{i} JSON error: {e} — skip", file=sys.stderr)
    return samples


def normalize_sample(sample: dict, default_domain: str, default_halt_prob: float | None) -> dict:
    """Ensure every sample has domain and halt_prob fields."""
    out = dict(sample)
    if "domain" not in out or not out["domain"]:
        out["domain"] = default_domain
    if "halt_prob" not in out or out["halt_prob"] is None:
        if default_halt_prob is not None:
            out["halt_prob"] = default_halt_prob
    # Clamp halt_prob to [0.0, 1.0]
    if "halt_prob" in out and out["halt_prob"] is not None:
        out["halt_prob"] = max(0.0, min(1.0, float(out["halt_prob"])))
    return out


def generate(shuffle: bool, seed: int, dry_run: bool) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    all_samples: list[dict] = []
    stats: dict[str, dict] = {}

    for filename, weight, default_domain, default_halt_prob in DATASETS:
        path = DATA_DIR / filename
        raw = load_jsonl(path)
        normalized = [normalize_sample(s, default_domain, default_halt_prob) for s in raw]
        repeated = normalized * weight
        all_samples.extend(repeated)
        stats[filename] = {
            "raw_count": len(raw),
            "weight": weight,
            "weighted_count": len(repeated),
            "default_domain": default_domain,
        }
        print(f"  {filename:40s}  {len(raw):4d} samples × {weight} = {len(repeated):5d}")

    total = len(all_samples)
    print(f"\nTotal samples before shuffle: {total}")

    if shuffle:
        rng = random.Random(seed)
        rng.shuffle(all_samples)
        print(f"Shuffled with seed={seed}")

    out_path = OUT_DIR / "mamba3_training.jsonl"
    stats_path = OUT_DIR / "mamba3_stats.json"

    if dry_run:
        print(f"\n[DRY RUN] Would write {total} samples to {out_path}")
        return

    with out_path.open("w", encoding="utf-8") as f:
        for sample in all_samples:
            f.write(json.dumps(sample, ensure_ascii=False) + "\n")

    # Count by domain
    domain_counts: dict[str, int] = {}
    halt_probs: list[float] = []
    for s in all_samples:
        d = s.get("domain", "UNKNOWN")
        domain_counts[d] = domain_counts.get(d, 0) + 1
        hp = s.get("halt_prob")
        if hp is not None:
            halt_probs.append(float(hp))

    meta = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "total_samples": total,
        "shuffle": shuffle,
        "seed": seed,
        "datasets": stats,
        "domain_distribution": domain_counts,
        "halt_prob_stats": {
            "count": len(halt_probs),
            "mean": round(sum(halt_probs) / len(halt_probs), 4) if halt_probs else None,
            "min": round(min(halt_probs), 4) if halt_probs else None,
            "max": round(max(halt_probs), 4) if halt_probs else None,
        },
    }

    with stats_path.open("w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    print(f"\nWrote {total} samples → {out_path}")
    print(f"Stats  → {stats_path}")
    print("\nDomain distribution:")
    for domain, count in sorted(domain_counts.items(), key=lambda x: -x[1]):
        print(f"  {domain:20s}  {count:5d}  ({100*count/total:.1f}%)")
    if halt_probs:
        print(f"\nhalt_prob: n={len(halt_probs)} mean={meta['halt_prob_stats']['mean']} "
              f"min={meta['halt_prob_stats']['min']} max={meta['halt_prob_stats']['max']}")


def main():
    parser = argparse.ArgumentParser(description="Generate Mamba3 2.8B training dataset")
    parser.add_argument("--no-shuffle", action="store_true", help="Disable shuffling")
    parser.add_argument("--seed", type=int, default=42, help="Random seed (default: 42)")
    parser.add_argument("--dry-run", action="store_true", help="Print stats without writing files")
    args = parser.parse_args()

    print("=== OO Mamba3 Training Dataset Generator ===")
    print(f"Source: {DATA_DIR}")
    print(f"Output: {OUT_DIR}")
    print()

    generate(
        shuffle=not args.no_shuffle,
        seed=args.seed,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
