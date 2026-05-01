from __future__ import annotations

import argparse
from pathlib import Path

from .config import load_policy
from .engine import run_cycles, write_report


def parse_action_cooldown_overrides(values: list[str]) -> dict[str, int]:
    overrides: dict[str, int] = {}
    for raw in values:
        text = str(raw).strip()
        if not text:
            continue
        if "=" not in text:
            raise ValueError(f"invalid action cooldown override '{text}', expected ACTION=VALUE")
        action, value = text.split("=", 1)
        action = action.strip()
        value = value.strip()
        if not action:
            raise ValueError(f"invalid action cooldown override '{text}', missing action")
        try:
            cooldown = int(value)
        except ValueError as exc:
            raise ValueError(f"invalid cooldown value '{value}' for action '{action}'") from exc
        overrides[action] = max(0, cooldown)
    return overrides


def simulation_gate_all_approved(reports: list) -> bool:
    return all(bool(getattr(r, "simulation_gate", {}).get("approved")) for r in reports)


def strict_apply_failed(total_applied: int, apply_enabled: bool, dry_run: bool) -> bool:
    if not apply_enabled or dry_run:
        return False
    return int(total_applied) <= 0


def strict_risk_failed(total_risk_blocked: int) -> bool:
    return int(total_risk_blocked) > 0


def _report_avg_score(report) -> float:
    modules = list(getattr(report, "observed_modules", []))
    if not modules:
        return 0.0
    scores = [float(getattr(module, "score", 0.0)) for module in modules]
    return sum(scores) / len(scores)


