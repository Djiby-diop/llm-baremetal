from __future__ import annotations

from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from typing import Any


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


@dataclass(slots=True)
class ModuleHealth:
    name: str
    path: str
    has_readme: bool
    has_tests: bool
    has_policy: bool
    score: float
    signals: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(slots=True)
class Decision:
    action: str
    target: str
    reason: str
    severity: str
    created_at: str = field(default_factory=utc_now_iso)
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(slots=True)
class CycleReport:
    cycle_index: int
    started_at: str
    root: str
    observed_modules: list[ModuleHealth]
    accepted_decisions: list[Decision]
    blocked_decisions: list[Decision]
    simulation_gate: dict[str, Any]
    deferred_decisions: list[Decision] = field(default_factory=list)
    deferred_summary: dict[str, Any] = field(default_factory=dict)
    applied_count: int = 0
    skipped_apply_count: int = 0
    apply_delta: dict[str, int] = field(default_factory=dict)
    risk_budget: dict[str, Any] = field(default_factory=dict)
    state_summary: dict[str, Any] = field(default_factory=dict)
    ended_at: str = field(default_factory=utc_now_iso)

    def to_dict(self) -> dict[str, Any]:
        return {
            "cycle_index": self.cycle_index,
            "started_at": self.started_at,
            "ended_at": self.ended_at,
            "root": self.root,
            "observed_modules": [m.to_dict() for m in self.observed_modules],
            "accepted_decisions": [d.to_dict() for d in self.accepted_decisions],
            "blocked_decisions": [d.to_dict() for d in self.blocked_decisions],
            "deferred_decisions": [d.to_dict() for d in self.deferred_decisions],
            "deferred_summary": self.deferred_summary,
            "simulation_gate": self.simulation_gate,
            "applied_count": self.applied_count,
            "skipped_apply_count": self.skipped_apply_count,
            "apply_delta": self.apply_delta,
            "risk_budget": self.risk_budget,
            "state_summary": self.state_summary,
        }
