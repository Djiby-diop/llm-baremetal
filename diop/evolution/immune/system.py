from __future__ import annotations

import json
from pathlib import Path

from ...adapters.factory import build_adapter
from ...adapters.base import GenerationRequest


class ImmuneSystem:
    """
    Biological Code Healing (Le Système Immunitaire).
    Scans the generated workspace for 'code rot', vulnerabilities, or missing best practices.
    Generates health reports and patch recommendations automatically.
    """

    def __init__(self, workspace_root: Path, adapter_name: str = "swarm") -> None:
        self.workspace_root = workspace_root
        self.adapter = build_adapter(adapter_name)
        self.workspace_root.mkdir(parents=True, exist_ok=True)

    def run_immune_check(self) -> None:
        print("\n[DIOP Immune System] Activating White Blood Cells...")
        
        # Scan for existing code files in the workspace
        files_to_check = []
        for path in self.workspace_root.rglob("*.*"):
            if path.suffix in (".py", ".c", ".js", ".md") and not path.name.endswith(".review.md"):
                files_to_check.append(path)
                
        if not files_to_check:
            print("[DIOP Immune System] Workspace clean. No tissues to inspect.")
            return

        print(f"[DIOP Immune System] Found {len(files_to_check)} files. Initiating random health inspection...")
        import random
        # Randomly sample a few files to prevent massive API spam
        sample = random.sample(files_to_check, min(3, len(files_to_check)))

        for file_path in sample:
            print(f"  -> Diagnosing: {file_path.name}")
            content = file_path.read_text(encoding="utf-8")
            
            request = GenerationRequest(
                worker="immune_cell",
                task_goal=f"Perform a deep biological health check on {file_path.name}.",
                mode="lunar",
                instructions=[
                    "Act as the DIOP Immune System White Blood Cell.",
                    "Review the provided code for security vulnerabilities, deprecation, or missing patterns.",
                    f"Code Content:\n{content[:2000]}", # Limit size for safety
                    "Output an artifact named 'health_report' with type 'health_scan'.",
                    "Content MUST be a strict JSON object: {'healthy': boolean, 'diagnosis': 'string', 'recommended_patch': 'string'}"
                ]
            )
            
            try:
                response = self.adapter.generate(request)
                for a in response.artifacts:
                    if a.get("name") == "health_report" or a.get("type") == "health_scan":
                        rep = a.get("content", {})
                        if isinstance(rep, str): rep = json.loads(rep)
                        
                        healthy = rep.get("healthy", True)
                        diagnosis = rep.get("diagnosis", "")
                        
                        if not healthy:
                            print(f"     ⚠️ Infection detected: {diagnosis}")
                            # Create a patch proposal file next to the infected file
                            review_path = file_path.with_suffix(".review.md")
                            with review_path.open("w", encoding="utf-8") as f:
                                f.write(f"# DIOP Immune System Report\n**File:** {file_path.name}\n**Diagnosis:** {diagnosis}\n\n**Recommended Patch:**\n```\n{rep.get('recommended_patch', '')}\n```")
                            print(f"     [+] Generated antibody patch: {review_path.name}")
                        else:
                            print("     [+] Tissue healthy.")
            except Exception as e:
                print(f"     [!] Diagnostic failed on {file_path.name}: {e}")
                
        print("[DIOP Immune System] Patrol complete. Deactivating.")
