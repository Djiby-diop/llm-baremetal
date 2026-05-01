from __future__ import annotations

from dataclasses import dataclass

from .profile_store import save_active_profile
from .system_writer import SystemProposal, create_proposal
from .twin_store import sync_twin_from_profile


@dataclass(frozen=True)
class FirstBootPlan:
    profile: dict[str, object]
    twin: dict[str, object]
    proposal: SystemProposal
    recommended_modules: list[str]


def _clean_list(values: list[str] | None) -> list[str]:
    if values is None:
        return []
    return [str(item).strip() for item in values if str(item).strip()]


def _recommend_modules(role: str, focus: list[str]) -> list[str]:
    text = " ".join([role, *focus]).lower()
    modules = ["profile-memory", "personal-context", "human-approval"]
    if any(word in text for word in ("architect", "architecte", "design", "plan")):
        modules.extend(["workspace-planner", "visual-reference-board"])
    if any(word in text for word in ("entrepreneur", "business", "client", "strategy", "strategie")):
        modules.extend(["strategy-dashboard", "client-pipeline"])
    if any(word in text for word in ("comptable", "accounting", "finance", "invoice", "facture")):
        modules.extend(["document-ledger", "validation-checklist"])
    if any(word in text for word in ("gameur", "gamer", "game", "training", "entrainement")):
        modules.extend(["performance-coach", "session-review"])
    return list(dict.fromkeys(modules))


def prepare_first_boot(
    *,
    role: str,
    workspace_style: str = "",
    focus: list[str] | None = None,
    preferences: dict[str, object] | None = None,
) -> FirstBootPlan:
    clean_role = role.strip()
    if not clean_role:
        raise ValueError("role is required")

    clean_focus = _clean_list(focus)
    profile = save_active_profile(
        role=clean_role,
        workspace_style=workspace_style,
        focus=clean_focus,
        preferences=preferences or {},
    )
    twin = sync_twin_from_profile()
    modules = _recommend_modules(clean_role, clean_focus)
    proposal = create_proposal(
        title=f"First boot workspace for {clean_role}",
        goal=(
            "Prepare a personalized OO workspace using the active profile, "
            "personal context, and human-approved system changes."
        ),
        summary=(
            f"workspace_style={workspace_style or 'default'}; "
            f"focus={', '.join(clean_focus) or 'none'}; "
            f"modules={', '.join(modules)}"
        ),
        files=[
            "diop/profile_store.py",
            "diop/twin_store.py",
            "diop/system_writer.py",
        ],
        risk_level="medium",
    )
    return FirstBootPlan(
        profile=profile,
        twin=twin,
        proposal=proposal,
        recommended_modules=modules,
    )
