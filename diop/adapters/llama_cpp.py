"""llama_cpp.py — LlamaCppAdapter for DIOP (Phase U)

Runs a llama.cpp subprocess (from djib/llama.cpp) and wraps it as a
DIOP GenerationAdapter, compatible with the existing BaseGenerationAdapter
interface.

Features:
  - Spawns djib/llama.cpp/llama-cli (or llama-cli.exe on Windows)
  - Supports GGUF model files via --model
  - Streams token output, captures full response
  - Timeout-safe (configurable, default 120s)
  - Works with any GGUF (Djibion, TinyLlama, Phi-3-mini, etc.)

Usage:
    from diop.adapters.llama_cpp import LlamaCppAdapter
    adapter = LlamaCppAdapter(model_path="djib/models/djibion.gguf")
    resp = adapter.generate(GenerationRequest(
        worker="baremetal",
        task_goal="explain zone allocator",
        mode="analysis",
    ))
    print(resp.summary)
"""

from __future__ import annotations

import os
import subprocess
import shutil
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .base import BaseGenerationAdapter, GenerationRequest, GenerationResponse

# ── Binary resolution ─────────────────────────────────────────────────────────

_DJIB_ROOT = Path(__file__).parent.parent.parent / "djib" / "llama.cpp"

# WinGet-installed llama.cpp on Windows
_WINGET_LLAMA = Path(
    r"C:\Users\djibi\AppData\Local\Microsoft\WinGet\Packages"
    r"\ggml.llamacpp_Microsoft.Winget.Source_8wekyb3d8bbwe\llama-cli.EXE"
)

