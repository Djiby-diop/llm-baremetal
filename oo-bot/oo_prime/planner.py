from __future__ import annotations

from .types import ModuleHealth, utc_now_iso


def build_system_action_plan(target: str, modules: list[ModuleHealth], reminder_targets: tuple[str, ...]) -> dict:
    weak_modules = [m for m in modules if m.score < 0.55][:5]
    weak_names = [m.name for m in weak_modules]

    docs_tasks = [
        "Document governance boundaries for OS-G runtime responsibilities.",
        "Add one architecture note that maps OS-G boot path to D+ guardrails.",
    ]
    tests_tasks = [
        "Run OS-G qemu smoke assertions: OS-G (Operating System Genesis)/qemu-test.ps1.",
        "Add one negative policy test using OS-G (Operating System Genesis)/qemu-test-negative.ps1.",
    ]
    policy_tasks = [
        "Review D+ mem.allocate bounds against OS-G (Operating System Genesis)/DPLUS.md constraints.",
        "Audit @@WARDEN:POLICY sections for explicit terminal allow/deny behavior.",
    ]

    reminder_tasks = [f"Keep explicit governance reminder for: {t}." for t in reminder_targets]

    if weak_names:
        docs_tasks.append(f"Document weak modules discovered this cycle: {', '.join(weak_names)}.")
        tests_tasks.append(f"Add focused smoke checks for: {', '.join(weak_names)}.")

    return {
        "schema": "oo-prime-system-plan-v1",
        "generated_at": utc_now_iso(),
        "target": target,
        "docs": docs_tasks,
        "tests": tests_tasks,
        "policy": policy_tasks,
        "reminders": reminder_tasks,
    }
