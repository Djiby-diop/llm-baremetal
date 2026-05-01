from __future__ import annotations

import os
import sys
from pathlib import Path


def _is_truthy(value: str | None) -> bool:
    if value is None:
        return False
    return value.strip().lower() in ("1", "true", "yes", "y", "on")


def _repo_default_memory_root() -> Path:
    # Keep the historical default inside the repo unless the user opts in to a data root.
    return Path(__file__).resolve().parent / "runtime" / "memory"


def get_data_root() -> Path | None:
    """
    Optional "data root" for large artifacts (models, caches, memory).

    If unset, DIOP keeps its historical behavior (memory under the repo unless overridden).
    """
    env = os.getenv("DIOP_DATA_ROOT") or os.getenv("DIOP_HOME")
    if env:
        return Path(env).expanduser().resolve()

    if sys.platform == "win32" and _is_truthy(os.getenv("DIOP_PREFER_D")):
        # Opt-in default for Windows users who want to keep heavy files off C:.
        if Path("D:/").exists():
            return Path("D:/diop").resolve()

    return None


def default_memory_root() -> Path:
    """
    Default memory root for CLI and services.

    Precedence:
    1) DIOP_MEMORY_ROOT
    2) DIOP_DATA_ROOT/DIOP_HOME (+ /memory)
    3) DIOP_PREFER_D=1 on Windows (+ D:/diop/memory)
    4) historical repo default (diop/runtime/memory)
    """
    env = os.getenv("DIOP_MEMORY_ROOT")
    if env:
        return Path(env).expanduser().resolve()

    data_root = get_data_root()
    if data_root is not None:
        return (data_root / "memory").resolve()

    return _repo_default_memory_root()


def default_model_dir() -> Path:
    """
    Default directory to search for local model files (GGUF, BIN, etc).

    Precedence:
    1) DIOP_MODEL_DIR
    2) DIOP_DATA_ROOT/DIOP_HOME (+ /models)
    3) DIOP_PREFER_D=1 on Windows (+ D:/diop/models)
    4) repo default: <repo>/models
    """
    env = os.getenv("DIOP_MODEL_DIR")
    if env:
        return Path(env).expanduser().resolve()

    data_root = get_data_root()
    if data_root is not None:
        return (data_root / "models").resolve()

    # paths.py lives at <repo>/diop/paths.py -> parents[1] is <repo>
    return (Path(__file__).resolve().parents[1] / "models").resolve()


def user_data_root() -> Path:
    """
    A safe place for DIOP to store persistent user data (registries, caches).

    Unlike default_memory_root(), this does NOT preserve historical repo-local behavior.
    It's intended for new features that should not write into the repo by default.
    """
    data_root = get_data_root()
    if data_root is not None:
        return data_root

    # Workspace-safe fallback: keep new registries/caches inside the repo.
    # This avoids permission issues in restricted environments and keeps everything portable.
    return (Path(__file__).resolve().parents[1] / ".diop_data").resolve()
