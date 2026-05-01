from __future__ import annotations

from pathlib import Path

from .types import ModuleHealth


IGNORED_PREFIXES = (".", "__")
MODULE_HINTS = ("-engine", "oo-", "-kernel", "-bus")


def _is_module_folder(path: Path) -> bool:
    if not path.is_dir():
        return False
    name = path.name.lower()
    if name.startswith(IGNORED_PREFIXES):
        return False
    if name in {"os-g", "operating system genesis", "os-g (operating system genesis)"}:
        return True
    if "operating system genesis" in name:
        return True
    return any(hint in name for hint in MODULE_HINTS)


def _has_tests(path: Path) -> bool:
    if (path / "tests").exists():
        return True
    for candidate in path.glob("test*.*"):
        if candidate.is_file():
            return True
    return False


def _has_local_policy(path: Path) -> bool:
    policy_names = ("policy.dplus", "default.dplus", "OOPOLICY.CRC", "OOPOLICY.BIN")
    return any((path / n).exists() for n in policy_names)


def observe_modules(root: Path, limit: int = 64) -> list[ModuleHealth]:
    modules: list[ModuleHealth] = []

    for child in sorted(root.iterdir(), key=lambda p: p.name.lower()):
        if not _is_module_folder(child):
            continue

        has_readme = (child / "README.md").exists()
        has_tests = _has_tests(child)
        has_policy = _has_local_policy(child)

        score = 0.25
        signals: list[str] = []

        if has_readme:
            score += 0.3
        else:
            signals.append("missing_readme")

        if has_tests:
            score += 0.3
        else:
            signals.append("missing_tests")

        if has_policy:
            score += 0.15
        else:
            signals.append("missing_local_policy")

        child_count = sum(1 for _ in child.iterdir())
        if child_count > 0:
            score += 0.1
        else:
            signals.append("empty_module")

        score = min(score, 1.0)

        modules.append(
            ModuleHealth(
                name=child.name,
                path=str(child),
                has_readme=has_readme,
                has_tests=has_tests,
                has_policy=has_policy,
                score=round(score, 3),
                signals=signals,
            )
        )

        if len(modules) >= limit:
            break

    return modules
