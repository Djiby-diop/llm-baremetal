#!/usr/bin/env python3
"""
oo-oracle-proxy.py — OO Oracle HTTP Proxy
==========================================
Bridges OO bare-metal (no TLS) → GPT-4 / Claude / Gemini (HTTPS).

OO sends a plain HTTP POST to this proxy on port 8080.
The proxy adds the Authorization header + forwards to the real API over HTTPS.

Usage:
    python3 oo-oracle-proxy.py --port 8080

Environment variables (or .env file):
    OPENAI_API_KEY      — for GPT-4o
    ANTHROPIC_API_KEY   — for Claude 3.5
    GOOGLE_API_KEY      — for Gemini 1.5

OO REPL usage (after setting proxy as federation server):
    /net_server 192.168.1.10 8080
    /net_oracle gpt4 What is the best way to optimize UEFI boot time?
    /net_oracle claude Explain how OO self-improvement works
    /net_oracle gemini List ideas for distributed bare-metal intelligence

Protocol:
    OO → POST http://proxy:8080/v1/chat/completions   (OpenAI format)
    OO → POST http://proxy:8080/v1/messages            (Anthropic format)
    OO → POST http://proxy:8080/v1beta/models/...      (Gemini format)
    proxy → HTTPS → real API → response → OO
"""

import os
import sys
import json
import signal
import argparse
import threading
import urllib.request
import urllib.error
from http.server import HTTPServer, BaseHTTPRequestHandler

# ── API endpoints ─────────────────────────────────────────────────────────────
ENDPOINTS = {
    "/v1/chat/completions": {
        "url": "https://api.openai.com/v1/chat/completions",
        "key_env": "OPENAI_API_KEY",
        "auth_header": "Bearer",
    },
    "/v1/messages": {
        "url": "https://api.anthropic.com/v1/messages",
        "key_env": "ANTHROPIC_API_KEY",
        "auth_header": "x-api-key",
        "extra_headers": {"anthropic-version": "2023-06-01"},
    },
}
# Gemini uses query param key, different format
GEMINI_PREFIX = "/v1beta/models/"

# ── .env loader (simple) ──────────────────────────────────────────────────────
def load_env():
    env_file = os.path.join(os.path.dirname(__file__), ".env")
    if os.path.exists(env_file):
        with open(env_file) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    os.environ.setdefault(k.strip(), v.strip())

# ── Response extractor ────────────────────────────────────────────────────────
def extract_content(resp_json: dict) -> str:
    """Pull the text content out of various API response formats."""
    # OpenAI format
    if "choices" in resp_json:
        choices = resp_json["choices"]
        if choices:
            msg = choices[0].get("message", {})
            return msg.get("content", "")
    # Anthropic format
    if "content" in resp_json:
        content = resp_json["content"]
        if isinstance(content, list) and content:
            return content[0].get("text", "")
        if isinstance(content, str):
            return content
    # Gemini format
    if "candidates" in resp_json:
        cands = resp_json["candidates"]
        if cands:
            parts = cands[0].get("content", {}).get("parts", [])
            if parts:
                return parts[0].get("text", "")
    return json.dumps(resp_json)

