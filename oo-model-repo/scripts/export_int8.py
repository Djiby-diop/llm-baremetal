#!/usr/bin/env python3
"""
export_int8.py — Export Mamba-2.8B vers .bin quantisé int8 pour bare-metal.

Pourquoi int8 ?
  - float32 = ~10.5 GB (trop grand pour UEFI / RAM bare-metal)
  - int8    = ~2.6 GB  (divisé par 4)
  - Précision dégradée de ~1-2% en génération (acceptable pour OO)

Format .bin (compatible ssm_infer.c) :
  Header (36 bytes, little-endian u32) :
    magic=0x4F4F5349 ("OOSI" = OO SSM Int8)
    version=2
    d_model, n_layer, d_state, d_conv, expand, vocab_size, halt_d_input

  Quantization header (per-layer + embedding) :
    scale : float32[out_features]   (scale per output channel)
    zero  : float32[out_features]   (zero point)

  Per-layer SSM weights (int8, row-quantized) :
    x_proj.weight  : int8[out * in]
    dt_proj.weight : int8[out * in]
    dt_proj.bias   : float32[out]  (biases kept float32)

  Embedding :
    embed_tokens.weight : int8[vocab * d_model]

  HaltingHead :
    (kept float32 — small, ~5 MB)

Usage:
    python export_int8.py <model_dir> <output.bin> [halting_head.pt]

Example:
    python export_int8.py "C:\\model" "C:\\Temp\\oo_mamba_int8.bin"
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import torch

MAGIC_INT8 = 0x4F4F5349  # "OOSI"
VERSION    = 2


def quantize_tensor_int8(t: torch.Tensor):
    """Per-row symmetric int8 quantization. Returns (int8_tensor, scale_f32)."""
    t = t.detach().float()
    # Per-row absmax
    scale = t.abs().max(dim=-1, keepdim=True).values.clamp(min=1e-8)
    q = (t / scale * 127.0).round().clamp(-128, 127).to(torch.int8)
    return q, scale.squeeze(-1)


def write_f32(f, t: torch.Tensor) -> int:
    data = t.detach().float().cpu().contiguous().numpy().tobytes()
    f.write(data)
    return len(data)


def write_int8(f, t: torch.Tensor) -> int:
    data = t.cpu().contiguous().numpy().tobytes()
    f.write(data)
    return len(data)


def write_quantized(f, t: torch.Tensor) -> int:
    """Quantize + write: scale(f32) then int8 weights."""
    q, scale = quantize_tensor_int8(t)
    n = write_f32(f, scale)   # scale per row
    n += write_int8(f, q)     # int8 weights
    return n


def build_halting_head(d_input=2561):
    import torch.nn as nn
    class HaltingHead(nn.Module):
        def __init__(self, d):
            super().__init__()
            self.net = nn.Sequential(
                nn.Linear(d, 512), nn.GELU(), nn.Dropout(0.1),
                nn.Linear(512, 64), nn.GELU(), nn.Linear(64, 1), nn.Sigmoid()
            )
        def forward(self, x): return self.net(x).squeeze(-1)
    return HaltingHead(d_input)


def export(model_dir: str, out_path: str, halt_path: str | None = None) -> None:
    from transformers import AutoModelForCausalLM

    model_dir = Path(model_dir)
    out_path  = Path(out_path)

    print(f"[export-int8] Loading model from {model_dir} (float32)...")
    model = AutoModelForCausalLM.from_pretrained(
        str(model_dir),
        dtype=torch.float32,
        trust_remote_code=True,
    )
    sd  = model.state_dict()
    cfg = model.config

    # HaltingHead
    if halt_path is None:
        halt_path = model_dir / "halting_head.pt"
    print(f"[export-int8] Loading HaltingHead from {halt_path}...")
    ckpt = torch.load(str(halt_path), weights_only=True, map_location="cpu")
    d_input = ckpt.get("d_input", 2561)
    head    = build_halting_head(d_input)
    head.load_state_dict(ckpt["state_dict"])
    head_sd = head.state_dict()

    d_model    = int(getattr(cfg, "hidden_size", getattr(cfg, "d_model", 2560)))
    n_layer    = int(cfg.n_layer)
    d_state    = int(getattr(cfg, "state_size", getattr(cfg, "d_state", 16)))
    d_conv     = int(getattr(cfg, "conv_kernel", getattr(cfg, "d_conv", 4)))
    expand     = int(getattr(cfg, "expand", 2))
    vocab_size = int(cfg.vocab_size)

    print(f"[export-int8] Config: d_model={d_model} n_layer={n_layer} "
          f"d_state={d_state} vocab={vocab_size} halt_d_input={d_input}")
    print(f"[export-int8] Writing → {out_path}")

    total = 0
    with out_path.open("wb") as f:
        # Header
        hdr = struct.pack("<IIIIIIIII",
            MAGIC_INT8, VERSION,
            d_model, n_layer, d_state, d_conv, expand,
            vocab_size, d_input)
        f.write(hdr)
        total += len(hdr)

        # Per-layer SSM weights (quantized)
        for i in range(n_layer):
            pfx = f"backbone.layers.{i}.mixer"
            for wkey in (f"{pfx}.x_proj.weight", f"{pfx}.dt_proj.weight"):
                key = wkey
                if key not in sd:
                    key = key.replace("backbone.layers", "model.layers")
                if key in sd:
                    total += write_quantized(f, sd[key])
                else:
                    print(f"  [WARN] missing: {wkey}")

            # biases stay float32
            bkey = f"{pfx}.dt_proj.bias"
            if bkey not in sd:
                bkey = bkey.replace("backbone.layers", "model.layers")
            if bkey in sd:
                total += write_f32(f, sd[bkey])
            else:
                print(f"  [WARN] missing bias: {pfx}.dt_proj.bias")

            if (i + 1) % 16 == 0:
                pct = (i + 1) / n_layer * 100
                gb  = total / (1024**3)
                print(f"  layer {i+1}/{n_layer} ({pct:.0f}%) — {gb:.2f} GB written")

        # Embedding (quantized)
        for ekey in ["backbone.embeddings.weight",
                     "backbone.embed_tokens.weight",
                     "model.embed_tokens.weight"]:
            if ekey in sd:
                print(f"  [embed] quantizing {ekey}...")
                total += write_quantized(f, sd[ekey])
                break
        else:
            print("  [WARN] embed_tokens.weight not found")

        # HaltingHead — kept float32 (only ~5 MB)
        print("  [halt] writing HaltingHead (float32)...")
        for key in sorted(head_sd.keys()):
            total += write_f32(f, head_sd[key])

    mb = total / (1024 * 1024)
    gb = total / (1024 ** 3)
    print(f"\n[export-int8] Done: {out_path}")
    print(f"  Size    : {mb:.1f} MB ({gb:.2f} GB)")
    print(f"  Bytes   : {total:,}")
    print(f"  Format  : OOSI v2 (int8 quantized, scale per row)")
    print(f"  vs f32  : {gb:.2f} GB vs {gb*4:.2f} GB (÷{4.0:.0f}x)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: export_int8.py <model_dir> <output.bin> [halting_head.pt]")
        print()
        print("Example:")
        print('  python export_int8.py "C:\\model" "C:\\Temp\\oo_mamba_int8.bin"')
        sys.exit(1)
    export(
        model_dir  = sys.argv[1],
        out_path   = sys.argv[2],
        halt_path  = sys.argv[3] if len(sys.argv) > 3 else None,
    )
