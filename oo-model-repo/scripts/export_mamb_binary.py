"""
Export a Hugging Face Mamba checkpoint to bare-metal MAMB format.

This exporter targets the current llm-baremetal runtime loader:
  - engine/ssm/mamba_weights.h
  - engine/ssm/mamba_weights.c

Output layout (little-endian, row-major F32):
  [MambaFileHeader: 64 bytes]
  [embed]
  [per-layer weights]
  [final_norm]
  [lm_head]

Per-layer layout:
  in_proj.weight
  conv1d.weight        (squeezed from [d_inner, 1, d_conv] to [d_inner, d_conv])
  conv1d.bias
  x_proj.weight
  dt_proj.weight
  dt_proj.bias
  A_log
  D
  out_proj.weight
  norm.weight
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path
import re

import torch

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

MAMBA_MAGIC = 0x4D414D42  # 'MAMB'
MAMBA_VERSION = 1


def write_header(f, d_model: int, n_layers: int, vocab_size: int,
                 d_state: int, d_conv: int, expand: int, dt_rank: int) -> None:
    header = struct.pack(
        "<II7i7i",
        MAMBA_MAGIC,
        MAMBA_VERSION,
        d_model,
        n_layers,
        vocab_size,
        d_state,
        d_conv,
        expand,
        dt_rank,
        0, 0, 0, 0, 0, 0, 0,
    )
    if len(header) != 64:
        raise ValueError(f"unexpected header size: {len(header)}")
    f.write(header)


def write_tensor(f, tensor: torch.Tensor) -> int:
    data = tensor.detach().float().cpu().contiguous().numpy().tobytes()
    f.write(data)
    return len(data)


def resolve_key(state_dict: dict[str, torch.Tensor], candidates: list[str], *, required: bool = True) -> torch.Tensor | None:
    for key in candidates:
        if key in state_dict:
            return state_dict[key]
    if required:
        raise KeyError(f"missing weight, tried: {candidates}")
    return None


def infer_config(model) -> tuple[int, int, int, int, int, int, int]:
    cfg = model.config
    d_model = int(getattr(cfg, "d_model"))
    n_layers = int(getattr(cfg, "n_layer", getattr(cfg, "num_hidden_layers")))
    vocab_size = int(getattr(cfg, "vocab_size"))
    d_state = int(getattr(cfg, "d_state", 16))
    d_conv = int(getattr(cfg, "d_conv", 4))
    expand = int(getattr(cfg, "expand", 2))
    dt_rank = getattr(cfg, "dt_rank", "auto")
    if dt_rank == "auto":
        dt_rank = max(1, (d_model + 15) // 16)
    dt_rank = int(dt_rank)
    return d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank


def infer_config_from_state_dict(state_dict: dict[str, torch.Tensor]) -> tuple[int, int, int, int, int, int, int]:
    embed = resolve_key(state_dict, [
        "backbone.embedding.weight",
        "backbone.embeddings.weight",
        "backbone.embed_tokens.weight",
        "model.embed_tokens.weight",
        "embedding.weight",
    ])
    vocab_size, d_model = [int(x) for x in embed.shape]

    n_layers = 0
    layer_patterns = [
        re.compile(r"^backbone\.layers\.(\d+)\."),
        re.compile(r"^model\.layers\.(\d+)\."),
        re.compile(r"^layers\.(\d+)\."),
    ]
    for key in state_dict:
        for pattern in layer_patterns:
            match = pattern.match(key)
            if match:
                n_layers = max(n_layers, int(match.group(1)) + 1)
                break
    if n_layers <= 0:
        raise ValueError("could not infer layer count from state dict")

    a_log = None
    x_proj = None
    conv_weight = None
    for key, value in state_dict.items():
        if a_log is None and key.endswith("layers.0.mixer.A_log"):
            a_log = value
        if x_proj is None and key.endswith("layers.0.mixer.x_proj.weight"):
            x_proj = value
        if conv_weight is None and key.endswith("layers.0.mixer.conv1d.weight"):
            conv_weight = value
    if a_log is None or x_proj is None:
        prefixes = [
            "backbone.layers.0.mixer.",
            "model.layers.0.mixer.",
            "layers.0.mixer.",
            "backbone.layers.0.",
        ]
        for prefix in prefixes:
            if a_log is None and prefix + "A_log" in state_dict:
                a_log = state_dict[prefix + "A_log"]
            if x_proj is None and prefix + "x_proj.weight" in state_dict:
                x_proj = state_dict[prefix + "x_proj.weight"]
            if conv_weight is None and prefix + "conv1d.weight" in state_dict:
                conv_weight = state_dict[prefix + "conv1d.weight"]

    if a_log is None or x_proj is None:
        raise ValueError("could not infer SSM dimensions from state dict")

    d_inner, d_state = [int(x) for x in a_log.shape]
    expand = max(1, d_inner // d_model)
    dt_rank = int(x_proj.shape[0]) - (2 * d_state)
    if dt_rank <= 0:
        dt_rank = max(1, (d_model + 15) // 16)

    if conv_weight is not None:
        d_conv = int(conv_weight.shape[-1] if conv_weight.ndim == 3 else conv_weight.shape[1])
    else:
        d_conv = 4

    return d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank


def expected_size_bytes(d_model: int, n_layers: int, vocab_size: int,
                        d_state: int, d_conv: int, expand: int, dt_rank: int) -> int:
    d_inner = d_model * expand
    total = 64
    total += vocab_size * d_model * 4
    for _ in range(n_layers):
        total += (2 * d_inner * d_model) * 4
        total += (d_inner * d_conv) * 4
        total += d_inner * 4
        total += ((dt_rank + 2 * d_state) * d_inner) * 4
        total += (d_inner * dt_rank) * 4
        total += d_inner * 4
        total += (d_inner * d_state) * 4
        total += d_inner * 4
        total += (d_model * d_inner) * 4
        total += d_model * 4
    total += d_model * 4
    total += (vocab_size * d_model) * 4
    return total


def load_state_dict_from_file(model_file: Path) -> dict[str, torch.Tensor]:
    print(f"[export-mamb] Loading state dict from {model_file}...")
    state = torch.load(str(model_file), map_location="cpu", weights_only=True)
    if isinstance(state, dict) and "state_dict" in state and isinstance(state["state_dict"], dict):
        state = state["state_dict"]
    if not isinstance(state, dict):
        raise TypeError(f"unsupported checkpoint structure in {model_file}")
    tensor_state = {k: v for k, v in state.items() if isinstance(v, torch.Tensor)}
    if not tensor_state:
        raise ValueError(f"no tensors found in {model_file}")
    return tensor_state


def load_source(model_source: str) -> tuple[dict[str, torch.Tensor], tuple[int, int, int, int, int, int, int]]:
    source = Path(model_source)
    if source.is_file():
        state_dict = load_state_dict_from_file(source)
        return state_dict, infer_config_from_state_dict(state_dict)

    from transformers import AutoModelForCausalLM

    print(f"[export-mamb] Loading model from {model_source}...")
    model = AutoModelForCausalLM.from_pretrained(
        model_source,
        torch_dtype=torch.float32,
        trust_remote_code=True,
    )
    state_dict = model.state_dict()
    return state_dict, infer_config(model)


def export(model_source: str, out_path: str) -> None:
    out_path = Path(out_path)

    state_dict, inferred = load_source(model_source)
    d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank = inferred
    d_inner = d_model * expand

    print(
        f"[export-mamb] d_model={d_model} n_layers={n_layers} vocab={vocab_size} "
        f"d_state={d_state} d_conv={d_conv} expand={expand} dt_rank={dt_rank}"
    )

    embed = resolve_key(state_dict, [
        "backbone.embedding.weight",
        "backbone.embeddings.weight",
        "backbone.embed_tokens.weight",
        "model.embed_tokens.weight",
        "embedding.weight",
    ])

    final_norm = resolve_key(state_dict, [
        "backbone.norm_f.weight",
        "model.norm_f.weight",
        "norm_f.weight",
        "backbone.final_layernorm.weight",
    ], required=False)
    if final_norm is None:
        final_norm = torch.ones(d_model, dtype=torch.float32)

    lm_head = resolve_key(state_dict, [
        "lm_head.weight",
        "backbone.embedding.weight",
        "backbone.embeddings.weight",
        "backbone.embed_tokens.weight",
        "model.embed_tokens.weight",
    ])

    total_bytes = 0
    with out_path.open("wb") as f:
        write_header(f, d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank)
        total_bytes += 64

        total_bytes += write_tensor(f, embed)

        for layer_idx in range(n_layers):
            prefixes = [
                f"backbone.layers.{layer_idx}.mixer.",
                f"model.layers.{layer_idx}.mixer.",
                f"layers.{layer_idx}.mixer.",
                f"backbone.layers.{layer_idx}.",
            ]
            norm_prefixes = [
                f"backbone.layers.{layer_idx}.",
                f"model.layers.{layer_idx}.",
                f"layers.{layer_idx}.",
            ]

            def get(names: list[str], *, required: bool = True) -> torch.Tensor | None:
                keys: list[str] = []
                for prefix in prefixes:
                    for name in names:
                        keys.append(prefix + name)
                return resolve_key(state_dict, keys, required=required)

            in_proj = get(["in_proj.weight"])
            conv_weight = get(["conv1d.weight"])
            conv_bias = get(["conv1d.bias"], required=False)
            x_proj = get(["x_proj.weight"])
            dt_proj_weight = get(["dt_proj.weight"])
            dt_proj_bias = get(["dt_proj.bias"])
            a_log = get(["A_log"])
            d_skip = get(["D"])
            out_proj = get(["out_proj.weight"])

            norm_weight = None
            for prefix in norm_prefixes:
                key = prefix + "norm.weight"
                if key in state_dict:
                    norm_weight = state_dict[key]
                    break
            if norm_weight is None:
                norm_weight = torch.ones(d_model, dtype=torch.float32)

            if conv_weight.ndim == 3:
                conv_weight = conv_weight.squeeze(1)
            if conv_bias is None:
                conv_bias = torch.zeros(d_inner, dtype=torch.float32)

            total_bytes += write_tensor(f, in_proj)
            total_bytes += write_tensor(f, conv_weight)
            total_bytes += write_tensor(f, conv_bias)
            total_bytes += write_tensor(f, x_proj)
            total_bytes += write_tensor(f, dt_proj_weight)
            total_bytes += write_tensor(f, dt_proj_bias)
            total_bytes += write_tensor(f, a_log)
            total_bytes += write_tensor(f, d_skip)
            total_bytes += write_tensor(f, out_proj)
            total_bytes += write_tensor(f, norm_weight)

        total_bytes += write_tensor(f, final_norm)
        total_bytes += write_tensor(f, lm_head)

    expected_bytes = expected_size_bytes(d_model, n_layers, vocab_size, d_state, d_conv, expand, dt_rank)
    if total_bytes != expected_bytes:
        raise ValueError(
            f"size mismatch: wrote {total_bytes} bytes, expected {expected_bytes} bytes for runtime layout"
        )

    print(f"[export-mamb] Wrote {out_path} ({total_bytes} bytes, {total_bytes / (1024 * 1024):.1f} MB)")
    print("[export-mamb] Bare-metal load path: /ssm_load <file.mamb>")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: export_mamb_binary.py <model_dir|model_id|checkpoint.pt> <output.mamb>")
        sys.exit(1)
    export(sys.argv[1], sys.argv[2])
