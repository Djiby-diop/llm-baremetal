#!/usr/bin/env python3
"""
export_mamba_baremetal.py
Export a Mamba2 PyTorch checkpoint to flat binary format for bare-metal inference.

Usage:
    python export_mamba_baremetal.py --model path/to/model --out mamba.mamb [--d_state 16] [--d_conv 4]

Output binary format (MAMB):
    [MambaFileHeader: 64 bytes]
    [embed: vocab_size * d_model * 4 bytes, f32]
    [layer_0 weights...]
    ...
    [layer_N weights...]
    [final_norm: d_model * 4 bytes, f32]
    [lm_head: vocab_size * d_model * 4 bytes, f32]

Per-layer layout (in order):
    in_proj:        [2*d_inner, d_model]
    conv_weight:    [d_inner, d_conv]
    conv_bias:      [d_inner]
    x_proj:         [dt_rank + 2*d_state, d_inner]
    dt_proj_weight: [d_inner, dt_rank]
    dt_proj_bias:   [d_inner]
    A_log:          [d_inner, d_state]
    D:              [d_inner]
    out_proj:       [d_model, d_inner]
    norm_weight:    [d_model]

All tensors are F32, row-major (C order).
"""

import argparse
import struct
import sys
import os
import numpy as np

MAMBA_MAGIC   = 0x4D414D42  # 'MAMB'
MAMBA_VERSION = 1

def write_header(f, d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank):
    header = struct.pack(
        '<IIiiiiiiii' + 'i'*7,
        MAMBA_MAGIC, MAMBA_VERSION,
        d_model, n_layers, vocab_size,
        d_state, d_conv, expand, dt_rank,
        0,  # reserved
        0, 0, 0, 0, 0, 0, 0  # pad[7]
    )
    assert len(header) == 64, f"Header size mismatch: {len(header)}"
    f.write(header)

def write_f32(f, arr):
    """Write a numpy array as f32 row-major."""
    data = np.array(arr, dtype=np.float32).flatten()
    f.write(data.tobytes())
    return data.shape

def load_pytorch_model(model_path):
    """Load a Mamba model from PyTorch checkpoint."""
    try:
        import torch
    except ImportError:
        print("ERROR: PyTorch not installed. Install with: pip install torch")
        sys.exit(1)

    print(f"[export] Loading: {model_path}")

    # Try HuggingFace transformers first (MambaForCausalLM)
    state = None
    try:
        state = torch.load(model_path, map_location='cpu', weights_only=True)
    except Exception as e:
        print(f"[export] torch.load failed: {e}")
        sys.exit(1)

    if isinstance(state, dict) and 'state_dict' in state:
        state = state['state_dict']

    # Print available keys for debugging
    print(f"[export] Keys ({len(state)}): {list(state.keys())[:10]}...")

    return state

def export_from_state_dict(f, state, d_model, n_layers, vocab_size,
                            d_state, d_conv, expand, dt_rank):
    """Export weights in MAMB order from a state dict."""
    d_inner = d_model * expand

    # Write header
    write_header(f, d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank)

    # Embedding
    embed = state.get('backbone.embedding.weight') or \
            state.get('model.embed_tokens.weight') or \
            state.get('embedding.weight')
    if embed is None:
        print("ERROR: embedding weight not found in state dict")
        sys.exit(1)
    print(f"[export] embed: {embed.shape}")
    write_f32(f, embed.numpy())

    # Per-layer weights
    for l in range(n_layers):
        # Try common naming patterns for Mamba
        pfx = [
            f'backbone.layers.{l}.mixer.',
            f'model.layers.{l}.mixer.',
            f'layers.{l}.mixer.',
            f'backbone.layers.{l}.',
        ]

        def get(names, required=True):
            for p in pfx:
                for name in names:
                    key = p + name
                    if key in state:
                        return state[key]
            if required:
                print(f"ERROR: layer {l} weight not found. Tried: {[p+n for p in pfx for n in names]}")
                sys.exit(1)
            return None

        in_proj        = get(['in_proj.weight'])
        conv_weight    = get(['conv1d.weight'])
        conv_bias      = get(['conv1d.bias'], required=False)
        x_proj         = get(['x_proj.weight'])
        dt_proj_weight = get(['dt_proj.weight'])
        dt_proj_bias   = get(['dt_proj.bias'])
        A_log          = get(['A_log'])
        D              = get(['D'])
        out_proj       = get(['out_proj.weight'])

        # Norm: try layer norm before mixer
        norm_pfx = [f'backbone.layers.{l}.', f'model.layers.{l}.', f'layers.{l}.']
        norm_w = None
        for p in norm_pfx:
            if p + 'norm.weight' in state:
                norm_w = state[p + 'norm.weight']
                break
        if norm_w is None:
            print(f"[export] WARNING: layer {l} norm not found, using ones")
            norm_w = np.ones(d_model, dtype=np.float32)

        # Reshape conv_weight: [d_inner, 1, d_conv] → [d_inner, d_conv]
        conv_w = conv_weight.numpy()
        if conv_w.ndim == 3:
            conv_w = conv_w.squeeze(1)
        conv_b = conv_bias.numpy() if conv_bias is not None else np.zeros(d_inner, dtype=np.float32)

        print(f"[export] layer {l}: in_proj={in_proj.shape}, A_log={A_log.shape}")

        write_f32(f, in_proj.numpy())    # [2*d_inner, d_model]
        write_f32(f, conv_w)             # [d_inner, d_conv]
        write_f32(f, conv_b)             # [d_inner]
        write_f32(f, x_proj.numpy())     # [dt_rank+2*d_state, d_inner]
        write_f32(f, dt_proj_weight.numpy())  # [d_inner, dt_rank]
        write_f32(f, dt_proj_bias.numpy())    # [d_inner]
        write_f32(f, A_log.numpy())      # [d_inner, d_state]
        write_f32(f, D.numpy())          # [d_inner]
        write_f32(f, out_proj.numpy())   # [d_model, d_inner]
        write_f32(f, np.array(norm_w, dtype=np.float32))  # [d_model]

    # Final norm
    final_norm = state.get('backbone.norm_f.weight') or \
                 state.get('model.norm_f.weight') or \
                 state.get('norm_f.weight')
    if final_norm is None:
        print("[export] WARNING: final norm not found, using ones")
        final_norm = np.ones(d_model, dtype=np.float32)
    write_f32(f, np.array(final_norm, dtype=np.float32))

    # LM head (often tied to embedding)
    lm_head = state.get('lm_head.weight') or \
              state.get('backbone.embedding.weight') or \
              state.get('model.embed_tokens.weight')
    if lm_head is None:
        print("ERROR: lm_head not found")
        sys.exit(1)
    print(f"[export] lm_head: {lm_head.shape}")
    write_f32(f, lm_head.numpy())

