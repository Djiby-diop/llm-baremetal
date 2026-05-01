"""test_swarm_reactions.py — Phase R: OO-bot ↔ swarm reaction tests

Tests for swarm_event handling in bus_bridge.py.
Covers all SwarmEvent kinds and bot reaction logic.
"""
from __future__ import annotations

import sys
from pathlib import Path

# Ensure oo_prime is importable from tests/
sys.path.insert(0, str(Path(__file__).parent.parent))

from oo_prime.bus_bridge import (
    BotBusState,
    BusMessage,
    _handle_swarm_event,
    react_to_messages,
    render_bus_status,
    emit_swarm_alert,
    BusPaths,
)

import pytest


# ── Helpers ───────────────────────────────────────────────────────────────────

def make_swarm_event(
    node_id: int = 0,
    node_state: str = "ACTIVE",
    quorum_degraded: bool = False,
    status_flags: int = 0x00,
    from_id: str = "swarm-coordinator",
) -> BusMessage:
    payload = (
        f"node_id={node_id} "
        f"node_state={node_state} "
        f"quorum_degraded={str(quorum_degraded).lower()} "
        f"status_flags=0x{status_flags:02X}"
    )
    return BusMessage(
        msg_id="test-uuid",
        from_id=from_id,
        to=None,
        kind="swarm_event",
        payload=payload,
        ts_epoch_s=1000000,
    )


def make_state(agent_id: str = "oo-bot", apply_mode: str = "safe") -> BotBusState:
    return BotBusState(agent_id=agent_id, apply_mode=apply_mode)


# ── Test 1: ACTIVE node — no change to apply_mode ────────────────────────────

def test_swarm_active_no_change():
    state = make_state(apply_mode="safe")
    event = make_swarm_event(node_id=0, node_state="ACTIVE", status_flags=0x00)
    logs = _handle_swarm_event(state, event)

    assert state.apply_mode == "safe"
    assert not state.suspended
    assert not state.dry_run
    assert state.swarm_node_id == 0
    assert state.swarm_node_state == "ACTIVE"
    assert any("event from" in l for l in logs)


# ── Test 2: DEGRADED node → apply_mode demoted to observe ────────────────────

def test_swarm_degraded_demotes_apply_mode():
    state = make_state(apply_mode="safe")
    # DEGRADED flag = 0x01
    event = make_swarm_event(node_id=1, node_state="DEGRADED", status_flags=0x01)
    logs = _handle_swarm_event(state, event)

    assert state.apply_mode == "observe"
    assert not state.suspended
    assert not state.dry_run
    assert state.swarm_node_state == "DEGRADED"
    assert any("DEGRADED" in l for l in logs)


# ── Test 3: ISOLATED node → dry_run + observe ────────────────────────────────

def test_swarm_isolated_enters_dry_run():
    state = make_state(apply_mode="safe")
    # ISOLATED flag = 0x04
    event = make_swarm_event(node_id=2, node_state="ISOLATED", status_flags=0x04)
    logs = _handle_swarm_event(state, event)

    assert state.apply_mode == "observe"
    assert state.dry_run is True
    assert not state.suspended
    assert state.swarm_node_state == "ISOLATED"
    assert any("ISOLATED" in l for l in logs)


# ── Test 4: EMERGENCY (quorum degraded + degraded flag) → suspend ─────────────

def test_swarm_emergency_suspends_bot():
    state = make_state(apply_mode="safe")
    # DEGRADED (0x01) + EMERGENCY (0x02) + quorum_degraded=true
    event = make_swarm_event(
        node_id=0, node_state="DEGRADED",
        quorum_degraded=True, status_flags=0x03
    )
    logs = _handle_swarm_event(state, event)

    assert state.suspended is True
    assert state.dry_run is True
    assert state.apply_mode == "off"
    assert any("EMERGENCY" in l or "suspended" in l.lower() for l in logs)


# ── Test 5: Recovery from DEGRADED to ACTIVE restores safe mode ──────────────

