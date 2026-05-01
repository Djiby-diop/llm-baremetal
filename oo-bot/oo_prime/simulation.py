from __future__ import annotations

import json
import subprocess
import tempfile
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path


@dataclass(slots=True)
class SimulationGateResult:
    approved: bool
    source: str
    reason: str


@dataclass(slots=True)
class SimulationRunResult:
    ok: bool
    proof_path: str
    reason: str


def _parse_iso(value: str) -> datetime | None:
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def evaluate_oo_sim_gate(
    root: Path,
    proof_path: Path | None,
    max_age_minutes: int,
) -> SimulationGateResult:
    default_proof = root / "oo-sim" / "reports" / "oo-prime-sim-ok.json"
    candidate = proof_path or default_proof

    if not candidate.exists():
        return SimulationGateResult(
            approved=False,
            source=str(candidate),
            reason="simulation proof missing; run oo-sim and produce oo-prime-sim-ok.json",
        )

    try:
        payload = json.loads(candidate.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return SimulationGateResult(
            approved=False,
            source=str(candidate),
            reason="simulation proof is not valid JSON",
        )

    status = str(payload.get("status", "")).lower().strip()
    if status not in {"ok", "pass", "passed"}:
        return SimulationGateResult(
            approved=False,
            source=str(candidate),
            reason=f"simulation proof status is not ok: status={status or 'missing'}",
        )

    generated_at_raw = str(payload.get("generated_at", "")).strip()
    generated_at = _parse_iso(generated_at_raw)
    if generated_at is None:
        return SimulationGateResult(
            approved=False,
            source=str(candidate),
            reason="simulation proof has invalid generated_at timestamp",
        )

    now = datetime.now(timezone.utc)
    max_age = timedelta(minutes=max(1, int(max_age_minutes)))
    if now - generated_at.astimezone(timezone.utc) > max_age:
        return SimulationGateResult(
            approved=False,
            source=str(candidate),
            reason=(
                f"simulation proof is too old; max_age_minutes={int(max_age_minutes)} "
                f"generated_at={generated_at_raw}"
            ),
        )

    return SimulationGateResult(
        approved=True,
        source=str(candidate),
        reason="simulation proof validated",
    )


def run_oo_sim_and_write_proof(
    root: Path,
    proof_path: Path | None,
    scenario_path: Path | None,
    mode: str,
    timeout_seconds: int,
) -> SimulationRunResult:
    reports_dir = root / "oo-sim" / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)

    candidate = proof_path or (reports_dir / "oo-prime-sim-ok.json")
    scenario_file = scenario_path or (root / "oo-sim" / "scenarios" / "oo-prime-gate.json")

    required_stdout = "final summary"
    required_log = "tick="
    if scenario_file.exists():
        try:
            scenario_raw = json.loads(scenario_file.read_text(encoding="utf-8"))
            required_stdout = str(scenario_raw.get("required_stdout", required_stdout)).strip() or required_stdout
            required_log = str(scenario_raw.get("required_log", required_log)).strip() or required_log
            mode = str(scenario_raw.get("mode", mode)).strip() or mode
        except json.JSONDecodeError:
            pass

    oo_sim_dir = root / "oo-sim"
    oo_sim_exe = oo_sim_dir / "oo-sim.exe"
    if oo_sim_exe.exists():
        command = [str(oo_sim_exe), mode]
        engine = "oo-sim.exe"
    else:
        payload = {
            "status": "fail",
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "scenario": str(scenario_file),
            "mode": mode,
            "engine": "missing",
            "reason": "oo-sim.exe missing; build once with oo-sim/build.ps1",
        }
        candidate.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        return SimulationRunResult(ok=False, proof_path=str(candidate), reason=payload["reason"])

    with tempfile.TemporaryDirectory(prefix="oo-prime-sim-") as temp_dir:
        try:
            result = subprocess.run(
                command,
                cwd=temp_dir,
                capture_output=True,
                text=True,
                timeout=max(5, int(timeout_seconds)),
                check=False,
            )
            stdout = result.stdout or ""
            stderr = result.stderr or ""
            log_path = Path(temp_dir) / "OOSIM.LOG"
            log_content = (
                log_path.read_text(encoding="utf-8", errors="ignore") if log_path.exists() else ""
            )
        except subprocess.TimeoutExpired:
            payload = {
                "status": "fail",
                "generated_at": datetime.now(timezone.utc).isoformat(),
                "scenario": str(scenario_file),
                "mode": mode,
                "engine": engine,
                "reason": f"sim process timed out after {int(timeout_seconds)}s",
            }
            candidate.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
            return SimulationRunResult(ok=False, proof_path=str(candidate), reason=payload["reason"])
        except OSError as exc:
            payload = {
                "status": "fail",
                "generated_at": datetime.now(timezone.utc).isoformat(),
                "scenario": str(scenario_file),
                "mode": mode,
                "engine": engine,
                "reason": f"failed to execute simulation process: {exc}",
            }
            candidate.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
            return SimulationRunResult(ok=False, proof_path=str(candidate), reason=payload["reason"])

    ok = (
        result.returncode == 0
        and required_stdout.lower() in stdout.lower()
        and required_log.lower() in log_content.lower()
    )

    reason = "simulation run succeeded"
    if result.returncode != 0:
        reason = f"sim process failed with code {result.returncode}"
    elif required_stdout.lower() not in stdout.lower():
        reason = f"stdout missing required marker: {required_stdout}"
    elif required_log.lower() not in log_content.lower():
        reason = f"log missing required marker: {required_log}"

    payload = {
        "status": "ok" if ok else "fail",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "scenario": str(scenario_file),
        "mode": mode,
        "engine": engine,
        "reason": reason,
        "exit_code": result.returncode,
        "required_stdout": required_stdout,
        "required_log": required_log,
        "stdout_tail": "\n".join(stdout.splitlines()[-12:]),
        "stderr_tail": "\n".join(stderr.splitlines()[-12:]),
    }
    candidate.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    return SimulationRunResult(ok=ok, proof_path=str(candidate), reason=reason)
