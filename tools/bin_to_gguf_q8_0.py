#!/usr/bin/env python
"""Convert a llama2.c-style .bin (float32 weights) into a GGUF file.

This is intended for llm-baremetal:
- Reads the 7-int header from .bin.
- Writes the minimal GGUF KVs required by gguf_infer.c.
- Writes required tensors using llama.cpp-compatible names.
- Quantizes 2D tensors to Q8_0 (per 32-float block) for smaller files.
- Writes 1D norm weights as F32.

Output GGUF is suitable for llm-baremetal's loader (gguf_infer.c).
"""

from __future__ import annotations

import argparse
import os
import struct
from dataclasses import dataclass
from typing import BinaryIO, Iterable, List, Optional, Sequence, Tuple

import numpy as np


GGUF_MAGIC = b"GGUF"
GGUF_VERSION = 3

# gguf_infer.c expects these numeric IDs
GGUF_KV_UINT32 = 4

GGML_TYPE_F32 = 0
GGML_TYPE_Q8_0 = 8

ALIGN = 32


def align_up(x: int, a: int = ALIGN) -> int:
    return (x + (a - 1)) // a * a


def write_u32(f: BinaryIO, v: int) -> None:
    f.write(struct.pack("<I", int(v)))


def write_u64(f: BinaryIO, v: int) -> None:
    f.write(struct.pack("<Q", int(v)))


def write_i32(f: BinaryIO, v: int) -> None:
    f.write(struct.pack("<i", int(v)))


def write_f32(f: BinaryIO, a: np.ndarray) -> None:
    assert a.dtype == np.float32
    f.write(a.tobytes(order="C"))


@dataclass
class BinConfig:
    dim: int
    hidden_dim: int
    n_layers: int
    n_heads: int
    n_kv_heads: int
    vocab_size: int
    seq_len: int
    shared_classifier_hint: bool

    @property
    def head_size(self) -> int:
        return self.dim // self.n_heads

    @property
    def kv_dim(self) -> int:
        return (self.dim * self.n_kv_heads) // self.n_heads


@dataclass
class TensorDesc:
    name: str
    dims: Tuple[int, ...]  # GGML convention: dims[0]=cols (ne0), dims[1]=rows (ne1)
    ggml_type: int
    offset: int = 0

    def n_dims(self) -> int:
        return len(self.dims)


def read_bin_header(f: BinaryIO) -> Tuple[BinConfig, int]:
    hdr = f.read(7 * 4)
    if len(hdr) != 7 * 4:
        raise ValueError("Invalid .bin: header too short")
    dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size_signed, seq_len = struct.unpack("<7i", hdr)

    shared_hint = vocab_size_signed < 0
    vocab_size = abs(vocab_size_signed)

    cfg = BinConfig(
        dim=dim,
        hidden_dim=hidden_dim,
        n_layers=n_layers,
        n_heads=n_heads,
        n_kv_heads=n_kv_heads,
        vocab_size=vocab_size,
        seq_len=seq_len,
        shared_classifier_hint=shared_hint,
    )
    return cfg, vocab_size_signed


def infer_shared_classifier_from_size(cfg: BinConfig, bin_path: str, vocab_size_signed: int) -> bool:
    # Mirror llama2_efi_final.c logic: file size can indicate wcls presence.
    # If header explicitly encodes shared (negative vocab), trust it.
    if vocab_size_signed < 0:
        return True

    size = os.path.getsize(bin_path)
    header_bytes = 7 * 4
    if size < header_bytes:
        return True

    available = size - header_bytes
    kv_dim = cfg.kv_dim
    head_size = cfg.head_size

    n_floats_base = 0
    n_floats_base += cfg.vocab_size * cfg.dim  # token_embedding_table
    n_floats_base += cfg.n_layers * cfg.dim  # rms_att_weight
    n_floats_base += cfg.n_layers * cfg.dim * cfg.dim  # wq
    n_floats_base += cfg.n_layers * cfg.kv_dim * cfg.dim  # wk (kv_dim x dim)
    n_floats_base += cfg.n_layers * cfg.kv_dim * cfg.dim  # wv
    n_floats_base += cfg.n_layers * cfg.dim * cfg.dim  # wo
    n_floats_base += cfg.n_layers * cfg.dim  # rms_ffn_weight
    n_floats_base += cfg.n_layers * cfg.hidden_dim * cfg.dim  # w1
    n_floats_base += cfg.n_layers * cfg.dim * cfg.hidden_dim  # w2
    n_floats_base += cfg.n_layers * cfg.hidden_dim * cfg.dim  # w3
    n_floats_base += cfg.dim  # rms_final
    n_floats_base += cfg.seq_len * head_size // 2  # freq_cis_real
    n_floats_base += cfg.seq_len * head_size // 2  # freq_cis_imag

    n_floats_with = n_floats_base + cfg.vocab_size * cfg.dim

    bytes_base = n_floats_base * 4
    bytes_with = n_floats_with * 4

    if available < bytes_with and available >= bytes_base:
        return True
    if available >= bytes_with:
        return False

    # Unknown; assume shared (safer / smaller)
    return True


