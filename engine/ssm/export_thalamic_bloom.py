#!/usr/bin/env python3
"""
export_thalamic_bloom.py
Export batteryphil/thalamic-bloom (Mamba3MIMORLF) → MAMB bare-metal binary.

Architecture: d_model=768, n_layers=24, vocab=50304, d_state=16, d_conv=4, expand=2
Strategy: export backbone (24 main Mamba layers) + best MIMO arm (arm 0 = OO/general).
          The MIMO domain_router is simplified to a C lookup table.

Usage:
    python export_thalamic_bloom.py --model thalamic_bloom_150m_oo.pth --out thalamic_bloom.mamb
    python export_thalamic_bloom.py --model thalamic_bloom_150m_oo.pth --out thalamic_bloom.mamb --arm 0

Output:
    thalamic_bloom.mamb  → copy to UEFI FAT partition
    Then in REPL: /ssm_load thalamic_bloom.mamb
"""

import argparse
import struct
import sys
import os
import numpy as np

MAMBA_MAGIC   = 0x4D414D42  # 'MAMB'
MAMBA_VERSION = 2            # v2 = thalamic-bloom MIMO export

# Thalamic-bloom config (Mamba3MIMORLF defaults)
TB_D_MODEL    = 768
TB_N_LAYERS   = 24
TB_VOCAB_SIZE = 50304
TB_D_STATE    = 16
TB_D_CONV     = 4
TB_EXPAND     = 2
TB_MIMO_ARMS  = 4


def write_header(f, d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank):
    header = struct.pack(
        '<IIiiiiiiii' + 'i'*7,
        MAMBA_MAGIC, MAMBA_VERSION,
        d_model, n_layers, vocab_size,
        d_state, d_conv, expand, dt_rank,
        0,
        0, 0, 0, 0, 0, 0, 0
    )
    assert len(header) == 64
    f.write(header)


def write_f32(f, arr):
    data = np.array(arr, dtype=np.float32).flatten()
    f.write(data.tobytes())
    return data.shape


def load_checkpoint(path):
    try:
        import torch
    except ImportError:
        print("ERROR: pip install torch")
        sys.exit(1)

    print(f"[tb-export] Loading: {path}")
    ckpt = torch.load(path, map_location='cpu', weights_only=True)

    if 'model_state_dict' in ckpt:
        state = ckpt['model_state_dict']
        step  = ckpt.get('step', '?')
        print(f"[tb-export] Checkpoint step: {step}")
    else:
        state = ckpt

    print(f"[tb-export] Total keys: {len(state)}")
    # Show key prefixes for debug
    prefixes = set(k.split('.')[0] for k in state.keys())
    print(f"[tb-export] Top-level modules: {sorted(prefixes)}")
    return state


def get_layer_weights(state, layer_prefix, d_model, d_inner, d_state, d_conv, dt_rank):
    """
    Extract MambaLayer weights from thalamic-bloom state dict.
    thalamic-bloom key pattern: {layer_prefix}.ssm.{weight_name}
                                {layer_prefix}.norm.{weight_name}
    """
    def g(name, required=True):
        key = f"{layer_prefix}.{name}"
        if key in state:
            return state[key]
        if required:
            print(f"ERROR: key not found: {key}")
            sys.exit(1)
        return None

    in_proj        = g('ssm.in_proj.weight')           # [2*d_inner, d_model]
    conv_weight    = g('ssm.conv1d.weight')             # [d_inner, 1, d_conv]
    conv_bias      = g('ssm.conv1d.bias', required=False)
    x_proj         = g('ssm.x_proj.weight')             # [dt_rank+2*d_state, d_inner]
    dt_proj_weight = g('ssm.dt_proj.weight')            # [d_inner, dt_rank]
    dt_proj_bias   = g('ssm.dt_proj.bias')              # [d_inner]
    A_log          = g('ssm.A_log')                     # [d_inner, d_state]
    D              = g('ssm.D')                         # [d_inner]
    out_proj       = g('ssm.out_proj.weight')           # [d_model, d_inner]
    norm_weight    = g('norm.weight')                   # [d_model]

    # Squeeze conv: [d_inner,1,d_conv] → [d_inner, d_conv]
    conv_w = conv_weight.numpy()
    if conv_w.ndim == 3:
        conv_w = conv_w.squeeze(1)
    conv_b = conv_bias.numpy() if conv_bias is not None else np.zeros(d_inner, dtype=np.float32)

    return {
        'in_proj':        in_proj.numpy(),
        'conv_weight':    conv_w,
        'conv_bias':      conv_b,
        'x_proj':         x_proj.numpy(),
        'dt_proj_weight': dt_proj_weight.numpy(),
        'dt_proj_bias':   dt_proj_bias.numpy(),
        'A_log':          A_log.numpy(),
        'D':              D.numpy(),
        'out_proj':       out_proj.numpy(),
        'norm_weight':    norm_weight.numpy(),
    }


