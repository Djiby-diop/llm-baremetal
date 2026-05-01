from __future__ import annotations

import json
from ..contracts.types import Task, new_id
from ...adapters.base import BaseGenerationAdapter, GenerationRequest


class TaskPlanner:
    """Builds a plan from a user goal, dynamically using the LLM if available."""

    def __init__(self, adapter: BaseGenerationAdapter | None = None) -> None:
        self.adapter = adapter

    def build_plan(self, goal: str, mode: str) -> list[Task]:
        # If we have a real LLM adapter, we can ask it to dynamically plan the workflow
        if self.adapter and self.adapter.name != "mock":
            try:
                return self._build_dynamic_plan(goal, mode)
            except Exception as e:
                print(f"[Planner] Dynamic planning failed ({e}). Falling back to deterministic MVP plan.")

        return self._build_deterministic_plan(goal, mode)

    def _build_dynamic_plan(self, goal: str, mode: str) -> list[Task]:
        request = GenerationRequest(
            worker="planner",
            task_goal=f"Decompose this intention into an orchestration plan: {goal}",
            mode=mode,
            instructions=[
                "Act as the DIOP Chief Orchestrator.",
                "Output an artifact named 'plan' with type 'task_list'.",
                "The content MUST be a JSON array of task objects.",
                "Each task object MUST have: 'kind' (string), 'worker' (analysis|architecture|code|refactor|science|baremetal|compiler|warden|executor), 'goal' (string), 'constraints' (list of strings)."
            ]
        )
        response = self.adapter.generate(request)
        
        # Parse the output artifact
        for artifact in response.artifacts:
            if artifact.get("type") == "task_list" or artifact.get("name") == "plan":
                content = artifact.get("content", [])
                if isinstance(content, str):
                    content = json.loads(content)
                
                tasks = []
                for i, t in enumerate(content):
                    tasks.append(
                        Task(
                            id=new_id("task"),
                            kind=t.get("kind", "unknown"),
                            goal=t.get("goal", goal),
                            worker=t.get("worker", "analysis"),
                            mode=mode,
                            constraints=t.get("constraints", []),
                            priority="high" if i == 0 else "medium",
                            depends_on=[tasks[-1].id] if tasks else []
                        )
                    )
                if tasks:
                    return tasks
                    
        raise ValueError("LLM did not return a valid task_list artifact.")

    def _build_deterministic_plan(self, goal: str, mode: str) -> list[Task]:
        goal_lower = goal.lower()
        
        # Specialist plans based on keywords
        if "driver" in goal_lower or "pci" in goal_lower or "hardware" in goal_lower:
            analysis_task = Task(
                id=new_id("task"),
                kind="analysis",
                goal=f"Analyze hardware specs for: {goal}",
                worker="analysis",
                mode=mode,
                constraints=["identify vendor/device ids", "lookup mmio registers"],
                priority="high",
            )
            code_task = Task(
                id=new_id("task"),
                kind="code",
                goal=f"Implement driver stub for: {goal}",
                worker="baremetal",
                mode=mode,
                constraints=["zero-copy", "no malloc", "direct port io"],
                priority="medium",
                depends_on=[analysis_task.id],
            )
            compile_task = Task(
                id=new_id("task"),
                kind="validation",
                goal=f"Verify compilation for: {goal}",
                worker="compiler",
                mode=mode,
                constraints=["syntax-only", "ffreestanding"],
                priority="low",
                depends_on=[code_task.id],
            )
            warden_task = Task(
                id=new_id("task"),
                kind="security",
                goal=f"Audit security for: {goal}",
                worker="warden",
                mode=mode,
                constraints=["check memory ranges", "check privileged opcodes"],
                priority="high",
                depends_on=[compile_task.id],
            )
            executor_task = Task(
                id=new_id("task"),
                kind="execution",
                goal=f"Execute HITL test for: {goal}",
                worker="executor",
                mode=mode,
                constraints=["monitor serial output", "detect faults"],
                priority="low",
                depends_on=[warden_task.id],
            )
            return [analysis_task, code_task, compile_task, warden_task, executor_task]

        # Default MVP plan
        analysis_task = Task(
            id=new_id("task"),
            kind="analysis",
            goal=f"Clarify request: {goal}",
            worker="analysis",
            mode=mode,
            constraints=["extract requirements", "identify risks"],
            priority="high",
        )
        architecture_task = Task(
            id=new_id("task"),
            kind="architecture",
            goal=f"Design solution for: {goal}",
            worker="architecture",
            mode=mode,
            constraints=["define modules", "define interfaces"],
            priority="high",
            depends_on=[analysis_task.id],
        )
        code_task = Task(
            id=new_id("task"),
            kind="code",
            goal=f"Produce implementation outline for: {goal}",
            worker="code",
            mode=mode,
            constraints=["respect architecture", "include test hooks"],
            priority="medium",
            depends_on=[analysis_task.id, architecture_task.id],
        )
        tasks = [analysis_task, architecture_task, code_task]

        if mode == "solar" or self._needs_science_exploration(goal_lower):
            science_task = Task(
                id=new_id("task"),
                kind="science",
                goal=f"Explore advanced options for: {goal}",
                worker="science",
                mode=mode,
                constraints=["surface speculative paths", "highlight experimental risk"],
                priority="medium",
                depends_on=[analysis_task.id],
            )
            tasks.insert(1, science_task)

        if self._needs_refactor(goal_lower):
            refactor_dependencies = [task.id for task in tasks if task.worker in {"architecture", "code"}]
            refactor_task = Task(
                id=new_id("task"),
                kind="refactor",
                goal=f"Refine and optimize solution for: {goal}",
                worker="refactor",
                mode=mode,
                constraints=["reduce complexity", "improve maintainability"],
                priority="medium",
                depends_on=refactor_dependencies,
            )
            tasks.append(refactor_task)

        return tasks

    @staticmethod
    def _needs_refactor(goal: str) -> bool:
        keywords = ("refactor", "optimize", "cleanup", "clean up", "performance", "improve existing")
        return any(keyword in goal for keyword in keywords)

    @staticmethod
    def _needs_science_exploration(goal: str) -> bool:
        keywords = ("research", "experimental", "science", "simulate", "prototype", "innovation")
        return any(keyword in goal for keyword in keywords)
