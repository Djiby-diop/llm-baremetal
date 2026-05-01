
import subprocess
import tempfile
import os
from pathlib import Path
from ..base import BaseWorker

class CompilerWorker(BaseWorker):
    """
    Validation Worker that attempts to physically compile the generated C code.
    This provides ground-truth feedback to the model.
    """
    def __init__(self, adapter):
        super().__init__(adapter)
        self.name = "compiler"

    def execute(self, task, prior_results=None):
        if not prior_results:
            return self._error_response("No code found to compile.")
            
        # On cherche le dernier code généré dans les résultats précédents
        code_to_test = ""
        for res in reversed(prior_results):
            for art in res.artifacts:
                if art.get("type") == "code":
                    code_to_test = art.get("content", "")
                    break
            if code_to_test: break
            
        if not code_to_test:
            return self._error_response("Could not find any C code artifact in prior results.")

        print(f"[{self.name.upper()}] Attempting physical compilation of generated code...")
        
        # 🧪 Test de compilation
        success, error_log = self._check_syntax(code_to_test)
        
        if success:
            print(f"   [+] Syntax Check PASSED.")
            return {
                "worker": self.name,
                "status": "completed",
                "summary": "C syntax check passed successfully.",
                "artifacts": [{"name": "compile_log.txt", "type": "text", "content": "OK"}],
                "risks": [],
                "recommendations": ["Code is syntactically valid C."]
            }
        else:
            print(f"   [!] Syntax Check FAILED.")
            # On renvoie l'erreur détaillée pour que le Sleep Engine ou le Feedback Loop s'en serve
            return {
                "worker": self.name,
                "status": "failed",
                "summary": "C syntax check failed.",
                "artifacts": [{"name": "compiler_errors.txt", "type": "text", "content": error_log}],
                "risks": ["Generated code contains syntax errors"],
                "recommendations": ["Fix the following errors: " + error_log[:200]]
            }

    def _check_syntax(self, code):
        """Appelle gcc (si disponible) pour une vérification de syntaxe uniquement."""
        with tempfile.NamedTemporaryFile(suffix=".c", delete=False, mode="w") as tmp:
            tmp.write(code)
            tmp_path = tmp.name
            
        try:
            # -fsyntax-only : vérifie la syntaxe sans générer d'objet (très rapide)
            # -ffreestanding : essentiel pour le bare-metal (pas de stdlib)
            cmd = ["gcc", "-fsyntax-only", "-ffreestanding", tmp_path]
            result = subprocess.run(cmd, capture_output=True, text=True)
            return (result.returncode == 0, result.stderr)
        except FileNotFoundError:
            return (False, "GCC not found on system. Please install a C compiler.")
        finally:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)

    def _error_response(self, msg):
        return {
            "worker": self.name,
            "status": "failed",
            "summary": msg,
            "artifacts": [],
            "risks": ["Internal worker error"],
            "recommendations": []
        }
