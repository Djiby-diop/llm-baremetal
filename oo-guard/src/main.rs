use std::env;
use std::ffi::OsStr;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

const EXIT_OK: i32 = 0;
const EXIT_USAGE: i32 = 1;
const EXIT_VIOLATION: i32 = 2;

const OOJOUR_MAX_BYTES: u64 = 64 * 1024;
const OOCONSULT_MAX_BYTES: u64 = 64 * 1024;
const OOOUTCOME_MAX_BYTES: u64 = 256 * 1024;
const DELTA_MAX_BYTES: u64 = 32 * 1024;

#[derive(Debug, Clone)]
struct Args {
    cmd: String,
    root: PathBuf,
    delta: Option<PathBuf>,
    logs_dir: Option<PathBuf>,
    sources: Vec<PathBuf>,
    serial_file: Option<PathBuf>,
    tail_lines: usize,
    auto_restart: bool,
    quiet: bool,
}

fn print_usage() {
    eprintln!(
        "oo-guard: host-side validator for baremetal OO artifacts

USAGE:
  oo-guard check [--root <dir>] [--logs-dir <dir>] [--delta <file>] [--quiet]
  oo-guard prebuild [--root <dir>] [--quiet] <source.c> [<source2.c> ...]
  oo-guard watch [--serial <file>] [--tail <N>] [--auto-restart] [--quiet]

COMMANDS:
  check        Validate runtime artifacts (logs, DNA delta)
  prebuild     Analyze C sources for risk patterns (small buffers, malloc/free, loops)
  watch        Monitor serial output for runtime corruption patterns

DEFAULTS:
  --root        .
  --logs-dir    <root>  (check only)
  --delta       <root>/OO_DNA.DELTA (check only, optional)
  --serial      qemu-serial.txt  (watch only)
  --tail        100  (watch only, number of recent lines to analyze)

EXIT CODES:
  0 ok
  1 usage error
  2 invariant violation
"
    );
}

fn parse_args() -> Result<Args, String> {
    let mut it = env::args();
    let _exe = it.next().unwrap_or_else(|| "oo-guard".to_string());

    let cmd = match it.next() {
        Some(v) => v,
        None => return Err("missing command".to_string()),
    };

    let mut root = PathBuf::from(".");
    let mut delta: Option<PathBuf> = None;
    let mut logs_dir: Option<PathBuf> = None;
    let mut sources: Vec<PathBuf> = Vec::new();
    let mut serial_file: Option<PathBuf> = None;
    let mut tail_lines: usize = 100;
    let mut auto_restart = false;
    let mut quiet = false;

    let mut pending: Option<String> = None;
    for a in it {
        if let Some(flag) = pending.take() {
            match flag.as_str() {
                "--root" => root = PathBuf::from(a),
                "--delta" => delta = Some(PathBuf::from(a)),
                "--logs-dir" => logs_dir = Some(PathBuf::from(a)),
                "--serial" => serial_file = Some(PathBuf::from(a)),
                "--tail" => {
                    tail_lines = a.parse::<usize>()
                        .map_err(|_| format!("invalid --tail value: {}", a))?;
                }
                _ => return Err(format!("unknown flag {}", flag)),
            }
            continue;
        }

        match a.as_str() {
            "-h" | "--help" => return Err("help".to_string()),
            "--quiet" => quiet = true,
            "--auto-restart" => auto_restart = true,
            "--root" | "--delta" | "--logs-dir" | "--serial" | "--tail" => pending = Some(a),
            _ => {
                // For prebuild command, positional args are source files
                if cmd == "prebuild" && !a.starts_with("--") {
                    sources.push(PathBuf::from(a));
                } else {
                    return Err(format!("unexpected argument: {}", a));
                }
            }
        }
    }
    if let Some(flag) = pending {
        return Err(format!("missing value after {}", flag));
    }

    Ok(Args {
        cmd,
        root,
        delta,
        logs_dir,
        sources,
        serial_file,
        tail_lines,
        auto_restart,
        quiet,
    })
}

fn is_ascii_printable_byte(b: u8) -> bool {
    // Allow common text: space..~ plus tab? We intentionally reject tab to keep logs stable.
    (0x20..=0x7E).contains(&b)
}

