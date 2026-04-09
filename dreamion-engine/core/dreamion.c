/* dreamion.c — Dream State Engine (full implementation)
 *
 * Freestanding C11: no libc, no malloc.
 * See dreamion.h for architecture overview.
 */

#include "dreamion.h"

/* ── Internal helpers ───────────────────────────────────────────── */

static void dreami_memset(void *dst, int v, uint32_t n) {
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)v;
}

static void dreami_memcpy(void *dst, const void *src, uint32_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
}

/* FNV-1a 32-bit hash */
static uint32_t dreami_fnv1a(const void *data, uint32_t len) {
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)data;
    for (uint32_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* Simple LCG for synthetic generation (no rand()) */
static uint32_t dreami_lcg(uint32_t *seed) {
    *seed = (*seed * 1664525u) + 1013904223u;
    return *seed;
}

/* Integer division with rounding */
static uint32_t dreami_divround(uint32_t a, uint32_t b) {
    if (!b) return 0;
    return (a + b / 2) / b;
}

/* Copy null-terminated string into buffer, return length written */
static uint32_t dreami_strcpy(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

/* Append string to buf[off], return new offset */
static uint32_t dreami_append(char *buf, uint32_t cap, uint32_t off,
                               const char *s) {
    while (*s && off + 1 < cap) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

/* Format uint32 decimal into buf, return end offset */
static uint32_t dreami_fmt_u32(char *buf, uint32_t cap, uint32_t off,
                                uint32_t v) {
    char tmp[12];
    int len = 0;
    if (!v) { tmp[len++] = '0'; }
    else { while (v) { tmp[len++] = '0' + (v % 10); v /= 10; } }
    for (int i = len - 1; i >= 0 && off + 1 < cap; i--)
        buf[off++] = tmp[i];
    buf[off] = '\0';
    return off;
}

/* Format float with 3 decimals, e.g. "0.734" */
static uint32_t dreami_fmt_f3(char *buf, uint32_t cap, uint32_t off,
                               float v) {
    if (v < 0.0f) { if (off + 1 < cap) buf[off++] = '-'; v = -v; }
    uint32_t i = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)i) * 1000.0f + 0.5f);
    off = dreami_fmt_u32(buf, cap, off, i);
    if (off + 1 < cap) buf[off++] = '.';
    /* always 3 digits */
    if (f < 10  && off + 1 < cap) buf[off++] = '0';
    if (f < 100 && off + 1 < cap) buf[off++] = '0';
    off = dreami_fmt_u32(buf, cap, off, f);
    buf[off] = '\0';
    return off;
}

/* ── Lifecycle ──────────────────────────────────────────────────── */

void dreamion_init(DreamionEngine *e) {
    if (!e) return;
    dreami_memset(e, 0, sizeof(*e));
    e->awake              = 1;
    e->idle_light_thresh  = DREAMION_IDLE_LIGHT_THRESH;
    e->idle_deep_thresh   = DREAMION_IDLE_DEEP_THRESH;
    e->current_task       = DREAMION_TASK_NONE;
}

void dreamion_set_mode(DreamionEngine *e, DreamionMode mode) {
    if (!e) return;
    e->mode = mode;
    if (mode != DREAMION_MODE_OFF) e->awake = 0;
    else                           e->awake = 1;
}

void dreamion_wake(DreamionEngine *e) {
    if (!e) return;
    e->mode         = DREAMION_MODE_OFF;
    e->idle_ticks   = 0;
    e->active_ticks = 0;
    e->awake        = 1;
    e->current_task = DREAMION_TASK_NONE;
    e->task_phase   = 0;
    e->stats.wakes++;
}

/* ── Tick (call in main idle loop) ─────────────────────────────── */

