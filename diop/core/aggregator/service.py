from __future__ import annotations

from ..contracts.types import WorkerResult


class Aggregator:
    def aggregate(self, goal: str, results: list[WorkerResult]) -> dict[str, object]:
        summaries = [result.summary for result in results]
        artifacts: list[dict[str, object]] = []
        risks: list[str] = []
        recommendations: list[str] = []

        for result in results:
            artifacts.extend(result.artifacts)
            risks.extend(result.risks)
            recommendations.extend(result.recommendations)

        return {
            "goal": goal,
            "summary": " | ".join(summaries),
            "artifacts": artifacts,
            "risks": sorted(set(risks)),
            "recommendations": sorted(set(recommendations)),
        }
