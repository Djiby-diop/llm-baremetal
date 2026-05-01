from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from ...core.contracts.types import MemoryRecord


class DataSelector:
    """
    Scans the raw daily memory (wake phase) and selects high-value experiences:
    - Errors/rejections (to learn what not to do)
    - Human corrections (to extract rules)
    - Major successes (to extract positive patterns)
    """
    
    def __init__(self, memory_root: Path) -> None:
        self.memory_root = memory_root
        
    def select_candidates(self) -> list[dict[str, Any]]:
        decision_path = self.memory_root / "decision.jsonl"
        if not decision_path.exists():
            return []
            
        candidates = []
        with decision_path.open("r", encoding="utf-8") as f:
            for line in f:
                if not line.strip(): continue
                try:
                    data = json.loads(line)
                    content = data.get("content", {})
                    validation = content.get("validation", {})
                    status = validation.get("status")
                    
                    # We want to learn from mistakes and human corrections
                    if status in ("approved_with_changes", "needs_more_analysis"):
                        candidates.append({
                            "type": "correction",
                            "run_id": content.get("run_id"),
                            "changes": validation.get("changes_requested", []),
                            "rationale": validation.get("rationale", "")
                        })
                    # Or pristine successes
                    elif status == "approved":
                        candidates.append({
                            "type": "success",
                            "run_id": content.get("run_id"),
                            "rationale": "Human approved without changes."
                        })
                except Exception:
                    pass
                    
        # In a real implementation, we would filter out already-processed run_ids
        return candidates[-10:] # Return last 10 for batch processing
