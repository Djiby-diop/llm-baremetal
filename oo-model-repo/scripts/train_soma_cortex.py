#!/usr/bin/env python3
"""
train_soma_cortex.py — Phase X
Train the SomaMind cortex model (small OOSS, 15M–60M params) on soma_dataset.

Architecture: Single-layer Mamba-style SSM  
  - d_model: 256 (fast) or 512 (quality)
  - n_layers: 4
  - vocab_size: 256 (byte-level) or custom BPE
  - Output: cortex_oo.bin (OOSS bare-metal binary format)

Usage:
    python train_soma_cortex.py --dataset soma_dataset/ --output models/cortex_oo.bin
    python train_soma_cortex.py --dataset soma_dataset/ --d_model 512 --n_layers 8 --epochs 20

Requires: torch, transformers (optional), numpy
"""

import argparse
import json
import math
import os
import struct
import sys
import time
from pathlib import Path

try:
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False

OOSS_MAGIC = 0x4F4F5353  # "OOSS"
OOSS_VERSION = 1

# ─── Tiny SSM model (simplified Mamba-style) ──────────────────────────────────

class SomaCortexSSM(nn.Module if HAS_TORCH else object):
    """
    Minimal Mamba-inspired SSM for bare-metal cortex.
    Input: token IDs → Output: next-token logits + domain class.
    """
    def __init__(self, vocab_size=256, d_model=256, n_layers=4, n_domains=7):
        super().__init__()
        self.vocab_size = vocab_size
        self.d_model = d_model
        self.n_layers = n_layers

        self.embed = nn.Embedding(vocab_size, d_model)
        self.layers = nn.ModuleList([
            SomaSSMLayer(d_model) for _ in range(n_layers)
        ])
        self.norm = nn.LayerNorm(d_model)
        self.lm_head = nn.Linear(d_model, vocab_size, bias=False)
        self.domain_head = nn.Linear(d_model, n_domains, bias=False)

        # Tie embed/lm_head weights
        self.lm_head.weight = self.embed.weight

    def forward(self, x):
        # x: [B, T] token ids
        h = self.embed(x)
        for layer in self.layers:
            h = layer(h)
        h = self.norm(h)
        logits = self.lm_head(h)          # [B, T, vocab]
        domain = self.domain_head(h[:, -1])  # [B, n_domains] — last position
        return logits, domain


class SomaSSMLayer(nn.Module if HAS_TORCH else object):
    def __init__(self, d_model):
        super().__init__()
        self.d_model = d_model
        d_inner = d_model * 2
        d_state = 16

        self.in_proj = nn.Linear(d_model, d_inner * 2, bias=False)
        self.conv1d = nn.Conv1d(d_inner, d_inner, kernel_size=4, padding=3, groups=d_inner)
        self.x_proj = nn.Linear(d_inner, d_state * 2 + d_inner, bias=False)
        self.dt_proj = nn.Linear(d_inner, d_inner)
        self.out_proj = nn.Linear(d_inner, d_model, bias=False)
        self.norm = nn.LayerNorm(d_model)

        # A matrix (log-space diagonal)
        A = torch.arange(1, d_state + 1, dtype=torch.float32).unsqueeze(0).expand(d_inner, -1)
        self.A_log = nn.Parameter(torch.log(A))
        self.D = nn.Parameter(torch.ones(d_inner))

    def forward(self, x):
        residual = x
        B, T, C = x.shape

        xz = self.in_proj(x)
        x_in, z = xz.chunk(2, dim=-1)

        # Conv1d along sequence
        x_in = self.conv1d(x_in.transpose(1, 2))[:, :, :T].transpose(1, 2)
        x_in = F.silu(x_in)

        # SSM parameters
        x_dbl = self.x_proj(x_in)
        dt_rank = self.d_model * 2
        d_state = 16
        dt, B_ssm, C_ssm = x_dbl.split([dt_rank, d_state, d_state], dim=-1)
        dt = F.softplus(self.dt_proj(dt))

        # Simplified SSM scan (non-recurrent for training efficiency)
        A = -torch.exp(self.A_log)
        y = x_in * self.D.unsqueeze(0).unsqueeze(0)  # skip state for simplicity

        y = y * F.sigmoid(z)
        out = self.out_proj(y)
        return self.norm(out + residual)


# ─── Dataset ──────────────────────────────────────────────────────────────────

