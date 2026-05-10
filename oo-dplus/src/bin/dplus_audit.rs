use std::fs;

use osg_memory_warden::dplus_compiler::bytecode::Bytecode;
use osg_memory_warden::dplus_compiler::compiler::Compiler;
use osg_memory_warden::dplus_compiler::executor::PolicyExecutor;
use osg_memory_warden::dplus_compiler::parser::parse;
use osg_memory_warden::dplus_compiler::vm::Verdict;

struct Options {
    path: String,
    runs: usize,
    action_filter: Option<String>,
    verdict_filter: Option<Verdict>,
}

fn usage() -> ! {
    eprintln!(
        "usage: dplus_audit <policy.dplus> [--runs N] [--action-filter ID] [--verdict-filter VERDICT]"
    );
    eprintln!("verdict values: allow|allowwarn|defer|throttle|monitor|quarantine|compensate|forbid|emergency");
    std::process::exit(2);
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

fn parse_options() -> Options {
    let mut args = std::env::args().skip(1);
    let path = args.next().unwrap_or_else(|| usage());

    let mut runs = 3usize;
    let mut action_filter = None;
    let mut verdict_filter = None;

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

    let entries: Vec<_> = if let Some(action_id) = &opts.action_filter {
        executor.get_journal_entries_for_action(action_id)
    } else if let Some(verdict) = opts.verdict_filter {
        executor.get_journal_entries_for_verdict(verdict)
    } else {
        executor.get_journal().entries().iter().collect()
    };

    println!("policy={} runs={} matched_entries={}", opts.path, opts.runs, entries.len());
    for entry in entries {
        println!(
            "action={} verdict={:?} zone={:?} reason={}",
            entry.action_id, entry.verdict, entry.zone, entry.reasoning
        );
    }
}
