from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .types import Decision, utc_now_iso


def _default_state() -> dict[str, Any]:
    return {
        "schema": "oo-prime-state-v1",
        "cycles_run": 0,
        "action_stats": {},
        "target_stats": {},
        "target_memory": {},
        "decision_memory": {},
        "history": [],
    }


def load_state(path: Path) -> dict[str, Any]:
    if not path.exists():
        return _default_state()

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return _default_state()

    state = _default_state()
    state.update(payload if isinstance(payload, dict) else {})
    return state


def decision_priority(decision: Decision, state: dict[str, Any] | None) -> float:
    if not state:
        return 0.0

    action_stats = state.get("action_stats", {}).get(decision.action, {})
    target_stats = state.get("target_stats", {}).get(decision.target, {})

    def _score(stats: dict[str, Any]) -> float:
        created = float(stats.get("created", 0))
        updated = float(stats.get("updated", 0))
        recorded = float(stats.get("recorded", 0))
        accepted = float(stats.get("accepted", 0))
        noop = float(stats.get("noop", 0))
        blocked = float(stats.get("blocked", 0))
        return (created * 1.5) + (updated * 1.25) + (recorded * 0.5) + (accepted * 0.2) - (noop * 0.6) - (blocked * 0.8)

    return _score(action_stats) + (_score(target_stats) * 0.35)


def _decision_key(action: str, target: str) -> str:
    return f"{action}::{target}"


def should_defer_decision(
    decision: Decision,
    state: dict[str, Any] | None,
    *,
    target_cooldown_cycles: int,
    action_cooldown_by_action: dict[str, int] | None = None,
    repetition_threshold: int,
    repetition_statuses: set[str],
) -> tuple[bool, str]:
    if not state:
        return False, ""

    next_cycle = int(state.get("cycles_run", 0)) + 1

    if action_cooldown_by_action:
        action_cooldown = int(action_cooldown_by_action.get(decision.action, 0))
        if action_cooldown > 0:
            memory = state.get("decision_memory", {}).get(_decision_key(decision.action, decision.target), {})
            last_cycle = int(memory.get("last_cycle", 0))
            if last_cycle:
                gap = next_cycle - last_cycle
                if gap <= action_cooldown:
                    return True, (
                        f"deferred by action cooldown: action={decision.action} target={decision.target} "
                        f"gap={gap} cooldown={action_cooldown}"
                    )

    if target_cooldown_cycles > 0:
        target_memory = state.get("target_memory", {}).get(decision.target, {})
        last_cycle = int(target_memory.get("last_cycle", 0))
        if last_cycle:
            gap = next_cycle - last_cycle
            if gap <= target_cooldown_cycles:
                return True, (
                    f"deferred by target cooldown: target={decision.target} "
                    f"gap={gap} cooldown={target_cooldown_cycles}"
                )

    if repetition_threshold > 0:
        memory = state.get("decision_memory", {}).get(_decision_key(decision.action, decision.target), {})
        last_status = str(memory.get("last_status", "")).strip()
        repeat_count = int(memory.get("repeat_count", 0))
        if last_status in repetition_statuses and repeat_count >= repetition_threshold:
            return True, (
                f"deferred by repetition guard: action={decision.action} target={decision.target} "
                f"status={last_status} repeat_count={repeat_count}"
            )

    return False, ""


def summarize_state(state: dict[str, Any]) -> dict[str, Any]:
    action_stats = state.get("action_stats", {})
    return {
        "cycles_run": int(state.get("cycles_run", 0)),
        "tracked_actions": len(action_stats),
        "tracked_targets": len(state.get("target_stats", {})),
        "decision_memory_entries": len(state.get("decision_memory", {})),
        "target_memory_entries": len(state.get("target_memory", {})),
        "history_entries": len(state.get("history", [])),
    }


def update_state(
    path: Path,
    state: dict[str, Any],
    decision_outcomes: list[dict[str, Any]],
    blocked_decisions: list[Decision],
) -> dict[str, Any]:
    def _bump(bucket: dict[str, Any], key: str) -> None:
        bucket[key] = int(bucket.get(key, 0)) + 1

    action_stats = state.setdefault("action_stats", {})
    target_stats = state.setdefault("target_stats", {})
    target_memory = state.setdefault("target_memory", {})
    decision_memory = state.setdefault("decision_memory", {})
    current_cycle = int(state.get("cycles_run", 0)) + 1

    def _remember(action: str, target: str, status: str) -> None:
        if not action or not target or not status:
            return

        target_bucket = target_memory.setdefault(target, {})
        previous_target_action = str(target_bucket.get("last_action", "")).strip()
        previous_target_cycle = int(target_bucket.get("last_cycle", 0))
        previous_target_repeat = int(target_bucket.get("repeat_count", 0))
        target_bucket["last_cycle"] = current_cycle
        target_bucket["last_action"] = action
        target_bucket["repeat_count"] = (
            previous_target_repeat + 1
            if previous_target_action == action and previous_target_cycle == current_cycle - 1
            else 1
        )

        decision_key = _decision_key(action, target)
        decision_bucket = decision_memory.setdefault(decision_key, {})
        previous_status = str(decision_bucket.get("last_status", "")).strip()
        previous_cycle = int(decision_bucket.get("last_cycle", 0))
        previous_repeat = int(decision_bucket.get("repeat_count", 0))
        decision_bucket["action"] = action
        decision_bucket["target"] = target
        decision_bucket["last_cycle"] = current_cycle
        decision_bucket["last_status"] = status
        decision_bucket["repeat_count"] = (
            previous_repeat + 1
            if previous_status == status and previous_cycle == current_cycle - 1
            else 1
        )

    for outcome in decision_outcomes:
        action = str(outcome.get("action", "")).strip()
        target = str(outcome.get("target", "")).strip()
        status = str(outcome.get("status", "accepted")).strip()
        if not action or not target:
            continue

        action_bucket = action_stats.setdefault(action, {})
        target_bucket = target_stats.setdefault(target, {})
        _bump(action_bucket, status)
        _bump(target_bucket, status)
        _remember(action, target, status)

    for decision in blocked_decisions:
        action_bucket = action_stats.setdefault(decision.action, {})
        target_bucket = target_stats.setdefault(decision.target, {})
        _bump(action_bucket, "blocked")
        _bump(target_bucket, "blocked")
        _remember(decision.action, decision.target, "blocked")

    state["cycles_run"] = current_cycle
    history = state.setdefault("history", [])
    summary = {
        "timestamp": utc_now_iso(),
        "accepted": len(decision_outcomes),
        "blocked": len(blocked_decisions),
        "created": sum(1 for item in decision_outcomes if item.get("status") == "created"),
        "updated": sum(1 for item in decision_outcomes if item.get("status") == "updated"),
        "noop": sum(1 for item in decision_outcomes if item.get("status") == "noop"),
    }
    history.append(summary)
    state["history"] = history[-50:]

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return state
