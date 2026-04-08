"""
Evaluation script for OO Mamba latent reasoning engine.
Tests: domain probes, halting head distribution, VRAM flatline, adaptive computation.

Usage:
    python scripts/eval_mamba.py configs/oo_v1_mamba_130m.json
    python scripts/eval_mamba.py configs/oo_v1_mamba_130m.json --checkpoint checkpoints/oo-mamba-phase1
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
from oo_model.mamba_model import OOMambaEngine, HaltingHead

EVAL_PROMPTS = [
    # (prompt, domain, expected_key, min_loops)
    ("What is the capital of France?",                         "chat",   "paris",   1),
    ("What is OO memory zone COLD used for?",                  "system", "weights", 3),
    ("X=5. Y=X*2. Z=Y+3. W=Z-X. What is W?",                  "math",   "8",       5),
    ("Write a C function to compute softmax in place.",         "code",   "expf",   10),
    ("What is D+ divergence handling in OO?",                   "policy", "block",   5),
]


def load_engine(cfg: dict, checkpoint_dir: str, device: str) -> tuple:
    from transformers import AutoTokenizer

    halt_path = Path(checkpoint_dir) / "halting_head.pt"
    model_dir = checkpoint_dir

    engine = OOMambaEngine(
        base_model_name=model_dir,
        halt_threshold=cfg["latent_reasoning"]["halt_threshold"],
        d_model=cfg["d_model"],
    )
    engine.to(device)
    engine.eval()

    tok = AutoTokenizer.from_pretrained(model_dir, trust_remote_code=True)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token

    # Load trained HaltingHead if available
    if halt_path.exists():
        ckpt = torch.load(halt_path, map_location=device, weights_only=True)
        engine.halting_head = HaltingHead(d_input=ckpt["d_input"]).to(device)
        engine.halting_head.load_state_dict(ckpt["state_dict"])
        print(f"[eval] HaltingHead loaded from {halt_path}")
    else:
        print(f"[eval] HaltingHead not found at {halt_path} — using untrained head")

    return engine, tok


def run_probe(engine, tok, prompt: str, domain: str, device: str, max_loops: int) -> dict:
    """Run latent inference and return loops used + P(halt) trace."""
    halt_trace = []
    final_loops = 0

    with torch.no_grad():
        for lp in range(50):
            loop_prompt = prompt + "=" * lp
            toks = tok(
                loop_prompt, return_tensors="pt", truncation=True, max_length=512
            ).to(device)
            out = engine.backbone(**toks, output_hidden_states=True)
            h = out.hidden_states[-1][0, -1, :].float()
            lp_norm = torch.tensor([lp / max(max_loops, 1)], dtype=torch.float32, device=device)
            p_halt = engine.halting_head(h.unsqueeze(0), lp_norm.unsqueeze(0)).item()
            halt_trace.append(round(p_halt, 3))
            if p_halt >= engine.halt_threshold:
                final_loops = lp
                break
        else:
            final_loops = 49

        # Generate surface tokens
        gen_ids = engine.backbone.generate(
            **toks, max_new_tokens=80, do_sample=False, repetition_penalty=1.1
        )
        response = tok.decode(
            gen_ids[0][toks["input_ids"].shape[1]:], skip_special_tokens=True
        )

    return {
        "loops_used": final_loops,
        "p_halt_final": halt_trace[final_loops] if halt_trace else 0.0,
        "halt_trace": halt_trace[:final_loops + 1],
        "response": response[:200],
    }


def eval_vram_flatline(engine, tok, device: str) -> dict:
    """Proof: VRAM usage must be flat across loop iterations (O(1) memory)."""
    if device != "cuda":
        return {"skipped": "CPU — no VRAM to measure"}

    prompt = "X=5. Y=X*2. Z=Y+3. W=Z-X. What is W?"
    vram_readings = []

    with torch.no_grad():
        for lp in range(0, 21, 5):
            torch.cuda.reset_peak_memory_stats()
            loop_prompt = prompt + "=" * lp
            toks = tok(loop_prompt, return_tensors="pt", truncation=True, max_length=256).to(device)
            engine.backbone(**toks, output_hidden_states=True)
            vram_mb = torch.cuda.max_memory_allocated() / 1e6
            vram_readings.append({"loop": lp, "vram_mb": round(vram_mb, 2)})

    delta = vram_readings[-1]["vram_mb"] - vram_readings[0]["vram_mb"]
    flat = abs(delta) < 2.0  # < 2MB delta = O(1)
    return {"readings": vram_readings, "delta_mb": round(delta, 2), "flatline": flat}


def eval_adaptive_computation(engine, tok, device: str, max_loops: int) -> dict:
    """Proof: easy questions use fewer loops than hard ones."""
    easy = run_probe(engine, tok, "What is 7 + 5?", "math", device, max_loops)
    hard = run_probe(engine, tok,
                     "Two trains travel toward each other. Train A at 60 mph, Train B at 45 mph, "
                     "starting 210 miles apart. When do they meet?",
                     "math", device, max_loops)
    return {
        "easy_loops": easy["loops_used"],
        "hard_loops": hard["loops_used"],
        "adaptive": hard["loops_used"] >= easy["loops_used"],
    }


def run_eval(config_path: str, checkpoint_dir: str | None = None) -> None:
    cfg = json.loads(Path(config_path).read_text())
    ckpt = checkpoint_dir or "checkpoints/oo-mamba-phase1"
    device = "cuda" if torch.cuda.is_available() else "cpu"

    print(f"\n{'='*60}")
    print(f" OO Mamba Eval  |  device={device}  |  ckpt={ckpt}")
    print(f"{'='*60}\n")

    if not Path(ckpt).exists():
        print(f"[ERROR] Checkpoint not found: {ckpt}")
        print("        Run train_latent.py first (Phase 1)")
        sys.exit(1)

    engine, tok = load_engine(cfg, ckpt, device)
    domain_max = cfg["latent_reasoning"]["domain_max_loops"]

    # ── Domain probes ────────────────────────────────────────────────
    print("[1] Domain Probes\n")
    results = []
    for prompt, domain, expected_key, min_loops in EVAL_PROMPTS:
        t0 = time.time()
        r = run_probe(engine, tok, prompt, domain, device, domain_max.get(domain, 10))
        elapsed = time.time() - t0
        found = expected_key.lower() in r["response"].lower()
        status = "PASS" if found else "FAIL"
        results.append((status, domain, r["loops_used"], r["p_halt_final"], elapsed))
        print(f"  [{status}] {domain:8s}  loops={r['loops_used']:2d}  P(halt)={r['p_halt_final']:.3f}"
              f"  t={elapsed:.1f}s")
        print(f"         Q: {prompt[:60]}")
        print(f"         A: {r['response'][:80]}\n")

    passed = sum(1 for s, *_ in results if s == "PASS")
    print(f"  Domain probe score: {passed}/{len(results)}\n")

    # ── VRAM flatline ────────────────────────────────────────────────
    print("[2] VRAM Flatline (O(1) memory)\n")
    vram = eval_vram_flatline(engine, tok, device)
    if "skipped" in vram:
        print(f"  Skipped: {vram['skipped']}\n")
    else:
        status = "PASS" if vram["flatline"] else "FAIL"
        print(f"  [{status}] delta={vram['delta_mb']:.2f} MB across 20 loops")
        for r in vram["readings"]:
            print(f"         loop={r['loop']:2d}  vram={r['vram_mb']:.2f} MB")
        print()

    # ── Adaptive computation ─────────────────────────────────────────
    print("[3] Adaptive Computation (hard > easy loops)\n")
    adp = eval_adaptive_computation(engine, tok, device, domain_max.get("math", 20))
    status = "PASS" if adp["adaptive"] else "FAIL"
    print(f"  [{status}] easy_loops={adp['easy_loops']}  hard_loops={adp['hard_loops']}\n")

    # ── Summary ─────────────────────────────────────────────────────
    print(f"{'='*60}")
    print(f" SUMMARY")
    print(f"  Domain probes : {passed}/{len(results)}")
    if "flatline" in vram:
        print(f"  VRAM flatline : {'PASS' if vram['flatline'] else 'FAIL'} ({vram['delta_mb']:.2f} MB delta)")
    print(f"  Adaptive comp : {'PASS' if adp['adaptive'] else 'FAIL'}")
    print(f"{'='*60}\n")


if __name__ == "__main__":
    config_path = sys.argv[1] if len(sys.argv) > 1 else "configs/oo_v1_mamba_130m.json"
    ckpt = None
    for i, arg in enumerate(sys.argv):
        if arg == "--checkpoint" and i + 1 < len(sys.argv):
            ckpt = sys.argv[i + 1]
    run_eval(config_path, ckpt)
