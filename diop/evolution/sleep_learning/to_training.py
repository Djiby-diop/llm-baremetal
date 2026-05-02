"""
to_training.py — MemoryRecord → Batterfyl training brick converter.

Phase T3: DIOP Sleep Learning wired into the Batterfyl pipeline.

A MemoryRecord with category="distilled_knowledge" holds:
  content.brick  — concept / pattern name
  content.rule   — generalized rule (positive or negative/preventive)
  content.source_run — originating DIOP run ID (optional)
  tags           — ["rule","cement", ...], may contain "negative" or "error"

Output is a Batterfyl-compatible instruction/response dict:
  {"instruction": "...", "response": "...", "domain": "OO_META", "halt_prob": 0.12}
"""

from __future__ import annotations

from typing import Any


def record_to_training_brick(record: "dict[str, Any] | Any") -> "dict[str, Any] | None":
    """
    Convert a MemoryRecord (or plain dict) with category='distilled_knowledge'
    into a Batterfyl training sample.

    Returns None if the record is not usable (missing brick/rule, wrong category).
    """
    if isinstance(record, dict):
        category = record.get("category", "")
        content  = record.get("content", {})
        tags     = record.get("tags", [])
    else:
        # MemoryRecord dataclass
        category = record.category
        content  = record.content if isinstance(record.content, dict) else {}
        tags     = list(record.tags)

    if category != "distilled_knowledge":
        return None

    brick = str(content.get("brick", "")).strip()
    rule  = str(content.get("rule",  "")).strip()
    if not brick or not rule:
        return None

    is_negative = (
        "negative" in tags
        or any(kw in brick.lower() for kw in ("error", "fail", "reject", "violation", "overflow", "corrupt"))
    )

    if is_negative:
        instruction = (
            f"[SLEEP-RULE] The OO system encountered the pattern '{brick}'. "
            f"What preventive rule was extracted during sleep consolidation?"
        )
        halt_prob = 0.18
    else:
        instruction = (
            f"[SLEEP-RULE] The OO system consolidated the success pattern '{brick}'. "
            f"What reusable principle was distilled during sleep consolidation?"
        )
        halt_prob = 0.10

    source = content.get("source_run", "")
    response = rule if not source else f"{rule}  [source_run={source}]"

    return {
        "instruction": instruction,
        "response":    response,
        "domain":      "OO_META",
        "halt_prob":   halt_prob,
    }


def memory_store_to_bricks(store_path: "str | Any") -> "list[dict[str, Any]]":
    """
    Load all MemoryRecords from a JsonMemoryStore path and convert
    distilled_knowledge records to Batterfyl training bricks.

    store_path: path to the JSON memory store file (e.g., djib/memory/records.json)
    """
    import json
    from pathlib import Path

    path = Path(store_path)
    if not path.exists():
        return []

    records = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(records, list):
        records = [records]

    bricks = []
    for r in records:
        brick = record_to_training_brick(r)
        if brick:
            bricks.append(brick)
    return bricks