def load_dataset(dataset_dir: str):
    train_path = os.path.join(dataset_dir, "train.jsonl")
    if not os.path.exists(train_path):
        print(f"[ERROR] {train_path} not found. Run export_soma_dataset.py first.", file=sys.stderr)
        sys.exit(1)

    records = []
    with open(train_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def encode_byte_level(text: str, max_len: int = 128) -> list[int]:
    bs = text.encode("utf-8", errors="replace")[:max_len]
    return list(bs)


def make_batch(records: list, batch_size: int, device):
    import random
    batch = random.sample(records, min(batch_size, len(records)))
    max_len = 128
    input_ids = []
    domain_labels = []
    for rec in batch:
        text = rec.get("text", rec.get("prompt", "") + rec.get("response", ""))
        ids = encode_byte_level(text, max_len)
        if len(ids) < 2:
            ids = [0, 0]
        # Pad to max_len
        ids = ids[:max_len]
        while len(ids) < max_len:
            ids.append(0)
        input_ids.append(ids)
        domain_labels.append(rec.get("domain", 0))

    x = torch.tensor(input_ids, dtype=torch.long, device=device)
    d = torch.tensor(domain_labels, dtype=torch.long, device=device)
    return x, d


# ─── OOSS Binary Export ────────────────────────────────────────────────────────

def export_ooss_binary(model, output_path: str, vocab_size: int, d_model: int, n_layers: int):
    """
    Write OOSS bare-metal binary.
    Format: [header 64 bytes][weights: embed, layers, norm, heads]
    """
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    state = model.state_dict()

    # Flatten all weights to F32 using pure torch (no numpy dependency)
    weight_tensors = []
    weight_names = []
    total_floats = 0
    for name, param in state.items():
        flat = param.detach().cpu().float().reshape(-1)
        weight_tensors.append(flat)
        weight_names.append(name)
        total_floats += flat.numel()

    total_bytes = 64 + total_floats * 4  # 64-byte header + F32 weights

    with open(output_path, "wb") as f:
        # Header (64 bytes)
        f.write(struct.pack("<I", OOSS_MAGIC))       # magic
        f.write(struct.pack("<I", OOSS_VERSION))     # version
        f.write(struct.pack("<I", vocab_size))       # vocab_size
        f.write(struct.pack("<I", d_model))          # d_model
        f.write(struct.pack("<I", n_layers))         # n_layers
        f.write(struct.pack("<I", 7))                # n_domains
        f.write(struct.pack("<I", total_floats))     # total_floats
        f.write(struct.pack("<I", len(weight_tensors)))  # n_tensors
        f.write(b'\x00' * (64 - 32))                # padding to 64 bytes

        # Weights (F32 little-endian — stdlib array, no numpy needed)
        import array as _array, sys as _sys
        for flat in weight_tensors:
            buf = _array.array('f', flat.to(torch.float32).tolist())
            if _sys.byteorder == 'big':
                buf.byteswap()
            f.write(buf.tobytes())

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(f"[OK] OOSS binary → {output_path}  ({size_mb:.1f} MB)")
    print(f"     vocab={vocab_size} d_model={d_model} n_layers={n_layers}")
    print(f"     total_floats={total_floats:,}  total_bytes={total_bytes:,}")
    print(f"     tensors: {len(weight_tensors)}")


# ─── Training Loop ─────────────────────────────────────────────────────────────

def train(args):
    if not HAS_TORCH:
        print("[ERROR] torch not installed. Run: pip install torch", file=sys.stderr)
        sys.exit(1)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"[INFO] Device: {device}")

    records = load_dataset(args.dataset)
    print(f"[INFO] Dataset: {len(records)} records")

    model = SomaCortexSSM(
        vocab_size=256,
        d_model=args.d_model,
        n_layers=args.n_layers,
        n_domains=7,
    ).to(device)

    n_params = sum(p.numel() for p in model.parameters())
    print(f"[INFO] Model params: {n_params:,} ({n_params/1e6:.1f}M)")

    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    best_loss = float("inf")
    for epoch in range(1, args.epochs + 1):
        model.train()
        epoch_loss = 0.0
        n_steps = max(1, len(records) // args.batch_size)

        for step in range(n_steps):
            x, domain_labels = make_batch(records, args.batch_size, device)
            logits, domain_logits = model(x)

            # LM loss (next-token prediction)
            lm_loss = F.cross_entropy(
                logits[:, :-1].reshape(-1, 256),
                x[:, 1:].reshape(-1),
                ignore_index=0,
            )
            # Domain classification loss
            domain_loss = F.cross_entropy(domain_logits, domain_labels)

            loss = lm_loss + 0.1 * domain_loss
            optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            epoch_loss += loss.item()

        scheduler.step()
        avg_loss = epoch_loss / n_steps
        lr_now = scheduler.get_last_lr()[0]
        print(f"  epoch {epoch:3d}/{args.epochs}  loss={avg_loss:.4f}  lr={lr_now:.2e}")

        if avg_loss < best_loss:
            best_loss = avg_loss
            torch.save(model.state_dict(), args.output.replace(".bin", "_best.pt"))

    print(f"\n[OK] Training complete. Best loss: {best_loss:.4f}")

    # Load best weights for export
    best_pt = args.output.replace(".bin", "_best.pt")
    if os.path.exists(best_pt):
        model.load_state_dict(torch.load(best_pt, map_location=device))

    export_ooss_binary(model, args.output, vocab_size=256,
                       d_model=args.d_model, n_layers=args.n_layers)
    print(f"\n[NEXT] Load on bare-metal: /cortex_load {os.path.basename(args.output)}")


def main():
    parser = argparse.ArgumentParser(description="Train SomaMind cortex model (OOSS format)")
    parser.add_argument("--dataset", default="soma_dataset", help="Dataset directory from export_soma_dataset.py")
    parser.add_argument("--output", default="models/cortex_oo.bin", help="Output OOSS binary")
    parser.add_argument("--d_model", type=int, default=256, help="Model dimension (256 fast, 512 quality)")
    parser.add_argument("--n_layers", type=int, default=4, help="Number of SSM layers")
    parser.add_argument("--epochs", type=int, default=30, help="Training epochs")
    parser.add_argument("--batch_size", type=int, default=8, help="Batch size")
    parser.add_argument("--lr", type=float, default=3e-4, help="Learning rate")
    args = parser.parse_args()
    train(args)


if __name__ == "__main__":
    main()
