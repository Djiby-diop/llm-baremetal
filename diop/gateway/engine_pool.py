from __future__ import annotations

import json
import time
from typing import Any

from ..model_store import get_model
from .runtime_state import (
    clear_runtime_state,
    list_loaded_models,
    load_model_into_runtime,
    touch_loaded_model,
    unload_model_from_runtime,
)


class RuntimeEnginePool:
    def __init__(self) -> None:
        self._engines: dict[str, Any] = {}
        self._stats: dict[str, object] = {
            "pool_created_at_unix": int(time.time()),
            "loads": 0,
            "load_failures": 0,
            "generations": 0,
        }

    def list_slots(self) -> list[dict[str, object]]:
        return list_loaded_models()

    def runtime_snapshot(self, adapter: str) -> dict[str, object]:
        native_probe: dict[str, object]
        try:
            from ..engine.bridge import NativeEngineBridge

            native_probe = NativeEngineBridge.probe_runtime()
        except Exception as e:
            native_probe = {
                "platform": "unknown",
                "library_path": "",
                "library_exists": False,
                "status": "unavailable",
                "error": str(e),
            }

        return {
            "adapter": (adapter or "mock").strip().lower(),
            "pool": {
                **self._stats,
                "resident_slots": len(self._engines),
            },
            "native_runtime": native_probe,
            "loaded_models": self.list_slots(),
        }

    @staticmethod
    def _safe_parse_json(raw: str) -> dict[str, object]:
        try:
            parsed = json.loads(raw)
            return parsed if isinstance(parsed, dict) else {}
        except Exception:
            return {}

    def load(self, name: str, adapter: str) -> dict[str, object] | None:
        registered = get_model(name)
        if registered is None:
            return None

        normalized_adapter = (adapter or "mock").strip().lower()
        self._stats["loads"] = int(self._stats.get("loads", 0)) + 1
        if normalized_adapter == "native":
            try:
                from ..engine.bridge import NativeEngineBridge

                existing = self._engines.get(name)
                if existing is None:
                    engine = NativeEngineBridge()
                    engine.load_model(registered.path)
                    self._engines[name] = engine
                else:
                    engine = existing

                inspect_payload = self._safe_parse_json(str(engine.inspect_model()))
                plan_payload = self._safe_parse_json(str(engine.plan_load()))
                prepare_payload = self._safe_parse_json(str(engine.prepare_runtime()))
                return load_model_into_runtime(
                    name,
                    adapter="native",
                    resident=True,
                    status="ready",
                    last_error="",
                    extra_fields={
                        "load_strategy": "native-ffi-resident",
                        "load_stage": "runtime-prepared",
                        "inspect_summary": inspect_payload.get("summary", ""),
                        "plan_summary": plan_payload.get("summary", ""),
                        "prepare_summary": prepare_payload.get("summary", ""),
                    },
                )
            except Exception as e:
                self._stats["load_failures"] = int(self._stats.get("load_failures", 0)) + 1
                return load_model_into_runtime(
                    name,
                    adapter="native",
                    resident=False,
                    status="fallback",
                    last_error=str(e),
                    extra_fields={
                        "load_strategy": "native-ffi-fallback",
                        "load_stage": "ffi-error",
                        "inspect_summary": "",
                        "plan_summary": "",
                        "prepare_summary": "",
                    },
                )

        # Mock/local logical residency.
        self._engines[name] = self._engines.get(name, {"kind": "mock-runtime"})
        return load_model_into_runtime(
            name,
            adapter=normalized_adapter,
            resident=True,
            status="ready",
            last_error="",
            extra_fields={
                "load_strategy": f"{normalized_adapter}-resident",
                "load_stage": "logical-ready",
                "inspect_summary": "Logical runtime slot prepared.",
                "plan_summary": "",
                "prepare_summary": "",
            },
        )

    def unload(self, name: str) -> bool:
        engine = self._engines.pop(name, None)
        if engine is not None and hasattr(engine, "__del__"):
            try:
                engine.__del__()
            except Exception:
                pass
        return unload_model_from_runtime(name)

    def generate(self, model: str, prompt: str, adapter: str, max_tokens: int = 512) -> str:
        slot = self.load(model, adapter)
        if slot is None:
            return f"[model_not_found] {model}"

        self._stats["generations"] = int(self._stats.get("generations", 0)) + 1
        normalized_adapter = (adapter or "mock").strip().lower()
        if normalized_adapter == "native":
            engine = self._engines.get(model)
            if engine is not None and hasattr(engine, "generate"):
                touch_loaded_model(model)
                return str(engine.generate(prompt, max_tokens=max_tokens))
            return f"[native_unavailable] {slot.get('last_error', 'native engine unavailable')}"

        touch_loaded_model(model)
        return f"[mock] {prompt}"

    def close_all(self) -> None:
        for name in list(self._engines.keys()):
            self.unload(name)

    def reset(self) -> dict[str, object]:
        self.close_all()
        clear_runtime_state()
        self._stats = {
            "pool_created_at_unix": int(time.time()),
            "loads": 0,
            "load_failures": 0,
            "generations": 0,
        }
        return {
            "status": "reset",
            "resident_slots": 0,
        }
