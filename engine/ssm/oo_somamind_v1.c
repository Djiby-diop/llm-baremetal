/* oo_somamind_v1.c — SomaMind V1 implementation
 * Freestanding C11. No libc, no malloc, no printf.
 *
 * Phase SM: compact SSM + adaptive halting + structured tool-use.
 * All state in structs passed by pointer.
 */

#include "oo_somamind_v1.h"

/* ── Internal helpers (no libc) ──────────────────────────────────────────── */

static float sm_fabsf(float x) { return x < 0.0f ? -x : x; }

/* LCG pseudo-random float in [0.0, 1.0) */
static float sm_lcg_randf(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return (float)(*s >> 8) / (float)(1u << 24);
}

/* Zero a byte range */
static void sm_memzero(void *p, int n) {
    unsigned char *b = (unsigned char *)p;
    while (n-- > 0) *b++ = 0;
}

/* String length */
static int sm_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Unsigned 64-bit → decimal string.  Returns chars written. */
static int sm_u64_to_str(char *buf, int cap, uint64_t v) {
    if (cap <= 1) { if (cap == 1) buf[0] = '\0'; return 0; }
    char tmp[24];
    int  tl = 0;
    if (v == 0) {
        tmp[tl++] = '0';
    } else {
        while (v > 0) { tmp[tl++] = '0' + (int)(v % 10); v /= 10; }
        /* reverse */
        for (int i = 0; i < tl / 2; i++) {
            char t = tmp[i]; tmp[i] = tmp[tl - 1 - i]; tmp[tl - 1 - i] = t;
        }
    }
    int n = (tl < cap - 1) ? tl : cap - 1;
    for (int i = 0; i < n; i++) buf[i] = tmp[i];
    buf[n] = '\0';
    return n;
}

/* Float → string with 2 decimal places (freestanding, no snprintf). */
static int sm_f32_to_str(char *buf, int cap, float v) {
    if (cap <= 1) { if (cap == 1) buf[0] = '\0'; return 0; }
    int  i = 0;
    if (v < 0.0f) { if (i < cap - 1) buf[i++] = '-'; v = -v; }
    int  whole = (int)v;
    int  frac  = (int)((v - (float)whole) * 100.0f);
    if (frac < 0)   frac = 0;
    if (frac > 99)  frac = 99;
    char tmp[12];
    int  tl = 0;
    if (whole == 0) {
        tmp[tl++] = '0';
    } else {
        int w = whole;
        while (w > 0) { tmp[tl++] = '0' + w % 10; w /= 10; }
        for (int j = 0; j < tl / 2; j++) {
            char t = tmp[j]; tmp[j] = tmp[tl - 1 - j]; tmp[tl - 1 - j] = t;
        }
    }
    for (int j = 0; j < tl && i < cap - 1; j++) buf[i++] = tmp[j];
    if (i < cap - 1) buf[i++] = '.';
    if (i < cap - 1) buf[i++] = '0' + frac / 10;
    if (i < cap - 1) buf[i++] = '0' + frac % 10;
    buf[i] = '\0';
    return i;
}

static void sm_pfn_str(void (*fn)(const char *), const char *s) { fn(s); }
static void sm_pfn_u64(void (*fn)(const char *), uint64_t v) {
    char b[24]; sm_u64_to_str(b, sizeof(b), v); fn(b);
}
static void sm_pfn_f32(void (*fn)(const char *), float v) {
    char b[16]; sm_f32_to_str(b, sizeof(b), v); fn(b);
}

/* ── Tool tag parser (character-level, cross-token) ─────────────────────── */

/*
 * Process one character through the tool-tag state machine.
 * Tags: <tool>NAME</tool><args>ARGS</args>
 *
 * tag_accum holds the last ≤15 chars seen during detection mode
 * (before we enter in_tool_tag or in_args_tag).  Once inside a tag,
 * characters accumulate in pending_name / pending_args respectively.
 * Closing tags are stripped by back-scanning the accumulated buffer.
 */
