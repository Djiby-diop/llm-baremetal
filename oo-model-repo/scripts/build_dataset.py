"""
Build latent training dataset for OO-Native model.
Sources: docs reels du projet OO (OO_VISION, ARCHITECTURE, DPLUS, OO_SPEC, etc.)
Output: data/processed/train.jsonl, valid.jsonl, test.jsonl, eval_oo.jsonl

Format par ligne:
  {"instruction": "...", "dark_loops": N, "response": "...", "domain": "system|math|code|chat|policy|arch"}
"""
from __future__ import annotations

import json
import random
import sys
from pathlib import Path

random.seed(42)

SYSTEM_SAMPLES = [
    # Vision OO
    ("What is an Operating Organism (OO)?",
     "An Operating Organism is a long-lived AI-based system that boots directly on hardware, maintains its own state and goals, and can be audited and controlled. Unlike a traditional OS that manages processes, an OO manages memory, identity, goals, and policies -- it thinks.",
     "system", 5),

    ("How does OO differ from a traditional operating system?",
     "A traditional OS manages processes, memory, and devices but does not think or maintain goals. OO has identity, goals, memory, modes, and policies. Every decision is logged, persisted, and passes through a policy gate (D+). OO has a host twin that reads its artifacts and helps humans steer it.",
     "system", 5),

    ("What are the 7 layers of the OO architecture?",
     "1. Cognitive Core (llm-baremetal): UEFI kernel + LLM + Mamba SSM. 2. Execution Kernel (oo-host): agents + scheduling. 3. Simulation Layer (oo-sim): world simulation + test. 4. Research Layer (oo-lab): prototypes. 5. Evolution Layer (oo-dplus): D+ policy + mutation. 6. Meta Layer: OO modifies OO. 7. Interface Layer: CLI + API + hardware bridge.",
     "system", 6),

    ("What is the OO Message Bus?",
     "The OO Message Bus is the shared communication channel between all 7 layers. Layers communicate only via the bus -- no direct cross-layer calls. It is defined in shared/oo-proto and uses channels like OO_CH_INFERENCE_STEP, OO_CH_MEM_PRESSURE, OO_CH_TOKEN_OUT.",
     "system", 4),

    ("What is the Cognitive Core (llm-baremetal)?",
     "The Cognitive Core is the bare-metal brain of OO. It boots under UEFI (no OS), runs a 7-phase boot sequence, loads LLM weights into memory zones (COLD/WARM/HOT), runs inference via llama2 or Mamba SSM, and exposes a REPL. All code is -ffreestanding with zero libc dependency.",
     "system", 6),

    ("What is the Execution Kernel (oo-host)?",
     "oo-host is the host-side runtime that manages agent lifecycle (spawn, pause, kill), task graph execution, and resource allocation. It reads the sovereign''s artifacts (OOHANDOFF.TXT, OOJOUR.LOG) and bridges the firmware world with developer tools like Git and GitHub.",
     "system", 4),

    ("What are OO memory zones?",
     "OO uses fixed bump-allocator zones: COLD (model weights -- never freed), WARM (KV cache -- persisted across turns), HOT (activations + scratch -- reset per inference pass). No dynamic allocator at runtime. Zone usage drives the pressure system.",
     "system", 5),

    ("What is KV cache persistence in OO?",
     "KV persistence saves the transformer KV cache state to FAT32 (oo-journal) across reboots. This gives OO identity continuity -- the model remembers prior conversation context even after power cycle. It is stored in the WARM zone and loaded at boot if the checksum matches.",
     "system", 5),

    ("What is the OO sovereign boot sequence?",
     "The OO sovereign boots in 7 phases: 1. UEFI file system ready. 2. Graphics/GOP init. 3. repl.cfg loaded. 4. Model weights loaded into COLD zone. 5. Tokenizer loaded. 6. KV cache recovered (if persistent). 7. REPL ready (llmk> prompt appears). Each phase is logged with a visible marker.",
     "system", 6),

    ("What are OO modes?",
     "OO has 3 operating modes: NORMAL (nominal operation, full capabilities), DEGRADED (stricter budgets, reduced context length, some features off), SAFE (minimum viable REPL + diagnostics + logs, no risky actions). Mode transitions: 3 consecutive boot failures -> SAFE. Stable SAFE for N boots -> DEGRADED -> NORMAL.",
     "system", 5),

    ("What is the OO Organism Tick?",
     "The Organism Tick is the main OO decision loop: 1. Read oo_state.bin (persistent state). 2. Measure vitals (RAM, errors, boot time). 3. Decide actions (adjust parameters, validate config, choose fallback model, trigger reboot). 4. Log decision with reason + delta. 5. Write oo_state.bin atomically.",
     "system", 6),

    ("What persistent artifacts does OO maintain?",
     "OO maintains: oo_state.bin (minimal state: version, boot count, mode, budgets, last failures), oo_journal.log (append-only log of events and decisions), oo_recovery.bin (last known-good state for rollback). All writes use atomic rename (*.tmp -> final) with checksum and .bak backup.",
     "system", 5),

    ("What is the OO host twin?",
     "The host twin (oo-host) is a companion runtime on a normal machine that reads sovereign artifacts, produces GitHub-ready reports, runs integrity checks, and bridges firmware state with CI workflows. It understands OOHANDOFF.TXT and OOJOUR.LOG formats. The oo-bot binary automates GitHub interaction.",
     "system", 4),

    ("What are OO design principles?",
     "OO follows: Sovereignty (runs independently, no cloud dependency for core function), Minimalism (no large libs, every line justified), Auditability (decisions traceable via logs), Robustness over features (recovery and SAFE modes first), Host integration (firmware never isolated), Incremental evolution (small testable steps).",
     "system", 5),

    ("What is the OO simulation layer (oo-sim)?",
     "oo-sim is a small world simulator with tasks, deadlines, and safety classes (normal, recovery, experimental). It simulates mode transitions (SAFE/DEGRADED/NORMAL) and logs to OOSIM.LOG. It is a safe lab to test scheduling ideas before changing UEFI firmware.",
     "system", 4),
]

