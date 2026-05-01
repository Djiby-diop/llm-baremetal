from __future__ import annotations

"""
DIOP Native Model — Architecture Configuration
==============================================
Pilier 2: Defines the mini-Transformer architecture.

Target: ~120M parameters, specialized on bare-metal C/Rust/Zig code.
Compatible with the llama2.c checkpoint format (.bin) so diop_engine.c
can load it directly without any format conversion.

llama2.c checkpoint header layout (binary, little-endian):
  int dim        - transformer dimension
  int hidden_dim - FFN hidden dimension  
  int n_layers   - number of layers
  int n_heads    - number of attention heads
  int n_kv_heads - number of KV heads (GQA)
  int vocab_size - vocabulary size
  int seq_len    - max sequence length
"""

import json
import struct
from dataclasses import asdict, dataclass, field
from pathlib import Path


@dataclass
class DiopModelConfig:
    """
    Architecture parameters for the DIOP native mini-model.
    All values chosen to stay under 200MB on disk (Q4 quantized).
    """
    # Transformer dimensions
    dim: int = 512           # embedding dimension
    hidden_dim: int = 1376   # FFN hidden (= dim * 8/3, rounded to multiple of 256)
    n_layers: int = 8        # transformer blocks
    n_heads: int = 8         # attention heads
    n_kv_heads: int = 4      # GQA key-value heads (halved for memory efficiency)
    
    # Vocabulary
    vocab_size: int = 32000  # same as LLaMA tokenizer
    seq_len: int = 512       # context window (bare-metal prompts are short)
    
    # Training hyperparameters (not stored in binary checkpoint)
    max_iters: int = 2000
    eval_interval: int = 200
    learning_rate: float = 3e-4
    batch_size: int = 4
    grad_clip: float = 1.0
    warmup_iters: int = 100
    
    # Domain tags (for documentation only)
    domain: str = "bare-metal-oo"
    version: str = "0.1.0"

    @property
    def n_params(self) -> int:
        """Approximate parameter count."""
        embed   = self.vocab_size * self.dim
        attn    = self.n_layers * (4 * self.dim * self.dim)
        ffn     = self.n_layers * (3 * self.dim * self.hidden_dim)
        return embed + attn + ffn

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8") as f:
            json.dump(asdict(self), f, indent=2)
        print(f"[ModelConfig] Saved config -> {path}")

    @classmethod
    def load(cls, path: Path) -> "DiopModelConfig":
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        # Remove non-constructor keys before passing
        for k in ("domain", "version"):
            data.pop(k, None)
        # Filter only dataclass fields
        valid = {f.name for f in cls.__dataclass_fields__.values()}
        filtered = {k: v for k, v in data.items() if k in valid}
        return cls(**filtered)

    def to_llama2c_header(self) -> bytes:
        """
        Pack into the 7-integer binary header expected by llama2.c / diop_engine.c.
        Negative vocab_size signals 'shared weights' flag used by llama2.c.
        """
        return struct.pack(
            "<iiiiiii",
            self.dim,
            self.hidden_dim,
            self.n_layers,
            self.n_heads,
            self.n_kv_heads,
            -self.vocab_size,  # negative = shared embedding weights
            self.seq_len,
        )


# Pre-defined profiles for different use cases
PROFILES = {
    "micro": DiopModelConfig(
        dim=192, hidden_dim=512, n_layers=4, n_heads=4, n_kv_heads=2,
        vocab_size=32000, seq_len=1024, max_iters=500,
    ),
    "mini": DiopModelConfig(
        dim=512, hidden_dim=1376, n_layers=8, n_heads=8, n_kv_heads=4,
        vocab_size=32000, seq_len=2048, max_iters=500,
    ),
    "base": DiopModelConfig(
        dim=768, hidden_dim=2048, n_layers=12, n_heads=12, n_kv_heads=6,
        vocab_size=32000, seq_len=4096, max_iters=5000,
    ),
}
