"""bus_bridge.py — OO-Bot Bus Bridge (Phase M)

Connects oo_prime to the oo-host file bus so the Governor can send
directives and oo-bot can publish its cycle reports back.

Bus topology (file-based, same format as oo-host/src/bus.rs):
  bus_dir/
    inbox/<agent_id>.jsonl     ← Governor sends directives here
    outbox/<agent_id>.jsonl    ← oo-bot publishes events here
    broadcast.jsonl            ← All instances see this

Message kinds handled (inbound, from Governor):
  goal_sync "goal=pause_noncritical"      → set apply_mode=observe
  goal_sync "goal=pause_external_agents"  → set apply_mode=observe, dry_run=True
  goal_sync "goal=emergency_halt"         → suspend all cycles
  goal_sync "goal=resume_all"             → resume apply_mode=safe
  goal_sync "goal=resume_critical"        → resume apply_mode=observe
  heartbeat                               → record Governor heartbeat

Message kinds emitted (outbound):
  heartbeat   → periodic alive signal from oo-bot
  goal_sync   → after each cycle: "cycle_done actions=N accepted=N"
  uart_event  → forwarded oo-bot decisions as structured events
"""

from __future__ import annotations

import json
import os
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


# ── BusMessage (mirrors oo-host bus.rs BusMessage) ────────────────────────────

@dataclass(slots=True)
class BusMessage:
    msg_id: str
    from_id: str
    to: str | None
    kind: str
    payload: str
    ts_epoch_s: int
    reply_to: str | None = None

    @staticmethod
    def new(from_id: str, to: str | None, kind: str, payload: str) -> "BusMessage":
        return BusMessage(
            msg_id=str(uuid.uuid4()),
            from_id=from_id,
            to=to,
            kind=kind,
            payload=payload,
            ts_epoch_s=int(time.time()),
        )

    def to_json(self) -> str:
        to_field = f'"{self.to}"' if self.to else "null"
        reply_field = f'"{self.reply_to}"' if self.reply_to else "null"
        safe_payload = self.payload.replace('"', '\\"')
        return (
            f'{{"msg_id":"{self.msg_id}",'
            f'"from":"{self.from_id}",'
            f'"to":{to_field},'
            f'"kind":"{self.kind}",'
            f'"payload":"{safe_payload}",'
            f'"ts_epoch_s":{self.ts_epoch_s},'
            f'"reply_to":{reply_field}}}'
        )

    @staticmethod
    def from_json(line: str) -> "BusMessage | None":
        try:
            d = json.loads(line)
            return BusMessage(
                msg_id=d.get("msg_id", ""),
                from_id=d.get("from", "unknown"),
                to=d.get("to"),
                kind=d.get("kind", ""),
                payload=d.get("payload", ""),
                ts_epoch_s=int(d.get("ts_epoch_s", 0)),
                reply_to=d.get("reply_to"),
            )
        except (json.JSONDecodeError, KeyError, TypeError):
            return None


# ── Bus paths (mirrors oo-host BusPaths) ─────────────────────────────────────

class BusPaths:
    def __init__(self, bus_dir: Path, instance_id: str) -> None:
        self.bus_dir = bus_dir
        self.instance_id = instance_id
        self.inbox = bus_dir / "inbox" / f"{instance_id}.jsonl"
        self.outbox = bus_dir / "outbox" / f"{instance_id}.jsonl"
        self.broadcast = bus_dir / "broadcast.jsonl"

    def init_dirs(self) -> None:
        self.inbox.parent.mkdir(parents=True, exist_ok=True)
        self.outbox.parent.mkdir(parents=True, exist_ok=True)


def _append_msg(path: Path, msg: BusMessage) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as f:
        f.write(msg.to_json() + "\n")


def broadcast(bus: BusPaths, msg: BusMessage) -> None:
    _append_msg(bus.broadcast, msg)
    _append_msg(bus.outbox, msg)


def send(bus: BusPaths, msg: BusMessage) -> None:
    _append_msg(bus.outbox, msg)
    if msg.to:
        _append_msg(bus.bus_dir / "inbox" / f"{msg.to}.jsonl", msg)


