from __future__ import annotations

from .config import PolicyConfig
from .types import Decision


def _risk_points(severity: str) -> int:
    return {"high": 5, "medium": 3, "low": 1}.get(severity, 2)


def split_safe_decisions(
    decisions: list[Decision],
    policy: PolicyConfig,
    simulation_ok: bool,
) -> tuple[list[Decision], list[Decision]]:
    accepted: list[Decision] = []
    blocked: list[Decision] = []

    for decision in decisions:
        if decision.action in policy.blocked_actions:
            decision.reason = f"blocked by policy action ban: {decision.action}"
            blocked.append(decision)
            continue

        if decision.action in policy.require_simulation_for and not simulation_ok:
            decision.reason = (
                f"requires simulation approval: action={decision.action}; "
                "run with --simulate-ok after simulation"
            )
            blocked.append(decision)
            continue

        if decision.target in policy.protected_targets and decision.action == "stabilize_module":
            decision.reason = (
                f"target '{decision.target}' is protected; only advisory decisions are allowed"
            )
            decision.severity = "medium"
            decision.action = "advisory_stabilize"

        accepted.append(decision)

    return accepted, blocked


def enforce_risk_budget(
    decisions: list[Decision],
    max_risk_points: int,
) -> tuple[list[Decision], list[Decision], int]:
    kept: list[Decision] = []
    blocked: list[Decision] = []
    used = 0

    for decision in decisions:
        points = _risk_points(decision.severity)
        if used + points > max_risk_points:
            decision.reason = (
                f"blocked by cycle risk budget: severity={decision.severity} points={points} "
                f"used={used} budget={max_risk_points}"
            )
            blocked.append(decision)
            continue

        kept.append(decision)
        used += points

    return kept, blocked, used
