from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(slots=True)
class PolicyConfig:
    min_health_score: float = 0.55
    max_decisions_per_cycle: int = 8
    blocked_actions: set[str] = field(default_factory=lambda: {"delete_module", "rewrite_genome"})
    require_simulation_for: set[str] = field(
        default_factory=lambda: {"evolve_policy", "reallocate_critical_resource", "stabilize_module"}
    )
    protected_targets: set[str] = field(default_factory=lambda: {"OS-G (Operating System Genesis)", "oo-guard"})
    apply_allowlist: set[str] = field(
        default_factory=lambda: {
            "propose_docs",
            "propose_test_scaffold",
            "advisory_stabilize",
            "propose_system_action_plan",
            "propose_osg_action_plan",
            "propose_module_doc_patch",
            "propose_module_test_scaffold",
        }
    )
    apply_target_prefixes: tuple[str, ...] = ()
    reminder_targets: tuple[str, ...] = ("OS-G (Operating System Genesis)",)
    apply_default_mode: str = "safe"
    max_risk_points_per_cycle: int = 10
    target_cooldown_cycles: int = 1
    action_cooldown_by_action: dict[str, int] = field(default_factory=lambda: {"propose_system_action_plan": 2})
    repetition_threshold: int = 2
    repetition_statuses: set[str] = field(default_factory=lambda: {"blocked", "noop", "skipped"})
    max_deferred_reasons_in_log: int = 10


def load_policy(path: Path) -> PolicyConfig:
    if not path.exists():
        return PolicyConfig()

    raw = json.loads(path.read_text(encoding="utf-8"))
    return PolicyConfig(
        min_health_score=float(raw.get("min_health_score", 0.55)),
        max_decisions_per_cycle=int(raw.get("max_decisions_per_cycle", 8)),
        blocked_actions=set(raw.get("blocked_actions", ["delete_module", "rewrite_genome"])),
        require_simulation_for=set(
            raw.get(
                "require_simulation_for",
                ["evolve_policy", "reallocate_critical_resource", "stabilize_module"],
            )
        ),
        protected_targets=set(raw.get("protected_targets", ["OS-G (Operating System Genesis)", "oo-guard"])),
        apply_allowlist=set(
            raw.get(
                "apply_allowlist",
                [
                    "propose_docs",
                    "propose_test_scaffold",
                    "advisory_stabilize",
                    "propose_system_action_plan",
                    "propose_osg_action_plan",
                    "propose_module_doc_patch",
                    "propose_module_test_scaffold",
                ],
            )
        ),
        apply_target_prefixes=tuple(raw.get("apply_target_prefixes", [])),
        reminder_targets=tuple(raw.get("reminder_targets", ["OS-G (Operating System Genesis)"])),
        apply_default_mode=str(raw.get("apply_default_mode", "safe")),
        max_risk_points_per_cycle=int(raw.get("max_risk_points_per_cycle", 10)),
        target_cooldown_cycles=int(raw.get("target_cooldown_cycles", 1)),
        action_cooldown_by_action={
            str(k): int(v)
            for k, v in dict(raw.get("action_cooldown_by_action", {"propose_system_action_plan": 2})).items()
        },
        repetition_threshold=int(raw.get("repetition_threshold", 2)),
        repetition_statuses=set(raw.get("repetition_statuses", ["blocked", "noop", "skipped"])),
        max_deferred_reasons_in_log=int(raw.get("max_deferred_reasons_in_log", 10)),
    )
