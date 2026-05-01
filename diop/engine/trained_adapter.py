from __future__ import annotations

"""
DIOP Native Model — Trained Model Inference Adapter (v3)
=========================================================
Inference path priority:
  A': LlamaCpp GGUF  — fastest path when a .gguf exists + llama-cli available
  A:  PyTorch .pt    — local model checkpoint
  B:  C FFI .bin     — bare-metal binary bridge
  C:  Rule-based     — always available fallback

No external runtime, no HTTP, no network.
"""

import json
import shutil
import struct
from pathlib import Path

from ..adapters.base import BaseGenerationAdapter, GenerationRequest, GenerationResponse

_MODEL_DIR  = Path(__file__).resolve().parent / "model"
_DIOP_ROOT  = Path(__file__).resolve().parent.parent          # diop/
_DJIB_ROOT  = _DIOP_ROOT.parent / "djib"                      # djib/

# WinGet llama-cli (Windows) — mirrors diop/adapters/llama_cpp.py
_WINGET_LLAMA = Path(
    r"C:\Users\djibi\AppData\Local\Microsoft\WinGet\Packages"
    r"\ggml.llamacpp_Microsoft.Winget.Source_8wekyb3d8bbwe\llama-cli.EXE"
)

try:
    import torch
    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False


def _find_gguf_for_model(model_name: str) -> Path | None:
    """
    Locate a .gguf file for the given model name.
    Search order:
      1. diop/models/<model_name>.gguf
      2. djib/models/<model_name>.gguf
      3. djib/models/djibion.gguf  (universal fallback)
      4. djib/models/*.gguf        (first found)
    """
    search_dirs = [
        _DIOP_ROOT / "models",
        _DJIB_ROOT / "models",
    ]
    # Exact name match first
    for d in search_dirs:
        p = d / f"{model_name}.gguf"
        if p.exists():
            return p
    # Djibion universal fallback
    djibion = _DJIB_ROOT / "models" / "djibion.gguf"
    if djibion.exists():
        return djibion
    # Any .gguf in djib/models/
    djib_models = _DJIB_ROOT / "models"
    if djib_models.is_dir():
        for p in djib_models.glob("*.gguf"):
            return p
    return None


def _llama_cli_available() -> str | None:
    """Return path to llama-cli binary, or None if not found."""
    if _WINGET_LLAMA.exists():
        return str(_WINGET_LLAMA)
    found = shutil.which("llama-cli") or shutil.which("llama-cli.exe")
    return found


