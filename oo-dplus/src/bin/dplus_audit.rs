use std::collections::BTreeMap;
use std::fs::{self, OpenOptions};
use std::io::Write;

use osg_memory_warden::dplus_compiler::bytecode::Bytecode;
use osg_memory_warden::dplus_compiler::compiler::Compiler;
use osg_memory_warden::dplus_compiler::executor::PolicyExecutor;
use osg_memory_warden::dplus_compiler::parser::parse;
use osg_memory_warden::dplus_compiler::vm::{JournalEntry, MemoryZone, Verdict};

struct Options {
    path: String,
    runs: usize,
    action_filter: Option<String>,
    verdict_filter: Option<Verdict>,
    zone_filter: Option<MemoryZone>,
    reason_contains: Option<String>,
    limit: Option<usize>,
    tail: bool,
    json_output: bool,
    jsonl_output: bool,
    summary_output: bool,
    fail_on_verdict: Option<Verdict>,
    max_divergence_rate: Option<f64>,
    output_path: Option<String>,
    append_output: bool,
}

fn usage() -> ! {
    eprintln!(
        "usage: dplus_audit <policy.dplus> [--runs N] [--action-filter ID] [--verdict-filter VERDICT] [--zone-filter ZONE] [--reason-contains TEXT] [--limit N] [--tail] [--summary] [--json] [--jsonl] [--fail-on-verdict VERDICT] [--max-divergence-rate 0..1] [--output PATH] [--append]"
    );
    eprintln!("verdict values: allow|allowwarn|defer|throttle|monitor|quarantine|compensate|forbid|emergency");
    eprintln!("zone values: frozen|cold|warm|hot|sentinel|journal");
    std::process::exit(2);
}

fn escape_json(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len() + 8);
    for ch in raw.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn parse_verdict(raw: &str) -> Option<Verdict> {
    match raw.to_ascii_lowercase().as_str() {
        "allow" => Some(Verdict::Allow),
        "allowwarn" => Some(Verdict::AllowWarn),
        "defer" => Some(Verdict::Defer),
        "throttle" => Some(Verdict::Throttle),
        "monitor" => Some(Verdict::Monitor),
        "quarantine" => Some(Verdict::Quarantine),
        "compensate" => Some(Verdict::Compensate),
        "forbid" => Some(Verdict::Forbid),
        "emergency" => Some(Verdict::Emergency),
        _ => None,
    }
}

fn parse_zone(raw: &str) -> Option<MemoryZone> {
    match raw.to_ascii_lowercase().as_str() {
        "frozen" => Some(MemoryZone::Frozen),
        "cold" => Some(MemoryZone::Cold),
        "warm" => Some(MemoryZone::Warm),
        "hot" => Some(MemoryZone::Hot),
        "sentinel" => Some(MemoryZone::Sentinel),
        "journal" => Some(MemoryZone::Journal),
        _ => None,
    }
}

fn parse_options() -> Options {
    let mut args = std::env::args().skip(1);
    let path = args.next().unwrap_or_else(|| usage());

    let mut runs = 3usize;
    let mut action_filter = None;
    let mut verdict_filter = None;
    let mut zone_filter = None;
    let mut reason_contains = None;
    let mut limit = None;
    let mut tail = false;
    let mut json_output = false;
    let mut jsonl_output = false;
    let mut summary_output = false;
    let mut fail_on_verdict = None;
    let mut max_divergence_rate = None;
    let mut output_path = None;
    let mut append_output = false;

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--runs" => {
                let v = args.next().unwrap_or_else(|| usage());
                runs = v.parse::<usize>().unwrap_or_else(|_| {
                    eprintln!("invalid --runs value: {}", v);
                    usage();
                });
            }
            "--action-filter" => {
                action_filter = Some(args.next().unwrap_or_else(|| usage()));
            }
            "--verdict-filter" => {
                let raw = args.next().unwrap_or_else(|| usage());
                verdict_filter = parse_verdict(&raw).or_else(|| {
                    eprintln!("invalid --verdict-filter value: {}", raw);
                    usage();
                });
            }
            "--zone-filter" => {
                let raw = args.next().unwrap_or_else(|| usage());
                zone_filter = parse_zone(&raw).or_else(|| {
                    eprintln!("invalid --zone-filter value: {}", raw);
                    usage();
                });
            }
            "--reason-contains" => {
                reason_contains = Some(args.next().unwrap_or_else(|| usage()));
            }
            "--limit" => {
                let v = args.next().unwrap_or_else(|| usage());
                limit = Some(v.parse::<usize>().unwrap_or_else(|_| {
                    eprintln!("invalid --limit value: {}", v);
                    usage();
                }));
            }
            "--tail" => {
                tail = true;
            }
            "--json" => {
                json_output = true;
            }
            "--jsonl" => {
                jsonl_output = true;
            }
            "--summary" => {
                summary_output = true;
            }
            "--fail-on-verdict" => {
                let raw = args.next().unwrap_or_else(|| usage());
                fail_on_verdict = parse_verdict(&raw).or_else(|| {
                    eprintln!("invalid --fail-on-verdict value: {}", raw);
                    usage();
                });
            }
            "--max-divergence-rate" => {
                let raw = args.next().unwrap_or_else(|| usage());
                let parsed = raw.parse::<f64>().unwrap_or_else(|_| {
                    eprintln!("invalid --max-divergence-rate value: {}", raw);
                    usage();
                });
                if !(0.0..=1.0).contains(&parsed) {
                    eprintln!("--max-divergence-rate must be between 0.0 and 1.0");
                    usage();
                }
                max_divergence_rate = Some(parsed);
            }
            "--output" => {
                output_path = Some(args.next().unwrap_or_else(|| usage()));
            }
            "--append" => {
                append_output = true;
            }
            "-h" | "--help" => usage(),
            _ => {
                eprintln!("unknown argument: {}", arg);
                usage();
            }
        }
    }

    Options {
        path,
        runs,
        action_filter,
        verdict_filter,
        zone_filter,
        reason_contains,
        limit,
        tail,
        json_output,
        jsonl_output,
        summary_output,
        fail_on_verdict,
        max_divergence_rate,
        output_path,
        append_output,
    }
}