def write_layer(f, w):
    write_f32(f, w['in_proj'])
    write_f32(f, w['conv_weight'])
    write_f32(f, w['conv_bias'])
    write_f32(f, w['x_proj'])
    write_f32(f, w['dt_proj_weight'])
    write_f32(f, w['dt_proj_bias'])
    write_f32(f, w['A_log'])
    write_f32(f, w['D'])
    write_f32(f, w['out_proj'])
    write_f32(f, w['norm_weight'])


def export_thalamic_bloom(state, out_path, mimo_arm=0,
                           d_model=TB_D_MODEL, n_layers=TB_N_LAYERS,
                           vocab_size=TB_VOCAB_SIZE, d_state=TB_D_STATE,
                           d_conv=TB_D_CONV, expand=TB_EXPAND):

    d_inner  = d_model * expand
    dt_rank  = max(1, d_model // 16)   # = 48 for d_model=768

    # We export: backbone (n_layers) + 1 MIMO arm appended as extra layers
    # Total exported layers = n_layers + 1  (arm is the "reflex" layer)
    total_layers = n_layers + 1

    print(f"[tb-export] Config: d_model={d_model} n_layers={n_layers}+1arm vocab={vocab_size}")
    print(f"[tb-export]         d_inner={d_inner} d_state={d_state} dt_rank={dt_rank}")
    print(f"[tb-export] MIMO arm selected: {mimo_arm} (0=general/OO)")

    with open(out_path, 'wb') as f:
        write_header(f, d_model, total_layers, vocab_size, d_state, d_conv, expand, dt_rank)

        # Embedding
        embed = state.get('embedding.weight')
        if embed is None:
            print("ERROR: embedding.weight not found")
            sys.exit(1)
        print(f"[tb-export] embed: {embed.shape}")
        write_f32(f, embed.numpy())

        # Backbone layers 0..23
        for l in range(n_layers):
            prefix = f"layers.{l}"
            w = get_layer_weights(state, prefix, d_model, d_inner, d_state, d_conv, dt_rank)
            print(f"[tb-export] layer {l:2d}: in_proj={w['in_proj'].shape} A_log={w['A_log'].shape}")
            write_layer(f, w)

        # MIMO arm (appended as layer n_layers)
        arm_prefix = f"mimo_reasoning_blocks.{mimo_arm}"
        print(f"[tb-export] MIMO arm {mimo_arm}: {arm_prefix}")
        w_arm = get_layer_weights(state, arm_prefix, d_model, d_inner, d_state, d_conv, dt_rank)
        write_layer(f, w_arm)

        # Final norm
        norm_f = state.get('norm_f.weight')
        if norm_f is None:
            print("[tb-export] WARNING: norm_f.weight not found, using ones")
            norm_f_arr = np.ones(d_model, dtype=np.float32)
        else:
            norm_f_arr = norm_f.numpy()
        write_f32(f, norm_f_arr)

        # LM head (tied to embedding)
        lm_head = state.get('lm_head.weight') or state.get('embedding.weight')
        print(f"[tb-export] lm_head: {lm_head.shape}")
        write_f32(f, lm_head.numpy())

    size_mb = os.path.getsize(out_path) / (1024 * 1024)
    print(f"\n[tb-export] ✅ Done: {out_path} ({size_mb:.1f} MB)")
    print(f"[tb-export]    Copy to UEFI FAT partition (EFI/ or root)")
    print(f"[tb-export]    Then in OO REPL: /ssm_load thalamic_bloom.mamb")
    print(f"[tb-export]    Then:            /ssm_gen What are the 5 Organic Laws?")


def main():
    p = argparse.ArgumentParser(description='Export thalamic-bloom → MAMB bare-metal')
    p.add_argument('--model', required=True,
                   help='Path to thalamic_bloom_150m_oo.pth')
    p.add_argument('--out', default='thalamic_bloom.mamb',
                   help='Output .mamb file (default: thalamic_bloom.mamb)')
    p.add_argument('--arm', type=int, default=0,
                   help='MIMO arm to export as reflex layer (0-3, default: 0=general/OO)')
    p.add_argument('--d_model',    type=int, default=TB_D_MODEL)
    p.add_argument('--n_layers',   type=int, default=TB_N_LAYERS)
    p.add_argument('--vocab_size', type=int, default=TB_VOCAB_SIZE)
    p.add_argument('--d_state',    type=int, default=TB_D_STATE)
    p.add_argument('--d_conv',     type=int, default=TB_D_CONV)
    p.add_argument('--expand',     type=int, default=TB_EXPAND)
    args = p.parse_args()

    if not os.path.exists(args.model):
        print(f"ERROR: model file not found: {args.model}")
        print("Download from: https://huggingface.co/batteryphil/thalamic-bloom")
        print("  pip install huggingface_hub")
        print("  python -c \"from huggingface_hub import hf_hub_download; "
              "hf_hub_download('batteryphil/thalamic-bloom', "
              "'thalamic_bloom_150m_oo.pth', local_dir='.')\"")
        sys.exit(1)

    state = load_checkpoint(args.model)
    export_thalamic_bloom(
        state, args.out, mimo_arm=args.arm,
        d_model=args.d_model, n_layers=args.n_layers,
        vocab_size=args.vocab_size, d_state=args.d_state,
        d_conv=args.d_conv, expand=args.expand
    )


if __name__ == '__main__':
    main()
