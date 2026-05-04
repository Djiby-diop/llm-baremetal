"""
Test OONV export integrity.
Vérifie : magic/header, dimensions, forward pass (Python model).

Usage:
  python test_oonv_export.py checkpoints/oo-native-v1/oo_native_v1.oonv
                              checkpoints/oo-native-v1/oo_native_v1.pt
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import torch

MAGIC   = 0x4F4F4E56  # "OONV"
VERSION = 1

HDR_FMT  = "<IIIIIIIII"  # 9 × u32
HDR_SIZE = struct.calcsize(HDR_FMT)


def check_header(oonv_path: str) -> dict:
    with open(oonv_path, "rb") as f:
        raw = f.read(HDR_SIZE)
    (magic, version, vocab_size, d_model, n_layer,
     d_state, d_conv, expand, context_len) = struct.unpack(HDR_FMT, raw)

    assert magic   == MAGIC,   f"Bad magic: 0x{magic:08X} (expected 0x{MAGIC:08X})"
    assert version == VERSION, f"Bad version: {version}"

    cfg = dict(vocab_size=vocab_size, d_model=d_model, n_layer=n_layer,
               d_state=d_state, d_conv=d_conv, expand=expand,
               context_length=context_len)
    print(f"[test] Header OK — vocab={vocab_size} d_model={d_model} "
          f"n_layer={n_layer} d_state={d_state}")
    return cfg


def check_size(oonv_path: str) -> None:
    size = Path(oonv_path).stat().st_size
    print(f"[test] File size: {size / (1024*1024):.2f} MB")
    assert size > 0, "Empty file"


def check_forward(pt_path: str) -> None:
    sys.path.insert(0, str(Path(__file__).parent.parent.parent / "oo-model" / "src"))
    from oo_model.oo_native import OONativeModel, OONativeConfig

    ckpt = torch.load(pt_path, map_location="cpu", weights_only=True)
    cfg  = OONativeConfig(**ckpt["config"])
    model = OONativeModel(cfg)
    model.load_state_dict(ckpt["model_state"])
    model.eval()

    # Single token forward pass
    token = torch.zeros(1, 1, dtype=torch.long)
    with torch.no_grad():
        out = model(input_ids=token)

    logits = out.get("logits")
    assert logits is not None and logits.shape[-1] == cfg.vocab_size, \
        f"Unexpected logits shape: {logits.shape if logits is not None else None}"
    next_tok = logits[0, -1].argmax().item()
    print(f"[test] Forward pass OK — logits shape={tuple(logits.shape)}, "
          f"argmax token={next_tok}")


def main() -> None:
    if len(sys.argv) < 3:
        print("Usage: test_oonv_export.py <model.oonv> <model.pt>")
        sys.exit(1)

    oonv_path = sys.argv[1]
    pt_path   = sys.argv[2]

    print(f"[test] ── OONV validation ──────────────────")
    check_header(oonv_path)
    check_size(oonv_path)
    check_forward(pt_path)
    print(f"[test] ALL CHECKS PASSED ✓")


if __name__ == "__main__":
    main()