fn check_ascii_text(bytes: &[u8]) -> Result<(), String> {
    for (i, &b) in bytes.iter().enumerate() {
        if b == b'\n' || b == b'\r' {
            continue;
        }
        if !is_ascii_printable_byte(b) {
            return Err(format!("non-ascii-or-control byte 0x{:02X} at offset {}", b, i));
        }
    }
    Ok(())
}

fn stat_size(path: &Path) -> io::Result<u64> {
    Ok(fs::metadata(path)?.len())
}

fn read_if_exists(path: &Path) -> io::Result<Option<Vec<u8>>> {
    match fs::read(path) {
        Ok(v) => Ok(Some(v)),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(e),
    }
}

#[derive(Default)]
struct Report {
    violations: Vec<String>,
    warnings: Vec<String>,
}

impl Report {
    fn violate(&mut self, msg: impl Into<String>) {
        self.violations.push(msg.into());
    }

    fn warn(&mut self, msg: impl Into<String>) {
        self.warnings.push(msg.into());
    }

    fn ok(&self) -> bool {
        self.violations.is_empty()
    }
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .and_then(OsStr::to_str)
        .unwrap_or("(unknown)")
        .to_string()
}

fn validate_file_header(report: &mut Report, path: &Path, max_bytes: u64) -> Option<Vec<u8>> {
    let sz = match stat_size(path) {
        Ok(v) => v,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return None,
        Err(e) => {
            report.violate(format!("{}: unable to stat: {}", path.display(), e));
            return None;
        }
    };

    if sz > max_bytes {
        report.violate(format!(
            "{}: too large ({} bytes > {} bytes)",
            path.display(),
            sz,
            max_bytes
        ));
        // Still try to read a bit for diagnostics if possible.
    }

    match fs::read(path) {
        Ok(bytes) => {
            if let Err(e) = check_ascii_text(&bytes) {
                report.violate(format!("{}: {}", path.display(), e));
            }
            Some(bytes)
        }
        Err(e) if e.kind() == io::ErrorKind::NotFound => None,
        Err(e) => {
            report.violate(format!("{}: unable to read: {}", path.display(), e));
            None
        }
    }
}

fn lines_from_bytes(bytes: &[u8]) -> Vec<String> {
    // Accept CRLF/LF; strip trailing CR.
    let s = String::from_utf8_lossy(bytes);
    s.lines()
        .map(|l| l.strip_suffix('\r').unwrap_or(l).to_string())
        .collect()
}

fn validate_oojour(report: &mut Report, path: &Path) {
    let Some(bytes) = validate_file_header(report, path, OOJOUR_MAX_BYTES) else {
        return;
    };

    for (idx, line) in lines_from_bytes(&bytes).into_iter().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        if !line.starts_with("oo event=") {
            report.violate(format!(
                "{}: line {}: expected prefix 'oo event='",
                file_name(path),
                idx + 1
            ));
            continue;
        }
        let payload = &line["oo event=".len()..];
        if payload.is_empty() {
            report.violate(format!("{}: line {}: empty event", file_name(path), idx + 1));
            continue;
        }
        if payload.len() > 160 {
            report.violate(format!(
                "{}: line {}: event too long ({} chars)",
                file_name(path),
                idx + 1,
                payload.len()
            ));
        }
    }
}

fn is_token(s: &str) -> bool {
    !s.is_empty() && s.bytes().all(|b| matches!(b, b'a'..=b'z' | b'A'..=b'Z' | b'0'..=b'9' | b'_'))
}

fn parse_kv_tokens(line: &str) -> Vec<(&str, &str)> {
    // Tokens are space-separated like: "consult b=1 m=N ..."
    // First token may be "consult" or "out" and we skip it.
    let mut out = Vec::new();
    for tok in line.split_whitespace().skip(1) {
        if let Some((k, v)) = tok.split_once('=') {
            out.push((k, v));
        }
    }
    out
}

