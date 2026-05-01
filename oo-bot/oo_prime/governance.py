from __future__ import annotations

from .config import PolicyConfig
from .planner import build_system_action_plan
from .state import decision_priority, should_defer_decision
from .types import Decision, ModuleHealth


def _is_target_prefixed(target: str, prefixes: tuple[str, ...]) -> bool:
    lowered = target.lower()
    return any(lowered.startswith(p.lower()) for p in prefixes)


def _apply_memory_policy(
    decisions: list[Decision],
    policy: PolicyConfig,
    memory_state: dict | None,
) -> tuple[list[Decision], list[Decision]]:
    selected: list[Decision] = []
    deferred_decisions: list[Decision] = []
    for decision in decisions:
        deferred, reason = should_defer_decision(
            decision,
            memory_state,
            target_cooldown_cycles=policy.target_cooldown_cycles,
            action_cooldown_by_action=policy.action_cooldown_by_action,
            repetition_threshold=policy.repetition_threshold,
            repetition_statuses=policy.repetition_statuses,
        )
        if deferred:
            decision.reason = reason
            deferred_decisions.append(decision)
            continue
        selected.append(decision)
    return selected, deferred_decisions


def decide_with_trace(
    modules: list[ModuleHealth],
    policy: PolicyConfig,
    prioritize_target_prefixes: tuple[str, ...] = (),
    memory_state: dict | None = None,
) -> tuple[list[Decision], list[Decision]]:
    decisions: list[Decision] = []
    deferred_decisions: list[Decision] = []

    weakest = sorted(modules, key=lambda m: m.score)

    for module in weakest:
        if module.score < policy.min_health_score:
            decisions.append(
                Decision(
                    action="stabilize_module",
                    target=module.name,
                    severity="high",
                    reason=(
                        f"health score {module.score:.3f} below threshold {policy.min_health_score:.3f}; "
                        f"signals={','.join(module.signals) if module.signals else 'none'}"
                    ),
                    metadata={"score": module.score, "signals": module.signals, "module_path": module.path},
                )
            )

        if not module.has_tests:
            decisions.append(
                Decision(
                    action="propose_module_test_scaffold",
                    target=module.name,
                    severity="medium",
                    reason="module has no tests; create minimal local test scaffold",
                    metadata={"module_path": module.path},
                )
            )
            decisions.append(
                Decision(
                    action="propose_test_scaffold",
                    target=module.name,
                    severity="medium",
                    reason="module has no tests; add minimal smoke tests",
                    metadata={"module_path": module.path},
                )
            )

        if not module.has_readme:
            decisions.append(
                Decision(
                    action="propose_module_doc_patch",
                    target=module.name,
                    severity="medium",
                    reason="module has no README.md; create module baseline documentation",
                    metadata={"module_path": module.path},
                )
            )
            decisions.append(
                Decision(
                    action="propose_docs",
                    target=module.name,
                    severity="low",
                    reason="module has no README.md; add purpose + interfaces + runbook",
                    metadata={"module_path": module.path},
                )
            )

    decisions.sort(
        key=lambda d: (
            {"high": 0, "medium": 1, "low": 2}.get(d.severity, 3),
            -decision_priority(d, memory_state),
            d.target,
            d.action,
        )
    )
    selected, deferred_from_base = _apply_memory_policy(decisions, policy, memory_state)
    deferred_decisions.extend(deferred_from_base)
    selected = selected[: policy.max_decisions_per_cycle]

    if prioritize_target_prefixes:
        reminder_target = prioritize_target_prefixes[0]
        plan = build_system_action_plan(
            target="SYSTEM",
            modules=modules,
            reminder_targets=prioritize_target_prefixes,
        )
        plan_decision = Decision(
            action="propose_system_action_plan",
            target="SYSTEM",
            severity="medium",
            reason="generate system-wide docs/tests/policy action plan (v0.2)",
            metadata={"plan": plan},
        )
        deferred_plan, reason = should_defer_decision(
            plan_decision,
            memory_state,
            target_cooldown_cycles=policy.target_cooldown_cycles,
            action_cooldown_by_action=policy.action_cooldown_by_action,
            repetition_threshold=policy.repetition_threshold,
            repetition_statuses=policy.repetition_statuses,
        )
        if not deferred_plan:
            selected = [plan_decision, *selected][: policy.max_decisions_per_cycle]
        else:
            plan_decision.reason = reason
            deferred_decisions.append(plan_decision)

        has_targeted = any(_is_target_prefixed(d.target, prioritize_target_prefixes) for d in selected)
        if not has_targeted:
            reminder_decision = Decision(
                action="propose_docs",
                target=reminder_target,
                severity="low",
                reason="ensure OS-G governance heartbeat action in apply mode",
            )
            deferred_reminder, reason = should_defer_decision(
                reminder_decision,
                memory_state,
                target_cooldown_cycles=policy.target_cooldown_cycles,
                action_cooldown_by_action=policy.action_cooldown_by_action,
                repetition_threshold=policy.repetition_threshold,
                repetition_statuses=policy.repetition_statuses,
            )
            if not deferred_reminder:
                selected = [reminder_decision, *selected][: policy.max_decisions_per_cycle]
            else:
                reminder_decision.reason = reason
                deferred_decisions.append(reminder_decision)

    return selected, deferred_decisions


def decide(
    modules: list[ModuleHealth],
    policy: PolicyConfig,
    prioritize_target_prefixes: tuple[str, ...] = (),
    memory_state: dict | None = None,
) -> list[Decision]:
    selected, _ = decide_with_trace(
        modules,
        policy,
        prioritize_target_prefixes=prioritize_target_prefixes,
        memory_state=memory_state,
    )
    return selected
