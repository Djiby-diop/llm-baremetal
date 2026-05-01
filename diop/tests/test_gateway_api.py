from __future__ import annotations

import http.client
import json
import threading
import unittest
from pathlib import Path
from uuid import uuid4

from http.server import ThreadingHTTPServer

from diop.gateway.server import DiopGatewayHandler
from diop.model_store import add_model
from diop.profile_store import clear_active_profile
from diop.system_writer import clear_proposals
from diop.twin_store import clear_personal_twin


class GatewayApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        repo_models = Path(__file__).resolve().parents[2] / "models" / "stories15M.q8_0.gguf"
        if repo_models.exists():
            add_model("test-runtime-model", repo_models)

    def _start_server(self) -> tuple[ThreadingHTTPServer, int, threading.Thread]:
        httpd = ThreadingHTTPServer(("127.0.0.1", 0), DiopGatewayHandler)
        # Avoid hanging the test process on lingering request threads.
        httpd.daemon_threads = True  # type: ignore[attr-defined]
        setattr(httpd, "diop_adapter", "mock")

        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        host, port = httpd.server_address
        return httpd, int(port), thread

    def setUp(self) -> None:
        clear_active_profile()
        clear_personal_twin()
        clear_proposals()

    def _workspace_tempdir(self) -> Path:
        base = Path(__file__).resolve().parent / ".tmp" / "gateway_system_writer"
        base.mkdir(parents=True, exist_ok=True)
        target = base / f"case_{uuid4().hex[:8]}"
        target.mkdir(parents=True, exist_ok=True)
        return target

    def test_tags_and_generate_endpoints(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)

            conn.request("GET", "/api/tags")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertIn("models", payload)

            body = json.dumps({"model": "unknown", "prompt": "ping", "stream": False}).encode("utf-8")
            conn.request("POST", "/api/generate", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("done"), True)
            self.assertIn("response", payload)

            # Streaming NDJSON
            body = json.dumps({"model": "unknown", "prompt": "ping", "stream": True}).encode("utf-8")
            conn.request("POST", "/api/generate", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            raw = res.read().decode("utf-8").strip().splitlines()
            self.assertGreaterEqual(len(raw), 2)  # at least one chunk + final done
            first = json.loads(raw[0])
            last = json.loads(raw[-1])
            self.assertEqual(first.get("done"), False)
            self.assertEqual(last.get("done"), True)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_delete_unknown_model_returns_404(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps({"name": "definitely-not-a-model"}).encode("utf-8")
            conn.request("POST", "/api/delete", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 404)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_create_and_show_blueprint(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "name": "planner-lab",
                    "from": "tinyllama-q4km",
                    "system": "You are a planner.",
                    "template": "Task:\\n{{prompt}}",
                    "parameters": {"temperature": 0.2},
                }
            ).encode("utf-8")
            conn.request("POST", "/api/create", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "created")

            body = json.dumps({"name": "planner-lab"}).encode("utf-8")
            conn.request("POST", "/api/show", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("name"), "planner-lab")
            self.assertEqual(payload.get("details", {}).get("kind"), "blueprint")

            conn.request("GET", "/api/blueprints")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            names = [item.get("name") for item in payload.get("blueprints", [])]
            self.assertIn("planner-lab", names)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_chat_session_persists_history(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)

            body = json.dumps(
                {
                    "model": "unknown",
                    "session": "test-session-1",
                    "messages": [{"role": "user", "content": "hello"}],
                    "stream": False,
                }
            ).encode("utf-8")
            conn.request("POST", "/api/chat", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("session"), "test-session-1")
            self.assertEqual(payload.get("done"), True)

            body = json.dumps(
                {
                    "model": "unknown",
                    "session": "test-session-1",
                    "messages": [{"role": "user", "content": "continue"}],
                    "stream": False,
                }
            ).encode("utf-8")
            conn.request("POST", "/api/chat", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertGreaterEqual(payload.get("context", {}).get("message_count", 0), 4)

            conn.request("GET", "/api/session?id=test-session-1")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("id"), "test-session-1")
            self.assertGreaterEqual(len(payload.get("messages", [])), 4)

            body = json.dumps({"id": "test-session-1"}).encode("utf-8")
            conn.request("POST", "/api/session/clear", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "cleared")
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_runtime_load_ps_and_unload(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)

            body = json.dumps({"name": "test-runtime-model"}).encode("utf-8")
            conn.request("POST", "/api/load", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "loaded")
            self.assertEqual(payload.get("model", {}).get("resident"), True)
            self.assertEqual(payload.get("model", {}).get("adapter"), "mock")
            self.assertEqual(payload.get("model", {}).get("load_strategy"), "mock-resident")
            self.assertEqual(payload.get("model", {}).get("load_stage"), "logical-ready")

            conn.request("GET", "/api/ps")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            names = [item.get("name") for item in payload.get("models", [])]
            self.assertIn("test-runtime-model", names)
            loaded = next(item for item in payload.get("models", []) if item.get("name") == "test-runtime-model")
            self.assertEqual(loaded.get("resident"), True)
            self.assertEqual(loaded.get("inspect_summary"), "Logical runtime slot prepared.")

            body = json.dumps({"name": "test-runtime-model"}).encode("utf-8")
            conn.request("POST", "/api/unload", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "unloaded")
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_pull_can_mark_model_as_loaded(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps({"name": "test-runtime-model", "load": True, "stream": False}).encode("utf-8")
            conn.request("POST", "/api/pull", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("loaded"), True)

            conn.request("GET", "/api/ps")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            names = [item.get("name") for item in payload.get("models", [])]
            self.assertIn("test-runtime-model", names)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_generate_reuses_runtime_slot_metadata(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps({"name": "test-runtime-model"}).encode("utf-8")
            conn.request("POST", "/api/load", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            _ = res.read()

            body = json.dumps({"model": "test-runtime-model", "prompt": "ping", "stream": False}).encode("utf-8")
            conn.request("POST", "/api/generate", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("model"), "test-runtime-model")

            conn.request("GET", "/api/ps")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            loaded = next(item for item in payload.get("models", []) if item.get("name") == "test-runtime-model")
            self.assertEqual(loaded.get("status"), "ready")
            self.assertEqual(loaded.get("resident"), True)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_runtime_endpoint_reports_pool_and_native_probe(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            conn.request("GET", "/api/health")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "ok")
            self.assertEqual(payload.get("adapter"), "mock")

            conn.request("GET", "/api/runtime")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("adapter"), "mock")
            self.assertIn("pool", payload)
            self.assertIn("native_runtime", payload)
            self.assertIn(payload.get("native_runtime", {}).get("status"), ("ready", "unavailable", "missing"))
            self.assertIn("loaded_models", payload)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_profile_and_twin_endpoints(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "role": "entrepreneur",
                    "workspace_style": "strategy-wall",
                    "focus": ["planning", "execution"],
                }
            ).encode("utf-8")
            conn.request("POST", "/api/profile/set", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "saved")
            self.assertEqual(payload.get("profile", {}).get("role"), "entrepreneur")

            conn.request("GET", "/api/profile")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("role"), "entrepreneur")

            conn.request("GET", "/api/twin")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("profile_role"), "entrepreneur")
            self.assertEqual(payload.get("status"), "seeded")
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_boot_setup_gateway_seeds_profile_twin_and_proposal(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "role": "entrepreneur",
                    "workspace_style": "strategy-wall",
                    "focus": ["clients", "strategie"],
                    "preferences": {"validation": "human"},
                }
            ).encode("utf-8")
            conn.request("POST", "/api/boot/setup", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "prepared")
            self.assertEqual(payload.get("profile", {}).get("role"), "entrepreneur")
            self.assertEqual(payload.get("twin", {}).get("status"), "seeded")
            self.assertIn("strategy-dashboard", payload.get("recommended_modules", []))
            self.assertEqual(payload.get("proposal", {}).get("status"), "pending")

            conn.request("GET", "/api/system/proposals")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            titles = [item.get("title") for item in payload.get("proposals", [])]
            self.assertIn("First boot workspace for entrepreneur", titles)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_generate_injects_active_profile_context(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "role": "architecte",
                    "workspace_style": "atelier-calme",
                    "focus": ["plans", "clients"],
                    "preferences": {"validation": "human"},
                }
            ).encode("utf-8")
            conn.request("POST", "/api/profile/set", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            _ = res.read()

            body = json.dumps({"prompt": "prepare mon bureau", "stream": False}).encode("utf-8")
            conn.request("POST", "/api/generate", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            response = str(payload.get("response", ""))
            self.assertIn("[personal_context]", response)
            self.assertIn("role: architecte", response)
            self.assertIn("workspace_style: atelier-calme", response)
            self.assertIn("focus: plans, clients", response)
            self.assertIn("preferences: validation=human", response)
            self.assertIn("twin_status: seeded", response)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_openai_chat_completions_injects_active_profile_context(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "role": "gameur",
                    "workspace_style": "setup-performance",
                    "focus": ["entrainement", "strategie"],
                }
            ).encode("utf-8")
            conn.request("POST", "/api/profile/set", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            _ = res.read()

            body = json.dumps(
                {
                    "messages": [{"role": "user", "content": "aide moi a progresser"}],
                    "stream": False,
                }
            ).encode("utf-8")
            conn.request("POST", "/v1/chat/completions", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            content = str(payload.get("choices", [{}])[0].get("message", {}).get("content", ""))
            self.assertIn("[personal_context]", content)
            self.assertIn("role: gameur", content)
            self.assertIn("behavior_markers: entrainement, strategie", content)
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_runtime_reset_clears_loaded_slots(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps({"name": "test-runtime-model"}).encode("utf-8")
            conn.request("POST", "/api/load", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            _ = res.read()

            conn.request("POST", "/api/reset", body=b"{}", headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("status"), "reset")
            self.assertEqual(payload.get("resident_slots"), 0)

            conn.request("GET", "/api/ps")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("models"), [])
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_system_proposal_gateway_flow(self) -> None:
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "title": "First boot profile flow",
                    "goal": "Let the user describe their work style during setup.",
                    "summary": "Store the intent as a pending system proposal.",
                    "files": ["diop/boot_profile.py"],
                    "risk_level": "medium",
                }
            ).encode("utf-8")
            conn.request("POST", "/api/system/propose", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            proposal_id = payload.get("proposal", {}).get("id")
            self.assertTrue(proposal_id)
            self.assertEqual(payload.get("proposal", {}).get("status"), "pending")

            conn.request("GET", "/api/system/proposals")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            ids = [item.get("id") for item in payload.get("proposals", [])]
            self.assertIn(proposal_id, ids)

            conn.request("GET", f"/api/system/proposals?id={proposal_id}")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("proposal", {}).get("title"), "First boot profile flow")

            body = json.dumps({"id": proposal_id}).encode("utf-8")
            conn.request("POST", "/api/system/approve", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("proposal", {}).get("status"), "approved")
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)

    def test_system_patch_gateway_requires_approval_before_apply(self) -> None:
        workspace = self._workspace_tempdir()
        patch_text = "\n".join(
            [
                "diff --git a/gateway-generated.txt b/gateway-generated.txt",
                "new file mode 100644",
                "--- /dev/null",
                "+++ b/gateway-generated.txt",
                "@@ -0,0 +1,1 @@",
                "+gateway-alpha",
                "",
            ]
        )
        httpd, port, thread = self._start_server()
        conn = None
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            body = json.dumps(
                {
                    "title": "Gateway patch flow",
                    "goal": "Apply a guarded patch through the API.",
                }
            ).encode("utf-8")
            conn.request("POST", "/api/system/propose", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            proposal_id = payload.get("proposal", {}).get("id")
            self.assertTrue(proposal_id)

            body = json.dumps({"id": proposal_id, "patch_text": patch_text}).encode("utf-8")
            conn.request("POST", "/api/system/patch", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("proposal", {}).get("patch_status"), "draft")

            body = json.dumps({"id": proposal_id, "repo_root": str(workspace)}).encode("utf-8")
            conn.request("POST", "/api/system/apply", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 400)

            body = json.dumps({"id": proposal_id}).encode("utf-8")
            conn.request("POST", "/api/system/approve", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            _ = res.read()

            body = json.dumps({"id": proposal_id, "repo_root": str(workspace)}).encode("utf-8")
            conn.request("POST", "/api/system/apply", body=body, headers={"Content-Type": "application/json"})
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            payload = json.loads(res.read().decode("utf-8"))
            self.assertEqual(payload.get("proposal", {}).get("patch_status"), "applied")
            self.assertEqual((workspace / "gateway-generated.txt").read_text(encoding="utf-8"), "gateway-alpha\n")
        finally:
            if conn is not None:
                conn.close()
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
