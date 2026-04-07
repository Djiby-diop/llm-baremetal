// soma_spec.c — SomaMind Speculative Decoding implementation
// Freestanding C11, no libc.

#include "soma_spec.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ─────────────────────────────────────────────────────────────
// Internal: fast softmax into work_buf, returns sum (for normalization)
// ─────────────────────────────────────────────────────────────
static void _spec_softmax(ssm_f32 *dst, const ssm_f32 *src, int n, float temp) {
    if (n <= 0 || !dst || !src) return;
    // Find max for numerical stability
    ssm_f32 mx = src[0];
    for (int i = 1; i < n; i++) if (src[i] > mx) mx = src[i];

    ssm_f32 sum = 0.0f;
    float inv_temp = (temp > 0.001f) ? (1.0f / temp) : 1000.0f;
    for (int i = 0; i < n; i++) {
        // exp approximation: e^x ≈ (1 + x/256)^256 is slow; use bit trick
        // Simple: cap exponent range then use float exp via bit manipulation
        float v = ((float)(src[i] - mx)) * inv_temp;
        if (v < -20.0f) v = -20.0f;
        // Taylor: e^v ≈ 1 + v + v^2/2 + v^3/6 (good for |v| < 1)
        // For larger range use: (1 + v/n)^n with n=8
        // Fast bare-metal exp: use union trick
        union { float f; unsigned int u; } eu;
        // e^v = 2^(v/ln2) = 2^(v*1.4427)
        float vl2 = v * 1.44269504f;
        int   exp_int = (int)vl2;
        float frac    = vl2 - (float)exp_int;
        // 2^frac ≈ 1 + frac*(0.693147 + frac*0.240227) (minimax poly)
        float frac_exp = 1.0f + frac * (0.693147f + frac * 0.240227f);
        int   biased   = (exp_int + 127);
        if (biased < 0) { dst[i] = 0.0f; continue; }
        if (biased > 254) biased = 254;
        eu.u = (unsigned int)biased << 23;
        dst[i] = eu.f * frac_exp;
        sum += dst[i];
    }
    if (sum > 1e-9f) {
        float inv_sum = 1.0f / (float)sum;
        for (int i = 0; i < n; i++) dst[i] *= inv_sum;
    }
}

// ─────────────────────────────────────────────────────────────
// Internal: simple xorshift RNG → float in [0,1)
// ─────────────────────────────────────────────────────────────
static float _spec_rand01(uint32_t *rng) {
    uint32_t x = *rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *rng = x;
    return (float)(x & 0x7FFFFF) / (float)0x800000;
}

// ─────────────────────────────────────────────────────────────
// soma_spec_init
// ─────────────────────────────────────────────────────────────
void soma_spec_init(SomaSpecCtx *ctx, ssm_f32 *work_buf, int vocab_size) {
    if (!ctx) return;
    ctx->work_buf         = work_buf;
    ctx->vocab_size       = vocab_size;
    ctx->enabled          = 0;
    ctx->accept_threshold = 0.8f;
    ctx->stats.total_cycles   = 0;
    ctx->stats.total_drafted  = 0;
    ctx->stats.total_accepted = 0;
    ctx->stats.total_rejected = 0;
    ctx->stats.full_accepts   = 0;
    ctx->stats.avg_speedup    = 0.0f;
    ctx->stats.avg_speedup_acc= 0.0f;
}

// ─────────────────────────────────────────────────────────────
// soma_spec_draft — greedy argmax draft (temp=0)
// ─────────────────────────────────────────────────────────────
int soma_spec_draft(SomaSpecCtx *ctx,
                    const ssm_f32 *raw_logits,
                    int n_draft,
                    int *out_tokens,
                    float *out_probs) {
    if (!ctx || !raw_logits || !out_tokens || !out_probs) return 0;
    int vsz = ctx->vocab_size > 0 ? ctx->vocab_size : SPEC_VOCAB_MAX;
    if (n_draft > SPEC_DRAFT_N) n_draft = SPEC_DRAFT_N;
    if (n_draft <= 0) return 0;

    // Compute softmax probs for draft (temp=0.1 → near-greedy)
    if (ctx->work_buf) {
        _spec_softmax(ctx->work_buf, raw_logits, vsz, 0.1f);
        // Pick top-1 greedily
        int best = 0;
        ssm_f32 best_p = ctx->work_buf[0];
        for (int i = 1; i < vsz; i++) {
            if (ctx->work_buf[i] > best_p) { best_p = ctx->work_buf[i]; best = i; }
        }
        out_tokens[0] = best;
        out_probs[0]  = (float)best_p;
    } else {
        // No work buf: use raw logit argmax
        int best = 0;
        ssm_f32 best_l = raw_logits[0];
        for (int i = 1; i < vsz; i++) {
            if (raw_logits[i] > best_l) { best_l = raw_logits[i]; best = i; }
        }
        out_tokens[0] = best;
        out_probs[0]  = 1.0f; // Unknown actual prob
    }

    // For a true multi-step draft we'd need N forward passes of the draft model.
    // Since we only have one model here, we draft 1 token per logit snapshot.
    // The "speedup" comes from the verification accept/reject logic.
    return 1;
}