POLICY_SAMPLES = [
    ("What is D+ (D-Plus)?",
     "D+ is a multi-facet language and policy engine for OO. It combines SPEED (low-level primitives), LOGIC (high-level rules), LAW (policies like eBPF), and PROOF (formal invariants) in one artifact. For critical operations, D+ requires cross-validation: a decision executes only if multiple independent formulations converge.",
     "policy", 6),

    ("What are D+ facets?",
     "D+ has 4 facets: SPEED (assembly-level primitives, fast path), LOGIC (high-level reasoning, Rust-like), LAW (rules and policies, eBPF-like verifier), PROOF (contracts and invariants, formal logic assertions). Each facet compiles to a common IR. Divergence between facets is a safety signal.",
     "policy", 5),

    ("What is D+ divergence handling?",
     "If two or more D+ facets disagree on a critical operation, the instruction enters safe fallback mode: the action is blocked, a log entry is written, and the suspicious operation is quarantined. Divergence is a signal, not just an error.",
     "policy", 5),

    ("What is the Memory Warden in OO?",
     "The Memory Warden replaces a naive allocator with governance. Memory requests carry intent: Allocate{kind, size, latency_slo, lifetime_hint, secrecy, priority}. The Warden responds with Granted, Limited, Sandboxed, or Denied. Capabilities (handles) replace raw pointers -- each contains range, rights, quota, TTL, and security label.",
     "policy", 6),

    ("What are OO memory capabilities?",
     "OO capabilities are handles that replace raw pointers. Each capability contains: address range (or object), rights (R/W/X), budget (quota), TTL (lifetime), owning cell context, and security label. Cells can delegate sub-capabilities with reduced rights (principle of least privilege).",
     "policy", 5),

    ("What is the OO Sentinel?",
     "The Sentinel tracks CPU cycle budgets per inference phase. Each phase (prefill, decode) has a max_cycles limit. If a phase exceeds its budget, the sentinel trips (sets tripped=1) and halts execution safely. The sentinel prevents infinite loops and runaway inference from consuming all resources.",
     "policy", 5),

    ("What is the OO pressure system?",
     "The pressure system monitors memory arena usage as a pain signal. Levels: NORMAL (<70%), AWARE (70-85%), STRESSED (85-95%), CRITICAL (95-100%), DYING (100%). High pressure reduces max_gen_tokens: STRESSED=-128, CRITICAL=-512, DYING=-1024. This prevents OOM by shrinking generation budget under memory load.",
     "policy", 6),

    ("What is the OO guardrails system?",
     "Guardrails provide safe-mode caps after a sentinel trip. When enabled, guardrails cap: top_k <= safe_top_k_cap, max_gen_tokens <= safe_max_tokens_cap, temperature <= safe_temp, top_p <= safe_top_p. Caps apply for safe_mode_turns turns after the trip. This limits risk during unstable operation.",
     "policy", 5),

    ("What is the OO D+ policy gate?",
     "Every action in OO passes through the D+ policy gate before execution. The gate evaluates: is this action allowed by current mode? Is it within budget? Does it match the LAW facet rules? If blocked, the action is denied with a reason logged. No action bypasses the gate.",
     "policy", 6),
]

