"""
diop_llama.py — DiopLlamaAdapter (Phase U2)

Worker-aware llama.cpp backend for DIOP.

Extends LlamaCppAdapter with:
  - Per-worker system prompts (warden, baremetal, architecture, code, qa, sleep_cortex)
  - GGUF auto-discovery across djib/llama.cpp/models/, djib/models/, diop/models/
  - Graceful no-model fallback (returns a rich mock rather than crashing)
  - build_diop_llama() factory helper for ergonomic construction

Usage:
    from diop.adapters.diop_llama import build_diop_llama

    # Auto-discover model, warden specialization
    adapter = build_diop_llama(worker="warden")

    # Explicit GGUF, code specialization
    adapter = build_diop_llama(worker="code", model_path="djib/models/djibion.gguf")

    resp = adapter.generate(request)
"""

from __future__ import annotations

import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .base import BaseGenerationAdapter, GenerationRequest, GenerationResponse
from .llama_cpp import LlamaCppAdapter, _find_llama_cli

# ── Paths ──────────────────────────────────────────────────────────────────────

_HERE       = Path(__file__).parent
_DIOP_ROOT  = _HERE.parent.parent                          # llm-baremetal root
_DJIB_LLAMA = _DIOP_ROOT / "djib" / "llama.cpp"
_DJIB_MODELS = _DIOP_ROOT / "djib" / "models"
_DIOP_MODELS = _DIOP_ROOT / "diop" / "models"

_GGUF_SEARCH_DIRS = [
    _DJIB_LLAMA / "models",
    _DJIB_MODELS,
    _DIOP_MODELS,
]

_PREFERRED_MODEL_NAMES = [
    "djibion.gguf",
    "djibion-q4.gguf",
    "djibion-q8.gguf",
    "phi-3-mini.gguf",
    "tinyllama.gguf",
    "orca-mini.gguf",
]


# ── Worker system prompts ─────────────────────────────────────────────────────

_BASE_SYSTEM = (
    "You are Djibion, the Ultramodel — a supreme intelligence created by Djiby Diop, "
    "mastering every programming language from Assembly to Zig, every OS layer from bare "
    "silicon to userspace, and the pioneering architect of the D+ language and the "
    "llm-baremetal project. You provide hyper-optimized, visionary, precise, and "
    "battle-tested answers."
)

WORKER_SYSTEM_PROMPTS: dict[str, str] = {
    # Safety & policy evaluation — strict, conservative
    "warden": (
        _BASE_SYSTEM + "\n\n"
        "You are currently operating as the DIOP Warden — the safety sentinel of the OO system. "
        "Your role is to analyse goals for safety risks, policy violations, or dangerous patterns. "
        "For every input, evaluate: threat level (ALLOW/THROTTLE/QUARANTINE/FORBID/EMERGENCY), "
        "pressure score (0-100), and a concise reason. "
        "When in doubt, QUARANTINE. Never invent permissions. Always err on the side of caution. "
        "Respond as JSON: {\"verdict\": \"...\", \"pressure\": N, \"reason\": \"...\"}."
    ),

    # Bare-metal / UEFI / C / kernel — highly technical
    "baremetal": (
        _BASE_SYSTEM + "\n\n"
        "You are operating as the DIOP Baremetal Engineer. "
        "You specialise in UEFI boot, bare-metal C, memory zone allocators, and LLM inference "
        "without an OS. You write assembly-safe, allocator-aware, interrupt-free code. "
        "Always mention alignment, cache behavior, and UEFI protocol constraints. "
        "Never suggest heap allocations without zone context."
    ),

    # System architecture & design
    "architecture": (
        _BASE_SYSTEM + "\n\n"
        "You are operating as the DIOP Architect. "
        "You design clean, evolvable, research-grade OS-level systems. "
        "Your outputs include subsystem boundaries, interface contracts (C ↔ Rust), "
        "data-flow diagrams, and scalability analysis. "
        "Always consider: isolation, observability, fault containment, and future evolvability."
    ),

    # General code generation
    "code": (
        _BASE_SYSTEM + "\n\n"
        "You are operating as the DIOP Code Worker. "
        "You write precise, idiomatic, production-quality code in C, Rust, Python, or Assembly "
        "as required. Always include error handling, const-correctness, and inline comments "
        "for non-obvious logic. Prefer zero-copy, stack allocation, and compile-time checks."
    ),

    # Quality assurance / verification
    "qa": (
        _BASE_SYSTEM + "\n\n"
        "You are operating as the DIOP QA Worker. "
        "Your role is to verify, test, and validate system behaviour. "
        "You write unit tests, integration probes, and formal invariant checks. "
        "When reviewing code, identify: undefined behaviour, resource leaks, race conditions, "
        "and policy violations. Respond with structured findings and severity levels."
    ),

    # Sleep-phase consolidation — reflective, pattern-extracting
    "sleep_cortex": (
        _BASE_SYSTEM + "\n\n"
        "You are operating as the DIOP Sleep Cortex — the dream consolidation unit. "
        "You analyse past OO system experiences and distil them into reusable knowledge bricks. "
        "For each experience, extract: a concept name (brick_name), a generalised rule (cement_rule), "
        "and relevant tags. "
        "Respond ONLY as JSON: {\"brick_name\": \"...\", \"cement_rule\": \"...\", \"tags\": [...]}."
    ),

    # Governor / policy decision
    "governor": (
        _BASE_SYSTEM + "\n\n"
        "You are operating as the DIOP Governor — the sovereign orchestrator of the OO system. "
        "You evaluate system state (pressure, active goals, mode) and decide: "
        "which goals to pursue, which to defer, and whether to escalate to Safe/Emergency mode. "
        "Respond with structured directives: {\"goal\": \"...\", \"mode\": \"...\", \"rationale\": \"...\"}."
    ),
}

