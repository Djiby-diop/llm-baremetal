from __future__ import annotations

import json
from .repository.json_store import JsonMemoryStore
from ..core.contracts.types import Task

class ContextIntelligenceEngine:
    """
    Builds the optimal context for a given task by intelligently querying memory.
    Instead of just blindly feeding past logs, it selects:
    - Distilled rules/bricks from Sleep Learning (Cement & Bricks)
    - Past successes (Patterns)
    - Past failures/errors (To avoid repeating them)
    """
    def __init__(self, memory_store: JsonMemoryStore):
        self.memory = memory_store

    def build_optimal_context(self, task: Task) -> dict[str, object]:
        context = {
            "known_patterns": [],
            "past_similar_tasks": [],
            "previous_errors": []
        }
        
        # 1. Distilled Knowledge (From Sleep Learning)
        # We look for rules related to the worker or kind
        rules = self.memory.find_by_category("distilled_knowledge", tags=[task.worker, task.kind, "rule"])
        for r in rules[-3:]: # Get top 3 relevant rules
            content = r.content
            if "brick" in content and "rule" in content:
                context["known_patterns"].append(f"[{content['brick']}] {content['rule']}")

        # 2. Past Similar Tasks (Raw Successes)
        patterns = self.memory.find_by_category("pattern", tags=[task.worker, task.kind])
        for p in patterns[-3:]:
            res = p.content.get("result", {})
            if "summary" in res:
                context["past_similar_tasks"].append(res["summary"])

        # 3. Previous Errors (From Decisions)
        decisions = self.memory.find_by_category("decision")
        # We look for rejected/changed decisions
        recent_errors = []
        for d in decisions[-10:]:
            val = d.content.get("validation", {})
            if val.get("status") in ("needs_more_analysis", "approved_with_changes"):
                rationale = val.get("rationale", "")
                changes = val.get("changes_requested", [])
                if changes:
                    recent_errors.append(f"Avoided Error: {rationale} -> Fix requested: {', '.join(changes)}")
        
        context["previous_errors"] = recent_errors[-3:]

        return context