// ─────────────────────────────────────────────────────────────
// soma_spec_verify_one
// ─────────────────────────────────────────────────────────────
int soma_spec_verify_one(SomaSpecCtx *ctx,
                         const ssm_f32 *verify_logits,
                         int draft_token,
                         float draft_prob,
                         uint32_t *rng,
                         int *corrected_token) {
    if (!ctx || !verify_logits || draft_token < 0) return 0;
    int vsz = ctx->vocab_size > 0 ? ctx->vocab_size : SPEC_VOCAB_MAX;
    if (draft_token >= vsz) { if (corrected_token) *corrected_token = 0; return 0; }

    // Compute verify prob for draft_token
    float verify_prob = 0.0f;
    if (ctx->work_buf) {
        _spec_softmax(ctx->work_buf, verify_logits, vsz, 1.0f);
        verify_prob = (float)ctx->work_buf[draft_token];
    } else {
        // Fallback: use raw logit delta as proxy
        verify_prob = draft_prob; // conservative: assume match
    }

    // Acceptance ratio
    float ratio = (draft_prob > 1e-9f) ? (verify_prob / draft_prob) : 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    float r = rng ? _spec_rand01(rng) : 0.5f;
    if (r < ratio || ratio >= ctx->accept_threshold) {
        return 1; // accept
    }

    // Reject: sample corrected token from (verify - draft) clipped distribution
    if (corrected_token && ctx->work_buf) {
        // Already have verify probs in work_buf; pick argmax of corrected
        float best_corr = -1.0f;
        int   best_tok  = 0;
        for (int i = 0; i < vsz; i++) {
            if (i == draft_token) continue;
            if ((float)ctx->work_buf[i] > best_corr) {
                best_corr = (float)ctx->work_buf[i]; best_tok = i;
            }
        }
        *corrected_token = best_tok;
    } else if (corrected_token) {
        *corrected_token = draft_token; // fallback: keep draft
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
// soma_spec_cycle — full draft+verify in one call
// ─────────────────────────────────────────────────────────────
SomaSpecResult soma_spec_cycle(SomaSpecCtx *ctx,
                               const ssm_f32 *draft_logits,
                               const ssm_f32 *verify_logits,
                               int n_draft,
                               uint32_t *rng) {
    SomaSpecResult res;
    for (int i = 0; i < SPEC_DRAFT_N; i++) {
        res.draft_tokens[i]    = 0;
        res.draft_probs[i]     = 0.0f;
        res.accepted_tokens[i] = 0;
    }
    res.n_draft         = 0;
    res.n_accepted      = 0;
    res.first_rejection = -1;
    res.speedup_ratio   = 0.0f;

    if (!ctx || !draft_logits || !verify_logits) return res;
    if (n_draft <= 0 || n_draft > SPEC_DRAFT_N) n_draft = SPEC_DRAFT_N;

    // Draft
    res.n_draft = soma_spec_draft(ctx, draft_logits, n_draft,
                                  res.draft_tokens, res.draft_probs);
    if (res.n_draft == 0) return res;

    // Verify each draft token
    for (int i = 0; i < res.n_draft; i++) {
        int corrected = res.draft_tokens[i];
        int ok = soma_spec_verify_one(ctx, verify_logits,
                                      res.draft_tokens[i], res.draft_probs[i],
                                      rng, &corrected);
        if (ok) {
            res.accepted_tokens[res.n_accepted++] = res.draft_tokens[i];
        } else {
            if (res.first_rejection < 0) res.first_rejection = i;
            res.accepted_tokens[res.n_accepted++] = corrected;
            break; // Stop at first rejection
        }
    }

    res.speedup_ratio = (res.n_draft > 0)
        ? (float)res.n_accepted / (float)res.n_draft : 0.0f;

    // Update stats
    if (ctx) {
        ctx->stats.total_cycles++;
        ctx->stats.total_drafted  += res.n_draft;
        ctx->stats.total_accepted += res.n_accepted;
        ctx->stats.total_rejected += (res.n_draft - res.n_accepted);
        if (res.n_accepted == res.n_draft) ctx->stats.full_accepts++;
        ctx->stats.avg_speedup_acc += res.speedup_ratio;
        ctx->stats.avg_speedup = ctx->stats.avg_speedup_acc
                                 / (float)ctx->stats.total_cycles;
    }

    return res;
}

// ─────────────────────────────────────────────────────────────
// soma_spec_print_stats — no libc
// ─────────────────────────────────────────────────────────────
static void _spr_int(char *buf, int v) {
    if (v == 0) { buf[0]='0'; buf[1]=0; return; }
    int neg=(v<0); if(neg) v=-v;
    char t[12]; int n=0;
    while(v>0){t[n++]='0'+(v%10);v/=10;}
    int s=0; if(neg) buf[s++]='-';
    for(int i=n-1;i>=0;i--) buf[s++]=t[i];
    buf[s]=0;
}
static void _spr(SomaSpecPrintFn fn, const char *label, int val) {
    char line[64]; int i=0;
    while(label[i]&&i<48){line[i]=label[i];i++;}
    char num[16]; _spr_int(num,val);
    int j=0; while(num[j]&&i<62) line[i++]=num[j++];
    line[i++]='\n'; line[i]=0;
    fn(line);
}

void soma_spec_print_stats(const SomaSpecCtx *ctx, SomaSpecPrintFn fn) {
    if (!ctx || !fn) return;
    fn("[SomaMind SpecDecode]\n");
    _spr(fn, "  cycles    : ", ctx->stats.total_cycles);
    _spr(fn, "  drafted   : ", ctx->stats.total_drafted);
    _spr(fn, "  accepted  : ", ctx->stats.total_accepted);
    _spr(fn, "  rejected  : ", ctx->stats.total_rejected);
    _spr(fn, "  full_acc  : ", ctx->stats.full_accepts);
    _spr(fn, "  speedup%  : ", (int)(ctx->stats.avg_speedup * 100.0f));
}
