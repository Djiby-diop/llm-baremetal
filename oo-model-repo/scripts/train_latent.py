"""
Phase 1 latent SFT training for OO Mamba engine.
Trains x_proj, dt_proj, embed_tokens on dark-loop dataset.

Dataset format (JSONL):
  {"instruction": "...", "dark_loops": 7, "response": "...", "domain": "math"}
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import torch
from torch.utils.data import Dataset, DataLoader
from transformers import AutoTokenizer, get_cosine_schedule_with_warmup

# Add src to path
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
from oo_model.mamba_model import OOMambaEngine


LOOP_TOKEN = "="


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


class LatentDataset(Dataset):
    def __init__(self, path: str, tokenizer, max_length: int = 512):
        self.samples: list[dict] = []
        self.tokenizer = tokenizer
        self.max_length = max_length

        with open(path) as f:
            for line in f:
                line = line.strip()
                if line:
                    self.samples.append(json.loads(line))

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(self, idx: int) -> dict[str, torch.Tensor]:
        row = self.samples[idx]
        loops = int(row.get("dark_loops", 3))
        # Format: instruction + dark loops + response
        text = row["instruction"] + LOOP_TOKEN * loops + row["response"]
        enc = self.tokenizer(
            text,
            max_length=self.max_length,
            truncation=True,
            padding="max_length",
            return_tensors="pt",
        )
        input_ids = enc["input_ids"].squeeze(0)
        return {"input_ids": input_ids, "labels": input_ids.clone()}


def train(config_path: str, dry_run: bool = False) -> None:
    import json
    cfg = json.loads(Path(config_path).read_text())

    base_model = cfg["training"]["base_model"]
    train_path = cfg["dataset"]["train"]
    steps      = 5 if dry_run else cfg["training"]["phases"][0]["steps"]
    lr         = cfg["training"]["phases"][0]["lr"]
    batch_size = 1 if dry_run else cfg["training"]["micro_batch_size"]
    grad_accum = 1 if dry_run else cfg["training"]["gradient_accumulation"]
    warmup     = 0 if dry_run else cfg["training"]["warmup_steps"]
    device     = "cuda" if torch.cuda.is_available() else "cpu"
    seed       = cfg["training"]["seed"]

    if dry_run:
        print("[dry-run] steps=5 batch=1 grad_accum=1")

    torch.manual_seed(seed)
    print(f"[train] device={device} base={base_model}")

    # Model
    engine = OOMambaEngine(base_model_name=base_model)
    engine.to(device)
    stats = engine.count_trainable_params()
    print(f"[train] params: total={stats['total']:,} trainable={stats['trainable']:,}")

    # Tokenizer
    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained(base_model, trust_remote_code=True)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token
    added_tokens = configure_special_tokens(tok, cfg)
    if added_tokens:
        engine.backbone.resize_token_embeddings(len(tok))
        print(f"[train] added_special_tokens={added_tokens} vocab={len(tok)}")

    # Dataset
    ds = LatentDataset(train_path, tok)
    dl = DataLoader(ds, batch_size=batch_size, shuffle=True, drop_last=True)

    # Optimizer
    optim = torch.optim.AdamW(
        [p for p in engine.parameters() if p.requires_grad],
        lr=lr,
        weight_decay=0.01,
    )
    scheduler = get_cosine_schedule_with_warmup(optim, warmup, steps)

    # Training loop
    engine.train()
    step = 0
    accum = 0
    optim.zero_grad()

    for epoch in range(9999):
        for batch in dl:
            if step >= steps:
                break
            input_ids = batch["input_ids"].to(device)
            labels    = batch["labels"].to(device)

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

                if step % 50 == 0:
                    print(f"[train] step={step}/{steps} loss={loss.item() * grad_accum:.4f} lr={scheduler.get_last_lr()[0]:.6f}")

        if step >= steps:
            break

    # Save
    save_dir = Path("checkpoints/oo-mamba-phase1")
    save_dir.mkdir(parents=True, exist_ok=True)
    if not dry_run:
        engine.backbone.save_pretrained(str(save_dir))
        tok.save_pretrained(str(save_dir))
    print(f"[train] {'dry-run complete' if dry_run else f'Saved to {save_dir}'}")


if __name__ == "__main__":
    config   = sys.argv[1] if len(sys.argv) > 1 else "configs/oo_v1_mamba_130m.json"
    dry_run  = "--dry-run" in sys.argv
    train(config, dry_run=dry_run)