ARCH_SAMPLES = [
    ("What is bare-metal LLM inference?",
     "Bare-metal LLM inference runs a language model directly under UEFI firmware without any OS. The model weights are loaded from FAT32 into a fixed COLD memory zone. Inference uses hand-optimized C with AVX2/SSE2 SIMD. No malloc, no libc. The entire inference stack is -ffreestanding.",
     "arch", 6),

    ("What is the OO inference budget system?",
     "OO tracks CPU cycles per inference phase using hardware TSC. Budget prefill_max and decode_max are set at boot. If a phase exceeds its budget, overruns are counted. After hard_stop_overruns_decode overruns, a budget guard trips and stops generation. Budgets auto-adjust based on measured cycle times.",
     "arch", 6),

    ("What is Mamba SSM and why is it suited for bare-metal?",
     "Mamba SSM (State Space Model) processes tokens with O(1) memory per step -- no KV cache growth. Unlike transformers where KV cache grows O(n) with context, Mamba''s recurrent state is fixed size. This makes it ideal for bare-metal: memory usage is constant regardless of sequence length.",
     "arch", 7),

    ("What is the OO REPL?",
     "The OO REPL (llmk>) is the interactive interface to the bare-metal LLM. It supports: natural language prompts (routed to LLM inference), /commands (/oo_status, /oo_new, /oo_consult, /models, /draw, etc.), autorun mode (reads commands from llmk-autorun.txt), and serial output for automated testing.",
     "arch", 4),

    ("What is the OO journal system?",
     "The OO journal (oo-journal) is an append-only log of cognitive events: inference results, mode transitions, policy decisions, entity state changes. Format: simple text with stable field names. Written to FAT32. Survives reboots. The host twin (oo-host) reads and analyzes it.",
     "arch", 5),

    ("What is GGUF format in OO?",
     "GGUF is the model weight format used in llm-baremetal. It stores transformer weights with quantization metadata (q4_0, q8_0, etc.). OO loads GGUF directly from FAT32 into the COLD zone at boot. The COLD zone is read-only after loading -- weights are never modified at runtime.",
     "arch", 4),

    ("What is the OO split-brain engine?",
     "The split-brain engine runs two parallel inference paths: RATIONAL (deterministic, temperature=0, top_k=1) and CREATIVE (stochastic, higher temperature). Their outputs are compared and merged by D+ policy. Divergence between paths is logged as a signal. Inspired by dual-process cognition theory.",
     "arch", 7),

    ("How does OO handle inference without dynamic allocation?",
     "All memory is pre-allocated at boot in fixed zones: COLD (weights), WARM (KV cache), SCRATCH (inference workspace), ACTIVATIONS (layer outputs). Sizes are computed from model config at load time. Bump allocators advance a cursor within each zone. No malloc/free calls during inference.",
     "arch", 5),

    ("What is a dark loop?",
     "A dark loop is a silent recurrence cycle (= token) where the model processes context without emitting surface tokens. The model uses these cycles to reason internally. The HaltingHead decides when enough dark loops have occurred and surface generation should begin.",
     "arch", 4),

    ("What is the HaltingHead?",
     "The HaltingHead is a small MLP that takes [hidden_state | loop_position_scalar] as input and outputs P(halt) in [0,1]. When P(halt) >= 0.7 the model stops dark loops and begins surface generation. Position conditioning prevents representational collapse -- the MLP sees both content and position.",
     "arch", 6),
]

