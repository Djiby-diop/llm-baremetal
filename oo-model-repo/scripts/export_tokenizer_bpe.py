"""
Export GPT-NeoX / Mamba-2.8B tokenizer → llama.c BPE binary format.

The llama.c tokenizer.bin format (little-endian):
  Header:
    max_token_length : int32
  Per-token (vocab_size entries):
    score            : float32   (merge priority; use log-prob or rank)
    token_len        : int32
    token_bytes      : uint8[token_len]

Usage:
    python export_tokenizer_bpe.py \
        --tokenizer_json  "path/to/tokenizer.json" \
        --output          "gpt_neox_tokenizer.bin"

The output is drop-in compatible with llmk_tokenizer_init() in llm-baremetal.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path


def load_vocab_from_tokenizer_json(tok_path: Path) -> tuple[list[bytes], list[float]]:
    """
    Returns (vocab_bytes, vocab_scores) in token-id order.
    vocab_bytes[i]  = raw bytes for token i
    vocab_scores[i] = merge score (higher = earlier merge, used by BPE decoder)
    """
    with open(tok_path, "r", encoding="utf-8") as f:
        tok = json.load(f)

    # ── vocab map: token_str → id ────────────────────────────────────────────
    model = tok.get("model", {})
    vocab_map: dict[str, int] = model.get("vocab", {})
    if not vocab_map:
        # HuggingFace fast tokenizer keeps vocab here too
        vocab_map = tok.get("vocab", {})
    if not vocab_map:
        raise RuntimeError("Could not find vocab in tokenizer.json")

    vocab_size = max(vocab_map.values()) + 1
    print(f"[tokenizer] vocab_size = {vocab_size}")

    # ── build id → bytes table ───────────────────────────────────────────────
    # GPT-NeoX uses byte-level BPE with the standard byte_decoder mapping
    # (see openai/gpt-2 byte_to_unicode).  Token strings like "Ġhello"
    # encode space as Ġ (U+0120).  We decode them back to raw bytes.
    byte_decoder = _build_byte_decoder()

    id_to_bytes: list[bytes] = [b""] * vocab_size
    for tok_str, tok_id in vocab_map.items():
        try:
            raw = bytes([byte_decoder[c] for c in tok_str])
        except KeyError:
            # Fallback: encode as UTF-8 (handles special tokens)
            raw = tok_str.encode("utf-8")
        id_to_bytes[tok_id] = raw

    # ── merge scores: use rank (earlier merge = higher priority) ─────────────
    merges: list[str] = model.get("merges", [])
    # Build merge rank map: (a, b) → rank (0 = highest priority)
    merge_rank: dict[tuple[str, str], int] = {}
    for rank, merge_entry in enumerate(merges):
        # merges can be either "a b" string or [a, b] list (HF fast tokenizer)
        if isinstance(merge_entry, (list, tuple)) and len(merge_entry) == 2:
            parts = list(merge_entry)
        elif isinstance(merge_entry, str):
            parts = merge_entry.split(" ", 1)
        else:
            continue
        if len(parts) == 2:
            merge_rank[(parts[0], parts[1])] = rank

    # Score = -rank / total  → in [-1, 0], higher (closer to 0) = better merge
    n_merges = len(merges) if merges else 1
    id_to_score: list[float] = [0.0] * vocab_size

    # Assign merge score: find the merge that produced each token
    # Simple heuristic: if token is the result of merge (a, b), score = -rank/n
    # For base byte tokens (single byte), give score 0.
    # For unknown / special tokens, give score -1.
    for (a, b), rank in merge_rank.items():
        merged = a + b
        if merged in vocab_map:
            tid = vocab_map[merged]
            score = -rank / n_merges
            # Keep highest score (= lowest rank = earliest merge)
            if score > id_to_score[tid]:
                id_to_score[tid] = score

    return id_to_bytes, id_to_score


def _build_byte_decoder() -> dict[str, int]:
    """
    Inverse of the GPT-2 byte_to_unicode map.
    Maps unicode chars back to byte values 0-255.
    """
    bs: list[int] = (
        list(range(ord("!"), ord("~") + 1))
        + list(range(ord("¡"), ord("¬") + 1))
        + list(range(ord("®"), ord("ÿ") + 1))
    )
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    byte_decoder = {chr(c): b for b, c in zip(bs, cs)}
    return byte_decoder


def write_llama_tokenizer(
    out_path: Path,
    vocab_bytes: list[bytes],
    vocab_scores: list[float],
) -> None:
    """Write in llama.c tokenizer.bin format."""
    vocab_size = len(vocab_bytes)
    max_len = max((len(b) for b in vocab_bytes), default=1)

    with open(out_path, "wb") as f:
        # Header: max_token_length (int32)
        f.write(struct.pack("<i", max_len))

        written = 0
        for i in range(vocab_size):
            raw = vocab_bytes[i]
            score = vocab_scores[i]
            f.write(struct.pack("<f", score))
            f.write(struct.pack("<i", len(raw)))
            f.write(raw)
            written += 1

    size_mb = out_path.stat().st_size / (1024 * 1024)
    print(f"[tokenizer] Written {written} tokens → {out_path}  ({size_mb:.2f} MB)")
    print(f"[tokenizer] max_token_length = {max_len}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Export GPT-NeoX tokenizer → llama.c BPE binary")
    ap.add_argument(
        "--tokenizer_json",
        default=None,
        help="Path to tokenizer.json (HuggingFace format)",
    )
    ap.add_argument(
        "--model_dir",
        default=None,
        help="Path to model directory (auto-finds tokenizer.json)",
    )
    ap.add_argument(
        "--output",
        default="gpt_neox_tokenizer.bin",
        help="Output path (default: gpt_neox_tokenizer.bin)",
    )
    args = ap.parse_args()

    # Resolve tokenizer.json
    tok_path: Path | None = None
    if args.tokenizer_json:
        tok_path = Path(args.tokenizer_json)
    elif args.model_dir:
        tok_path = Path(args.model_dir) / "tokenizer.json"
    else:
        # Auto-detect from common locations
        candidates = [
            Path("tokenizer.json"),
            Path("model/tokenizer.json"),
            Path("../tokenizer.json"),
        ]
        for c in candidates:
            if c.exists():
                tok_path = c
                break

    if tok_path is None or not tok_path.exists():
        print(f"ERROR: tokenizer.json not found. Use --tokenizer_json or --model_dir")
        raise SystemExit(1)

    print(f"[tokenizer] Loading: {tok_path}")
    vocab_bytes, vocab_scores = load_vocab_from_tokenizer_json(tok_path)

    out_path = Path(args.output)
    write_llama_tokenizer(out_path, vocab_bytes, vocab_scores)
    print("[tokenizer] Done.")


if __name__ == "__main__":
    main()