fn apply_limit_tail<'a>(mut entries: Vec<&'a JournalEntry>, limit: Option<usize>, tail: bool) -> Vec<&'a JournalEntry> {
    if let Some(limit) = limit {
        if entries.len() > limit {
            if tail {
                entries = entries.split_off(entries.len() - limit);
            } else {
                entries.truncate(limit);
            }
        }
    }
    entries
}

fn divergence_rate(entries: &[&JournalEntry]) -> f64 {
    if entries.is_empty() {
        return 0.0;
    }

    let divergence_count = entries
        .iter()
        .filter(|entry| entry.reasoning.contains("divergence=true"))
        .count();

    divergence_count as f64 / entries.len() as f64
}

fn strict_failure_reason(opts: &Options, entries: &[&JournalEntry]) -> Option<String> {
    if let Some(verdict) = opts.fail_on_verdict {
        if entries.iter().any(|entry| entry.verdict == verdict) {
            return Some(format!(
                "strict-fail: found verdict {:?} in matched entries",
                verdict
            ));
        }
    }

    if let Some(max_rate) = opts.max_divergence_rate {
        let rate = divergence_rate(entries);
        if rate > max_rate {
            return Some(format!(
                "strict-fail: divergence_rate {:.6} exceeds max {:.6}",
                rate, max_rate
            ));
        }
    }

    None
}

fn build_output(opts: &Options, entries: &[&JournalEntry]) -> String {
    let mut out = String::new();

    if opts.summary_output {
        let mut verdict_counts: BTreeMap<String, usize> = BTreeMap::new();
        let mut action_counts: BTreeMap<String, usize> = BTreeMap::new();
        let mut divergence_count = 0usize;

        for entry in entries {
            *verdict_counts
                .entry(format!("{:?}", entry.verdict))
                .or_insert(0) += 1;
            *action_counts.entry(entry.action_id.clone()).or_insert(0) += 1;
            if entry.reasoning.contains("divergence=true") {
                divergence_count += 1;
            }
        }

        let mut top_actions = action_counts.into_iter().collect::<Vec<_>>();
        top_actions.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
        top_actions.truncate(5);

        out.push_str(&format!(
            "summary policy={} runs={} matched_entries={}\n",
            opts.path,
            opts.runs,
            entries.len()
        ));
        out.push_str(&format!(
            "summary divergence_count={} divergence_rate={:.3}\n",
            divergence_count,
            if entries.is_empty() {
                0.0
            } else {
                divergence_count as f64 / entries.len() as f64
            }
        ));
        for (verdict, count) in verdict_counts {
            out.push_str(&format!("summary verdict={} count={}\n", verdict, count));
        }
        for (action, count) in top_actions {
            out.push_str(&format!("summary top_action={} count={}\n", action, count));
        }
    }

    if opts.jsonl_output {
        for entry in entries {
            out.push_str(&format!(
                "{{\"policy\":\"{}\",\"runs\":{},\"action\":\"{}\",\"verdict\":\"{:?}\",\"zone\":\"{:?}\",\"reason\":\"{}\"}}\n",
                escape_json(&opts.path),
                opts.runs,
                escape_json(&entry.action_id),
                entry.verdict,
                entry.zone,
                escape_json(&entry.reasoning)
            ));
        }
    } else if opts.json_output {
        out.push_str("{\n");
        out.push_str(&format!("  \"policy\": \"{}\",\n", escape_json(&opts.path)));
        out.push_str(&format!("  \"runs\": {},\n", opts.runs));
        out.push_str(&format!("  \"matched_entries\": {},\n", entries.len()));
        out.push_str("  \"entries\": [\n");
        for (idx, entry) in entries.iter().enumerate() {
            let comma = if idx + 1 < entries.len() { "," } else { "" };
            out.push_str(&format!(
                "    {{\"action\":\"{}\",\"verdict\":\"{:?}\",\"zone\":\"{:?}\",\"reason\":\"{}\"}}{}\n",
                escape_json(&entry.action_id),
                entry.verdict,
                entry.zone,
                escape_json(&entry.reasoning),
                comma
            ));
        }
        out.push_str("  ]\n");
        out.push_str("}\n");
    } else {
        out.push_str(&format!(
            "policy={} runs={} matched_entries={}\n",
            opts.path,
            opts.runs,
            entries.len()
        ));
        for entry in entries {
            out.push_str(&format!(
                "action={} verdict={:?} zone={:?} reason={}\n",
                entry.action_id, entry.verdict, entry.zone, entry.reasoning
            ));
        }
    }

    out
}

