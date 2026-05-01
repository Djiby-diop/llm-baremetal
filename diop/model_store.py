from __future__ import annotations

import json
import time
from dataclasses import asdict, dataclass
from pathlib import Path

from .paths import user_data_root


@dataclass(frozen=True)
class RegisteredModel:
    name: str
    path: str
    format: str = "gguf"
    added_at_unix: int = 0


def _registry_path() -> Path:
    root = user_data_root()
    path = root / "models" / "registry.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_text("[]\n", encoding="utf-8")
    return path


def list_models() -> list[RegisteredModel]:
    path = _registry_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8") or "[]")
    except Exception:
        data = []
    models: list[RegisteredModel] = []
    for item in data if isinstance(data, list) else []:
        try:
            models.append(RegisteredModel(**item))
        except Exception:
            continue
    return models


def add_model(name: str, model_path: Path, model_format: str = "gguf") -> RegisteredModel:
    name = name.strip()
    if not name:
        raise ValueError("Model name cannot be empty.")

    model_path = model_path.expanduser().resolve()
    if not model_path.exists():
        raise FileNotFoundError(str(model_path))

    reg = _registry_path()
    models = list_models()

    entry = RegisteredModel(
        name=name,
        path=str(model_path),
        format=model_format.strip() or "unknown",
        added_at_unix=int(time.time()),
    )

    # Replace same-name entry (simple deterministic behavior).
    out = [asdict(m) for m in models if m.name != name]
    out.append(asdict(entry))
    reg.write_text(json.dumps(out, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return entry


def resolve_model_path(name: str) -> Path | None:
    normalized = name.strip()
    if not normalized:
        return None
    for model in list_models():
        if model.name == normalized:
            return Path(model.path).expanduser().resolve()
    return None


def remove_model(name: str) -> bool:
    normalized = name.strip()
    if not normalized:
        return False

    reg = _registry_path()
    models = list_models()
    kept = [asdict(m) for m in models if m.name != normalized]
    removed = len(kept) != len(models)
    if removed:
        reg.write_text(json.dumps(kept, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return removed


def get_model(name: str) -> RegisteredModel | None:
    normalized = name.strip()
    if not normalized:
        return None
    for m in list_models():
        if m.name == normalized:
            return m
    return None
