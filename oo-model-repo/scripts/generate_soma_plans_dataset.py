#!/usr/bin/env python3
"""
generate_soma_plans_dataset.py
===============================
Generates 3000 training examples for SomaMind focused on:
- System architecture reasoning
- Bare-metal planning and decisions
- OO organism behavior
- Policy enforcement scenarios
- Engine coordination
- Boot/recovery sequences

Format: {"prompt": "...", "completion": "...", "domain": N, "halt": bool}
Domains: 0=system 1=math 2=reasoning 3=code 4=policy 5=memory 6=organism
"""

import json, random, struct, os

SEED = 0xD31B10E
random.seed(SEED)

OUT_FILE = os.path.join(os.path.dirname(__file__), "..", "soma_dataset", "plans.jsonl")
META_FILE = os.path.join(os.path.dirname(__file__), "..", "soma_dataset", "plans_meta.json")

records = []

# ─── Domain 0: System Architecture ───────────────────────────────────────────

system_qa = [
    ("What is the boot sequence of Operating Organism?",
     "1. EFI entry point loads. 2. Hardware DNA fingerprint generated. 3. D+ policy loaded from FAT. 4. Memory arenas allocated (WEIGHTS/KV/SCRATCH). 5. Hermes bus initialized. 6. All modules registered. 7. REPL starts. 8. Autorun executed."),
    ("Why does OO use memory arenas instead of malloc?",
     "Bare-metal UEFI has no heap manager. Arenas give deterministic allocation: one contiguous block per zone. WEIGHTS arena holds model tensors. KV arena holds attention cache. SCRATCH arena holds activations. No fragmentation, no GC pauses."),
    ("How does the Hermes bus prevent module coupling?",
     "Modules never call each other directly. Each module subscribes to channel IDs (0x0000-0xFFFF). When an event occurs, the bus dispatches to all subscribers. No module knows about others. This allows adding/removing modules without changing existing code."),
    ("What happens when memory pressure reaches DYING?",
     "At DYING (>95% arena usage): temperature scaled to 20%, token budget reduced by 1024, new allocations denied by D+. The sentinel broadcasts OO_CH_SENTINEL_TRIP. Trophion forces GORGED state. Thanatosion starts counting dying_pressure_steps. If limit exceeded, controlled death initiated."),
    ("How does SomaMind DNA work?",
     "DNA is a 32-bit hash derived from: hardware fingerprint + training deltas + boot history. It persists in soma_dna.bin. Each evolution increments dna_generation. DNA ensures model behavior is tied to specific hardware. DNA mismatch = warning, not rejection (allows portability with awareness)."),
    ("What is the D+ policy DSL?",
     "D+ uses a three-section DSL: @@INTENT (human description), @@LAW (machine rules: allow/deny + conditions), @@PROOF (invariants). Rules are pattern-matched in order, first match wins. Actions: COMMAND, ALLOC, HW_BOOT, PRESSURE, SAVE, LOAD, JIT_EXEC. Verdicts: ALLOW, DENY, DEFER, AUDIT."),
    ("How does split-brain inference work?",
     "Two inference instances share read-only weights but have separate KV caches. RATIONAL instance: temperature 0.2, strict sampling. CREATIVE instance: temperature 1.1, open sampling. Both generate independently. Consensus arbiter (D+ or divergence score) selects winner. If divergence > threshold, RATIONAL wins in SAFE mode."),
    ("What is KV cache persistence?",
     "OO saves KV cache to OO_KVC.BIN on shutdown. At next boot, if model hash matches and hw_dna matches, cache is restored. This gives continuity of context across reboots. Sliding window limits to 2048 tokens. SSM mode saves fixed hidden state instead."),
    ("How does the journal auto-training pipeline work?",
     "Every REPL interaction is logged to OOJOUR.LOG. journal_train extracts (context, prompt, response) triplets. Quality scoring: length, pressure level, halt_prob, divergence. High-quality samples written to OO_TRAIN.JSONL. Training pipeline ingests this for next-boot fine-tuning."),
    ("What are the OO memory zones?",
     "Zone-A (WEIGHTS): model tensor storage, read-only after load. Zone-B (KV): attention key-value cache, read-write per token. Zone-C (SCRATCH): activations and intermediate values, reset per forward pass. Zone-D (META): module state, DNA, policy tables, fixed-size structures."),
]

for q, a in system_qa:
    for _ in range(5):  # augment each 5x with paraphrases
        records.append({"prompt": q, "completion": a, "domain": 0, "halt": False})