fn validate_ooconsult(report: &mut Report, path: &Path) {
    let Some(bytes) = validate_file_header(report, path, OOCONSULT_MAX_BYTES) else {
        return;
    };

    for (idx, line) in lines_from_bytes(&bytes).into_iter().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        if !line.starts_with("consult ") {
            report.violate(format!(
                "{}: line {}: expected prefix 'consult '",
                file_name(path),
                idx + 1
            ));
            continue;
        }
        if line.len() > 240 {
            report.violate(format!(
                "{}: line {}: too long ({} chars)",
                file_name(path),
                idx + 1,
                line.len()
            ));
        }

        let kv = parse_kv_tokens(&line);
        let get = |key: &str| kv.iter().find(|(k, _)| *k == key).map(|(_, v)| *v);

        let Some(b) = get("b") else {
            report.violate(format!("{}: line {}: missing b=", file_name(path), idx + 1));
            continue;
        };
        if !b.bytes().all(|c| c.is_ascii_digit()) {
            report.violate(format!("{}: line {}: bad b=", file_name(path), idx + 1));
        }

        let Some(m) = get("m") else {
            report.violate(format!("{}: line {}: missing m=", file_name(path), idx + 1));
            continue;
        };
        if m != "N" && m != "D" && m != "S" {
            report.violate(format!("{}: line {}: bad m=", file_name(path), idx + 1));
        }

        for k in ["r", "c", "s", "sc", "th", "g"] {
            let Some(v) = get(k) else {
                report.violate(format!("{}: line {}: missing {}=", file_name(path), idx + 1, k));
                continue;
            };
            if !v.bytes().all(|c| c.is_ascii_digit()) {
                report.violate(format!("{}: line {}: bad {}=", file_name(path), idx + 1, k));
            }
        }

        let Some(d) = get("d") else {
            report.violate(format!("{}: line {}: missing d=", file_name(path), idx + 1));
            continue;
        };
        if !is_token(d) {
            report.violate(format!("{}: line {}: bad d=", file_name(path), idx + 1));
        }

        let Some(a) = get("a") else {
            report.violate(format!("{}: line {}: missing a=", file_name(path), idx + 1));
            continue;
        };
        if a != "0" && a != "1" {
            report.violate(format!("{}: line {}: bad a=", file_name(path), idx + 1));
        }

        let Some(g) = get("g") else {
            report.violate(format!("{}: line {}: missing g=", file_name(path), idx + 1));
            continue;
        };
        if g != "0" && g != "1" {
            report.violate(format!("{}: line {}: bad g=", file_name(path), idx + 1));
        }
    }
}

fn validate_oooutcome(report: &mut Report, path: &Path) {
    let Some(bytes) = validate_file_header(report, path, OOOUTCOME_MAX_BYTES) else {
        return;
    };

    for (idx, line) in lines_from_bytes(&bytes).into_iter().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        if !line.starts_with("out ") {
            report.violate(format!(
                "{}: line {}: expected prefix 'out '",
                file_name(path),
                idx + 1
            ));
            continue;
        }

        let kv = parse_kv_tokens(&line);
        let get = |key: &str| kv.iter().find(|(k, _)| *k == key).map(|(_, v)| *v);

        let Some(a) = get("a") else {
            report.violate(format!("{}: line {}: missing a=", file_name(path), idx + 1));
            continue;
        };
        if !a.bytes().all(|b| matches!(b, b'a'..=b'z' | b'0'..=b'9' | b'_')) {
            report.violate(format!("{}: line {}: bad a=", file_name(path), idx + 1));
        }

        let Some(i) = get("i") else {
            report.violate(format!("{}: line {}: missing i=", file_name(path), idx + 1));
            continue;
        };
        if i != "-1" && i != "0" && i != "1" {
            report.violate(format!("{}: line {}: bad i=", file_name(path), idx + 1));
        }
    }
}

