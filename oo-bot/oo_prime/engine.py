from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .bus_bridge import BusPaths, BotBusState, emit_cycle_report, emit_heartbeat, sync_bus_once
from .config import PolicyConfig
from .executor import apply_decisions
from .governance import decide_with_trace
from .observer import observe_modules
from .security import enforce_risk_budget, split_safe_decisions
from .simulation import evaluate_oo_sim_gate, run_oo_sim_and_write_proof
from .state import load_state, summarize_state, update_state
from .types import CycleReport, utc_now_iso


def _append_jsonl(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(payload, ensure_ascii=False) + "\n")


def _defer_category(reason: str) -> str:
    if not reason:
        return "unknown"
    head = reason.split(":", 1)[0].strip().lower()
    if "action cooldown" in head:
        return "action_cooldown"
    if "target cooldown" in head:
        return "target_cooldown"
    if "repetition guard" in head:
        return "repetition_guard"
    return "other"


def _summarize_deferred(decisions: list) -> dict:
    by_rule: Counter[str] = Counter()
    by_action: Counter[str] = Counter()
    by_target: Counter[str] = Counter()
    for decision in decisions:
        by_rule[_defer_category(decision.reason)] += 1
        by_action[decision.action] += 1
        by_target[decision.target] += 1
    return {
        "total": len(decisions),
        "by_rule": dict(by_rule),
        "top_actions": [
            {"action": action, "count": count}
            for action, count in by_action.most_common(5)
        ],
        "top_targets": [
            {"target": target, "count": count}
            for target, count in by_target.most_common(5)
        ],
    }


def _health_trend(reports: list[CycleReport]) -> dict:
    per_cycle: list[dict] = []
    all_scores: list[float] = []
    for report in reports:
        scores = [float(m.score) for m in report.observed_modules]
        all_scores.extend(scores)
        if not scores:
            per_cycle.append(
                {
                    "cycle": int(report.cycle_index),
                    "module_count": 0,
                    "avg_score": 0.0,
                    "min_score": 0.0,
                    "max_score": 0.0,
                }
            )
            continue
        per_cycle.append(
            {
                "cycle": int(report.cycle_index),
                "module_count": len(scores),
                "avg_score": round(sum(scores) / len(scores), 3),
                "min_score": round(min(scores), 3),
                "max_score": round(max(scores), 3),
            }
        )

    if not all_scores:
        return {
            "cycles": len(reports),
            "avg_observed_score": 0.0,
            "min_observed_score": 0.0,
            "max_observed_score": 0.0,
            "per_cycle": per_cycle,
        }

    return {
        "cycles": len(reports),
        "avg_observed_score": round(sum(all_scores) / len(all_scores), 3),
        "min_observed_score": round(min(all_scores), 3),
        "max_observed_score": round(max(all_scores), 3),
        "per_cycle": per_cycle,
    }


