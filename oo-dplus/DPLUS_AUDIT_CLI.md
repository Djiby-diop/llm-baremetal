# dplus_audit CLI Guide

This guide covers the host-side audit inspector:

- binary: `dplus_audit`
- feature flag: `--features std`

The tool compiles and executes a `.dplus` policy in a deterministic host simulation and prints audit entries with optional filters and structured output.

## Quick Start

Run with default settings (3 simulated actions):

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus
```

## Core Options

- `--runs N`: number of simulated actions (default: `3`)
- `--action-filter ID`: keep entries for one action id
- `--verdict-filter VERDICT`: keep entries matching a verdict
- `--zone-filter ZONE`: keep entries for one memory zone
- `--reason-contains TEXT`: keep entries whose reason contains text
- `--limit N`: keep only the first `N` entries (or last `N` with `--tail`)
- `--tail`: apply `--limit` from the end instead of the start
- `--summary`: print aggregate observability summary
- `--json`: print one structured JSON document
- `--jsonl`: print one JSON line per audit entry

Verdict values:

- `allow`
- `allowwarn`
- `defer`
- `throttle`
- `monitor`
- `quarantine`
- `compensate`
- `forbid`
- `emergency`

Zone values:

- `frozen`
- `cold`
- `warm`
- `hot`
- `sentinel`
- `journal`

## Examples

Filter by action id:

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --action-filter action_2
```

Filter by verdict and emit JSON:

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --verdict-filter forbid --json
```

Filter by zone and reason substring:

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --zone-filter journal --reason-contains execution_path
```

Take last 10 entries in JSONL:

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --runs 25 --limit 10 --tail --jsonl
```

Summary mode (verdict distribution, top actions, divergence rate):

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --runs 20 --summary
```

## Output Notes

- Text mode prints one line per entry with action, verdict, zone, and reason.
- JSON mode prints one document containing metadata and the filtered entry array.
- JSONL mode prints one event per line for log pipelines and stream processors.

## Typical Workflow

1. Validate policy quickly:

```bash
cargo run --features std --bin dplus_check -- policy-strict.dplus
```

2. Inspect resulting audit trail:

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --summary
```

3. Deep-dive suspicious verdicts:

```bash
cargo run --features std --bin dplus_audit -- policy-strict.dplus --verdict-filter forbid --jsonl
```
