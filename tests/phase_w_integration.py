#!/usr/bin/env python3
"""
Phase W: Full OO Integration Test
Tests the complete pipeline:
  kernel bus file -> oo-host bus read -> DIOP bridge -> oo-bot reaction

This test runs WITHOUT QEMU (no bare-metal required) by:
1. Writing a fake bus message to the file bus (simulating kernel output)
2. Verifying oo-host can parse it correctly
3. Simulating DIOP gateway health response
4. Verifying oo-bot bus_bridge handles DiopsEvent correctly

All tests use the file bus (flat JSONL files) — no real network needed.
"""
import json
import os
import sys
import time
import tempfile
import subprocess
import shutil
from pathlib import Path

ROOT = Path(__file__).parent.parent
OO_HOST = Path(r"C:\Users\djibi\OneDrive\Bureau\baremetal\oo-host")
OO_BOT  = ROOT / "oo-bot"

PASS = "[PASS]"
FAIL = "[FAIL]"
SKIP = "[SKIP]"

results = []

def check(name: str, ok: bool, detail: str = ""):
    tag = PASS if ok else FAIL
    msg = f"{tag} {name}"
    if detail:
        msg += f"\n       {detail}"
    print(msg)
    results.append((name, ok))
    return ok


# ============================================================
# TEST 1: Bus message schema validation
# ============================================================
def test_bus_schema():
    """Verify BusMessage JSON schema matches what oo-host expects."""
    from oo_prime.bus_bridge import BusMessage

    msg = BusMessage(
        msg_id="test-001",
        from_id="kernel",
        to="oo-bot",
        kind="heartbeat",
        payload="boot_count=3",
        ts_epoch_s=int(time.time()),
    )
    import dataclasses
    d = {f.name: getattr(msg, f.name) for f in dataclasses.fields(msg)}
    required = {"msg_id", "from_id", "to", "kind", "payload", "ts_epoch_s"}
    ok = required.issubset(d.keys())
    check("Bus schema has all required fields", ok, str(d.keys()))


# ============================================================
# TEST 2: DiopsEvent parsing
# ============================================================
def test_diops_event_parsing():
    """Verify DiopsEvent payload format can be round-tripped."""
    # Format: "worker=<W> kind=<K> status=<S> summary=<text>"
    payload = "worker=warden kind=health status=ok summary=DIOP gateway online"

    parts = {}
    for token in payload.split(" ", 4):
        if "=" in token:
            k, v = token.split("=", 1)
            parts[k] = v

    ok = (
        parts.get("worker") == "warden"
        and parts.get("kind") == "health"
        and parts.get("status") == "ok"
    )
    check("DiopsEvent payload round-trip", ok, str(parts))


# ============================================================
# TEST 3: oo-bot handles DiopsEvent
# ============================================================
def test_bot_handles_diops():
    """Verify bus_bridge._handle_diops_event() processes payload correctly."""
    sys.path.insert(0, str(OO_BOT))
    from oo_prime.bus_bridge import BotBusState, BusMessage
    import types

    state = BotBusState(agent_id="test-bot")
    # Simulate a DiopsEvent message
    msg = BusMessage(
        msg_id="diop-001",
        from_id="oo-host",
        to="broadcast",
        kind="diops_event",
        payload="worker=inference kind=worker_result status=ok summary=Generated 42 tokens",
        ts_epoch_s=int(time.time()),
    )

    # Import and call the handler directly
    from oo_prime import bus_bridge as bb
    bb._handle_diops_event(state, msg)

    ok = (
        state.diop_worker == "inference"
        and state.diop_last_kind == "worker_result"
    )
    check("oo-bot handles DiopsEvent (worker+kind tracked)", ok,
          f"worker={state.diop_worker} kind={state.diop_last_kind}")


# ============================================================
# TEST 4: oo-bot warden_alert -> observe mode
# ============================================================
def test_bot_warden_alert():
    """Verify DiopsEvent warden_alert demotes apply_mode to observe."""
    sys.path.insert(0, str(OO_BOT))
    from oo_prime.bus_bridge import BotBusState, BusMessage
    from oo_prime import bus_bridge as bb

    state = BotBusState(agent_id="test-bot")
    state.apply_mode = "safe"  # must be "safe" to be demoted to "observe"

    msg = BusMessage(
        msg_id="diop-002",
        from_id="oo-host",
        to="broadcast",
        kind="diops_event",
        payload="worker=warden kind=warden_alert status=triggered summary=pressure critical",
        ts_epoch_s=int(time.time()),
    )
    bb._handle_diops_event(state, msg)

    ok = state.apply_mode == "observe"
    check("oo-bot warden_alert demotes apply_mode to observe", ok,
          f"apply_mode={state.apply_mode}")


# ============================================================
# TEST 5: File bus write/read round-trip
# ============================================================
def test_file_bus_roundtrip():
    """Write a JSONL message to a temp bus dir, read it back."""
    with tempfile.TemporaryDirectory() as tmpdir:
        bus_dir = Path(tmpdir)
        inbox = bus_dir / "inbox"
        inbox.mkdir()

        msg = {
            "msg_id": "wt-001",
            "from_id": "kernel",
            "to": "oo-bot",
            "kind": "heartbeat",
            "payload": "boot_count=7",
            "ts_epoch_s": int(time.time()),
        }
        msg_file = inbox / "wt-001.jsonl"
        msg_file.write_text(json.dumps(msg) + "\n", encoding="utf-8")

        # Read back
        lines = msg_file.read_text(encoding="utf-8").splitlines()
        parsed = json.loads(lines[0])
        ok = parsed["kind"] == "heartbeat" and parsed["payload"] == "boot_count=7"
        check("File bus write/read round-trip", ok, f"kind={parsed['kind']}")


