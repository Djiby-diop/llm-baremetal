from __future__ import annotations

from .base import BaseGenerationAdapter, GenerationRequest, GenerationResponse


class MockGenerationAdapter(BaseGenerationAdapter):
    name = "mock"

    def generate(self, request: GenerationRequest) -> GenerationResponse:
        artifacts = self._artifacts_for(request)
        risks = self._risks_for(request)
        recommendations = self._recommendations_for(request)
        return GenerationResponse(
            summary=self._summary_for(request),
            artifacts=artifacts,
            risks=risks,
            recommendations=recommendations,
            metadata={
                "adapter": self.name,
                "prior_summary_count": len(request.prior_summaries),
            },
        )

    def _summary_for(self, request: GenerationRequest) -> str:
        if request.worker == "strategy":
            return f"Assessed strategy for '{request.task_goal}' with a deterministic fallback."
        if request.worker == "planner":
            return f"Built a fallback execution plan for '{request.task_goal}'."
        if request.worker == "qa":
            return f"Reviewed upstream output for '{request.task_goal}' and found no blocking issue."
        if request.worker == "analysis":
            return (
                f"Analyzed '{request.task_goal}' in {request.mode} mode and extracted an initial requirement frame."
            )
        if request.worker == "architecture":
            return f"Drafted a modular architecture for '{request.task_goal}' using prior worker context."
        if request.worker == "code":
            return f"Prepared an implementation scaffold for '{request.task_goal}' aligned with the current plan."
        if request.worker == "science":
            return f"Explored experimental paths for '{request.task_goal}' and surfaced candidate innovations."
        if request.worker == "refactor":
            return f"Prepared a refactor strategy for '{request.task_goal}' based on upstream execution signals."
        return f"Generated output for '{request.task_goal}'."

    def _artifacts_for(self, request: GenerationRequest) -> list[dict[str, object]]:
        if request.worker == "strategy":
            return [
                {
                    "type": "assessment",
                    "name": "strategy_assessment",
                    "content": {
                        "complexity_score": 0.55,
                        "recommended_mode": "solar" if request.mode == "solar" else "lunar",
                        "directives": [
                            "Prefer explicit contracts between workers.",
                            "Keep human validation for architecture and code outputs.",
                        ],
                    },
                }
            ]
        if request.worker == "planner":
            return [
                {
                    "type": "task_list",
                    "name": "plan",
                    "content": [
                        {
                            "kind": "analysis",
                            "worker": "analysis",
                            "goal": f"Clarify request: {request.task_goal}",
                            "constraints": ["extract requirements", "identify risks"],
                        },
                        {
                            "kind": "architecture",
                            "worker": "architecture",
                            "goal": f"Design solution for: {request.task_goal}",
                            "constraints": ["define modules", "define interfaces"],
                        },
                        {
                            "kind": "code",
                            "worker": "code",
                            "goal": f"Produce implementation outline for: {request.task_goal}",
                            "constraints": ["respect architecture", "include test hooks"],
                        },
                    ],
                }
            ]
        if request.worker == "qa":
            return [
                {
                    "type": "qa_assessment",
                    "name": "qa_report",
                    "content": {
                        "passed": True,
                        "reason": "",
                        "fix": "",
                    },
                }
            ]
        if request.worker == "analysis":
            return [
                {
                    "type": "analysis_brief",
                    "name": "requirements-summary",
                    "content": {
                        "goal": request.task_goal,
                        "constraints": request.instructions,
                        "mode": request.mode,
                    },
                }
            ]
        if request.worker == "architecture":
            return [
                {
                    "type": "architecture_outline",
                    "name": "diop-modules",
                    "content": [
                        "core.orchestrator",
                        "core.planner",
                        "workers.analysis",
                        "workers.architecture",
                        "workers.code",
                        "memory.repository",
                        "validation.human_interface",
                    ],
                }
            ]
        if request.worker == "code":
            return [
                {
                    "type": "implementation_plan",
                    "name": "mvp-scaffold",
                    "content": {
                        "packages": ["core", "workers", "memory", "validation", "evolution", "adapters"],
                        "tests": ["unit", "pipeline"],
                        "depends_on": request.prior_summaries,
                    },
                }
            ]
        if request.worker == "science":
            return [
                {
                    "type": "research_notes",
                    "name": "advanced-options",
                    "content": [
                        "compare deterministic and adaptive routing policies",
                        "prototype simulation-based worker selection",
                        "evaluate memory retrieval feedback loops",
                    ],
                }
            ]
        if request.worker == "refactor":
            return [
                {
                    "type": "refactor_plan",
                    "name": "maintainability-pass",
                    "content": {
                        "upstream_summaries": request.prior_summaries,
                        "focus": ["complexity reduction", "naming consistency", "test reinforcement"],
                    },
                }
            ]
        return []

    def _risks_for(self, request: GenerationRequest) -> list[str]:
        common = []
        if request.mode == "solar":
            common.append("exploratory generation may require additional narrowing before implementation")
        if request.worker == "analysis":
            common.append("scope may still hide domain-specific edge cases")
        elif request.worker == "architecture":
            common.append("runtime integration boundaries remain conceptual")
        elif request.worker == "code":
            common.append("generated scaffold still needs domain adapters and real model connectors")
        elif request.worker == "science":
            common.append("experimental directions may increase system complexity if adopted too early")
        elif request.worker == "refactor":
            common.append("refactor plan may require regression coverage before execution")
        return common

    def _recommendations_for(self, request: GenerationRequest) -> list[str]:
        if request.worker == "strategy":
            return ["escalate high-risk core logic to lunar mode"]
        if request.worker == "planner":
            return ["review generated task order before expanding the worker graph"]
        if request.worker == "qa":
            return []
        if request.worker == "analysis":
            return ["confirm non-functional requirements before implementation"]
        if request.worker == "architecture":
            return ["lock task and result schemas before expanding worker count"]
        if request.worker == "code":
            return ["add concrete worker adapters after validating contracts"]
        if request.worker == "science":
            return ["validate one experimental path at a time against the MVP contracts"]
        if request.worker == "refactor":
            return ["add characterization tests before applying broad structural changes"]
        return []
