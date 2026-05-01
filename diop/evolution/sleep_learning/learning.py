from __future__ import annotations

import json
from typing import Any

from ...adapters.base import BaseGenerationAdapter, GenerationRequest


class LearningModule:
    """
    Acts as the 'dream/consolidation' cortex.
    Takes raw experiences (candidates) and uses the LLM adapter to distill them into 
    concrete 'bricks' (patterns) and 'cement' (rules/heuristics).
    """
    
    def __init__(self, adapter: BaseGenerationAdapter) -> None:
        self.adapter = adapter
        
    def digest_experience(self, candidate: dict[str, Any], raw_context: str) -> dict[str, Any]:
        """Ask the LLM to analyze the day's experience and extract a lesson."""
        
        is_negative = candidate.get("type") == "rejected_task"
        
        prompt = (
            f"Experience Type: {candidate['type']}\n"
            f"Result: {'FAILURE/REJECTED' if is_negative else 'SUCCESS'}\n"
            f"Rationale/Feedback: {candidate.get('rationale')}\n\n"
            f"Raw Context (What happened):\n{raw_context}\n\n"
            "Analyze this execution. Extract the core lesson as a JSON object with:\n"
            "- 'brick_name': a short concept name (e.g., 'pci_init_error')\n"
            "- 'cement_rule': " + ("the generalized rule to NEVER REPEAT THIS ERROR" if is_negative else "the rule to replicate success") + "\n"
            "- 'tags': list of relevant keywords"
        )
        
        if is_negative:
             request = GenerationRequest(
                worker="sleep_cortex",
                task_goal=f"CRITICAL: Analyze why the user REJECTED this output. Create a NEGATIVE RULE to prevent this mistake again. Prompt: {prompt}",
                mode="lunar"
            )
        else:
            request = GenerationRequest(
                worker="sleep_cortex",
                task_goal=f"Analyze this success and extract a reusable pattern. Prompt: {prompt}",
                mode="lunar"
            )
        
        try:
            response = self.adapter.generate(request)
            for artifact in response.artifacts:
                if artifact.get("name") == "consolidated_rule":
                    content = artifact.get("content", {})
                    if isinstance(content, str):
                        content = json.loads(content)
                    return content
        except Exception as e:
            print(f"[Learning Module] Failed to digest experience: {e}")
            
        return {}
