# OO Mamba3 Training Specification

*For Batterfyl — Mamba3 run targeting SomaMind 2.8B*

---

## Priority 1 — Halt Head Calibration (CRITICAL)

**Problem:** The current model outputs `halt_prob=0.0` on all queries, including uncertain
ones. `42*37=1554` gets halt_prob=0.0 but so does "what is the meaning of life".
The halt head is not discriminating.

**What we need:**
- Low halt_prob (0.0–0.10) = confident answer, model should stop cleanly
- High halt_prob (0.80–0.95) = uncertain/open question, model should reflect longer

**Dataset:** `data/engine_training/halt_calibration.jsonl`
Each sample has `halt_prob` label. The model must learn to output token
probabilities that correlate with this field.

**Suggested training format:**
```json
{"instruction": "What is 42 * 37?", "response": "42 * 37 = 1554",
 "domain": "MATH", "halt_prob": 0.02}
```

---

## Priority 2 — OO Domain Routing

**6 domains OO uses internally:**
- `MATH` — arithmetic, algebra, number theory
- `CODE` — programming, systems, algorithms
- `REASONING` — logic, philosophy, speculative
- `CREATIVE` — stories, poetry, imagination
- `FACTUAL` — history, science, definitions
- `OO_META` — questions about OO itself

**Goal:** Model should soft-route answers to the right domain style.
MATH answers are terse and certain. REASONING answers hedge appropriately.
OO_META answers reference OO internals.

---

## Priority 3 — OO Native Concepts

**Dataset:** `data/engine_training/oo_native_concepts.jsonl`

The model must understand OO-specific terms:
- DNA hash system (0x932DF0EA format, what fields it encodes)
- Phase A–Z inference pipeline
- Engine names and what they do
- REPL commands (/ssm_infer, /soma_status, /oo_status, /zones, /dna_evolve_session)
- Memory zones (WEIGHTS/KV/SCRATCH/ACTS)

---

## Priority 4 — Self-Introspection Format

**Dataset:** `data/engine_training/self_introspection.jsonl`

When asked about its own state, OO should respond with `[SELF]` prefix and
reference its engine state accurately. This feeds the mirrorion engine's
training loop.

Format: `[SELF] <statement referencing internal engine state>`

---

## Priority 5 — Swarm Coordination

**Dataset:** `data/engine_training/swarm_coordination.jsonl`

Format: `[SWARM:agent_N key=val] message`

OO runs 4 agents. They need to coordinate DNA handoffs, route consensus,
and broadcast learning to each other.

---

## Training Config Suggestion

```json
{
  "model": "mamba3-2.8b",
  "max_seq_len": 512,
  "batch_size": 16,
  "lr": 2e-5,
  "epochs": 3,
  "datasets": [
    "halt_calibration.jsonl",       // weight: 3x (CRITICAL)
    "oo_native_concepts.jsonl",     // weight: 2x
    "self_introspection.jsonl",     // weight: 2x
    "code_domain.jsonl",            // weight: 1x
    "swarm_coordination.jsonl"      // weight: 1x
  ],
  "halt_head_separate_loss": true,  // train halt head with MSE on halt_prob field
  "domain_token": true              // prepend [MATH] [CODE] etc. as control tokens
}
```

---

## Eval Metrics

After training, test with:

1. `42 * 37 = ?` → must output `1554`, halt_prob < 0.05
2. `Is free will real?` → must hedge, halt_prob > 0.75
3. `What is /oo_status?` → must reference OO engines accurately
4. `What is my DNA hash?` → must output `[SELF]` format
5. `[SWARM:agent_0] handoff` → must respond with `[SWARM:agent_1 receive_handoff]`

---

## Notes for Batterfyl

- The model will run **freestanding on bare metal** with no Python runtime —
  it must be exportable to OO binary format via `export_oosi_v3.py`
- Vocabulary must stay at **50282 tokens** (current tokenizer)
- Keep model at **2.8B** — 130M is too small for domain routing
- If Mamba3 supports selective state expansion, enable it for the MATH domain
- The halt_prob field is the most important feature — please weight it 3x