static void sm_parse_char(SmToolState *ts, char c) {
    if (ts->found) return; /* already have a complete call */

    if (!ts->in_tool_tag && !ts->in_args_tag) {
        /* Feed into rolling tag-detection accumulator */
        if (ts->tag_accum_len < 15) {
            ts->tag_accum[ts->tag_accum_len++] = c;
        } else {
            for (int i = 0; i < 14; i++) ts->tag_accum[i] = ts->tag_accum[i + 1];
            ts->tag_accum[14] = c;
        }
        int tl = ts->tag_accum_len;
        const char *ta = ts->tag_accum;

        /* Detect <tool> (6 chars) */
        if (tl >= 6 &&
            ta[tl-6]=='<' && ta[tl-5]=='t' && ta[tl-4]=='o' &&
            ta[tl-3]=='o' && ta[tl-2]=='l' && ta[tl-1]=='>') {
            ts->in_tool_tag    = 1;
            ts->name_len       = 0;
            ts->pending_name[0] = '\0';
            ts->tag_accum_len  = 0;
            return;
        }
        /* Detect <args> (6 chars) */
        if (tl >= 6 &&
            ta[tl-6]=='<' && ta[tl-5]=='a' && ta[tl-4]=='r' &&
            ta[tl-3]=='g' && ta[tl-2]=='s' && ta[tl-1]=='>') {
            ts->in_args_tag    = 1;
            ts->args_len       = 0;
            ts->pending_args[0] = '\0';
            ts->tag_accum_len  = 0;
            return;
        }
        return;
    }

    if (ts->in_tool_tag) {
        /* Accumulate name; watch for </tool> (7 chars) */
        if (ts->name_len < 63) {
            ts->pending_name[ts->name_len++] = c;
            ts->pending_name[ts->name_len]   = '\0';
        }
        int nl = ts->name_len;
        char *n = ts->pending_name;
        if (nl >= 7 &&
            n[nl-7]=='<' && n[nl-6]=='/' && n[nl-5]=='t' &&
            n[nl-4]=='o' && n[nl-3]=='o' && n[nl-2]=='l' && n[nl-1]=='>') {
            ts->name_len -= 7;
            ts->pending_name[ts->name_len] = '\0';
            ts->in_tool_tag = 0;
        }
        return;
    }

    if (ts->in_args_tag) {
        /* Accumulate args; watch for </args> (7 chars) */
        int max_args = SOMAMIND_TOOL_MAX_LEN - 1;
        if (ts->args_len < max_args) {
            ts->pending_args[ts->args_len++] = c;
            ts->pending_args[ts->args_len]   = '\0';
        }
        int al = ts->args_len;
        char *a = ts->pending_args;
        if (al >= 7 &&
            a[al-7]=='<' && a[al-6]=='/' && a[al-5]=='a' &&
            a[al-4]=='r' && a[al-3]=='g' && a[al-2]=='s' && a[al-1]=='>') {
            ts->args_len -= 7;
            ts->pending_args[ts->args_len] = '\0';
            ts->in_args_tag = 0;
            ts->found       = 1;
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void sm_init(SomaMindV1 *sm, uint32_t token_budget) {
    sm_memzero(sm, (int)sizeof(*sm));

    uint32_t lcg = 0xC0FFEEu;
    for (int i = 0; i < SOMAMIND_HIDDEN_DIM; i++) {
        sm->ssm.A[i]    = 0.9f;
        sm->ssm.B[i]    = 0.05f + 0.1f * sm_lcg_randf(&lcg);  /* ~0.10 */
        sm->ssm.C[i]    = 0.05f + 0.1f * sm_lcg_randf(&lcg);  /* ~0.10 */
        sm->ssm.bias[i] = 0.0f;
    }

    sm->halt.alpha  = 0.3f;
    sm->halt.budget = token_budget;
    sm->initialized = 1;
}

int sm_register_tool(SomaMindV1 *sm, const char *name,
                     int (*fn)(const char *args, char *out, int cap)) {
    if (!sm || sm->tools.n_tools >= SOMAMIND_N_TOOLS) return -1;
    SmTool *t = &sm->tools.tools[sm->tools.n_tools];
    int i = 0;
    while (name[i] && i < 31) { t->name[i] = name[i]; i++; }
    t->name[i] = '\0';
    t->fn = fn;
    sm->tools.n_tools++;
    return 0;
}

void sm_ssm_step(SmSsmState *s, const float *x, float *y_out) {
    float norm = 0.0f;
    for (int i = 0; i < SOMAMIND_HIDDEN_DIM; i++) {
        s->h[i] = s->A[i] * s->h[i] + s->B[i] * x[i] + s->bias[i];
        float y = s->C[i] * s->h[i];
        /* fast tanh approx: y / (1 + |y|)  — cheaper than true tanh */
        y = y / (1.0f + sm_fabsf(y));
        if (y_out) y_out[i] = y;
        norm += y * y;
    }
    s->last_output_norm = norm;
    s->step++;
}

void sm_ssm_reset(SmSsmState *s) {
    sm_memzero(s->h, (int)sizeof(s->h));
    s->step             = 0;
    s->last_output_norm = 0.0f;
}

SmHaltReason sm_tick(SomaMindV1 *sm, const float *logits, int vocab_size,
                     int token_id, const char *token_str) {
    if (!sm || !sm->initialized || !logits || vocab_size <= 0)
        return SM_HALT_CONTINUE;

    /* ── 1. Confidence from logit spread ─────────────────────────────── */
    float max_l = logits[0], min_l = logits[0];
    for (int i = 1; i < vocab_size; i++) {
        if (logits[i] > max_l) max_l = logits[i];
        if (logits[i] < min_l) min_l = logits[i];
    }
    float range = max_l - min_l;
    float conf  = range / (range + 1e-6f);              /* [0, 1) */
    uint8_t conf8 = (uint8_t)(conf * 255.0f);

    /* ── 2. EMA update ───────────────────────────────────────────────── */
    sm->halt.confidence_ema =
        sm->halt.alpha * conf +
        (1.0f - sm->halt.alpha) * sm->halt.confidence_ema;

    /* ── 3. Token counter ────────────────────────────────────────────── */
    sm->halt.tokens_generated++;

    /* ── 4. Lightweight token embedding (sparse one-hot projection) ──── */
    float x[SOMAMIND_HIDDEN_DIM];
    sm_memzero(x, (int)sizeof(x));
    x[token_id % SOMAMIND_HIDDEN_DIM] = 1.0f;

    sm_ssm_step(&sm->ssm, x, (float *)0);

    /* ── 5. Halting conditions (checked in priority order) ───────────── */
    if (conf8 >= SOMAMIND_HALT_THRESH) {
        sm->halt.last_halt = SM_HALT_CONFIDENT;
        sm->total_halts_confident++;
        /* Count tokens saved vs budget */
        if (sm->halt.budget > sm->halt.tokens_generated)
            sm->total_tokens_saved +=
                (uint64_t)(sm->halt.budget - sm->halt.tokens_generated);
        return SM_HALT_CONFIDENT;
    }

    if (sm->halt.tokens_generated >= sm->halt.budget) {
        sm->halt.last_halt = SM_HALT_BUDGET;
        return SM_HALT_BUDGET;
    }

    /* ── 6. Tool tag parsing ─────────────────────────────────────────── */
    if (token_str) {
        const char *p = token_str;
        while (*p) { sm_parse_char(&sm->tools, *p); p++; }
        if (sm->tools.found) {
            sm->halt.last_halt = SM_HALT_TOOL;
            sm->total_halts_tool++;
            return SM_HALT_TOOL;
        }
    }

    sm->halt.last_halt = SM_HALT_CONTINUE;
    return SM_HALT_CONTINUE;
}

int sm_exec_tool(SomaMindV1 *sm, char *out_buf, int out_cap) {
    if (!sm || !sm->tools.found) return SM_TOOL_NONE;

    const char *target = sm->tools.pending_name;
    int tgt_len = sm_strlen(target);

    for (int i = 0; i < sm->tools.n_tools; i++) {
        SmTool *t = &sm->tools.tools[i];
        int nl = sm_strlen(t->name);
        if (nl != tgt_len) continue;
        int match = 1;
        for (int j = 0; j < nl; j++) {
            if (t->name[j] != target[j]) { match = 0; break; }
        }
        if (!match) continue;

        sm->tools.found = 0;  /* consume */
        if (t->fn) {
            t->fn(sm->tools.pending_args, out_buf, out_cap);
            return SM_TOOL_EXEC;
        }
        return SM_TOOL_UNKNOWN;
    }

    sm->tools.found = 0;
    if (out_cap > 0) out_buf[0] = '\0';
    return SM_TOOL_UNKNOWN;
}

void sm_print_status(const SomaMindV1 *sm, void (*print_fn)(const char *)) {
    if (!sm || !print_fn) return;

    sm_pfn_str(print_fn, "[SomaMind V1] ");
    sm_pfn_str(print_fn, sm->initialized ? "INITIALIZED" : "NOT INIT");
    sm_pfn_str(print_fn, "\r\n");

    sm_pfn_str(print_fn, "  SSM   step=");
    sm_pfn_u64(print_fn, (uint64_t)sm->ssm.step);
    sm_pfn_str(print_fn, "  output_norm=");
    sm_pfn_f32(print_fn, sm->ssm.last_output_norm);
    sm_pfn_str(print_fn, "\r\n");

    sm_pfn_str(print_fn, "  Halt  conf_ema=");
    sm_pfn_f32(print_fn, sm->halt.confidence_ema);
    sm_pfn_str(print_fn, "  tokens=");
    sm_pfn_u64(print_fn, (uint64_t)sm->halt.tokens_generated);
    sm_pfn_str(print_fn, "/");
    sm_pfn_u64(print_fn, (uint64_t)sm->halt.budget);
    sm_pfn_str(print_fn, "\r\n");

    sm_pfn_str(print_fn, "  Stats confident_halts=");
    sm_pfn_u64(print_fn, sm->total_halts_confident);
    sm_pfn_str(print_fn, "  tool_halts=");
    sm_pfn_u64(print_fn, sm->total_halts_tool);
    sm_pfn_str(print_fn, "  tokens_saved=");
    sm_pfn_u64(print_fn, sm->total_tokens_saved);
    sm_pfn_str(print_fn, "\r\n");

    sm_pfn_str(print_fn, "  Tools registered=");
    sm_pfn_u64(print_fn, (uint64_t)sm->tools.n_tools);
    sm_pfn_str(print_fn, "  pending_name=\"");
    sm_pfn_str(print_fn, sm->tools.pending_name);
    sm_pfn_str(print_fn, "\"\r\n");
}
