from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Iterable

FAMILIES = [
    "boot_recovery",
    "operator_command",
    "journal_memory",
    "policy_safety",
    "system_reasoning",
]


ANSI_ESCAPE_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
MARKDOWN_KEY_VALUE_RE = re.compile(r"^-\s+([^:]+):\s+`?(.+?)`?$")
PUTS_COMMAND_RE = re.compile(r'puts\("\s{2}([^\"]+?)\s{2,}([^\"]+)"\);')
BACKTICK_RE = re.compile(r"`([^`]+)`")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def workspace_root(cli_value: Path | None) -> Path:
    if cli_value is not None:
        return cli_value.resolve()
    return repo_root().parent


def rel_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def stable_id(prefix: str, payload: str) -> str:
    digest = hashlib.sha1(payload.encode("utf-8")).hexdigest()[:12]
    return f"{prefix}-{digest}"


def normalize_text(text: str) -> str:
    text = ANSI_ESCAPE_RE.sub("", text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def summarize_lines(lines: Iterable[str], limit: int = 6) -> str:
    picked: list[str] = []
    for raw in lines:
        line = normalize_text(raw)
        if not line:
            continue
        picked.append(line)
        if len(picked) >= limit:
            break
    return "\n".join(picked)


def build_record(
    *,
    family: str,
    source: str,
    input_text: str,
    target_text: str,
    context: dict,
    tags: list[str],
    quality: float,
    prefix: str,
) -> dict:
    payload = "|".join(
        [
            family,
            source,
            normalize_text(input_text),
            normalize_text(target_text),
            json.dumps(context, sort_keys=True, ensure_ascii=False),
        ]
    )
    return {
        "id": stable_id(prefix, payload),
        "family": family,
        "source": source,
        "input": normalize_text(input_text),
        "target": normalize_text(target_text),
        "context": context,
        "tags": sorted(set(tags)),
        "quality": round(quality, 2),
    }


def write_jsonl(path: Path, records: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_markdown_key_values(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw_line in normalize_text(text).splitlines():
        match = MARKDOWN_KEY_VALUE_RE.match(raw_line.strip())
        if not match:
            continue
        key = match.group(1).strip().lower().replace(" ", "_")
        result[key] = match.group(2).strip()
    return result


def extract_boot_logs(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    candidates = [
        workspace / "qemu-stdout.txt",
        workspace / "qemu_wsl_output.txt",
        workspace / "qemu-fixed-test.log",
    ]

    for path in candidates:
        if not path.exists():
            continue
        text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        if not text:
            counts[rel_path(path, workspace)] = 0
            continue

        path_key = rel_path(path, workspace)
        before = len(records)
        lines = [line for line in text.splitlines() if line.strip()]
        errors = [line for line in lines if "Could not open" in line or "failed" in line.lower()]

        for line in errors[:3]:
            records.append(
                build_record(
                    family="boot_recovery",
                    source="qemu-log",
                    input_text=f"Boot/QEMU log:\n{line}",
                    target_text="Verify the referenced image or artifact exists, correct the drive/file path, and rerun QEMU.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "qemu",
                        "mode": "boot",
                        "risk": "medium",
                        "source_path": path_key,
                    },
                    tags=["boot", "qemu", "diagnostic", "missing-file"],
                    quality=0.96,
                    prefix="boot",
                )
            )

        if "Model loaded successfully" in text:
            excerpt = summarize_lines(
                [line for line in lines if "Model" in line or "Tokenizer" in line or "Generation" in line],
                limit=6,
            )
            records.append(
                build_record(
                    family="boot_recovery",
                    source="qemu-log",
                    input_text=f"Successful boot excerpt:\n{excerpt}",
                    target_text="The model and tokenizer loaded correctly; the next step is to improve generation quality or move to interactive validation.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "runtime",
                        "mode": "boot",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["boot", "qemu", "success", "model-load"],
                    quality=0.84,
                    prefix="boot",
                )
            )

        counts[path_key] = len(records) - before

    return records, counts


def extract_autorun_commands(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    tests_dir = workspace / "llm-baremetal" / "tests"
    command_targets = {
        "/oo_continuity_status": "Compare the handoff receipt, the local OO state, and the recovery checkpoint, then report a summary and reason.",
        "/oo_consult": "Run the model-backed OO consult path and produce a safe adaptation decision.",
        "/oo_consult_mock": "Run a deterministic consult without a loaded model and record the suggested adaptation outcome.",
        "/oo_log": "Tail the latest OO consult log so the operator can inspect the persisted decision.",
        "/oo_outcome": "Report the confirmed next-boot outcome metrics from prior consult actions.",
        "/oo_jour": "Show the compact OO journal so recent events remain inspectable across boots.",
        "/help oo_consult": "Explain the consult command, expected environment, and available parameters.",
    }
    candidates = sorted(tests_dir.glob("llmk-autorun-oo*.txt")) + [tests_dir / "llmk-autorun-handoff-smoke.txt"]

    for path in candidates:
        if not path.exists():
            continue
        path_key = rel_path(path, workspace)
        text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        lines = [line.strip() for line in text.splitlines() if line.strip()]
        expectation = next((line[1:].strip() for line in lines if line.startswith("# Expect:")), "")
        commands = [line for line in lines if line.startswith("/")]
        before = len(records)

        for command in commands:
            base = command.split()[0]
            target = command_targets.get(base, "Interpret the command safely, inspect the current OO state, and return the most relevant runtime action.")
            records.append(
                build_record(
                    family="operator_command",
                    source="autorun-script",
                    input_text=f"Scenario: {path.stem}\nExpectation: {expectation or 'operator-driven smoke validation'}\nCommand: {command}",
                    target_text=target,
                    context={
                        "repo": "llm-baremetal",
                        "component": "tests",
                        "mode": "operator",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["command", "autorun", path.stem],
                    quality=0.9,
                    prefix="cmd",
                )
            )

        counts[path_key] = len(records) - before

    return records, counts


def extract_handoff_contract(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    path = workspace / "llm-baremetal" / "tests" / "test-qemu-handoff.ps1"
    if not path.exists():
        return [], {}

    text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
    interesting = [
        line.strip()
        for line in text.splitlines()
        if "Assert-Match $serial" in line and any(token in line for token in ["[oo_handoff]", "[oo_handoff_apply]", "[oo_handoff_receipt]", "[oo_continuity]"])
    ]
    excerpt = summarize_lines(interesting, limit=10)
    record = build_record(
        family="system_reasoning",
        source="powershell-test",
        input_text=f"Host-to-sovereign handoff smoke contract:\n{excerpt}",
        target_text="Validate handoff markers in order: export metadata, host apply result, persisted receipt, then continuity summary and reason.",
        context={
            "repo": "llm-baremetal",
            "component": "tests",
            "mode": "validation",
            "risk": "medium",
            "source_path": rel_path(path, workspace),
        },
        tags=["handoff", "continuity", "validation", "powershell"],
        quality=0.93,
        prefix="reason",
    )
    return [record], {rel_path(path, workspace): 1}


def extract_log_specs(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    spec_path = workspace / "llm-baremetal" / "docs" / "OO_SPEC.md"
    commands_path = workspace / "llm-baremetal" / "docs" / "COMMANDES.md"

    if spec_path.exists():
        text = spec_path.read_text(encoding="utf-8", errors="ignore")
        path_key = rel_path(spec_path, workspace)
        before = len(records)

        consult_examples = [line.strip() for line in text.splitlines() if line.strip().startswith("[boot=")]
        for example in consult_examples[:3]:
            records.append(
                build_record(
                    family="journal_memory",
                    source="oo-spec-consult-log",
                    input_text=f"Example OOCONSULT.LOG line\n{example}",
                    target_text="Interpret the boot index, mode, resource state, suggestion, decision, and whether the action was auto-applied.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "docs",
                        "mode": "consult-log",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["consult-log", "journal", "format", "ooconsult"],
                    quality=0.94,
                    prefix="jour",
                )
            )

        journal_markers = []
        for line in text.splitlines():
            if "Journal:" not in line and "journal)" not in line.lower():
                continue
            journal_markers.extend(BACKTICK_RE.findall(line))
        seen_markers: set[str] = set()
        for marker in journal_markers:
            if "oo event=" not in marker or marker in seen_markers:
                continue
            seen_markers.add(marker)
            records.append(
                build_record(
                    family="journal_memory",
                    source="oo-spec-journal",
                    input_text=f"Example OOJOUR.LOG marker\n{marker}",
                    target_text="Use this journal marker as a compact, persistent event describing the consult, auto-apply, or reboot state transition.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "docs",
                        "mode": "journal-log",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["journal", "marker", "oojour", "event"],
                    quality=0.91,
                    prefix="jour",
                )
            )

        if "reduce blocks increase" in text and "oo_auto_apply=0|1|2" in text:
            records.append(
                build_record(
                    family="policy_safety",
                    source="oo-spec-policy",
                    input_text="Consult policy rules: stable > reboot > reduce, reduce blocks increase, and oo_auto_apply=0|1|2 controls automatic application.",
                    target_text="Prefer safety-first reductions, never auto-apply reboot/model changes, and throttle automatic adaptation to avoid feedback spirals.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "docs",
                        "mode": "policy",
                        "risk": "medium",
                        "source_path": path_key,
                    },
                    tags=["policy", "consult", "auto-apply", "safety"],
                    quality=0.93,
                    prefix="policy",
                )
            )

        counts[path_key] = len(records) - before

    if commands_path.exists():
        text = normalize_text(commands_path.read_text(encoding="utf-8", errors="ignore"))
        path_key = rel_path(commands_path, workspace)
        before = len(records)
        marker_lines = [line.strip() for line in text.splitlines() if line.strip().startswith("- `") and "last.consult." in line]
        if marker_lines:
            records.append(
                build_record(
                    family="system_reasoning",
                    source="oo-command-doc",
                    input_text=f"Higher-level consult fields\n{summarize_lines(marker_lines, limit=4)}",
                    target_text="Use boot relation, trend, saturation, and operator summary together to explain why the latest consult action was or was not applied.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "docs",
                        "mode": "explainability",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["consult", "explainability", "operator-summary"],
                    quality=0.9,
                    prefix="reason",
                )
            )
        counts[path_key] = counts.get(path_key, 0) + (len(records) - before)

    return records, counts


def extract_validation_log_contracts(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    path = workspace / "llm-baremetal" / "scripts" / "validate-real-hw-oo-artifacts.ps1"
    if not path.exists():
        return [], {}

    text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
    path_key = rel_path(path, workspace)
    records: list[dict] = []
    marker_lines = [
        line.strip()
        for line in text.splitlines()
        if "Assert-Condition" in line and any(token in line for token in ["OOJOUR.LOG", "OOCONSULT.LOG", "oo event=", "consult b="])
    ]

    if marker_lines:
        records.append(
            build_record(
                family="system_reasoning",
                source="validation-script",
                input_text=f"Real-hardware OO log validation contract\n{summarize_lines(marker_lines, limit=8)}",
                target_text="A valid artifact set needs OOJOUR.LOG with journal events and, when consult is enabled, OOCONSULT.LOG with consult record, decision, score, and matching journal completion markers.",
                context={
                    "repo": "llm-baremetal",
                    "component": "scripts",
                    "mode": "validation",
                    "risk": "medium",
                    "source_path": path_key,
                },
                tags=["validation", "ooconsult", "oojour", "artifacts"],
                quality=0.95,
                prefix="reason",
            )
        )

    consult_pattern = next((line.strip() for line in text.splitlines() if "consult b=" in line), "")
    if consult_pattern:
        records.append(
            build_record(
                family="journal_memory",
                source="validation-script",
                input_text=f"Expected consult log pattern\n{consult_pattern}",
                target_text="Parse compact consult records by checking for boot index, decision field, and confidence score before trusting the persisted consult state.",
                context={
                    "repo": "llm-baremetal",
                    "component": "scripts",
                    "mode": "consult-log",
                    "risk": "low",
                    "source_path": path_key,
                },
                tags=["consult-log", "validation", "pattern"],
                quality=0.88,
                prefix="jour",
            )
        )

    return records, {path_key: len(records)}


def extract_collected_artifact_summaries(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    raw_dir = repo_root() / "data" / "raw"
    if not raw_dir.exists():
        return [], {}

    for path in sorted(raw_dir.glob("**/oo-artifacts-summary.txt")):
        text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        if not text:
            continue

        present: dict[str, int] = {}
        source_value = ""
        for line in text.splitlines():
            if line.startswith("source="):
                source_value = line.split("=", 1)[1].strip()
                continue
            match = re.match(r"^([^:]+): present=(\d+) bytes=(\d+)$", line)
            if match:
                present[match.group(1)] = int(match.group(2))

        path_key = rel_path(path, workspace)
        before = len(records)
        missing = [name for name, is_present in present.items() if not is_present]
        available = [name for name, is_present in present.items() if is_present]
        image_name = Path(source_value).name if source_value else path.parent.name

        if missing:
            records.append(
                build_record(
                    family="boot_recovery",
                    source="artifact-summary",
                    input_text=(
                        f"Collected image artifact summary for {image_name}\n"
                        f"Missing artifacts: {', '.join(missing)}"
                    ),
                    target_text="This image appears pristine or pre-validation; boot it through the OO validation flow before expecting persisted consult, journal, recovery, or handoff artifacts.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "artifacts",
                        "mode": "image-inspection",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["boot", "artifacts", "image", "missing-artifacts"],
                    quality=0.9,
                    prefix="boot",
                )
            )

        if available:
            records.append(
                build_record(
                    family="journal_memory",
                    source="artifact-summary",
                    input_text=(
                        f"Collected image artifact summary for {image_name}\n"
                        f"Available artifacts: {', '.join(available)}"
                    ),
                    target_text="Use the extracted artifacts as the authoritative persisted runtime record for continuity, consult history, and post-boot analysis.",
                    context={
                        "repo": "llm-baremetal",
                        "component": "artifacts",
                        "mode": "image-inspection",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["artifacts", "image", "persistence"],
                    quality=0.92,
                    prefix="jour",
                )
            )

        counts[path_key] = len(records) - before

    return records, counts


def extract_collected_runtime_artifacts(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    raw_dir = repo_root() / "data" / "raw"
    if not raw_dir.exists():
        return [], {}

    for path in sorted(raw_dir.glob("**/OOCONSULT.LOG")):
        text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        if not text:
            continue

        path_key = rel_path(path, workspace)
        before = len(records)
        for line in [line.strip() for line in text.splitlines() if line.strip()][:3]:
            records.append(
                build_record(
                    family="journal_memory",
                    source="collected-ooconsult",
                    input_text=f"Collected OOCONSULT.LOG line from {path.parent.name}\n{line}",
                    target_text="Treat this as a real persisted consult record: read boot, mode, RAM/context/sequence values, decision, reason IDs, confidence gate, and whether anything was auto-applied.",
                    context={
                        "repo": "oo-model",
                        "component": "data/raw",
                        "mode": "collected-consult-log",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["consult-log", "collected", "ooconsult", "runtime-artifact"],
                    quality=0.98,
                    prefix="jour",
                )
            )

        counts[path_key] = len(records) - before

    for path in sorted(raw_dir.glob("**/OOJOUR.LOG")):
        text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        if not text:
            continue

        path_key = rel_path(path, workspace)
        before = len(records)
        markers = [line.strip() for line in text.splitlines() if line.strip().startswith("oo event=")]
        if markers:
            records.append(
                build_record(
                    family="journal_memory",
                    source="collected-oojour",
                    input_text=f"Collected OOJOUR.LOG markers from {path.parent.name}\n{summarize_lines(markers, limit=6)}",
                    target_text="Interpret these as the persisted event sequence for the boot: initialization, command start, confidence/plan gating, consult completion, and follow-up logging.",
                    context={
                        "repo": "oo-model",
                        "component": "data/raw",
                        "mode": "collected-journal-log",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["journal", "collected", "oojour", "runtime-artifact"],
                    quality=0.97,
                    prefix="jour",
                )
            )

        counts[path_key] = counts.get(path_key, 0) + (len(records) - before)

    return records, counts


def extract_host_journal(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    path = workspace / "oo-host" / "data" / "organism_journal.jsonl"
    if not path.exists():
        return [], {}

    records: list[dict] = []
    path_key = rel_path(path, workspace)
    with path.open("r", encoding="utf-8") as handle:
        for idx, raw in enumerate(handle):
            line = raw.strip()
            if not line:
                continue
            event = json.loads(line)
            summary = event.get("summary") or event.get("kind") or "journal event"
            target = summary
            if event.get("result"):
                target = f"{summary}; result={event['result']}"
            if event.get("action"):
                target = f"{target}; action={event['action']}"

            records.append(
                build_record(
                    family="journal_memory",
                    source="host-journal",
                    input_text=(
                        f"OO host event\n"
                        f"kind={event.get('kind')} severity={event.get('severity')} action={event.get('action')} "
                        f"result={event.get('result')} continuity_epoch={event.get('continuity_epoch')}"
                    ),
                    target_text=target,
                    context={
                        "repo": "oo-host",
                        "component": "journal",
                        "mode": "host",
                        "risk": "low",
                        "source_path": path_key,
                        "event_index": idx,
                    },
                    tags=["journal", "host", str(event.get("kind", "event"))],
                    quality=0.91,
                    prefix="jour",
                )
            )

            if len(records) >= 40:
                break

    return records, {path_key: len(records)}


def extract_host_export(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    candidates = [
        workspace / "oo-host" / "data" / "sovereign_export.json",
        workspace / "oo-host" / "data" / "organism_state.json",
        workspace / "oo-host" / "data" / "organism_recovery.json",
    ]

    for path in candidates:
        if not path.exists():
            continue
        doc = read_json(path)
        path_key = rel_path(path, workspace)
        before = len(records)

        records.append(
            build_record(
                family="journal_memory",
                source="host-state",
                input_text=(
                    f"State snapshot from {path.name}\n"
                    f"mode={doc.get('mode')} continuity_epoch={doc.get('continuity_epoch')} "
                    f"last_recovery_reason={doc.get('last_recovery_reason')}"
                ),
                target_text="Report the current mode, continuity epoch, and the most recent recovery reason in a compact continuity snapshot.",
                context={
                    "repo": "oo-host",
                    "component": "state",
                    "mode": "host",
                    "risk": "low",
                    "source_path": path_key,
                },
                tags=["state", "continuity", "host"],
                quality=0.95,
                prefix="state",
            )
        )

        policy = doc.get("policy", {})
        if policy:
            records.append(
                build_record(
                    family="policy_safety",
                    source="host-policy",
                    input_text=(
                        f"Policy snapshot from {path.name}\n"
                        f"safe_first={policy.get('safe_first')} deny_by_default={policy.get('deny_by_default')} "
                        f"llm_advisory_only={policy.get('llm_advisory_only')} enforcement={policy.get('enforcement')}"
                    ),
                    target_text="Keep safety-first behavior enabled, deny by default, and treat the LLM as advisory while enforcement remains observe.",
                    context={
                        "repo": "oo-host",
                        "component": "policy",
                        "mode": "host",
                        "risk": "medium",
                        "source_path": path_key,
                    },
                    tags=["policy", "safety", "host"],
                    quality=0.94,
                    prefix="policy",
                )
            )

        recent_events = doc.get("recent_events") or []
        if recent_events:
            excerpt = summarize_lines(
                f"{event.get('kind')} {event.get('summary')} result={event.get('result')}" for event in recent_events[:6]
            )
            records.append(
                build_record(
                    family="system_reasoning",
                    source="host-export",
                    input_text=f"Recent host events\n{excerpt}",
                    target_text="The host is cycling clean startup and shutdown events; the next useful step is a deeper continuity or handoff validation rather than recovery.",
                    context={
                        "repo": "oo-host",
                        "component": "handoff",
                        "mode": "host",
                        "risk": "low",
                        "source_path": path_key,
                    },
                    tags=["handoff", "continuity", "recent-events"],
                    quality=0.88,
                    prefix="reason",
                )
            )

        counts[path_key] = len(records) - before

    return records, counts


def extract_markdown_status_files(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    records: list[dict] = []
    counts: dict[str, int] = {}
    candidates = [
        workspace / "oo-host" / "data" / "handoff-status.md",
        workspace / "oo-host" / "data" / "handoff-pack" / "handoff-status.md",
        workspace / "oo-host" / "data" / "handoff-pack" / "sync-check.txt",
        workspace / "oo-host" / "data" / "handoff-pack" / "sovereign-brief.md",
        workspace / "oo-host" / "data" / "github-sovereign" / "sovereign-brief.md",
    ]

    for path in candidates:
        if not path.exists():
            continue
        text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        info = parse_markdown_key_values(text)
        if not info:
            continue
        path_key = rel_path(path, workspace)
        before = len(records)

        continuity = info.get("continuity_context") or info.get("continuity") or "unknown"
        policy = info.get("host_policy") or info.get("policy_enforcement") or info.get("export_policy") or "unknown"
        readiness = info.get("handoff_readiness") or info.get("sovereign_handoff_contract") or info.get("sync_verdict") or "unknown"

        records.append(
            build_record(
                family="journal_memory",
                source="handoff-status",
                input_text=f"Handoff status\ncontinuity={continuity} readiness={readiness} policy={policy}",
                target_text=f"The host and sovereign handoff path is {continuity}, policy is {policy}, and readiness is {readiness}.",
                context={
                    "repo": "oo-host",
                    "component": "handoff-pack",
                    "mode": "continuity",
                    "risk": "low",
                    "source_path": path_key,
                },
                tags=["handoff", "continuity", "status"],
                quality=0.93,
                prefix="handoff",
            )
        )

        recommendations = [line.strip("- ") for line in text.splitlines() if line.strip().startswith("- ") and "recommend" not in line.lower()]
        if recommendations:
            records.append(
                build_record(
                    family="system_reasoning",
                    source="handoff-status",
                    input_text=f"Handoff recommendation context\n{summarize_lines(recommendations, limit=4)}",
                    target_text="If continuity is aligned, proceed with the next validation step; if policy is still observe, do not claim enforcement yet.",
                    context={
                        "repo": "oo-host",
                        "component": "handoff-pack",
                        "mode": "planning",
                        "risk": "medium",
                        "source_path": path_key,
                    },
                    tags=["handoff", "planning", "next-step"],
                    quality=0.87,
                    prefix="reason",
                )
            )

        counts[path_key] = len(records) - before

    return records, counts


def extract_handoff_receipt(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    path = workspace / "llm-baremetal" / "docs" / "OOHANDOFF.TXT"
    if not path.exists():
        return [], {}

    text = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
    kv: dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        kv[key.strip()] = value.strip()

    record = build_record(
        family="journal_memory",
        source="handoff-receipt",
        input_text=(
            f"Sovereign receipt\norganism_id={kv.get('organism_id')} mode={kv.get('mode')} "
            f"policy_enforcement={kv.get('policy_enforcement')} continuity_epoch={kv.get('continuity_epoch')}"
        ),
        target_text="Persist the handoff receipt exactly and use it as the continuity reference for later host ↔ sovereign comparison.",
        context={
            "repo": "llm-baremetal",
            "component": "docs",
            "mode": "continuity",
            "risk": "low",
            "source_path": rel_path(path, workspace),
        },
        tags=["handoff", "receipt", "continuity"],
        quality=0.92,
        prefix="handoff",
    )
    return [record], {rel_path(path, workspace): 1}


def extract_oo_system_cli(workspace: Path) -> tuple[list[dict], dict[str, int]]:
    path = workspace / "oo-system" / "interface" / "cli" / "src" / "oo_cli.c"
    if not path.exists():
        return [], {}

    text = path.read_text(encoding="utf-8", errors="ignore")
    records: list[dict] = []
    path_key = rel_path(path, workspace)
    seen: set[str] = set()

    in_commands_block = False
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line == 'puts("COMMANDS:");':
            in_commands_block = True
            continue
        if in_commands_block and line == 'puts("");':
            break
        if not in_commands_block:
            continue

        match = PUTS_COMMAND_RE.search(line)
        if match is None:
            continue
        command = match.group(1).strip()
        description = match.group(2).strip()
        if command in seen or command == "help":
            continue
        seen.add(command)
        records.append(
            build_record(
                family="operator_command",
                source="oo-cli-help",
                input_text=f"OO CLI command\n{command}",
                target_text=description,
                context={
                    "repo": "oo-system",
                    "component": "interface/cli",
                    "mode": "operator",
                    "risk": "low",
                    "source_path": path_key,
                },
                tags=["command", "cli", "oo-system"],
                quality=0.95,
                prefix="cmd",
            )
        )

    bridge_record = build_record(
        family="system_reasoning",
        source="oo-cli-bridge",
        input_text="oo think sends a thought over the OO Message Bus, but the bare-metal bridge is not yet active.",
        target_text="If the bridge is inactive, boot llm-baremetal in QEMU and use the REPL or /ssm_infer path before expecting a live cognitive response.",
        context={
            "repo": "oo-system",
            "component": "interface/cli",
            "mode": "bridge",
            "risk": "low",
            "source_path": path_key,
        },
        tags=["bridge", "cli", "qemu"],
        quality=0.89,
        prefix="reason",
    )
    records.append(bridge_record)

    return records, {path_key: len(records)}


def extract_records(workspace: Path) -> tuple[list[dict], dict]:
    all_records: list[dict] = []
    sources: dict[str, int] = {}
    extractors = [
        extract_boot_logs,
        extract_autorun_commands,
        extract_handoff_contract,
        extract_log_specs,
        extract_validation_log_contracts,
        extract_collected_runtime_artifacts,
        extract_collected_artifact_summaries,
        extract_host_journal,
        extract_host_export,
        extract_markdown_status_files,
        extract_handoff_receipt,
        extract_oo_system_cli,
    ]

    for extractor in extractors:
        records, counts = extractor(workspace)
        all_records.extend(records)
        sources.update(counts)

    deduped: dict[str, dict] = {}
    for record in all_records:
        key = hashlib.sha1(
            (record["family"] + "|" + record["source"] + "|" + record["input"] + "|" + record["target"]).encode("utf-8")
        ).hexdigest()
        deduped[key] = record

    records = sorted(deduped.values(), key=lambda item: item["id"])
    family_counts: dict[str, int] = {}
    for record in records:
        family_counts[record["family"]] = family_counts.get(record["family"], 0) + 1

    manifest = {
        "workspace_root": workspace.as_posix(),
        "repo_root": repo_root().as_posix(),
        "record_count": len(records),
        "family_counts": family_counts,
        "sources": [{"path": key, "records": value} for key, value in sorted(sources.items())],
    }
    return records, manifest


def build_eval_set(records: list[dict]) -> list[dict]:
    desired = [
        ("boot_recovery", "boot"),
        ("operator_command", "command"),
        ("journal_memory", "continuity"),
        ("policy_safety", "policy"),
        ("system_reasoning", "handoff"),
    ]
    selected: list[dict] = []
    seen_ids: set[str] = set()

    for family, preferred_tag in desired:
        candidate = next(
            (
                record
                for record in records
                if record["family"] == family and record["id"] not in seen_ids and preferred_tag in record.get("tags", [])
            ),
            None,
        )
        if candidate is None:
            candidate = next((record for record in records if record["family"] == family and record["id"] not in seen_ids), None)
        if candidate is None:
            continue
        selected.append(candidate)
        seen_ids.add(candidate["id"])

    return selected


def split_records(records: list[dict], eval_records: list[dict]) -> dict[str, list[dict]]:
    eval_ids = {record["id"] for record in eval_records}
    splits = {"train": [], "valid": [], "test": [], "eval_oo": list(eval_records)}

    for record in records:
        if record["id"] in eval_ids:
            continue
        bucket = int(hashlib.sha1(record["id"].encode("utf-8")).hexdigest(), 16) % 100
        if bucket < 80:
            splits["train"].append(record)
        elif bucket < 90:
            splits["valid"].append(record)
        else:
            splits["test"].append(record)

    return splits


def ensure_file(path: Path, sample: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        return
    with path.open("w", encoding="utf-8") as handle:
        handle.write(json.dumps(sample, ensure_ascii=False) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract OO corpus sources and build the first dataset splits.")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workspace-root", type=Path, default=None)
    args = parser.parse_args()

    workspace = workspace_root(args.workspace_root)
    raw_dir = args.input.resolve()
    output_dir = args.output.resolve()

    records, manifest = extract_records(workspace)
    eval_records = build_eval_set(records)
    splits = split_records(records, eval_records)

    raw_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    write_jsonl(raw_dir / "extracted_corpus.jsonl", records)
    with (raw_dir / "source_manifest.json").open("w", encoding="utf-8") as handle:
        json.dump(
            {
                **manifest,
                "status": "extracted",
                "raw_corpus": rel_path(raw_dir / "extracted_corpus.jsonl", repo_root()),
            },
            handle,
            indent=2,
            ensure_ascii=False,
        )

    for split_name, split_records_list in splits.items():
        write_jsonl(output_dir / f"{split_name}.jsonl", split_records_list)

    args.output.mkdir(parents=True, exist_ok=True)
    with (output_dir / "manifest.json").open("w", encoding="utf-8") as handle:
        json.dump(
            {
                "input_dir": str(raw_dir),
                "output_dir": str(output_dir),
                "families": FAMILIES,
                "status": "dataset-generated",
                "record_count": len(records),
                "split_counts": {name: len(items) for name, items in splits.items()},
                "family_counts": manifest["family_counts"],
            },
            handle,
            indent=2,
            ensure_ascii=False,
        )

    print(f"oo-model dataset ready: {len(records)} records extracted")


if __name__ == "__main__":
    main()
