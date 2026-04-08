#!/usr/bin/env python3
"""
export_oosi_v3.py — Export Mamba-2.8B → OOSI v3 full standalone binary.

OOSI v3 includes ALL Mamba block weights so the bare-metal engine can run
full SSM inference WITHOUT needing a separate MAMB float32 binary.

Binary layout (little-endian):
  Header (40 bytes):
    magic           : u32 = 0x4F4F5333  ("OOS3")
    version         : u32 = 3
    d_model         : u32
    n_layer         : u32
    d_state         : u32
    d_conv          : u32
    expand          : u32
    vocab_size      : u32
    dt_rank         : u32
    halt_d_input    : u32

  Per-layer weights (n_layer times, all int8 unless noted):
    norm_weight     : f32[d_model]
    in_proj_scale   : f32[2*d_inner]
    in_proj_q8      : int8[2*d_inner * d_model]
    conv_weight     : f32[d_inner * d_conv]      (small, kept f32)
    conv_bias       : f32[d_inner]
    x_proj_scale    : f32[dt_rank + 2*d_state]
    x_proj_q8       : int8[(dt_rank+2*d_state) * d_inner]
    dt_proj_scale   : f32[d_inner]
    dt_proj_q8      : int8[d_inner * dt_rank]
    dt_proj_bias    : f32[d_inner]
    A_log           : f32[d_inner * d_state]     (small, kept f32)
    D               : f32[d_inner]
    out_proj_scale  : f32[d_model]
    out_proj_q8     : int8[d_model * d_inner]

  Global weights:
    final_norm      : f32[d_model]
    embed_scale     : f32[vocab_size]
    embed_q8        : int8[vocab_size * d_model]
    lm_head_scale   : f32[vocab_size]           (tied=same as embed)
    lm_head_q8      : int8[vocab_size * d_model]

  HaltingHead (float32 MLP, ~5 MB):
    layer_0.weight  : f32[512 * halt_d_input]
    layer_0.bias    : f32[512]
    layer_2.weight  : f32[64 * 512]
    layer_2.bias    : f32[64]
    layer_4.weight  : f32[1 * 64]
    layer_4.bias    : f32[1]

Usage:
    python export_oosi_v3.py <model_dir> <output.bin> [halting_head.pt]

Example:
    python export_oosi_v3.py "C:\\model d'esseyage" C:\\Temp\\oo_v3.bin
"""

from __future__ import annotations

import struct
import sys
import os
from pathlib import Path

import torch

MAGIC_V3  = 0x4F4F5333  # "OOS3"
VERSION   = 3


# ─── Quantization helpers ──────────────────────────────────────────────────

def quantize_int8_rowwise(t: torch.Tensor):
    """Per-row symmetric int8. Returns (int8_tensor, scale_f32_per_row)."""
    t = t.detach().float().contiguous()
    if t.dim() == 1:
        t = t.unsqueeze(0)
    scale = t.abs().max(dim=-1, keepdim=True).values.clamp(min=1e-8)
    q = (t / scale * 127.0).round().clamp(-128, 127).to(torch.int8)
    return q.squeeze(), scale.squeeze()


def write_f32(f, t: torch.Tensor) -> int:
    d = t.detach().float().cpu().contiguous().numpy().tobytes()
    f.write(d)
    return len(d)


def write_int8_quantized(f, t: torch.Tensor) -> int:
    """Write: scale(f32 per row) then int8 weights. Returns bytes written."""
    q, scale = quantize_int8_rowwise(t)
    n = write_f32(f, scale)
    data = q.cpu().contiguous().numpy().tobytes()
    f.write(data)
    return n + len(data)


# ─── HaltingHead builder (same as export_int8.py) ─────────────────────────

def build_halting_head(d_input: int):
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


# ─── Main export ──────────────────────────────────────────────────────────

