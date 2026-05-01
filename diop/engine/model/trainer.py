from __future__ import annotations

"""
DIOP Native Model — Training Pipeline (v2)
==========================================
Two-backend strategy:
  A) PyTorch (if installed): real autograd backprop -> llama2.c .bin checkpoint
  B) Statistical fallback (pure numpy): TF-IDF rule index -> compact .bin

Usage:
    python -m diop train --profile micro
"""

import json
import math
import random
import struct
from pathlib import Path
from typing import Optional

from .config import DiopModelConfig, PROFILES
from .tokenizer import DiopTokenizer
from .data_exporter import TrainingDataExporter

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

try:
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False


# ---------------------------------------------------------------------------
# Backend A — PyTorch MiniLM
# ---------------------------------------------------------------------------

def _build_torch_model(cfg: DiopModelConfig) -> "nn.Module":
    class CausalTransformer(nn.Module):
        def __init__(self):
            super().__init__()
            self.tok_emb = nn.Embedding(cfg.vocab_size, cfg.dim)
            # Use learned position embeddings (standard for mini models)
            self.pos_emb = nn.Embedding(cfg.seq_len, cfg.dim)
            
            # Proper Decoder Blocks
            # Note: We use nn.ModuleList to be compatible with the export function
            self.layers = nn.ModuleList([
                nn.TransformerEncoderLayer(
                    d_model=cfg.dim, 
                    nhead=cfg.n_heads,
                    dim_feedforward=cfg.hidden_dim,
                    dropout=0.1, 
                    batch_first=True, 
                    norm_first=True,
                    activation="gelu" # More stable than ReLU for small models
                ) for _ in range(cfg.n_layers)
            ])
            
            self.norm    = nn.LayerNorm(cfg.dim)
            self.lm_head = nn.Linear(cfg.dim, cfg.vocab_size, bias=False)
            # Weight sharing (standard for LLMs)
            self.lm_head.weight = self.tok_emb.weight

        def forward(self, idx):
            B, T = idx.size()
            device = idx.device
            
            # Position encoding
            pos = torch.arange(0, T, dtype=torch.long, device=device).unsqueeze(0)
            x = self.tok_emb(idx) + self.pos_emb(pos)
            
            # Causal mask: ensures token at i can only see tokens 0..i
            mask = torch.triu(torch.full((T, T), float("-inf"), device=device), diagonal=1)
            
            for layer in self.layers:
                # In PyTorch's TransformerEncoderLayer, we pass the mask to forward
                x = layer(x, src_mask=mask, is_causal=True)
                
            return self.lm_head(self.norm(x))
            
    return CausalTransformer()


# ---------------------------------------------------------------------------
# Backend B — Statistical Rule Index
# ---------------------------------------------------------------------------

class StatisticalRuleIndex:
    def __init__(self):
        self.prompts:     list[str] = []
        self.completions: list[str] = []
        self.tfidf: Optional["np.ndarray"] = None
        self.vocab: dict[str, int] = {}

    def fit(self, samples: list[dict]) -> None:
        self.prompts     = [s["prompt"]     for s in samples]
        self.completions = [s["completion"] for s in samples]
        if not HAS_NUMPY or not self.prompts:
            return
        tokenized = [p.lower().split() for p in self.prompts]
        freq: dict[str, int] = {}
        for toks in tokenized:
            for t in toks:
                freq[t] = freq.get(t, 0) + 1
        self.vocab = {w: i for i, (w, _) in enumerate(sorted(freq.items(), key=lambda x: -x[1])[:2000])}
        V, N = len(self.vocab), len(self.prompts)
        tf = np.zeros((N, V), dtype=np.float32)
        for i, toks in enumerate(tokenized):
            for t in toks:
                if t in self.vocab:
                    tf[i, self.vocab[t]] += 1
            s = tf[i].sum()
            if s > 0:
                tf[i] /= s
        df  = (tf > 0).sum(axis=0).astype(np.float32)
        idf = np.log((N + 1) / (df + 1)) + 1.0
        self.tfidf = tf * idf[None, :]

    def export_bin(self, path: Path) -> None:
        with path.open("wb") as f:
            f.write(struct.pack("<I", 0x444F4950))  # magic 'DIOP'
            f.write(struct.pack("<I", len(self.prompts)))
            for p, c in zip(self.prompts, self.completions):
                pb, cb = p.encode("utf-8"), c.encode("utf-8")
                f.write(struct.pack("<I", len(pb))); f.write(pb)
                f.write(struct.pack("<I", len(cb))); f.write(cb)
        print(f"[StatIndex] {len(self.prompts)} rules -> {path} ({path.stat().st_size/1024:.1f} KB)")


