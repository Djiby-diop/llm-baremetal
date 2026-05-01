from __future__ import annotations

"""
DIOP Native Model — Tokenizer Wrapper
======================================
Wraps the LLaMA tokenizer (tokenizer.model / tokenizer.bin) for use in training.
Falls back to a character-level tokenizer when no sentencepiece model is found,
so training can always start even without external dependencies.
"""

import json
import os
import struct
from pathlib import Path
from typing import Optional


class DiopTokenizer:
    """
    Priority order for tokenizer backend:
      1. sentencepiece (if installed + tokenizer.model exists)
      2. llama2.c tokenizer.bin  (custom binary format)
      3. CharTokenizer           (zero-dependency fallback)
    """

    def __init__(self, model_path: Optional[Path] = None):
        self._backend = "char"
        self._sp = None
        self._vocab: list[str] = []
        self._encode_map: dict[str, int] = {}
        self.vocab_size = 256  # char fallback default

        if model_path and model_path.exists():
            suffix = model_path.suffix.lower()
            if suffix == ".model":
                self._load_sentencepiece(model_path)
            elif suffix == ".bin":
                self._load_llama2c_bin(model_path)
        else:
            self._init_char_tokenizer()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def encode(self, text: str) -> list[int]:
        if self._backend == "sentencepiece":
            return self._sp.encode(text, out_type=int)
        if self._backend == "llama2c":
            return self._encode_bpe(text)
        # char fallback
        return [ord(c) & 0xFF for c in text]

    def decode(self, ids: list[int]) -> str:
        if self._backend == "sentencepiece":
            return self._sp.decode(ids)
        if self._backend == "llama2c":
            return "".join(self._vocab[i] if i < len(self._vocab) else "?" for i in ids)
        return "".join(chr(i) for i in ids)

    # ------------------------------------------------------------------
    # Loaders
    # ------------------------------------------------------------------

    def _load_sentencepiece(self, path: Path) -> None:
        try:
            import sentencepiece as spm  # type: ignore
            self._sp = spm.SentencePieceProcessor()
            self._sp.Load(str(path))
            self.vocab_size = self._sp.GetPieceSize()
            self._backend = "sentencepiece"
            print(f"[Tokenizer] SentencePiece backend — vocab {self.vocab_size}")
        except ImportError:
            print("[Tokenizer] sentencepiece not installed, falling back to char tokenizer.")
            self._init_char_tokenizer()

    def _load_llama2c_bin(self, path: Path) -> None:
        """
        llama2.c tokenizer.bin format:
          4 bytes: max_token_length (int32)
          For each token:
            4 bytes: score (float32)
            4 bytes: token_len (int32)
            token_len bytes: token bytes
        """
        try:
            with path.open("rb") as f:
                max_token_length = struct.unpack("<i", f.read(4))[0]
                vocab: list[str] = []
                scores: list[float] = []
                while True:
                    raw = f.read(4)
                    if len(raw) < 4:
                        break
                    score = struct.unpack("<f", raw)[0]
                    length = struct.unpack("<i", f.read(4))[0]
                    token = f.read(length).decode("utf-8", errors="replace")
                    vocab.append(token)
                    scores.append(score)

            self._vocab = vocab
            self._encode_map = {t: i for i, t in enumerate(vocab)}
            self.vocab_size = len(vocab)
            self._backend = "llama2c"
            print(f"[Tokenizer] llama2.c binary backend — vocab {self.vocab_size}")
        except Exception as e:
            print(f"[Tokenizer] Failed to load tokenizer.bin ({e}), falling back to char.")
            self._init_char_tokenizer()

    def _init_char_tokenizer(self) -> None:
        self._vocab = [chr(i) for i in range(256)]
        self._encode_map = {c: i for i, c in enumerate(self._vocab)}
        self.vocab_size = 256
        self._backend = "char"
        print("[Tokenizer] Character-level fallback tokenizer active (vocab=256).")

    # ------------------------------------------------------------------
    # BPE encode (greedy longest-match for llama2c vocab)
    # ------------------------------------------------------------------

    def _encode_bpe(self, text: str) -> list[int]:
        """Greedy longest-match BPE tokenization."""
        ids: list[int] = []
        i = 0
        while i < len(text):
            best_id = -1
            best_len = 0
            for length in range(min(16, len(text) - i), 0, -1):
                token = text[i:i + length]
                if token in self._encode_map:
                    best_id = self._encode_map[token]
                    best_len = length
                    break
            if best_id == -1:
                # unknown char -> use char-level encoding
                ids.append(ord(text[i]) & 0xFF)
                i += 1
            else:
                ids.append(best_id)
                i += best_len
        return ids
