use std::fs;
use std::collections::BTreeMap;

use osg_memory_warden::dplus_compiler::bytecode::Bytecode;
use osg_memory_warden::dplus_compiler::compiler::Compiler;
use osg_memory_warden::dplus_compiler::executor::PolicyExecutor;
use osg_memory_warden::dplus_compiler::parser::parse;
use osg_memory_warden::dplus_compiler::vm::{MemoryZone, Verdict};

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
}

fn usage() -> ! {
    eprintln!(
        "usage: dplus_audit <policy.dplus> [--runs N] [--action-filter ID] [--verdict-filter VERDICT] [--zone-filter ZONE] [--reason-contains TEXT] [--limit N] [--tail] [--summary] [--json] [--jsonl] [--fail-on-verdict VERDICT] [--max-divergence-rate 0..1]"
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
    }
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

    if let Some(limit) = opts.limit {
        if entries.len() > limit {
            if opts.tail {
                entries = entries.split_off(entries.len() - limit);
            } else {
                entries.truncate(limit);
            }
        }
    }

    if opts.summary_output {
        let mut verdict_counts: BTreeMap<String, usize> = BTreeMap::new();
        let mut action_counts: BTreeMap<String, usize> = BTreeMap::new();
        let mut divergence_count = 0usize;

        for entry in &entries {
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

        println!("summary policy={} runs={} matched_entries={}", opts.path, opts.runs, entries.len());
        println!(
            "summary divergence_count={} divergence_rate={:.3}",
            divergence_count,
            if entries.is_empty() {
                0.0
            } else {
                divergence_count as f64 / entries.len() as f64
            }
        );
        for (verdict, count) in verdict_counts {
            println!("summary verdict={} count={}", verdict, count);
        }
        for (action, count) in top_actions {
            println!("summary top_action={} count={}", action, count);
        }
    }

    let divergence_count = entries
        .iter()
        .filter(|entry| entry.reasoning.contains("divergence=true"))
        .count();
    let divergence_rate = if entries.is_empty() {
        0.0
    } else {
        divergence_count as f64 / entries.len() as f64
    };

    if let Some(verdict) = opts.fail_on_verdict {
        if entries.iter().any(|entry| entry.verdict == verdict) {
            eprintln!(
                "strict-fail: found verdict {:?} in matched entries",
                verdict
            );
            std::process::exit(1);
        }
    }

    if let Some(max_rate) = opts.max_divergence_rate {
        if divergence_rate > max_rate {
            eprintln!(
                "strict-fail: divergence_rate {:.6} exceeds max {:.6}",
                divergence_rate, max_rate
            );
            std::process::exit(1);
        }
    }

    if opts.jsonl_output {
        for entry in entries {
            println!(
                "{{\"policy\":\"{}\",\"runs\":{},\"action\":\"{}\",\"verdict\":\"{:?}\",\"zone\":\"{:?}\",\"reason\":\"{}\"}}",
                escape_json(&opts.path),
                opts.runs,
                escape_json(&entry.action_id),
                entry.verdict,
                entry.zone,
                escape_json(&entry.reasoning)
            );
        }
    } else if opts.json_output {
        println!("{{");
        println!("  \"policy\": \"{}\",", escape_json(&opts.path));
        println!("  \"runs\": {},", opts.runs);
        println!("  \"matched_entries\": {},", entries.len());
        println!("  \"entries\": [");
        for (idx, entry) in entries.iter().enumerate() {
            let comma = if idx + 1 < entries.len() { "," } else { "" };
            println!(
                "    {{\"action\":\"{}\",\"verdict\":\"{:?}\",\"zone\":\"{:?}\",\"reason\":\"{}\"}}{}",
                escape_json(&entry.action_id),
                entry.verdict,
                entry.zone,
                escape_json(&entry.reasoning),
                comma
            );
        }
        println!("  ]");
        println!("}}");
    } else {
        println!("policy={} runs={} matched_entries={}", opts.path, opts.runs, entries.len());
        for entry in entries {
            println!(
                "action={} verdict={:?} zone={:?} reason={}",
                entry.action_id, entry.verdict, entry.zone, entry.reasoning
            );
        }
    }
}
