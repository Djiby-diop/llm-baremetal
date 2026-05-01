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
    raise ValueError(f"Unsupported DIOP adapter '{name}'")