CODE_SAMPLES = [
    ("Write a C function to check if an integer is a power of 2.",
     "int is_power_of_two(int n) {\n    return n > 0 && (n & (n - 1)) == 0;\n}",
     "code", 8),

    ("Write a C bump allocator for a fixed-size arena.",
     "typedef struct { uint8_t *base; size_t cursor; size_t size; } Arena;\nvoid *arena_alloc(Arena *a, size_t n) {\n    if (a->cursor + n > a->size) return NULL;\n    void *p = a->base + a->cursor;\n    a->cursor += n;\n    return p;\n}",
     "code", 14),

    ("Write a Python function to build BPE pairs from a word frequency dict.",
     "def get_pairs(vocab):\n    pairs = {}\n    for word, freq in vocab.items():\n        syms = word.split()\n        for i in range(len(syms)-1):\n            p = (syms[i], syms[i+1])\n            pairs[p] = pairs.get(p, 0) + freq\n    return pairs",
     "code", 12),

    ("Write a C function to compute softmax in place.",
     "void softmax(float *x, int n) {\n    float m = x[0];\n    for (int i=1;i<n;i++) if(x[i]>m) m=x[i];\n    float s = 0.0f;\n    for (int i=0;i<n;i++) { x[i]=expf(x[i]-m); s+=x[i]; }\n    for (int i=0;i<n;i++) x[i]/=s;\n}",
     "code", 12),

    ("Write a Python function to load a JSONL file into a list of dicts.",
     "def load_jsonl(path):\n    rows = []\n    with open(path) as f:\n        for line in f:\n            line = line.strip()\n            if line:\n                rows.append(json.loads(line))\n    return rows",
     "code", 6),

    ("Write a C struct for an OO memory zone.",
     "typedef struct {\n    uint8_t  *base;\n    size_t    size;\n    size_t    cursor;\n    uint32_t  flags;   /* OO_ZONE_FLAG_COLD | WARM | HOT */\n    uint32_t  id;\n} OoZone;",
     "code", 8),

    ("Write a Python dataclass for an OO training sample.",
     "from dataclasses import dataclass\n@dataclass\nclass OOSample:\n    instruction: str\n    dark_loops: int\n    response: str\n    domain: str  # system|policy|arch|math|code|chat",
     "code", 6),

    ("Write a C function to compute RMS normalization.",
     "void rmsnorm(float *o, float *x, float *w, int n) {\n    float ss = 0.0f;\n    for (int i=0;i<n;i++) ss += x[i]*x[i];\n    ss = 1.0f / sqrtf(ss/n + 1e-5f);\n    for (int i=0;i<n;i++) o[i] = w[i] * (ss * x[i]);\n}",
     "code", 12),

    ("Write a Python function to check if a number is prime.",
     "def is_prime(n):\n    if n < 2: return False\n    for i in range(2, int(n**0.5)+1):\n        if n % i == 0: return False\n    return True",
     "code", 10),

    ("Write a Fibonacci generator in Python.",
     "def fib(n):\n    a, b = 0, 1\n    for _ in range(n):\n        yield a\n        a, b = b, a+b",
     "code", 8),

    ("Write a C function to reverse a string in place.",
     "void reverse(char *s) {\n    int l=0, r=strlen(s)-1;\n    while(l<r){char t=s[l];s[l++]=s[r];s[r--]=t;}\n}",
     "code", 12),

    ("Write a binary search in C.",
     "int bsearch_oo(int *a, int n, int t) {\n    int l=0,r=n-1;\n    while(l<=r){int m=(l+r)/2;\n    if(a[m]==t)return m;\n    if(a[m]<t)l=m+1;else r=m-1;}\n    return -1;\n}",
     "code", 14),
]

MATH_SAMPLES = [
    ("A model has 512 dimensions and 12 layers. x_proj shape is (dt_rank+2*d_state) x d_inner. With dt_rank=32, d_state=16, expand=2, how many parameters in x_proj per layer?",
     "d_inner = 512 * 2 = 1024. Output dim = dt_rank + 2*d_state = 32 + 32 = 64. x_proj.weight shape = 64 x 1024. Params per layer = 64 * 1024 = 65536.",
     "math", 10),

    ("A context window is 1024 tokens. KV cache stores 2 floats per token per layer per head. With 12 layers and 8 heads, how many MB (float32)?",
     "Total floats = 1024 * 12 * 8 * 2 = 196608. Bytes = 196608 * 4 = 786432 = 0.75 MB.",
     "math", 8),

    ("A model has 16M parameters. At q8_0 (1 byte per weight), what is the file size?",
     "16,000,000 bytes = 16 MB. With GGUF header overhead (~10KB), total ~16.01 MB.",
     "math", 7),

    ("Prefill takes 60 seconds for 3 tokens. Decode takes 2 seconds per token. How long for a 100-token response?",
     "Prefill: 60s. Decode: 100 * 2s = 200s. Total: 260s = 4 min 20s.",
     "math", 6),

    ("X=5. Y=X*2. Z=Y+3. W=Z-X. What is W?",
     "Y = 5*2 = 10. Z = 10+3 = 13. W = 13-5 = 8.",
     "math", 8),

    ("A bump allocator has 256MB. Weights need 120MB, KV cache 64MB, scratch 32MB. How much free?",
     "Used = 120 + 64 + 32 = 216 MB. Free = 256 - 216 = 40 MB.",
     "math", 6),

    ("pressure_pct = 97%. OO_PRESSURE_TOKENS_DYING = -1024. Initial max_gen_tokens = 160. What is max_gen_tokens after pressure?",
     "97% is DYING level. delta = -1024. new = 160 + (-1024) = -864. Clamped to minimum 1. max_gen_tokens = 1.",
     "math", 10),

    ("Calculate 7 * 8 + 15.",
     "7 * 8 = 56. 56 + 15 = 71.",
     "math", 3),

    ("What is 15% of 240?",
     "15% = 0.15. 0.15 * 240 = 36.",
     "math", 4),

    ("Solve: x^2 - 5x + 6 = 0.",
     "Factor: (x-2)(x-3) = 0. x = 2 or x = 3.",
     "math", 7),

    ("A train travels 120 miles in 2 hours. What is its speed?",
     "Speed = distance / time = 120 / 2 = 60 mph.",
     "math", 3),

    ("What is 2^10?",
     "2^10 = 1024.",
     "math", 2),
]

