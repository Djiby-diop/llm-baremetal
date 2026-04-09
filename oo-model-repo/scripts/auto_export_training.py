#!/usr/bin/env python3
"""
auto_export_training.py — OO Training Feedback Loop: Step 2

Merges all training sources into a single deduplicated dataset:
  1. data/journal_export/journal_*.jsonl   (from journal_to_dataset.py)
  2. data/engine_training/*.jsonl          (curated engine datasets)
  3. Any extra JSONL passed via --extra

Applies per-domain weighting, deduplicates by MD5 of (prompt+response),
then writes:
  data/processed/train.jsonl   (90%)
  data/processed/val.jsonl     (10%)
  data/processed/metadata.json (stats, sources, weights)

Usage:
  python scripts/auto_export_training.py
  python scripts/auto_export_training.py --extra my_extra.jsonl --dry-run
"""

import argparse
import hashlib
import json
import random
import sys
from pathlib import Path

# ── Weights: how many times to repeat each domain ────────────────────────────
DOMAIN_WEIGHTS = {
    "HALT":         3,   # halt_prob calibration — critical
    "HALT_LOW":     3,
    "MATH":         2,
    "REFLEX":       2,
    "SYSTEM":       2,
    "CODE":         2,
    "CHAT":         1,
    # Engine training files
    "halt_calibration":   3,
    "oo_native_concepts": 2,
    "self_introspection": 2,
    "code_domain":        2,
    "swarm_coordination": 2,
}

SCRIPT_DIR  = Path(__file__).parent
REPO_ROOT   = SCRIPT_DIR.parent
JOURNAL_DIR = REPO_ROOT / "data" / "journal_export"
ENGINE_DIR  = REPO_ROOT / "data" / "engine_training"
OUT_DIR     = REPO_ROOT / "data" / "processed"

def md5_key(prompt: str, response: str) -> str:
    return hashlib.md5((prompt.strip() + response.strip()).encode()).hexdigest()

def load_jsonl(path: Path, source_tag: str):
    records = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                p = (obj.get('prompt') or obj.get('input') or obj.get('instruction') or '').strip()
                r = (obj.get('response') or obj.get('output') or obj.get('completion') or '').strip()
                if not p or not r:
                    continue
                records.append({
                    "prompt":    p,
                    "response":  r,
                    "domain":    obj.get('domain', source_tag),
                    "halt_prob": obj.get('halt_prob', 0.75),
                    "source":    source_tag,
                })
            except json.JSONDecodeError:
                continue
    return records

def collect_all(extra_paths):
    all_records = []

    # 1. Journal exports
    if JOURNAL_DIR.exists():
        for jf in sorted(JOURNAL_DIR.glob("journal_*.jsonl")):
            tag = jf.stem.replace("journal_", "").upper()
            recs = load_jsonl(jf, tag)
            print(f"  [journal/{tag}] {len(recs)} entries from {jf.name}")
            all_records.extend(recs)
    else:
        print(f"  [WARN] journal_export/ not found — skipping journal data")

    # 2. Engine training datasets
    if ENGINE_DIR.exists():
        for ef in sorted(ENGINE_DIR.glob("*.jsonl")):
            tag = ef.stem
            recs = load_jsonl(ef, tag)
            print(f"  [engine/{tag}] {len(recs)} entries from {ef.name}")
            all_records.extend(recs)

    # 3. Extra files
    for ep in extra_paths:
        p = Path(ep)
        if p.exists():
            recs = load_jsonl(p, p.stem)
            print(f"  [extra/{p.stem}] {len(recs)} entries")
            all_records.extend(recs)

    return all_records

def apply_weights(records):
    weighted = []
    for r in records:
        domain = r.get('domain', 'CHAT')
        w = DOMAIN_WEIGHTS.get(domain, 1)
        for _ in range(w):
            weighted.append(r)
    return weighted

def dedup(records):
    seen = {}
    out = []
    for r in records:
        k = md5_key(r['prompt'], r['response'])
        if k not in seen:
            seen[k] = True
            out.append(r)
    return out

def split(records, val_ratio=0.10, seed=42):
    random.seed(seed)
    shuffled = list(records)
    random.shuffle(shuffled)
    n_val = max(1, int(len(shuffled) * val_ratio))
    return shuffled[n_val:], shuffled[:n_val]

def write_jsonl(path: Path, records):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        for r in records:
            f.write(json.dumps(r, ensure_ascii=False) + '\n')

def main():
    ap = argparse.ArgumentParser(description="Merge + deduplicate OO training data")
    ap.add_argument('--extra', nargs='*', default=[], help="Additional JSONL files")
    ap.add_argument('--val-ratio', type=float, default=0.10)
    ap.add_argument('--seed', type=int, default=42)
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    print("[auto_export] Collecting sources...")
    raw = collect_all(args.extra)
    print(f"[auto_export] {len(raw)} raw records")

    weighted = apply_weights(raw)
    print(f"[auto_export] {len(weighted)} after weighting")

    unique = dedup(weighted)
    print(f"[auto_export] {len(unique)} after dedup ({len(weighted)-len(unique)} removed)")

    train, val = split(unique, args.val_ratio, args.seed)
    print(f"[auto_export] train={len(train)} val={len(val)}")

    # Domain distribution
    domain_counts = {}
    for r in unique:
        d = r.get('domain', '?')
        domain_counts[d] = domain_counts.get(d, 0) + 1

    if args.dry_run:
        print("\n[DRY RUN] Domain distribution:")
        for d, n in sorted(domain_counts.items(), key=lambda x: -x[1]):
            print(f"  {d:20s} {n:5d}")
        return

    write_jsonl(OUT_DIR / "train.jsonl", train)
    write_jsonl(OUT_DIR / "val.jsonl", val)

    meta = {
        "total_unique": len(unique),
        "train": len(train),
        "val": len(val),
        "val_ratio": args.val_ratio,
        "domains": domain_counts,
        "weights": DOMAIN_WEIGHTS,
    }
    (OUT_DIR / "metadata.json").write_text(json.dumps(meta, indent=2))

    print(f"\n[auto_export] Written to {OUT_DIR}/")
    print(f"  train.jsonl  {len(train):5d} samples")
    print(f"  val.jsonl    {len(val):5d} samples")
    print(f"  metadata.json")
    for d, n in sorted(domain_counts.items(), key=lambda x: -x[1]):
        print(f"    {d:20s} {n:5d}")

if __name__ == '__main__':
    main()
