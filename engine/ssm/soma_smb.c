// soma_smb.c — SomaMind Synaptic Memory Bus implementation
//
// Ring-buffer short-term memory. No malloc. Freestanding C11.

#include "soma_smb.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ============================================================
// soma_smb_hash — FNV-1a 32-bit (same as soma_dna.c)
// ============================================================
uint32_t soma_smb_hash(const char *text, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len && text[i]; i++) {
        h ^= (uint8_t)text[i];
        h *= 16777619u;
    }
    return h;
}

// ============================================================
// soma_smb_init
// ============================================================
void soma_smb_init(SomaSmbCtx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < SOMA_SMB_CAPACITY; i++) {
        ctx->slots[i].input_hash  = 0;
        ctx->slots[i].domain      = 0;
        ctx->slots[i].route       = 0;
        ctx->slots[i].core_used   = 0;
        ctx->slots[i].confidence  = 0.0f;
        ctx->slots[i].relevance   = 0.0f;
        ctx->slots[i].turn        = 0;
        ctx->slots[i].gist_len    = 0;
        for (int j = 0; j < SOMA_SMB_GIST_LEN; j++)
            ctx->slots[i].gist[j] = 0;
    }
    ctx->head         = 0;
    ctx->count        = 0;
    ctx->turn         = 0;
    ctx->total_writes = 0;
    ctx->total_hits   = 0;
    ctx->total_misses = 0;
}

// ============================================================
// soma_smb_write — record completed interaction
// ============================================================
void soma_smb_write(SomaSmbCtx *ctx,
                    uint32_t input_hash,
                    SomaDomain domain,
                    SomaRoute route,
                    SomaCoreUsed core_used,
                    float confidence,
                    const uint16_t *out_tokens,
                    int n_out_tokens) {
    if (!ctx) return;

    SomaSmbSlot *slot = &ctx->slots[ctx->head];
    slot->input_hash = input_hash;
    slot->domain     = (uint8_t)domain;
    slot->route      = (uint8_t)route;
    slot->core_used  = (uint8_t)core_used;
    slot->confidence = confidence;
    slot->relevance  = 1.0f;   // Fresh memory = max relevance
    slot->turn       = ctx->turn;

    // Compress gist: store first SOMA_SMB_GIST_LEN output tokens
    int glen = n_out_tokens < SOMA_SMB_GIST_LEN ? n_out_tokens : SOMA_SMB_GIST_LEN;
    slot->gist_len = (uint8_t)glen;
    if (out_tokens) {
        for (int i = 0; i < glen; i++) slot->gist[i] = out_tokens[i];
    }
    for (int i = glen; i < SOMA_SMB_GIST_LEN; i++) slot->gist[i] = 0;

    // Advance ring
    ctx->head = (ctx->head + 1) & (SOMA_SMB_CAPACITY - 1);
    if (ctx->count < SOMA_SMB_CAPACITY) ctx->count++;
    ctx->total_writes++;
}

// ============================================================
// soma_smb_tick — advance turn, decay relevance
// ============================================================
void soma_smb_tick(SomaSmbCtx *ctx) {
    if (!ctx) return;
    ctx->turn++;
    for (int i = 0; i < ctx->count; i++) {
        ctx->slots[i].relevance *= SOMA_SMB_DECAY;
        // Hard-zero very stale slots (< 1% relevance)
        if (ctx->slots[i].relevance < 0.01f)
            ctx->slots[i].relevance = 0.0f;
    }
}

