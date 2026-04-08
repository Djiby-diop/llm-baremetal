"""
Export OO Mamba engine weights to bare-metal binary format.
Compatible with ssm_infer.c in llm-baremetal.

Output layout (little-endian, no padding):
  Header:
    magic       : u32 = 0x4F4F5353  ("OOSS")
    version     : u32 = 1
    d_model     : u32
    n_layer     : u32
    d_state     : u32
    d_conv      : u32
    expand      : u32
    vocab_size  : u32
    halting_head_d_input : u32

  Per-layer weights (n_layer times):
    x_proj.weight : float32[out_features * in_features]
    dt_proj.weight: float32[out_features * in_features]
    dt_proj.bias  : float32[out_features]

  Embed tokens:
    embed_tokens.weight: float32[vocab_size * d_model]

  HaltingHead weights (sequential MLP layers):
    layer_0.weight: float32[512 * d_input]
    layer_0.bias  : float32[512]
    layer_2.weight: float32[64 * 512]
    layer_2.bias  : float32[64]
    layer_4.weight: float32[1 * 64]
    layer_4.bias  : float32[1]
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

MAGIC = 0x4F4F5353  # "OOSS"
VERSION = 1


def write_tensor(f, t: torch.Tensor) -> int:
    data = t.detach().float().cpu().contiguous().numpy().tobytes()
    f.write(data)
    return len(data)


def resolve_halting_checkpoint(engine_dir: Path, halt_path: str | None = None) -> Path:
    candidates = []
    if halt_path:
        candidates.append(Path(halt_path))
    candidates.extend([
        engine_dir / "halting_head.pt",
        Path("checkpoints/oo-mamba-phase2-halt/halting_head.pt"),
    ])

    for candidate in candidates:
        if candidate.exists():
            return candidate

    searched = "\n - ".join(str(p) for p in candidates)
    raise FileNotFoundError(f"halting_head.pt not found. Searched:\n - {searched}")


def export(engine_dir: str, out_path: str, halt_path: str | None = None) -> None:
    from transformers import AutoModelForCausalLM
    from oo_model.mamba_model import HaltingHead

    engine_dir = Path(engine_dir)
    out_path = Path(out_path)

    print(f"[export] Loading model from {engine_dir}...")
    model = AutoModelForCausalLM.from_pretrained(
        str(engine_dir),
        torch_dtype=torch.float32,
        trust_remote_code=True,
    )
    sd = model.state_dict()

    # Load HaltingHead
    halt_ckpt_path = resolve_halting_checkpoint(engine_dir, halt_path)
    print(f"[export] Loading halting head from {halt_ckpt_path}...")
    halt_ckpt = torch.load(halt_ckpt_path, weights_only=True)
    d_input = halt_ckpt["d_input"]
    head = HaltingHead(d_input=d_input)
    head.load_state_dict(halt_ckpt["state_dict"])
    head_sd = head.state_dict()

    # Probe config
    cfg = model.config
    d_model    = int(cfg.d_model)
    n_layer    = int(cfg.n_layer)
    d_state    = int(getattr(cfg, "d_state", 16))
    d_conv     = int(getattr(cfg, "d_conv", 4))
    expand     = int(getattr(cfg, "expand", 2))
    vocab_size = int(cfg.vocab_size)

    print(f"[export] d_model={d_model} n_layer={n_layer} d_state={d_state} vocab_size={vocab_size}")
    print(f"[export] halting_head d_input={d_input}")

    total_bytes = 0
    with out_path.open("wb") as f:
        # Header
        header = struct.pack(
            "<IIIIIIIII",
            MAGIC, VERSION,
            d_model, n_layer, d_state, d_conv, expand,
            vocab_size, d_input,
        )
        f.write(header)
        total_bytes += len(header)

        # Per-layer SSM weights
        for i in range(n_layer):
            prefix = f"backbone.layers.{i}.mixer"
            for key in (
                f"{prefix}.x_proj.weight",
                f"{prefix}.dt_proj.weight",
                f"{prefix}.dt_proj.bias",
            ):
                if key in sd:
                    total_bytes += write_tensor(f, sd[key])
                else:
                    print(f"[WARN] missing key: {key}")

        # Embedding
        embed_key = "backbone.embeddings.weight"
        if embed_key not in sd:
            embed_key = "backbone.embed_tokens.weight"
        if embed_key in sd:
            total_bytes += write_tensor(f, sd[embed_key])
        else:
            print("[WARN] embed_tokens.weight not found")

        # HaltingHead MLP weights in order
        for key in sorted(head_sd.keys()):
            total_bytes += write_tensor(f, head_sd[key])

    mb = total_bytes / (1024 * 1024)
    print(f"[export] Written: {out_path} ({mb:.1f} MB, {total_bytes} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: export_ssm_binary.py <engine_dir> <output.bin> [halting_head.pt]")
        sys.exit(1)
    export(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
