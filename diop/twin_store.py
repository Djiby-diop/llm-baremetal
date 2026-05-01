from __future__ import annotations

import json
import time
from pathlib import Path

from .paths import user_data_root
from .profile_store import load_active_profile


def _twin_path() -> Path:
    path = user_data_root() / "profiles" / "personal_twin.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def load_personal_twin() -> dict[str, object]:
    path = _twin_path()
    if not path.exists():
        return {
            "id": "twin-active",
            "profile_role": "",
            "status": "empty",
            "behavior_markers": [],
            "delegation_scope": [],
            "updated_at_unix": 0,
        }
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            data.setdefault("id", "twin-active")
            data.setdefault("behavior_markers", [])
            data.setdefault("delegation_scope", [])
            return data
    except Exception:
        pass
    return {
        "id": "twin-active",
        "profile_role": "",
        "status": "empty",
        "behavior_markers": [],
        "delegation_scope": [],
        "updated_at_unix": 0,
    }


def sync_twin_from_profile() -> dict[str, object]:
    profile = load_active_profile()
    now = int(time.time())
    payload = {
        "id": "twin-active",
        "profile_role": str(profile.get("role", "")),
        "status": "seeded" if profile.get("role") else "empty",
        "behavior_markers": list(profile.get("focus", []))[:8],
        "delegation_scope": ["assist", "prepare", "observe"] if profile.get("role") else [],
        "updated_at_unix": now,
    }
    _twin_path().write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return payload


def clear_personal_twin() -> bool:
    path = _twin_path()
    if not path.exists():
        return False
    path.unlink()
    return True