void dreamion_tick(DreamionEngine *e, uint32_t idle_cycles) {
    if (!e || !idle_cycles) return;
    e->idle_ticks   += idle_cycles;
    e->active_ticks  = 0;   /* reset active counter on idle */

    /* Auto-transitions */
    if (e->mode == DREAMION_MODE_OFF &&
        e->idle_ticks >= e->idle_light_thresh) {
        e->mode  = DREAMION_MODE_LIGHT;
        e->awake = 0;
    }
    if (e->mode == DREAMION_MODE_LIGHT &&
        e->idle_ticks >= e->idle_deep_thresh) {
        e->mode = DREAMION_MODE_DEEP;
    }

    e->stats.total_dream_cycles++;
    if (e->mode == DREAMION_MODE_LIGHT) e->stats.light_cycles++;
    if (e->mode == DREAMION_MODE_DEEP)  e->stats.deep_cycles++;
}

void dreamion_tick_active(DreamionEngine *e, uint32_t active_cycles) {
    if (!e || !active_cycles) return;
    e->active_ticks += active_cycles;
    if (e->active_ticks >= DREAMION_WAKE_THRESH && !e->awake) {
        dreamion_wake(e);
    }
}

/* ── Record inference session ───────────────────────────────────── */

void dreamion_record_inference(DreamionEngine *e,
                                const uint16_t *prompt_toks, uint8_t prompt_len,
                                const uint16_t *output_toks, uint8_t output_len,
                                float halt_prob, float confidence,
                                uint8_t domain, uint8_t used_solar) {
    if (!e) return;
    DreamionMemory *m = &e->memory[e->memory_head % DREAMION_MEMORY_RING_SIZE];
    dreami_memset(m, 0, sizeof(*m));

    uint8_t pl = prompt_len < 32 ? prompt_len : 32;
    uint8_t ol = output_len < 32 ? output_len : 32;
    if (prompt_toks) dreami_memcpy(m->prompt_tokens, prompt_toks, pl * 2);
    if (output_toks) dreami_memcpy(m->output_tokens, output_toks, ol * 2);
    m->prompt_len  = pl;
    m->output_len  = ol;
    m->halt_prob   = halt_prob;
    m->confidence  = confidence;
    m->domain      = domain;
    m->used_solar  = used_solar;
    m->valid       = 1;

    e->memory_head++;
    if (e->memory_count < DREAMION_MEMORY_RING_SIZE) e->memory_count++;
}

/* ── Task scheduling ────────────────────────────────────────────── */

DreamionTaskType dreamion_suggest_task(const DreamionEngine *e) {
    if (!e || e->mode == DREAMION_MODE_OFF || e->awake) return DREAMION_TASK_NONE;

    uint32_t phase = e->stats.total_dream_cycles % 8;
    if (e->mode == DREAMION_MODE_LIGHT) {
        if (phase < 3) return DREAMION_TASK_DEDUP;
        if (phase < 6) return DREAMION_TASK_COMPACT;
        return DREAMION_TASK_PREDICT;
    }
    /* DEEP mode rotates through all tasks */
    switch (phase) {
        case 0: case 1: return DREAMION_TASK_DEDUP;
        case 2:         return DREAMION_TASK_COMPACT;
        case 3:         return DREAMION_TASK_PREDICT;
        case 4: case 5: return DREAMION_TASK_SYNTH;
        case 6:         return DREAMION_TASK_DNA_MUTATE;
        case 7:         return DREAMION_TASK_CONSOLIDATE;
        default:        return DREAMION_TASK_NONE;
    }
}

/* ── Task implementations ───────────────────────────────────────── */

/* TASK_DEDUP: scan memory ring for repeated prompt token pairs */
static void dreami_run_dedup(DreamionEngine *e) {
    DreamionDedupResult r = {0};
    uint32_t n = e->memory_count;
    for (uint32_t i = 0; i < n; i++) {
        DreamionMemory *mi = &e->memory[i % DREAMION_MEMORY_RING_SIZE];
        if (!mi->valid) continue;
        for (uint32_t j = i + 1; j < n && j < i + 8; j++) {
            DreamionMemory *mj = &e->memory[j % DREAMION_MEMORY_RING_SIZE];
            if (!mj->valid) continue;
            /* Check if prompts share a common 4-token prefix */
            uint8_t ml = mi->prompt_len < mj->prompt_len
                       ? mi->prompt_len : mj->prompt_len;
            if (ml >= 4) {
                int match = 1;
                for (int k = 0; k < 4; k++) {
                    if (mi->prompt_tokens[k] != mj->prompt_tokens[k]) {
                        match = 0; break;
                    }
                }
                if (match) {
                    r.pairs_scanned++;
                    /* Mark older entry as not needing separate warm-up */
                    if (mi->confidence < mj->confidence) {
                        r.pairs_deduped++;
                        r.tokens_freed += mi->prompt_len;
                    }
                }
            }
            r.pairs_scanned++;
        }
    }
    e->last_dedup = r;
    e->stats.dedup_pairs += r.pairs_deduped;
}

