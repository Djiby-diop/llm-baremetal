from __future__ import annotations

from abc import ABC, abstractmethod

from ..adapters.base import BaseGenerationAdapter, GenerationRequest, GenerationResponse
from ..core.contracts.types import Task, WorkerResult


class BaseWorker(ABC):
    name: str

    def __init__(self, adapter: BaseGenerationAdapter) -> None:
        self._adapter = adapter

    @abstractmethod
    def execute(self, task: Task, prior_results: list[WorkerResult]) -> WorkerResult:
        raise NotImplementedError

    def _generate(
        self,
        task: Task,
        prior_results: list[WorkerResult],
        instructions: list[str],
    ) -> GenerationResponse:
        request = GenerationRequest(
            worker=self.name,
            task_goal=task.goal,
            mode=task.mode,
            instructions=instructions,
            prior_summaries=[result.summary for result in prior_results],
            context=task.context,
        )
        return self._adapter.generate(request)