def build_tensor_plan(cfg: BinConfig, shared_classifier: bool) -> List[TensorDesc]:
    d = cfg.dim
    h = cfg.hidden_dim
    L = cfg.n_layers
    kv = cfg.kv_dim
    V = cfg.vocab_size

    tensors: List[TensorDesc] = []

    # token embedding table: rows=vocab, cols=dim -> dims=(dim, vocab)
    tensors.append(TensorDesc("token_embd.weight", (d, V), GGML_TYPE_Q8_0))

    # per-layer norms (1D)
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.attn_norm.weight", (d,), GGML_TYPE_F32))

    # attention matrices
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.attn_q.weight", (d, d), GGML_TYPE_Q8_0))
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.attn_k.weight", (d, kv), GGML_TYPE_Q8_0))
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.attn_v.weight", (d, kv), GGML_TYPE_Q8_0))
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.attn_output.weight", (d, d), GGML_TYPE_Q8_0))

    # FFN norms
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.ffn_norm.weight", (d,), GGML_TYPE_F32))

    # FFN matrices: w1 (hidden x dim), w2 (dim x hidden), w3 (hidden x dim)
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.ffn_gate.weight", (d, h), GGML_TYPE_Q8_0))
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.ffn_down.weight", (h, d), GGML_TYPE_Q8_0))
    for l in range(L):
        tensors.append(TensorDesc(f"blk.{l}.ffn_up.weight", (d, h), GGML_TYPE_Q8_0))

    # final norm
    tensors.append(TensorDesc("norm.weight", (d,), GGML_TYPE_F32))

    # optional output.weight (classifier)
    if not shared_classifier:
        tensors.append(TensorDesc("output.weight", (d, V), GGML_TYPE_Q8_0))

    return tensors


def q8_0_bytes_for_matrix(rows: int, cols: int) -> int:
    if cols % 32 != 0:
        raise ValueError(f"Q8_0 requires cols%32==0, got cols={cols}")
    blocks = cols // 32
    # block_q8_0: fp16 scale (2) + 32 int8 = 34 bytes
    return rows * blocks * 34


def tensor_nbytes(t: TensorDesc) -> int:
    if t.ggml_type == GGML_TYPE_F32:
        # 1D only in our plan
        ne0 = t.dims[0]
        return int(ne0) * 4
    if t.ggml_type == GGML_TYPE_Q8_0:
        if len(t.dims) != 2:
            raise ValueError(f"Q8_0 expects 2D tensor: {t.name}")
        cols, rows = t.dims
        return q8_0_bytes_for_matrix(int(rows), int(cols))
    raise ValueError(f"Unsupported ggml_type {t.ggml_type} for {t.name}")


def write_kv_u32(f: BinaryIO, key: str, value: int) -> None:
    kb = key.encode("utf-8")
    write_u32(f, len(kb))
    f.write(kb)
    write_u32(f, GGUF_KV_UINT32)
    write_u32(f, int(value))


def quantize_write_q8_0(
    out: BinaryIO,
    mat: np.ndarray,
    chunk_rows: int = 1024,
) -> None:
    assert mat.dtype == np.float32
    rows, cols = mat.shape
    if cols % 32 != 0:
        raise ValueError(f"Q8_0 requires cols%32==0, got cols={cols}")

    blocks = cols // 32

    for r0 in range(0, rows, chunk_rows):
        r1 = min(rows, r0 + chunk_rows)
        x = mat[r0:r1]
        x = x.reshape((r1 - r0, blocks, 32))

        maxabs = np.max(np.abs(x), axis=2)
        d = maxabs / np.float32(127.0)

        inv_d = np.zeros_like(d, dtype=np.float32)
        nz = d != 0
        inv_d[nz] = np.float32(1.0) / d[nz]

        q = np.rint(x * inv_d[:, :, None]).astype(np.int32)
        q = np.clip(q, -127, 127).astype(np.int8)

        d_half_u16 = d.astype(np.float16).view(np.uint16)
        d_half_u8 = d_half_u16.view(np.uint8).reshape((r1 - r0, blocks, 2))

        out_u8 = np.empty((r1 - r0, blocks, 34), dtype=np.uint8)
        out_u8[:, :, 0:2] = d_half_u8
        out_u8[:, :, 2:34] = q.view(np.uint8)

        out.write(out_u8.tobytes(order="C"))


def read_f32_matrix(f: BinaryIO, rows: int, cols: int) -> np.ndarray:
    n = rows * cols
    a = np.fromfile(f, dtype=np.float32, count=n)
    if a.size != n:
        raise ValueError(f"Unexpected EOF while reading matrix {rows}x{cols}")
    return a.reshape((rows, cols))