fn emit_output(text: &str, output_path: Option<&str>, append: bool) -> Result<(), String> {
    if let Some(path) = output_path {
        let mut writer = OpenOptions::new()
            .create(true)
            .write(true)
            .append(append)
            .truncate(!append)
            .open(path)
            .map_err(|e| format!("output open failed: {e}"))?;
        writer
            .write_all(text.as_bytes())
            .map_err(|e| format!("output write failed: {e}"))?;
        writer.flush().map_err(|e| format!("output flush failed: {e}"))?;
        return Ok(());
    }

    print!("{}", text);
    Ok(())
}

fn main() {
    let opts = parse_options();

    let src = fs::read_to_string(&opts.path).unwrap_or_else(|e| {
        eprintln!("read failed: {e}");
        std::process::exit(2);
    });

    let ast = parse(&src).unwrap_or_else(|e| {
        eprintln!("parse failed: {e}");
        std::process::exit(1);
    });

    let mut compiler = Compiler::new();
    let module = compiler.compile(&ast).unwrap_or_else(|e| {
        eprintln!("compile failed: {e}");
        std::process::exit(1);
    });

    let mut executor = PolicyExecutor::new("CLI_AUDIT".to_string(), &module).unwrap_or_else(|e| {
        eprintln!("executor init failed: {e}");
        std::process::exit(1);
    });

    for idx in 0..opts.runs {
        let action_id = format!("action_{}", idx + 1);
        let result = executor.execute_action(
            &action_id,
            vec![Bytecode::LoadBool(true), Bytecode::Return],
        );
        if let Err(e) = result {
            eprintln!("execution failed for {}: {e}", action_id);
            std::process::exit(1);
        }
    }

    let mut entries: Vec<_> = executor
        .get_journal()
        .entries()
        .iter()
        .filter(|entry| {
            if let Some(action_id) = &opts.action_filter {
                if &entry.action_id != action_id {
                    return false;
                }
            }

            if let Some(verdict) = opts.verdict_filter {
                if entry.verdict != verdict {
                    return false;
                }
            }

            if let Some(zone) = opts.zone_filter {
                if entry.zone != zone {
                    return false;
                }
            }

            if let Some(sub) = &opts.reason_contains {
                if !entry.reasoning.contains(sub) {
                    return false;
                }
            }

            true
        })
        .collect();

    entries = apply_limit_tail(entries, opts.limit, opts.tail);

    if let Some(reason) = strict_failure_reason(&opts, &entries) {
        eprintln!("{}", reason);
        std::process::exit(1);
    }

    let output = build_output(&opts, &entries);
    if let Err(err) = emit_output(&output, opts.output_path.as_deref(), opts.append_output) {
        eprintln!("{}", err);
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;
    use std::fs::read_to_string;

    fn sample_opts() -> Options {
        Options {
            path: "policy.dplus".to_string(),
            runs: 3,
            action_filter: None,
            verdict_filter: None,
            zone_filter: None,
            reason_contains: None,
            limit: None,
            tail: false,
            json_output: false,
            jsonl_output: false,
            summary_output: false,
            fail_on_verdict: None,
            max_divergence_rate: None,
            output_path: None,
            append_output: false,
        }
    }

    fn entry(verdict: Verdict, reasoning: &str) -> JournalEntry {
        JournalEntry {
            timestamp: 0,
            action_id: "a1".to_string(),
            verdict,
            zone: MemoryZone::Warm,
            reasoning: reasoning.to_string(),
        }
    }

    #[test]
    fn strict_mode_fails_when_target_verdict_present() {
        let mut opts = sample_opts();
        opts.fail_on_verdict = Some(Verdict::Forbid);

        let e1 = entry(Verdict::Allow, "divergence=false");
        let e2 = entry(Verdict::Forbid, "divergence=false");
        let entries = vec![&e1, &e2];

        let reason = strict_failure_reason(&opts, &entries);
        assert!(reason.is_some());
        assert!(reason.unwrap().contains("found verdict Forbid"));
    }

    #[test]
    fn strict_mode_fails_when_divergence_rate_exceeds_threshold() {
        let mut opts = sample_opts();
        opts.max_divergence_rate = Some(0.4);

        let e1 = entry(Verdict::Allow, "divergence=true");
        let e2 = entry(Verdict::Allow, "divergence=true");
        let e3 = entry(Verdict::Allow, "divergence=false");
        let entries = vec![&e1, &e2, &e3];

        let reason = strict_failure_reason(&opts, &entries);
        assert!(reason.is_some());
        assert!(reason.unwrap().contains("divergence_rate"));
    }

    #[test]
    fn strict_mode_passes_when_below_threshold_and_no_forbidden_verdict() {
        let mut opts = sample_opts();
        opts.fail_on_verdict = Some(Verdict::Emergency);
        opts.max_divergence_rate = Some(0.5);

        let e1 = entry(Verdict::Allow, "divergence=true");
        let e2 = entry(Verdict::AllowWarn, "divergence=false");
        let e3 = entry(Verdict::Defer, "divergence=false");
        let entries = vec![&e1, &e2, &e3];

        let reason = strict_failure_reason(&opts, &entries);
        assert!(reason.is_none());
    }

    #[test]
    fn strict_mode_passes_when_divergence_rate_equals_threshold() {
        let mut opts = sample_opts();
        opts.max_divergence_rate = Some(0.5);

        let e1 = entry(Verdict::Allow, "divergence=true");
        let e2 = entry(Verdict::Allow, "divergence=false");
        let entries = vec![&e1, &e2];

        let reason = strict_failure_reason(&opts, &entries);
        assert!(reason.is_none());
    }

    #[test]
    fn strict_mode_passes_with_empty_entries() {
        let mut opts = sample_opts();
        opts.fail_on_verdict = Some(Verdict::Forbid);
        opts.max_divergence_rate = Some(0.0);
        let entries: Vec<&JournalEntry> = vec![];

        let reason = strict_failure_reason(&opts, &entries);
        assert!(reason.is_none());
    }

    #[test]
    fn apply_limit_tail_keeps_last_entries_when_tail_enabled() {
        let e1 = entry(Verdict::Allow, "divergence=false");
        let e2 = entry(Verdict::AllowWarn, "divergence=false");
        let e3 = entry(Verdict::Defer, "divergence=false");
        let entries = vec![&e1, &e2, &e3];

        let limited = apply_limit_tail(entries, Some(2), true);
        assert_eq!(limited.len(), 2);
        assert_eq!(limited[0].verdict, Verdict::AllowWarn);
        assert_eq!(limited[1].verdict, Verdict::Defer);
    }

    #[test]
    fn strict_mode_applies_after_limit_tail_reduction() {
        let mut opts = sample_opts();
        opts.fail_on_verdict = Some(Verdict::Forbid);

        let e1 = entry(Verdict::Forbid, "divergence=false");
        let e2 = entry(Verdict::Allow, "divergence=false");
        let e3 = entry(Verdict::AllowWarn, "divergence=false");
        let entries = vec![&e1, &e2, &e3];

        let limited = apply_limit_tail(entries, Some(2), true);
        let reason = strict_failure_reason(&opts, &limited);
        assert!(reason.is_none());
    }

    #[test]
    fn emit_output_writes_and_appends_to_file() {
        let path = env::temp_dir().join(format!(
            "dplus-audit-output-{}.txt",
            std::process::id()
        ));
        let file_path = path.to_string_lossy().to_string();

        emit_output("one\n", Some(&file_path), false).unwrap();
        emit_output("two\n", Some(&file_path), true).unwrap();

        let got = read_to_string(&file_path).unwrap();
        assert_eq!(got, "one\ntwo\n");

        let _ = std::fs::remove_file(&file_path);
    }
}