# ---------------------------------------------------------------------------
# Unified DiopModelTrainer
# ---------------------------------------------------------------------------

class DiopModelTrainer:
    def __init__(
        self,
        memory_root: Path,
        output_dir: Path,
        profile: str = "micro",
        tokenizer_path: Optional[Path] = None,
        dataset_file: Optional[str] = None,
        output_name: str = "diop_model"
    ) -> None:
        self.memory_root = memory_root
        self.output_dir  = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.cfg         = PROFILES.get(profile, PROFILES["micro"])
        self.tokenizer   = DiopTokenizer(tokenizer_path)
        self.cfg.vocab_size = self.tokenizer.vocab_size
        self.dataset_file = dataset_file
        self.output_name = output_name

    def run(self) -> Path:
        backend = "torch" if HAS_TORCH else ("numpy-stat" if HAS_NUMPY else "none")
        print("\n" + "=" * 60)
        print(f"  DIOP Native Model Training — {self.output_name}")
        print("=" * 60)
        print(f"  Backend   : {backend}")
        print(f"  Profile   : {self.cfg.dim}d / {self.cfg.n_layers}L")
        print(f"  ~Params   : {self.cfg.n_params/1e6:.1f}M")
        self.cfg.save(self.output_dir / f"{self.output_name}_config.json")

        # Step 1: Data
        if self.dataset_file:
            print(f"\n[1/3] Using specialized dataset: {self.dataset_file}")
            data_path = self.memory_root / self.dataset_file
        else:
            print("\n[1/3] Exporting training data from memory...")
            data_path = self.output_dir / "train.jsonl"
            n = TrainingDataExporter(self.memory_root, data_path).export()
            if n == 0:
                n = self._write_seed_data(data_path)

        samples = self._load_samples(data_path)
        print(f"      {len(samples)} samples ready.")

        # Step 2: Train
        print(f"\n[2/3] Training ({backend} backend)...")
        if HAS_TORCH:
            ckpt = self._train_torch(samples)
        else:
            ckpt = self._train_statistical(samples)

        mb = ckpt.stat().st_size / 1024 / 1024
        print(f"\n[Done] {ckpt} ({mb:.2f} MB)")
        print(f"  Run: python -m diop run \"goal\" --adapter {self.output_name}")
        print("=" * 60)
        return ckpt

    # ---- PyTorch ----

    def _train_torch(self, samples: list[dict]) -> Path:
        device = "cuda" if torch.cuda.is_available() else "cpu"
        print(f"      Device: {device}")
        model = _build_torch_model(self.cfg).to(device)
        opt   = torch.optim.AdamW(model.parameters(), lr=self.cfg.learning_rate, weight_decay=0.01)
        seqs  = self._tokenize_all(samples)
        if not seqs:
            print("      [!] No sequences to train on.")
            return self._export_torch_bin(model)

        log_every = max(1, self.cfg.max_iters // 10)
        best = float("inf")
        model.train()
        for step in range(self.cfg.max_iters):
            # LR schedule
            if step < self.cfg.warmup_iters:
                lr = self.cfg.learning_rate * step / max(1, self.cfg.warmup_iters)
            else:
                p = (step - self.cfg.warmup_iters) / max(1, self.cfg.max_iters - self.cfg.warmup_iters)
                lr = self.cfg.learning_rate * 0.5 * (1 + math.cos(math.pi * p))
            for pg in opt.param_groups:
                pg["lr"] = lr

            seq    = random.choice(seqs)
            ids    = torch.tensor([seq], dtype=torch.long, device=device)
            logits = model(ids[:, :-1])
            loss   = F.cross_entropy(logits.reshape(-1, self.cfg.vocab_size), ids[:, 1:].reshape(-1))
            opt.zero_grad(); loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), self.cfg.grad_clip)
            opt.step()
            v = loss.item()
            if v < best:
                best = v
            if (step + 1) % log_every == 0:
                print(f"      step {step+1}/{self.cfg.max_iters} | loss {v:.4f} | lr {lr:.2e}")

        print(f"      Best loss: {best:.4f}")
        # Save PyTorch state dict
        pt_path = self.output_dir / f"{self.output_name}.pt"
        torch.save(model.state_dict(), str(pt_path))
        print(f"      PyTorch checkpoint: {pt_path}")
        return self._export_torch_bin(model)

    def _export_torch_bin(self, model: "nn.Module") -> Path:
        path = self.output_dir / f"{self.output_name}.bin"
        with path.open("wb") as f:
            f.write(self.cfg.to_llama2c_header())
            def w(t): f.write(t.detach().cpu().float().numpy().tobytes())
            w(model.tok_emb.weight)
            w(model.pos_emb.weight)
            # Iterate over our custom ModuleList
            for layer in model.layers:
                w(layer.norm1.weight)
            for layer in model.layers:
                w(layer.self_attn.in_proj_weight[:self.cfg.dim])
            for layer in model.layers:
                w(layer.self_attn.in_proj_weight[self.cfg.dim:2*self.cfg.dim])
            for layer in model.layers:
                w(layer.self_attn.in_proj_weight[2*self.cfg.dim:])
            for layer in model.layers:
                w(layer.self_attn.out_proj.weight)
            for layer in model.layers:
                w(layer.norm2.weight)
            for layer in model.layers:
                # TransformerEncoderLayer uses Linear layers for FFN
                w(layer.linear1.weight.T)
            for layer in model.layers:
                w(layer.linear2.weight.T)
            w(model.norm.weight)
        return path

    # ---- Statistical ----

    def _train_statistical(self, samples: list[dict]) -> Path:
        print("      (Install torch for neural training: pip install torch)")
        idx = StatisticalRuleIndex()
        idx.fit(samples)
        path = self.output_dir / "diop_model.bin"
        idx.export_bin(path)
        return path

    # ---- Helpers ----

    def _load_samples(self, path: Path) -> list[dict]:
        out = []
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    try: out.append(json.loads(line))
                    except Exception: pass
        return out

    def _tokenize_all(self, samples: list[dict]) -> list[list[int]]:
        seqs = []
        for s in samples:
            prompt = s.get("prompt", "")
            completion = s.get("completion", "")
            
            # --- DATA FILTERING ---
            # 1. Skip obvious hallucinations or token loops
            if "tictint" in completion or "rororo" in completion:
                continue
            
            # 2. Skip "Unknown task" if it has no real content
            if "Unknown task" in prompt:
                try:
                    data = json.loads(completion)
                    if not data.get("artifacts") and not data.get("risks"):
                        continue
                except:
                    pass

            ids = self.tokenizer.encode(prompt + completion)[:self.cfg.seq_len]
            if len(ids) >= 4: # Need at least a few tokens to learn anything
                seqs.append(ids)
        
        print(f"      Filtered dataset: {len(seqs)} valid sequences (original: {len(samples)})")
        return seqs

    def _write_seed_data(self, path: Path) -> int:
        seed = [
            {"prompt": "You are DIOP-Core. Task: Write a static memory arena in C. No malloc.",
             "completion": json.dumps({"summary":"Static arena","artifacts":[{"name":"arena.c","type":"code","content":"#define A (64<<20)\nstatic char _a[A];\nstatic int _p=0;\nvoid* alloc(int n){if(_p+n>A)return 0;void* r=_a+_p;_p+=n;return r;}"}],"risks":[],"recommendations":[]})},
            {"prompt": "You are DIOP-Core. Task: Validate GGUF header in C. Bare-metal.",
             "completion": json.dumps({"summary":"GGUF validator","artifacts":[{"name":"gguf.c","type":"code","content":"int gguf_ok(void*b,int l){return l>=16&&*(unsigned*)b==0x46554747?0:-1;}"}],"risks":["little-endian only"],"recommendations":[]})},
            {"prompt": "You are DIOP-Core. Task: Static KV cache for transformer inference in C.",
             "completion": json.dumps({"summary":"KV cache","artifacts":[{"name":"kvcache.c","type":"code","content":"#define S 512\n#define D 256\nstatic float K[S][D],V[S][D];\nvoid kv_put(int i,float*k,float*v){for(int j=0;j<D;j++){K[i][j]=k[j];V[i][j]=v[j];}}"}],"risks":[],"recommendations":[]})},
        ]
        with path.open("w", encoding="utf-8") as f:
            for s in seed:
                f.write(json.dumps(s, ensure_ascii=False) + "\n")
        print(f"      Wrote {len(seed)} seed samples.")
        return len(seed)
