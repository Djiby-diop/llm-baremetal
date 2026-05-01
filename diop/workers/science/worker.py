from __future__ import annotations

import urllib.request
import urllib.parse
import json

from ...core.contracts.types import Task, WorkerResult
from ..base import BaseWorker


class ScienceWorker(BaseWorker):
    name = "science"

    def execute(self, task: Task, prior_results: list[WorkerResult]) -> WorkerResult:
        instructions = list(task.constraints)
        
        # Simulating Auto-Apprentissage via Web (using a public search API like DuckDuckGo)
        # Note: In a robust environment, this would use a dedicated API key or Search Tool.
        search_query = task.goal
        try:
            # We add a simulated or basic web-fetched context to simulate external model/net consultation
            instructions.append(f"EXTERNAL CONTEXT: When resolving this, simulate pulling the latest standard practices regarding '{search_query}'. Integrate this external knowledge into your artifacts.")
            # In a fully connected version, we'd do urllib.request to a search endpoint and append the raw text.
        except Exception:
            pass

        generated = self._generate(
            task=task,
            prior_results=prior_results,
            instructions=instructions,
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
