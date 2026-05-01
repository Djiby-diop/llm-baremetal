from __future__ import annotations

import json
import time
from dataclasses import asdict, dataclass
from pathlib import Path

from .paths import user_data_root


@dataclass(frozen=True)
class ModelBlueprint:
    name: str
    base_model: str
    system: str = ""
    template: str = ""
    parameters: dict[str, object] | None = None
    created_at_unix: int = 0


def _blueprints_path() -> Path:
    root = user_data_root()
    path = root / "models" / "blueprints.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_text("[]\n", encoding="utf-8")
    return path


def list_blueprints() -> list[ModelBlueprint]:
    path = _blueprints_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8") or "[]")
    except Exception:
        data = []
    items: list[ModelBlueprint] = []
    for item in data if isinstance(data, list) else []:
        try:
            items.append(ModelBlueprint(**item))
        except Exception:
            continue
    return items


def get_blueprint(name: str) -> ModelBlueprint | None:
    normalized = name.strip()
    if not normalized:
        return None
    for item in list_blueprints():
        if item.name == normalized:
            return item
    return None


def remove_blueprint(name: str) -> bool:
    normalized = name.strip()
    if not normalized:
        return False

    path = _blueprints_path()
    items = list_blueprints()
    kept = [asdict(item) for item in items if item.name != normalized]
    removed = len(kept) != len(items)
    if removed:
        path.write_text(json.dumps(kept, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return removed


def upsert_blueprint(
    name: str,
    base_model: str,
    system: str = "",
    template: str = "",
    parameters: dict[str, object] | None = None,
) -> ModelBlueprint:
    normalized = name.strip()
    if not normalized:
        raise ValueError("Blueprint name cannot be empty.")

    entry = ModelBlueprint(
        name=normalized,
        base_model=base_model.strip(),
        system=system,
        template=template,
        parameters=parameters or {},
        created_at_unix=int(time.time()),
    )

    path = _blueprints_path()
    items = [asdict(item) for item in list_blueprints() if item.name != normalized]
    items.append(asdict(entry))
    path.write_text(json.dumps(items, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return entry