fn validate_delta(report: &mut Report, path: &Path) {
    let Some(bytes) = validate_file_header(report, path, DELTA_MAX_BYTES) else {
        return;
    };

    for (idx, raw) in lines_from_bytes(&bytes).into_iter().enumerate() {
        let line = raw.trim();
        if line.is_empty() {
            continue;
        }
        if line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            report.violate(format!("{}: line {}: expected key=value", file_name(path), idx + 1));
            continue;
        };
        let key = k.trim();
        let val = v.trim();
        if !is_token(key) {
            report.violate(format!("{}: line {}: bad key", file_name(path), idx + 1));
        }
        if val.is_empty() {
            report.violate(format!("{}: line {}: empty value", file_name(path), idx + 1));
        }
        if val.len() > 160 {
            report.violate(format!(
                "{}: line {}: value too long ({} chars)",
                file_name(path),
                idx + 1,
                val.len()
            ));
        }
        // Conservative: values should be single token or digits; allows "0"/"1"/"auto" style.
        if !(is_token(val) || val.bytes().all(|c| c.is_ascii_digit())) {
            report.warn(format!(
                "{}: line {}: value is not a simple token (allowed but suspicious)",
                file_name(path),
                idx + 1
            ));
        }
    }
}

fn analyze_c_source(report: &mut Report, path: &Path) {
    let content = match fs::read_to_string(path) {
        Ok(v) => v,
        Err(e) => {
            report.violate(format!("{}: cannot read: {}", path.display(), e));
            return;
        }
    };

    let fname = file_name(path);
    
    // Track allocations (malloc/calloc/realloc) and deallocations (free)
    let mut malloc_count = 0;
    let mut free_count = 0;
    
    for (line_num, line) in content.lines().enumerate() {
        let line_trimmed = line.trim();
        
        // Skip comments and empty lines (basic detection)
        if line_trimmed.starts_with("//") || line_trimmed.is_empty() {
            continue;
        }
        
        // Pattern 1: Detect small fixed buffers (< 64 bytes)
        // Look for: char buf[N], uint8_t buffer[N], etc.
        // Skip: pointers (*), flexible arrays [0] or [], function calls
        if line_trimmed.contains('[') && line_trimmed.contains(']') 
            && !line_trimmed.contains('*')  // Skip pointer declarations  
            && !line_trimmed.contains('(')  // Skip function calls with array access
        {
            // Simple regex-like parsing for array declarations
            let parts: Vec<&str> = line_trimmed.split(&['[', ']'][..]).collect();
            if parts.len() >= 2 {
                let size_part = parts[1].trim();
                if let Ok(size) = size_part.parse::<u32>() {
                    if size > 0 && size < 64 && !line_trimmed.contains("// SAFE:") && !line_trimmed.contains("/* SAFE") {
                        report.violate(format!(
                            "{}:{}: small buffer [{}] without safety justification (expected // SAFE: comment)",
                            fname, line_num + 1, size
                        ));
                    }
                }
            }
        }
        
        // Pattern 2: Track malloc/free balance
        if line_trimmed.contains("malloc(") || line_trimmed.contains("calloc(") || line_trimmed.contains("realloc(") {
            malloc_count += 1;
        }
        if line_trimmed.contains("free(") {
            free_count += 1;
        }
        
        // Pattern 3: Unbounded loops
        if (line_trimmed.starts_with("while") && line_trimmed.contains("(1)"))
            || (line_trimmed.starts_with("for") && line_trimmed.contains("(;;)"))
        {
            if !line_trimmed.contains("// SAFE:") && !line_trimmed.contains("/* SAFE") {
                report.warn(format!(
                    "{}:{}: unbounded loop without safety justification",
                    fname, line_num + 1
                ));
            }
        }
        
        // Pattern 4: Magic numbers in critical operations (simple heuristic)
        if (line_trimmed.contains("memcpy") || line_trimmed.contains("memset"))
            && line_trimmed.matches(char::is_numeric).count() > 3
        {
            // Contains numeric literals - check if justified
            if !line_trimmed.contains("//") && !line_trimmed.contains("/*") {
                report.warn(format!(
                    "{}:{}: memory operation with magic number (consider named constant)",
                    fname, line_num + 1
                ));
            }
        }
    }
    
    // Report malloc/free imbalance
    if malloc_count > free_count + 2 {
        report.violate(format!(
            "{}: malloc/free imbalance ({} allocs, {} frees) - potential leak",
            fname, malloc_count, free_count
        ));
    }
}