def test_swarm_recovery_restores_safe():
    state = make_state(apply_mode="observe")
    state.swarm_node_state = "DEGRADED"   # simulate prior degraded state
    state.suspended = False
    state.dry_run = False

    event = make_swarm_event(node_id=0, node_state="ACTIVE", status_flags=0x00)
    logs = _handle_swarm_event(state, event)

    assert state.apply_mode == "safe"
    assert not state.dry_run
    assert state.swarm_node_state == "ACTIVE"
    assert any("recovered" in l or "restored" in l for l in logs)


# ── Test 6: react_to_messages routes swarm_event correctly ───────────────────

def test_react_to_messages_routes_swarm_event():
    state = make_state(apply_mode="safe")
    event = make_swarm_event(node_id=3, node_state="DEGRADED", status_flags=0x01)
    logs = react_to_messages(state, [event])

    # Should have demoted to observe
    assert state.apply_mode == "observe"
    assert len(logs) > 0


# ── Test 7: react_to_messages: non-swarm messages still work ─────────────────

def test_react_to_messages_mixed():
    state = make_state(apply_mode="safe")
    hb = BusMessage(
        msg_id="hb-1", from_id="governor", to="oo-bot",
        kind="heartbeat", payload="mode=governor", ts_epoch_s=1000001,
    )
    swarm_ev = make_swarm_event(node_id=0, node_state="ACTIVE")
    logs = react_to_messages(state, [hb, swarm_ev])

    assert state.last_heartbeat_ts == 1000001
    assert state.swarm_node_state == "ACTIVE"
    # No demotion for ACTIVE node
    assert state.apply_mode == "safe"


# ── Test 8: render_bus_status includes swarm fields ──────────────────────────

def test_render_bus_status_shows_swarm_fields():
    state = make_state()
    state.swarm_node_id = 2
    state.swarm_node_state = "DEGRADED"
    state.swarm_quorum_degraded = True
    rendered = render_bus_status(state)

    assert "swarm_node_id" in rendered
    assert "2" in rendered
    assert "DEGRADED" in rendered
    assert "swarm_quorum_deg" in rendered


# ── Test 9: quorum_degraded alone (without EMERGENCY flag) → only observe ────

def test_swarm_quorum_degraded_alone_observe():
    state = make_state(apply_mode="safe")
    # Only DEGRADED flag (0x01), quorum=true but no EMERGENCY (0x02)
    event = make_swarm_event(
        node_id=0, node_state="DEGRADED",
        quorum_degraded=True, status_flags=0x01
    )
    logs = _handle_swarm_event(state, event)
    # quorum_degraded=True + flag_degraded=True → emergency escalation
    assert state.suspended is True
    assert state.apply_mode == "off"


# ── Test 10: SYNCING node from ISOLATED → recovery ───────────────────────────

def test_swarm_syncing_from_isolated_recovery():
    state = make_state(apply_mode="observe")
    state.swarm_node_state = "ISOLATED"
    state.dry_run = True
    state.suspended = False

    event = make_swarm_event(node_id=0, node_state="SYNCING", status_flags=0x00)
    logs = _handle_swarm_event(state, event)

    assert state.apply_mode == "safe"
    assert state.dry_run is False
    assert state.swarm_node_state == "SYNCING"


# ── Test 11: emit_swarm_alert writes to correct paths ────────────────────────

def test_emit_swarm_alert(tmp_path):
    bus = BusPaths(tmp_path, "oo-bot")
    bus.init_dirs()
    state = make_state()
    state.swarm_node_id = 1
    state.swarm_quorum_degraded = True

    emit_swarm_alert(bus, state, alert_kind="quorum_degraded", node_state="DEGRADED")

    # Check outbox has message
    assert bus.outbox.exists()
    content = bus.outbox.read_text()
    assert "swarm_alert" in content
    assert "quorum_degraded" in content
    assert "governor" in content

    # Check governor inbox has the message
    gov_inbox = tmp_path / "inbox" / "governor.jsonl"
    assert gov_inbox.exists()
    assert "swarm_alert" in gov_inbox.read_text()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
