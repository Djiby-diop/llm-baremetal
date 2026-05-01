from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from typing import Any
from uuid import uuid4


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def new_id(prefix: str) -> str:
    return f"{prefix}_{uuid4().hex[:10]}"


@dataclass(slots=True)
class Task:
    id: str
    kind: str
    goal: str
    worker: str
    mode: str
    context: dict[str, Any] = field(default_factory=dict)
    constraints: list[str] = field(default_factory=list)
    priority: str = "medium"
    depends_on: list[str] = field(default_factory=list)
    created_at: str = field(default_factory=utc_now_iso)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(slots=True)
class WorkerResult:
    task_id: str
    worker: str
    status: str
    summary: str
    artifacts: list[dict[str, Any]] = field(default_factory=list)
    risks: list[str] = field(default_factory=list)
    recommendations: list[str] = field(default_factory=list)
    needs_validation: bool = False
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(slots=True)
class ValidationDecision:
    status: str
    reviewer: str
    rationale: str
    changes_requested: list[str] = field(default_factory=list)
    created_at: str = field(default_factory=utc_now_iso)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(slots=True)
class MemoryRecord:
    id: str
    category: str
    content: dict[str, Any]
    created_at: str = field(default_factory=utc_now_iso)
    tags: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(slots=True)
class ExecutionReport:
    run_id: str
    goal: str
    mode: str
    tasks: list[Task]
    results: list[WorkerResult]
    validation: ValidationDecision
    evolution_signals: dict[str, Any]
    memory_records: list[MemoryRecord]
    created_at: str = field(default_factory=utc_now_iso)

    def to_dict(self) -> dict[str, Any]:
        return {
            "run_id": self.run_id,
            "goal": self.goal,
            "mode": self.mode,
            "created_at": self.created_at,
            "tasks": [task.to_dict() for task in self.tasks],
            "results": [result.to_dict() for result in self.results],
            "validation": self.validation.to_dict(),
            "evolution_signals": self.evolution_signals,
            "memory_records": [record.to_dict() for record in self.memory_records],
        }
