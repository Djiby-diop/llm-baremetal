from __future__ import annotations

import json
import unittest
from pathlib import Path
from uuid import uuid4

from diop import DIOPOrchestrator


class DiopTests(unittest.TestCase):
    def _workspace_tempdir(self) -> Path:
        base = Path(__file__).resolve().parent / ".tmp"
        base.mkdir(parents=True, exist_ok=True)
        target = base / f"case_{uuid4().hex[:8]}"
        target.mkdir(parents=True, exist_ok=True)
        return target

    def test_orchestrator_runs_pipeline_and_persists_memory(self) -> None:
        tmp = self._workspace_tempdir()
        orchestrator = DIOPOrchestrator(memory_root=tmp)
        report = orchestrator.run(goal="Build an authentication service", mode="lunar")

        self.assertEqual(len(report.tasks), 3)
        self.assertEqual([result.worker for result in report.results], ["analysis", "architecture", "code"])
        self.assertEqual(report.validation.status, "needs_more_analysis")

        project_log = tmp / "project.jsonl"
        self.assertTrue(project_log.exists())
        rows = [json.loads(line) for line in project_log.read_text(encoding="utf-8").splitlines() if line]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["category"], "project")
        self.assertEqual(report.results[0].metadata["adapter"], "mock")

    def test_auto_approve_without_critical_risk(self) -> None:
        tmp = self._workspace_tempdir()
        orchestrator = DIOPOrchestrator(memory_root=tmp)
        report = orchestrator.run(
            goal="Draft a documentation pipeline",
            mode="solar",
            auto_approve=True,
        )

        self.assertEqual(report.validation.status, "approved")
        self.assertEqual(report.evolution_signals["recommended_mode"], "solar")

    def test_solar_mode_adds_science_worker(self) -> None:
        tmp = self._workspace_tempdir()
        orchestrator = DIOPOrchestrator(memory_root=tmp)
        report = orchestrator.run(
            goal="Prototype an experimental orchestration workflow",
            mode="solar",
            auto_approve=True,
        )

        workers = [result.worker for result in report.results]
        self.assertIn("science", workers)
        self.assertEqual(len(report.tasks), 4)

    def test_refactor_goal_adds_refactor_worker(self) -> None:
        tmp = self._workspace_tempdir()
        orchestrator = DIOPOrchestrator(memory_root=tmp)
        report = orchestrator.run(
            goal="Refactor and optimize an existing authentication service",
            mode="lunar",
        )

        workers = [result.worker for result in report.results]
        self.assertIn("refactor", workers)
        self.assertEqual(len(report.tasks), 4)

    def test_code_worker_receives_prior_context_in_artifact(self) -> None:
        tmp = self._workspace_tempdir()
        orchestrator = DIOPOrchestrator(memory_root=tmp)
        report = orchestrator.run(
            goal="Build an orchestration runtime",
            mode="lunar",
        )

        code_result = next(result for result in report.results if result.worker == "code")
        plan_artifact = next(artifact for artifact in code_result.artifacts if artifact["type"] == "implementation_plan")
        self.assertGreaterEqual(len(plan_artifact["content"]["depends_on"]), 2)


if __name__ == "__main__":
    unittest.main()
