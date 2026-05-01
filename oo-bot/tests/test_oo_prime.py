from __future__ import annotations

import json
import tempfile
import unittest
from types import SimpleNamespace
from datetime import datetime, timedelta, timezone
from pathlib import Path

from oo_prime.cli import (
    _report_avg_score,
    health_guard_failure,
    parse_action_cooldown_overrides,
    simulation_gate_all_approved,
    strict_apply_failed,
    strict_risk_failed,
)
from oo_prime.config import PolicyConfig
from oo_prime.engine import run_cycles, write_report
from oo_prime.executor import apply_decisions
from oo_prime.governance import decide, decide_with_trace
from oo_prime.observer import observe_modules
from oo_prime.security import enforce_risk_budget, split_safe_decisions
from oo_prime.simulation import evaluate_oo_sim_gate, run_oo_sim_and_write_proof
from oo_prime.state import decision_priority, load_state, should_defer_decision
from oo_prime.types import Decision, ModuleHealth


class OoPrimeTests(unittest.TestCase):
    def test_cli_report_avg_score(self) -> None:
        report = SimpleNamespace(observed_modules=[SimpleNamespace(score=0.2), SimpleNamespace(score=0.6)])
        self.assertAlmostEqual(_report_avg_score(report), 0.4, places=6)

    def test_cli_health_guard_failure_min_score(self) -> None:
        reports = [
            SimpleNamespace(observed_modules=[SimpleNamespace(score=0.7)]),
            SimpleNamespace(observed_modules=[SimpleNamespace(score=0.4)]),
        ]
        failed, reason = health_guard_failure(reports, min_avg_score=0.5, max_drop=None)
        self.assertTrue(failed)
        self.assertIn("below threshold", reason)

    def test_cli_health_guard_failure_max_drop(self) -> None:
        reports = [
            SimpleNamespace(observed_modules=[SimpleNamespace(score=0.9)]),
            SimpleNamespace(observed_modules=[SimpleNamespace(score=0.5)]),
        ]
        failed, reason = health_guard_failure(reports, min_avg_score=None, max_drop=0.2)
        self.assertTrue(failed)
        self.assertIn("drop above threshold", reason)

    def test_cli_health_guard_failure_pass(self) -> None:
        reports = [
            SimpleNamespace(observed_modules=[SimpleNamespace(score=0.8)]),
            SimpleNamespace(observed_modules=[SimpleNamespace(score=0.75)]),
        ]
        failed, reason = health_guard_failure(reports, min_avg_score=0.7, max_drop=0.2)
        self.assertFalse(failed)
        self.assertEqual(reason, "")

    def test_cli_simulation_gate_all_approved(self) -> None:
        reports = [
            SimpleNamespace(simulation_gate={"approved": True}),
            SimpleNamespace(simulation_gate={"approved": True}),
        ]
        self.assertTrue(simulation_gate_all_approved(reports))

    def test_cli_simulation_gate_all_approved_detects_failure(self) -> None:
        reports = [
            SimpleNamespace(simulation_gate={"approved": True}),
            SimpleNamespace(simulation_gate={"approved": False}),
        ]
        self.assertFalse(simulation_gate_all_approved(reports))

    def test_cli_strict_apply_failed_when_no_apply(self) -> None:
        self.assertTrue(strict_apply_failed(0, apply_enabled=True, dry_run=False))

    def test_cli_strict_apply_ignored_without_apply_mode(self) -> None:
        self.assertFalse(strict_apply_failed(0, apply_enabled=False, dry_run=False))
        self.assertFalse(strict_apply_failed(0, apply_enabled=True, dry_run=True))

    def test_cli_strict_risk_failed_when_blocked(self) -> None:
        self.assertTrue(strict_risk_failed(1))
        self.assertFalse(strict_risk_failed(0))

    def test_cli_parse_action_cooldown_overrides(self) -> None:
        parsed = parse_action_cooldown_overrides(
            [
                "propose_system_action_plan=3",
                "propose_docs=1",
            ]
        )
        self.assertEqual(parsed.get("propose_system_action_plan"), 3)
        self.assertEqual(parsed.get("propose_docs"), 1)

    def test_cli_parse_action_cooldown_overrides_rejects_invalid(self) -> None:
        with self.assertRaises(ValueError):
            parse_action_cooldown_overrides(["bad-format"])

    def test_cli_parse_action_cooldown_overrides_clamps_negative(self) -> None:
        parsed = parse_action_cooldown_overrides(["propose_docs=-3"])
        self.assertEqual(parsed.get("propose_docs"), 0)

    def test_observer_detects_osg_and_scores_signals(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            osg = root / "OS-G (Operating System Genesis)"
            osg.mkdir(parents=True)
            (osg / "README.md").write_text("# OS-G\n", encoding="utf-8")

            modules = observe_modules(root)
            names = [m.name for m in modules]
            self.assertIn("OS-G (Operating System Genesis)", names)

    def test_security_blocks_stabilize_without_simulation(self) -> None:
        policy = PolicyConfig(require_simulation_for={"stabilize_module"})
        decisions = [
            Decision(
                action="stabilize_module",
                target="oo-test",
                reason="need stabilization",
                severity="high",
            )
        ]

        accepted, blocked = split_safe_decisions(decisions, policy, simulation_ok=False)
        self.assertEqual(len(accepted), 0)
        self.assertEqual(len(blocked), 1)
        self.assertIn("requires simulation approval", blocked[0].reason)

    def test_simulation_gate_accepts_recent_ok_proof(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            proof = root / "oo-sim" / "reports" / "oo-prime-sim-ok.json"
            proof.parent.mkdir(parents=True, exist_ok=True)

            now = datetime.now(timezone.utc)
            proof.write_text(
                json.dumps({"status": "ok", "generated_at": now.isoformat()}),
                encoding="utf-8",
            )

            result = evaluate_oo_sim_gate(root=root, proof_path=None, max_age_minutes=60)
            self.assertTrue(result.approved)

    def test_simulation_gate_rejects_stale_proof(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            proof = root / "oo-sim" / "reports" / "oo-prime-sim-ok.json"
            proof.parent.mkdir(parents=True, exist_ok=True)

            stale = datetime.now(timezone.utc) - timedelta(hours=5)
            proof.write_text(
                json.dumps({"status": "ok", "generated_at": stale.isoformat()}),
                encoding="utf-8",
            )

            result = evaluate_oo_sim_gate(root=root, proof_path=None, max_age_minutes=30)
            self.assertFalse(result.approved)
            self.assertIn("too old", result.reason)

    def test_executor_applies_only_allowlisted_osg_targets(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            policy = PolicyConfig(
                apply_allowlist={"propose_docs"},
                apply_target_prefixes=("OS-G (Operating System Genesis)",),
            )
            decisions = [
                Decision(
                    action="propose_docs",
                    target="OS-G (Operating System Genesis)",
                    reason="docs missing",
                    severity="low",
                ),
                Decision(
                    action="propose_test_scaffold",
                    target="OS-G (Operating System Genesis)",
                    reason="tests missing",
                    severity="medium",
                ),
                Decision(
                    action="propose_docs",
                    target="oo-bot",
                    reason="out of scope target",
                    severity="low",
                ),
            ]

            applied, skipped, delta, outcomes = apply_decisions(root=root, decisions=decisions, policy=policy)
            self.assertEqual(applied, 1)
            self.assertEqual(skipped, 2)
            self.assertGreaterEqual(delta.get("created", 0), 0)
            self.assertTrue(any(item["status"] == "recorded" for item in outcomes))

            ledger = root / "OS-G (Operating System Genesis)" / "OO_PRIME_ACTIONS.md"
            self.assertTrue(ledger.exists())
            content = ledger.read_text(encoding="utf-8")
            self.assertIn("action=propose_docs", content)

    def test_executor_writes_v02_osg_plan_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            policy = PolicyConfig(
                apply_allowlist={"propose_system_action_plan"},
                apply_target_prefixes=(),
            )
            decision = Decision(
                action="propose_system_action_plan",
                target="SYSTEM",
                reason="generate plan",
                severity="medium",
                metadata={
                    "plan": {
                        "generated_at": "2026-04-09T00:00:00+00:00",
                        "docs": ["Doc task"],
                        "tests": ["Test task"],
                        "policy": ["Policy task"],
                        "reminders": ["Reminder task"],
                    }
                },
            )

            applied, skipped, delta, outcomes = apply_decisions(root=root, decisions=[decision], policy=policy)
            self.assertEqual(applied, 1)
            self.assertEqual(skipped, 0)
            self.assertGreaterEqual(delta.get("created", 0), 1)
            self.assertEqual(outcomes[0]["status"], "created")

            plan_file = root / "OO_PRIME_SYSTEM_ACTION_PLAN.md"
            self.assertTrue(plan_file.exists())
            content = plan_file.read_text(encoding="utf-8")
            self.assertIn("# OO Prime System Action Plan", content)
            self.assertIn("## Documentation", content)
            self.assertIn("- [ ] Doc task", content)
            self.assertIn("## Reminders", content)

    def test_executor_safe_apply_module_doc_and_test_scaffold(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module_dir = root / "oo-module-x"
            module_dir.mkdir(parents=True, exist_ok=True)

            policy = PolicyConfig(
                apply_allowlist={"propose_module_doc_patch", "propose_module_test_scaffold"},
                apply_target_prefixes=(),
            )
            decisions = [
                Decision(
                    action="propose_module_doc_patch",
                    target="oo-module-x",
                    reason="missing docs",
                    severity="medium",
                    metadata={"module_path": str(module_dir)},
                ),
                Decision(
                    action="propose_module_test_scaffold",
                    target="oo-module-x",
                    reason="missing tests",
                    severity="medium",
                    metadata={"module_path": str(module_dir)},
                ),
            ]

            applied, skipped, delta, outcomes = apply_decisions(
                root=root,
                decisions=decisions,
                policy=policy,
                apply_mode="safe",
            )

            self.assertEqual(applied, 2)
            self.assertEqual(skipped, 0)
            self.assertGreaterEqual(delta.get("created", 0), 2)
            self.assertEqual(len(outcomes), 2)
            self.assertTrue((module_dir / "README.md").exists())
            self.assertTrue((module_dir / "tests" / "TEST_SCAFFOLD.md").exists())

    def test_executor_safe_apply_is_create_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module_dir = root / "oo-module-y"
            module_dir.mkdir(parents=True, exist_ok=True)
            readme = module_dir / "README.md"
            readme.write_text("original\n", encoding="utf-8")

            policy = PolicyConfig(
                apply_allowlist={"propose_module_doc_patch"},
                apply_target_prefixes=(),
            )
            decision = Decision(
                action="propose_module_doc_patch",
                target="oo-module-y",
                reason="missing docs",
                severity="medium",
                metadata={"module_path": str(module_dir)},
            )

            _, _, delta, outcomes = apply_decisions(
                root=root,
                decisions=[decision],
                policy=policy,
                apply_mode="safe",
            )

            self.assertEqual(readme.read_text(encoding="utf-8"), "original\n")
            self.assertGreaterEqual(delta.get("noop", 0), 1)
            self.assertEqual(outcomes[0]["status"], "noop")

    def test_governance_emits_stabilize_for_low_health(self) -> None:
        policy = PolicyConfig(min_health_score=0.8, max_decisions_per_cycle=5)
        modules = [
            ModuleHealth(
                name="oo-x",
                path="/tmp/oo-x",
                has_readme=False,
                has_tests=False,
                has_policy=False,
                score=0.3,
                signals=["missing_readme", "missing_tests"],
            )
        ]

        decisions = decide(modules, policy)
        self.assertTrue(any(d.action == "stabilize_module" for d in decisions))

    def test_governance_apply_mode_injects_osg_heartbeat(self) -> None:
        policy = PolicyConfig(max_decisions_per_cycle=3)
        modules = [
            ModuleHealth(
                name="oo-x",
                path="/tmp/oo-x",
                has_readme=True,
                has_tests=True,
                has_policy=True,
                score=0.95,
                signals=[],
            )
        ]

        decisions = decide(
            modules,
            policy,
            prioritize_target_prefixes=("OS-G (Operating System Genesis)",),
        )
        self.assertTrue(
            any(
                d.action in {"propose_docs", "propose_system_action_plan"}
                for d in decisions
            )
        )

    def test_governance_injects_v02_osg_action_plan(self) -> None:
        policy = PolicyConfig(max_decisions_per_cycle=4)
        modules = [
            ModuleHealth(
                name="oo-y",
                path="/tmp/oo-y",
                has_readme=False,
                has_tests=False,
                has_policy=False,
                score=0.2,
                signals=["missing_readme", "missing_tests", "missing_local_policy"],
            )
        ]

        decisions = decide(
            modules,
            policy,
            prioritize_target_prefixes=("OS-G (Operating System Genesis)",),
        )
        plan_decisions = [d for d in decisions if d.action == "propose_system_action_plan"]
        self.assertEqual(len(plan_decisions), 1)
        plan = plan_decisions[0].metadata.get("plan", {})
        self.assertIn("docs", plan)
        self.assertIn("tests", plan)
        self.assertIn("policy", plan)
        self.assertIn("reminders", plan)

    def test_auto_simulation_writes_fail_proof_when_oo_sim_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            proof = root / "oo-sim" / "reports" / "oo-prime-sim-ok.json"
            result = run_oo_sim_and_write_proof(
                root=root,
                proof_path=proof,
                scenario_path=None,
                mode="normal",
                timeout_seconds=5,
            )
            self.assertFalse(result.ok)
            self.assertTrue(proof.exists())
            payload = json.loads(proof.read_text(encoding="utf-8"))
            self.assertEqual(payload.get("status"), "fail")

    def test_risk_budget_blocks_excess_decisions(self) -> None:
        policy = PolicyConfig(max_risk_points_per_cycle=4)
        decisions = [
            Decision(action="propose_docs", target="a", reason="x", severity="medium"),
            Decision(action="propose_docs", target="b", reason="x", severity="medium"),
            Decision(action="propose_docs", target="c", reason="x", severity="low"),
        ]

        kept, blocked, used = enforce_risk_budget(decisions, policy.max_risk_points_per_cycle)
        self.assertEqual(len(kept), 2)
        self.assertEqual(len(blocked), 1)
        self.assertEqual(used, 4)
        self.assertIn("risk budget", blocked[0].reason)

    def test_state_priority_prefers_effective_decisions(self) -> None:
        state = {
            "action_stats": {
                "propose_module_doc_patch": {"created": 2},
                "propose_docs": {"noop": 2},
            },
            "target_stats": {},
            "history": [],
            "cycles_run": 1,
        }
        strong = Decision(
            action="propose_module_doc_patch",
            target="module-a",
            reason="x",
            severity="medium",
        )
        weak = Decision(action="propose_docs", target="module-b", reason="x", severity="medium")

        self.assertGreater(decision_priority(strong, state), decision_priority(weak, state))

    def test_state_defers_target_during_cooldown(self) -> None:
        decision = Decision(action="stabilize_module", target="module-a", reason="x", severity="high")
        deferred, reason = should_defer_decision(
            decision,
            {
                "cycles_run": 1,
                "target_memory": {"module-a": {"last_cycle": 1, "last_action": "stabilize_module"}},
                "decision_memory": {},
            },
            target_cooldown_cycles=2,
            action_cooldown_by_action={},
            repetition_threshold=2,
            repetition_statuses={"blocked", "noop", "skipped"},
        )
        self.assertTrue(deferred)
        self.assertIn("target cooldown", reason)

    def test_state_defers_repeated_noop_decision(self) -> None:
        decision = Decision(action="propose_system_action_plan", target="SYSTEM", reason="x", severity="medium")
        deferred, reason = should_defer_decision(
            decision,
            {
                "cycles_run": 4,
                "target_memory": {},
                "decision_memory": {
                    "propose_system_action_plan::SYSTEM": {
                        "last_cycle": 3,
                        "last_status": "noop",
                        "repeat_count": 2,
                    }
                },
            },
            target_cooldown_cycles=0,
            action_cooldown_by_action={},
            repetition_threshold=2,
            repetition_statuses={"blocked", "noop", "skipped"},
        )
        self.assertTrue(deferred)
        self.assertIn("repetition guard", reason)

    def test_state_defers_by_action_cooldown(self) -> None:
        decision = Decision(action="propose_system_action_plan", target="SYSTEM", reason="x", severity="medium")
        deferred, reason = should_defer_decision(
            decision,
            {
                "cycles_run": 3,
                "target_memory": {},
                "decision_memory": {
                    "propose_system_action_plan::SYSTEM": {
                        "last_cycle": 3,
                        "last_status": "noop",
                        "repeat_count": 1,
                    }
                },
            },
            target_cooldown_cycles=0,
            action_cooldown_by_action={"propose_system_action_plan": 2},
            repetition_threshold=10,
            repetition_statuses={"blocked", "noop", "skipped"},
        )
        self.assertTrue(deferred)
        self.assertIn("action cooldown", reason)

    def test_governance_respects_target_cooldown(self) -> None:
        policy = PolicyConfig(min_health_score=0.8, max_decisions_per_cycle=5, target_cooldown_cycles=2)
        modules = [
            ModuleHealth(
                name="oo-x",
                path="/tmp/oo-x",
                has_readme=False,
                has_tests=False,
                has_policy=False,
                score=0.3,
                signals=["missing_readme", "missing_tests"],
            )
        ]

        decisions = decide(
            modules,
            policy,
            memory_state={
                "cycles_run": 1,
                "target_memory": {"oo-x": {"last_cycle": 1, "last_action": "stabilize_module"}},
                "decision_memory": {},
            },
        )
        self.assertEqual(decisions, [])

    def test_governance_suppresses_repeated_noop_plan(self) -> None:
        policy = PolicyConfig(max_decisions_per_cycle=4, target_cooldown_cycles=0, repetition_threshold=2)
        modules = [
            ModuleHealth(
                name="oo-y",
                path="/tmp/oo-y",
                has_readme=True,
                has_tests=True,
                has_policy=True,
                score=0.95,
                signals=[],
            )
        ]

        decisions = decide(
            modules,
            policy,
            prioritize_target_prefixes=("OS-G (Operating System Genesis)",),
            memory_state={
                "cycles_run": 3,
                "target_memory": {},
                "decision_memory": {
                    "propose_system_action_plan::SYSTEM": {
                        "last_cycle": 2,
                        "last_status": "noop",
                        "repeat_count": 2,
                    }
                },
            },
        )
        self.assertFalse(any(d.action == "propose_system_action_plan" for d in decisions))

    def test_governance_trace_reports_deferred_decisions(self) -> None:
        policy = PolicyConfig(max_decisions_per_cycle=4, target_cooldown_cycles=1, repetition_threshold=2)
        modules = [
            ModuleHealth(
                name="oo-y",
                path="/tmp/oo-y",
                has_readme=True,
                has_tests=True,
                has_policy=True,
                score=0.95,
                signals=[],
            )
        ]

        selected, deferred = decide_with_trace(
            modules,
            policy,
            prioritize_target_prefixes=("OS-G (Operating System Genesis)",),
            memory_state={
                "cycles_run": 5,
                "target_memory": {"OS-G (Operating System Genesis)": {"last_cycle": 5, "last_action": "propose_docs"}},
                "decision_memory": {
                    "propose_system_action_plan::SYSTEM": {
                        "last_cycle": 5,
                        "last_status": "noop",
                        "repeat_count": 2,
                    }
                },
            },
        )

        self.assertEqual(selected, [])
        self.assertEqual(len(deferred), 2)
        self.assertTrue(
            any(
                ("repetition guard" in item.reason) or ("action cooldown" in item.reason)
                for item in deferred
            )
        )
        self.assertTrue(any("target cooldown" in item.reason for item in deferred))

    def test_run_cycles_persists_state_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module_dir = root / "module-a"
            module_dir.mkdir(parents=True, exist_ok=True)

            policy = PolicyConfig(
                max_decisions_per_cycle=2,
                max_risk_points_per_cycle=10,
                require_simulation_for=set(),
                apply_allowlist={"propose_module_doc_patch"},
            )
            output_state = root / "oo-bot-state" / "state.json"
            log_path = root / "oo-bot-state" / "log.jsonl"

            reports = run_cycles(
                root=root,
                policy=policy,
                cycles=2,
                simulation_proof=None,
                simulation_scenario=None,
                simulation_max_age_minutes=60,
                auto_simulate=False,
                apply_mode=True,
                apply_execution_mode="safe",
                state_path=output_state,
                log_path=log_path,
                module_limit=8,
            )

            self.assertEqual(len(reports), 2)
            self.assertEqual(reports[-1].state_summary.get("cycles_run"), 2)
            self.assertTrue(hasattr(reports[-1], "deferred_decisions"))
            self.assertIn("deferred_summary", reports[-1].to_dict())
            self.assertTrue(output_state.exists())
            persisted = load_state(output_state)
            self.assertEqual(persisted.get("cycles_run"), 2)
            self.assertTrue(persisted.get("decision_memory"))

    def test_engine_respects_max_deferred_reasons_limit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ["oo-m1", "oo-m2", "oo-m3"]:
                (root / name).mkdir(parents=True, exist_ok=True)

            policy = PolicyConfig(
                min_health_score=0.99,
                max_decisions_per_cycle=20,
                max_risk_points_per_cycle=100,
                require_simulation_for=set(),
                apply_allowlist=set(),
                target_cooldown_cycles=1,
                max_deferred_reasons_in_log=2,
            )
            output_state = root / "oo-bot-state" / "state.json"
            log_path = root / "oo-bot-state" / "log.jsonl"

            output_state.parent.mkdir(parents=True, exist_ok=True)
            output_state.write_text(
                json.dumps(
                    {
                        "schema": "oo-prime-state-v1",
                        "cycles_run": 1,
                        "action_stats": {},
                        "target_stats": {},
                        "target_memory": {
                            "oo-m1": {"last_cycle": 1, "last_action": "stabilize_module"},
                            "oo-m2": {"last_cycle": 1, "last_action": "stabilize_module"},
                            "oo-m3": {"last_cycle": 1, "last_action": "stabilize_module"},
                        },
                        "decision_memory": {},
                        "history": [],
                    }
                ),
                encoding="utf-8",
            )
            run_cycles(
                root=root,
                policy=policy,
                cycles=1,
                simulation_proof=None,
                simulation_scenario=None,
                simulation_max_age_minutes=60,
                auto_simulate=False,
                apply_mode=False,
                apply_execution_mode="safe",
                state_path=output_state,
                log_path=log_path,
                module_limit=16,
            )

            lines = [line for line in log_path.read_text(encoding="utf-8").splitlines() if line.strip()]
            payload = json.loads(lines[-1])
            deferred = payload.get("deferred", 0)
            reasons = payload.get("deferred_reasons", [])
            self.assertGreater(deferred, 0)
            self.assertEqual(len(reasons), 2)

    def test_engine_combined_override_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ["oo-m1", "oo-m2", "oo-m3"]:
                (root / name).mkdir(parents=True, exist_ok=True)

            policy = PolicyConfig(
                min_health_score=0.99,
                max_decisions_per_cycle=20,
                max_risk_points_per_cycle=100,
                require_simulation_for=set(),
                apply_allowlist=set(),
                target_cooldown_cycles=1,
                repetition_threshold=1,
                max_deferred_reasons_in_log=1,
                action_cooldown_by_action={"propose_system_action_plan": 3},
            )
            output_state = root / "oo-bot-state" / "state.json"
            log_path = root / "oo-bot-state" / "log.jsonl"

            output_state.parent.mkdir(parents=True, exist_ok=True)
            output_state.write_text(
                json.dumps(
                    {
                        "schema": "oo-prime-state-v1",
                        "cycles_run": 2,
                        "action_stats": {},
                        "target_stats": {},
                        "target_memory": {
                            "oo-m1": {"last_cycle": 2, "last_action": "stabilize_module"},
                            "OS-G (Operating System Genesis)": {"last_cycle": 2, "last_action": "propose_docs"},
                        },
                        "decision_memory": {
                            "propose_system_action_plan::SYSTEM": {
                                "last_cycle": 2,
                                "last_status": "noop",
                                "repeat_count": 1,
                            }
                        },
                        "history": [],
                    }
                ),
                encoding="utf-8",
            )

            run_cycles(
                root=root,
                policy=policy,
                cycles=1,
                simulation_proof=None,
                simulation_scenario=None,
                simulation_max_age_minutes=60,
                auto_simulate=False,
                apply_mode=True,
                apply_execution_mode="safe",
                state_path=output_state,
                log_path=log_path,
                module_limit=16,
            )

            payload = json.loads(log_path.read_text(encoding="utf-8").splitlines()[-1])
            self.assertGreater(payload.get("deferred", 0), 0)
            self.assertEqual(len(payload.get("deferred_reasons", [])), 1)
            by_rule = payload.get("deferred_summary", {}).get("by_rule", {})
            self.assertTrue("target_cooldown" in by_rule or "action_cooldown" in by_rule)

    def test_write_report_contains_health_trend(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "oo-a").mkdir(parents=True, exist_ok=True)
            (root / "oo-b").mkdir(parents=True, exist_ok=True)

            policy = PolicyConfig(require_simulation_for=set())
            report_path = root / "reports" / "latest.json"
            state_path = root / "state" / "state.json"
            log_path = root / "logs" / "run.log"

            reports = run_cycles(
                root=root,
                policy=policy,
                cycles=1,
                simulation_proof=None,
                simulation_scenario=None,
                simulation_max_age_minutes=60,
                auto_simulate=False,
                apply_mode=False,
                apply_execution_mode="safe",
                state_path=state_path,
                log_path=log_path,
                module_limit=16,
            )
            write_report(report_path, reports)
            payload = json.loads(report_path.read_text(encoding="utf-8"))
            trend = payload.get("health_trend", {})
            self.assertIn("avg_observed_score", trend)
            self.assertIn("per_cycle", trend)
            self.assertEqual(int(trend.get("cycles", 0)), 1)


if __name__ == "__main__":
    unittest.main()
