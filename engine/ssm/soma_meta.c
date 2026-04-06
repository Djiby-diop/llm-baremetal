// soma_meta.c — SomaMind Meta-Evolution Engine implementation
// Freestanding C11, no libc.

#include "soma_meta.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ============================================================
// soma_meta_init
// ============================================================
void soma_meta_init(SomaMetaCtx *ctx) {
    if (!ctx) return;
    ctx->history_head     = 0;
    ctx->history_count    = 0;
    ctx->best_score       = 0.0f;
    ctx->best_dna_hash    = 0;
    ctx->stagnation_count = 0;
    ctx->mutations_applied = 0;
    ctx->total_evaluations = 0;
    ctx->w_confidence     = 0.40f;
    ctx->w_reflex         = 0.25f;
    ctx->w_agreement      = 0.20f;
    ctx->w_memory         = 0.15f;
    for (int i = 0; i < SOMA_META_HISTORY; i++) {
        ctx->history[i].score              = 0.0f;
        ctx->history[i].confidence_contrib = 0.0f;
        ctx->history[i].reflex_contrib     = 0.0f;
        ctx->history[i].agreement_contrib  = 0.0f;
        ctx->history[i].memory_contrib     = 0.0f;
        ctx->history[i].generation         = 0;
        ctx->history[i].dna_hash           = 0;
    }
}

// ============================================================
// soma_meta_score
// ============================================================
SomaMetaFitness soma_meta_score(SomaMetaCtx *ctx,
                                const SomaDNA *dna,
                                const SomaRouterCtx *router,
                                const SomaDualCtx *dual,
                                const SomaSmbCtx *smb) {
    SomaMetaFitness f;
    f.score              = 0.0f;
    f.confidence_contrib = 0.0f;
    f.reflex_contrib     = 0.0f;
    f.agreement_contrib  = 0.0f;
    f.memory_contrib     = 0.0f;
    f.generation         = dna ? (int)dna->generation : 0;
    f.dna_hash           = dna ? soma_dna_hash(dna) : 0;

    float w_conf = ctx ? ctx->w_confidence : 0.40f;
    float w_ref  = ctx ? ctx->w_reflex     : 0.25f;
    float w_agr  = ctx ? ctx->w_agreement  : 0.20f;
    float w_mem  = ctx ? ctx->w_memory     : 0.15f;

    // ── Component 1: avg confidence from SMB ────────────────────────
    if (smb && smb->count > 0) {
        float acc = 0.0f; int n = 0;
        for (int i = 0; i < smb->count; i++) {
            if (smb->slots[i].relevance > 0.01f) {
                acc += smb->slots[i].confidence;
                n++;
            }
        }
        if (n > 0) f.confidence_contrib = acc / (float)n;
    }

    // ── Component 2: reflex hit rate ────────────────────────────────
    if (router && router->total_routed > 0) {
        f.reflex_contrib = (float)router->reflex_count / (float)router->total_routed;
    }

    // ── Component 3: core agreement rate ────────────────────────────
    if (dual) {
        int total = dual->stats.total_tokens;
        if (total > 0) {
            f.agreement_contrib = (float)dual->stats.agreements / (float)total;
        }
    }

    // ── Component 4: SMB memory hit rate ────────────────────────────
    if (smb) {
        int total_queries = smb->total_hits + smb->total_misses;
        if (total_queries > 0) {
            f.memory_contrib = (float)smb->total_hits / (float)total_queries;
        }
    }

    // ── Weighted sum ─────────────────────────────────────────────────
    f.score = w_conf * f.confidence_contrib
            + w_ref  * f.reflex_contrib
            + w_agr  * f.agreement_contrib
            + w_mem  * f.memory_contrib;

    // Store in history
    if (ctx) {
        ctx->history[ctx->history_head] = f;
        ctx->history_head = (ctx->history_head + 1) & (SOMA_META_HISTORY - 1);
        if (ctx->history_count < SOMA_META_HISTORY) ctx->history_count++;
        ctx->total_evaluations++;

        if (f.score > ctx->best_score) {
            ctx->best_score    = f.score;
            ctx->best_dna_hash = f.dna_hash;
            ctx->stagnation_count = 0;
        } else {
            ctx->stagnation_count++;
        }
    }

    return f;
}