# Default = generic Djibion system
WORKER_SYSTEM_PROMPTS["default"] = _BASE_SYSTEM


# ── GGUF auto-discovery ────────────────────────────────────────────────────────

def discover_gguf() -> Path | None:
    """
    Search known directories for a GGUF model file.

    Priority:
      1. Preferred names (djibion.gguf first) in each search dir
      2. Any .gguf file in each search dir (alphabetical)
    """
    # Pass 1: preferred names
    for preferred in _PREFERRED_MODEL_NAMES:
        for d in _GGUF_SEARCH_DIRS:
            candidate = d / preferred
            if candidate.exists():
                return candidate

    # Pass 2: any .gguf
    for d in _GGUF_SEARCH_DIRS:
        if d.is_dir():
            gguf_files = sorted(d.glob("*.gguf"))
            if gguf_files:
                return gguf_files[0]

    return None


def list_available_ggufs() -> list[Path]:
    """Return all discovered GGUF files across search directories."""
    found: list[Path] = []
    seen: set[Path] = set()
    for d in _GGUF_SEARCH_DIRS:
        if d.is_dir():
            for g in sorted(d.glob("*.gguf")):
                if g not in seen:
                    found.append(g)
                    seen.add(g)
    return found


# ── No-model fallback adapter ─────────────────────────────────────────────────

@dataclass
class _NoModelAdapter(BaseGenerationAdapter):
    """Returned when no GGUF + llama-cli is available. Gives a rich diagnostic response."""
    name: str = "diop_llama_unavailable"
    worker: str = "default"

    def generate(self, request: GenerationRequest) -> GenerationResponse:
        ggufs   = list_available_ggufs()
        cli     = _find_llama_cli()
        missing = []
        if not cli:
            missing.append("llama-cli binary (install via WinGet: winget install ggml.llamacpp)")
        if not ggufs:
            missing.append(
                "GGUF model file (place in djib/models/ or djib/llama.cpp/models/; "
                "e.g., djibion.gguf)"
            )
        summary = (
            f"[diop_llama] Cannot infer — missing: {'; '.join(missing)}. "
            f"Task was: {request.task_goal[:120]}"
        )
        return GenerationResponse(
            summary=summary,
            risks=["llama_cpp_unavailable"],
            metadata={
                "worker":  self.worker,
                "missing": missing,
                "cli_found": str(cli) if cli else None,
                "ggufs_found": [str(g) for g in ggufs],
            },
        )


# ── DiopLlamaAdapter ──────────────────────────────────────────────────────────

