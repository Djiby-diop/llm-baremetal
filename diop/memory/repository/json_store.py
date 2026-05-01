from __future__ import annotations

import json
from pathlib import Path

from ...core.contracts.types import MemoryRecord


class JsonMemoryStore:
    def __init__(self, root: Path) -> None:
        self._root = root
        self._root.mkdir(parents=True, exist_ok=True)

    def append(self, record: MemoryRecord) -> Path:
        path = self._root / f"{record.category}.jsonl"
        with path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record.to_dict(), ensure_ascii=True) + "\n")
        return path

    def find_patterns(self, tags: list[str]) -> list[MemoryRecord]:
        return self.find_by_category("pattern", tags)

    def find_by_category(self, category: str, tags: list[str] = None) -> list[MemoryRecord]:
        path = self._root / f"{category}.jsonl"
        if not path.exists():
            return []

        results = []
        with path.open("r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    data = json.loads(line)
                    record = MemoryRecord(**data)
                    # If tags provided, check intersection
                    if tags:
                        if any(t in record.tags for t in tags):
                            results.append(record)
                    else:
                        results.append(record)
                except Exception:
                    pass
        return results

    def store_experience(self, experience: dict) -> None:
        """Appends a new experience to the daily journal."""
        journal_path = self._root / "daily_journal.jsonl"
        with journal_path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(experience, ensure_ascii=False) + "\n")
        print(f"      [Memory] Experience stored in {journal_path.name}")
