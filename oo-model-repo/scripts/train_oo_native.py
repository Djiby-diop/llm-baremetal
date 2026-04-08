"""
Training from scratch — OO Native Model.
Pipeline complet: tokenizer → dataset → training → checkpoint.

Usage:
  python train_oo_native.py configs/oo_native_v1.json [--dry-run]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import torch
from torch.utils.data import Dataset, DataLoader
from transformers import get_cosine_schedule_with_warmup

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
from oo_model.oo_native import OONativeModel, OONativeConfig
from oo_model.oo_tokenizer import OOTokenizer

LOOP_TOKEN = "="
THINK_TOKEN = "[OO:THINK]"
ACT_TOKEN = "[OO:ACT]"
END_TOKEN = "[OO:END]"
DOMAIN_TOKENS = {"system": "[SYSTEM]", "math": "[MATH]", "code": "[CODE]", "chat": "[CHAT]"}


class OONativeDataset(Dataset):
    """
    Dataset pour OO-Native.
    Format d'entrée: instruction + dark_loops + response + domain
    Format tokenisé:
      [DOMAIN] [OO:THINK] instruction = * loops [OO:ACT] response [OO:END]
    """

    def __init__(self, path: str, tokenizer: OOTokenizer, max_length: int = 512):
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
        domain = row.get("domain", "chat")
        loops  = int(row.get("dark_loops", 3))

        domain_tok = DOMAIN_TOKENS.get(domain, "[CHAT]")
        text = (
            f"{domain_tok} {THINK_TOKEN} "
            f"{row['instruction']}"
            f"{LOOP_TOKEN * loops}"
            f"{ACT_TOKEN} {row['response']} {END_TOKEN}"
        )

        ids = self.tokenizer.encode(text)
        if len(ids) > self.max_length:
            ids = ids[:self.max_length]
        else:
            ids += [2] * (self.max_length - len(ids))  # pad with <eos>

        input_ids = torch.tensor(ids, dtype=torch.long)
        return {"input_ids": input_ids, "labels": input_ids.clone()}


def train(config_path: str, dry_run: bool = False) -> None:
    cfg_raw = json.loads(Path(config_path).read_text())
    tc = cfg_raw["training"]
    device = "cuda" if torch.cuda.is_available() else "cpu"
    seed   = tc["seed"]
    steps  = 20 if dry_run else tc["max_steps"]

    torch.manual_seed(seed)
    print(f"[oo-native] device={device} dry_run={dry_run}")

    # ── Tokenizer ──────────────────────────────────────────
    vocab_path = Path("data/oo_vocab.json")
    if not vocab_path.exists():
        print("[oo-native] Building tokenizer...")
        from oo_model.oo_tokenizer import cmd_build
        cmd_build(cfg_raw["dataset"]["train"], str(vocab_path))

    tokenizer = OOTokenizer.load(str(vocab_path))
    print(f"[oo-native] Tokenizer vocab_size={tokenizer.vocab_size}")

    # ── Model ───────────────────────────────────────────────
    arch = cfg_raw["architecture"]
    model_cfg = OONativeConfig(
        vocab_size     = tokenizer.vocab_size,
        d_model        = arch["d_model"],
        n_layer        = arch["n_layer"] if not dry_run else 2,
        d_state        = arch["d_state"],
        d_conv         = arch["d_conv"],
        expand         = arch["expand"],
        context_length = arch["context_length"],
    )
    model = OONativeModel(model_cfg).to(device)
    stats = model.count_params()
    print(f"[oo-native] Model params: {stats['total']:,} total | {stats['trainable']:,} trainable")

    # ── Dataset ─────────────────────────────────────────────
    ds = OONativeDataset(cfg_raw["dataset"]["train"], tokenizer, max_length=64 if dry_run else 512)
    dl = DataLoader(ds, batch_size=tc["micro_batch_size"], shuffle=True, drop_last=True)
    print(f"[oo-native] Dataset: {len(ds)} samples")

    # ── Optimizer ───────────────────────────────────────────
    optim = torch.optim.AdamW(
        model.parameters(),
        lr=tc["learning_rate"],
        weight_decay=tc["weight_decay"],
        betas=(0.9, 0.95),
    )
    scheduler = get_cosine_schedule_with_warmup(optim, tc["warmup_steps"], steps)

    # ── Training loop ───────────────────────────────────────
    model.train()
    step = 0
    grad_accum = tc["gradient_accumulation"]
    accum = 0
    optim.zero_grad()

    for epoch in range(9999):
        for batch in dl:
            if step >= steps:
                break
            input_ids = batch["input_ids"].to(device)
            labels    = batch["labels"].to(device)

            out  = model(input_ids=input_ids, labels=labels)
            loss = out["loss"] / grad_accum
            loss.backward()
            accum += 1

            if accum >= grad_accum:
                torch.nn.utils.clip_grad_norm_(model.parameters(), tc["grad_clip"])
                optim.step()
                scheduler.step()
                optim.zero_grad()
                accum = 0
                step += 1

                if step % 10 == 0 or dry_run:
                    print(f"[oo-native] step={step}/{steps} loss={loss.item() * grad_accum:.4f} "
                          f"lr={scheduler.get_last_lr()[0]:.6f}")

        if step >= steps:
            break

    # ── Save ────────────────────────────────────────────────
    save_dir = Path("checkpoints/oo-native-v1")
    save_dir.mkdir(parents=True, exist_ok=True)
    torch.save({"model_state": model.state_dict(), "config": model_cfg.__dict__}, save_dir / "oo_native_v1.pt")
    tokenizer.save(str(save_dir / "tokenizer.json"))
    print(f"[oo-native] Checkpoint saved to {save_dir}/")
    if dry_run:
        print("[oo-native] DRY RUN complete — model architecture validated ✓")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: train_oo_native.py <config.json> [--dry-run]")
        sys.exit(1)
    dry = "--dry-run" in sys.argv
    train(sys.argv[1], dry_run=dry)
