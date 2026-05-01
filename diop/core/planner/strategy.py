from __future__ import annotations

import json
from dataclasses import dataclass

from ...adapters.base import BaseGenerationAdapter, GenerationRequest


@dataclass
class StrategyContext:
    mode: str
    complexity: float
    strategic_directives: list[str]


class StrategyEngine:
    """
    Acts as the 'Frontal Lobe' of DIOP.
    Evaluates a goal's complexity, domain, and risk before any planning occurs.
    Decides the optimal execution mode ('solar' or 'lunar') and injects high-level directives.
    """

    def __init__(self, adapter: BaseGenerationAdapter | None = None) -> None:
        self.adapter = adapter

    def evaluate(self, goal: str, requested_mode: str = "auto") -> StrategyContext:
        if not self.adapter or self.adapter.name == "mock":
            # Fallback for mock environment
            return StrategyContext(
                mode=requested_mode if requested_mode != "auto" else "lunar",
                complexity=0.5,
                strategic_directives=[]
            )

        print("\n[Strategy] Evaluating goal complexity and strategic routing...")

        request = GenerationRequest(
            worker="strategy",
            task_goal=f"Analyze the complexity, technical risk, and domain of this user goal: {goal}",
            mode="lunar",  # Strategy evaluation must always be precise/analytical
            instructions=[
                "Act as the DIOP Chief Strategy Officer.",
                "Evaluate the risk. Security/Cryptographic/Core logic = High risk.",
                "Output an artifact named 'strategy_assessment' with type 'assessment'.",
                "Content MUST be a JSON object with keys: 'complexity_score' (float 0.0-1.0), 'recommended_mode' ('solar' or 'lunar'), 'directives' (list of strategic constraints)."
            ]
        )

        try:
            response = self.adapter.generate(request)
            for artifact in response.artifacts:
                if artifact.get("name") == "strategy_assessment" or artifact.get("type") == "assessment":
                    content = artifact.get("content", {})
                    if isinstance(content, str):
                        content = json.loads(content)

                    score = float(content.get("complexity_score", 0.5))
                    recommended_mode = content.get("recommended_mode", "lunar")
                    
                    # Decide mode based on score and user request
                    mode = recommended_mode
                    if requested_mode != "auto":
                        mode = requested_mode

                    # Safety Override: High complexity forces lunar (precision) mode
                    if score >= 0.8:
                        print(f"[Strategy] High complexity detected ({score}). Forcing 'lunar' mode.")
                        mode = "lunar"

                    directives = content.get("directives", [])
                    print(f"[Strategy] Assessment complete -> Complexity: {score} | Mode: {mode}")
                    if directives:
                        print(f"   -> Directives: {', '.join(directives[:2])}...")

                    return StrategyContext(mode=mode, complexity=score, strategic_directives=directives)
        except Exception as e:
            print(f"[Strategy] Failed to evaluate strategy: {e}")

        # Safe fallback
        return StrategyContext(
            mode=requested_mode if requested_mode != "auto" else "lunar",
            complexity=0.5,
            strategic_directives=[]
        )