/* TASK_COMPACT: compute gist tokens from memory ring */
static void dreami_run_compact(DreamionEngine *e) {
    /* Count token frequency in first output token position */
    uint32_t freq[8] = {0};
    uint16_t tok[8]  = {0};
    uint32_t n_tok   = 0;

    for (uint32_t i = 0; i < e->memory_count; i++) {
        DreamionMemory *m = &e->memory[i % DREAMION_MEMORY_RING_SIZE];
        if (!m->valid || !m->output_len) continue;
        uint16_t t = m->output_tokens[0];
        /* Find in table */
        int found = -1;
        for (uint32_t k = 0; k < n_tok; k++) {
            if (tok[k] == t) { found = (int)k; break; }
        }
        if (found >= 0) {
            freq[found]++;
        } else if (n_tok < 8) {
            tok[n_tok]  = t;
            freq[n_tok] = 1;
            n_tok++;
        }
    }
    e->stats.compact_entries += n_tok;
}

/* TASK_PREDICT: build predicted prompts from most recent 4 sessions */
static void dreami_run_predict(DreamionEngine *e) {
    e->pred_count = 0;
    uint32_t n = e->memory_count < 4 ? e->memory_count : 4;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (e->memory_head - 1 - i + DREAMION_MEMORY_RING_SIZE)
                       % DREAMION_MEMORY_RING_SIZE;
        DreamionMemory *m = &e->memory[idx];
        if (!m->valid || !m->prompt_len) continue;
        DreamionPrediction *p = &e->predictions[e->pred_count++];
        uint8_t l = m->prompt_len < 16 ? m->prompt_len : 16;
        dreami_memcpy(p->tokens, m->prompt_tokens, l * 2);
        p->len        = l;
        p->confidence = m->confidence * 0.9f; /* slight decay */
    }
}

/* TASK_SYNTH: produce one synthetic training pair via self-play */
static void dreami_run_synth(DreamionEngine *e) {
    if (e->memory_count < 2) return;

    /* Seed from DNA-like hash of recent memories */
    uint32_t seed = dreami_fnv1a(e->memory,
                                  e->memory_count * sizeof(DreamionMemory));

    DreamionSynthPair *p = &e->last_synth;
    dreami_memset(p, 0, sizeof(*p));

    /* Pick two random memories, interleave their prompts */
    uint32_t ia = dreami_lcg(&seed) % e->memory_count;
    uint32_t ib = dreami_lcg(&seed) % e->memory_count;
    if (ia == ib) ib = (ib + 1) % e->memory_count;

    DreamionMemory *ma = &e->memory[ia % DREAMION_MEMORY_RING_SIZE];
    DreamionMemory *mb = &e->memory[ib % DREAMION_MEMORY_RING_SIZE];

    uint8_t il = 0;
    for (uint8_t k = 0; k < ma->prompt_len && il < DREAMION_SYNTH_BUF_SIZE/2 - 1; k++)
        p->input_tokens[il++] = ma->prompt_tokens[k];
    p->input_len = il;

    /* Output = ma's output prefixed to mb's output */
    uint8_t ol = 0;
    for (uint8_t k = 0; k < ma->output_len && ol < DREAMION_SYNTH_BUF_SIZE/2 - 1; k++)
        p->output_tokens[ol++] = ma->output_tokens[k];
    for (uint8_t k = 0; k < mb->output_len && ol < DREAMION_SYNTH_BUF_SIZE/2 - 1; k++)
        p->output_tokens[ol++] = mb->output_tokens[k];
    p->output_len = ol;

    /* Quality = geometric mean of halt_probs (high halt_prob = confident) */
    float qa = 1.0f - ma->halt_prob;  /* low halt = confident output */
    float qb = 1.0f - mb->halt_prob;
    p->quality = (qa + qb) * 0.5f;

    /* Stage JSONL entry */
    if (e->jsonl.line_count < DREAMION_JSONL_BUF_LINES && p->input_len > 0) {
        char *line = e->jsonl.lines[e->jsonl.line_count];
        uint32_t off = 0;
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            "{\"type\":\"dream_synth\",\"quality\":");
        off = dreami_fmt_f3(line, DREAMION_JSONL_LINE_MAX, off, p->quality);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            ",\"input_len\":");
        off = dreami_fmt_u32(line, DREAMION_JSONL_LINE_MAX, off, p->input_len);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            ",\"output_len\":");
        off = dreami_fmt_u32(line, DREAMION_JSONL_LINE_MAX, off, p->output_len);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off, "}\n");
        (void)off;
        e->jsonl.line_count++;
    }

    e->stats.synth_pairs_generated++;
}

