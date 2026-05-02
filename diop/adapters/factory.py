from __future__ import annotations

from .base import BaseGenerationAdapter
from .mock import MockGenerationAdapter
from .local import LocalGenerationAdapter
from .swarm import SwarmGenerationAdapter
from .native import NativeGenerationAdapter
from ..engine.trained_adapter_fast import FastTrainedModelAdapter

ADAPTERS = {
    "mock":       MockGenerationAdapter,
    "local":      LocalGenerationAdapter,
    "swarm":      SwarmGenerationAdapter,
    "native":     NativeGenerationAdapter,
    "trained":    FastTrainedModelAdapter,
    "llama_cpp":  "LlamaCppAdapter",   # lazy-loaded
    "diop_llama": "DiopLlamaAdapter",  # Phase U2: worker-aware llama.cpp backend
}


def build_adapter(name: str = "mock") -> BaseGenerationAdapter:
    normalized = name.strip().lower()
    if normalized == "mock":
        return MockGenerationAdapter()
    if normalized == "local":
        return LocalGenerationAdapter()
    if normalized == "swarm":
        return SwarmGenerationAdapter()
    if normalized == "native":
        return NativeGenerationAdapter()
    if normalized.startswith("trained"):
        model_name = "diop_model"
        if ":" in normalized:
            model_name = normalized.split(":", 1)[1]
        return FastTrainedModelAdapter(model_name=model_name)
    if normalized.startswith("llama_cpp"):
        from .llama_cpp import LlamaCppAdapter
        model_path = ""
        if ":" in normalized:
            model_path = normalized.split(":", 1)[1]
        return LlamaCppAdapter(model_path=model_path)
    if normalized.startswith("diop_llama"):
        # "diop_llama"             → auto-discover model, default worker
        # "diop_llama:warden"      → auto-discover model, warden worker
        # "diop_llama:code:/path"  → explicit worker + explicit model path
        from .diop_llama import build_diop_llama
        suffix = normalized.split(":", 1)[1] if ":" in normalized else ""
        parts  = suffix.split(":", 1)
        worker     = parts[0] if parts[0] else "default"
        model_path = parts[1] if len(parts) > 1 else ""
        return build_diop_llama(worker=worker, model_path=model_path)
    raise ValueError(f"Unsupported DIOP adapter '{name}'")