class TrainedModelAdapter(BaseGenerationAdapter):
    """
    Adapter that uses the locally trained DIOP model.
    Priority: PyTorch .pt → C FFI .bin → Rule-based fallback.
    """
    def __init__(self, model_name: str = "diop_model"):
        self.model_name = model_name

    @property
    def name(self) -> str:
        return f"trained:{self.model_name}"

    def generate(self, request: GenerationRequest) -> GenerationResponse:
        pt_path  = _MODEL_DIR / f"{self.model_name}.pt"
        cfg_path = _MODEL_DIR / f"{self.model_name}_config.json"
        
        # Fallback to default config if specialized one missing
        if not cfg_path.exists():
            cfg_path = _MODEL_DIR / "model_config.json"

        # --- Path A': LlamaCpp GGUF inference (fastest, no Python deps) ---
        gguf_path = _find_gguf_for_model(self.model_name)
        llama_bin = _llama_cli_available()
        if gguf_path and llama_bin:
            try:
                return self._infer_llama_cpp(request, gguf_path, llama_bin)
            except Exception as e:
                print(f"[Trained:{self.model_name}] LlamaCpp failed: {e} — trying PyTorch")

        # --- Path A: PyTorch inference ---
        if HAS_TORCH and pt_path.exists() and cfg_path.exists():
            return self._infer_torch(request, pt_path, cfg_path)

        # --- Path B: C FFI bridge ---
        bin_path = _MODEL_DIR / f"{self.model_name}.bin"
        if bin_path.exists():
            try:
                return self._infer_ffi(request, bin_path)
            except Exception as e:
                print(f"[Trained:{self.model_name}] C FFI failed: {e} — using rule fallback.")

        # --- Path C: Rule-based fallback ---
        print(
            f"\n[Trained] No checkpoint found in {_MODEL_DIR}.\n"
            "  -> Run 'python -m diop train' to build the native model.\n"
            "  -> Using rule-based fallback."
        )
        return self._rule_fallback(request)

    # ------------------------------------------------------------------
    # Path A' — LlamaCpp GGUF inference (fastest path)
    # ------------------------------------------------------------------

    def _infer_llama_cpp(
        self, request: GenerationRequest, gguf_path: Path, llama_bin: str
    ) -> GenerationResponse:
        import subprocess, time
        from ..adapters.llama_cpp import _build_prompt as _llama_build_prompt

        print(f"\n[DIOP Native] LlamaCpp inference: {gguf_path.name} via {Path(llama_bin).name}")
        prompt = _llama_build_prompt(request)
        cmd = [
            llama_bin,
            "--model",            str(gguf_path),
            "--ctx-size",         "2048",
            "--n-predict",        "512",
            "--temp",             "0.7",
            "--top-p",            "0.9",
            "--chatml",
            "--no-display-prompt",
            "--prompt",           prompt,
            "--log-disable",
        ]
        t0 = time.monotonic()
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=120,
            encoding="utf-8", errors="replace",
        )
        elapsed = time.monotonic() - t0

        output = result.stdout.strip()
        marker = "<|im_start|>assistant\n"
        idx = output.rfind(marker)
        if idx != -1:
            output = output[idx + len(marker):].strip()
        output = output.removesuffix("<|im_end|>").strip()

        content = self._parse_output(output)
        return GenerationResponse(
            summary=content.get("summary", output[:200]) if output else "[empty response]",
            artifacts=content.get("artifacts", []),
            risks=content.get("risks", []) + ([] if result.returncode == 0 else [f"exit_code={result.returncode}"]),
            recommendations=content.get("recommendations", []),
            metadata={
                "provider":  "diop-llama-cpp",
                "model":     str(gguf_path),
                "elapsed_s": round(elapsed, 2),
                "exit_code": result.returncode,
            },
        )

    # ------------------------------------------------------------------
    # Path A — PyTorch direct inference
    # ------------------------------------------------------------------

    def _infer_torch(
        self, request: GenerationRequest, pt_path: Path, cfg_path: Path
    ) -> GenerationResponse:
        from .model.trainer import _build_torch_model
        from .model.config import DiopModelConfig
        from .model.tokenizer import DiopTokenizer

        print(f"\n[DIOP Native] Loading PyTorch checkpoint ({pt_path.name})...")
        cfg   = DiopModelConfig.load(cfg_path)
        tok   = DiopTokenizer()
        cfg.vocab_size = tok.vocab_size

        model = _build_torch_model(cfg)
        model.load_state_dict(torch.load(str(pt_path), map_location="cpu", weights_only=True))
        model.eval()

        prompt = self._build_prompt(request)
        # Use more context, but leave enough room for generation
        ids    = tok.encode(prompt)[: cfg.seq_len - 512]
        generated = self._generate_tokens(model, ids, cfg, max_new=512, tok=tok)

        print(f"[DIOP Native] Generated {len(generated)} tokens.")
        # Try to find JSON in the output, else wrap as text
        content = self._parse_output(generated)
        return GenerationResponse(
            summary=content.get("summary", generated[:120]),
            artifacts=content.get("artifacts", [
                {"name": f"{request.worker}_output.txt", "type": "text", "content": generated}
            ]),
            risks=content.get("risks", []),
            recommendations=content.get("recommendations", []),
            metadata={"provider": "diop-native-pytorch"},
        )

    def _generate_tokens(self, model, prompt_ids: list[int], cfg, max_new: int, tok) -> str:
        """Sampling-based generation to avoid loops."""
        ids = list(prompt_ids)
        temperature = 0.7  # Control creativity
        with torch.no_grad():
            for _ in range(max_new):
                context = ids[-cfg.seq_len:]
                inp     = torch.tensor([context], dtype=torch.long)
                logits  = model(inp)[:, -1, :] / temperature
                probs   = torch.softmax(logits, dim=-1)
                next_id = int(torch.multinomial(probs, num_samples=1))
                
                ids.append(next_id)
                if next_id == 0:              # EOS
                    break
        # Decode only the new tokens
        return tok.decode(ids[len(prompt_ids):])

    # ------------------------------------------------------------------
    # Path B — C FFI bridge
    # ------------------------------------------------------------------

    def _infer_ffi(self, request: GenerationRequest, bin_path: Path) -> GenerationResponse:
        # Check magic to distinguish DIOP stat index from llama2.c binary
        with bin_path.open("rb") as f:
            magic = struct.unpack("<I", f.read(4))[0]

        if magic == 0x444F4950:  # 'DIOP' stat index
            return self._infer_stat_index(request, bin_path)
        else:
            from .bridge import NativeEngineBridge
            print("[DIOP Native] Loading via C FFI bridge...")
            engine = NativeEngineBridge()
            engine.load_model(str(bin_path))
            raw    = engine.generate(self._build_prompt(request), max_tokens=512)
            content = self._parse_output(raw)
            return GenerationResponse(
                summary=content.get("summary", ""),
                artifacts=content.get("artifacts", []),
                risks=content.get("risks", []),
                recommendations=content.get("recommendations", []),
                metadata={"provider": "diop-native-c-ffi"},
            )

    def _infer_stat_index(self, request: GenerationRequest, bin_path: Path) -> GenerationResponse:
        """Read DIOP binary rule index and find closest match."""
        prompt = self._build_prompt(request)
        rules  = self._read_stat_bin(bin_path)
        if not rules:
            return self._rule_fallback(request)

        # Simple keyword overlap scoring
        q_words = set(prompt.lower().split())
        best_score, best_completion = 0, rules[0][1]
        for p, c in rules:
            score = len(q_words & set(p.lower().split()))
            if score > best_score:
                best_score, best_completion = score, c

        content = self._parse_output(best_completion)
        return GenerationResponse(
            summary=content.get("summary", best_completion[:100]),
            artifacts=content.get("artifacts", []),
            risks=content.get("risks", []),
            recommendations=content.get("recommendations", []),
            metadata={"provider": "diop-native-stat-index"},
        )

    @staticmethod
    def _read_stat_bin(path: Path) -> list[tuple[str, str]]:
        rules = []
        with path.open("rb") as f:
            f.read(4)  # skip magic
            n = struct.unpack("<I", f.read(4))[0]
            for _ in range(n):
                pl = struct.unpack("<I", f.read(4))[0]
                p  = f.read(pl).decode("utf-8", errors="replace")
                cl = struct.unpack("<I", f.read(4))[0]
                c  = f.read(cl).decode("utf-8", errors="replace")
                rules.append((p, c))
        return rules

    # ------------------------------------------------------------------
    # Shared helpers
    # ------------------------------------------------------------------

    def _build_prompt(self, request: GenerationRequest) -> str:
        # Default header (Core)
        header = (
            "You are DIOP-Core, an expert bare-metal engineer specialized in "
            "C, Rust, and Zig. You write zero-copy, malloc-free code for UEFI "
            "and embedded systems. Your output is always structured JSON."
        )
        
        if "warden" in self.model_name.lower():
            header = (
                "You are DIOP-Warden, a security audit agent for bare-metal systems. "
                "You analyze C code for memory violations, unsafe opcodes, and logic flaws. "
                "You output a structured JSON verdict: ALLOW, REJECT, or QUARANTINE."
            )
        elif "architect" in self.model_name.lower():
            header = (
                "You are DIOP-Architect, a systems architecture expert. "
                "You design module interactions and persistence flows for the oo-system. "
                "You output structured JSON describing architecture rules and module links."
            )
        
        # Must MATCH exactly the TrainingDataExporter pattern
        return (
            f"{header}\n\n"
            f"[TASK] {request.task_goal}"
        )

    @staticmethod
    def _parse_output(text: str) -> dict:
        """Try to extract JSON from generated text."""
        # Find first '{' ... last '}'
        start = text.find("{")
        end   = text.rfind("}")
        if start != -1 and end != -1 and end > start:
            try:
                return json.loads(text[start:end + 1])
            except json.JSONDecodeError:
                pass
        return {"summary": text[:200], "artifacts": [], "risks": [], "recommendations": []}

    def _rule_fallback(self, request: GenerationRequest) -> GenerationResponse:
        return GenerationResponse(
            summary=f"[Rule-based] {request.task_goal}",
            artifacts=[{
                "name": f"{request.worker}_heuristic.md",
                "type": "text",
                "content": (
                    f"# {request.worker.capitalize()} — Rule-based Output\n\n"
                    f"**Goal:** {request.task_goal}\n\n"
                    "**Status:** Native model not yet trained or not accessible.\n"
                    "Run `python -m diop train` to activate the native AI core.\n\n"
                    "**Baseline Rules:**\n"
                    "- No malloc() in critical paths\n"
                    "- Zero-copy memory model\n"
                    "- UEFI bare-metal compatibility required\n"
                )
            }],
            risks=["Native model checkpoint missing or incompatible"],
            recommendations=["Run: python -m diop train --profile micro"],
            metadata={"provider": "diop-rule-based-fallback"},
        )
