// soma_memory.c — SomaMind Phase H: Session Memory & Journal Reflex
//
// Implements rolling session memory: records (prompt, response) pairs,
// detects similarity to past prompts, and injects history context.
//
// Freestanding C11 — no libc, no UEFI calls.

#include "soma_memory.h"

// ============================================================
// Internal helpers
// ============================================================

// djb2 hash for fast prompt comparison
static unsigned int soma_mem_hash(const char *s) {
    unsigned int h = 5381;
    while (*s) { h = ((h << 5) + h) ^ (unsigned char)(*s); s++; }
    return h;
}

static int soma_mem_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void soma_mem_strncpy(char *dst, const char *src, int max) {
    int i = 0;
    for (; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int soma_mem_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

// Append a string to dst (NUL-terminated), stop at dst_cap.
// Returns new length.
static int soma_mem_append(char *dst, int cur_len, int dst_cap,
                           const char *src) {
    int i = 0;
    while (src[i] && cur_len + i < dst_cap - 1) {
        dst[cur_len + i] = src[i];
        i++;
    }
    dst[cur_len + i] = 0;
    return cur_len + i;
}

// Append a decimal integer
static int soma_mem_append_int(char *dst, int cur_len, int dst_cap, int v) {
    char tmp[16];
    int neg = (v < 0);
    if (neg) v = -v;
    int i = 0;
    if (v == 0) { tmp[i++] = '0'; }
    else { while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; } }
    if (neg) tmp[i++] = '-';
    // reverse
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t;
    }
    tmp[i] = 0;
    return soma_mem_append(dst, cur_len, dst_cap, tmp);
}

// Compute rough similarity score (0-100) between two prompts.
// Uses: hash equality (100), prefix match (60), first-word match (30).
static int soma_mem_similarity(const char *a, unsigned int hash_a,
                               const char *b, unsigned int hash_b) {
    if (hash_a == hash_b && soma_mem_strncmp(a, b, SOMA_MEM_PROMPT_LEN) == 0)
        return 100;

    // Compare first SOMA_MEM_SIM_PREFIX characters
    int plen = SOMA_MEM_SIM_PREFIX;
    int match = 0;
    for (int i = 0; i < plen; i++) {
        if (!a[i] && !b[i]) { match = plen; break; }
        if (!a[i] || !b[i]) break;
        if (a[i] == b[i]) match++;
        else break;
    }
    if (match >= plen) return 60;

    // First "word" match (up to first space)
    int wa = 0;
    while (a[wa] && a[wa] != ' ') wa++;
    int wb = 0;
    while (b[wb] && b[wb] != ' ') wb++;
    if (wa > 0 && wa == wb && soma_mem_strncmp(a, b, wa) == 0)
        return 30;

    return 0;
}

// ============================================================
// API
// ============================================================

void soma_memory_init(SomaMemCtx *ctx) {
    // Preserve boot_count and model_name across re-inits (if re-called)
    int bc = ctx->boot_count + 1;
    char model_bak[SOMA_MEM_MODEL_LEN];
    soma_mem_strncpy(model_bak, ctx->model_name, SOMA_MEM_MODEL_LEN);

    // Zero the whole struct
    for (int i = 0; i < (int)sizeof(SomaMemCtx); i++)
        ((char*)ctx)[i] = 0;

    ctx->enabled    = 1;
    ctx->boot_count = bc;
    soma_mem_strncpy(ctx->model_name, model_bak, SOMA_MEM_MODEL_LEN);
}

void soma_memory_set_model(SomaMemCtx *ctx, const char *model_name) {
    soma_mem_strncpy(ctx->model_name, model_name, SOMA_MEM_MODEL_LEN);
}

