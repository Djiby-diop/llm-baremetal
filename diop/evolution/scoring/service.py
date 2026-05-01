from __future__ import annotations

from ...core.contracts.types import ValidationDecision, WorkerResult


class EvolutionScorer:
    def score(self, results: list[WorkerResult], validation: ValidationDecision) -> dict[str, object]:
        worker_scores = {}
        for result in results:
            base = 1.0
            base -= min(0.6, len(result.risks) * 0.15)
            if result.needs_validation:
                base -= 0.1
            worker_scores[result.worker] = round(max(0.0, base), 2)

        return {
            "validation_status": validation.status,
            "worker_scores": worker_scores,
            "recommended_mode": "lunar" if validation.status != "approved" else "solar",
        }
