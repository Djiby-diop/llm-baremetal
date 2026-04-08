"""
Phase 2: Train HaltingHead on hidden states from Phase 1 checkpoint.
Uses fractional ramp labels (not binary) — avoids representational collapse.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))
from oo_model.mamba_model import OOMambaEngine, HaltingHead

LOOP_TOKEN = "="
RAMP = [0.25, 0.58, 0.91, 1.0]  # fractional labels at 0%, 33%, 66%, 100% of target depth


class HaltDataset(Dataset):
    def __init__(self, samples_path: str, engine: OOMambaEngine, tokenizer, device: str, d_model: int):
        self.items: list[tuple[torch.Tensor, float]] = []

        with open(samples_path) as f:
            rows = [json.loads(l) for l in f if l.strip()]

        engine.eval()
        with torch.no_grad():
            for row in rows:
                max_loops = int(row.get("dark_loops", 5))
                positions = [
                    0,
                    max(1, max_loops // 3),
                    max(2, 2 * max_loops // 3),
                    max_loops,
                ]
                for pos, label in zip(positions, RAMP):
                    text = row["instruction"] + LOOP_TOKEN * pos
                    toks = tokenizer(text, return_tensors="pt", truncation=True, max_length=256).to(device)
                    out = engine.backbone(**toks, output_hidden_states=True)
                    h = out.hidden_states[-1][0, -1, :].float().cpu()
                    lp_norm = torch.tensor([pos / max(max_loops, 1)], dtype=torch.float32)
                    inp = torch.cat([h, lp_norm])  # d_model + 1
                    self.items.append((inp, label))

    def __len__(self) -> int:
        return len(self.items)

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor]:
        x, y = self.items[idx]
        return x, torch.tensor([y], dtype=torch.float32)


def train_halting_head(config_path: str) -> None:
    cfg = json.loads(Path(config_path).read_text())
    phase1_dir = Path("checkpoints/oo-mamba-phase1")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    d_model = cfg["d_model"]
    d_input = d_model + 1

    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained(str(phase1_dir), trust_remote_code=True)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token

    engine = OOMambaEngine(base_model_name=str(phase1_dir), d_model=d_model)
    engine.to(device)

    print(f"[halt] Building hidden-state dataset...")
    ds = HaltDataset(cfg["dataset"]["train"], engine, tok, device, d_model)
    dl = DataLoader(ds, batch_size=64, shuffle=True)

    head = HaltingHead(d_input=d_input).to(device)
    optim = torch.optim.Adam(head.parameters(), lr=1e-3)
    criterion = nn.MSELoss()

    head.train()
    for epoch in range(100):
        total_loss = 0.0
        correct = 0
        total = 0
        for x, y in dl:
            x, y = x.to(device), y.to(device)
            h = x[:, :d_model]
            lp = x[:, d_model]
            pred = head(h, lp)
            loss = criterion(pred.unsqueeze(-1), y)
            optim.zero_grad()
            loss.backward()
            optim.step()
            total_loss += loss.item()
            correct += ((pred >= 0.7) == (y.squeeze() >= 0.7)).sum().item()
            total += len(y)

        if (epoch + 1) % 10 == 0:
            acc = correct / total * 100
            print(f"[halt] epoch={epoch+1}/100 loss={total_loss/len(dl):.4f} acc@0.7={acc:.1f}%")

    save_dir = Path("checkpoints/oo-mamba-phase2-halt")
    save_dir.mkdir(parents=True, exist_ok=True)
    torch.save({"d_input": d_input, "state_dict": head.state_dict()}, save_dir / "halting_head.pt")
    print(f"[halt] Saved to {save_dir}/halting_head.pt")


if __name__ == "__main__":
    config = sys.argv[1] if len(sys.argv) > 1 else "configs/oo_v1_mamba_130m.json"
    train_halting_head(config)