fn run_prebuild(args: &Args) -> i32 {
    if args.sources.is_empty() {
        eprintln!("ERROR: prebuild requires at least one source file");
        return EXIT_USAGE;
    }

    let mut report = Report::default();
    
    for source in &args.sources {
        let path = if source.is_absolute() {
            source.clone()
        } else {
            args.root.join(source)
        };
        
        analyze_c_source(&mut report, &path);
    }

    // Print results
    if !args.quiet {
        if !report.warnings.is_empty() {
            println!("\n⚠ WARNINGS:");
            for w in &report.warnings {
                println!("  {}", w);
            }
        }
        
        if !report.violations.is_empty() {
            println!("\n❌ VIOLATIONS:");
            for v in &report.violations {
                println!("  {}", v);
            }
        }
        
        if report.ok() && report.warnings.is_empty() {
            println!("\n✓ All C source checks passed");
        } else if report.ok() {
            println!("\n✓ No critical violations (warnings present)");
        } else {
            println!("\nERROR: oo-guard prebuild failed");
        }
    }

    if report.ok() {
        EXIT_OK
    } else {
        EXIT_VIOLATION
    }
}

fn run_check(args: &Args) -> i32 {
    let root = &args.root;
    let logs_dir = args.logs_dir.clone().unwrap_or_else(|| root.clone());
    let delta = args
        .delta
        .clone()
        .unwrap_or_else(|| root.join("OO_DNA.DELTA"));

    let oojour = logs_dir.join("OOJOUR.LOG");
    let ooconsult = logs_dir.join("OOCONSULT.LOG");
    let oooutcome = logs_dir.join("OOOUTCOME.LOG");

    let mut report = Report::default();

    validate_oojour(&mut report, &oojour);
    validate_ooconsult(&mut report, &ooconsult);
    validate_oooutcome(&mut report, &oooutcome);

    // Optional delta: only validate if present.
    match read_if_exists(&delta) {
        Ok(Some(_)) => validate_delta(&mut report, &delta),
        Ok(None) => {}
        Err(e) => report.violate(format!("{}: unable to read: {}", delta.display(), e)),
    }

    if !args.quiet {
        if !report.warnings.is_empty() {
            eprintln!("WARNINGS:");
            for w in &report.warnings {
                eprintln!("- {}", w);
            }
        }
        if !report.violations.is_empty() {
            eprintln!("VIOLATIONS:");
            for v in &report.violations {
                eprintln!("- {}", v);
            }
        }

        if report.ok() {
            println!("OK: oo-guard check passed");
        } else {
            println!("ERROR: oo-guard check failed");
        }
    }

    if report.ok() {
        EXIT_OK
    } else {
        EXIT_VIOLATION
    }
}

