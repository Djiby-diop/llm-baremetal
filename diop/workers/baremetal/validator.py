
import re

class BareMetalValidator:
    """
    Static analysis tool for bare-metal C code.
    Detects illegal functions and common hardware-level mistakes.
    """
    
    ILLEGAL_FUNCTIONS = [
        "malloc", "free", "realloc", "calloc",  # No heap without manager
        "printf", "scanf", "fopen", "fwrite",  # No standard I/O
        "exit", "abort",                       # No OS runtime
        "pthread_", "fork"                     # No user-space threading
    ]
    
    def validate(self, code: str) -> dict:
        issues = []
        
        # 1. Check for illegal standard functions
        for func in self.ILLEGAL_FUNCTIONS:
            if re.search(rf"\b{func}\s*\(", code):
                issues.append({
                    "severity": "CRITICAL",
                    "type": "illegal_runtime_call",
                    "message": f"Found forbidden standard call: {func}(). Bare-metal requires custom drivers."
                })
        
        # 2. Check for missing 'volatile' in hardware access
        # (Heuristic: if there's a hex address cast to a pointer, it should be volatile)
        if re.search(r"\*\s*\(\s*(uint[0-9]+_t|unsigned int)\s*\*\s*\)\s*0x", code):
            if "volatile" not in code.lower():
                issues.append({
                    "severity": "WARNING",
                    "type": "missing_volatile",
                    "message": "Hardware register access detected without 'volatile' keyword. Risk of compiler optimization bugs."
                })
        
        # 3. Domain Check (UART vs PCI)
        has_uart = "uart" in code.lower() or "16550" in code
        has_pci = "pci" in code.lower() or "0xcf8" in code.lower()
        if has_uart and has_pci:
             issues.append({
                    "severity": "WARNING",
                    "type": "domain_mix",
                    "message": "Detected mix of UART and PCI logic in the same artifact. High risk of concept confusion."
                })

        return {
            "passed": len([i for i in issues if i["severity"] == "CRITICAL"]) == 0,
            "issues": issues
        }
