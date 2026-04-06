// soma_dream.c — SomaMind Dreaming Engine implementation
// Freestanding C11, no libc.

#include "soma_dream.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ============================================================
// FNV-1a hash (reused internally)
// ============================================================
static uint32_t _dream_fnv(const uint8_t *data, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

// ============================================================
// soma_dream_init
// ============================================================
void soma_dream_init(SomaDreamCtx *ctx) {
    if (!ctx) return;
    ctx->history_head    = 0;
    ctx->history_count   = 0;
    ctx->total_dreams    = 0;
    ctx->dna_adjustments = 0;
    for (int i = 0; i < SOMA_DREAM_HISTORY; i++) {
        SomaDreamSummary *s = &ctx->history[i];
        s->dominant_domain         = SOMA_DOMAIN_CHAT;
        s->avg_confidence          = 0.0f;
        s->avg_relevance           = 0.0f;
        s->solar_count             = 0;
        s->lunar_count             = 0;
        s->total_slots             = 0;
        s->recommended_bias_delta  = 0.0f;
        s->recommended_temp_delta  = 0.0f;
        s->dream_quality           = 0;
        s->dream_hash              = 0;
        for (int d = 0; d < 6; d++) s->domain_counts[d] = 0;
        for (int t = 0; t < 3; t++) {
            s->top_gist_tokens[t] = 0;
            s->top_gist_counts[t] = 0;
        }
    }
}

// ============================================================
// soma_dream_run — scan SMB and extract patterns
// ============================================================
SomaDreamSummary soma_dream_run(SomaDreamCtx *ctx, const SomaSmbCtx *smb) {
    SomaDreamSummary out;
    out.dominant_domain        = SOMA_DOMAIN_CHAT;
    out.avg_confidence         = 0.0f;
    out.avg_relevance          = 0.0f;
    out.solar_count            = 0;
    out.lunar_count            = 0;
    out.total_slots            = 0;
    out.recommended_bias_delta = 0.0f;
    out.recommended_temp_delta = 0.0f;
    out.dream_quality          = 0;
    out.dream_hash             = 0;
    for (int d = 0; d < 6; d++) out.domain_counts[d] = 0;
    for (int t = 0; t < 3; t++) { out.top_gist_tokens[t] = 0; out.top_gist_counts[t] = 0; }

    if (!smb || smb->count == 0) return out;

    // ── Pass 1: aggregate stats ──────────────────────────────────────
    float conf_acc = 0.0f, rel_acc = 0.0f;
    // Simple token frequency table: track counts for up to 256 unique tokens
    // Using a tiny open-addressing hash table
    #define GIST_TABLE_SIZE 64
    uint16_t gist_keys[GIST_TABLE_SIZE];
    int      gist_vals[GIST_TABLE_SIZE];
    for (int i = 0; i < GIST_TABLE_SIZE; i++) { gist_keys[i] = 0xFFFF; gist_vals[i] = 0; }

    for (int i = 0; i < smb->count; i++) {
        const SomaSmbSlot *s = &smb->slots[i];
        if (s->relevance < 0.01f) continue;

        int dom = (int)s->domain;
        if (dom >= 0 && dom < 6) out.domain_counts[dom]++;

        conf_acc += s->confidence;
        rel_acc  += s->relevance;
        out.total_slots++;

        if ((SomaCoreUsed)s->core_used == SOMA_CORE_SOLAR ||
            (SomaCoreUsed)s->core_used == SOMA_CORE_FUSION)
            out.solar_count++;
        else
            out.lunar_count++;

        // Gist token frequency (first token of each slot)
        if (s->gist_len > 0) {
            uint16_t tok = s->gist[0];
            int slot_idx = tok & (GIST_TABLE_SIZE - 1);
            for (int probe = 0; probe < GIST_TABLE_SIZE; probe++) {
                int idx = (slot_idx + probe) & (GIST_TABLE_SIZE - 1);
                if (gist_keys[idx] == 0xFFFF) {
                    gist_keys[idx] = tok;
                    gist_vals[idx] = 1;
                    break;
                }
                if (gist_keys[idx] == tok) {
                    gist_vals[idx]++;
                    break;
                }
            }
        }
    }

    if (out.total_slots == 0) return out;

    out.avg_confidence = conf_acc / (float)out.total_slots;
    out.avg_relevance  = rel_acc  / (float)out.total_slots;

    // ── Find dominant domain ─────────────────────────────────────────
    int best_dom = 0;
    for (int d = 1; d < 6; d++)
        if (out.domain_counts[d] > out.domain_counts[best_dom]) best_dom = d;
    out.dominant_domain = (SomaDomain)best_dom;

    // ── Top-3 gist tokens ────────────────────────────────────────────
    for (int rank = 0; rank < 3; rank++) {
        int best_cnt = 0, best_idx = -1;
        for (int i = 0; i < GIST_TABLE_SIZE; i++) {
            if (gist_keys[i] == 0xFFFF) continue;
            // Skip already picked
            int already = 0;
            for (int r2 = 0; r2 < rank; r2++)
                if (out.top_gist_tokens[r2] == gist_keys[i]) { already = 1; break; }
            if (already) continue;
            if (gist_vals[i] > best_cnt) { best_cnt = gist_vals[i]; best_idx = i; }
        }
        if (best_idx >= 0) {
            out.top_gist_tokens[rank] = gist_keys[best_idx];
            out.top_gist_counts[rank] = best_cnt;
        }
    }

    // ── Compute recommended DNA adjustments ─────────────────────────
    // If Lunar chosen more than Solar → increase cognition_bias (more creative)
    float solar_frac = out.total_slots > 0
        ? (float)out.solar_count / (float)out.total_slots : 0.5f;
    // Target: bias toward the core that produced higher confidence
    // Simple rule: if avg_confidence < 0.3 (model unsure) → push toward creative (Lunar)
    //              if avg_confidence > 0.6 (model confident) → push toward logical (Solar)
    if (out.avg_confidence < 0.30f) {
        out.recommended_bias_delta  = +0.05f;  // More creative
        out.recommended_temp_delta  = +0.02f;  // Slightly warmer Solar
    } else if (out.avg_confidence > 0.60f && solar_frac > 0.7f) {
        out.recommended_bias_delta  = -0.03f;  // Slightly more logical
        out.recommended_temp_delta  = -0.01f;
    }
    // else: no adjustment recommended

    // ── Dream quality = how full was the SMB ────────────────────────
    out.dream_quality = (out.total_slots * 100) / SOMA_SMB_CAPACITY;

    // ── Dream hash ──────────────────────────────────────────────────
    out.dream_hash = _dream_fnv((const uint8_t *)&out, sizeof(out) - sizeof(out.dream_hash));

    // ── Store in history ─────────────────────────────────────────────
    if (ctx) {
        ctx->history[ctx->history_head] = out;
        ctx->history_head = (ctx->history_head + 1) & (SOMA_DREAM_HISTORY - 1);
        if (ctx->history_count < SOMA_DREAM_HISTORY) ctx->history_count++;
        ctx->total_dreams++;
    }

    return out;
    #undef GIST_TABLE_SIZE
}

// ============================================================
// soma_dream_apply — apply dream recommendations to DNA
// ============================================================
void soma_dream_apply(SomaDreamCtx *ctx, SomaDNA *dna,
                      const SomaDreamSummary *dream, int safe) {
    if (!dna || !dream || dream->total_slots < 2) return;

    float bias_delta = dream->recommended_bias_delta;
    float temp_delta = dream->recommended_temp_delta;

    if (safe) {
        // Clamp to ±0.05
        if (bias_delta >  0.05f) bias_delta =  0.05f;
        if (bias_delta < -0.05f) bias_delta = -0.05f;
        if (temp_delta >  0.05f) temp_delta =  0.05f;
        if (temp_delta < -0.05f) temp_delta = -0.05f;
    }

    // Apply
    dna->cognition_bias     += bias_delta;
    dna->temperature_solar  += temp_delta;

    // Clamp to valid ranges
    if (dna->cognition_bias    < 0.0f) dna->cognition_bias    = 0.0f;
    if (dna->cognition_bias    > 1.0f) dna->cognition_bias    = 1.0f;
    if (dna->temperature_solar < 0.05f) dna->temperature_solar = 0.05f;
    if (dna->temperature_solar > 2.0f)  dna->temperature_solar = 2.0f;

    // Update DNA generation (dreaming counts as a meta-evolution step)
    dna->generation++;

    if (ctx) ctx->dna_adjustments++;
}

// ============================================================
// soma_dream_should_run
// ============================================================
int soma_dream_should_run(const SomaDreamCtx *ctx,
                          const SomaSmbCtx *smb,
                          int min_turns_since_last) {
    if (!ctx || !smb) return 0;
    if (smb->count < 4) return 0;  // Not enough data yet

    if (ctx->total_dreams == 0) return 1;  // First dream

    // Check turns since last dream: use the last history entry's turn vs smb->turn
    int last_idx = (ctx->history_head - 1 + SOMA_DREAM_HISTORY) & (SOMA_DREAM_HISTORY - 1);
    // We don't store the turn in history directly; use total_dreams as proxy
    // Trigger every min_turns_since_last SMB ticks
    int turns_mod = (int)smb->turn % (min_turns_since_last > 0 ? min_turns_since_last : 8);
    return (turns_mod == 0) ? 1 : 0;
}

// ============================================================
// soma_dream_print — inline integer printing, no libc
// ============================================================
static void _dpr_int(char *buf, int v) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int neg = (v < 0); if (neg) v = -v;
    char t[12]; int n = 0;
    while (v > 0) { t[n++] = '0' + (v % 10); v /= 10; }
    int s = 0; if (neg) buf[s++] = '-';
    for (int i = n - 1; i >= 0; i--) buf[s++] = t[i];
    buf[s] = 0;
}

