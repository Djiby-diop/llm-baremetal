#!/usr/bin/env python3
"""
export_soma_dataset.py — Phase X
Convert soma_train.jsonl (exported from OO bare-metal via /soma_export)
into a clean HuggingFace-compatible dataset for oo-model cortex training.

Input:  soma_train.jsonl  (one JSON record per line)
Output: soma_dataset/train.jsonl + soma_dataset/meta.json

Usage:
    python export_soma_dataset.py --input soma_train.jsonl --output soma_dataset/

Record format from /soma_export:
  {"turn": 42, "prompt": "...", "response": "...", "domain": 2,
   "confidence": 0.87, "solar_tok": 1234, "lunar_tok": 5678,
   "dna_gen": 7, "dna_hash": "0xDEADBEEF", "smb_ticks": 128}
"""

import argparse
import json
import os
import sys
from pathlib import Path

DOMAIN_NAMES = {
    0: "system",
    1: "code",
    2: "reasoning",
    3: "creative",
    4: "factual",
    5: "safety",
    6: "meta",
}

# Confidence threshold — discard low-quality turns
MIN_CONFIDENCE = 0.35


def load_jsonl(path: str):
    records = []
    with open(path, "r", encoding="utf-8") as f:
        for i, line in enumerate(f):
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as e:
                print(f"[WARN] line {i+1} parse error: {e}", file=sys.stderr)
    return records


def clean_record(rec: dict) -> dict | None:
    prompt = rec.get("prompt", "").strip()
    response = rec.get("response", "").strip()
    if not prompt or not response:
        return None
    # Filter very low-confidence outputs
    conf = float(rec.get("confidence", 1.0))
    if conf < MIN_CONFIDENCE:
        return None
    domain = int(rec.get("domain", 0))
    return {
        "prompt": prompt,
        "response": response,
        "domain": domain,
        "domain_name": DOMAIN_NAMES.get(domain, "unknown"),
        "confidence": round(conf, 4),
        "dna_gen": int(rec.get("dna_gen", 0)),
        "dna_hash": rec.get("dna_hash", "0x00000000"),
        "smb_ticks": int(rec.get("smb_ticks", 0)),
        # Cortex training format: instruction + output
        "text": f"<|soma|>{prompt}<|/soma|><|response|>{response}<|/response|>",
    }


def write_dataset(records: list, output_dir: str):
    os.makedirs(output_dir, exist_ok=True)
    train_path = os.path.join(output_dir, "train.jsonl")
    meta_path = os.path.join(output_dir, "meta.json")

    domain_counts = {d: 0 for d in DOMAIN_NAMES}
    with open(train_path, "w", encoding="utf-8") as f:
        for rec in records:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
            domain_counts[rec["domain"]] = domain_counts.get(rec["domain"], 0) + 1

    meta = {
        "total_records": len(records),
        "domain_counts": {DOMAIN_NAMES[k]: v for k, v in domain_counts.items()},
        "format": "soma-v1",
        "special_tokens": {
            "soma_open": "<|soma|>",
            "soma_close": "<|/soma|>",
            "response_open": "<|response|>",
            "response_close": "<|/response|>",
        },
    }
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    print(f"[OK] Exported {len(records)} records → {train_path}")
    print(f"[OK] Meta → {meta_path}")
    for dn, count in meta["domain_counts"].items():
        if count > 0:
            print(f"  {dn:12s}: {count}")


def generate_synthetic_sample(output_path: str, n: int = 50):
    """Generate synthetic training data for cold-start when no USB data exists."""
    import random
    random.seed(42)

    samples = [
        ("What is the kernel doing?", "The kernel is managing memory zones and scheduling inference tasks.", 0),
        ("Explain the SomaMind architecture.", "SomaMind runs dual-core solar/lunar inference with DNA-driven temperature control.", 2),
        ("Write a bare-metal memory allocator.", "Use zone-based allocation: divide physical memory into zones (DMA, normal, high), each with a free list.", 1),
        ("What is homeostatic loss?", "Homeostatic loss combines survival pressure, routing accuracy, and memory coherence into a single training signal.", 2),
        ("How does speculative decoding work?", "A draft model proposes tokens; the main model verifies via acceptance ratio p_verify/p_draft.", 2),
        ("Describe the OO swarm.", "Four DNA-diverse agents (BASE, SOLAR, LUNAR, XPLR) vote on each token via majority or confidence-weighted consensus.", 3),
        ("List UEFI boot steps.", "1. Firmware POST. 2. GPT scan. 3. EFI loader. 4. ExitBootServices. 5. Kernel entry.", 0),
        ("What is D+ policy?", "D+ is the OO decision engine: routes inference through domain-specific DNA profiles with warden safety checks.", 0),
        ("How do zones protect memory?", "Each zone has guard pages at boundaries; writes outside zone bounds trigger warden reflex halt.", 5),
        ("Describe pheromion signals.", "Pheromion encodes gradient-like pressure signals between soma agents, biasing token sampling toward high-fitness regions.", 3),
    ]

    records = []
    for i in range(n):
        prompt, response, domain = random.choice(samples)
        # Add slight variation
        conf = 0.6 + random.random() * 0.35
        records.append({
            "turn": i,
            "prompt": prompt,
            "response": response,
            "domain": domain,
            "confidence": round(conf, 4),
            "dna_gen": random.randint(0, 12),
            "dna_hash": f"0x{random.randint(0, 0xFFFFFFFF):08X}",
            "smb_ticks": random.randint(0, 512),
        })

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")
    print(f"[OK] Generated {n} synthetic records → {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Export soma_train.jsonl to cortex training dataset")
    parser.add_argument("--input", default="soma_train.jsonl", help="Input JSONL from /soma_export")
    parser.add_argument("--output", default="soma_dataset", help="Output directory")
    parser.add_argument("--gen-synthetic", action="store_true", help="Generate synthetic sample if no USB data")
    parser.add_argument("--synthetic-n", type=int, default=50, help="Number of synthetic records")
    args = parser.parse_args()

    if args.gen_synthetic or not os.path.exists(args.input):
        synth_path = args.input if args.gen_synthetic else "soma_train_synthetic.jsonl"
        generate_synthetic_sample(synth_path, n=args.synthetic_n)
        if not os.path.exists(args.input):
            args.input = synth_path

    raw = load_jsonl(args.input)
    print(f"[INFO] Loaded {len(raw)} raw records from {args.input}")

    cleaned = [c for r in raw if (c := clean_record(r)) is not None]
    discarded = len(raw) - len(cleaned)
    print(f"[INFO] Cleaned: {len(cleaned)} kept, {discarded} discarded (conf < {MIN_CONFIDENCE})")

    write_dataset(cleaned, args.output)


if __name__ == "__main__":
    main()
