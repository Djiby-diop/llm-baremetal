from __future__ import annotations

import json
import time
from dataclasses import asdict, dataclass
from pathlib import Path

from .paths import user_data_root


VALID_STATUSES = {"pending", "approved", "rejected"}
PATCH_STATUSES = {"none", "draft", "applied"}


def _proposal_store_path() -> Path:
    path = user_data_root() / "system_writer" / "proposals.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def _new_proposal_id(now: int | None = None) -> str:
    return f"proposal-{now or int(time.time())}"


@dataclass(frozen=True)
class SystemProposal:
    id: str
    title: str
    goal: str
    summary: str
    files: list[str]
    risk_level: str
    status: str
    patch_text: str
    patch_status: str
    applied_at_unix: int
    created_at_unix: int
    updated_at_unix: int


def _empty_store() -> dict[str, object]:
    return {"proposals": []}


def _load_store() -> dict[str, object]:
    path = _proposal_store_path()
    if not path.exists():
        return _empty_store()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict) and isinstance(data.get("proposals"), list):
            return data
    except Exception:
        pass
    return _empty_store()


def _save_store(store: dict[str, object]) -> None:
    _proposal_store_path().write_text(json.dumps(store, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def _coerce_proposal(data: object) -> SystemProposal | None:
    if not isinstance(data, dict):
        return None
    status = str(data.get("status") or "pending")
    if status not in VALID_STATUSES:
        status = "pending"
    patch_status = str(data.get("patch_status") or "none")
    if patch_status not in PATCH_STATUSES:
        patch_status = "none"
    files = data.get("files")
    return SystemProposal(
        id=str(data.get("id") or ""),
        title=str(data.get("title") or ""),
        goal=str(data.get("goal") or ""),
        summary=str(data.get("summary") or ""),
        files=[str(item) for item in files] if isinstance(files, list) else [],
        risk_level=str(data.get("risk_level") or "medium"),
        status=status,
        patch_text=str(data.get("patch_text") or ""),
        patch_status=patch_status,
        applied_at_unix=int(data.get("applied_at_unix") or 0),
        created_at_unix=int(data.get("created_at_unix") or 0),
        updated_at_unix=int(data.get("updated_at_unix") or 0),
    )


def list_proposals(status: str | None = None) -> list[SystemProposal]:
    wanted = str(status or "").strip().lower()
    items: list[SystemProposal] = []
    for raw in _load_store().get("proposals", []):
        proposal = _coerce_proposal(raw)
        if proposal is None:
            continue
        if wanted and proposal.status != wanted:
            continue
        items.append(proposal)
    return items


def get_proposal(proposal_id: str) -> SystemProposal | None:
    target = proposal_id.strip()
    for proposal in list_proposals():
        if proposal.id == target:
            return proposal
    return None


def create_proposal(
    *,
    title: str,
    goal: str,
    summary: str = "",
    files: list[str] | None = None,
    risk_level: str = "medium",
) -> SystemProposal:
    store = _load_store()
    now = int(time.time())
    existing_ids = {p.id for p in list_proposals()}
    proposal_id = _new_proposal_id(now)
    suffix = 2
    while proposal_id in existing_ids:
        proposal_id = f"{_new_proposal_id(now)}-{suffix}"
        suffix += 1

    proposal = SystemProposal(
        id=proposal_id,
        title=title.strip(),
        goal=goal.strip(),
        summary=summary.strip(),
        files=[item.strip() for item in list(files or []) if item.strip()],
        risk_level=risk_level.strip().lower() or "medium",
        status="pending",
        patch_text="",
        patch_status="none",
        applied_at_unix=0,
        created_at_unix=now,
        updated_at_unix=now,
    )
    proposals = store.get("proposals")
    if not isinstance(proposals, list):
        proposals = []
        store["proposals"] = proposals
    proposals.append(asdict(proposal))
    _save_store(store)
    return proposal


def update_proposal_status(proposal_id: str, status: str) -> SystemProposal | None:
    normalized = status.strip().lower()
    if normalized not in VALID_STATUSES:
        raise ValueError(f"Invalid proposal status: {status}")

    store = _load_store()
    proposals = store.get("proposals")
    if not isinstance(proposals, list):
        return None

    now = int(time.time())
    for item in proposals:
        if not isinstance(item, dict):
            continue
        if str(item.get("id") or "") != proposal_id.strip():
            continue
        item["status"] = normalized
        item["updated_at_unix"] = now
        _save_store(store)
        return _coerce_proposal(item)
    return None


def attach_patch(proposal_id: str, patch_text: str) -> SystemProposal | None:
    store = _load_store()
    proposals = store.get("proposals")
    if not isinstance(proposals, list):
        return None

    now = int(time.time())
    for item in proposals:
        if not isinstance(item, dict):
            continue
        if str(item.get("id") or "") != proposal_id.strip():
            continue
        if str(item.get("status") or "pending") == "rejected":
            raise ValueError("Cannot attach a patch to a rejected proposal.")
        item["patch_text"] = patch_text
        item["patch_status"] = "draft" if patch_text.strip() else "none"
        item["updated_at_unix"] = now
        _save_store(store)
        return _coerce_proposal(item)
    return None


def _safe_repo_path(repo_root: Path, relative_path: str) -> Path:
    cleaned = relative_path.replace("\\", "/").strip()
    if not cleaned or cleaned.startswith("/") or ".." in Path(cleaned).parts:
        raise ValueError(f"Unsafe patch path: {relative_path}")
    root = repo_root.resolve()
    target = (root / cleaned).resolve()
    if root != target and root not in target.parents:
        raise ValueError(f"Patch path escapes repo root: {relative_path}")
    return target


def _parse_hunk_header(header: str) -> tuple[int, int]:
    # Example: @@ -1,3 +1,4 @@
    parts = header.split()
    if len(parts) < 3 or not parts[1].startswith("-") or not parts[2].startswith("+"):
        raise ValueError(f"Invalid hunk header: {header}")
    old = parts[1][1:]
    new = parts[2][1:]
    old_start = int(old.split(",", 1)[0])
    new_start = int(new.split(",", 1)[0])
    return old_start, new_start


def _apply_file_patch(repo_root: Path, old_path: str, new_path: str, lines: list[str]) -> str:
    target_path = new_path if new_path != "/dev/null" else old_path
    target = _safe_repo_path(repo_root, target_path)
    original = target.read_text(encoding="utf-8").splitlines() if target.exists() else []
    output: list[str] = []
    old_index = 0
    i = 0

    while i < len(lines):
        line = lines[i]
        if not line.startswith("@@"):
            i += 1
            continue
        old_start, _new_start = _parse_hunk_header(line)
        hunk_old_index = max(old_start - 1, 0)
        if hunk_old_index < old_index:
            raise ValueError(f"Overlapping hunk in patch for {target_path}")
        output.extend(original[old_index:hunk_old_index])
        old_index = hunk_old_index
        i += 1

        while i < len(lines) and not lines[i].startswith("@@"):
            current = lines[i]
            if current.startswith("\\"):
                i += 1
                continue
            marker = current[:1]
            content = current[1:]
            if marker == " ":
                if old_index >= len(original) or original[old_index] != content:
                    raise ValueError(f"Patch context mismatch in {target_path}")
                output.append(content)
                old_index += 1
            elif marker == "-":
                if old_index >= len(original) or original[old_index] != content:
                    raise ValueError(f"Patch removal mismatch in {target_path}")
                old_index += 1
            elif marker == "+":
                output.append(content)
            else:
                raise ValueError(f"Invalid patch line in {target_path}: {current}")
            i += 1

    output.extend(original[old_index:])
    target.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(output)
    if output:
        text += "\n"
    target.write_text(text, encoding="utf-8")
    return str(target)


def apply_unified_patch(repo_root: Path, patch_text: str) -> list[str]:
    lines = patch_text.splitlines()
    changed: list[str] = []
    i = 0
    while i < len(lines):
        if not lines[i].startswith("diff --git "):
            i += 1
            continue

        old_path = ""
        new_path = ""
        file_lines: list[str] = []
        i += 1
        while i < len(lines) and not lines[i].startswith("diff --git "):
            line = lines[i]
            if line.startswith("--- "):
                old_path = line[4:].strip()
                if old_path.startswith("a/"):
                    old_path = old_path[2:]
            elif line.startswith("+++ "):
                new_path = line[4:].strip()
                if new_path.startswith("b/"):
                    new_path = new_path[2:]
            elif line.startswith("@@") or file_lines:
                file_lines.append(line)
            i += 1

        if not new_path:
            raise ValueError("Patch file is missing a target path.")
        changed.append(_apply_file_patch(repo_root, old_path or new_path, new_path, file_lines))

    if not changed:
        raise ValueError("No file changes found in patch.")
    return changed


def apply_proposal(proposal_id: str, repo_root: Path) -> tuple[SystemProposal, list[str]] | None:
    store = _load_store()
    proposals = store.get("proposals")
    if not isinstance(proposals, list):
        return None

    now = int(time.time())
    for item in proposals:
        if not isinstance(item, dict):
            continue
        if str(item.get("id") or "") != proposal_id.strip():
            continue
        proposal = _coerce_proposal(item)
        if proposal is None:
            return None
        if proposal.status != "approved":
            raise ValueError("Proposal must be approved before applying its patch.")
        if proposal.patch_status == "applied":
            raise ValueError("Proposal patch is already applied.")
        if not proposal.patch_text.strip():
            raise ValueError("Proposal has no patch attached.")
        changed = apply_unified_patch(repo_root, proposal.patch_text)
        item["patch_status"] = "applied"
        item["applied_at_unix"] = now
        item["updated_at_unix"] = now
        _save_store(store)
        updated = _coerce_proposal(item)
        if updated is None:
            return None
        return updated, changed
    return None


def clear_proposals() -> bool:
    path = _proposal_store_path()
    if not path.exists():
        return False
    path.unlink()
    return True