def export(model_dir: str, out_path: str, halt_path: str | None = None) -> None:
    from transformers import AutoModelForCausalLM

    model_dir = Path(model_dir)
    out_path  = Path(out_path)

    print(f"[oosi-v3] Loading model from {model_dir} ...")
    model = AutoModelForCausalLM.from_pretrained(
        str(model_dir),
        torch_dtype=torch.float32,
        trust_remote_code=True,
    )
    model.eval()
    sd = model.state_dict()
    cfg = model.config

    d_model    = int(getattr(cfg, 'd_model',    getattr(cfg, 'hidden_size',    2560)))
    n_layer    = int(cfg.n_layer)
    d_state    = int(getattr(cfg, 'd_state',    getattr(cfg, 'state_size',     16)))
    d_conv     = int(getattr(cfg, 'd_conv',     getattr(cfg, 'conv_kernel',     4)))
    expand     = int(getattr(cfg, 'expand',     2))
    vocab_size = int(cfg.vocab_size)
    d_inner    = d_model * expand
    dt_rank    = (d_model + 15) // 16  # standard Mamba dt_rank formula

    print(f"[oosi-v3] Config: d_model={d_model} n_layer={n_layer} d_inner={d_inner}")
    print(f"[oosi-v3]         d_state={d_state} d_conv={d_conv} dt_rank={dt_rank} vocab={vocab_size}")

    # ── HaltingHead ────────────────────────────────────────────────────────
    if halt_path is None:
        halt_path = model_dir / "halting_head.pt"
    print(f"[oosi-v3] Loading HaltingHead from {halt_path} ...")
    ckpt = torch.load(str(halt_path), weights_only=True, map_location="cpu")
    halt_d_input = ckpt.get("d_input", d_model + 1)
    head = build_halting_head(halt_d_input)
    head.load_state_dict(ckpt["state_dict"])
    head_sd = head.state_dict()

    # ── Size estimate ─────────────────────────────────────────────────────
    per_layer_bytes = (
        d_model * 4 +                              # norm_weight
        (2*d_inner)*4 + (2*d_inner)*d_model +       # in_proj
        d_inner*d_conv*4 + d_inner*4 +              # conv
        (dt_rank+2*d_state)*4 + (dt_rank+2*d_state)*d_inner +  # x_proj
        d_inner*4 + d_inner*dt_rank +               # dt_proj
        d_inner*4 +                                 # dt_proj_bias
        d_inner*d_state*4 +                         # A_log
        d_inner*4 +                                 # D
        d_model*4 + d_model*d_inner                 # out_proj
    )
    embed_bytes = vocab_size*4 + vocab_size*d_model
    est_total   = n_layer * per_layer_bytes + 2*embed_bytes + d_model*4
    print(f"[oosi-v3] Estimated size: {est_total/1024/1024/1024:.2f} GB")

    # ── Write binary ──────────────────────────────────────────────────────
    total = 0
    with out_path.open("wb") as f:

        # Header (40 bytes)
        hdr = struct.pack("<IIIIIIIIII",
            MAGIC_V3, VERSION,
            d_model, n_layer, d_state, d_conv, expand,
            vocab_size, dt_rank, halt_d_input)
        f.write(hdr)
        total += len(hdr)

        # ── Per-layer weights ─────────────────────────────────────────────
        print(f"[oosi-v3] Writing {n_layer} layers ...")
        for i in range(n_layer):
            pfx1 = f"backbone.layers.{i}.mixer"
            pfx2 = f"model.layers.{i}.mixer"
            norm1 = f"backbone.layers.{i}.norm"
            norm2 = f"model.layers.{i}.norm"

            def get(key, alt=None):
                if key in sd: return sd[key]
                if alt and alt in sd: return sd[alt]
                # Try replacing backbone→model and vice-versa
                k2 = key.replace("backbone.layers", "model.layers")
                if k2 in sd: return sd[k2]
                k3 = key.replace("model.layers", "backbone.layers")
                if k3 in sd: return sd[k3]
                raise KeyError(f"Weight not found: {key}")

            # norm_weight (f32)
            try:
                nw = get(f"{norm1}.weight", f"{norm2}.weight")
            except KeyError:
                # Some Mamba checkpoints store norm inside mixer
                nw = torch.ones(d_model)
                print(f"  [warn] L{i} norm not found, using ones")
            total += write_f32(f, nw)

            # in_proj (int8 quantized)
            try:
                total += write_int8_quantized(f, get(f"{pfx1}.in_proj.weight"))
            except KeyError:
                print(f"  [warn] L{i} in_proj not found, using zeros")
                total += write_int8_quantized(f, torch.zeros(2*d_inner, d_model))

            # conv1d (f32, small)
            try:
                cw = get(f"{pfx1}.conv1d.weight")  # [d_inner, 1, d_conv]
                cw = cw.view(d_inner, d_conv)
            except KeyError:
                cw = torch.zeros(d_inner, d_conv)
                print(f"  [warn] L{i} conv1d.weight not found")
            total += write_f32(f, cw)
            try:
                cb = get(f"{pfx1}.conv1d.bias")
            except KeyError:
                cb = torch.zeros(d_inner)
            total += write_f32(f, cb)

            # x_proj (int8 quantized)
            try:
                total += write_int8_quantized(f, get(f"{pfx1}.x_proj.weight"))
            except KeyError:
                print(f"  [warn] L{i} x_proj not found, using zeros")
                total += write_int8_quantized(f, torch.zeros(dt_rank+2*d_state, d_inner))

            # dt_proj (int8 quantized)
            try:
                total += write_int8_quantized(f, get(f"{pfx1}.dt_proj.weight"))
            except KeyError:
                print(f"  [warn] L{i} dt_proj.weight not found, using zeros")
                total += write_int8_quantized(f, torch.zeros(d_inner, dt_rank))

            # dt_proj_bias (f32)
            try:
                total += write_f32(f, get(f"{pfx1}.dt_proj.bias"))
            except KeyError:
                total += write_f32(f, torch.zeros(d_inner))

            # A_log (f32)
            try:
                total += write_f32(f, get(f"{pfx1}.A_log"))
            except KeyError:
                print(f"  [warn] L{i} A_log not found, using zeros")
                total += write_f32(f, torch.zeros(d_inner, d_state))

            # D (f32)
            try:
                total += write_f32(f, get(f"{pfx1}.D"))
            except KeyError:
                total += write_f32(f, torch.zeros(d_inner))

            # out_proj (int8 quantized)
            try:
                total += write_int8_quantized(f, get(f"{pfx1}.out_proj.weight"))
            except KeyError:
                print(f"  [warn] L{i} out_proj not found, using zeros")
                total += write_int8_quantized(f, torch.zeros(d_model, d_inner))

            if (i + 1) % 8 == 0:
                pct = (i + 1) / n_layer * 100
                gb  = total / (1024**3)
                print(f"  Layer {i+1:3d}/{n_layer} ({pct:3.0f}%) — {gb:.2f} GB written")

        # ── Global weights ────────────────────────────────────────────────
        print("[oosi-v3] Writing final_norm ...")
        for k in ["backbone.norm_f.weight", "model.norm_f.weight", "norm_f.weight"]:
            if k in sd:
                total += write_f32(f, sd[k])
                break
        else:
            total += write_f32(f, torch.ones(d_model))
            print("[warn] final_norm not found, using ones")

        print("[oosi-v3] Writing embedding (int8) ...")
        for ekey in ["backbone.embeddings.weight", "backbone.embed_tokens.weight",
                     "model.embed_tokens.weight", "embeddings.weight"]:
            if ekey in sd:
                total += write_int8_quantized(f, sd[ekey])
                print(f"  embed key: {ekey}")
                embed_weights = sd[ekey]
                break
        else:
            embed_weights = torch.zeros(vocab_size, d_model)
            total += write_int8_quantized(f, embed_weights)
            print("[warn] embedding not found, using zeros")

        print("[oosi-v3] Writing lm_head (tied=embedding) ...")
        # Mamba typically ties lm_head with embedding
        lm_head_key = None
        for k in ["lm_head.weight", "backbone.lm_head.weight"]:
            if k in sd:
                lm_head_key = k
                break
        if lm_head_key:
            total += write_int8_quantized(f, sd[lm_head_key])
            print(f"  lm_head key: {lm_head_key} (separate)")
        else:
            total += write_int8_quantized(f, embed_weights)
            print("  lm_head: tied with embedding")

        # ── HaltingHead (float32) ─────────────────────────────────────────
        print("[oosi-v3] Writing HaltingHead (float32) ...")
        for key in sorted(head_sd.keys()):
            total += write_f32(f, head_sd[key])

        # ── Precomputed neg_exp_A trailer ─────────────────────────────────
        # Magic marker: 0x4E454741 ("NEGA") followed by f32[n_layer * d_inner * d_state]
        # This lets the bare-metal runtime skip exp() at runtime entirely.
        print("[oosi-v3] Writing precomputed neg_exp_A trailer ...")
        NEGA_MAGIC = 0x4E454741  # "NEGA"
        f.write(struct.pack("<I", NEGA_MAGIC))
        total += 4
        nega_count = 0
        for i in range(n_layer):
            pfx1 = f"backbone.layers.{i}.mixer"
            for k in [f"{pfx1}.A_log", f"model.layers.{i}.mixer.A_log"]:
                if k in sd:
                    a_log = sd[k].detach().float()
                    neg_exp_a = -torch.exp(a_log)
                    total += write_f32(f, neg_exp_a)
                    nega_count += 1
                    break
            else:
                # Fallback: write zeros
                total += write_f32(f, torch.zeros(d_inner, d_state))
                nega_count += 1
        print(f"  neg_exp_A: {nega_count} layers, {nega_count * d_inner * d_state * 4 / 1024 / 1024:.1f} MB")

    mb  = total / (1024 * 1024)
    gb  = total / (1024 ** 3)
    print(f"\n[oosi-v3] ✓ Done: {out_path}")
    print(f"  Size    : {mb:.0f} MB ({gb:.2f} GB)")
    print(f"  Format  : OOSI v3 (full Mamba, int8+f32 mixed)")
    print(f"  Layers  : {n_layer} × {per_layer_bytes/1024/1024:.1f} MB/layer")
    print(f"  Note    : Load with /ssm_load in OO REPL (detected via magic 0x4F4F5333)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: export_oosi_v3.py <model_dir> <output.bin> [halting_head.pt]")
        print()
        print("Example:")
        print("  python export_oosi_v3.py \"C:\\model\" C:\\Temp\\oo_v3.bin")
        sys.exit(1)
    export(
        model_dir  = sys.argv[1],
        out_path   = sys.argv[2],
        halt_path  = sys.argv[3] if len(sys.argv) > 3 else None,
    )