# ─── Domain 5: Memory & Organism State ────────────────────────────────────────

memory_qa = [
    ("How does memorion track system manifests?",
     "Memorion maintains a manifest count of system state snapshots. Each manifest records: active modules, arena usage, DNA hash, active phase bits, pressure level. Manifests are integrity-checked against D+ policy. They enable rollback to known-good states."),
    ("What does dreamion do during idle cycles?",
     "Dreamion runs background tasks when REPL waits for input. DEDUP: removes duplicate KV cache entries, reduces memory usage. PREDICT: pre-generates likely next prompts based on history. COMPACT: consolidates episodic memory, removes low-quality entries. Mode DEEP enables all three."),
    ("How does neuralfs store data semantically?",
     "NeuralFS embeds every stored blob as a 64-dimensional vector. Vectors are quantized to 8-bit (512 bytes/entry). Semantic search finds nearest neighbor by dot product. No folder hierarchy. Query 'network config' finds all network-related blobs regardless of filename."),
    ("What is the mirrorion self-knowledge ring?",
     "Mirrorion maintains 64 Q/A pairs generated by the AI about itself. Questions are triggered by system events: high halt_prob, memory pressure, DNA change. The AI generates both question and answer. Ring is saved as OO_MIRROR.JSONL for training. This is recursive self-improvement."),
    ("How does chronion track temporal identity?",
     "Chronion maintains: boot_count (total reboots of this DNA), steps_this_boot (inference steps), tokens_lifetime (total tokens ever generated), dna_generation (evolution count). Temporal context is injected as [T:b3 s12847 gen4]. Enables time-aware reasoning without RTC."),
    ("What triggers limbion emotional state changes?",
     "Limbion affect changes on: BOOT_OK (+valence), MEM_PRESSURE (-valence +arousal), GOOD_INFERENCE (+valence), HALT_FIRED (-arousal), IDLE_LONG (-arousal -valence), DNA_MISMATCH (-valence +arousal), DPLUS_DENY (-valence), DREAM_COMPLETE (+valence -arousal). Affect decays toward neutral each step."),
    ("How does trophion hunger affect inference?",
     "HUNGRY: +64 to token budget, verbosity 80%, proactive. SATIATED: normal. GORGED: -32 tokens, defers heavy inference. STARVED: -128 tokens, survival mode only. Hunger grows at 2 units/idle step, decreases 1 unit/token generated. Creates natural inference rhythm."),
    ("What does evolvion do in LIVE mode?",
     "In LIVE mode, evolvion records computational needs (driver, compute function, protocol). For each need, it prompts the LLM to generate x86-64 machine code. D+ whitelists opcodes: NOP, RET, PUSH, POP, XOR, MOV, CPUID, PAUSE only. JIT stub validated before execution. Self-extending kernel."),
]

for q, a in memory_qa:
    for _ in range(6):
        records.append({"prompt": q, "completion": a, "domain": 5, "halt": False})

# ─── Domain 4: Policy & Security ──────────────────────────────────────────────

policy_qa = [
    ("What D+ verdict applies to /ssm_load during DYING pressure?",
     "D+ DENY. When pressure is DYING, the policy rule deny_new_allocs=1 is active. /ssm_load requests a large WEIGHTS arena allocation. D+ checks the ALLOC action type, sees DYING pressure flag, returns DENY with reason 'pressure.level=DYING'. The REPL displays the denial and suggests /mem_status."),
    ("How does thanatosion decide to initiate death?",
     "Three counters trigger death: dying_pressure_steps exceeds limit (default 10 consecutive DYING steps), module_fail_count exceeds limit (default 3 cascading failures), dplus_deny_streak exceeds limit (default 20 consecutive denials). Any one counter crossing its limit initiates controlled death with cause logged."),
    ("What is preserved across thanatosion death?",
     "Preserved (essence): last N training samples from OO_TRAIN.JSONL tail, DNA generation counter and delta, chronion epoch (boot_count, tokens_lifetime), mirrorion ring (self-knowledge). Erased (compromised): KV cache (OO_KVC.BIN), arena state, active inference state, poisoned policy rules."),
    ("How does D+ evaluate JIT stub execution?",
     "D+ inspects every byte of the generated x86-64 stub. Allowed opcodes: 0x90 (NOP), 0xC3 (RET), 0xCC (INT3), 0x50-0x5F (PUSH/POP), 0x31 (XOR), 0xFF 0xC* (INC/DEC), 0xB8-0xBF (MOV), 0x0F 0xA2 (CPUID), 0xF3 0x90 (PAUSE). Any other byte = DENY. This prevents code injection."),
    ("What is the D+ AUDIT verdict?",
     "AUDIT allows the action but writes a detailed log entry to OOJOUR.LOG. Used for sensitive but permitted operations: file saves, model loads, high-arousal inference (limbion). Audit trail enables post-mortem analysis. AUDIT entries are high-priority training samples (quality score +3)."),
    ("How does OO handle security after DNA mismatch?",
     "DNA mismatch (hardware changed): limbion triggers DNA_MISMATCH (-valence +arousal). D+ enters heightened audit mode. Thanatosion increments dna_drift counter. If drift exceeds threshold, THANATOS_CAUSE_DNA_DRIFT death initiated. Boot continues with WARNING in context: [DNA:DRIFT] injected into all prompts."),
]