// ============================================================
// soma_smb_query — find best matching slot
//
// Score = relevance * domain_bonus * hash_proximity
//   domain_bonus: 1.0 if same domain, 0.5 if different
//   hash_proximity: 1.0 if hash matches exactly,
//                   0.5 if upper 16 bits match (related topic),
//                   0.1 otherwise (different topic)
// Threshold: only return match if score >= 0.15
// ============================================================
SomaSmbMatch soma_smb_query(const SomaSmbCtx *ctx,
                             uint32_t input_hash,
                             SomaDomain domain) {
    SomaSmbMatch result;
    result.found      = 0;
    result.slot_idx   = -1;
    result.match_score = 0.0f;
    result.slot       = NULL;

    if (!ctx || ctx->count == 0) return result;

    float best_score = 0.14f;  // Minimum threshold
    int   best_idx   = -1;

    for (int i = 0; i < ctx->count; i++) {
        const SomaSmbSlot *s = &ctx->slots[i];
        if (s->relevance < 0.01f) continue;  // Too stale

        // Hash proximity
        float hash_prox;
        if (s->input_hash == input_hash) {
            hash_prox = 1.0f;       // Exact match — same query
        } else if ((s->input_hash >> 16) == (input_hash >> 16)) {
            hash_prox = 0.5f;       // Upper bits match — related topic
        } else {
            hash_prox = 0.1f;       // Different topic
        }

        // Domain bonus
        float domain_bonus = ((SomaDomain)s->domain == domain) ? 1.0f : 0.5f;

        float score = s->relevance * domain_bonus * hash_prox;
        if (score > best_score) {
            best_score = score;
            best_idx   = i;
        }
    }

    if (best_idx >= 0) {
        result.found       = 1;
        result.slot_idx    = best_idx;
        result.match_score = best_score;
        result.slot        = &ctx->slots[best_idx];
        // (caller updates total_hits — ctx is const here)
    }

    return result;
}

// ============================================================
// soma_smb_print_stats — inline integer formatting, no libc
// ============================================================

static void _smb_fmt_int(char *buf, int v) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int neg = (v < 0);
    if (neg) v = -v;
    char tmp[12]; int n = 0;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int start = 0;
    if (neg) buf[start++] = '-';
    for (int i = n - 1; i >= 0; i--) buf[start++] = tmp[i];
    buf[start] = 0;
}

static void _smb_print_str(SomaSmbPrintFn fn, const char *a, const char *b) {
    // Print "a" + integer string b — simple concat onto stack
    char line[64];
    int i = 0;
    while (a[i] && i < 48) { line[i] = a[i]; i++; }
    int j = 0;
    while (b[j] && i < 62) { line[i++] = b[j++]; }
    line[i++] = '\n'; line[i] = 0;
    fn(line);
}

void soma_smb_print_stats(const SomaSmbCtx *ctx, SomaSmbPrintFn fn) {
    if (!ctx || !fn) return;
    char num[16];

    fn("[SomaMind SMB — Synaptic Memory Bus]\n");

    _smb_fmt_int(num, ctx->count);
    _smb_print_str(fn, "  slots_used   : ", num);
    _smb_fmt_int(num, SOMA_SMB_CAPACITY);
    _smb_print_str(fn, "  capacity     : ", num);
    _smb_fmt_int(num, (int)ctx->turn);
    _smb_print_str(fn, "  turn         : ", num);
    _smb_fmt_int(num, ctx->total_writes);
    _smb_print_str(fn, "  total_writes : ", num);
    _smb_fmt_int(num, ctx->total_hits);
    _smb_print_str(fn, "  total_hits   : ", num);
    _smb_fmt_int(num, ctx->total_misses);
    _smb_print_str(fn, "  total_misses : ", num);

    // Show top-3 most relevant slots
    fn("  top_slots:\n");
    int shown = 0;
    for (int r = 0; r < ctx->count && shown < 3; r++) {
        const SomaSmbSlot *s = &ctx->slots[r];
        if (s->relevance < 0.01f) continue;
        fn("    [");
        _smb_fmt_int(num, r); fn(num); fn("] ");
        fn("hash=0x");
        // Print hash as hex (simple)
        char hex[12]; int hv = (int)s->input_hash;
        for (int k = 7; k >= 0; k--) {
            int nibble = (hv >> (k * 4)) & 0xF;
            hex[7 - k] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        }
        hex[8] = 0; fn(hex);
        fn(" rel=");
        _smb_fmt_int(num, (int)(s->relevance * 100)); fn(num); fn("%");
        fn(" dom="); _smb_fmt_int(num, s->domain); fn(num);
        fn("\n");
        shown++;
    }
}
