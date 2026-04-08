"""
Phase 4 tool-use SFT for OO Mamba.

Trains the phase-1 checkpoint on structured traces using:
  [AGENT] instruction
  =====
  <TOOL: BASH>
  command
  </TOOL>
  <RESULT>
  stdout
  </RESULT>
  ==
  natural language answer
"""
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

import torch
from torch.utils.data import Dataset, DataLoader
from transformers import AutoTokenizer, get_cosine_schedule_with_warmup

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
from oo_model.mamba_model import OOMambaEngine


class ToolUseDataset(Dataset):
    def __init__(self, path: str, tokenizer, cfg: dict, max_length: int = 512):
        self.rows: list[dict] = []
        self.tokenizer = tokenizer
        self.max_length = max_length
        self.special = cfg["special_tokens"]

        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    self.rows.append(json.loads(line))

    def __len__(self) -> int:
        return len(self.rows)

    def __getitem__(self, idx: int) -> dict[str, torch.Tensor]:
        row = self.rows[idx]
        pre = "=" * int(row.get("dark_loops", 4))
        post = "=" * int(row.get("post_tool_loops", 2))

        text = (
            f"{self.special['agent_start']} {row['instruction']}\n"
            f"{pre}\n"
            f"{self.special['tool_start']}\n"
            f"{row['tool_command']}\n"
            f"{self.special['tool_end']}\n"
            f"{self.special['result_start']}\n"
            f"{row['tool_result']}\n"
            f"{self.special['result_end']}\n"
            f"{post}\n"
            f"{row['response']}"
        )

        enc = self.tokenizer(
            text,
            max_length=self.max_length,
            truncation=True,
            padding="max_length",
            return_tensors="pt",
        )
        input_ids = enc["input_ids"].squeeze(0)
        return {"input_ids": input_ids, "labels": input_ids.clone()}


def configure_special_tokens(tokenizer, cfg: dict) -> int:
    special_cfg = cfg.get("special_tokens", {})
    extra_tokens = [
        special_cfg.get("agent_start"),
        special_cfg.get("tool_start"),
        special_cfg.get("tool_end"),
        special_cfg.get("result_start"),
        special_cfg.get("result_end"),
    ]
    tokens = [tok for tok in extra_tokens if tok]
    if not tokens:
        return 0
    return tokenizer.add_special_tokens({"additional_special_tokens": tokens})


def train(config_path: str, dry_run: bool = False) -> None:
    cfg = json.loads(Path(config_path).read_text())
    phase1_dir = Path("checkpoints/oo-mamba-phase1")
    phase2_halt = Path("checkpoints/oo-mamba-phase2-halt/halting_head.pt")
    if not phase1_dir.exists():
        raise FileNotFoundError("Phase 1 checkpoint missing: checkpoints/oo-mamba-phase1")

    tool_path = Path(cfg["dataset"].get("tool_use", "data/processed/tool_use.jsonl"))
    if not tool_path.exists():
        raise FileNotFoundError(f"Tool-use dataset missing: {tool_path}. Run build_tool_dataset.py first.")

    phase_cfg = next((p for p in cfg["training"]["phases"] if p["name"] == "tool_sft"), None)
    if phase_cfg is None:
        raise ValueError("tool_sft phase missing from config")

    steps = 5 if dry_run else int(phase_cfg["steps"])
    lr = float(phase_cfg["lr"])
    batch_size = 1 if dry_run else int(cfg["training"]["micro_batch_size"])
    grad_accum = 1 if dry_run else int(cfg["training"]["gradient_accumulation"])
    warmup = 0 if dry_run else min(int(cfg["training"]["warmup_steps"]), steps)
    device = "cuda" if torch.cuda.is_available() else "cpu"

    print(f"[tool-sft] device={device} dry_run={dry_run}")

    tok = AutoTokenizer.from_pretrained(str(phase1_dir), trust_remote_code=True)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token

    engine = OOMambaEngine(
        base_model_name=str(phase1_dir),
        halt_threshold=cfg["latent_reasoning"]["halt_threshold"],
        d_model=cfg["d_model"],
    )
    added = configure_special_tokens(tok, cfg)
    if added:
        engine.backbone.resize_token_embeddings(len(tok))
        print(f"[tool-sft] added_special_tokens={added} vocab={len(tok)}")
    engine.to(device)

    ds = ToolUseDataset(str(tool_path), tok, cfg, max_length=256 if dry_run else 512)
    dl = DataLoader(ds, batch_size=batch_size, shuffle=True, drop_last=True)

    optim = torch.optim.AdamW(
        [p for p in engine.parameters() if p.requires_grad],
        lr=lr,
        weight_decay=float(cfg["training"].get("weight_decay", 0.01)),
    )
    scheduler = get_cosine_schedule_with_warmup(optim, warmup, steps)

    engine.train()
    step = 0
    accum = 0
    optim.zero_grad()

    for epoch in range(9999):
        for batch in dl:
            if step >= steps:
                break
            input_ids = batch["input_ids"].to(device)
            labels = batch["labels"].to(device)
            out = engine(input_ids=input_ids, labels=labels)
            loss = out["loss"] / grad_accum
            loss.backward()
            accum += 1

            if accum >= grad_accum:
                torch.nn.utils.clip_grad_norm_(engine.parameters(), 1.0)
                optim.step()
                scheduler.step()
                optim.zero_grad()
                accum = 0
                step += 1
                print(f"[tool-sft] step={step}/{steps} loss={loss.item() * grad_accum:.4f} lr={scheduler.get_last_lr()[0]:.6f}")

        if step >= steps:
            break

    save_dir = Path("checkpoints/oo-mamba-phase4-tool")
    save_dir.mkdir(parents=True, exist_ok=True)
    if not dry_run:
        engine.backbone.save_pretrained(str(save_dir))
        tok.save_pretrained(str(save_dir))
        if phase2_halt.exists():
            shutil.copy2(phase2_halt, save_dir / "halting_head.pt")
            print(f"[tool-sft] copied halting head -> {save_dir / 'halting_head.pt'}")
    print(f"[tool-sft] {'dry-run complete' if dry_run else f'Saved to {save_dir}'}")


if __name__ == "__main__":
    config = sys.argv[1] if len(sys.argv) > 1 else "configs/oo_v1_mamba_130m.json"
    dry = "--dry-run" in sys.argv
    train(config, dry_run=dry)