def read_f32_vector(f: BinaryIO, n: int) -> np.ndarray:
    a = np.fromfile(f, dtype=np.float32, count=n)
    if a.size != n:
        raise ValueError(f"Unexpected EOF while reading vector n={n}")
    return a


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("bin", help="Input .bin (llama2.c export)")
    ap.add_argument("gguf", help="Output .gguf")
    ap.add_argument("--chunk-rows", type=int, default=1024, help="Rows per quantization chunk")
    args = ap.parse_args()

    bin_path = args.bin
    gguf_path = args.gguf

    with open(bin_path, "rb") as f:
        cfg, vocab_size_signed = read_bin_header(f)

        shared_classifier = infer_shared_classifier_from_size(cfg, bin_path, vocab_size_signed)

        tensors = build_tensor_plan(cfg, shared_classifier)

        # Precompute offsets (relative to aligned data_start)
        cur = 0
        for t in tensors:
            cur = align_up(cur, ALIGN)
            t.offset = cur
            cur += tensor_nbytes(t)

        # Build file header in one pass
        with open(gguf_path, "wb") as out:
            out.write(GGUF_MAGIC)
            write_u32(out, GGUF_VERSION)
            write_u64(out, len(tensors))

            # Minimal KV set required by llm-baremetal gguf_infer.c
            kv = [
                ("llama.embedding_length", cfg.dim),
                ("llama.feed_forward_length", cfg.hidden_dim),
                ("llama.block_count", cfg.n_layers),
                ("llama.attention.head_count", cfg.n_heads),
                ("llama.attention.head_count_kv", cfg.n_kv_heads),
                ("llama.vocab_size", cfg.vocab_size),
                ("llama.context_length", cfg.seq_len),
            ]
            write_u64(out, len(kv))
            for k, v in kv:
                write_kv_u32(out, k, v)

            # Tensor table
            for t in tensors:
                nb = t.name.encode("utf-8")
                write_u32(out, len(nb))
                out.write(nb)
                write_u32(out, t.n_dims())
                for dd in t.dims:
                    write_u64(out, int(dd))
                write_u32(out, t.ggml_type)
                write_u64(out, t.offset)

            # Pad to ALIGN so data_start aligns.
            pos = out.tell()
            pad_to = align_up(pos, ALIGN)
            if pad_to != pos:
                out.write(b"\x00" * (pad_to - pos))

            data_start = out.tell()

            # Now stream tensor data from .bin
            d = cfg.dim
            h = cfg.hidden_dim
            L = cfg.n_layers
            kv_dim = cfg.kv_dim
            V = cfg.vocab_size
            head_size = cfg.head_size

            def seek_data_offset(t: TensorDesc) -> None:
                want = data_start + t.offset
                here = out.tell()
                if here > want:
                    raise ValueError(f"Internal error: wrote past tensor offset for {t.name}")
                if here < want:
                    out.write(b"\x00" * (want - here))

            # Helper to pop next expected tensor from the plan
            it = iter(tensors)

            # token_embd
            t = next(it)
            assert t.name == "token_embd.weight"
            seek_data_offset(t)
            mat = read_f32_matrix(f, rows=V, cols=d)
            quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # rms_att per layer
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.attn_norm.weight"
                seek_data_offset(t)
                vec = read_f32_vector(f, d)
                write_f32(out, vec)

            # wq
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.attn_q.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=d, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # wk
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.attn_k.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=kv_dim, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # wv
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.attn_v.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=kv_dim, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # wo
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.attn_output.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=d, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # rms_ffn per layer
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.ffn_norm.weight"
                seek_data_offset(t)
                vec = read_f32_vector(f, d)
                write_f32(out, vec)

            # w1
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.ffn_gate.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=h, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # w2
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.ffn_down.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=d, cols=h)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # w3
            for l in range(L):
                t = next(it)
                assert t.name == f"blk.{l}.ffn_up.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=h, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # rms_final
            t = next(it)
            assert t.name == "norm.weight"
            seek_data_offset(t)
            vec = read_f32_vector(f, d)
            write_f32(out, vec)

            # skip freq_cis_real/imag in .bin
            n_freq = cfg.seq_len * head_size // 2
            _ = read_f32_vector(f, n_freq)
            _ = read_f32_vector(f, n_freq)

            # optional classifier
            if not shared_classifier:
                t = next(it)
                assert t.name == "output.weight"
                seek_data_offset(t)
                mat = read_f32_matrix(f, rows=V, cols=d)
                quantize_write_q8_0(out, mat, chunk_rows=args.chunk_rows)

            # ensure we consumed plan
            try:
                extra = next(it)
            except StopIteration:
                extra = None
            if extra is not None:
                raise ValueError(f"Internal error: did not write tensor {extra.name}")

    print(
        f"Wrote {gguf_path}\n"
        f"  dim={cfg.dim} hidden={cfg.hidden_dim} layers={cfg.n_layers} heads={cfg.n_heads} kv_heads={cfg.n_kv_heads}\n"
        f"  vocab={cfg.vocab_size} ctx={cfg.seq_len} shared_classifier={shared_classifier}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