CHAT_SAMPLES = [
    ("What makes OO different from a cloud AI service?",
     "OO runs bare-metal under UEFI -- no cloud, no OS dependency. It has persistent state across reboots (not stateless). Every decision passes through D+ policy. It maintains goals, modes, and a journal. It is sovereign: it controls its own execution environment.",
     "chat", 4),

    ("Can OO run without internet?",
     "Yes. OO is designed for full sovereignty. The Cognitive Core boots under UEFI with local model weights only. No network calls for core inference. oo-host has optional network features but these are not required for the bare-metal core.",
     "chat", 3),

    ("What language is OO written in?",
     "OO is primarily written in C (-ffreestanding, no libc) for the bare-metal UEFI kernel and inference engine. The host-side runtime (oo-host) is in Rust. The policy engine (D+) has a C runtime. oo-model (training) uses Python + PyTorch.",
     "chat", 3),

    ("What is the llmk> prompt?",
     "llmk> is the OO bare-metal REPL prompt. It appears after the 7-phase boot sequence completes. Users can type natural language prompts (routed to LLM inference) or /commands like /oo_status, /models, /oo_consult.",
     "chat", 3),

    ("What is oo-model?",
     "oo-model is the AI model development repository for the OO project. It contains: the OO-Native model (custom SSM architecture from scratch), the Mamba track (fine-tuned 130M SSM with latent reasoning), the OO BPE tokenizer (16K vocab), training scripts, and export tools for bare-metal binary format.",
     "chat", 4),

    ("What is the OO native model?",
     "OO-Native is the model built from scratch for the OO project. It is an OO-SSM with 3 specialized heads: PolicyHead (D+ action compatibility), PressureHead (memory pressure prediction), HaltHead (latent loop halting). It uses a custom BPE tokenizer with OO-specific tokens like [OO:THINK], [OO:ACT], [OO:END].",
     "chat", 5),

    ("What is the capital of France?",
     "The capital of France is Paris.",
     "chat", 1),

    ("What is machine learning?",
     "Machine learning is a field of AI where systems learn patterns from data to make predictions or decisions without being explicitly programmed.",
     "chat", 3),

    ("What is the difference between RAM and storage?",
     "RAM is volatile fast memory used for active processes. Storage (disk) is persistent, slower, and holds data across power cycles.",
     "chat", 2),
]

ALL = SYSTEM_SAMPLES + POLICY_SAMPLES + ARCH_SAMPLES + CODE_SAMPLES + MATH_SAMPLES + CHAT_SAMPLES


def build(output_dir: str = "data/processed") -> None:
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    rows = []
    for inst, resp, domain, loops in ALL:
        rows.append({
            "instruction": inst,
            "dark_loops": loops,
            "response": resp,
            "domain": domain,
        })

    random.shuffle(rows)
    n = len(rows)
    n_train = int(n * 0.80)
    n_valid = int(n * 0.10)

    splits = {
        "train.jsonl": rows[:n_train],
        "valid.jsonl": rows[n_train:n_train + n_valid],
        "test.jsonl":  rows[n_train + n_valid:],
        "eval_oo.jsonl": [r for r in rows if r["domain"] in ("system", "policy", "arch")],
    }

    for fname, data in splits.items():
        p = out / fname
        with p.open("w", encoding="utf-8") as f:
            for r in data:
                f.write(json.dumps(r, ensure_ascii=True) + "\n")
        print(f"[dataset] {p}: {len(data)} rows  (domains: {set(r['domain'] for r in data)})")

    total = len(rows)
    domains = {}
    for r in rows:
        domains[r["domain"]] = domains.get(r["domain"], 0) + 1
    print(f"\n[dataset] Total: {total} samples")
    for d, c in sorted(domains.items()):
        print(f"          {d}: {c}")


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "data/processed")
