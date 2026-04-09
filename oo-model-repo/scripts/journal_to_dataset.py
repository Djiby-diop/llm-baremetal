#!/usr/bin/env python3
"""
journal_to_dataset.py — OO Training Feedback Loop: Step 1

Reads soma_journal.bin (EFI partition binary) OR soma_train.jsonl (if kernel
exported text already), classifies each entry by domain, scores halt_prob
from response patterns, and writes per-domain JSONL files.

Domains auto-detected:
  MATH     — digits, =, *, ÷, calcul, résultat
  CODE     — #include, void, int, function, for(, if(, ptr
  SYSTEM   — [OO], boot, kernel, mémoire, zone, warden, DNA
  REFLEX   — [MATH:], [SYS:], [CODE:], direct reflex patterns
  CHAT     — fallback for natural language
  HALT     — entries where halt_prob < 0.3 (low confidence — high value)

Binary format (little-endian, all fields packed):
  Header  32 bytes: magic(4) version(4) entry_count(4) total_turns(4)
                    boot_count(4) reserved(12)
  Entry  216 bytes: prompt(80) response(120) turn(4) session(4)
                    prompt_hash(4) flags(4)

Usage:
  python scripts/journal_to_dataset.py \\
      --input soma_journal.bin \\
      --output data/journal_export/

  python scripts/journal_to_dataset.py \\
      --input soma_train.jsonl \\
      --output data/journal_export/
"""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

# ── Binary constants ──────────────────────────────────────────────────────────
MAGIC             = 0x534A524E  # "SJRN"
HEADER_SIZE       = 32
ENTRY_SIZE        = 216
PROMPT_LEN        = 80
RESPONSE_LEN      = 120
ENTRY_FLAG_VALID  = 0x1

# ── Domain detection ─────────────────────────────────────────────────────────
DOMAIN_PATTERNS = {
    "MATH":   re.compile(r'\b(\d[\d\s*/+\-=.]{3,}|\bcalcul|\brésultat|\bsomme|\bproduit|[=<>]{1,2}\d)', re.I),
    "CODE":   re.compile(r'(#include|void\s+\w|int\s+\w|for\s*\(|if\s*\(|while\s*\(|\bptr\b|->|::\w|fn\s+\w|\bimpl\b)', re.I),
    "SYSTEM": re.compile(r'(\[OO\]|\[OOSI\]|boot|kernel|mémoire|zone|warden|DNA|EFI|UEFI|registre|SSM|Mamba|moteur)', re.I),
    "REFLEX": re.compile(r'(\[MATH\s*:|MATH\s*\d|logic|syllogisme|raisonnement|si.*alors|premise|conclusion)', re.I),
}

def detect_domain(prompt: str, response: str) -> str:
    text = (prompt + " " + response).lower()
    for domain, pat in DOMAIN_PATTERNS.items():
        if pat.search(text):
            return domain
    return "CHAT"

def estimate_halt_prob(response: str) -> float:
    """Heuristic halt confidence from response quality."""
    r = response.strip()
    if not r:
        return 0.05
    # Very short → uncertain
    if len(r) < 15:
        return 0.20
    # Contains explicit answer markers
    if re.search(r'(=\s*[\d.]+|résultat\s*:\s*[\d]|answer\s*:\s*[\w])', r, re.I):
        return 0.90
    # Truncated mid-sentence
    if r[-1] not in '.!?»"':
        return 0.55
    return 0.80

def clean_str(raw: bytes) -> str:
    """Null-strip and decode bytes from binary entry."""
    end = raw.find(b'\x00')
    if end >= 0:
        raw = raw[:end]
    return raw.decode('utf-8', errors='replace').strip()