# ── Request handler ───────────────────────────────────────────────────────────
class OracleProxyHandler(BaseHTTPRequestHandler):
    log_lock = threading.Lock()

    def log_message(self, fmt, *args):
        with self.log_lock:
            print(f"[proxy] {self.address_string()} — {fmt % args}")

    def do_POST(self):
        content_len = int(self.headers.get("Content-Length", 0))
        body_bytes = self.rfile.read(content_len) if content_len else b""

        try:
            body_json = json.loads(body_bytes) if body_bytes else {}
        except Exception:
            body_json = {}

        path = self.path.split("?")[0]

        # ── Route to correct upstream ─────────────────────────────────────
        is_gemini = path.startswith(GEMINI_PREFIX)

        if is_gemini:
            self._handle_gemini(path, body_json)
        elif path in ENDPOINTS:
            self._handle_standard(path, body_json)
        else:
            self.send_error(404, f"Unknown path: {path}")

    def _make_oo_response(self, text: str):
        """Return a minimal JSON response that OO's _nb_json_extract can parse."""
        payload = json.dumps({"content": text}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _handle_standard(self, path: str, body: dict):
        cfg = ENDPOINTS[path]
        api_key = os.environ.get(cfg["key_env"], "")
        if not api_key:
            print(f"[proxy] WARNING: {cfg['key_env']} not set in environment/.env")

        headers = {
            "Content-Type": "application/json",
        }
        if cfg["auth_header"] == "Bearer":
            headers["Authorization"] = f"Bearer {api_key}"
        else:
            headers[cfg["auth_header"]] = api_key

        headers.update(cfg.get("extra_headers", {}))

        req_bytes = json.dumps(body).encode()
        req = urllib.request.Request(cfg["url"], data=req_bytes, headers=headers)

        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                resp_bytes = resp.read()
            resp_json = json.loads(resp_bytes)
            text = extract_content(resp_json)
            print(f"[proxy] OK → {path} ({len(text)} chars)")
            self._make_oo_response(text)
        except urllib.error.HTTPError as e:
            err_body = e.read().decode(errors="replace")
            print(f"[proxy] HTTP error {e.code}: {err_body[:200]}")
            self._make_oo_response(f"[Oracle error {e.code}] {err_body[:500]}")
        except Exception as e:
            print(f"[proxy] Exception: {e}")
            self._make_oo_response(f"[Proxy error] {e}")

    def _handle_gemini(self, path: str, body: dict):
        api_key = os.environ.get("GOOGLE_API_KEY", "")
        if not api_key:
            print("[proxy] WARNING: GOOGLE_API_KEY not set")

        # Convert OpenAI-format body → Gemini format if needed
        if "messages" in body:
            parts = []
            for msg in body["messages"]:
                parts.append({"text": msg.get("content", "")})
            gemini_body = {"contents": [{"parts": parts}]}
        else:
            gemini_body = body  # assume already Gemini format

        # Build Gemini URL: https://generativelanguage.googleapis.com + path + ?key=...
        model_part = path[len(GEMINI_PREFIX):]  # e.g. "gemini-1.5-pro:generateContent"
        url = (
            f"https://generativelanguage.googleapis.com/v1beta/models/"
            f"{model_part}?key={api_key}"
        )

        req_bytes = json.dumps(gemini_body).encode()
        req = urllib.request.Request(
            url, data=req_bytes, headers={"Content-Type": "application/json"}
        )

        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                resp_bytes = resp.read()
            resp_json = json.loads(resp_bytes)
            text = extract_content(resp_json)
            print(f"[proxy] OK → Gemini ({len(text)} chars)")
            self._make_oo_response(text)
        except urllib.error.HTTPError as e:
            err_body = e.read().decode(errors="replace")
            print(f"[proxy] Gemini HTTP error {e.code}: {err_body[:200]}")
            self._make_oo_response(f"[Gemini error {e.code}] {err_body[:500]}")
        except Exception as e:
            print(f"[proxy] Gemini exception: {e}")
            self._make_oo_response(f"[Proxy error] {e}")

    def do_GET(self):
        """Health check endpoint."""
        if self.path == "/health":
            msg = b'{"status":"ok","name":"oo-oracle-proxy","version":"2.0"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)
        else:
            self.send_error(404)

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    load_env()

    parser = argparse.ArgumentParser(description="OO Oracle HTTP Proxy")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8080, help="Port (default: 8080)")
    parser.add_argument("--quiet", action="store_true", help="Suppress request logs")
    args = parser.parse_args()

    if args.quiet:
        OracleProxyHandler.log_message = lambda *a, **k: None

    # Print key status
    print("╔═══════════════════════════════════════════════════════╗")
    print("║         OO Oracle Proxy v2.0 — Phase 2 Bridge        ║")
    print("╚═══════════════════════════════════════════════════════╝")
    print(f"  Listening on http://{args.host}:{args.port}")
    print()
    keys = {
        "OPENAI_API_KEY":    "GPT-4o",
        "ANTHROPIC_API_KEY": "Claude 3.5",
        "GOOGLE_API_KEY":    "Gemini 1.5",
    }
    for env_var, name in keys.items():
        val = os.environ.get(env_var, "")
        if val:
            print(f"  ✅ {name:12} — key set ({len(val)} chars)")
        else:
            print(f"  ⚠️  {name:12} — NOT SET (set {env_var} in env or .env)")
    print()
    print("  OO REPL commands:")
    print(f"    /net_server <your-ip> {args.port}")
    print("    /net_oracle gpt4   <question>")
    print("    /net_oracle claude <question>")
    print("    /net_oracle gemini <question>")
    print()

    server = HTTPServer((args.host, args.port), OracleProxyHandler)

    def handle_shutdown(sig, frame):
        print("\n[proxy] Shutting down...")
        server.shutdown()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_shutdown)
    signal.signal(signal.SIGTERM, handle_shutdown)

    server.serve_forever()

if __name__ == "__main__":
    main()