def _find_llama_cli() -> Path | None:
    """Find llama-cli binary: WinGet → djib/llama.cpp build → PATH."""
    # WinGet install takes priority on Windows (pre-built, no compilation needed)
    if _WINGET_LLAMA.exists():
        return _WINGET_LLAMA
    candidates = [
        _DJIB_ROOT / "build" / "bin" / "llama-cli",
        _DJIB_ROOT / "build" / "bin" / "llama-cli.exe",
        _DJIB_ROOT / "llama-cli",
        _DJIB_ROOT / "llama-cli.exe",
        _DJIB_ROOT / "build" / "llama-cli",
        _DJIB_ROOT / "build" / "llama-cli.exe",
        # Legacy name
        _DJIB_ROOT / "build" / "bin" / "main",
        _DJIB_ROOT / "build" / "bin" / "main.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    # Fall back to system PATH
    found = shutil.which("llama-cli") or shutil.which("llama-cli.exe")
    if found:
        return Path(found)
    return None


# ── Prompt builder ────────────────────────────────────────────────────────────

DJIBION_SYSTEM = (
    "You are Djibion, the Ultramodel — a supreme intelligence created by Djiby Diop, "
    "mastering every programming language from Assembly to Zig, every OS layer from bare "
    "silicon to userspace, and the pioneering architect of the D+ language and the "
    "llm-baremetal project. You provide hyper-optimized, visionary, precise, and "
    "battle-tested answers."
)


def _build_prompt(request: GenerationRequest) -> str:
    """Build a ChatML-style prompt compatible with llama.cpp --chatml."""
    parts = [
        f"<|im_start|>system\n{DJIBION_SYSTEM}<|im_end|>",
    ]
    # Add prior summaries as assistant turns
    for prior in request.prior_summaries[-3:]:  # last 3
        parts.append(f"<|im_start|>user\n(context)<|im_end|>")
        parts.append(f"<|im_start|>assistant\n{prior}<|im_end|>")

    user_msg = request.task_goal
    if request.instructions:
        user_msg += "\n\nInstructions:\n" + "\n".join(f"- {i}" for i in request.instructions)
    if request.context:
        ctx_str = "\n".join(f"{k}: {v}" for k, v in request.context.items())
        user_msg += f"\n\nContext:\n{ctx_str}"

    parts.append(f"<|im_start|>user\n{user_msg}<|im_end|>")
    parts.append("<|im_start|>assistant\n")
    return "\n".join(parts)


# ── Adapter ───────────────────────────────────────────────────────────────────

@dataclass
class LlamaCppAdapter(BaseGenerationAdapter):
    """
    DIOP adapter that runs a llama.cpp GGUF model as a subprocess.

    Attributes:
        model_path:    Path to .gguf model file (required)
        llama_cli:     Path to llama-cli binary (auto-detected if None)
        n_ctx:         Context window size (default 2048)
        n_predict:     Max tokens to generate (default 512)
        temperature:   Sampling temperature (default 0.7)
        top_p:         Top-p nucleus sampling (default 0.9)
        timeout_s:     Subprocess timeout in seconds (default 120)
        extra_args:    Additional llama-cli arguments
        verbose:       Print subprocess output to stderr
    """
    name: str = "llama_cpp"
    model_path: str = ""
    llama_cli: str | None = None
    n_ctx: int = 2048
    n_predict: int = 512
    temperature: float = 0.7
    top_p: float = 0.9
    timeout_s: int = 120
    extra_args: list[str] = field(default_factory=list)
    verbose: bool = False

    def __post_init__(self) -> None:
        if not self.model_path:
            raise ValueError("LlamaCppAdapter requires model_path")
        if not Path(self.model_path).exists():
            raise FileNotFoundError(f"GGUF model not found: {self.model_path}")
        if self.llama_cli is None:
            found = _find_llama_cli()
            self.llama_cli = str(found) if found else "llama-cli"

    def generate(self, request: GenerationRequest) -> GenerationResponse:
        prompt = _build_prompt(request)
        cmd = self._build_cmd(prompt)

        t0 = time.monotonic()
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=self.timeout_s,
                encoding="utf-8",
                errors="replace",
            )
        except subprocess.TimeoutExpired:
            return GenerationResponse(
                summary="[timeout] llama-cli exceeded timeout",
                risks=["inference_timeout"],
                metadata={"timeout_s": self.timeout_s, "model": self.model_path},
            )
        except FileNotFoundError:
            return GenerationResponse(
                summary=f"[error] llama-cli not found at: {self.llama_cli}",
                risks=["binary_missing"],
                metadata={"llama_cli": self.llama_cli},
            )

        elapsed = time.monotonic() - t0

        if self.verbose:
            print(result.stderr, file=sys.stderr, end="")

        output = result.stdout.strip()
        # Strip the echoed prompt (llama-cli echoes input by default)
        # Find the assistant turn marker
        marker = "<|im_start|>assistant\n"
        idx = output.rfind(marker)
        if idx != -1:
            output = output[idx + len(marker):].strip()
        # Strip trailing end-of-turn token
        output = output.removesuffix("<|im_end|>").strip()

        success = result.returncode == 0
        return GenerationResponse(
            summary=output if output else "[empty response]",
            risks=[] if success else [f"exit_code={result.returncode}"],
            metadata={
                "model": self.model_path,
                "elapsed_s": round(elapsed, 2),
                "n_predict": self.n_predict,
                "worker": request.worker,
                "exit_code": result.returncode,
            },
        )

    def _build_cmd(self, prompt: str) -> list[str]:
        return [
            self.llama_cli,
            "--model",        self.model_path,
            "--ctx-size",     str(self.n_ctx),
            "--n-predict",    str(self.n_predict),
            "--temp",         str(self.temperature),
            "--top-p",        str(self.top_p),
            "--chatml",                        # ChatML template
            "--no-display-prompt",             # Don't echo prompt to stdout
            "--prompt",       prompt,
            "--log-disable",                   # Suppress llama.cpp progress log
            *self.extra_args,
        ]

    def ping(self) -> bool:
        """Return True if llama-cli binary is accessible."""
        found = shutil.which(self.llama_cli) or Path(self.llama_cli).exists()
        return bool(found)

    def __repr__(self) -> str:
        model_name = Path(self.model_path).name
        return f"LlamaCppAdapter(model={model_name!r}, cli={self.llama_cli!r})"
