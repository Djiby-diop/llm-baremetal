// SSM inference engine implementation — bare-metal Mamba
// No libc, no heap, no KV cache. Pure recurrent inference.

#include "ssm_infer.h"

// ============================================================
// Simple LCG RNG (no rand() — freestanding)
// ============================================================
static uint32_t ssm_rand(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float ssm_rand_f32(uint32_t *state) {
    uint32_t r = ssm_rand(state);
    // Map to [0, 1)
    union { uint32_t i; float f; } u;
    u.i = 0x3F800000 | (r >> 9);
    return u.f - 1.0f;
}

// ============================================================
// Softmax
// ============================================================
void ssm_softmax(ssm_f32 *x, int n) {
    // Find max for numerical stability
    ssm_f32 max_val = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max_val) max_val = x[i];

    ssm_f32 sum = 0.0f;
    for (int i = 0; i < n; i++) {
        // Reuse mamba_block's exp approximation inline
        ssm_f32 v = x[i] - max_val;
        // exp approximation: same trick as mamba_block.c
        ssm_f32 e;
        if (v < -20.0f) { e = 0.0f; }
        else if (v > 0.0f) { e = 1.0f; }
        else {
            e = 1.0f + v * (1.0f + v * (0.5f + v * (0.16666667f + v * 0.04166667f)));
        }
        x[i] = e;
        sum += e;
    }
    if (sum > 0.0f) {
        ssm_f32 inv = 1.0f / sum;
        for (int i = 0; i < n; i++) x[i] *= inv;
    }
}

// ============================================================
// Temperature scaling
// ============================================================
void ssm_temperature_scale(ssm_f32 *logits, int n, float temp) {
    if (temp <= 0.0f || temp == 1.0f) return;
    float inv = 1.0f / temp;
    for (int i = 0; i < n; i++) logits[i] *= inv;
}

// ============================================================
// Top-p (nucleus) sampling
// ============================================================
static int ssm_sample_topp(ssm_f32 *probs, int n, float top_p, uint32_t *rng) {
    // Simple top-p: find cutoff, sample from remaining
    // We don't sort (no qsort in freestanding) — use threshold approach
    float threshold = (1.0f - top_p);
    float cumsum = 0.0f;
    float r = ssm_rand_f32(rng) * top_p;

    // Find max prob token first as fallback
    int best = 0;
    for (int i = 1; i < n; i++) if (probs[i] > probs[best]) best = i;

    // Sample: skip low-prob tokens below threshold
    for (int i = 0; i < n; i++) {
        if (probs[i] < threshold) continue;
        cumsum += probs[i];
        if (r <= cumsum) return i;
    }
    return best;
}

// ============================================================
// Context init
// ============================================================
SsmStatus ssm_ctx_init(
    SsmCtx             *ctx,
    const MambaWeights *weights,
    ssm_f32            *logits_buf,
    ssm_f32            *x_buf,
    ssm_f32            *x_out_buf,
    ssm_f32            *scratch_buf,
    float               temperature,
    float               top_p,
    uint32_t            seed)
{
    if (!ctx || !weights || !logits_buf || !x_buf || !x_out_buf || !scratch_buf)
        return SSM_ERR_NOMEM;
    if (weights->n_layers_actual <= 0 || weights->d_model <= 0)
        return SSM_ERR_BADCONFIG;

    ctx->weights         = weights;
    ctx->logits          = logits_buf;
    ctx->x_buf           = x_buf;
    ctx->x_out_buf       = x_out_buf;
    ctx->scratch         = scratch_buf;
    ctx->temperature     = temperature;
    ctx->top_p           = top_p;
    ctx->rng_state       = seed ^ 0xDEADBEEFu;
    ctx->tokens_generated = 0;

    // Zero recurrent state
    ssm_ctx_reset(ctx);
    return SSM_OK;
}

void ssm_ctx_reset(SsmCtx *ctx) {
    // Zero all hidden states and conv buffers
    int n_layers = ctx->weights->n_layers_actual;
    for (int l = 0; l < n_layers && l < SSM_MAX_LAYERS; l++) {
        MambaLayerState *s = &ctx->state.layers[l];
        const MambaLayerWeights *w = &ctx->weights->layers[l];
        int nh = w->d_inner * w->d_state;
        int nc = w->d_inner * w->d_conv;
        for (int i = 0; i < nh; i++) s->h[i] = 0.0f;
        for (int i = 0; i < nc; i++) s->conv_buf[i] = 0.0f;
        s->conv_pos = 0;
    }
    ctx->state.n_layers = n_layers;
}

// ============================================================
// Single token forward pass
// ============================================================
int ssm_forward_one(SsmCtx *ctx, int token_id) {
    const MambaWeights *w = ctx->weights;
    int d_model = w->d_model;

    // 1. Token embedding lookup
    const ssm_f32 *emb = w->embed + token_id * d_model;
    for (int i = 0; i < d_model; i++) ctx->x_buf[i] = emb[i];

    // 2. Run through each Mamba layer
    for (int l = 0; l < w->n_layers_actual; l++) {
        mamba_block_forward(
            &w->layers[l],
            &ctx->state.layers[l],
            ctx->x_buf,
            ctx->x_out_buf,
            ctx->scratch
        );
        // Swap buffers
        ssm_f32 *tmp = ctx->x_buf;
        ctx->x_buf = ctx->x_out_buf;
        ctx->x_out_buf = tmp;
    }

    // 3. Final RMSNorm
    ssm_f32 *x_final = ctx->x_buf;
    mamba_rmsnorm(x_final, w->final_norm, ctx->x_out_buf, d_model, 1e-5f);

    // 4. LM head projection: logits = lm_head * x_normed
    mamba_matmul(w->lm_head, ctx->x_out_buf, ctx->logits, w->vocab_size, d_model);

    // 5. Sample next token
    ctx->tokens_generated++;
    return ssm_sample(ctx);
}

// ============================================================
// Sampling
// ============================================================
int ssm_sample(SsmCtx *ctx) {
    int n = ctx->weights->vocab_size;

    if (ctx->temperature <= 0.0f) {
        // Greedy
        int best = 0;
        for (int i = 1; i < n; i++)
            if (ctx->logits[i] > ctx->logits[best]) best = i;
        return best;
    }

    // Apply temperature
    ssm_temperature_scale(ctx->logits, n, ctx->temperature);

    // Softmax
    ssm_softmax(ctx->logits, n);

    // Top-p sample
    return ssm_sample_topp(ctx->logits, n, ctx->top_p, &ctx->rng_state);
}
