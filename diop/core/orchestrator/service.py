from __future__ import annotations

import json
from pathlib import Path

from ...adapters.factory import build_adapter
from ...evolution.scoring.service import EvolutionScorer
from ...memory.repository.json_store import JsonMemoryStore
from ...validation.human_interface.service import HumanValidationService
from ...workers.analysis.worker import AnalysisWorker
from ...workers.architecture.worker import ArchitectureWorker
from ...workers.code.worker import CodeWorker
from ...workers.refactor.worker import RefactorWorker
from ...workers.science.worker import ScienceWorker
from ..aggregator.service import Aggregator
from ..contracts.types import ExecutionReport, MemoryRecord, Task, ValidationDecision, WorkerResult, new_id
from ...workers.baremetal.worker import BareMetalWorker
from ...workers.compiler.worker import CompilerWorker
from ...workers.warden.worker import WardenWorker
from ...workers.executor.worker import QEMUExecutorWorker
from ..dispatcher.service import Dispatcher
from ..planner.service import TaskPlanner


class DIOPOrchestrator:
    def __init__(self, memory_root: Path, adapter_name: str = "mock") -> None:
        if adapter_name.startswith("trained"):
            self.core_adapter = build_adapter("trained:diop_model")
            self.warden_adapter = build_adapter("trained:diop_warden")
            self.architect_adapter = build_adapter("trained:diop_architect")
        else:
            shared_adapter = build_adapter(adapter_name)
            self.core_adapter = shared_adapter
            self.warden_adapter = shared_adapter
            self.architect_adapter = shared_adapter
        
        # The planner uses the Architect brain
        self._planner = TaskPlanner(self.architect_adapter)
        
        self._aggregator = Aggregator()
        self._validator = HumanValidationService()
        self._memory = JsonMemoryStore(memory_root)
        self._scorer = EvolutionScorer()
        
        # Specialized Dispatcher with brain-aware workers
        self._dispatcher = Dispatcher(
            {
                "analysis": AnalysisWorker(self.core_adapter),
                "architecture": ArchitectureWorker(self.architect_adapter),
                "code": CodeWorker(self.core_adapter),
                "refactor": RefactorWorker(self.core_adapter),
                "science": ScienceWorker(self.core_adapter),
                "baremetal": BareMetalWorker(self.core_adapter),
                "compiler": CompilerWorker(self.core_adapter),
                "warden": WardenWorker(self.warden_adapter),
                "executor": QEMUExecutorWorker(self.core_adapter),
            }
        )

    def run(self, goal: str, mode: str = "lunar", auto_approve: bool = False) -> ExecutionReport:
        run_id = new_id("run")
        
        # 1. Strategy Evaluation
        from ..planner.strategy import StrategyEngine
        # We use the same adapter as the planner for strategy evaluation
        strategy_engine = StrategyEngine(self._planner.adapter)
        strategy = strategy_engine.evaluate(goal=goal, requested_mode=mode)
        
        # Inject strategic directives into the goal to guide the Planner
        enhanced_goal = goal
        if strategy.strategic_directives:
            enhanced_goal += "\n[STRATEGY DIRECTIVES]: " + " | ".join(strategy.strategic_directives)
            
        # 2. Dynamic Planning
        tasks = self._planner.build_plan(goal=enhanced_goal, mode=strategy.mode)

        # Initialize Context Engine
        from ...memory.intelligence import ContextIntelligenceEngine
        context_engine = ContextIntelligenceEngine(self._memory)

        results: list[WorkerResult] = []
        for task in tasks:
            # Build intelligent context
            optimal_context = context_engine.build_optimal_context(task)
            task.context["intelligence_layer"] = optimal_context

            worker = self._dispatcher.resolve(task)
            
            # Auto-Correction Feedback Loop (Max 2 retries)
            max_retries = 2
            attempts = 0
            final_result = None
            
            while attempts < max_retries:
                attempts += 1
                if attempts > 1:
                    print(f"\n[Feedback Loop] Retrying task {task.worker} (Attempt {attempts}/{max_retries})...")
                    
                result = worker.execute(task=task, prior_results=results)
                
                # Only QA critical tasks if we have a real LLM
                if task.worker in ("architecture", "code") and self._planner.adapter.name != "mock":
                    from ...workers.qa.worker import QAWorker
                    
                    qa_worker = QAWorker(self._planner.adapter)
                    qa_task = Task(id=new_id("qa"), kind="qa", goal=f"Verify {task.worker} output", worker="qa", mode="lunar", constraints=[])
                    
                    print(f"[QA Worker] Evaluating {task.worker} output...")
                    qa_res = qa_worker.execute(task=qa_task, prior_results=[result])
                    
                    passed = True
                    fix_instruction = ""
                    reason = ""
                    
                    for a in qa_res.artifacts:
                        if a.get("name") == "qa_report" or a.get("type") == "qa_assessment":
                            content = a.get("content", {})
                            if isinstance(content, str): 
                                try: content = json.loads(content)
                                except: pass
                            
                            if isinstance(content, dict):
                                passed = content.get("passed", True)
                                reason = content.get("reason", "Unknown issue")
                                fix_instruction = content.get("fix", "Please correct errors.")
                                
                    if not passed and attempts < max_retries:
                        print(f"   [!] QA Failed: {reason}")
                        print(f"   -> Forcing auto-correction with fix: {fix_instruction}")
                        # Inject the specific QA feedback into the task constraints for the retry
                        task.constraints.append(f"QA FEEDBACK (CRITICAL FIX REQUIRED): {reason}. HOW TO FIX: {fix_instruction}")
                        continue # Retry execution
                    elif passed:
                        print("   [+] QA Passed.")
                
                final_result = result
                break # Success or max retries reached
                
            results.append(final_result)

        aggregate = self._aggregator.aggregate(goal=goal, results=results)
        validation = self._validator.review(results=results, aggregate=aggregate, auto_approve=auto_approve)
        memory_records = self._persist_run(run_id=run_id, goal=goal, tasks=tasks, results=results, validation=validation)
        evolution_signals = self._scorer.score(results=results, validation=validation)

        report = ExecutionReport(
            run_id=run_id,
            goal=goal,
            mode=mode,
            tasks=tasks,
            results=results,
            validation=validation,
            evolution_signals=evolution_signals,
            memory_records=memory_records,
        )

        from .emitter import WorkspaceEmitter
        emitter = WorkspaceEmitter()
        emitted_files = emitter.emit(report)
        if emitted_files:
            print(f"\n[Workspace Emitter] Successfully wrote {len(emitted_files)} physical artifact(s):")
            for path in emitted_files:
                print(f"  -> {path}")

        # 6. 🧠 Experience Consolidation (Autonomous Learning)
        print(f"\n[DIOP] Task complete. Consolidating experience for {mode} mode...")
        self._consolidate_experience(goal, report)
        
        return report

    def _consolidate_experience(self, goal: str, report: dict) -> None:
        """Saves the run results into the sleep learning buffer."""
        experience = {
            "goal": goal,
            "status": str(getattr(getattr(report, "validation", None), "status", "unknown")),
            "report": report.to_dict() if hasattr(report, "to_dict") else report,
        }
        # In a real setup, we would append to a daily_journal.jsonl
        # For now, we notify the memory store
        self._memory.store_experience(experience)

    def _persist_run(
        self,
        run_id: str,
        goal: str,
        tasks: list,
        results: list[WorkerResult],
        validation: ValidationDecision,
    ) -> list[MemoryRecord]:
        records = [
            MemoryRecord(
                id=new_id("mem"),
                category="project",
                tags=["run", "goal"],
                content={"run_id": run_id, "goal": goal},
            ),
            MemoryRecord(
                id=new_id("mem"),
                category="decision",
                tags=["validation"],
                content={"run_id": run_id, "validation": validation.to_dict()},
            ),
        ]

        records.extend(
            MemoryRecord(
                id=new_id("mem"),
                category="pattern",
                tags=[task.kind, result.worker],
                content={
                    "run_id": run_id,
                    "task": task.to_dict(),
                    "result": result.to_dict(),
                },
            )
            for task, result in zip(tasks, results, strict=True)
        )

        for record in records:
            self._memory.append(record)
        return records
