// soma_dual.c — SomaMind Dual Core Engine
//
// ☀ Solar + 🌙 Lunar sampling from the same logit vector.
// No second forward pass — just two different sampling strategies.
//
// Freestanding C11 — no libc.

#include "soma_dual.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Freestanding math helpers ──────────────────────────────────────────────

static float _dual_expf(float x) {
    // Fast exp approximation (Schraudolph 1999 — good to ~3%)
    if (x < -88.0f) return 0.0f;
    if (x >  88.0f) return 3.4e38f;
    union { float f; int i; } u;
    u.i = (int)(12102203.0f * x) + 1065353216;
    return u.f;
}

static void _dual_memcpy_f32(ssm_f32 *dst, const ssm_f32 *src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

// ── Softmax + sampling (self-contained, don't depend on oosi_v3 statics) ──

static void _dual_softmax(ssm_f32 *x, int n) {
    // Find max for numerical stability
    ssm_f32 max = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max) max = x[i];
    // Exponentiate and sum
    ssm_f32 sum = 0.0f;
    for (int i = 0; i < n; i++) { x[i] = _dual_expf(x[i] - max); sum += x[i]; }
    ssm_f32 inv = 1.0f / sum;
    for (int i = 0; i < n; i++) x[i] *= inv;
}

static int _dual_argmax(const ssm_f32 *x, int n) {
    int best = 0;
    for (int i = 1; i < n; i++) if (x[i] > x[best]) best = i;
    return best;
}

