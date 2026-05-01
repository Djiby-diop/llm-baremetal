from __future__ import annotations

from ...workers.base import BaseWorker
from ..contracts.types import Task


class Dispatcher:
    def __init__(self, workers: dict[str, BaseWorker]) -> None:
        self._workers = workers

    def resolve(self, task: Task) -> BaseWorker:
        try:
            return self._workers[task.worker]
        except KeyError as exc:
            raise ValueError(f"No worker registered for '{task.worker}'") from exc