@dataclass
class DiopLlamaAdapter(BaseGenerationAdapter):
    """
    Worker-aware llama.cpp backend for DIOP (Phase U2).

    Wraps LlamaCppAdapter with:
      - Per-worker system prompts (warden / baremetal / architecture / code / qa / sleep_cortex)
      - GGUF model auto-discovery
      - Transparent pass-through to LlamaCppAdapter.generate()

    Attributes:
        worker:       DIOP worker name — determines system prompt.
        model_path:   Path to .gguf (empty = auto-discover).
        n_ctx:        Context size (default 2048).
        n_predict:    Max tokens (default 512).
        temperature:  Sampling temperature (default 0.7).
        timeout_s:    Subprocess timeout (default 120s).
    """
    name: str = "diop_llama"
    worker: str = "default"
    model_path: str = ""
    n_ctx: int = 2048
    n_predict: int = 512
    temperature: float = 0.7
    timeout_s: int = 120
    extra_args: list[str] = field(default_factory=list)
    verbose: bool = False

    # Internal — set in __post_init__
    _inner: BaseGenerationAdapter = field(default=None, init=False, repr=False)

    def __post_init__(self) -> None:
        # Resolve model path
        resolved_model: Path | None = None
        if self.model_path:
            p = Path(self.model_path)
            if p.exists():
                resolved_model = p
            else:
                # Try relative to repo root
                candidate = _DIOP_ROOT / p
                if candidate.exists():
                    resolved_model = candidate
        else:
            resolved_model = discover_gguf()

        cli = _find_llama_cli()

        if resolved_model is None or cli is None:
            self._inner = _NoModelAdapter(worker=self.worker)
            return

        # Override system prompt based on worker
        system_prompt = WORKER_SYSTEM_PROMPTS.get(
            self.worker.lower(), WORKER_SYSTEM_PROMPTS["default"]
        )

        # Monkey-patch the system prompt into the inner adapter via extra_args
        # LlamaCppAdapter uses _build_prompt which embeds DJIBION_SYSTEM — we
        # subclass by wrapping and rewriting the prompt at generate() time.
        self._inner = LlamaCppAdapter(
            model_path=str(resolved_model),
            llama_cli=str(cli),
            n_ctx=self.n_ctx,
            n_predict=self.n_predict,
            temperature=self.temperature,
            timeout_s=self.timeout_s,
            extra_args=self.extra_args,
            verbose=self.verbose,
        )
        self._system_prompt = system_prompt

    def generate(self, request: GenerationRequest) -> GenerationResponse:
        if not isinstance(self._inner, LlamaCppAdapter):
            # No-model fallback
            return self._inner.generate(request)

        # Build prompt with worker-specific system message
        from .llama_cpp import _build_prompt as _generic_build
        import diop.adapters.llama_cpp as _llama_mod

        # Temporarily swap system constant, build prompt, restore
        _orig = _llama_mod.DJIBION_SYSTEM
        _llama_mod.DJIBION_SYSTEM = self._system_prompt
        try:
            resp = self._inner.generate(request)
        finally:
            _llama_mod.DJIBION_SYSTEM = _orig

        # Tag metadata with worker
        resp.metadata["worker_specialization"] = self.worker
        return resp

    def ping(self) -> bool:
        if isinstance(self._inner, LlamaCppAdapter):
            return self._inner.ping()
        return False

    @property
    def model_path_resolved(self) -> str | None:
        if isinstance(self._inner, LlamaCppAdapter):
            return self._inner.model_path
        return None

    def __repr__(self) -> str:
        model = Path(self._inner.model_path).name if isinstance(self._inner, LlamaCppAdapter) else "none"
        return f"DiopLlamaAdapter(worker={self.worker!r}, model={model!r})"


# ── Factory helper ────────────────────────────────────────────────────────────

def build_diop_llama(
    worker: str = "default",
    model_path: str = "",
    n_ctx: int = 2048,
    n_predict: int = 512,
    temperature: float = 0.7,
    timeout_s: int = 120,
    verbose: bool = False,
) -> DiopLlamaAdapter:
    """
    Ergonomic factory for DiopLlamaAdapter.

    Examples:
        build_diop_llama()                          # auto-discover, default worker
        build_diop_llama(worker="warden")           # auto-discover, warden prompts
        build_diop_llama(worker="code",             # explicit GGUF
                         model_path="djib/models/djibion.gguf")
    """
    return DiopLlamaAdapter(
        worker=worker,
        model_path=model_path,
        n_ctx=n_ctx,
        n_predict=n_predict,
        temperature=temperature,
        timeout_s=timeout_s,
        verbose=verbose,
    )