def run_cycles(
    root: Path,
    policy: PolicyConfig,
    cycles: int,
    simulation_proof: Path | None,
    simulation_scenario: Path | None,
    simulation_max_age_minutes: int,
    auto_simulate: bool,
    apply_mode: bool,
    apply_execution_mode: str,
    state_path: Path,
    log_path: Path,
    module_limit: int,
    bus_dir: Path | None = None,
    bus_agent_id: str = "oo-bot",
) -> list[CycleReport]:
    reports: list[CycleReport] = []
    state = load_state(state_path)

    # Bus integration (optional — only if bus_dir provided)
    bus: BusPaths | None = None
    bot_state: BotBusState | None = None
    if bus_dir is not None:
        bus = BusPaths(bus_dir, bus_agent_id)
        bus.init_dirs()
        # Check for governor directives before starting
        bot_state = sync_bus_once(bus_dir, bus_agent_id)
        if bot_state.suspended:
            _append_jsonl(log_path, {
                "event": "governor_suspended",
                "agent_id": bus_agent_id,
                "gov_mode": bot_state.gov_mode,
            })
            return reports  # abort all cycles
        # Override apply_mode if governor says observe/off
        if bot_state.apply_mode == "off":
            apply_mode = False
        emit_heartbeat(bus, bot_state)

    for i in range(cycles):
        started_at = utc_now_iso()
        modules = observe_modules(root, limit=module_limit)
        proposed, deferred = decide_with_trace(
            modules,
            policy,
            prioritize_target_prefixes=policy.reminder_targets if apply_mode else (),
            memory_state=state,
        )

        if auto_simulate:
            run_oo_sim_and_write_proof(
                root=root,
                proof_path=simulation_proof,
                scenario_path=simulation_scenario,
                mode="normal",
                timeout_seconds=30,
            )

        gate = evaluate_oo_sim_gate(
            root=root,
            proof_path=simulation_proof,
            max_age_minutes=simulation_max_age_minutes,
        )

        accepted, blocked = split_safe_decisions(proposed, policy, simulation_ok=gate.approved)
        accepted, risk_blocked, risk_used = enforce_risk_budget(accepted, policy.max_risk_points_per_cycle)
        blocked.extend(risk_blocked)

        applied_count = 0
        skipped_apply_count = 0
        apply_delta = {"created": 0, "updated": 0, "noop": 0}
        decision_outcomes: list[dict] = []
        if apply_mode:
            applied_count, skipped_apply_count, apply_delta, decision_outcomes = apply_decisions(
                root,
                accepted,
                policy,
                apply_mode=apply_execution_mode,
            )
        else:
            decision_outcomes = [
                {"action": decision.action, "target": decision.target, "status": "accepted"}
                for decision in accepted
            ]

        state = update_state(state_path, state, decision_outcomes, blocked)

        report = CycleReport(
            cycle_index=i + 1,
            started_at=started_at,
            root=str(root),
            observed_modules=modules,
            accepted_decisions=accepted,
            blocked_decisions=blocked,
            deferred_decisions=deferred,
            deferred_summary=_summarize_deferred(deferred),
            simulation_gate={
                "approved": gate.approved,
                "source": gate.source,
                "reason": gate.reason,
            },
            applied_count=applied_count,
            skipped_apply_count=skipped_apply_count,
            apply_delta=apply_delta,
            risk_budget={
                "max": policy.max_risk_points_per_cycle,
                "used": risk_used,
                "blocked": len(risk_blocked),
            },
            state_summary=summarize_state(state),
            ended_at=utc_now_iso(),
        )
        reports.append(report)

        # Emit cycle report to bus (Phase M.3)
        if bus is not None and bot_state is not None:
            bot_state.cycles_done += 1
            emit_cycle_report(
                bus,
                bot_state,
                accepted=len(report.accepted_decisions),
                blocked=len(report.blocked_decisions),
                deferred=len(report.deferred_decisions),
            )

        _append_jsonl(
            log_path,
            {
                "event": "oo_prime_cycle",
                "cycle": report.cycle_index,
                "started_at": report.started_at,
                "ended_at": report.ended_at,
                "observed": len(report.observed_modules),
                "accepted": len(report.accepted_decisions),
                "blocked": len(report.blocked_decisions),
                "deferred": len(report.deferred_decisions),
                "simulation_gate": report.simulation_gate,
                "applied": report.applied_count,
                "skipped_apply": report.skipped_apply_count,
                "apply_delta": report.apply_delta,
                "risk_budget": report.risk_budget,
                "state_summary": report.state_summary,
                "deferred_summary": report.deferred_summary,
                "deferred_reasons": [
                    {"action": decision.action, "target": decision.target, "reason": decision.reason}
                    for decision in report.deferred_decisions[
                        : max(0, int(policy.max_deferred_reasons_in_log))
                    ]
                ],
            },
        )

    return reports


def write_report(path: Path, reports: list[CycleReport]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": "oo-prime-report-v1",
        "generated_at": utc_now_iso(),
        "health_trend": _health_trend(reports),
        "cycles": [r.to_dict() for r in reports],
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
