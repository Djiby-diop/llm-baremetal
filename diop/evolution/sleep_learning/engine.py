from __future__ import annotations

from pathlib import Path

from ...adapters.factory import build_adapter
from ...core.contracts.types import MemoryRecord, new_id
from ...memory.repository.json_store import JsonMemoryStore
from .learning import LearningModule
from .selector import DataSelector


class SleepLearningEngine:
    """
    The orchestrator for the Sleep Phase.
    Runs periodically (e.g., at night) to consolidate experiences into durable knowledge without huge model retraining.
    """
    
    def __init__(self, memory_root: Path, adapter_name: str = "local") -> None:
        self.memory_store = JsonMemoryStore(memory_root)
        self.selector = DataSelector(memory_root)
        self.learning = LearningModule(build_adapter(adapter_name))
        
    def run_sleep_cycle(self) -> list[MemoryRecord]:
        print("\n[🌙 DIOP Sleep Engine] Entering Sleep Cycle...")
        
        # 1. Data Selection
        candidates = self.selector.select_candidates()
        if not candidates:
            print("[🌙 DIOP Sleep Engine] No new experiences to consolidate. Waking up.")
            return []
            
        print(f"[🌙 DIOP Sleep Engine] Found {len(candidates)} experiences to digest.")
        
        new_knowledge = []
        
        # 2. Consolidation & Micro-learning
        for idx, candidate in enumerate(candidates):
            print(f"  -> Digesting memory {idx+1}/{len(candidates)} (Type: {candidate['type']})")
            
            # In a full implementation, we'd fetch the raw project pattern context here using candidate['run_id']
            raw_context_stub = f"Run ID {candidate['run_id']} executed."
            
            # Extract brick & cement
            distilled = self.learning.digest_experience(candidate, raw_context_stub)
            
            if distilled and "brick_name" in distilled and "cement_rule" in distilled:
                # 3. Memory Building
                record = MemoryRecord(
                    id=new_id("rule"),
                    category="distilled_knowledge",
                    tags=distilled.get("tags", []) + ["rule", "cement"],
                    content={
                        "brick": distilled["brick_name"],
                        "rule": distilled["cement_rule"],
                        "source_run": candidate.get("run_id")
                    }
                )
                self.memory_store.append(record)
                new_knowledge.append(record)
                print(f"     [+] Extracted rule: {distilled['brick_name']}")
                
        print(f"[☀️ DIOP Sleep Engine] Waking up. Consolidated {len(new_knowledge)} new rules into permanent memory.")
        return new_knowledge