static void _dpr(SomaDreamPrintFn fn, const char *label, int val) {
    char line[64]; int i = 0;
    while (label[i] && i < 48) { line[i] = label[i]; i++; }
    char num[16]; _dpr_int(num, val);
    int j = 0; while (num[j] && i < 62) line[i++] = num[j++];
    line[i++] = '\n'; line[i] = 0;
    fn(line);
}

void soma_dream_print(const SomaDreamSummary *dream, SomaDreamPrintFn fn) {
    if (!dream || !fn) return;
    fn("[SomaMind Dream Summary]\n");
    _dpr(fn, "  total_slots   : ", dream->total_slots);
    _dpr(fn, "  dream_quality : ", dream->dream_quality);
    _dpr(fn, "  dominant_dom  : ", (int)dream->dominant_domain);
    _dpr(fn, "  solar_count   : ", dream->solar_count);
    _dpr(fn, "  lunar_count   : ", dream->lunar_count);
    // Confidence as percentage
    _dpr(fn, "  avg_conf_pct  : ", (int)(dream->avg_confidence * 100.0f));
    _dpr(fn, "  bias_delta*100: ", (int)(dream->recommended_bias_delta * 100.0f));
    _dpr(fn, "  temp_delta*100: ", (int)(dream->recommended_temp_delta * 100.0f));
    fn("  top_gist: ");
    for (int i = 0; i < 3 && dream->top_gist_counts[i] > 0; i++) {
        char num[16]; _dpr_int(num, (int)dream->top_gist_tokens[i]);
        fn(num); fn("(x"); _dpr_int(num, dream->top_gist_counts[i]); fn(num); fn(") ");
    }
    fn("\n");
}