SomaMemResult soma_memory_scan(SomaMemCtx *ctx, const char *prompt) {
    SomaMemResult r;
    for (int i = 0; i < (int)sizeof(SomaMemResult); i++) ((char*)&r)[i] = 0;

    r.current_turn = ctx->total_turns;
    r.boot_count   = ctx->boot_count;
    soma_mem_strncpy(r.model_name, ctx->model_name, SOMA_MEM_MODEL_LEN);

    if (!ctx->enabled || ctx->count == 0 || !prompt || !prompt[0])
        return r;

    unsigned int ph = soma_mem_hash(prompt);

    // Search all valid entries, find best match
    int best_idx = -1;
    int best_score = 0;

    for (int i = 0; i < SOMA_MEM_MAX_ENTRIES; i++) {
        SomaMemEntry *e = &ctx->entries[i];
        if (!e->valid) continue;
        int score = soma_mem_similarity(prompt, ph, e->prompt, e->prompt_hash);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    // Only inject if similarity ≥ 30 (at least first-word match)
    if (best_idx < 0 || best_score < 30) return r;

    SomaMemEntry *best = &ctx->entries[best_idx];
    r.match_found      = 1;
    r.match_turn       = best->turn;
    r.match_similarity = best_score;
    soma_mem_strncpy(r.match_prompt,   best->prompt,   SOMA_MEM_PROMPT_LEN);
    soma_mem_strncpy(r.match_response, best->response, SOMA_MEM_RESPONSE_LEN);

    // Build injection string: [MEM: turn=N sim=S response="..."]
    int len = 0;
    len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, "[MEM: turn=");
    len = soma_mem_append_int(r.injection, len, SOMA_MEM_INJECT_MAX, r.match_turn);
    len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, " sim=");
    len = soma_mem_append_int(r.injection, len, SOMA_MEM_INJECT_MAX, best_score);
    if (r.model_name[0]) {
        len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, " model=");
        len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, r.model_name);
    }
    len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, " boot=");
    len = soma_mem_append_int(r.injection, len, SOMA_MEM_INJECT_MAX, r.boot_count);
    if (best->response[0]) {
        len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, " prev=\"");
        // Truncate response to first 40 chars for injection
        char trunc[41];
        soma_mem_strncpy(trunc, best->response, 41);
        len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, trunc);
        len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, "\"");
    }
    len = soma_mem_append(r.injection, len, SOMA_MEM_INJECT_MAX, "]\n");
    r.injection_len = len;
    r.triggered     = 1;

    ctx->total_triggers++;
    return r;
}

void soma_memory_record(SomaMemCtx *ctx, const char *prompt,
                        const char *response_summary) {
    if (!prompt || !prompt[0]) return;

    SomaMemEntry *slot = &ctx->entries[ctx->head];
    slot->valid       = 1;
    slot->turn        = ctx->total_turns;
    slot->prompt_hash = soma_mem_hash(prompt);
    slot->domain      = 0;  // default: SOMA_DOMAIN_CHAT
    soma_mem_strncpy(slot->prompt,   prompt,           SOMA_MEM_PROMPT_LEN);
    soma_mem_strncpy(slot->response, response_summary ? response_summary : "",
                     SOMA_MEM_RESPONSE_LEN);

    ctx->head = (ctx->head + 1) % SOMA_MEM_MAX_ENTRIES;
    if (ctx->count < SOMA_MEM_MAX_ENTRIES) ctx->count++;
    ctx->total_turns++;
}

// Phase R: Record with explicit domain tag
void soma_memory_record_tagged(SomaMemCtx *ctx, const char *prompt,
                               const char *response_summary,
                               unsigned char domain) {
    if (!prompt || !prompt[0]) return;

    SomaMemEntry *slot = &ctx->entries[ctx->head];
    slot->valid       = 1;
    slot->turn        = ctx->total_turns;
    slot->prompt_hash = soma_mem_hash(prompt);
    slot->domain      = domain;
    soma_mem_strncpy(slot->prompt,   prompt,           SOMA_MEM_PROMPT_LEN);
    soma_mem_strncpy(slot->response, response_summary ? response_summary : "",
                     SOMA_MEM_RESPONSE_LEN);

    ctx->head = (ctx->head + 1) % SOMA_MEM_MAX_ENTRIES;
    if (ctx->count < SOMA_MEM_MAX_ENTRIES) ctx->count++;
    ctx->total_turns++;
}

// Phase R: Count entries matching a domain
int soma_memory_count_domain(const SomaMemCtx *ctx, unsigned char domain) {
    if (!ctx) return 0;
    int count = 0;
    for (int i = 0; i < SOMA_MEM_MAX_ENTRIES; i++) {
        if (ctx->entries[i].valid && ctx->entries[i].domain == domain)
            count++;
    }
    return count;
}

void soma_memory_print_stats(const SomaMemCtx *ctx) {
    // Caller is expected to have Print() available — but this is a
    // freestanding module. We write to a temp buffer; caller prints it.
    // (Actual print is done in the god file REPL command handler.)
    (void)ctx;
    // Intentionally empty — stats are formatted by the REPL handler.
}
