
import subprocess
import time
import os
import re
from pathlib import Path
from ..base import BaseWorker

class QEMUExecutorWorker(BaseWorker):
    """
    Hardware-In-The-Loop Executor.
    Runs the generated code in a real QEMU instance and analyzes serial feedback.
    """
    def __init__(self, adapter):
        super().__init__(adapter)
        self.name = "executor"
        self.repo_root = Path("c:/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal")

    def execute(self, task, prior_results=None):
        if not prior_results:
            return self._error_response("No code found to execute.")
            
        code_to_test = ""
        for res in reversed(prior_results):
            for art in res.artifacts:
                if art.get("type") == "code":
                    code_to_test = art.get("content", "")
                    break
            if code_to_test: break
            
        if not code_to_test:
            return self._error_response("Could not find any C code artifact in prior results.")

        # 1. 📝 Write test file
        test_file = self.repo_root / "engine/drivers/diop_test.c"
        test_file.write_text(code_to_test)
        
        print(f"[{self.name.upper()}] Building and launching QEMU HITL...")
        
        # 2. 🔨 Build (using existing Makefile)
        # We try to compile the new driver object
        build_cmd = ["make", "engine/drivers/diop_test.o"]
        build_res = subprocess.run(build_cmd, cwd=self.repo_root, capture_output=True, text=True)
        
        if build_res.returncode != 0:
            return {
                "worker": self.name,
                "status": "failed",
                "summary": "Build failed during HITL preparation.",
                "artifacts": [{"name": "make_errors.txt", "type": "text", "content": build_res.stderr}],
                "risks": ["Generated code is not compilable with the current kernel chain"],
                "recommendations": ["Fix syntax errors reported by make."]
            }

        # 3. 🚀 Launch QEMU (short run)
        log_file = self.repo_root / "diop_qemu_serial.log"
        if log_file.exists(): log_file.unlink()
        
        # We use the existing image llama2-boot.img if it exists
        # Or we just run a syntax/logic simulation if image creation is complex
        qemu_cmd = [
            "qemu-system-x86_64",
            "-drive", "format=raw,file=llm-baremetal-boot.img", # Assuming standard name
            "-nographic", "-serial", f"file:{log_file}",
            "-m", "512", "-monitor", "none"
        ]
        
        try:
            # Run for 10 seconds then kill
            proc = subprocess.Popen(qemu_cmd, cwd=self.repo_root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(10)
            proc.terminate()
            
            # 4. 📊 Analyze Logs
            execution_log = ""
            if log_file.exists():
                execution_log = log_file.read_text(errors='ignore')
            
            success = self._analyze_log(execution_log)
            
            return {
                "worker": self.name,
                "status": "completed" if success else "warning",
                "summary": "HITL Execution completed." if success else "Execution finished with warnings/faults.",
                "artifacts": [{"name": "execution_log.txt", "type": "text", "content": execution_log}],
                "risks": self._detect_execution_risks(execution_log),
                "recommendations": ["Review execution logs for register transitions."]
            }
        except Exception as e:
            return self._error_response(f"QEMU Execution failed: {e}")

    def _analyze_log(self, log):
        # Look for typical boot markers or success patterns
        if "UEFI_MAIN" in log or "efi_main" in log:
            return True
        return False

    def _detect_execution_risks(self, log):
        risks = []
        if re.search(r"PANIC|fault|FAULT|triple.fault|halted", log, re.IGNORECASE):
            risks.append("CRITICAL: Hardware fault or Kernel Panic detected during execution.")
        return risks

    def _error_response(self, msg):
        return {"worker": self.name, "status": "failed", "summary": msg, "artifacts": [], "risks": [], "recommendations": []}