def health_guard_failure(
    reports: list,
    *,
    min_avg_score: float | None,
    max_drop: float | None,
) -> tuple[bool, str]:
    if not reports:
        return False, ""

    first_avg = _report_avg_score(reports[0])
    last_avg = _report_avg_score(reports[-1])
    drop = first_avg - last_avg

    if min_avg_score is not None and last_avg < float(min_avg_score):
        return True, (
            "health average below threshold: "
            f"avg={last_avg:.3f} threshold={float(min_avg_score):.3f}"
        )

    if max_drop is not None and drop > float(max_drop):
        return True, (
            "health drop above threshold: "
            f"drop={drop:.3f} max_drop={float(max_drop):.3f}"
        )

    return False, ""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="OO Prime governance bot (v0.3-C+)")
    parser.add_argument("--root", default="..", help="Repository root to observe")
    parser.add_argument("--policy", default="config/policy.json", help="Path to OO Prime policy JSON")
    parser.add_argument("--cycles", default=1, type=int, help="Number of governance cycles")
    parser.add_argument(
        "--output", default="reports/latest.json", help="Path to generated cycle report JSON"
    )
    parser.add_argument("--log", default="logs/oo-prime.log", help="Path to JSONL log")
    parser.add_argument("--state", default="state/oo-prime-state.json", help="Path to persistent state JSON")
    parser.add_argument("--module-limit", default=64, type=int, help="Maximum modules observed per cycle")
    parser.add_argument(
        "--simulation-proof",
        default="",
        help=(
            "Path to oo-sim simulation proof JSON. If omitted, defaults to "
            "<root>/oo-sim/reports/oo-prime-sim-ok.json"
        ),
    )
    parser.add_argument(
        "--simulation-scenario",
        default="",
        help=(
            "Path to oo-sim scenario JSON. If omitted, defaults to "
            "<root>/oo-sim/scenarios/oo-prime-gate.json"
        ),
    )
    parser.add_argument(
        "--simulation-max-age-minutes",
        default=60,
        type=int,
        help="Maximum accepted age for simulation proof",
    )
    parser.add_argument(
        "--auto-simulate",
        action="store_true",
        help="Run oo-sim before each cycle to refresh simulation proof",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Apply allowlisted decisions in configured scope",
    )
    parser.add_argument(
        "--apply-mode",
        choices=["safe", "confirmed"],
        default="safe",
        help="Execution mode for apply: safe=create-only, confirmed=allow updates",
    )
    parser.add_argument(
        "--max-deferred-reasons-in-log",
        default=None,
        type=int,
        help="Override policy max_deferred_reasons_in_log for this run",
    )
    parser.add_argument(
        "--target-cooldown-cycles",
        default=None,
        type=int,
        help="Override policy target_cooldown_cycles for this run",
    )
    parser.add_argument(
        "--repetition-threshold",
        default=None,
        type=int,
        help="Override policy repetition_threshold for this run",
    )
    parser.add_argument(
        "--action-cooldown",
        action="append",
        default=[],
        help="Override action cooldown for this run; repeatable ACTION=VALUE",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Run observation and decision pipeline without external side effects",
    )
    parser.add_argument(
        "--explain",
        action="store_true",
        help="Print compact cycle explanation (accepted/blocked/deferred summaries)",
    )
    parser.add_argument(
        "--strict-simulation-gate",
        action="store_true",
        help="Return non-zero exit code if any cycle simulation gate is not approved",
    )
    parser.add_argument(
        "--strict-apply",
        action="store_true",
        help="Return non-zero exit code if apply mode is enabled but no action is applied",
    )
    parser.add_argument(
        "--strict-risk",
        action="store_true",
        help="Return non-zero exit code if any decision is blocked by risk budget",
    )
    parser.add_argument(
        "--strict-health-min-score",
        default=None,
        type=float,
        help="Return non-zero exit code if final observed average score is below this value",
    )
    parser.add_argument(
        "--strict-health-max-drop",
        default=None,
        type=float,
        help="Return non-zero exit code if avg score drop between first and last cycle exceeds this value",
    )
    parser.add_argument(
        "--strict-all",
        action="store_true",
        help="Enable all strict checks (simulation gate, apply, risk, health)",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()

    root = Path(args.root).resolve()
    policy_path = Path(args.policy).resolve()
    output_path = Path(args.output).resolve()
    log_path = Path(args.log).resolve()
    state_path = Path(args.state).resolve()
    proof_path = Path(args.simulation_proof).resolve() if str(args.simulation_proof).strip() else None
    scenario_path = (
        Path(args.simulation_scenario).resolve() if str(args.simulation_scenario).strip() else None
    )

    policy = load_policy(policy_path)
    if args.max_deferred_reasons_in_log is not None:
        policy.max_deferred_reasons_in_log = max(0, int(args.max_deferred_reasons_in_log))
    if args.target_cooldown_cycles is not None:
        policy.target_cooldown_cycles = max(0, int(args.target_cooldown_cycles))
    if args.repetition_threshold is not None:
        policy.repetition_threshold = max(0, int(args.repetition_threshold))
    try:
        policy.action_cooldown_by_action.update(parse_action_cooldown_overrides(list(args.action_cooldown)))
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    strict_simulation_gate = bool(args.strict_simulation_gate or args.strict_all)
    strict_apply = bool(args.strict_apply or args.strict_all)
    strict_risk = bool(args.strict_risk or args.strict_all)
    strict_health_min = args.strict_health_min_score if args.strict_health_min_score is not None else (
        0.45 if args.strict_all else None
    )
    strict_health_drop = args.strict_health_max_drop if args.strict_health_max_drop is not None else (
        0.20 if args.strict_all else None
    )

    reports = run_cycles(
        root=root,
        policy=policy,
        cycles=max(1, int(args.cycles)),
        simulation_proof=proof_path,
        simulation_scenario=scenario_path,
        simulation_max_age_minutes=max(1, int(args.simulation_max_age_minutes)),
        auto_simulate=bool(args.auto_simulate),
        apply_mode=bool(args.apply) and not bool(args.dry_run),
        apply_execution_mode=str(args.apply_mode),
        state_path=state_path,
        log_path=log_path,
        module_limit=max(1, int(args.module_limit)),
    )
    write_report(output_path, reports)

    total_observed = sum(len(r.observed_modules) for r in reports)
    total_accepted = sum(len(r.accepted_decisions) for r in reports)
    total_blocked = sum(len(r.blocked_decisions) for r in reports)
    total_applied = sum(r.applied_count for r in reports)
    total_skipped_apply = sum(r.skipped_apply_count for r in reports)
    total_created = sum(int(r.apply_delta.get("created", 0)) for r in reports)
    total_updated = sum(int(r.apply_delta.get("updated", 0)) for r in reports)
    total_noop = sum(int(r.apply_delta.get("noop", 0)) for r in reports)
    total_risk_used = sum(int(r.risk_budget.get("used", 0)) for r in reports)
    total_risk_blocked = sum(int(r.risk_budget.get("blocked", 0)) for r in reports)
    final_state = reports[-1].state_summary if reports else {}
    first_avg = _report_avg_score(reports[0]) if reports else 0.0
    last_avg = _report_avg_score(reports[-1]) if reports else 0.0
    health_drop = first_avg - last_avg

    simulation_gate_approved = simulation_gate_all_approved(reports)

    mode = "dry-run" if args.dry_run else "advisory"
    print(
        "OO Prime completed "
        f"({mode}): cycles={len(reports)} observed={total_observed} "
        f"accepted={total_accepted} blocked={total_blocked} "
        f"simulation_ok={simulation_gate_approved}"
    )
    if args.apply and not args.dry_run:
        print(f"Applied: {total_applied} (skipped by allowlist/scope: {total_skipped_apply})")
        print(f"Apply delta: created={total_created} updated={total_updated} noop={total_noop}")
    print(f"Risk budget: used={total_risk_used} blocked={total_risk_blocked}")
    print(
        "State: "
        f"cycles={int(final_state.get('cycles_run', 0))} "
        f"tracked_actions={int(final_state.get('tracked_actions', 0))}"
    )
    print(f"Deferred reason cap: {int(policy.max_deferred_reasons_in_log)}")
    print(f"Target cooldown: {int(policy.target_cooldown_cycles)} cycles")
    print(f"Repetition threshold: {int(policy.repetition_threshold)}")
    print(f"Action cooldown map size: {len(policy.action_cooldown_by_action)}")
    print(f"Health avg: first={first_avg:.3f} last={last_avg:.3f} drop={health_drop:.3f}")
    if args.explain and reports:
        last = reports[-1]
        print(
            "Explain: "
            f"accepted={len(last.accepted_decisions)} "
            f"blocked={len(last.blocked_decisions)} "
            f"deferred={len(last.deferred_decisions)}"
        )
        by_rule = last.deferred_summary.get("by_rule", {}) if isinstance(last.deferred_summary, dict) else {}
        if isinstance(by_rule, dict) and by_rule:
            summary = ", ".join(f"{k}={int(v)}" for k, v in sorted(by_rule.items()))
            print(f"Explain deferred by rule: {summary}")
        top_actions = (
            last.deferred_summary.get("top_actions", [])
            if isinstance(last.deferred_summary, dict)
            else []
        )
        if isinstance(top_actions, list) and top_actions:
            compact = ", ".join(
                f"{str(item.get('action', ''))}:{int(item.get('count', 0))}"
                for item in top_actions[:3]
            )
            print(f"Explain top deferred actions: {compact}")
    print(f"Report: {output_path}")
    print(f"Log: {log_path}")
    print(f"State file: {state_path}")

    if strict_simulation_gate and not simulation_gate_approved:
        first_failed = next((r for r in reports if not r.simulation_gate.get("approved")), None)
        if first_failed is not None:
            print(
                "Strict gate failure: "
                f"source={first_failed.simulation_gate.get('source', '')} "
                f"reason={first_failed.simulation_gate.get('reason', '')}"
            )
        return 2

    if strict_apply and strict_apply_failed(
        total_applied,
        apply_enabled=bool(args.apply),
        dry_run=bool(args.dry_run),
    ):
        print(
            "Strict apply failure: "
            f"applied={int(total_applied)} skipped_apply={int(total_skipped_apply)}"
        )
        return 3

    if strict_risk and strict_risk_failed(total_risk_blocked):
        print(
            "Strict risk failure: "
            f"risk_blocked={int(total_risk_blocked)}"
        )
        return 4

    health_failed, health_reason = health_guard_failure(
        reports,
        min_avg_score=strict_health_min,
        max_drop=strict_health_drop,
    )
    if health_failed:
        print(f"Strict health failure: {health_reason}")
        return 5

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
