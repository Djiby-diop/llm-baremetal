from __future__ import annotations

import sys

from ...core.contracts.types import ValidationDecision, WorkerResult


class HumanValidationService:
    def review(
        self,
        results: list[WorkerResult],
        aggregate: dict[str, object],
        auto_approve: bool = False,
    ) -> ValidationDecision:
        all_risks = [risk for result in results for risk in result.risks]
        critical = [risk for risk in all_risks if "critical" in risk.lower()]

        if auto_approve and not critical:
            return ValidationDecision(
                status="approved",
                reviewer="system-auto-validator",
                rationale="Auto-approved because no critical risk marker was detected.",
            )

        # In non-interactive runs such as automated tests, fall back to a safe review result
        # instead of blocking on input.
        if not sys.stdin or not sys.stdin.isatty():
            return ValidationDecision(
                status="needs_more_analysis",
                reviewer="human-validation-layer",
                rationale="Interactive validation unavailable; defaulting to manual review required.",
                changes_requested=list(aggregate.get("recommendations", [])),
            )

        print("\n" + "=" * 40)
        print("=== HUMAN VALIDATION REQUIRED ===")
        print("=" * 40)
        print("\nAggregate Summary:")
        print("  " + str(aggregate.get("summary")))
        
        if all_risks:
            print("\nIdentified Risks:")
            for risk in all_risks:
                print(f"  - {risk}")
        
        print("\nDecision?")
        print("  [y]es (approve)")
        print("  [n]o  (needs more analysis/reject)")
        print("  [c]hanges requested (approve with changes)")
        
        try:
            choice = input("\n> ").strip().lower()
        except EOFError:
            choice = "y"

        if choice in ("y", "yes"):
            status = "approved"
            rationale = "Human explicitly approved the output."
            changes = []
        elif choice in ("c", "changes"):
            status = "approved_with_changes"
            rationale = "Human approved with mandatory changes."
            changes_input = input("Specify requested changes: ").strip()
            changes = [changes_input] if changes_input else []
        else:
            status = "needs_more_analysis"
            rationale = "Human rejected or requested more analysis."
            changes = list(aggregate.get("recommendations", []))

        return ValidationDecision(
            status=status,
            reviewer="human-validation-layer",
            rationale=rationale,
            changes_requested=changes,
        )
