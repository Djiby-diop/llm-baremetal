from __future__ import annotations

from ...core.contracts.types import Task, WorkerResult
from ..base import BaseWorker


class RefactorWorker(BaseWorker):
    name = "refactor"

    def execute(self, task: Task, prior_results: list[WorkerResult]) -> WorkerResult:
        generated = self._generate(
            task=task,
            prior_results=prior_results,
            instructions=task.constraints,
        )
        return WorkerResult(
            task_id=task.id,
            worker=self.name,
            status="completed",
            summary=generated.summary,
            artifacts=generated.artifacts,
            risks=generated.risks,
            recommendations=generated.recommendations,
            needs_validation=True,
            metadata=generated.metadata,
        )