def read_inbox(bus: BusPaths, since: int | None = None) -> list[BusMessage]:
    msgs: list[BusMessage] = []
    for path in [bus.inbox, bus.broadcast]:
        if not path.exists():
            continue
        with path.open(encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                m = BusMessage.from_json(line)
                if m and (since is None or m.ts_epoch_s >= since):
                    msgs.append(m)
    msgs.sort(key=lambda m: m.ts_epoch_s)
    return msgs


# ── Governor directive model ──────────────────────────────────────────────────

@dataclass
class BotDirective:
    """Parsed governor directive affecting oo-bot behavior."""
    apply_mode: str = "safe"      # "safe" | "observe" | "off"
    dry_run: bool = False
    suspended: bool = False
    gov_mode: str = "normal"      # sovereign_mode from governor


def parse_directive(payload: str) -> BotDirective | None:
    """Extract a BotDirective from a goal_sync payload string."""
    goal = _kv(payload, "goal")
    if not goal:
        return None
    gov_mode = _kv(payload, "gov_mode") or "normal"
    if goal == "emergency_halt":
        return BotDirective(apply_mode="off", dry_run=True, suspended=True, gov_mode=gov_mode)
    if goal == "pause_external_agents":
        return BotDirective(apply_mode="observe", dry_run=True, suspended=False, gov_mode=gov_mode)
    if goal == "pause_noncritical":
        return BotDirective(apply_mode="observe", dry_run=False, suspended=False, gov_mode=gov_mode)
    if goal == "resume_all":
        return BotDirective(apply_mode="safe", dry_run=False, suspended=False, gov_mode=gov_mode)
    if goal == "resume_critical":
        return BotDirective(apply_mode="observe", dry_run=False, suspended=False, gov_mode=gov_mode)
    return None


def _kv(s: str, key: str) -> str | None:
    """Extract value from 'key=value' space-separated string."""
    needle = f"{key}="
    idx = s.find(needle)
    if idx == -1:
        return None
    rest = s[idx + len(needle):]
    end = rest.find(" ")
    return rest[:end] if end != -1 else rest


# ── Bot state ─────────────────────────────────────────────────────────────────

@dataclass
class BotBusState:
    agent_id: str
    apply_mode: str = "safe"
    dry_run: bool = False
    suspended: bool = False
    gov_mode: str = "normal"
    last_directive_ts: int = 0
    last_heartbeat_ts: int = 0
    consecutive_ok: int = 0
    cycles_done: int = 0
    cycles_suspended: int = 0


# ── Reactor ───────────────────────────────────────────────────────────────────

def react_to_messages(
    state: BotBusState,
    messages: list[BusMessage],
) -> list[str]:
    """
    Process incoming bus messages, update state, return list of log lines.
    """
    logs: list[str] = []
    for msg in messages:
        if msg.kind == "goal_sync":
            directive = parse_directive(msg.payload)
            if directive:
                prev_suspended = state.suspended
                state.apply_mode = directive.apply_mode
                state.dry_run = directive.dry_run
                state.suspended = directive.suspended
                state.gov_mode = directive.gov_mode
                state.last_directive_ts = msg.ts_epoch_s
                logs.append(
                    f"[bus] directive from {msg.from_id}: "
                    f"apply_mode={state.apply_mode} "
                    f"dry_run={state.dry_run} "
                    f"suspended={state.suspended} "
                    f"gov_mode={state.gov_mode}"
                )
                if not prev_suspended and state.suspended:
                    logs.append("[bus] ⚠️  oo-bot SUSPENDED by Governor (emergency_halt)")
                elif prev_suspended and not state.suspended:
                    logs.append("[bus] ✅  oo-bot RESUMED by Governor")
        elif msg.kind == "heartbeat":
            state.last_heartbeat_ts = msg.ts_epoch_s
    return logs


# ── Emitters ──────────────────────────────────────────────────────────────────

def emit_heartbeat(bus: BusPaths, state: BotBusState) -> None:
    msg = BusMessage.new(
        from_id=state.agent_id,
        to=None,
        kind="heartbeat",
        payload=(
            f"mode=oo-bot gov_mode={state.gov_mode} "
            f"apply_mode={state.apply_mode} "
            f"suspended={state.suspended} "
            f"cycles_done={state.cycles_done}"
        ),
    )
    broadcast(bus, msg)


def emit_cycle_report(
    bus: BusPaths,
    state: BotBusState,
    accepted: int,
    blocked: int,
    deferred: int,
) -> None:
    msg = BusMessage.new(
        from_id=state.agent_id,
        to=None,
        kind="goal_sync",
        payload=(
            f"goal=cycle_done "
            f"accepted={accepted} "
            f"blocked={blocked} "
            f"deferred={deferred} "
            f"apply_mode={state.apply_mode} "
            f"gov_mode={state.gov_mode}"
        ),
    )
    broadcast(bus, msg)


def render_bus_status(state: BotBusState) -> str:
    suspended_str = "YES ⚠️" if state.suspended else "no"
    return (
        f"  agent_id         : {state.agent_id}\n"
        f"  gov_mode         : {state.gov_mode}\n"
        f"  apply_mode       : {state.apply_mode}\n"
        f"  dry_run          : {state.dry_run}\n"
        f"  suspended        : {suspended_str}\n"
        f"  cycles_done      : {state.cycles_done}\n"
        f"  last_directive   : {state.last_directive_ts}\n"
        f"  last_heartbeat   : {state.last_heartbeat_ts}\n"
    )


# ── Event loop ────────────────────────────────────────────────────────────────

def run_bus_listener(
    bus_dir: Path,
    agent_id: str,
    poll_ms: int = 500,
    on_directive_change: Any | None = None,
) -> None:
    """
    Run the oo-bot bus event loop.

    Polls inbox + broadcast for governor directives and updates BotBusState.
    Calls `on_directive_change(state)` when state changes (for integration with
    the oo_prime engine).

    Runs until interrupted (Ctrl+C).

    Args:
        bus_dir: Path to the bus directory (contains inbox/, outbox/, broadcast.jsonl)
        agent_id: This bot's instance ID (e.g. "oo-bot")
        poll_ms: Poll interval in milliseconds
        on_directive_change: Optional callback(BotBusState) called on state change
    """
    bus = BusPaths(bus_dir, agent_id)
    bus.init_dirs()

    state = BotBusState(agent_id=agent_id)
    last_seen_ts: int = int(time.time()) - 5  # start from last 5s
    hb_interval_s = 30

    print(f"[oo-bot bus] Listening on {bus.inbox}")
    print(f"[oo-bot bus] Agent ID: {agent_id}")
    print(f"[oo-bot bus] Poll: {poll_ms}ms | Heartbeat: {hb_interval_s}s")

    # Initial heartbeat
    emit_heartbeat(bus, state)
    state.last_heartbeat_ts = int(time.time())

    try:
        while True:
            msgs = read_inbox(bus, since=last_seen_ts)
            new_msgs = [m for m in msgs if m.ts_epoch_s > last_seen_ts]

            if new_msgs:
                prev_state = BotBusState(**state.__dict__)
                logs = react_to_messages(state, new_msgs)
                for log in logs:
                    print(log)

                # Notify integration callback if state changed
                if (
                    on_directive_change
                    and (
                        state.apply_mode != prev_state.apply_mode
                        or state.suspended != prev_state.suspended
                    )
                ):
                    on_directive_change(state)

                # Update last seen
                last_seen_ts = max(m.ts_epoch_s for m in new_msgs)

            # Periodic heartbeat
            now = int(time.time())
            if now - state.last_heartbeat_ts >= hb_interval_s:
                emit_heartbeat(bus, state)
                state.last_heartbeat_ts = now

            time.sleep(poll_ms / 1000.0)

    except KeyboardInterrupt:
        print("\n[oo-bot bus] Shutting down")


# ── Sync helper for one-shot integration with run_cycles() ───────────────────

def sync_bus_once(
    bus_dir: Path,
    agent_id: str,
    since: int | None = None,
) -> BotBusState:
    """
    Read the bus once and return the current BotBusState.
    Used to check for directives before running a cycle.
    """
    bus = BusPaths(bus_dir, agent_id)
    bus.init_dirs()
    state = BotBusState(agent_id=agent_id)
    msgs = read_inbox(bus, since=since)
    react_to_messages(state, msgs)
    return state
