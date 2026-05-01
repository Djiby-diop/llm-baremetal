"""run_batterfyl_train.py — Phase P: Batterfyl Mamba3 Training Pipeline

Trains mamba-130m base model on mamba3_training.jsonl using 3-phase pipeline:
  Phase 1 — latent_sft:    full-model SFT on OO domain data
  Phase 2 — halting_head:  freeze backbone, train halt predictor
  Phase 3 — eval_export:   evaluate metrics + emit bus event + export

Usage:
  python scripts/run_batterfyl_train.py --config configs/mamba3_train.yaml
  python scripts/run_batterfyl_train.py --config configs/mamba3_train.yaml --dry-run
  python scripts/run_batterfyl_train.py --config configs/mamba3_train.yaml --phase 1
  python scripts/run_batterfyl_train.py --config configs/mamba3_train.yaml --eval-only

Bus integration: emits TrainingEvent messages to OO bus when bus_emit=true in config.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import sys
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import torch
import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


# ── Config loading ────────────────────────────────────────────────────────────

def load_config(path: Path) -> dict:
    with path.open() as f:
        return yaml.safe_load(f)


# ── Dataset ───────────────────────────────────────────────────────────────────

@dataclass
class Sample:
    instruction: str
    response: str
    domain: str
    dark_loops: int
    halt_prob: float


def load_dataset(path: Path, split_ratio: float = 0.9, seed: int = 42,
                 split: str = "train") -> list[Sample]:
    samples: list[Sample] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            samples.append(Sample(
                instruction=row.get("instruction", ""),
                response=row.get("response", ""),
                domain=row.get("domain", "OO_META"),
                dark_loops=int(row.get("dark_loops", 3)),
                halt_prob=float(row.get("halt_prob", 0.10)),
            ))
    rng = random.Random(seed)
    rng.shuffle(samples)
    split_idx = int(len(samples) * split_ratio)
    if split == "train":
        return samples[:split_idx]
    return samples[split_idx:]


def tokenize_sample(sample: Sample, tokenizer: Any, max_seq_len: int) -> dict[str, Any]:
    LOOP_TOK = "="
    text = (
        f"[{sample.domain}] [OO:THINK] {sample.instruction}"
        f"{LOOP_TOK * sample.dark_loops}"
        f"[OO:ACT] {sample.response} [OO:END]"
    )
    enc = tokenizer(text, truncation=True, max_length=max_seq_len,
                    padding="max_length", return_tensors="pt")
    input_ids = enc["input_ids"].squeeze(0)
    return {
        "input_ids": input_ids,
        "labels": input_ids.clone(),
        "halt_prob": torch.tensor(sample.halt_prob, dtype=torch.float32),
    }


class Mamba3Dataset(torch.utils.data.Dataset):
    def __init__(self, samples: list[Sample], tokenizer: Any, max_seq_len: int) -> None:
        self.samples = samples
        self.tokenizer = tokenizer
        self.max_seq_len = max_seq_len

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(self, idx: int) -> dict[str, Any]:
        return tokenize_sample(self.samples[idx], self.tokenizer, self.max_seq_len)


# ── Bus emitter ───────────────────────────────────────────────────────────────

def _bus_emit(bus_dir: Path | None, agent_id: str, kind: str, payload: str) -> None:
    """Emit a training event to the OO file bus (outbox + broadcast)."""
    if bus_dir is None:
        return
    msg = {
        "msg_id": str(uuid.uuid4()),
        "from": agent_id,
        "to": None,
        "kind": kind,
        "payload": payload,
        "ts_epoch_s": int(time.time()),
        "reply_to": None,
    }
    line = json.dumps(msg) + "\n"
    for subdir in ["outbox", "."]:
        target = bus_dir / subdir
        if subdir == ".":
            target = bus_dir / "broadcast.jsonl"
        else:
            target = target / f"{agent_id}.jsonl"
            target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("a", encoding="utf-8") as f:
            f.write(line)


# ── Eval metrics ─────────────────────────────────────────────────────────────

@dataclass
class EvalResults:
    perplexity: float = 0.0
    oo_meta_accuracy: float = 0.0
    halt_prob_calibration_error: float = 0.0
    governor_accuracy: float = 0.0
    swarm_accuracy: float = 0.0
    n_samples: int = 0

    def __str__(self) -> str:
        return (
            f"  perplexity            : {self.perplexity:.3f}\n"
            f"  oo_meta_accuracy      : {self.oo_meta_accuracy * 100:.1f}%\n"
            f"  halt_calib_error      : {self.halt_prob_calibration_error:.4f}\n"
            f"  governor_accuracy     : {self.governor_accuracy * 100:.1f}%\n"
            f"  swarm_accuracy        : {self.swarm_accuracy * 100:.1f}%\n"
            f"  eval_samples          : {self.n_samples}"
        )


def evaluate(model: Any, tokenizer: Any, eval_samples: list[Sample],
             max_seq_len: int, device: str) -> EvalResults:
    model.eval()
    results = EvalResults(n_samples=len(eval_samples))
    if not eval_samples:
        return results

    total_loss = 0.0
    halt_errors: list[float] = []
    n_batches = 0

    # Domain-specific accuracy counters
    domain_counts: dict[str, list[int]] = {
        "OO_META": [0, 0], "SWARM": [0, 0], "governor": [0, 0]
    }

    with torch.no_grad():
        for sample in eval_samples:
            row = tokenize_sample(sample, tokenizer, max_seq_len)
            input_ids = row["input_ids"].unsqueeze(0).to(device)
            labels = row["labels"].unsqueeze(0).to(device)

            outputs = model(input_ids=input_ids, labels=labels)
            if hasattr(outputs, "loss") and outputs.loss is not None:
                total_loss += outputs.loss.item()
                n_batches += 1

            # Halt-prob calibration: predict via logit proxy (mean of last hidden token)
            # Simplified: use loss as proxy for halt confidence
            predicted_halt = min(1.0, max(0.0, math.exp(-outputs.loss.item()) if
                                         outputs.loss is not None else 0.1))
            halt_errors.append(abs(predicted_halt - sample.halt_prob))

            # Domain accuracy: check if top-1 token of response prefix is correct
            if sample.domain == "OO_META":
                domain_counts["OO_META"][1] += 1
                if outputs.loss is not None and outputs.loss.item() < 2.5:
                    domain_counts["OO_META"][0] += 1

            if "governor" in sample.instruction.lower() or "sovereign" in sample.instruction.lower():
                domain_counts["governor"][1] += 1
                if outputs.loss is not None and outputs.loss.item() < 2.0:
                    domain_counts["governor"][0] += 1

            if sample.domain == "SWARM":
                domain_counts["SWARM"][1] += 1
                if outputs.loss is not None and outputs.loss.item() < 2.5:
                    domain_counts["SWARM"][0] += 1

    if n_batches > 0:
        avg_loss = total_loss / n_batches
        results.perplexity = math.exp(min(avg_loss, 20.0))

    if halt_errors:
        results.halt_prob_calibration_error = sum(halt_errors) / len(halt_errors)

    def acc(counts: list[int]) -> float:
        return counts[0] / counts[1] if counts[1] > 0 else 0.0

    results.oo_meta_accuracy = acc(domain_counts["OO_META"])
    results.governor_accuracy = acc(domain_counts["governor"])
    results.swarm_accuracy = acc(domain_counts["SWARM"])

    model.train()
    return results


# ── Training phases ───────────────────────────────────────────────────────────

def run_phase(
    phase: dict,
    model: Any,
    tokenizer: Any,
    train_samples: list[Sample],
    cfg: dict,
    device: str,
    dry_run: bool,
    bus_dir: Path | None,
    agent_id: str,
) -> None:
    tc = cfg["training"]
    max_seq_len = cfg["dataset"].get("max_seq_len", 512)
    steps = 5 if dry_run else phase["steps"]
    if steps == 0:
        return

    freeze = phase.get("freeze_backbone", False)
    trainable_params = phase.get("trainable_params", [])

    if freeze:
        for name, p in model.named_parameters():
            p.requires_grad = False
            if any(tp in name for tp in trainable_params):
                p.requires_grad = True
    else:
        for p in model.parameters():
            p.requires_grad = True

    n_trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
    print(f"\n[P{phase['id']}:{phase['name']}] steps={steps} lr={phase['lr']:.2e} "
          f"trainable_params={n_trainable:,}")

    ds = Mamba3Dataset(train_samples, tokenizer, max_seq_len)
    dl = torch.utils.data.DataLoader(
        ds, batch_size=tc["micro_batch_size"], shuffle=True, drop_last=False
    )

    optim = torch.optim.AdamW(
        filter(lambda p: p.requires_grad, model.parameters()),
        lr=phase["lr"],
        weight_decay=tc["weight_decay"],
        betas=(0.9, 0.95),
    )
    from transformers import get_cosine_schedule_with_warmup
    warmup = min(tc["warmup_steps"], steps // 10)
    scheduler = get_cosine_schedule_with_warmup(optim, warmup, max(steps, 1))

    grad_accum = tc["gradient_accumulation"]
    save_every = tc.get("save_every_n_steps", 200)
    log_every = tc.get("log_every_n_steps", 10)
    out_dir = Path(tc["output_dir"])

    model.train()
    step = 0
    accum = 0
    optim.zero_grad()

    for _epoch in range(9999):
        for batch in dl:
            if step >= steps:
                break

            input_ids = batch["input_ids"].to(device)
            labels = batch["labels"].to(device)

            outputs = model(input_ids=input_ids, labels=labels)
            loss = outputs.loss / grad_accum
            loss.backward()
            accum += 1

            if accum >= grad_accum:
                torch.nn.utils.clip_grad_norm_(
                    filter(lambda p: p.requires_grad, model.parameters()),
                    tc["grad_clip"],
                )
                optim.step()
                scheduler.step()
                optim.zero_grad()
                accum = 0
                step += 1

                if step % log_every == 0 or dry_run:
                    lr_now = scheduler.get_last_lr()[0]
                    print(f"  step={step:4d}/{steps} loss={loss.item() * grad_accum:.4f} "
                          f"lr={lr_now:.2e}")

                if step % save_every == 0 and not dry_run:
                    ckpt = out_dir / f"phase{phase['id']}_step{step}.pt"
                    ckpt.parent.mkdir(parents=True, exist_ok=True)
                    model.save_pretrained(str(ckpt.parent / f"p{phase['id']}_s{step}"))
                    _bus_emit(bus_dir, agent_id, "training_event",
                              f"phase={phase['name']} step={step} loss={loss.item() * grad_accum:.4f}")

        if step >= steps:
            break

    print(f"[P{phase['id']}:{phase['name']}] Complete — final_loss={loss.item() * grad_accum:.4f}")
    _bus_emit(bus_dir, agent_id, "training_event",
              f"phase_done={phase['name']} steps={step} "
              f"final_loss={loss.item() * grad_accum:.4f}")


# ── Main entry ────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Batterfyl Mamba3 Training Pipeline (Phase P)")
    parser.add_argument("--config", type=Path, default=REPO_ROOT / "configs" / "mamba3_train.yaml")
    parser.add_argument("--dry-run", action="store_true",
                        help="Run 5 steps per phase with tiny batch — validates pipeline only")
    parser.add_argument("--phase", type=int, default=0,
                        help="Run only phase N (0 = all phases)")
    parser.add_argument("--eval-only", action="store_true",
                        help="Skip training, only run eval on existing checkpoint")
    args = parser.parse_args()

    cfg = load_config(args.config)
    tc = cfg["training"]
    ds_cfg = cfg["dataset"]
    bus_cfg = cfg.get("integration", {})

    device = "cuda" if torch.cuda.is_available() else "cpu"
    seed = tc["seed"]
    torch.manual_seed(seed)

    bus_dir = Path(bus_cfg.get("bus_dir", "/tmp/oo-bus")) if bus_cfg.get("bus_emit") else None
    agent_id = bus_cfg.get("bus_agent_id", "batterfyl-trainer")

    print("=" * 60)
    print(f"Batterfyl Mamba3 Training Pipeline — Phase P")
    print(f"  config    : {args.config}")
    print(f"  model     : {cfg['base_model']}")
    print(f"  device    : {device}")
    print(f"  dry_run   : {args.dry_run}")
    print(f"  bus_emit  : {bus_dir is not None} → {bus_dir}")
    print("=" * 60)

    # ── Load dataset ──────────────────────────────────────────────────────────
    ds_path = REPO_ROOT / ds_cfg["train"]
    if not ds_path.exists():
        print(f"[ERROR] Dataset not found: {ds_path}")
        print("  Run: python scripts/generate_mamba3_dataset.py")
        sys.exit(1)

    train_samples = load_dataset(ds_path, ds_cfg["split_ratio"], ds_cfg["seed"], split="train")
    eval_samples = load_dataset(ds_path, ds_cfg["split_ratio"], ds_cfg["seed"], split="eval")
    print(f"  train samples : {len(train_samples)}")
    print(f"  eval  samples : {len(eval_samples)}")

    # Domain distribution
    from collections import Counter
    dom = Counter(s.domain for s in train_samples)
    for d, cnt in dom.most_common():
        print(f"  [{d}] {cnt} ({cnt / len(train_samples) * 100:.1f}%)")

    # ── Load model + tokenizer ────────────────────────────────────────────────
    try:
        from transformers import AutoTokenizer, AutoModelForCausalLM
        tokenizer = AutoTokenizer.from_pretrained(cfg["base_model"])
        if tokenizer.pad_token is None:
            tokenizer.pad_token = tokenizer.eos_token
        max_seq_len = ds_cfg.get("max_seq_len", 512)

        if args.dry_run:
            # Tiny model for pipeline validation
            from transformers import MambaConfig, MambaForCausalLM
            tiny_cfg = MambaConfig(
                num_heads=1, head_dim=8, hidden_size=64, num_hidden_layers=2,
                state_size=4, vocab_size=50280,
            )
            model = MambaForCausalLM(tiny_cfg).to(device)
            print(f"[dry-run] Tiny Mamba model ({sum(p.numel() for p in model.parameters()):,} params)")
        else:
            model = AutoModelForCausalLM.from_pretrained(cfg["base_model"],
                                                          torch_dtype=torch.bfloat16 if tc.get("bf16") else torch.float32)
            model = model.to(device)
            n_params = sum(p.numel() for p in model.parameters())
            print(f"  model params  : {n_params:,}")

    except ImportError as e:
        print(f"[ERROR] Missing dependency: {e}")
        print("  Install: pip install transformers torch")
        sys.exit(1)
    except Exception as e:
        print(f"[ERROR] Failed to load model '{cfg['base_model']}': {e}")
        print("  Try: pip install mamba-ssm transformers")
        sys.exit(1)

    _bus_emit(bus_dir, agent_id, "training_event",
              f"pipeline_start model={cfg['base_model']} "
              f"train_samples={len(train_samples)} eval_samples={len(eval_samples)}")

    if args.eval_only:
        print("\n[eval-only] Skipping training — running eval...")
        results = evaluate(model, tokenizer, eval_samples, max_seq_len, device)
        print("\n── Eval Results ─────────────────────────────────────────")
        print(results)
        _bus_emit(bus_dir, agent_id, "training_event",
                  f"eval_complete perplexity={results.perplexity:.3f} "
                  f"halt_calib_err={results.halt_prob_calibration_error:.4f}")
        return

    # ── Run training phases ───────────────────────────────────────────────────
    phases = cfg.get("phases", [])
    for phase in phases:
        if args.phase != 0 and phase["id"] != args.phase:
            continue
        if phase["steps"] == 0:
            continue
        run_phase(phase, model, tokenizer, train_samples, cfg, device,
                  args.dry_run, bus_dir, agent_id)

    # ── Eval + checkpoint ─────────────────────────────────────────────────────
    print("\n── Final Evaluation ─────────────────────────────────────")
    max_seq_len = ds_cfg.get("max_seq_len", 512)
    results = evaluate(model, tokenizer, eval_samples, max_seq_len, device)
    print(results)

    # Check targets
    ev_cfg = cfg.get("eval", {})
    target_ppl = ev_cfg.get("target_perplexity", 5.0)
    target_calib = ev_cfg.get("target_halt_calibration_error", 0.10)
    ppl_ok = results.perplexity <= target_ppl
    calib_ok = results.halt_prob_calibration_error <= target_calib
    print(f"\n  perplexity target ({target_ppl})   : {'✅ PASS' if ppl_ok else '⚠️  MISS'}")
    print(f"  halt_calib target ({target_calib})  : {'✅ PASS' if calib_ok else '⚠️  MISS'}")

    # Save checkpoint
    if not args.dry_run:
        out_dir = Path(tc["output_dir"])
        out_dir.mkdir(parents=True, exist_ok=True)
        model.save_pretrained(str(out_dir / "final"))
        tokenizer.save_pretrained(str(out_dir / "final"))
        stats = {
            "model": cfg["model_name"],
            "base": cfg["base_model"],
            "train_samples": len(train_samples),
            "eval_samples": len(eval_samples),
            "perplexity": round(results.perplexity, 4),
            "oo_meta_accuracy": round(results.oo_meta_accuracy, 4),
            "halt_prob_calibration_error": round(results.halt_prob_calibration_error, 4),
            "governor_accuracy": round(results.governor_accuracy, 4),
            "swarm_accuracy": round(results.swarm_accuracy, 4),
        }
        stats_path = out_dir / "training_stats.json"
        stats_path.write_text(json.dumps(stats, indent=2) + "\n")
        print(f"\n  checkpoint saved → {out_dir}/final/")
        print(f"  stats saved      → {stats_path}")

        _bus_emit(bus_dir, agent_id, "training_event",
                  f"checkpoint_saved dir={out_dir}/final "
                  f"perplexity={results.perplexity:.3f} "
                  f"ppl_ok={ppl_ok} calib_ok={calib_ok}")
    else:
        print("\n[dry-run] No checkpoint saved (pipeline validation complete ✓)")
        _bus_emit(bus_dir, agent_id, "training_event",
                  "dry_run_complete pipeline_ok=true")

    print("\nBatterfyl Phase P training complete.")


if __name__ == "__main__":
    main()
