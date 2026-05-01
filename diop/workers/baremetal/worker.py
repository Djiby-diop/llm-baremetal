
import json
from pathlib import Path
from ..base import BaseWorker
from ...adapters.base import GenerationRequest

class BareMetalWorker(BaseWorker):
    """
    Expert Worker specialized in Low-Level C for UEFI/Kernel development.
    Uses the local 40M model assisted by Pattern Memory.
    """
    def __init__(self, adapter):
        super().__init__(adapter)
        self.name = "baremetal"

    def execute(self, task, prior_results=None):
        from .validator import BareMetalValidator
        validator = BareMetalValidator()
        goal = task.goal
        
        # 1. 🔍 Context Injection
        patterns = self._retrieve_patterns(goal)
        enriched_prompt = self._build_expert_prompt(goal, patterns)
        
        # 2. 🤖 Generation
        request = GenerationRequest(
            worker=self.name,
            task_goal=enriched_prompt,
            mode="lunar"
        )
        response = self.adapter.generate(request)
        
        # 3. 🧪 Validation & Post-Processing
        validation_res = validator.validate(str(response.artifacts))
        for issue in validation_res["issues"]:
            msg = f"[{issue['severity']}] {issue['message']}"
            response.risks.append(msg)
            print(f"   [!] Validation: {msg}")
            
        if not validation_res["passed"]:
            response.summary = "[REJECTED BY VALIDATOR] " + response.summary
            
        return response

    def _retrieve_patterns(self, goal):
        """Recherche de patterns C dans la mémoire."""
        patterns = []
        g_lower = goal.lower()
        if "pci" in g_lower:
            patterns.append("PATTERN: PCI Config Space Access (0xCF8/0xCFC)")
            patterns.append("RULE: Always enable Bus Master bit in Command Register")
        if "uart" in g_lower or "serial" in g_lower:
            patterns.append("PATTERN: 16550 UART Init (Baudrate Divisor)")
            patterns.append("RULE: Disable interrupts during UART init")
        return patterns

    def _build_expert_prompt(self, goal, patterns):
        patterns_str = "\n".join([f"- {p}" for p in patterns])
        return f"""[DOMAIN: BAREMETAL]
Expert Rules:
- No malloc, no printf.
- Use volatile for MMIO.
- Strict separation of hardware domains.

Relevant Patterns from Memory:
{patterns_str}

Task:
{goal}

Output: Structured JSON with C code artifacts."""

    def _detect_risks(self, content):
        risks = []
        c_lower = content.lower()
        if "uart" in c_lower and "pci" in c_lower:
            risks.append("CRITICAL: Subsystem mix detected (UART code inside PCI task)")
        if "printf(" in c_lower:
            risks.append("WARNING: Standard printf detected in kernel space")
        if "malloc(" in c_lower:
            risks.append("ERROR: Illegal malloc call in bare-metal")
        return risks
