from __future__ import annotations

from .base import BaseGenerationAdapter
from .mock import MockGenerationAdapter
from .local import LocalGenerationAdapter
from .swarm import SwarmGenerationAdapter
from .native import NativeGenerationAdapter
from ..engine.trained_adapter_fast import FastTrainedModelAdapter

ADAPTERS = {
    "mock": MockGenerationAdapter,
    "local": LocalGenerationAdapter,
    "swarm": SwarmGenerationAdapter,
    "native": NativeGenerationAdapter,
    "trained": FastTrainedModelAdapter,
    "llama_cpp": "LlamaCppAdapter",  # lazy-loaded to avoid subprocess import at startup
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
    raise ValueError(f"Unsupported DIOP adapter '{name}'")
