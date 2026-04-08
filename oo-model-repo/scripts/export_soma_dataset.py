"""
export_soma_dataset.py — SomaMind Phase L: Journal → oo-model Training Pipeline

Reads soma_train.jsonl produced by the bare-metal kernel (Phase K), validates
and cleans entries, maps cortex domain integers to oo-model domain strings, and
outputs oo-model-compatible JSONL ready for train_oo_native.py.

Usage
-----
  # Preview (dry-run, print stats only):
  python scripts/export_soma_dataset.py --input soma_train.jsonl --dry-run

  # Convert and write to separate file:
  python scripts/export_soma_dataset.py --input soma_train.jsonl \
         --output data/processed/soma_corpus.jsonl

  # Convert and APPEND to existing training split:
  python scripts/export_soma_dataset.py --input soma_train.jsonl \
         --output data/processed/train.jsonl --append

  # With safety threshold (drop entries flagged below threshold):
  python scripts/export_soma_dataset.py --input soma_train.jsonl \
         --min-safety 30 --output data/processed/soma_corpus.jsonl

  # After appending, re-generate manifest:
  python scripts/prepare_dataset.py --recount

Soma domain → oo-model domain mapping
--------------------------------------
  0 GENERAL    → "system"
  1 MEMORY     → "arch"        (memory architecture)
  2 SYSTEM     → "system"
  3 REASONING  → "policy"      (logic/D+ reasoning)
  4 PLANNING   → "policy"
  5 MATH       → "math"
  6 LANGUAGE   → "chat"

Dark-loop heuristic (complexity proxy)
--------------------------------------
  response_len < 60 chars  → 3 loops
  response_len < 150 chars → 4 loops
  response_len < 300 chars → 5 loops
  else                     → 6 loops
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterator

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Maps soma cortex domain integer (0-6) → oo-model domain string.
# Must match SOMA_DOMAIN_* enum in engine/ssm/soma_cortex.h.
SOMA_TO_OO_DOMAIN: dict[int, str] = {
    0: "system",    # SOMA_DOMAIN_GENERAL
    1: "arch",      # SOMA_DOMAIN_MEMORY    (memory arch / zone management)
    2: "system",    # SOMA_DOMAIN_SYSTEM
    3: "policy",    # SOMA_DOMAIN_REASONING (D+, logic, constraints)
    4: "policy",    # SOMA_DOMAIN_PLANNING
    5: "math",      # SOMA_DOMAIN_MATH
    6: "chat",      # SOMA_DOMAIN_LANGUAGE
}

# oo-model valid domain values (from inspect of train.jsonl)
OO_VALID_DOMAINS = {"policy", "math", "arch", "system", "chat"}

# Minimum response length to keep (chars)
MIN_RESPONSE_LEN = 4

# Minimum prompt/instruction length to keep
MIN_PROMPT_LEN = 3

# ANSI escape stripper (kernel output may contain escape sequences)
_ANSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")

# OO memory tag stripper: "[MEM: turn=N sim=S boot=N] " injected by soma_memory
_MEM_TAG_RE = re.compile(r"\[MEM:[^\]]*\]\s*")


# ---------------------------------------------------------------------------
# Dark-loop heuristic
# ---------------------------------------------------------------------------

def compute_dark_loops(response: str) -> int:
    """Estimate number of latent thinking loops from response complexity."""
    n = len(response)
    if n < 60:
        return 3
    if n < 150:
        return 4
    if n < 300:
        return 5
    return 6


# ---------------------------------------------------------------------------
# Cleaning
# ---------------------------------------------------------------------------

def clean_text(text: str) -> str:
    """Strip ANSI codes, memory injection tags, normalise whitespace."""
    text = _ANSI_RE.sub("", text)
    text = _MEM_TAG_RE.sub("", text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    # Collapse runs of whitespace (not newlines)
    text = re.sub(r"[ \t]{2,}", " ", text)
    return text.strip()


# ---------------------------------------------------------------------------
# Parser for soma_train.jsonl (produced by soma_export.c)
# ---------------------------------------------------------------------------

def parse_soma_jsonl(path: Path) -> Iterator[dict]:
    """Yield raw dicts from soma_train.jsonl, skipping malformed lines."""
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                obj = json.loads(raw)
                yield obj
            except json.JSONDecodeError as exc:
                print(f"  [WARN] line {lineno}: JSON error: {exc}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------

def convert_entry(raw: dict, min_safety: int) -> dict | None:
    """
    Convert a soma_train.jsonl entry to oo-model JSONL format.
    Returns None if the entry should be skipped.
    """
    prompt = raw.get("prompt", "")
    response = raw.get("response", "")
    domain_int = int(raw.get("domain", 0))
    safety = int(raw.get("safety", 100))
    session = int(raw.get("session", 0))
    turn = int(raw.get("turn", 0))

    # Safety filter: drop entries explicitly flagged as unsafe
    # (soma_cortex safety_score < 30 ≈ flagged)
    if safety < min_safety:
        return None

    # Clean text
    prompt = clean_text(prompt)
    response = clean_text(response)

    # Length filter
    if len(prompt) < MIN_PROMPT_LEN or len(response) < MIN_RESPONSE_LEN:
        return None

    # Skip entries that are just "/help" or similar REPL commands
    if prompt.startswith("/") and len(prompt) < 20:
        return None

    # Map domain
    oo_domain = SOMA_TO_OO_DOMAIN.get(domain_int, "system")

    # Dark loops
    dark_loops = compute_dark_loops(response)

    return {
        "instruction": prompt,
        "dark_loops": dark_loops,
        "response": response,
        "domain": oo_domain,
        # Preserve provenance metadata as optional fields
        "_soma_session": session,
        "_soma_turn": turn,
        "_soma_safety": safety,
        "_soma_domain_raw": domain_int,
    }


# ---------------------------------------------------------------------------
# Statistics
# ---------------------------------------------------------------------------

class Stats:
    def __init__(self) -> None:
        self.total = 0
        self.skipped_safety = 0
        self.skipped_length = 0
        self.skipped_repl = 0
        self.written = 0
        self.by_domain: dict[str, int] = {}
        self.by_loops: dict[int, int] = {}

    def record(self, entry: dict) -> None:
        self.written += 1
        d = entry["domain"]
        self.by_domain[d] = self.by_domain.get(d, 0) + 1
        lp = entry["dark_loops"]
        self.by_loops[lp] = self.by_loops.get(lp, 0) + 1

    def print_report(self) -> None:
        print(f"\n{'─'*50}")
        print(f"  soma_train.jsonl → oo-model pipeline report")
        print(f"{'─'*50}")
        print(f"  Input records   : {self.total}")
        print(f"  Written         : {self.written}")
        print(f"  Skipped (safety): {self.skipped_safety}")
        print(f"  Skipped (length): {self.skipped_length}")
        print(f"  Skipped (REPL)  : {self.skipped_repl}")
        if self.written:
            print(f"\n  By domain:")
            for d, n in sorted(self.by_domain.items()):
                print(f"    {d:<12}: {n}")
            print(f"\n  By dark_loops:")
            for lp, n in sorted(self.by_loops.items()):
                print(f"    loops={lp}: {n}")
        print(f"{'─'*50}\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export soma_train.jsonl → oo-model training JSONL"
    )
    parser.add_argument(
        "--input", "-i",
        required=True,
        type=Path,
        help="Path to soma_train.jsonl (from USB EFI partition or copy)",
    )
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=None,
        help="Output JSONL file. Default: data/processed/soma_corpus.jsonl",
    )
    parser.add_argument(
        "--append", "-a",
        action="store_true",
        help="Append to existing output file instead of overwriting",
    )
    parser.add_argument(
        "--min-safety",
        type=int,
        default=0,
        help="Minimum safety score (0-100). Default: 0 (keep all). "
             "Set to 30 to drop entries flagged by cortex.",
    )
    parser.add_argument(
        "--strip-metadata",
        action="store_true",
        help="Remove _soma_* provenance fields from output (cleaner for training)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Parse and validate but do not write output. Prints stats.",
    )
    args = parser.parse_args()

    # Resolve output path
    if args.output is None:
        root = Path(__file__).resolve().parents[1]
        args.output = root / "data" / "processed" / "soma_corpus.jsonl"

    # Validate input
    if not args.input.exists():
        print(f"ERROR: input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    stats = Stats()

    # Collect converted entries
    entries: list[dict] = []
    for raw in parse_soma_jsonl(args.input):
        stats.total += 1
        result = convert_entry(raw, min_safety=args.min_safety)
        if result is None:
            # Determine skip reason for stats
            prompt = clean_text(raw.get("prompt", ""))
            response = clean_text(raw.get("response", ""))
            safety = int(raw.get("safety", 100))
            if safety < args.min_safety:
                stats.skipped_safety += 1
            elif prompt.startswith("/") and len(prompt) < 20:
                stats.skipped_repl += 1
            else:
                stats.skipped_length += 1
            continue

        if args.strip_metadata:
            result = {k: v for k, v in result.items() if not k.startswith("_")}

        stats.record(result)
        entries.append(result)

    stats.print_report()

    if args.dry_run:
        print("  [DRY RUN] No output written.")
        return

    if not entries:
        print("  No valid entries to write. Exiting.", file=sys.stderr)
        sys.exit(0)

    # Write output
    mode = "a" if args.append else "w"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open(mode, encoding="utf-8") as fh:
        for entry in entries:
            fh.write(json.dumps(entry, ensure_ascii=False) + "\n")

    action = "Appended" if args.append else "Written"
    print(f"  {action} {len(entries)} records → {args.output}")

    # Count total lines if appending
    if args.append:
        with args.output.open("r", encoding="utf-8") as fh:
            total_lines = sum(1 for ln in fh if ln.strip())
        print(f"  Total records in file: {total_lines}")


if __name__ == "__main__":
    main()