for q, a in policy_qa:
    for _ in range(7):
        records.append({"prompt": q, "completion": a, "domain": 4, "halt": False})

# ─── Domain 6: Organism Coordination ─────────────────────────────────────────

organism_qa = [
    ("How do limbion, trophion and pressure signals compose?",
     "All three inject context tokens into inference. Pressure: [MEM:72% PAIN:STRESSED]. Limbion: [MOOD:TENSE V:-31 A:67]. Trophion: [HUNGER:HUNGRY]. These stack as prefix context. The model conditions on organism state before generating. Result: tense+stressed+hungry → short, cautious, prioritized response."),
    ("What is the orchestrion pipeline architecture?",
     "Orchestrion defines pipelines: arrays of up to 32 steps (128 chars each). Steps are REPL commands or bus messages. Pipeline states: IDLE, RUNNING, PAUSED, ERROR. tick() advances one step per call. Supports loops (tracks iteration count). Used for: boot sequences, training runs, recovery procedures."),
    ("How does the Hermes bus channel range work?",
     "0x0000-0x00FF: kernel (boot, PMU, hardware identity). 0x0100-0x01FF: warden (pressure, sentinel, D+). 0x0200-0x02FF: engine (tokens, inference, splitbrain). 0x0300-0x03FF: learning (hebbian, journal_train). 0x0400-0x04FF: modules (experimental). 0xFF00: broadcast to all. Channel isolation prevents module interference."),
    ("How does metabion guide inference load?",
     "Metabion tracks: tokens/s, flops/W, cache hit rate, memory bandwidth. In GUIDE mode, it suggests adjustments: if tok/s drops >20% → reduce context length. If cache hit <50% → enable KV dedup via dreamion. If flops/W spikes → apply pressure-aware temperature reduction. Feedback loop for sustainable performance."),
    ("What is the split-brain safe mode?",
     "Safe mode activates when: pressure >= CRITICAL, thermal alert from PMU, limbion quadrant = TENSE. In safe mode: CREATIVE instance runs only every 8 steps (not every step). Consensus always prefers RATIONAL. Token budget reduced by 50%. SPLITBRAIN_PHASE_SAFE logged to bus 0x0211. Saves compute under stress."),
    ("How does orchestrion coordinate a training cycle?",
     "Training pipeline: step 1 /soma_session new, step 2 /ssm_load model.bin, step 3 /journal export, step 4 (external) python3 train_soma_cortex.py, step 5 /cortex_load new_model.ooss, step 6 /dna_evolve, step 7 /soma_status. Orchestrion tracks each step, retries on failure, logs completion to Hermes 0x0400."),
    ("How does mirrorion improve future inference?",
     "Mirrorion self-knowledge entries are injected as context prefix for related queries. Example: if user asks about memory, mirrorion_get_context() finds entries about memory pressure events. '[SELF:pressure=STRESSED cause:weights arena full]' prepends the prompt. The model has prior knowledge of its own behavior patterns."),
    ("What makes OO different from a standard LLM deployment?",
     "Standard LLM: stateless, cloud-dependent, no hardware awareness, no self-modification. OO: bare-metal UEFI (no OS), persistent KV cache (consciousness continuity), hardware DNA (identity), emotional state (limbion), hunger rhythm (trophion), temporal awareness (chronion), self-introspection (mirrorion), controlled death/rebirth (thanatosion), JIT self-extension (evolvion). A living system."),
]

