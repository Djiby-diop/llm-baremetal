from __future__ import annotations

import json
import time
import uuid
from pathlib import Path

from ..paths import user_data_root


def _sessions_root() -> Path:
    root = user_data_root() / "gateway" / "sessions"
    root.mkdir(parents=True, exist_ok=True)
    return root


def _session_path(session_id: str) -> Path:
    return _sessions_root() / f"{session_id}.json"


def create_session_id() -> str:
    return f"sess_{uuid.uuid4().hex[:16]}"


def load_session(session_id: str) -> dict[str, object]:
    path = _session_path(session_id)
    if not path.exists():
        return {"id": session_id, "created_at_unix": int(time.time()), "messages": []}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            data.setdefault("id", session_id)
            data.setdefault("messages", [])
            return data
    except Exception:
        pass
    return {"id": session_id, "created_at_unix": int(time.time()), "messages": []}


def save_session(session_id: str, messages: list[dict[str, object]]) -> dict[str, object]:
    path = _session_path(session_id)
    payload = {
        "id": session_id,
        "updated_at_unix": int(time.time()),
        "messages": messages,
    }
    if path.exists():
        existing = load_session(session_id)
        payload["created_at_unix"] = int(existing.get("created_at_unix", int(time.time())))
    else:
        payload["created_at_unix"] = int(time.time())
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return payload


def append_session_messages(session_id: str, new_messages: list[dict[str, object]]) -> dict[str, object]:
    session = load_session(session_id)
    messages = list(session.get("messages", []))
    messages.extend(new_messages)
    return save_session(session_id, messages)


def clear_session(session_id: str) -> bool:
    path = _session_path(session_id)
    if not path.exists():
        return False
    path.unlink()
    return True
