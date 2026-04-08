"""
Evaluate the OO Mamba phase-4 tool-use checkpoint.

Checks whether the model emits the expected structured format:
  [AGENT] ...
  <TOOL: BASH>
  command
  </TOOL>
  <RESULT>
  ...
  </RESULT>

Usage:
  python scripts/eval_tool_use.py configs/oo_v1_mamba_130m.json
  python scripts/eval_tool_use.py configs/oo_v1_mamba_130m.json --checkpoint checkpoints/oo-mamba-phase4-tool
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
from oo_model.mamba_model import OOMambaEngine, HaltingHead

PROMPTS = [
    {
        "prompt": "Check which Python version is installed on this machine.",
        "command_keys": ["python", "--version"],
        "result_keys": ["python", "3."],
    },
    {
        "prompt": "Show whether CUDA is available to PyTorch.",
        "command_keys": ["torch.cuda.is_available", "python -c"],
        "result_keys": ["false"],
    },
    {
        "prompt": "List the top-level files in the repository.",
        "command_keys": ["Get-ChildItem", "-Name"],
        "result_keys": ["README.md", "scripts"],
    },
    {
        "prompt": "Check free space on the main drive.",
        "command_keys": ["Get-PSDrive", "C"],
        "result_keys": ["Free", "Used"],
    },
]


def load_engine(cfg: dict, checkpoint_dir: Path, device: str):
    from transformers import AutoTokenizer

    tok = AutoTokenizer.from_pretrained(str(checkpoint_dir), trust_remote_code=True)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token

    engine = OOMambaEngine(
        base_model_name=str(checkpoint_dir),
        halt_threshold=cfg["latent_reasoning"]["halt_threshold"],
        d_model=cfg["d_model"],
    )
    engine.to(device)
    engine.eval()

    halt_path = checkpoint_dir / "halting_head.pt"
    if halt_path.exists():
        ckpt = torch.load(halt_path, map_location=device, weights_only=True)
        engine.halting_head = HaltingHead(d_input=ckpt["d_input"]).to(device)
        engine.halting_head.load_state_dict(ckpt["state_dict"])
        print(f"[tool-eval] halting head loaded from {halt_path}")
    else:
        print(f"[tool-eval] halting head not found in {checkpoint_dir} — using current head weights")

    return engine, tok


def run_tool_prompt(engine, tok, prompt: str, device: str, max_loops: int) -> dict:
    trace = []
    final_loops = 0

    with torch.no_grad():
        for lp in range(50):
            loop_prompt = f"[AGENT] {prompt}" + "=" * lp
            toks = tok(loop_prompt, return_tensors="pt", truncation=True, max_length=512).to(device)
            out = engine.backbone(**toks, output_hidden_states=True)
            h = out.hidden_states[-1][0, -1, :].float()
            lp_norm = torch.tensor([lp / max(max_loops, 1)], dtype=torch.float32, device=device)
            p_halt = engine.halting_head(h.unsqueeze(0), lp_norm.unsqueeze(0)).item()
            trace.append(round(p_halt, 3))
            if p_halt >= engine.halt_threshold:
                final_loops = lp
                break
        else:
            final_loops = 49

        gen_ids = engine.backbone.generate(
            **toks,
            max_new_tokens=160,
            do_sample=False,
            repetition_penalty=1.05,
        )
        text = tok.decode(gen_ids[0][toks["input_ids"].shape[1]:], skip_special_tokens=False)

    return {
        "loops_used": final_loops,
        "p_halt_final": trace[final_loops] if trace else 0.0,
        "text": text.strip(),
    }


def has_tool_structure(text: str) -> bool:
    required = ["<TOOL: BASH>", "</TOOL>", "<RESULT>", "</RESULT>"]
    return all(tok in text for tok in required)


def keys_present(text: str, keys: list[str]) -> bool:
    lower = text.lower()
    return all(key.lower() in lower for key in keys)


def run_eval(config_path: str, checkpoint_dir: str | None = None) -> None:
    cfg = json.loads(Path(config_path).read_text())
    ckpt = Path(checkpoint_dir or "checkpoints/oo-mamba-phase4-tool")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    max_loops = cfg["latent_reasoning"]["domain_max_loops"].get("tool", 8)

    if not ckpt.exists():
        print(f"[ERROR] Tool-use checkpoint not found: {ckpt}")
        print("        Run train_tool_sft.py first.")
        sys.exit(1)

    print(f"[tool-eval] device={device} ckpt={ckpt}")
    engine, tok = load_engine(cfg, ckpt, device)

    passed = 0
    for idx, spec in enumerate(PROMPTS, start=1):
        result = run_tool_prompt(engine, tok, spec["prompt"], device, max_loops)
        structure_ok = has_tool_structure(result["text"])
        command_ok = keys_present(result["text"], spec["command_keys"])
        result_ok = keys_present(result["text"], spec["result_keys"])
        ok = structure_ok and command_ok and result_ok
        passed += int(ok)
        status = "PASS" if ok else "FAIL"
        print(f"[{idx}] {status} loops={result['loops_used']} P(halt)={result['p_halt_final']:.3f}")
        print(f"    Q: {spec['prompt']}")
        print(f"    A: {result['text'][:220]}\n")

    print(f"[tool-eval] score={passed}/{len(PROMPTS)}")


if __name__ == "__main__":
    config_path = sys.argv[1] if len(sys.argv) > 1 else "configs/oo_v1_mamba_130m.json"
    ckpt = None
    for i, arg in enumerate(sys.argv):
        if arg == "--checkpoint" and i + 1 < len(sys.argv):
            ckpt = sys.argv[i + 1]
    run_eval(config_path, ckpt)