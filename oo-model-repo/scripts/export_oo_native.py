"""
Export OO Native model vers format binaire OONV.
Compatible ssm_infer.c (llm-baremetal).

Format binaire (little-endian):
  Header (magic + config):
    magic       : u32 = 0x4F4F4E56  ("OONV")
    version     : u32 = 1
    vocab_size  : u32
    d_model     : u32
    n_layer     : u32
    d_state     : u32
    d_conv      : u32
    expand      : u32
    context_len : u32

  Embedding weights:
    embed[vocab_size * d_model] : f32

  Per-layer weights (n_layer times):
    in_proj.weight  [2*d_inner * d_model] : f32
    conv1d.weight   [d_inner * d_conv]    : f32
    conv1d.bias     [d_inner]             : f32
    x_proj.weight   [(dt_rank+2*d_state) * d_inner] : f32
    dt_proj.weight  [d_inner * dt_rank]   : f32
    dt_proj.bias    [d_inner]             : f32
    A_log           [d_inner * d_state]   : f32
    D               [d_inner]             : f32
    out_proj.weight [d_model * d_inner]   : f32
    norm.weight     [d_model]             : f32
    norm.bias       [d_model]             : f32

  Output norm:
    norm_out.weight [d_model] : f32
    norm_out.bias   [d_model] : f32

  OO Heads:
    policy_head (3 linear layers)
    pressure_head (3 linear layers)
    halt_head (3 linear layers)
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import torch


MAGIC   = 0x4F4F4E56  # "OONV"
VERSION = 1


def write_f32(f, t: torch.Tensor) -> int:
    data = t.detach().float().cpu().contiguous().numpy().tobytes()
    f.write(data)
    return len(data)


def export(checkpoint_path: str, out_path: str) -> None:
    sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
    from oo_model.oo_native import OONativeModel, OONativeConfig

    ckpt = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    cfg_dict = ckpt["config"]
    cfg = OONativeConfig(**cfg_dict)
    model = OONativeModel(cfg)
    model.load_state_dict(ckpt["model_state"])
    model.eval()
    sd = model.state_dict()

    print(f"[export] Model: {cfg.n_layer}L d_model={cfg.d_model} vocab={cfg.vocab_size}")

    total_bytes = 0
    with open(out_path, "wb") as f:
        # ── Header ──
        hdr = struct.pack(
            "<IIIIIIIII",
            MAGIC, VERSION,
            cfg.vocab_size, cfg.d_model, cfg.n_layer,
            cfg.d_state, cfg.d_conv, cfg.expand, cfg.context_length,
        )
        f.write(hdr)
        total_bytes += len(hdr)

        # ── Embedding ──
        total_bytes += write_f32(f, sd["embedding.weight"])

        # ── Layers ──
        for i in range(cfg.n_layer):
            p = f"layers.{i}"
            for key in (
                f"{p}.in_proj.weight",
                f"{p}.conv1d.weight",
                f"{p}.conv1d.bias",
                f"{p}.x_proj.weight",
                f"{p}.dt_proj.weight",
                f"{p}.dt_proj.bias",
                f"{p}.A_log",
                f"{p}.D",
                f"{p}.out_proj.weight",
                f"{p}.norm.weight",
                f"{p}.norm.bias",
            ):
                if key in sd:
                    total_bytes += write_f32(f, sd[key])
                else:
                    print(f"[WARN] missing: {key}")

        # ── Output norm ──
        total_bytes += write_f32(f, sd["norm_out.weight"])
        total_bytes += write_f32(f, sd["norm_out.bias"])

        # ── OO Heads (all linear layers in order) ──
        for head in ("policy_head", "pressure_head", "halt_head"):
            for k in sorted(k for k in sd if k.startswith(head)):
                total_bytes += write_f32(f, sd[k])

    mb = total_bytes / (1024 * 1024)
    print(f"[export] Written: {out_path} ({mb:.2f} MB)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: export_oo_native.py <checkpoint.pt> <output.bin>")
        sys.exit(1)
    export(sys.argv[1], sys.argv[2])
