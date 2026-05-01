from __future__ import annotations

import json
import urllib.parse
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from ..first_boot import prepare_first_boot
from ..model_blueprints import get_blueprint, list_blueprints, remove_blueprint, upsert_blueprint
from ..model_store import get_model, list_models, remove_model, resolve_model_path
from ..profile_store import clear_active_profile, load_active_profile, save_active_profile
from ..system_writer import apply_proposal, attach_patch, create_proposal, get_proposal, list_proposals, update_proposal_status
from ..twin_store import load_personal_twin, sync_twin_from_profile
from .engine_pool import RuntimeEnginePool
from .session_store import append_session_messages, clear_session, create_session_id, load_session


def _now_iso() -> str:
    # Keep it simple and dependency-free.
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _safe_filesize(path: Path) -> int:
    try:
        return path.stat().st_size
    except Exception:
        return 0


class DiopGatewayHandler(BaseHTTPRequestHandler):
    server_version = "DIOP-Gateway/0.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        # Less noisy than the default (still keeps basic visibility).
        print("[diop-gateway]", fmt % args)

    def _engine_pool(self) -> RuntimeEnginePool:
        pool = getattr(self.server, "diop_engine_pool", None)
        if pool is None:
            pool = RuntimeEnginePool()
            setattr(self.server, "diop_engine_pool", pool)
        return pool

    def do_GET(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/tags":
            return self._handle_tags()
        if parsed.path == "/api/ps":
            return self._handle_ps()
        if parsed.path == "/api/health":
            return self._handle_health()
        if parsed.path == "/api/blueprints":
            return self._handle_blueprints()
        if parsed.path == "/api/profile":
            return self._handle_profile_get()
        if parsed.path == "/api/twin":
            return self._handle_twin_get()
        if parsed.path == "/api/runtime":
            return self._handle_runtime()
        if parsed.path == "/api/system/proposals":
            return self._handle_system_proposals_get(parsed.query)
        if parsed.path == "/api/session":
            return self._handle_session_get(parsed.query)
        self._send_json({"error": "not_found", "path": self.path}, status=404)

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/api/generate":
            return self._handle_generate()
        if self.path == "/api/chat":
            return self._handle_chat()
        if self.path == "/api/show":
            return self._handle_show()
        if self.path == "/api/create":
            return self._handle_create()
        if self.path == "/api/load":
            return self._handle_load()
        if self.path == "/api/unload":
            return self._handle_unload()
        if self.path == "/api/reset":
            return self._handle_reset()
        if self.path == "/api/profile/set":
            return self._handle_profile_set()
        if self.path == "/api/profile/clear":
            return self._handle_profile_clear()
        if self.path == "/api/boot/setup":
            return self._handle_boot_setup()
        if self.path == "/api/session/clear":
            return self._handle_session_clear()
        if self.path == "/api/system/propose":
            return self._handle_system_propose()
        if self.path == "/api/system/patch":
            return self._handle_system_patch()
        if self.path == "/api/system/approve":
            return self._handle_system_status("approved")
        if self.path == "/api/system/reject":
            return self._handle_system_status("rejected")
        if self.path == "/api/system/apply":
            return self._handle_system_apply()
        if self.path == "/api/pull":
            return self._handle_pull()
        if self.path == "/api/delete":
            return self._handle_delete()
        if self.path in ("/v1/chat/completions",):
            return self._handle_openai_chat_completions()
        self._send_json({"error": "not_found", "path": self.path}, status=404)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length) if length > 0 else b"{}"
        try:
            data = json.loads(raw.decode("utf-8"))
            return data if isinstance(data, dict) else {}
        except Exception:
            return {}

    def _send_json(self, payload: dict[str, Any], status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_ndjson(self, events: list[dict[str, Any]], status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/x-ndjson")
        self.end_headers()
        for ev in events:
            line = (json.dumps(ev, ensure_ascii=True) + "\n").encode("utf-8")
            self.wfile.write(line)
            try:
                self.wfile.flush()
            except Exception:
                pass

    def _send_sse(self, events: list[dict[str, Any]], status: int = 200) -> None:
        # Minimal OpenAI streaming surface.
        self.send_response(status)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        for ev in events:
            chunk = ("data: " + json.dumps(ev, ensure_ascii=True) + "\n\n").encode("utf-8")
            self.wfile.write(chunk)
            try:
                self.wfile.flush()
            except Exception:
                pass
        self.wfile.write(b"data: [DONE]\n\n")

    def _handle_tags(self) -> None:
        models = []
        for m in list_models():
            p = Path(m.path)
            models.append(
                {
                    "name": m.name,
                    "model": m.name,
                    "modified_at": _now_iso(),
                    "size": _safe_filesize(p),
                    "digest": "",
                    "details": {"format": m.format},
                }
            )
        self._send_json({"models": models})

    def _handle_ps(self) -> None:
        self._send_json({"models": self._engine_pool().list_slots()})

    def _handle_health(self) -> None:
        adapter = str(getattr(self.server, "diop_adapter", "mock"))
        snapshot = self._engine_pool().runtime_snapshot(adapter)
        native = snapshot.get("native_runtime", {})
        self._send_json(
            {
                "status": "ok",
                "adapter": snapshot.get("adapter"),
                "resident_slots": snapshot.get("pool", {}).get("resident_slots", 0),
                "native_status": native.get("status", "unknown") if isinstance(native, dict) else "unknown",
            }
        )

    def _handle_blueprints(self) -> None:
        items = []
        for item in list_blueprints():
            items.append(
                {
                    "name": item.name,
                    "base_model": item.base_model,
                    "system": item.system,
                    "template": item.template,
                    "parameters": item.parameters or {},
                    "created_at_unix": item.created_at_unix,
                }
            )
        self._send_json({"blueprints": items})

    def _handle_runtime(self) -> None:
        adapter = str(getattr(self.server, "diop_adapter", "mock"))
        self._send_json(self._engine_pool().runtime_snapshot(adapter))

    def _handle_profile_get(self) -> None:
        self._send_json(load_active_profile())

    def _handle_twin_get(self) -> None:
        self._send_json(load_personal_twin())

    def _handle_session_get(self, query: str) -> None:
        params = urllib.parse.parse_qs(query)
        session_id = str((params.get("id") or [""])[0] or "").strip()
        if not session_id:
            self._send_json({"error": "missing_session_id"}, status=400)
            return
        self._send_json(load_session(session_id))

    def _handle_session_clear(self) -> None:
        req = self._read_json()
        session_id = str(req.get("id") or req.get("session") or "").strip()
        if not session_id:
            self._send_json({"error": "missing_session_id"}, status=400)
            return
        cleared = clear_session(session_id)
        if not cleared:
            self._send_json({"error": "session_not_found", "id": session_id}, status=404)
            return
        self._send_json({"status": "cleared", "id": session_id})

    @staticmethod
    def _proposal_payload(proposal: object) -> dict[str, Any]:
        return {
            "id": getattr(proposal, "id", ""),
            "title": getattr(proposal, "title", ""),
            "goal": getattr(proposal, "goal", ""),
            "summary": getattr(proposal, "summary", ""),
            "files": getattr(proposal, "files", []),
            "risk_level": getattr(proposal, "risk_level", ""),
            "status": getattr(proposal, "status", ""),
            "patch_text": getattr(proposal, "patch_text", ""),
            "patch_status": getattr(proposal, "patch_status", "none"),
            "applied_at_unix": getattr(proposal, "applied_at_unix", 0),
            "created_at_unix": getattr(proposal, "created_at_unix", 0),
            "updated_at_unix": getattr(proposal, "updated_at_unix", 0),
        }

    def _handle_system_proposals_get(self, query: str) -> None:
        params = urllib.parse.parse_qs(query)
        proposal_id = str((params.get("id") or [""])[0] or "").strip()
        if proposal_id:
            proposal = get_proposal(proposal_id)
            if proposal is None:
                self._send_json({"error": "proposal_not_found", "id": proposal_id}, status=404)
                return
            self._send_json({"proposal": self._proposal_payload(proposal)})
            return

        status = str((params.get("status") or [""])[0] or "").strip()
        proposals = [self._proposal_payload(item) for item in list_proposals(status=status or None)]
        self._send_json({"proposals": proposals})

    def _handle_system_propose(self) -> None:
        req = self._read_json()
        title = str(req.get("title") or "").strip()
        goal = str(req.get("goal") or "").strip()
        if not title or not goal:
            self._send_json({"error": "invalid_proposal", "hint": "title and goal are required"}, status=400)
            return
        files = req.get("files")
        proposal = create_proposal(
            title=title,
            goal=goal,
            summary=str(req.get("summary") or ""),
            files=[str(item) for item in files] if isinstance(files, list) else [],
            risk_level=str(req.get("risk_level") or req.get("risk") or "medium"),
        )
        self._send_json({"status": "created", "proposal": self._proposal_payload(proposal)})

    def _handle_system_patch(self) -> None:
        req = self._read_json()
        proposal_id = str(req.get("id") or "").strip()
        patch_text = str(req.get("patch_text") or "")
        if not proposal_id:
            self._send_json({"error": "missing_proposal_id"}, status=400)
            return
        if not patch_text.strip():
            self._send_json({"error": "missing_patch_text"}, status=400)
            return
        try:
            proposal = attach_patch(proposal_id, patch_text)
        except ValueError as e:
            self._send_json({"error": "patch_rejected", "message": str(e)}, status=400)
            return
        if proposal is None:
            self._send_json({"error": "proposal_not_found", "id": proposal_id}, status=404)
            return
        self._send_json({"status": "patched", "proposal": self._proposal_payload(proposal)})

    def _handle_system_status(self, status: str) -> None:
        req = self._read_json()
        proposal_id = str(req.get("id") or "").strip()
        if not proposal_id:
            self._send_json({"error": "missing_proposal_id"}, status=400)
            return
        proposal = update_proposal_status(proposal_id, status)
        if proposal is None:
            self._send_json({"error": "proposal_not_found", "id": proposal_id}, status=404)
            return
        self._send_json({"status": status, "proposal": self._proposal_payload(proposal)})

    def _handle_system_apply(self) -> None:
        req = self._read_json()
        proposal_id = str(req.get("id") or "").strip()
        repo_root = Path(str(req.get("repo_root") or Path.cwd()))
        if not proposal_id:
            self._send_json({"error": "missing_proposal_id"}, status=400)
            return
        try:
            result = apply_proposal(proposal_id, repo_root)
        except ValueError as e:
            self._send_json({"error": "apply_failed", "message": str(e)}, status=400)
            return
        if result is None:
            self._send_json({"error": "proposal_not_found", "id": proposal_id}, status=404)
            return
        proposal, changed = result
        self._send_json({"status": "applied", "proposal": self._proposal_payload(proposal), "changed": changed})


    def _handle_profile_set(self) -> None:
        req = self._read_json()
        role = str(req.get("role") or "").strip()
        if not role:
            self._send_json({"error": "missing_role"}, status=400)
            return
        workspace_style = str(req.get("workspace_style") or "")
        focus = req.get("focus")
        profile = save_active_profile(
            role=role,
            workspace_style=workspace_style,
            focus=focus if isinstance(focus, list) else [],
            preferences=req.get("preferences") if isinstance(req.get("preferences"), dict) else {},
        )
        twin = sync_twin_from_profile()
        self._send_json({"status": "saved", "profile": profile, "twin": twin})

    def _handle_profile_clear(self) -> None:
        cleared = clear_active_profile()
        self._send_json({"status": "cleared" if cleared else "missing"})

    def _handle_boot_setup(self) -> None:
        req = self._read_json()
        role = str(req.get("role") or "").strip()
        focus = req.get("focus")
        preferences = req.get("preferences")
        try:
            plan = prepare_first_boot(
                role=role,
                workspace_style=str(req.get("workspace_style") or ""),
                focus=[str(item) for item in focus] if isinstance(focus, list) else [],
                preferences=preferences if isinstance(preferences, dict) else {},
            )
        except ValueError as e:
            self._send_json({"error": "boot_setup_failed", "message": str(e)}, status=400)
            return
        self._send_json(
            {
                "status": "prepared",
                "profile": plan.profile,
                "twin": plan.twin,
                "proposal": self._proposal_payload(plan.proposal),
                "recommended_modules": plan.recommended_modules,
            }
        )

    def _generate_text(self, model: str | None, prompt: str) -> str:
        # Adapter is a policy knob. In early phases, we mostly want the HTTP surface to exist.
        # When the native DLL becomes 64-bit, the same endpoint becomes "real".
        adapter = str(getattr(self.server, "diop_adapter", "mock")).strip().lower()
        if model:
            return self._engine_pool().generate(model, prompt, adapter=adapter, max_tokens=512)

        if adapter == "native":
            try:
                from ..engine.bridge import NativeEngineBridge
                from ..adapters.native import NativeGenerationAdapter

                bridge = NativeEngineBridge()
                resolved = resolve_model_path(model) if model else None
                model_path = resolved or NativeGenerationAdapter._resolve_model_path()
                bridge.load_model(str(model_path))
                return bridge.generate(prompt, max_tokens=512)
            except Exception as e:
                return f"[native_unavailable] {e}"

        # Default to a predictable mock response.
        return f"[mock] {prompt}"

    def _resolve_generation_target(self, model: str | None) -> tuple[str | None, str, str]:
        """
        Returns:
        - resolved model slot/name used for execution
        - system prompt prefix
        - template string
        """
        if not model:
            return None, "", ""

        blueprint = get_blueprint(model)
        if blueprint is None:
            return model, "", ""
        return blueprint.base_model, blueprint.system, blueprint.template

    @staticmethod
    def _apply_blueprint(prompt: str, system: str, template: str) -> str:
        rendered = prompt
        if template:
            rendered = template.replace("{{prompt}}", prompt)
        if system:
            rendered = f"[system]\n{system}\n\n{rendered}"
        return rendered

    @staticmethod
    def _format_list_field(values: object) -> str:
        if not isinstance(values, list):
            return ""
        cleaned = [str(item).strip() for item in values if str(item).strip()]
        return ", ".join(cleaned[:8])

    def _personal_context_prompt(self) -> str:
        profile = load_active_profile()
        twin = load_personal_twin()

        lines: list[str] = []
        role = str(profile.get("role") or "").strip()
        workspace_style = str(profile.get("workspace_style") or "").strip()
        focus = self._format_list_field(profile.get("focus"))
        preferences = profile.get("preferences")
        twin_status = str(twin.get("status") or "").strip()
        behavior_markers = self._format_list_field(twin.get("behavior_markers"))
        delegation_scope = self._format_list_field(twin.get("delegation_scope"))

        if role:
            lines.append(f"role: {role}")
        if workspace_style:
            lines.append(f"workspace_style: {workspace_style}")
        if focus:
            lines.append(f"focus: {focus}")
        if isinstance(preferences, dict) and preferences:
            rendered_preferences = ", ".join(f"{k}={v}" for k, v in sorted(preferences.items())[:8])
            if rendered_preferences:
                lines.append(f"preferences: {rendered_preferences}")
        if twin_status and twin_status != "empty":
            lines.append(f"twin_status: {twin_status}")
        if behavior_markers:
            lines.append(f"behavior_markers: {behavior_markers}")
        if delegation_scope:
            lines.append(f"delegation_scope: {delegation_scope}")

        if not lines:
            return ""
        return "[personal_context]\n" + "\n".join(lines)

    def _apply_personal_context(self, prompt: str) -> str:
        context = self._personal_context_prompt()
        if not context:
            return prompt
        return f"{context}\n\n{prompt}".strip()

    @staticmethod
    def _chunk_text(text: str, max_chunk_chars: int = 32) -> list[str]:
        if not text:
            return [""]
        chunks: list[str] = []
        i = 0
        while i < len(text):
            chunks.append(text[i : i + max_chunk_chars])
            i += max_chunk_chars
        return chunks

    def _handle_generate(self) -> None:
        req = self._read_json()
        model = req.get("model")
        prompt = req.get("prompt") or ""
        stream = bool(req.get("stream", False))

        resolved_model, system, template = self._resolve_generation_target(model if isinstance(model, str) else None)
        final_prompt = self._apply_blueprint(str(prompt), system, template)
        final_prompt = self._apply_personal_context(final_prompt)
        text = self._generate_text(resolved_model, final_prompt)
        base = {
            "model": model or "unknown",
            "created_at": _now_iso(),
            "context": [],
            "total_duration": 0,
            "load_duration": 0,
            "prompt_eval_count": 0,
            "eval_count": 0,
        }

        if not stream:
            self._send_json({**base, "response": text, "done": True})
            return

        events: list[dict[str, Any]] = []
        for chunk in self._chunk_text(text):
            events.append({**base, "response": chunk, "done": False})
        events.append({**base, "response": "", "done": True})
        self._send_ndjson(events)

    def _handle_chat(self) -> None:
        # The chat endpoint uses message lists, not a raw prompt.
        req = self._read_json()
        model = req.get("model")
        messages = req.get("messages") or []
        stream = bool(req.get("stream", False))
        session_id = str(req.get("session") or "").strip() or create_session_id()

        prompt_parts: list[str] = []
        persisted_messages: list[dict[str, object]] = []
        persisted_session = load_session(session_id)
        for m in persisted_session.get("messages", []):
            if isinstance(m, dict):
                role = str(m.get("role", "user"))
                content = str(m.get("content", ""))
                prompt_parts.append(f"{role}: {content}")
                persisted_messages.append({"role": role, "content": content})

        inbound_messages: list[dict[str, object]] = []
        if isinstance(messages, list):
            for m in messages:
                if not isinstance(m, dict):
                    continue
                role = m.get("role", "user")
                content = m.get("content", "")
                prompt_parts.append(f"{role}: {content}")
                inbound_messages.append({"role": str(role), "content": str(content)})
        prompt = "\n".join(prompt_parts).strip()

        resolved_model, system, template = self._resolve_generation_target(model if isinstance(model, str) else None)
        final_prompt = self._apply_blueprint(prompt, system, template)
        final_prompt = self._apply_personal_context(final_prompt)
        text = self._generate_text(resolved_model, final_prompt)
        base = {"model": model or "unknown", "created_at": _now_iso(), "session": session_id}

        final_session = append_session_messages(
            session_id,
            inbound_messages + [{"role": "assistant", "content": text}],
        )

        if not stream:
            self._send_json(
                {
                    **base,
                    "message": {"role": "assistant", "content": text},
                    "done": True,
                    "context": {"message_count": len(final_session.get("messages", []))},
                }
            )
            return

        events: list[dict[str, Any]] = []
        for chunk in self._chunk_text(text):
            events.append({**base, "message": {"role": "assistant", "content": chunk}, "done": False})
        events.append({**base, "message": {"role": "assistant", "content": ""}, "done": True})
        self._send_ndjson(events)

    def _handle_show(self) -> None:
        req = self._read_json()
        name = str(req.get("name") or "")
        blueprint = get_blueprint(name)
        if blueprint is not None:
            self._send_json(
                {
                    "name": blueprint.name,
                    "modified_at": _now_iso(),
                    "size": 0,
                    "details": {
                        "kind": "blueprint",
                        "base_model": blueprint.base_model,
                        "system": blueprint.system,
                        "template": blueprint.template,
                        "parameters": blueprint.parameters or {},
                    },
                    "modelfile": "",
                    "parameters": json.dumps(blueprint.parameters or {}, ensure_ascii=True),
                    "template": blueprint.template,
                }
            )
            return

        model = get_model(name)
        if model is None:
            self._send_json({"error": "model_not_found", "name": name}, status=404)
            return
        p = Path(model.path)
        self._send_json(
            {
                "name": model.name,
                "modified_at": _now_iso(),
                "size": _safe_filesize(p),
                "details": {"format": model.format, "path": model.path},
                "modelfile": "",
                "parameters": "",
                "template": "",
            }
        )

    def _handle_create(self) -> None:
        req = self._read_json()
        name = str(req.get("name") or "").strip()
        base_model = str(req.get("from") or req.get("base_model") or "").strip()
        system = str(req.get("system") or "")
        template = str(req.get("template") or "")
        parameters = req.get("parameters")
        if not name or not base_model:
            self._send_json({"error": "invalid_create_request", "hint": "name and from/base_model are required"}, status=400)
            return

        entry = upsert_blueprint(
            name=name,
            base_model=base_model,
            system=system,
            template=template,
            parameters=parameters if isinstance(parameters, dict) else {},
        )
        self._send_json(
            {
                "status": "created",
                "name": entry.name,
                "details": {
                    "base_model": entry.base_model,
                    "system": entry.system,
                    "template": entry.template,
                    "parameters": entry.parameters or {},
                },
            }
        )

    def _handle_pull(self) -> None:
        # For DIOP this currently means "resolve from local registry and report readiness".
        req = self._read_json()
        name = str(req.get("name") or "")
        stream = bool(req.get("stream", False))
        load = bool(req.get("load", False))

        model = get_model(name)
        if model is None:
            if not stream:
                self._send_json({"error": "model_not_found", "name": name}, status=404)
                return
            self._send_ndjson(
                [
                    {"status": "error", "error": "model_not_found", "name": name, "done": True},
                ],
                status=404,
            )
            return

        # Simulate progress in a deterministic way (no network).
        steps = [
            {"status": "checking manifest", "completed": 1, "total": 3},
            {"status": "verifying blobs", "completed": 2, "total": 3},
            {"status": "ready", "completed": 3, "total": 3},
        ]
        if not stream:
            if load:
                entry = self._engine_pool().load(name, str(getattr(self.server, "diop_adapter", "mock")))
                loaded = entry is not None and bool(entry.get("resident"))
            else:
                loaded = False
            self._send_json({"status": "ready", "name": name, "done": True, "loaded": loaded})
            return
        events = [{**s, "name": name, "done": False} for s in steps[:-1]]
        loaded = False
        if load:
            entry = self._engine_pool().load(name, str(getattr(self.server, "diop_adapter", "mock")))
            loaded = entry is not None and bool(entry.get("resident"))
        events.append({**steps[-1], "name": name, "done": True, "loaded": loaded})
        self._send_ndjson(events)

    def _handle_delete(self) -> None:
        req = self._read_json()
        name = str(req.get("name") or "")
        self._engine_pool().unload(name)
        if remove_blueprint(name):
            self._send_json({"status": "deleted", "name": name, "kind": "blueprint"})
            return
        removed = remove_model(name)
        if not removed:
            self._send_json({"error": "model_not_found", "name": name}, status=404)
            return
        self._send_json({"status": "deleted", "name": name})

    def _handle_load(self) -> None:
        req = self._read_json()
        name = str(req.get("name") or "").strip()
        if not name:
            self._send_json({"error": "missing_model_name"}, status=400)
            return
        entry = self._engine_pool().load(name, str(getattr(self.server, "diop_adapter", "mock")))
        if entry is None:
            self._send_json({"error": "model_not_found", "name": name}, status=404)
            return
        self._send_json({"status": "loaded", "model": entry})

    def _handle_unload(self) -> None:
        req = self._read_json()
        name = str(req.get("name") or "").strip()
        if not name:
            self._send_json({"error": "missing_model_name"}, status=400)
            return
        removed = self._engine_pool().unload(name)
        if not removed:
            if get_model(name) is not None:
                self._send_json({"status": "unloaded", "name": name, "already_absent": True})
                return
            self._send_json({"error": "model_not_loaded", "name": name}, status=404)
            return
        self._send_json({"status": "unloaded", "name": name})

    def _handle_reset(self) -> None:
        result = self._engine_pool().reset()
        self._send_json(result)

    def _handle_openai_chat_completions(self) -> None:
        req = self._read_json()
        model = req.get("model")
        messages = req.get("messages") or []
        stream = bool(req.get("stream", False))

        # Minimal prompt assembly.
        prompt_parts: list[str] = []
        if isinstance(messages, list):
            for m in messages:
                if not isinstance(m, dict):
                    continue
                role = m.get("role", "user")
                content = m.get("content", "")
                prompt_parts.append(f"{role}: {content}")
        prompt = "\n".join(prompt_parts).strip()

        resolved_model, system, template = self._resolve_generation_target(model if isinstance(model, str) else None)
        final_prompt = self._apply_blueprint(prompt, system, template)
        final_prompt = self._apply_personal_context(final_prompt)
        text = self._generate_text(resolved_model, final_prompt)
        created = int(time.time())
        session_id = str(req.get("session") or "").strip() or create_session_id()
        inbound_messages: list[dict[str, object]] = []
        if isinstance(messages, list):
            for m in messages:
                if not isinstance(m, dict):
                    continue
                inbound_messages.append(
                    {"role": str(m.get("role", "user")), "content": str(m.get("content", ""))}
                )
        append_session_messages(session_id, inbound_messages + [{"role": "assistant", "content": text}])
        if not stream:
            self._send_json(
                {
                    "id": f"chatcmpl_{created}",
                    "object": "chat.completion",
                    "created": created,
                    "model": model or "unknown",
                    "session": session_id,
                    "choices": [
                        {
                            "index": 0,
                            "message": {"role": "assistant", "content": text},
                            "finish_reason": "stop",
                        }
                    ],
                    "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0},
                }
            )
            return

        chunks = self._chunk_text(text)
        events: list[dict[str, Any]] = []
        for chunk in chunks:
            events.append(
                {
                    "id": f"chatcmpl_{created}",
                    "object": "chat.completion.chunk",
                    "created": created,
                    "model": model or "unknown",
                    "choices": [{"index": 0, "delta": {"content": chunk}, "finish_reason": None}],
                }
            )
        events.append(
            {
                "id": f"chatcmpl_{created}",
                "object": "chat.completion.chunk",
                "created": created,
                "model": model or "unknown",
                "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
            }
        )
        self._send_sse(events)


def serve(host: str = "127.0.0.1", port: int = 11434, adapter: str = "mock") -> None:
    httpd = ThreadingHTTPServer((host, port), DiopGatewayHandler)
    # Stash adapter choice on the server instance so handlers can access it.
    setattr(httpd, "diop_adapter", adapter)
    setattr(httpd, "diop_engine_pool", RuntimeEnginePool())

    print(f"[diop-gateway] Listening on http://{host}:{port}")
    print(f"[diop-gateway] Adapter: {adapter}")
    print("[diop-gateway] Endpoints: GET /api/tags, GET /api/ps, POST /api/generate, POST /api/chat, POST /api/show, POST /api/create, POST /api/pull, POST /api/delete, POST /v1/chat/completions")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[diop-gateway] Shutdown.")
    finally:
        pool = getattr(httpd, "diop_engine_pool", None)
        if pool is not None:
            pool.close_all()