/* TASK_DNA_MUTATE: compute recommended DNA bias delta */
static void dreami_run_dna_mutate(DreamionEngine *e) {
    if (e->memory_count < 4) return;

    /* Count domain distribution */
    uint32_t domain_counts[8] = {0};
    float    sum_conf = 0.0f;
    float    sum_halt = 0.0f;
    uint32_t n_valid  = 0;

    for (uint32_t i = 0; i < e->memory_count; i++) {
        DreamionMemory *m = &e->memory[i % DREAMION_MEMORY_RING_SIZE];
        if (!m->valid) continue;
        if (m->domain < 8) domain_counts[m->domain]++;
        sum_conf += m->confidence;
        sum_halt += m->halt_prob;
        n_valid++;
    }
    if (!n_valid) return;

    float avg_conf = sum_conf / (float)n_valid;
    float avg_halt = sum_halt / (float)n_valid;

    /* Find dominant domain */
    uint32_t dom_idx = 0;
    for (uint32_t d = 1; d < 8; d++)
        if (domain_counts[d] > domain_counts[dom_idx]) dom_idx = d;

    float dom_frac = (float)domain_counts[dom_idx] / (float)n_valid;

    /* bias_delta: nudge toward dominant domain
     * if avg_conf high → small positive bias (trust current state)
     * if avg_halt high → negative bias (too many halts = hallucinating)
     */
    e->pending_bias_delta = (dom_frac - 0.4f) * 0.05f;   /* ±0.03 range */
    e->pending_temp_delta = (avg_halt - 0.5f) * (-0.02f); /* counter-bias halts */
    e->pending_dna_ready  = 1;

    e->stats.last_dna_bias_delta = e->pending_bias_delta;
    e->stats.last_dna_temp_delta = e->pending_temp_delta;
    e->stats.dna_mutations_suggested++;

    /* Stage JSONL entry */
    if (e->jsonl.line_count < DREAMION_JSONL_BUF_LINES) {
        char *line = e->jsonl.lines[e->jsonl.line_count];
        uint32_t off = 0;
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            "{\"type\":\"dream_dna\",\"avg_conf\":");
        off = dreami_fmt_f3(line, DREAMION_JSONL_LINE_MAX, off, avg_conf);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            ",\"avg_halt\":");
        off = dreami_fmt_f3(line, DREAMION_JSONL_LINE_MAX, off, avg_halt);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            ",\"bias_delta\":");
        off = dreami_fmt_f3(line, DREAMION_JSONL_LINE_MAX, off,
                            e->pending_bias_delta);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            ",\"dominant_domain\":");
        off = dreami_fmt_u32(line, DREAMION_JSONL_LINE_MAX, off, dom_idx);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off, "}\n");
        (void)off;
        e->jsonl.line_count++;
    }
}

