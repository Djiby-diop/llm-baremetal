from __future__ import annotations

import time
from pathlib import Path

from .core.orchestrator.service import DIOPOrchestrator
from .evolution.sleep_learning import SleepLearningEngine


class DIOPDaemon:
    """
    Fully autonomous background loop for DIOP.
    Monitors an inbox for goals, executes them automatically, 
    and triggers Sleep Learning cycles when idle.
    """
    def __init__(self, memory_root: Path, adapter_name: str = "local") -> None:
        self.memory_root = memory_root
        self.adapter_name = adapter_name
        self.orchestrator = DIOPOrchestrator(memory_root, adapter_name)
        self.sleep_engine = SleepLearningEngine(memory_root, adapter_name)
        
        self.inbox_dir = self.memory_root.parent / "inbox"
        self.inbox_dir.mkdir(parents=True, exist_ok=True)
        self.tasks_since_last_sleep = 0

    def start(self) -> None:
        print("=" * 50)
        print("🤖 DIOP AUTONOMOUS DAEMON STARTED")
        print("=" * 50)
        print(f"Monitoring inbox: {self.inbox_dir}")
        print("Drop any .txt file with a goal into the inbox to start execution.\n")
        
        try:
            while True:
                # 1. Check Inbox
                goal_files = list(self.inbox_dir.glob("*.txt"))
                
                # 2. Idle State -> Sleep & Immune Cycle
                if not goal_files:
                    if self.tasks_since_last_sleep > 0:
                        print("\n[Daemon] Inbox empty. Triggering automatic Maintenance Cycles...")
                        self.sleep_engine.run_sleep_cycle()
                        
                        from .evolution.immune.system import ImmuneSystem
                        workspace_root = Path("diop_workspace")
                        immune_sys = ImmuneSystem(workspace_root=workspace_root, adapter_name=self.adapter_name)
                        immune_sys.run_immune_check()
                        
                        self.tasks_since_last_sleep = 0
                        print(f"\n[Daemon] Monitoring inbox: {self.inbox_dir}")
                    
                    time.sleep(3)
                    continue

                # 3. Wake State -> Execution
                for goal_file in goal_files:
                    try:
                        goal_text = goal_file.read_text(encoding="utf-8").strip()
                        if not goal_text:
                            goal_file.unlink()
                            continue
                            
                        print(f"\n[Daemon][Wake] Picked up new goal: '{goal_text}'")
                        print(f"[Daemon][Wake] Processing {goal_file.name}...")
                        
                        # Full autonomy: auto_approve=True
                        self.orchestrator.run(goal=goal_text, mode="solar", auto_approve=True)
                        self.tasks_since_last_sleep += 1
                        
                        # Cleanup
                        goal_file.unlink()
                        print(f"[Daemon][Wake] Successfully completed {goal_file.name}")
                        
                    except Exception as e:
                        print(f"[Daemon][Error] Failed processing {goal_file.name}: {e}")
                        # Mark as error to avoid infinite retry loops
                        error_path = goal_file.with_suffix(".error")
                        goal_file.rename(error_path)
                        
        except KeyboardInterrupt:
            print("\n[Daemon] Shutdown signal received. Exiting.")
