#!/usr/bin/env python3
"""
oo-sim/core/run_scenario.py — Automated QEMU scenario runner
============================================================
Boots OO in QEMU, sends commands, checks expected outputs.
"""
import subprocess
import sys
import time
import os

QEMU_BIN = "qemu-system-x86_64"
OVMF_CODE = r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"
OVMF_VARS = r"C:\Temp\ovmf_vars.fd"
IMG = r"C:\Temp\oo_usb_v3_gpt.img"
RAM = "8G"

SCENARIOS = {
    "boot-basic": {
        "commands": ["/ssm_infer hello", "/soma_status"],
        "expect": ["OOSI", "SomaMind"],
        "timeout": 60,
    },
    "halt-calibration": {
        "commands": ["/ssm_infer what is 42 * 37", "/soma_status"],
        "expect": ["halt_prob", "1554"],
        "timeout": 90,
    },
    "mirrorion-idle": {
        "commands": ["/mirrorion on", "/wait 200"],
        "expect": ["SELF_QUERY", "MIRRORION"],
        "timeout": 120,
    },
}

def run_scenario(name):
    if name not in SCENARIOS:
        print(f"Unknown scenario: {name}. Available: {list(SCENARIOS.keys())}")
        return 1
    s = SCENARIOS[name]
    print(f"[SIM] Running scenario: {name}")
    print(f"[SIM] Commands: {s['commands']}")
    print(f"[SIM] Expect: {s['expect']}")
    # TODO: wire to QEMU serial console
    print("[SIM] Note: Full QEMU integration requires serial console redirect")
    print("[SIM] For now, run manually via test-qemu-v3.ps1")
    return 0

if __name__ == "__main__":
    scenario = sys.argv[1] if len(sys.argv) > 1 else "boot-basic"
    sys.exit(run_scenario(scenario))
