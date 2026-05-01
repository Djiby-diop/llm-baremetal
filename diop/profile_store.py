from __future__ import annotations

import json
import time
from pathlib import Path

from .paths import user_data_root


def _profile_path() -> Path:
    path = user_data_root() / "profiles" / "active_profile.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def load_active_profile() -> dict[str, object]:
    path = _profile_path()
    if not path.exists():
        return {
            "id": "active",
            "role": "",
            "workspace_style": "",
            "focus": [],
            "preferences": {},
            "created_at_unix": 0,
            "updated_at_unix": 0,
        }
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            data.setdefault("id", "active")
            data.setdefault("focus", [])
            data.setdefault("preferences", {})
            return data
    except Exception:
        pass
    return {
        "id": "active",
        "role": "",
        "workspace_style": "",
        "focus": [],
        "preferences": {},
        "created_at_unix": 0,
        "updated_at_unix": 0,
    }


def save_active_profile(
    *,
    role: str,
    workspace_style: str = "",
    focus: list[str] | None = None,
    preferences: dict[str, object] | None = None,
) -> dict[str, object]:
    path = _profile_path()
    current = load_active_profile()
    now = int(time.time())
    payload = {
        "id": "active",
        "role": role.strip(),
        "workspace_style": workspace_style.strip(),
        "focus": list(focus or []),
        "preferences": dict(preferences or {}),
        "created_at_unix": int(current.get("created_at_unix", 0) or now),
        "updated_at_unix": now,
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return payload


def clear_active_profile() -> bool:
    path = _profile_path()
    if not path.exists():
        return False
    path.unlink()
    return True