def infer_config_from_state(state):
    """Try to infer model config from state dict."""
    # Look for A_log in layer 0 to get d_inner, d_state
    for key, val in state.items():
        if 'A_log' in key and 'layers.0' in key:
            d_inner, d_state = val.shape
            break
    else:
        print("ERROR: cannot infer d_inner/d_state from state dict")
        sys.exit(1)

    # d_model from embedding
    for key, val in state.items():
        if 'embed' in key.lower() and 'weight' in key:
            vocab_size, d_model = val.shape
            break
    else:
        print("ERROR: cannot infer d_model/vocab_size from embedding")
        sys.exit(1)

    # n_layers
    n_layers = 0
    while any(f'layers.{n_layers}' in k for k in state.keys()):
        n_layers += 1

    expand = d_inner // d_model

    # dt_rank from x_proj
    for key, val in state.items():
        if 'x_proj' in key and 'layers.0' in key:
            dt_rank = val.shape[0] - 2 * d_state
            break
    else:
        dt_rank = max(1, d_model // 16)

    # d_conv from conv1d
    for key, val in state.items():
        if 'conv1d.weight' in key and 'layers.0' in key:
            d_conv = val.shape[-1] if val.ndim == 3 else val.shape[1]
            break
    else:
        d_conv = 4

    return d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank

def main():
    parser = argparse.ArgumentParser(description='Export Mamba model to MAMB bare-metal format')
    parser.add_argument('--model', required=True, help='Path to PyTorch .pt/.bin checkpoint')
    parser.add_argument('--out',   required=True, help='Output .mamb file path')
    parser.add_argument('--d_model',    type=int, default=0)
    parser.add_argument('--n_layers',   type=int, default=0)
    parser.add_argument('--vocab_size', type=int, default=0)
    parser.add_argument('--d_state',    type=int, default=16)
    parser.add_argument('--d_conv',     type=int, default=4)
    parser.add_argument('--expand',     type=int, default=2)
    parser.add_argument('--dt_rank',    type=int, default=0)
    args = parser.parse_args()

    state = load_pytorch_model(args.model)

    # Infer config
    d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank = infer_config_from_state(state)
    if args.d_model    > 0: d_model    = args.d_model
    if args.n_layers   > 0: n_layers   = args.n_layers
    if args.vocab_size > 0: vocab_size = args.vocab_size
    if args.d_state    > 0: d_state    = args.d_state
    if args.d_conv     > 0: d_conv     = args.d_conv
    if args.expand     > 0: expand     = args.expand
    if args.dt_rank    > 0: dt_rank    = args.dt_rank
    if dt_rank == 0:        dt_rank    = max(1, d_model // 16)

    d_inner = d_model * expand
    print(f"[export] Config: d_model={d_model} n_layers={n_layers} vocab={vocab_size}")
    print(f"[export]         d_inner={d_inner} d_state={d_state} d_conv={d_conv} dt_rank={dt_rank}")

    out_path = args.out
    print(f"[export] Writing: {out_path}")
    with open(out_path, 'wb') as f:
        export_from_state_dict(f, state, d_model, n_layers, vocab_size,
                               d_state, d_conv, expand, dt_rank)

    size_mb = os.path.getsize(out_path) / (1024*1024)
    print(f"[export] Done. {out_path} ({size_mb:.1f} MB)")
    print(f"[export] Copy to UEFI disk, then: /ssm_load {os.path.basename(out_path)}")

if __name__ == '__main__':
    main()
