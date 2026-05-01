from __future__ import annotations

"""
DIOP Native Model — Training Data Exporter
==========================================
Pilier 1: Reads distilled_knowledge.jsonl and decision.jsonl from the Sleep Engine
and converts them into structured training samples (prompt/completion pairs)
ready to fine-tune a small Transformer on the OO domain.

Output format: JSONL where each line = {"prompt": "...", "completion": "..."}
"""

import json
import re
from pathlib import Path
from typing import Iterator


class TrainingDataExporter:
    """
    Reads DIOP memory files and emits training samples.

    Sources:
      - memory/distilled_knowledge.jsonl  (brick/cement rules from Sleep Engine)
      - memory/decision.jsonl             (human approvals / corrections)
      - memory/pattern.jsonl              (successful task outputs)
    """

    SYSTEM_HEADER = (
        "You are DIOP-Core, an expert bare-metal engineer specialized in "
        "C, Rust, and Zig. You write zero-copy, malloc-free code for UEFI "
        "and embedded systems. Your output is always structured JSON."
    )

    def __init__(self, memory_root: Path, output_path: Path) -> None:
        self.memory_root = memory_root
        self.output_path = output_path

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def export(self) -> int:
        """Export all training samples. Returns the count written."""
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        count = 0
        with self.output_path.open("w", encoding="utf-8") as fout:
            for sample in self._all_samples():
                if self._is_valid_sample(sample):
                    fout.write(json.dumps(sample, ensure_ascii=False) + "\n")
                    count += 1
        print(f"[TrainingDataExporter] Exported {count} samples -> {self.output_path}")
        return count

    def _is_valid_sample(self, sample: dict) -> bool:
        """Filter out garbage, token loops, and empty responses."""
        prompt = sample.get("prompt", "").lower()
        completion = sample.get("completion", "")
        
        # 1. Reject obvious hallucinations / token loops
        if "tictint" in completion or "rororo" in completion:
            return False
            
        # 2. Reject empty or near-empty JSON artifacts
        try:
            data = json.loads(completion)
            # If it's a standard DIOP response, check for real content
            if isinstance(data, dict):
                artifacts = data.get("artifacts", [])
                recommendations = data.get("recommendations", [])
                risks = data.get("risks", [])
                
                # If everything is empty, it's not a useful training sample
                if not artifacts and not recommendations and not risks:
                    return False
        except:
            # If not valid JSON, reject (unless it's a raw text completion, 
            # but DIOP targets JSON)
            return False

        # 3. Reject samples where goal is "Unknown task" if it has no high-quality content
        if "unknown task" in prompt:
            # If it's just an auto-approval without specifics, skip it
            if "auto-approved because no critical risk" in completion.lower():
                return False

        return True

    # ------------------------------------------------------------------
    # Private generators
    # ------------------------------------------------------------------

    def _all_samples(self) -> Iterator[dict]:
        yield from self._from_distilled_knowledge()
        yield from self._from_decisions()
        yield from self._from_patterns()

    def _from_distilled_knowledge(self) -> Iterator[dict]:
        """Convert brick/cement rules into Q&A training pairs."""
        path = self.memory_root / "distilled_knowledge.jsonl"
        if not path.exists():
            return
        for obj in self._read_jsonl(path):
            content = obj.get("content", {})
            brick = content.get("brick_name") or content.get("brick", "")
            rule  = content.get("cement_rule") or content.get("rule", "")
            tags  = content.get("tags", [])
            if not brick or not rule:
                continue
            prompt = (
                f"{self.SYSTEM_HEADER}\n\n"
                f"[RULE REQUEST] What is the rule for '{brick}' "
                f"in the context of: {', '.join(tags)}?"
            )
            completion = json.dumps({
                "summary": f"Rule: {brick}",
                "artifacts": [],
                "risks": [],
                "recommendations": [rule]
            })
            yield {"prompt": prompt, "completion": completion}

    def _from_decisions(self) -> Iterator[dict]:
        """Convert human corrections into negative/positive examples."""
        path = self.memory_root / "decision.jsonl"
        if not path.exists():
            return
        for obj in self._read_jsonl(path):
            content = obj.get("content", {})
            validation = content.get("validation", {})
            status  = validation.get("status", "")
            changes = validation.get("changes_requested", [])
            rationale = validation.get("rationale", "")
            goal = content.get("goal", "Unknown task")

            if status in ("approved_with_changes", "needs_more_analysis") and changes:
                # Teach the model to avoid this class of mistake
                prompt = (
                    f"{self.SYSTEM_HEADER}\n\n"
                    f"[TASK] {goal}\n"
                    f"[PREVIOUS MISTAKE] {rationale}\n"
                    f"[CORRECTION REQUIRED] {', '.join(changes)}\n"
                    "Produce a corrected response:"
                )
                completion = json.dumps({
                    "summary": f"Corrected: {rationale}",
                    "artifacts": [],
                    "risks": [rationale],
                    "recommendations": changes
                })
                yield {"prompt": prompt, "completion": completion}

            elif status == "approved":
                prompt = (
                    f"{self.SYSTEM_HEADER}\n\n"
                    f"[TASK] {goal}\n"
                    "Produce a high-quality response:"
                )
                completion = json.dumps({
                    "summary": f"Approved without changes: {rationale}",
                    "artifacts": [],
                    "risks": [],
                    "recommendations": [rationale] if rationale else []
                })
                yield {"prompt": prompt, "completion": completion}

    def _from_patterns(self) -> Iterator[dict]:
        """Convert successful task outputs into demonstration pairs."""
        path = self.memory_root / "pattern.jsonl"
        if not path.exists():
            return
        for obj in self._read_jsonl(path):
            content = obj.get("content", {})
            result  = content.get("result", {})
            task    = content.get("task", {})
            if not result or not task:
                continue
            goal = task.get("goal", "")
            if not goal:
                continue
            prompt = (
                f"{self.SYSTEM_HEADER}\n\n"
                f"[TASK] {goal}"
            )
            completion = json.dumps(result)
            yield {"prompt": prompt, "completion": completion}

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _read_jsonl(path: Path):
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    yield json.loads(line)
                except json.JSONDecodeError:
                    pass
