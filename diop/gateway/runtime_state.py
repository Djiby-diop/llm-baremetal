from __future__ import annotations

import json
import time
from pathlib import Path

from ..model_store import get_model
from ..paths import user_data_root


def _runtime_state_path() -> Path:
    path = user_data_root() / "gateway" / "runtime_state.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_text('{"models": []}\n', encoding="utf-8")
    return path


def load_runtime_state() -> dict[str, object]:
    path = _runtime_state_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            data.setdefault("models", [])
            return data
    except Exception:
        pass
    return {"models": []}


def save_runtime_state(state: dict[str, object]) -> dict[str, object]:
    path = _runtime_state_path()
    path.write_text(json.dumps(state, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return state


def list_loaded_models() -> list[dict[str, object]]:
    state = load_runtime_state()
    models = state.get("models", [])
    return models if isinstance(models, list) else []


def load_model_into_runtime(
    name: str,
    *,
    adapter: str = "mock",
    resident: bool = False,
    status: str = "ready",
    last_error: str = "",
    extra_fields: dict[str, object] | None = None,
) -> dict[str, object] | None:
    name = name.strip()
    if not name:
        return None

    registered = get_model(name)
    if registered is None:
        return None

    state = load_runtime_state()
    models = list_loaded_models()

    now = int(time.time())
    entry = {
        "name": registered.name,
        "format": registered.format,
        "path": registered.path,
        "loaded_at_unix": now,
        "last_used_unix": now,
        "status": status,
        "adapter": adapter,
        "resident": resident,
        "last_error": last_error,
        "size": Path(registered.path).stat().st_size if Path(registered.path).exists() else 0,
    }
    if extra_fields:
        entry.update(extra_fields)

    replaced = False
    for i, item in enumerate(models):
        if isinstance(item, dict) and item.get("name") == registered.name:
            models[i] = {**item, **entry, "loaded_at_unix": item.get("loaded_at_unix", now)}
            replaced = True
            break
    if not replaced:
        models.append(entry)

    state["models"] = models
    save_runtime_state(state)
    return entry


def unload_model_from_runtime(name: str) -> bool:
    name = name.strip()
    if not name:
        return False
    state = load_runtime_state()
    models = list_loaded_models()
    kept = [m for m in models if not (isinstance(m, dict) and m.get("name") == name)]
    changed = len(kept) != len(models)
    if changed:
        state["models"] = kept
        save_runtime_state(state)
    return changed


def touch_loaded_model(name: str) -> dict[str, object] | None:
    name = name.strip()
    if not name:
        return None
    state = load_runtime_state()
    models = list_loaded_models()
    now = int(time.time())
    updated: dict[str, object] | None = None
    for item in models:
        if isinstance(item, dict) and item.get("name") == name:
            item["last_used_unix"] = now
            updated = item
            break
    if updated is not None:
        state["models"] = models
        save_runtime_state(state)
    return updated


def clear_runtime_state() -> None:
    save_runtime_state({"models": []})
