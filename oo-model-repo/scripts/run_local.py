#!/usr/bin/env python3
"""
run_local.py — Mamba-2.8B Latent Reasoning Engine (LOCAL version)
==================================================================
Loads the model from a local folder instead of downloading from HuggingFace.

Usage:
    python run_local.py                           # interactive chat
    python run_local.py --prompt "X=5. Y=X*2."   # single prompt
    python run_local.py --domain code             # code mode (more loops)
    python run_local.py --export model.bin        # export to bare-metal .bin

Local model folder expected:
    model.safetensors    — main weights (~5.5 GB)
    halting_head.pt      — HaltingHead checkpoint (~5 MB)
    tokenizer.json       — tokenizer
    config.json          — model config
"""

import sys
import struct
import argparse
from pathlib import Path

# ── Locate model dir (same dir as this script) ──────────────────────────────
SCRIPT_DIR = Path(__file__).resolve().parent

def find_model_dir(override=None):
    if override:
        p = Path(override)
        if p.exists():
            return p
        raise FileNotFoundError(f"Model dir not found: {p}")
    # Default: same directory as this script
    if (SCRIPT_DIR / "model.safetensors").exists():
        return SCRIPT_DIR
    raise FileNotFoundError(
        f"model.safetensors not found in {SCRIPT_DIR}\n"
        "  Use --model-dir <path> to specify the model folder."
    )

# ── HaltingHead (inline, no oo_model import needed) ─────────────────────────
def build_halting_head(d_input=2561):
    import torch.nn as nn
    class HaltingHead(nn.Module):
        def __init__(self, d_input=2561):
            super().__init__()
            self.net = nn.Sequential(
                nn.Linear(d_input, 512), nn.GELU(), nn.Dropout(0.1),
                nn.Linear(512, 64),  nn.GELU(), nn.Linear(64, 1), nn.Sigmoid()
            )
        def forward(self, x): return self.net(x).squeeze(-1)
    return HaltingHead(d_input)

# ── VRAM detection ────────────────────────────────────────────────────────────
def get_load_config():
    import torch
    if not torch.cuda.is_available():
        print("    No CUDA GPU — CPU mode (float32, ~30-90s per response)")
        return {"dtype": torch.float32}, "cpu"
    vram_gb = torch.cuda.get_device_properties(0).total_memory / 1e9
    name    = torch.cuda.get_device_properties(0).name
    print(f"    GPU: {name}  ({vram_gb:.1f} GB VRAM)")
    if vram_gb >= 11.5:
        print("    Mode: BF16 GPU ✅")
        return {"dtype": torch.bfloat16, "device_map": "cuda:0"}, "cuda"
    elif vram_gb >= 7.5:
        print("    Mode: 4-bit quantized")
        from transformers import BitsAndBytesConfig
        import torch
        bnb = BitsAndBytesConfig(
            load_in_4bit=True,
            bnb_4bit_compute_dtype=torch.bfloat16,
            bnb_4bit_use_double_quant=True,
        )
        return {"quantization_config": bnb, "device_map": "auto"}, "4bit"
    else:
        print(f"    Only {vram_gb:.1f} GB VRAM → CPU mode")
        import torch
        return {"dtype": torch.float32}, "cpu"

# ── Load engine ───────────────────────────────────────────────────────────────
def load_engine(model_dir: Path, load_cfg: dict, mode: str):
    import torch
    from transformers import AutoTokenizer, AutoModelForCausalLM

    print(f"    Loading tokenizer from {model_dir}...")
    tok = AutoTokenizer.from_pretrained(str(model_dir), trust_remote_code=True)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token

    print(f"    Loading model weights (this takes a few minutes in CPU mode)...")
    model = AutoModelForCausalLM.from_pretrained(
        str(model_dir), trust_remote_code=True, **load_cfg
    )
    model.eval()

    halt_path = model_dir / "halting_head.pt"
    if not halt_path.exists():
        raise FileNotFoundError(f"halting_head.pt not found in {model_dir}")
    print(f"    Loading HaltingHead from {halt_path}...")
    ckpt = torch.load(str(halt_path), weights_only=True, map_location="cpu")
    d_input = ckpt.get("d_input", 2561)
    head = build_halting_head(d_input)
    head.load_state_dict(ckpt["state_dict"])
    head.eval()

    print(f"    Engine loaded. d_input={d_input}")
    return tok, model, head

# ── Inference ─────────────────────────────────────────────────────────────────
def generate_latent(prompt, tok, model, head, domain="chat",
                    halt_threshold=0.70, max_new=150, mode="cpu"):
    import torch
    DOMAIN_MAX = {"chat": 5, "math": 25, "code": 45, "tool": 10}
    m = DOMAIN_MAX.get(domain, 10)
    device = next(model.parameters()).device

    p = 0.0
    lp = 0
    with torch.no_grad():
        for lp in range(50):
            toks = tok(
                prompt + "=" * lp,
                return_tensors="pt", truncation=True, max_length=512
            ).to(device)
            out  = model(**toks, output_hidden_states=True)
            h    = out.hidden_states[-1][0, -1, :].float().cpu()
            ln   = torch.tensor([lp / m], dtype=torch.float32)
            feat = torch.cat([h, ln]).unsqueeze(0)
            p    = head(feat).item()
            if p >= halt_threshold:
                break

        gen_out = model.generate(
            **toks, max_new_tokens=max_new,
            do_sample=False, repetition_penalty=1.1
        )

    answer = tok.decode(
        gen_out[0][toks["input_ids"].shape[1]:], skip_special_tokens=True
    ).strip()
    return answer, lp + 1, round(p, 3)