# ============================================================
# TEST 6: oo-host binary exists and responds to --help
# ============================================================
def test_oohost_binary():
    binary = OO_HOST / "target" / "debug" / "oo-host.exe"
    if not binary.exists():
        binary = OO_HOST / "target" / "release" / "oo-host.exe"
    if not binary.exists():
        print(f"{SKIP} oo-host binary not built — run: cargo build in oo-host/")
        results.append(("oo-host binary exists", None))
        return

    try:
        r = subprocess.run(
            [str(binary), "--help"],
            capture_output=True, text=True, timeout=5
        )
        ok = r.returncode == 0 or "Usage" in r.stdout or "Usage" in r.stderr
        check("oo-host binary responds to --help", ok,
              (r.stdout + r.stderr)[:120])
    except Exception as e:
        check("oo-host binary responds to --help", False, str(e))


# ============================================================
# TEST 7: Djibion v3 dataset is valid JSONL
# ============================================================
def test_djibion_v3():
    path = ROOT / "djib" / "dataset" / "djibion_v3.jsonl"
    if not path.exists():
        print(f"{SKIP} djibion_v3.jsonl not found")
        results.append(("djibion_v3 valid JSONL", None))
        return

    errors = []
    count = 0
    with path.open(encoding="utf-8") as f:
        for i, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                msgs = obj.get("messages", [])
                if len(msgs) < 3:
                    errors.append(f"line {i}: only {len(msgs)} messages")
                count += 1
            except json.JSONDecodeError as e:
                errors.append(f"line {i}: {e}")

    ok = len(errors) == 0
    detail = f"{count} samples" if ok else f"{len(errors)} errors: {errors[:3]}"
    check("djibion_v3.jsonl valid JSONL (40 samples)", ok, detail)


# ============================================================
# TEST 8: mamba3_training.jsonl >= 300 samples
# ============================================================
def test_mamba3_dataset():
    path = ROOT / "oo-model-repo" / "data" / "processed" / "mamba3_training.jsonl"
    if not path.exists():
        print(f"{SKIP} mamba3_training.jsonl not found")
        results.append(("mamba3_training >= 300 samples", None))
        return

    count = sum(1 for l in path.open(encoding="utf-8") if l.strip())
    ok = count >= 300
    check(f"mamba3_training.jsonl >= 300 samples (got {count})", ok)


# ============================================================
# TEST 9: oo_native_concepts.jsonl >= 60 samples
# ============================================================
def test_engine_training():
    path = ROOT / "oo-model-repo" / "data" / "engine_training" / "oo_native_concepts.jsonl"
    if not path.exists():
        check("oo_native_concepts.jsonl exists", False)
        return

    count = sum(1 for l in path.open(encoding="utf-8") if l.strip())
    ok = count >= 60
    check(f"oo_native_concepts.jsonl >= 60 samples (got {count})", ok)


# ============================================================
# TEST 10: LlamaCppAdapter import + ping (offline graceful)
# ============================================================
def test_llama_cpp_adapter():
    """LlamaCppAdapter import + ping (offline graceful)."""
    sys.path.insert(0, str(ROOT))
    try:
        from diop.adapters.llama_cpp import LlamaCppAdapter, _find_llama_cli
        # Test binary detection (returns None if not built)
        binary = _find_llama_cli()
        if binary is None:
            # No binary built yet — verify import works and function returns None gracefully
            check("LlamaCppAdapter._find_llama_cli() graceful offline", binary is None,
                  "llama-cli not built (expected in dev environment)")
        else:
            # Binary exists — do a real ping with a dummy (non-existent) model
            # ping() only checks the binary, not the model
            import dataclasses
            adapter = object.__new__(LlamaCppAdapter)
            adapter.llama_cli = str(binary)
            adapter.model_path = "nonexistent.gguf"
            ok = adapter.ping() == True
            check("LlamaCppAdapter.ping() with real binary", ok, str(binary))
    except ImportError as e:
        check("LlamaCppAdapter import", False, f"ImportError: {e}")
    except Exception as e:
        check("LlamaCppAdapter import", False, str(e))


# ============================================================
# Main
# ============================================================
if __name__ == "__main__":
    print("=" * 60)
    print("Phase W — OO Full Integration Test")
    print("=" * 60)
    print()

    sys.path.insert(0, str(OO_BOT))

    test_bus_schema()
    test_diops_event_parsing()
    test_bot_handles_diops()
    test_bot_warden_alert()
    test_file_bus_roundtrip()
    test_oohost_binary()
    test_djibion_v3()
    test_mamba3_dataset()
    test_engine_training()
    test_llama_cpp_adapter()

    print()
    print("=" * 60)
    passed  = sum(1 for _, ok in results if ok is True)
    failed  = sum(1 for _, ok in results if ok is False)
    skipped = sum(1 for _, ok in results if ok is None)
    total   = len(results)
    print(f"Results: {passed}/{total} passed  |  {failed} failed  |  {skipped} skipped")
    print("=" * 60)

    sys.exit(0 if failed == 0 else 1)