fn analyze_serial_output(report: &mut Report, content: &str, tail_lines: usize) {
    let lines: Vec<&str> = content.lines().collect();
    let start_idx = if lines.len() > tail_lines {
        lines.len() - tail_lines
    } else {
        0
    };
    
    let mut deadlock_count = 0;
    let mut panic_count = 0;
    let mut corruption_count = 0;
    let mut last_timestamp_line: Option<usize> = None;
    let mut stall_count = 0;
    
    for (idx, line) in lines[start_idx..].iter().enumerate() {
        let line_num = start_idx + idx + 1;
        let lower = line.to_lowercase();
        
        // Pattern 1: Explicit panic/crash markers
        if lower.contains("panic") || lower.contains("kernel panic") || lower.contains("fatal") {
            panic_count += 1;
            report.violate(format!("Line {}: PANIC detected: {}", line_num, line.chars().take(80).collect::<String>()));
        }
        
        // Pattern 2: Stack overflow indicators
        if lower.contains("stack overflow") || lower.contains("stack guard") {
            report.violate(format!("Line {}: Stack overflow detected", line_num));
        }
        
        // Pattern 3: Memory corruption markers
        if lower.contains("corrupt") || lower.contains("invalid pointer") || lower.contains("heap corrupt") {
            corruption_count += 1;
            report.violate(format!("Line {}: Memory corruption detected: {}", line_num, line.chars().take(80).collect::<String>()));
        }
        
        // Pattern 4: Deadlock/hang indicators (same message repeating many times)
        if lower.contains("waiting") || lower.contains("timeout") || lower.contains("stall") {
            deadlock_count += 1;
        }
        
        // Pattern 5: Timestamp stall detection (if timestamps present)
        if line.contains("us]") || line.contains("ms]") || line.contains("timestamp") {
            if let Some(prev_line) = last_timestamp_line {
                if line_num - prev_line > 100 {
                    stall_count += 1;
                }
            }
            last_timestamp_line = Some(line_num);
        }
        
        // Pattern 6: Out of memory
        if lower.contains("out of memory") || lower.contains("oom") || lower.contains("allocation failed") {
            report.violate(format!("Line {}: OOM detected", line_num));
        }
        
        // Pattern 7: Infinite loop detection (same line repeating)
        if idx > 0 && *line == lines[start_idx + idx - 1] && line.len() > 10 {
            let repeat_count = lines[start_idx..start_idx + idx + 1]
                .iter()
                .rev()
                .take_while(|&&l| l == *line)
                .count();
            if repeat_count > 10 {
                report.warn(format!("Line {}: Potential infinite loop (message repeated {} times)", line_num, repeat_count));
            }
        }
    }
    
    // Aggregate warnings
    if deadlock_count > 20 {
        report.warn(format!("High deadlock/stall indicator count: {} occurrences", deadlock_count));
    }
    
    if stall_count > 3 {
        report.warn(format!("Timestamp stall detected: {} gaps", stall_count));
    }
    
    if panic_count == 0 && corruption_count == 0 && deadlock_count < 5 {
        // Looks healthy
    } else {
        if panic_count > 0 {
            report.violate(format!("Total panics: {}", panic_count));
        }
        if corruption_count > 0 {
            report.violate(format!("Total corruption events: {}", corruption_count));
        }
    }
}

fn run_watch(args: &Args) -> i32 {
    let serial_path = args.serial_file
        .clone()
        .unwrap_or_else(|| PathBuf::from("qemu-serial.txt"));
    
    if !serial_path.exists() {
        eprintln!("ERROR: Serial file not found: {}", serial_path.display());
        eprintln!("Tip: Run QEMU with -serial file:qemu-serial.txt");
        return EXIT_USAGE;
    }
    
    let content = match fs::read_to_string(&serial_path) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("ERROR: Cannot read serial file: {}", e);
            return EXIT_USAGE;
        }
    };
    
    if !args.quiet {
        println!("Analyzing {} (last {} lines)", serial_path.display(), args.tail_lines);
    }
    
    let mut report = Report::default();
    analyze_serial_output(&mut report, &content, args.tail_lines);
    
    // Print results
    if !args.quiet {
        if !report.warnings.is_empty() {
            println!("\n⚠ WARNINGS:");
            for w in &report.warnings {
                println!("  {}", w);
            }
        }
        
        if !report.violations.is_empty() {
            println!("\n❌ VIOLATIONS:");
            for v in &report.violations {
                println!("  {}", v);
            }
        }
        
        if report.ok() && report.warnings.is_empty() {
            println!("\n✓ Serial output clean (no critical patterns detected)");
        } else if report.ok() {
            println!("\n✓ No critical violations (warnings present)");
        } else {
            println!("\nERROR: oo-guard watch detected critical runtime issues");
        }
    }
    
    if report.ok() {
        EXIT_OK
    } else {
        EXIT_VIOLATION
    }
}

fn main() {
    let args = match parse_args() {
        Ok(v) => v,
        Err(e) if e == "help" => {
            print_usage();
            std::process::exit(EXIT_OK);
        }
        Err(e) => {
            eprintln!("ERROR: {}", e);
            print_usage();
            std::process::exit(EXIT_USAGE);
        }
    };

    match args.cmd.as_str() {
        "check" => {
            let code = run_check(&args);
            std::process::exit(code);
        }
        "prebuild" => {
            let code = run_prebuild(&args);
            std::process::exit(code);
        }
        "watch" => {
            let code = run_watch(&args);
            std::process::exit(code);
        }
        "--help" | "-h" => {
            print_usage();
            std::process::exit(EXIT_OK);
        }
        _ => {
            eprintln!("ERROR: unknown command: {}", args.cmd);
            print_usage();
            std::process::exit(EXIT_USAGE);
        }
    }
}