def detect_domain(prompt: str) -> str:
    p = prompt.lower()
    if any(k in p for k in ["def ", "import ", "class ", "```python", "function", "code"]):
        return "code"
    if any(k in p for k in ["=", "calculate", "solve", "math", "sum", "equation"]):
        return "math"
    return "chat"

# ── Export to .bin ────────────────────────────────────────────────────────────
def export_binary(model_dir: Path, out_path: Path):
    """Export to bare-metal binary format (OOSS magic). Same as export_ssm_binary.py."""
    import torch
    from transformers import AutoModelForCausalLM

    MAGIC   = 0x4F4F5353  # "OOSS"
    VERSION = 1

    def write_tensor(f, t):
        data = t.detach().float().cpu().contiguous().numpy().tobytes()
        f.write(data)
        return len(data)

    print(f"[export] Loading model from {model_dir} (float32)...")
    model = AutoModelForCausalLM.from_pretrained(
        str(model_dir), torch_dtype=torch.float32, trust_remote_code=True
    )
    sd  = model.state_dict()
    cfg = model.config

    halt_path = model_dir / "halting_head.pt"
    print(f"[export] Loading HaltingHead from {halt_path}...")
    ckpt    = torch.load(str(halt_path), weights_only=True, map_location="cpu")
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

    print(f"[export] d_model={d_model} n_layer={n_layer} d_state={d_state} "
          f"vocab_size={vocab_size} d_input={d_input}")

    total_bytes = 0
    with out_path.open("wb") as f:
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
                    # Try alternative key format
                    alt = key.replace("backbone.layers", "model.layers")
                    if alt in sd:
                        total_bytes += write_tensor(f, sd[alt])
                    else:
                        print(f"[WARN] key not found: {key}")

        # Embedding
        for embed_key in [
            "backbone.embeddings.weight",
            "backbone.embed_tokens.weight",
            "model.embed_tokens.weight",
        ]:
            if embed_key in sd:
                total_bytes += write_tensor(f, sd[embed_key])
                break
        else:
            print("[WARN] embed_tokens.weight not found in state_dict")

        # HaltingHead MLP weights
        for key in sorted(head_sd.keys()):
            total_bytes += write_tensor(f, head_sd[key])

    mb = total_bytes / (1024 * 1024)
    print(f"[export] Written: {out_path} ({mb:.1f} MB, {total_bytes:,} bytes)")
    return total_bytes

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Mamba-2.8B Local Runner")
    parser.add_argument("--model-dir",  type=str, default=None,
                        help="Path to model folder (default: same dir as script)")
    parser.add_argument("--prompt",     type=str, default=None)
    parser.add_argument("--domain",     type=str, default=None,
                        help="chat/math/code/tool (auto-detected if not set)")
    parser.add_argument("--loops",      type=float, default=0.70,
                        help="Halt threshold 0-1 (default 0.70)")
    parser.add_argument("--max-new",    type=int, default=150)
    parser.add_argument("--export",     type=str, default=None,
                        help="Export to bare-metal .bin (e.g. --export model.bin)")
    args = parser.parse_args()

    print()
    print("⚡ Mamba-2.8B Latent Reasoning Engine [LOCAL]")
    print()

    model_dir = find_model_dir(args.model_dir)
    print(f"  Model dir : {model_dir}")

    # ── Export mode (no chat, just export) ────────────────────────────────────
    if args.export:
        out = Path(args.export)
        export_binary(model_dir, out)
        return

    # ── Inference mode ────────────────────────────────────────────────────────
    print("  Detecting hardware...")
    load_cfg, mode = get_load_config()

    print("  Loading engine...")
    tok, model, head = load_engine(model_dir, load_cfg, mode)

    if mode == "cpu":
        print("\n  [READY] CPU mode — responses will take 30-90 seconds each")
    else:
        import torch
        vram = torch.cuda.memory_allocated() / 1e6
        print(f"\n  [READY] VRAM used: {vram:.0f} MB")

    print("  Domains: [LOGIC] math  [CHAT] conversation  [CODE] code  [TOOL] bash")
    print()

    def run_prompt(text):
        domain = args.domain or detect_domain(text)
        if not any(text.startswith(t) for t in ["[LOGIC]", "[CHAT]", "[CODE]", "[TOOL]"]):
            tag = {"math": "[LOGIC]", "code": "[CODE]", "chat": "[CHAT]"}.get(domain, "[CHAT]")
            text = f"{tag} {text}"
        if mode == "cpu":
            print("  Thinking... (CPU, please wait ~30-90s)")
        answer, loops, p = generate_latent(
            text, tok, model, head,
            domain=domain, halt_threshold=args.loops,
            max_new=args.max_new, mode=mode
        )
        print(f"\n  ({loops} loops, P={p})")
        print(f"  {answer}\n")

    if args.prompt:
        run_prompt(args.prompt)
    else:
        print("  Type your prompt and press Enter. 'quit' to exit.\n")
        while True:
            try:
                user_input = input("You: ").strip()
            except (EOFError, KeyboardInterrupt):
                print("\n  Goodbye.")
                break
            if not user_input:
                continue
            if user_input.lower() in ("quit", "exit", "q"):
                print("  Goodbye.")
                break
            run_prompt(user_input)

if __name__ == "__main__":
    main()