/* TASK_CONSOLIDATE: merge summaries into a long-term pattern */
static void dreami_run_consolidate(DreamionEngine *e) {
    /* Compute a "dream DNA" hash from all memories */
    uint32_t h = dreami_fnv1a(e->memory,
                               e->memory_count * sizeof(DreamionMemory));

    if (e->jsonl.line_count < DREAMION_JSONL_BUF_LINES) {
        char *line = e->jsonl.lines[e->jsonl.line_count];
        uint32_t off = 0;
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            "{\"type\":\"dream_consolidate\",\"memory_count\":");
        off = dreami_fmt_u32(line, DREAMION_JSONL_LINE_MAX, off, e->memory_count);
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off,
                            ",\"dream_hash\":\"0x");
        /* Format h as hex */
        const char *hx = "0123456789ABCDEF";
        for (int bit = 28; bit >= 0 && off + 1 < DREAMION_JSONL_LINE_MAX; bit -= 4)
            line[off++] = hx[(h >> bit) & 0xF];
        line[off] = '\0';
        off = dreami_append(line, DREAMION_JSONL_LINE_MAX, off, "\"}\n");
        (void)off;
        e->jsonl.line_count++;
    }
}

/* ── Main step function ─────────────────────────────────────────── */

DreamionTaskType dreamion_step(DreamionEngine *e) {
    if (!e || e->mode == DREAMION_MODE_OFF || e->awake) return DREAMION_TASK_NONE;

    DreamionTaskType t = dreamion_suggest_task(e);
    e->current_task = t;

    switch (t) {
        case DREAMION_TASK_DEDUP:
            dreami_run_dedup(e);
            break;
        case DREAMION_TASK_COMPACT:
            dreami_run_compact(e);
            break;
        case DREAMION_TASK_PREDICT:
            dreami_run_predict(e);
            break;
        case DREAMION_TASK_SYNTH:
            if (e->mode == DREAMION_MODE_DEEP) dreami_run_synth(e);
            break;
        case DREAMION_TASK_DNA_MUTATE:
            if (e->mode == DREAMION_MODE_DEEP) dreami_run_dna_mutate(e);
            break;
        case DREAMION_TASK_CONSOLIDATE:
            if (e->mode == DREAMION_MODE_DEEP) dreami_run_consolidate(e);
            break;
        case DREAMION_TASK_FLUSH_JSONL:
            /* Caller must call dreamion_flush_jsonl() */
            break;
        default:
            break;
    }

    /* Auto-flush when buffer nearly full */
    if (e->jsonl.line_count >= DREAMION_JSONL_BUF_LINES - 2)
        e->current_task = DREAMION_TASK_FLUSH_JSONL;

    return e->current_task;
}

/* ── DNA mutation query ─────────────────────────────────────────── */

int dreamion_has_dna_mutation(const DreamionEngine *e) {
    return e && e->pending_dna_ready;
}

int dreamion_pop_dna_mutation(DreamionEngine *e,
                               float *out_bias, float *out_temp) {
    if (!e || !e->pending_dna_ready) return 0;
    if (out_bias) *out_bias = e->pending_bias_delta;
    if (out_temp) *out_temp = e->pending_temp_delta;
    e->pending_dna_ready  = 0;
    e->pending_bias_delta = 0.0f;
    e->pending_temp_delta = 0.0f;
    return 1;
}

/* ── JSONL flush ────────────────────────────────────────────────── */

uint32_t dreamion_flush_jsonl(DreamionEngine *e,
                               DreamionFlushCb cb, void *userdata) {
    if (!e || !cb || !e->jsonl.line_count) return 0;
    uint32_t flushed = 0;
    for (uint32_t i = 0; i < e->jsonl.line_count; i++) {
        uint32_t len = 0;
        while (e->jsonl.lines[i][len] && len < DREAMION_JSONL_LINE_MAX) len++;
        if (len > 0) {
            cb(e->jsonl.lines[i], len, userdata);
            flushed++;
        }
    }
    e->jsonl.line_count = 0;
    e->jsonl.total_flushed += flushed;
    e->stats.jsonl_lines_flushed += flushed;
    return flushed;
}

/* ── Diagnostics ────────────────────────────────────────────────── */

