
from __future__ import annotations
import json
from ...core.contracts.types import Task, WorkerResult
from ..base import BaseWorker

class WardenWorker(BaseWorker):
    """
    Security Warden Worker.
    Acts as the System Guardian, auditing code and architecture against 
    the DIOP Security Redlines and the oo-system policies.
    """
    name = "warden"

    def execute(self, task: Task, prior_results: list[WorkerResult]) -> WorkerResult:
        if not prior_results:
            raise ValueError("Warden Worker requires prior results to audit.")
            
        target_result = prior_results[-1]
        
        # We use a specialized "Security Auditor" prompt
        instructions = [
            "Act as the DIOP System Warden (Security Specialist).",
            "Your ONLY goal is to find security violations in the provided artifacts.",
            "REFER to the 'security_policy.json' rules for forbidden memory and opcodes.",
            "Check if the code correctly uses 'oo_bus_bridge_emit' for hardware status reporting.",
            "Verify that no direct pointer manipulation hits restricted ranges (0x00000000 - 0x000003FF).",
            "Output an artifact named 'warden_verdict' with type 'security_audit'.",
            "Content MUST be a strict JSON object: {'verdict': 'ALLOW'|'REJECT'|'QUARANTINE', 'violations': [], 'reason': '...'}"
        ]
        
        generated = self._generate(
            task=task,
            prior_results=[target_result],
            instructions=instructions,
        )
        
        # If the verdict is REJECT, we add a critical risk to the result
        audit_res = self._parse_verdict(generated.artifacts)
        if audit_res.get("verdict") in ("REJECT", "QUARANTINE"):
            generated.risks.append(f"WARDEN ALERT: {audit_res.get('reason')}")
            generated.summary = f"[SECURITY FAILURE] {generated.summary}"
        
        return WorkerResult(
            task_id=task.id,
            worker=self.name,
            status="completed",
            summary=generated.summary,
            artifacts=generated.artifacts,
            risks=generated.risks,
            recommendations=generated.recommendations,
            needs_validation=False,
            metadata=generated.metadata,
        )

    def _parse_verdict(self, artifacts):
        for a in artifacts:
            if a.get("name") == "warden_verdict":
                content = a.get("content", {})
                if isinstance(content, str):
                    try: return json.loads(content)
                    except: pass
                return content
        return {"verdict": "ALLOW"}