# ── Readers ───────────────────────────────────────────────────────────────────
def read_binary(path: Path):
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        print(f"[ERROR] File too small: {len(data)} bytes", file=sys.stderr)
        return []

    magic, version, entry_count, total_turns, boot_count = struct.unpack_from('<IIIII', data, 0)
    if magic != MAGIC:
        print(f"[ERROR] Bad magic 0x{magic:08X} (expected 0x{MAGIC:08X})", file=sys.stderr)
        return []

    print(f"[journal] version={version} entries={entry_count} "
          f"total_turns={total_turns} boot_count={boot_count}")

    entries = []
    offset = HEADER_SIZE
    for i in range(entry_count):
        if offset + ENTRY_SIZE > len(data):
            print(f"[WARN] Truncated at entry {i}", file=sys.stderr)
            break
        chunk = data[offset:offset + ENTRY_SIZE]
        prompt_raw   = chunk[0:PROMPT_LEN]
        response_raw = chunk[PROMPT_LEN:PROMPT_LEN + RESPONSE_LEN]
        turn,    = struct.unpack_from('<i', chunk, PROMPT_LEN + RESPONSE_LEN)
        session, = struct.unpack_from('<I', chunk, PROMPT_LEN + RESPONSE_LEN + 4)
        flags,   = struct.unpack_from('<I', chunk, PROMPT_LEN + RESPONSE_LEN + 12)

        offset += ENTRY_SIZE
        if not (flags & ENTRY_FLAG_VALID):
            continue

        p = clean_str(prompt_raw)
        r = clean_str(response_raw)
        if not p or not r:
            continue
        entries.append({'prompt': p, 'response': r, 'turn': turn, 'session': int(session)})

    return entries

def read_jsonl(path: Path):
    entries = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                p = obj.get('prompt') or obj.get('input') or obj.get('instruction', '')
                r = obj.get('response') or obj.get('output') or obj.get('completion', '')
                if p and r:
                    entries.append({
                        'prompt': p.strip(),
                        'response': r.strip(),
                        'turn': obj.get('turn', 0),
                        'session': obj.get('session', 0),
                    })
            except json.JSONDecodeError:
                continue
    return entries

# ── Writer ────────────────────────────────────────────────────────────────────
def write_datasets(entries, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)

    buckets = {}  # domain → list of records
    seen_hashes = set()
    total_skipped = 0

    for e in entries:
        # Dedup by prompt hash
        h = hashlib.md5((e['prompt'] + e['response']).encode()).hexdigest()
        if h in seen_hashes:
            total_skipped += 1
            continue
        seen_hashes.add(h)

        domain = detect_domain(e['prompt'], e['response'])
        halt_p = estimate_halt_prob(e['response'])

        record = {
            "prompt":    e['prompt'],
            "response":  e['response'],
            "domain":    domain,
            "halt_prob": round(halt_p, 3),
            "session":   e['session'],
            "turn":      e['turn'],
        }
        buckets.setdefault(domain, []).append(record)
        # High-value halt samples go to dedicated file too
        if halt_p < 0.45:
            buckets.setdefault("HALT_LOW", []).append(record)

    stats = {}
    for domain, records in buckets.items():
        fname = out_dir / f"journal_{domain.lower()}.jsonl"
        with open(fname, 'w', encoding='utf-8') as f:
            for r in records:
                f.write(json.dumps(r, ensure_ascii=False) + '\n')
        stats[domain] = len(records)
        print(f"  [{domain:10s}] {len(records):4d} samples → {fname.name}")

    print(f"  [SKIPPED  ] {total_skipped:4d} duplicates removed")

    # Write metadata
    meta = {"total": sum(stats.values()), "skipped": total_skipped, "domains": stats}
    (out_dir / "metadata.json").write_text(json.dumps(meta, indent=2))
    return stats

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="OO journal → JSONL training datasets")
    ap.add_argument('--input',  required=True, help="soma_journal.bin or soma_train.jsonl")
    ap.add_argument('--output', default='data/journal_export', help="Output directory")
    ap.add_argument('--dry-run', action='store_true', help="Stats only, no files written")
    args = ap.parse_args()

    src = Path(args.input)
    if not src.exists():
        print(f"[ERROR] Not found: {src}", file=sys.stderr)
        sys.exit(1)

    print(f"[journal_to_dataset] reading {src}")
    if src.suffix == '.bin':
        entries = read_binary(src)
    else:
        entries = read_jsonl(src)

    print(f"[journal_to_dataset] {len(entries)} valid entries loaded")

    if args.dry_run:
        for domain, pat in DOMAIN_PATTERNS.items():
            n = sum(1 for e in entries if detect_domain(e['prompt'], e['response']) == domain)
            print(f"  {domain}: {n}")
        print(f"  CHAT: {sum(1 for e in entries if detect_domain(e['prompt'], e['response']) == 'CHAT')}")
        return

    stats = write_datasets(entries, Path(args.output))
    total = sum(stats.values())
    print(f"[journal_to_dataset] done — {total} records in {len(stats)} domains")

if __name__ == '__main__':
    main()