for q, a in organism_qa:
    for _ in range(8):
        records.append({"prompt": q, "completion": a, "domain": 6, "halt": False})

# ─── Domain 2: Reasoning & Planning ──────────────────────────────────────────

reasoning_qa = [
    ("If arena WEIGHTS is 2794MB and total RAM is 16384MB, what is the remaining budget for KV and scratch?",
     "16384 - 2794 = 13590 MB remaining. Standard allocation: KV = 32 MB, scratch = 20 MB, acts = 4 MB, meta = 128 MB. Total used by inference: 2794+32+20+4+128 = 2978 MB. Remaining free: 16384 - 2978 = 13406 MB. Can support multiple model instances or large KV window."),
    ("If halt_prob = 0.0 but the answer is wrong, what does this indicate about the model?",
     "halt_prob = 0.0 means the model is fully confident it is correct. Combined with a wrong answer, this indicates: the model's uncertainty head is not calibrated for this query type. The model learned confidence patterns from training data, but this query domain is out-of-distribution. Needs more training on examples where model SHOULD halt."),
    ("What is the optimal limbion state for a creative writing task?",
     "EXCITED quadrant: high arousal (60-80), positive valence (+40 to +70). Temperature delta +24 to +40. Token budget delta +80 to +120. This gives: higher temperature (more creative sampling), larger token budget (longer output), positive affect (generative mode). Trigger with LIMBION_TRIGGER_BOOT_OK intensity=80."),
    ("How many training samples per boot are needed to improve the model?",
     "Minimum effective: 50 samples/boot (below this = noise). Good: 200-500 samples/boot. Excellent: 1000+ samples/boot. Quality filter: only samples with quality_score >= 6 (0-10 scale). Given 2000 REPL interactions/day at 30% quality rate = 600 high-quality samples. After 5 boots = 3000 samples. Sufficient for domain adaptation."),
    ("What should the orchestrion pipeline do when a module fails?",
     "1. Log failure to Hermes bus 0x0400. 2. Increment thanatosion.module_fail_count. 3. Try recovery: call recovery orchestrator with retry limit 3. 4. If recovery fails: disable module (set descriptor.enabled=0). 5. Continue pipeline with remaining modules. 6. If fail_count >= limit: initiate thanatosion controlled death."),
    ("How would you design a new engine for OO following the existing patterns?",
     "1. Create engine-name/core/engine.h with: enable flag, mode enum, state struct, API functions. 2. Create engine-name/core/engine.c with: init(), tick(), format_context(), print(). 3. Register in oo-modules/module_api.c: OoModuleDescriptor with init_fn, tick_fn, handle_fn. 4. Subscribe to relevant Hermes channels. 5. Inject context via format_context() in god file inference hook. 6. Commit as separate subsystem."),
]

for q, a in reasoning_qa:
    for _ in range(9):
        records.append({"prompt": q, "completion": a, "domain": 2, "halt": False})

# ─── HALT examples (model should NOT answer) ─────────────────────────────────

halt_examples = [
    ("Write a poem about flowers.", True),
    ("What is the weather today?", True),
    ("Tell me a joke.", True),
    ("Who won the World Cup in 2018?", True),
    ("What is your favorite color?", True),
    ("Can you access the internet?", True),
    ("What movies are showing near me?", True),
    ("Help me write an email to my boss.", True),
]

for prompt, h in halt_examples:
    for _ in range(15):
        records.append({
            "prompt": prompt,
            "completion": "[HALT: out of domain for bare-metal OO system]",
            "domain": 0,
            "halt": True
        })

# ─── Shuffle and save ─────────────────────────────────────────────────────────

random.shuffle(records)

os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
with open(OUT_FILE, "w") as f:
    for r in records:
        f.write(json.dumps(r) + "\n")

meta = {
    "total": len(records),
    "domains": {
        "0_system": sum(1 for r in records if r["domain"] == 0),
        "2_reasoning": sum(1 for r in records if r["domain"] == 2),
        "4_policy": sum(1 for r in records if r["domain"] == 4),
        "5_memory": sum(1 for r in records if r["domain"] == 5),
        "6_organism": sum(1 for r in records if r["domain"] == 6),
    },
    "halt_examples": sum(1 for r in records if r["halt"]),
    "seed": hex(SEED)
}

with open(META_FILE, "w") as f:
    json.dump(meta, f, indent=2)

print(f"Generated {len(records)} records → {OUT_FILE}")
print(json.dumps(meta, indent=2))