static uint32_t _dual_xorshift(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// Sort-free top-p nucleus sampling (same algorithm as oosi_v3)
static int _dual_sample_topp(const ssm_f32 *probs, int n,
                             ssm_f32 top_p, uint32_t *rng) {
    int best = _dual_argmax(probs, n);
    if (top_p <= 0.0f) return best;

    // Random float [0, 1)
    uint32_t rv = _dual_xorshift(rng);
    ssm_f32 r = (rv >> 8) * (1.0f / (1u << 24));

    // Sort-free nucleus
    ssm_f32 thresh = probs[best] * 0.005f;
    ssm_f32 nucleus_mass = 0.0f;
    for (int i = 0; i < n; i++)
        if (probs[i] >= thresh) nucleus_mass += probs[i];

    ssm_f32 cutoff = (nucleus_mass < top_p) ? nucleus_mass : top_p;
    ssm_f32 target = r * cutoff;
    ssm_f32 cumul = 0.0f;
    for (int i = 0; i < n; i++) {
        if (probs[i] >= thresh) {
            cumul += probs[i];
            if (cumul >= target) return i;
        }
    }
    for (int i = 0; i < n; i++) {
        if (probs[i] < thresh) {
            cumul += probs[i];
            if (cumul >= target) return i;
        }
    }
    return best;
}

// Apply temperature scaling then softmax → return max prob as confidence
// Returns the sampled token and sets *max_prob_out.
static int _dual_tempered_sample(const ssm_f32 *raw_logits, int n,
                                 float temperature, float top_p,
                                 ssm_f32 *work_buf, uint32_t *rng,
                                 float *max_prob_out) {
    // Copy logits
    _dual_memcpy_f32(work_buf, raw_logits, n);

    int token;
    if (temperature <= 0.0f) {
        // Greedy — confidence = 1.0 conceptually
        token = _dual_argmax(work_buf, n);
        if (max_prob_out) *max_prob_out = 1.0f;
        return token;
    }

    // Scale by 1/temperature
    float inv_t = 1.0f / temperature;
    for (int i = 0; i < n; i++) work_buf[i] *= inv_t;

    // Softmax
    _dual_softmax(work_buf, n);

    // Max prob = confidence
    if (max_prob_out) {
        float mp = 0.0f;
        for (int i = 0; i < n; i++) if (work_buf[i] > mp) mp = work_buf[i];
        *max_prob_out = mp;
    }

    // Sample
    token = _dual_sample_topp(work_buf, n, top_p, rng);
    return token;
}

// ============================================================
// soma_dual_init
// ============================================================
void soma_dual_init(SomaDualCtx *ctx, ssm_f32 *work_buf, int vocab_size) {
    if (!ctx) return;
    // Zero stats
    ctx->stats.total_tokens     = 0;
    ctx->stats.solar_chosen     = 0;
    ctx->stats.lunar_chosen     = 0;
    ctx->stats.agreements       = 0;
    ctx->stats.disagreements    = 0;
    ctx->stats.avg_confidence   = 0.0f;
    ctx->stats.avg_confidence_acc = 0.0f;
    ctx->work_buf  = work_buf;
    ctx->vocab_size = vocab_size;
    ctx->ready      = (work_buf != NULL && vocab_size > 0) ? 1 : 0;
}

// ============================================================
// soma_dual_confidence — compute max(softmax(logits))
// Does NOT modify the logit buffer.
// ============================================================
float soma_dual_confidence(const ssm_f32 *logits, int vocab_size) {
    if (!logits || vocab_size <= 0) return 0.0f;

    // Find max for stability
    ssm_f32 maxv = logits[0];
    for (int i = 1; i < vocab_size; i++) if (logits[i] > maxv) maxv = logits[i];

    // Compute sum of exp without allocating — we just need max_prob
    // max_prob = exp(max - max) / sum = 1.0 / sum_of_all_exp
    ssm_f32 sum = 0.0f;
    for (int i = 0; i < vocab_size; i++) sum += _dual_expf(logits[i] - maxv);
    if (sum <= 0.0f) return 0.0f;

    // The argmax token has exp(0) = 1.0
    return 1.0f / sum;
}

// ============================================================
// soma_dual_sample
// ============================================================
SomaDualResult soma_dual_sample(SomaDualCtx *ctx,
                                const ssm_f32 *raw_logits,
                                int vocab_size,
                                const SomaDNA *dna,
                                uint32_t *rng) {
    SomaDualResult res;
    res.solar_token    = 0;
    res.lunar_token    = 0;
    res.selected_token = 0;
    res.core_used      = SOMA_CORE_SOLAR;
    res.confidence     = 0.0f;
    res.solar_prob     = 0.0f;
    res.lunar_prob     = 0.0f;

    if (!ctx || !ctx->ready || !raw_logits || !dna || !rng) return res;

    float temp_solar = dna->temperature_solar;
    float temp_lunar = dna->temperature_lunar;
    float top_p_solar = dna->top_p_solar;
    float top_p_lunar = dna->top_p_lunar;
    float bias = dna->cognition_bias;
    float conf_threshold = dna->confidence_threshold;

    // ── Pass 1: compute confidence from raw logits ──────────────────
    res.confidence = soma_dual_confidence(raw_logits, vocab_size);

    // ── Pass 2: Solar sampling ──────────────────────────────────────
    // Use work_buf for Solar pass
    res.solar_token = _dual_tempered_sample(
        raw_logits, vocab_size,
        temp_solar, top_p_solar,
        ctx->work_buf, rng,
        &res.solar_prob);

    // ── Pass 3: Lunar sampling ──────────────────────────────────────
    // Reuse work_buf for Lunar pass (Solar result already saved)
    uint32_t lunar_rng = *rng ^ 0xA5A5A5A5u;  // Divergent RNG for Lunar
    res.lunar_token = _dual_tempered_sample(
        raw_logits, vocab_size,
        temp_lunar, top_p_lunar,
        ctx->work_buf, &lunar_rng,
        &res.lunar_prob);
    // Don't update main rng with lunar divergence — keep Solar's rng state

    // ── Fusion: select token ────────────────────────────────────────
    if (res.solar_token == res.lunar_token) {
        // Both cores agree → high certainty, use either
        res.selected_token = res.solar_token;
        res.core_used = SOMA_CORE_SOLAR;
        ctx->stats.agreements++;
    } else {
        ctx->stats.disagreements++;
        // Decide based on confidence + cognition_bias
        if (res.confidence >= conf_threshold) {
            // Model is confident → trust Solar (logic wins)
            res.selected_token = res.solar_token;
            res.core_used = SOMA_CORE_SOLAR;
            ctx->stats.solar_chosen++;
        } else if (bias > 0.7f) {
            // DNA leans creative → Lunar
            res.selected_token = res.lunar_token;
            res.core_used = SOMA_CORE_LUNAR;
            ctx->stats.lunar_chosen++;
        } else if (bias < 0.3f) {
            // DNA leans logical → Solar
            res.selected_token = res.solar_token;
            res.core_used = SOMA_CORE_SOLAR;
            ctx->stats.solar_chosen++;
        } else {
            // Balanced DNA: pick by confidence
            // High confidence → Solar; low confidence → Lunar
            float threshold = conf_threshold * (1.0f - bias);
            if (res.confidence >= threshold) {
                res.selected_token = res.solar_token;
                res.core_used = SOMA_CORE_SOLAR;
                ctx->stats.solar_chosen++;
            } else {
                res.selected_token = res.lunar_token;
                res.core_used = SOMA_CORE_LUNAR;
                ctx->stats.lunar_chosen++;
            }
        }
    }

    // ── Update stats ────────────────────────────────────────────────
    ctx->stats.total_tokens++;
    ctx->stats.avg_confidence_acc += res.confidence;
    if (ctx->stats.total_tokens > 0) {
        ctx->stats.avg_confidence =
            ctx->stats.avg_confidence_acc / (float)ctx->stats.total_tokens;
    }

    return res;
}

// ============================================================
// soma_dual_print_stats
// ============================================================
void soma_dual_print_stats(const SomaDualCtx *ctx, SomaPrintFn2 fn) {
    if (!ctx || !fn) return;

    fn("[SomaMind Dual Core Stats]\n");

    // Inline integer formatting
    #define _FMT(buf, v) do { \
        int _v = (v); char _t[12]; int _n = 0; \
        if (_v == 0) { _t[_n++] = '0'; } \
        else { int _a = _v < 0 ? -_v : _v; \
               if (_v < 0) _t[_n++] = '-'; \
               char _d[10]; int _dn = 0; \
               while (_a > 0) { _d[_dn++] = '0' + (_a % 10); _a /= 10; } \
               for (int _i = _dn - 1; _i >= 0; _i--) _t[_n++] = _d[_i]; } \
        _t[_n] = 0; \
        for (int _i = 0; _t[_i]; _i++) buf[_i] = _t[_i]; buf[_n] = 0; \
    } while(0)

    char b[16];
    char line[128];
    int p;

    // Line 1: tokens solar lunar agree
    p = 0;
    const char *l1 = "  tokens="; while (*l1) line[p++] = *l1++;
    _FMT(b, ctx->stats.total_tokens);
    for (int i = 0; b[i]; i++) line[p++] = b[i];
    const char *l2 = " solar="; while (*l2) line[p++] = *l2++;
    _FMT(b, ctx->stats.solar_chosen + ctx->stats.agreements);
    for (int i = 0; b[i]; i++) line[p++] = b[i];
    const char *l3 = " lunar="; while (*l3) line[p++] = *l3++;
    _FMT(b, ctx->stats.lunar_chosen);
    for (int i = 0; b[i]; i++) line[p++] = b[i];
    const char *l4 = " agree="; while (*l4) line[p++] = *l4++;
    _FMT(b, ctx->stats.agreements);
    for (int i = 0; b[i]; i++) line[p++] = b[i];
    line[p++] = '\n'; line[p] = 0;
    fn(line);

    // Line 2: avg confidence as %
    p = 0;
    const char *l5 = "  avg_confidence="; while (*l5) line[p++] = *l5++;
    int cf = (int)(ctx->stats.avg_confidence * 100.0f);
    _FMT(b, cf);
    for (int i = 0; b[i]; i++) line[p++] = b[i];
    line[p++] = '%'; line[p++] = '\n'; line[p] = 0;
    fn(line);

    #undef _FMT
}