const char *dreamion_mode_name_ascii(DreamionMode mode) {
    switch (mode) {
        case DREAMION_MODE_OFF:   return "off";
        case DREAMION_MODE_LIGHT: return "light";
        case DREAMION_MODE_DEEP:  return "deep";
        default:                  return "?";
    }
}

const char *dreamion_task_name_ascii(DreamionTaskType t) {
    switch (t) {
        case DREAMION_TASK_NONE:        return "none";
        case DREAMION_TASK_DEDUP:       return "kv_dedup";
        case DREAMION_TASK_COMPACT:     return "compact";
        case DREAMION_TASK_PREDICT:     return "predict";
        case DREAMION_TASK_SYNTH:       return "synth_selfplay";
        case DREAMION_TASK_DNA_MUTATE:  return "dna_mutate";
        case DREAMION_TASK_CONSOLIDATE: return "consolidate";
        case DREAMION_TASK_FLUSH_JSONL: return "flush_jsonl";
        default:                        return "?";
    }
}

void dreamion_print_status(const DreamionEngine *e,
                           void (*print_fn)(const char *)) {
    if (!e || !print_fn) return;
    char buf[128];
    uint32_t off;

    print_fn("[Dreamion] status\n");

    off = dreami_append(buf, sizeof(buf), 0, "  mode        : ");
    off = dreami_append(buf, sizeof(buf), off, dreamion_mode_name_ascii(e->mode));
    off = dreami_append(buf, sizeof(buf), off, e->awake ? " (awake)\n" : " (dreaming)\n");
    (void)off;
    print_fn(buf);

    off = dreami_append(buf, sizeof(buf), 0, "  idle_ticks   : ");
    off = dreami_fmt_u32(buf, sizeof(buf), off, e->idle_ticks);
    off = dreami_append(buf, sizeof(buf), off, "\n");
    (void)off;
    print_fn(buf);

    off = dreami_append(buf, sizeof(buf), 0, "  memories     : ");
    off = dreami_fmt_u32(buf, sizeof(buf), off, e->memory_count);
    off = dreami_append(buf, sizeof(buf), off, "/");
    off = dreami_fmt_u32(buf, sizeof(buf), off, DREAMION_MEMORY_RING_SIZE);
    off = dreami_append(buf, sizeof(buf), off, "\n");
    (void)off;
    print_fn(buf);

    off = dreami_append(buf, sizeof(buf), 0, "  current_task : ");
    off = dreami_append(buf, sizeof(buf), off,
                        dreamion_task_name_ascii(e->current_task));
    off = dreami_append(buf, sizeof(buf), off, "\n");
    (void)off;
    print_fn(buf);

    off = dreami_append(buf, sizeof(buf), 0, "  synth_pairs  : ");
    off = dreami_fmt_u32(buf, sizeof(buf), off, e->stats.synth_pairs_generated);
    off = dreami_append(buf, sizeof(buf), off, "  dna_mutations: ");
    off = dreami_fmt_u32(buf, sizeof(buf), off, e->stats.dna_mutations_suggested);
    off = dreami_append(buf, sizeof(buf), off, "\n");
    (void)off;
    print_fn(buf);

    off = dreami_append(buf, sizeof(buf), 0, "  jsonl_flushed: ");
    off = dreami_fmt_u32(buf, sizeof(buf), off, e->stats.jsonl_lines_flushed);
    off = dreami_append(buf, sizeof(buf), off, "  wakes        : ");
    off = dreami_fmt_u32(buf, sizeof(buf), off, e->stats.wakes);
    off = dreami_append(buf, sizeof(buf), off, "\n");
    (void)off;
    print_fn(buf);

    if (e->pending_dna_ready) {
        off = dreami_append(buf, sizeof(buf), 0,
                            "  [DNA pending] bias_delta=");
        off = dreami_fmt_f3(buf, sizeof(buf), off, e->pending_bias_delta);
        off = dreami_append(buf, sizeof(buf), off, " temp_delta=");
        off = dreami_fmt_f3(buf, sizeof(buf), off, e->pending_temp_delta);
        off = dreami_append(buf, sizeof(buf), off, "\n");
        (void)off;
        print_fn(buf);
    }
}