// ============================================================
// soma_meta_evolve
// ============================================================
int soma_meta_evolve(SomaMetaCtx *ctx,
                     SomaDNA *dna,
                     const SomaMetaFitness *fitness,
                     int stagnation_threshold) {
    if (!ctx || !dna || !fitness) return 0;
    if (stagnation_threshold <= 0) stagnation_threshold = 5;

    if (ctx->stagnation_count < stagnation_threshold) return 0;

    // Stagnation detected → mutate DNA
    // Use generation as entropy source for mutation direction
    uint32_t seed = fitness->dna_hash ^ ((uint32_t)ctx->stagnation_count * 2654435761u);
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;

    // Mutation magnitude: small (0.02 - 0.08) to avoid destabilizing
    float mag = 0.02f + 0.06f * ((float)(seed & 0xFF) / 255.0f);
    soma_dna_mutate(dna, mag);

    ctx->stagnation_count = 0;  // Reset stagnation counter after mutation
    ctx->mutations_applied++;

    return 1;
}

// ============================================================
// soma_meta_cycle — score + evolve in one call
// ============================================================
int soma_meta_cycle(SomaMetaCtx *ctx,
                    SomaDNA *dna,
                    const SomaRouterCtx *router,
                    const SomaDualCtx *dual,
                    const SomaSmbCtx *smb,
                    int stagnation_threshold) {
    if (!ctx || !dna) return 0;
    SomaMetaFitness f = soma_meta_score(ctx, dna, router, dual, smb);
    return soma_meta_evolve(ctx, dna, &f, stagnation_threshold);
}

// ============================================================
// soma_meta_print_stats — inline integer printing
// ============================================================
static void _mpr_int(char *buf, int v) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int neg = (v < 0); if (neg) v = -v;
    char t[12]; int n = 0;
    while (v > 0) { t[n++] = '0' + (v % 10); v /= 10; }
    int s = 0; if (neg) buf[s++] = '-';
    for (int i = n - 1; i >= 0; i--) buf[s++] = t[i];
    buf[s] = 0;
}

static void _mpr(SomaMetaPrintFn fn, const char *label, int val) {
    char line[64]; int i = 0;
    while (label[i] && i < 48) { line[i] = label[i]; i++; }
    char num[16]; _mpr_int(num, val);
    int j = 0; while (num[j] && i < 62) line[i++] = num[j++];
    line[i++] = '\n'; line[i] = 0;
    fn(line);
}

void soma_meta_print_stats(const SomaMetaCtx *ctx, SomaMetaPrintFn fn) {
    if (!ctx || !fn) return;
    fn("[SomaMind Meta-Evolution]\n");
    _mpr(fn, "  total_evals    : ", ctx->total_evaluations);
    _mpr(fn, "  mutations      : ", ctx->mutations_applied);
    _mpr(fn, "  stagnation     : ", ctx->stagnation_count);
    _mpr(fn, "  best_score_pct : ", (int)(ctx->best_score * 100.0f));

    fn("  fitness_history:\n");
    int n = ctx->history_count < SOMA_META_HISTORY ? ctx->history_count : SOMA_META_HISTORY;
    // Print from oldest to newest
    int start = (ctx->history_head - n + SOMA_META_HISTORY) & (SOMA_META_HISTORY - 1);
    for (int i = 0; i < n; i++) {
        int idx = (start + i) & (SOMA_META_HISTORY - 1);
        const SomaMetaFitness *f = &ctx->history[idx];
        fn("    gen="); char num[16]; _mpr_int(num, f->generation); fn(num);
        fn(" score="); _mpr_int(num, (int)(f->score * 100)); fn(num); fn("%");
        fn(" conf=");  _mpr_int(num, (int)(f->confidence_contrib * 100)); fn(num); fn("%");
        fn(" ref=");   _mpr_int(num, (int)(f->reflex_contrib * 100)); fn(num); fn("%\n");
    }
}
