// oosi_infer.c — OOSI v2 inference with adaptive halting — bare-metal freestanding
//
// Uses OOSI v2 quantized x_proj/dt_proj (int8) combined with float32
// MAMB weights (in_proj, A_log, D, out_proj, embed, lm_head).
// Runs HaltingHead (float32 MLP) after each token to decide halt.

#include "oosi_infer.h"
#include "mamba_block.h"

// ============================================================
// Math primitives (no libm — same approach as mamba_block.c)
// ============================================================

static ssm_f32 oosi_expf(ssm_f32 x) {
    if (x < -20.0f) return 0.0f;
    if (x >  20.0f) return 485165195.0f;
    int n = (int)(x * 1.4426950f);
    ssm_f32 r = x - (ssm_f32)n * 0.6931472f;
    ssm_f32 e = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f + r * 0.04166667f)));
    union { ssm_f32 f; uint32_t i; } u;
    u.f = e;
    u.i += (uint32_t)((n + 127) - 127) << 23;
    return u.f;
}

// GELU approximation: 0.5*x*(1 + tanh(sqrt(2/pi)*(x + 0.044715*x^3)))
static ssm_f32 oosi_gelu(ssm_f32 x) {
    // Fast approximation via sigmoid: gelu(x) ≈ x * sigmoid(1.702 * x)
    ssm_f32 s = 1.0f / (1.0f + oosi_expf(-1.702f * x));
    return x * s;
}

// Sigmoid
static ssm_f32 oosi_sigmoid(ssm_f32 x) {
    if (x >= 0.0f) {
        ssm_f32 e = oosi_expf(-x);
        return 1.0f / (1.0f + e);
    } else {
        ssm_f32 e = oosi_expf(x);
        return e / (1.0f + e);
    }
}

// LCG RNG
static uint32_t oosi_rand(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static ssm_f32 oosi_rand_f32(uint32_t *state) {
    uint32_t r = oosi_rand(state);
    union { uint32_t i; ssm_f32 f; } u;
    u.i = 0x3F800000 | (r >> 9);
    return u.f - 1.0f;
}

// ============================================================
// oosi_halt_head_parse — parse HaltingHead from halt_data blob
//
// Python sorted(state_dict.keys()) = [
//   'net.0.bias',   'net.0.weight',   net.2.bias, net.2.weight,
//   'net.4.bias',   'net.4.weight'
// ]
// All tensors stored as float32 row-major.
// ============================================================
SsmStatus oosi_halt_head_parse(OosiHaltHead *head, const OosiWeights *w) {
    if (!head || !w || !w->halt_data || w->halt_bytes == 0)
        return SSM_ERR_BADWEIGHT;

    int d   = w->halt_d_input;  // 2561
    const ssm_f32 *p = w->halt_data;
    const uint8_t *base  = (const uint8_t *)w->halt_data;
    uint32_t remaining = w->halt_bytes;

    // Helper: advance and check bounds
#define HALT_ADVANCE(n_elem) do {                         \
    if ((n_elem) * sizeof(ssm_f32) > remaining) return SSM_ERR_BADWEIGHT; \
    remaining -= (uint32_t)((n_elem) * sizeof(ssm_f32));        \
} while(0)

    // net.0.bias [512]
    head->l0_bias = p;
    HALT_ADVANCE(512);
    p += 512;

    // net.0.weight [512 * d_input]
    head->l0_weight = p;
    HALT_ADVANCE((uint32_t)(512 * d));
    p += 512 * d;

    // net.2.bias [64]
    head->l2_bias = p;
    HALT_ADVANCE(64);
    p += 64;

    // net.2.weight [64 * 512]
    head->l2_weight = p;
    HALT_ADVANCE(64 * 512);
    p += 64 * 512;

    // net.4.bias [1]
    head->l4_bias = p;
    HALT_ADVANCE(1);
    p += 1;

    // net.4.weight [1 * 64]
    head->l4_weight = p;
    HALT_ADVANCE(64);
    p += 64;

#undef HALT_ADVANCE

    head->d_input = d;
    (void)base;
    return SSM_OK;
}

// ============================================================
// oosi_halting_head_forward — run the MLP, return halt probability
//
// Input:  hidden[d_model] + position (scalar)
// Output: P in [0, 1]
//
// Architecture: Linear→GELU→Linear→GELU→Linear→Sigmoid
// ============================================================
float oosi_halting_head_forward(
    const OosiHaltHead *head,
    const ssm_f32      *hidden,
    int                 position,
    ssm_f32            *h1_buf,   // [512]
    ssm_f32            *h2_buf,   // [64]
    ssm_f32            *inp_buf   // [d_input = d_model+1]
) {
    int d = head->d_input;  // e.g. 2561

    // Build input: [hidden[0..d_model-1], (float)position]
    for (int i = 0; i < d - 1; i++) inp_buf[i] = hidden[i];
    inp_buf[d - 1] = (ssm_f32)position;

    // Layer 0: h1 = GELU(W0 * inp + b0),  W0:[512, d], b0:[512]
    for (int i = 0; i < 512; i++) {
        ssm_f32 acc = head->l0_bias[i];
        const ssm_f32 *row = head->l0_weight + (uint32_t)i * d;
        for (int j = 0; j < d; j++) acc += row[j] * inp_buf[j];
        h1_buf[i] = oosi_gelu(acc);
    }

    // Layer 2: h2 = GELU(W2 * h1 + b2),  W2:[64, 512], b2:[64]
    for (int i = 0; i < 64; i++) {
        ssm_f32 acc = head->l2_bias[i];
        const ssm_f32 *row = head->l2_weight + i * 512;
        for (int j = 0; j < 512; j++) acc += row[j] * h1_buf[j];
        h2_buf[i] = oosi_gelu(acc);
    }

    // Layer 4: logit = W4 * h2 + b4,  W4:[1, 64], b4:[1]
    ssm_f32 logit = head->l4_bias[0];
    for (int j = 0; j < 64; j++) logit += head->l4_weight[j] * h2_buf[j];

    return (float)oosi_sigmoid(logit);
}

// ============================================================
// oosi_gen_ctx_init
// ============================================================
SsmStatus oosi_gen_ctx_init(
    OosiGenCtx         *ctx,
    const OosiWeights  *oosi,
    const MambaWeights *mamb,
    ssm_f32 *x_buf,
    ssm_f32 *x_out_buf,
    ssm_f32 *scratch,
    ssm_f32 *logits,
    ssm_f32 *halt_buf,
    ssm_f32 *halt_h1,
    ssm_f32 *halt_h2,
    float halt_threshold,
    float temperature,
    float top_p,
    uint32_t seed,
    int max_tokens)
{
    if (!ctx || !oosi) return SSM_ERR_NOMEM;
    // mamb may be NULL in OOSI-only mode (int8 projections only, no full MAMB binary)
    if (!x_buf || !x_out_buf || !scratch || !logits) return SSM_ERR_NOMEM;
    if (!halt_buf || !halt_h1 || !halt_h2) return SSM_ERR_NOMEM;

    ctx->oosi          = oosi;
    ctx->mamb          = mamb;
    ctx->x_buf         = x_buf;
    ctx->x_out_buf     = x_out_buf;
    ctx->scratch       = scratch;
    ctx->logits        = logits;
    ctx->halt_buf      = halt_buf;
    ctx->halt_h1       = halt_h1;
    ctx->halt_h2       = halt_h2;
    ctx->halt_threshold = (halt_threshold > 0.0f) ? halt_threshold : 0.7f;
    ctx->temperature   = temperature;
    ctx->top_p         = top_p;
    ctx->rng_state     = seed ^ 0xDEADBEEFu;
    ctx->max_tokens    = max_tokens;
    ctx->tokens_generated = 0;

    // Parse HaltingHead
    SsmStatus s = oosi_halt_head_parse(&ctx->halt_head, oosi);
    if (s != SSM_OK) return s;

    oosi_gen_ctx_reset(ctx);
    return SSM_OK;
}

// ============================================================
// oosi_gen_ctx_reset — zero recurrent state
// ============================================================
void oosi_gen_ctx_reset(OosiGenCtx *ctx) {
    // Use OOSI header for layer count when MAMB is not loaded
    int n = ctx->mamb ? ctx->mamb->n_layers_actual : (int)ctx->oosi->header.n_layer;
    if (n > SSM_MAX_LAYERS) n = SSM_MAX_LAYERS;

    int d_inner = ctx->mamb ? ctx->mamb->layers[0].d_inner : (int)(ctx->oosi->header.d_model * ctx->oosi->header.expand);
    int d_state = ctx->mamb ? ctx->mamb->layers[0].d_state  : (int)ctx->oosi->header.d_state;
    int d_conv  = ctx->mamb ? ctx->mamb->layers[0].d_conv   : (int)ctx->oosi->header.d_conv;

    for (int l = 0; l < n; l++) {
        MambaLayerState *s = &ctx->state.layers[l];
        int nh, nc;
        if (ctx->mamb) {
            const MambaLayerWeights *w = &ctx->mamb->layers[l];
            nh = w->d_inner * w->d_state;
            nc = w->d_inner * w->d_conv;
        } else {
            nh = d_inner * d_state;
            nc = d_inner * d_conv;
        }
        if (nh > SSM_MAX_D_INNER * 16) nh = SSM_MAX_D_INNER * 16;
        if (nc > SSM_MAX_D_INNER * 4)  nc = SSM_MAX_D_INNER * 4;
        for (int i = 0; i < nh; i++) s->h[i] = 0.0f;
        for (int i = 0; i < nc; i++) s->conv_buf[i] = 0.0f;
        s->conv_pos = 0;
    }
    ctx->state.n_layers = n;
    ctx->tokens_generated = 0;
}

// ============================================================
// Sampling helpers (same as ssm_infer.c)
// ============================================================
static int oosi_sample_greedy(const ssm_f32 *logits, int n) {
    int best = 0;
    for (int i = 1; i < n; i++)
        if (logits[i] > logits[best]) best = i;
    return best;
}

static int oosi_sample_topp(ssm_f32 *probs, int n, float top_p, uint32_t *rng) {
    float threshold = 1.0f - top_p;
    float r = oosi_rand_f32(rng) * top_p;
    int best = 0;
    for (int i = 1; i < n; i++) if (probs[i] > probs[best]) best = i;
    float cumsum = 0.0f;
    for (int i = 0; i < n; i++) {
        if (probs[i] < threshold) continue;
        cumsum += probs[i];
        if (r <= cumsum) return i;
    }
    return best;
}

static void oosi_softmax(ssm_f32 *x, int n) {
    ssm_f32 max_val = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max_val) max_val = x[i];
    ssm_f32 sum = 0.0f;
    for (int i = 0; i < n; i++) {
        ssm_f32 v = x[i] - max_val;
        ssm_f32 e = (v < -20.0f) ? 0.0f : oosi_expf(v);
        x[i] = e;
        sum += e;
    }
    if (sum > 0.0f) {
        ssm_f32 inv = 1.0f / sum;
        for (int i = 0; i < n; i++) x[i] *= inv;
    }
}

// ============================================================
// oosi_forward_one — single token forward pass with adaptive halting
//
// Strategy: use MAMB float32 weights for all layers EXCEPT x_proj/dt_proj,
// which are replaced by oosi_dequant_matvec() calls using OOSI v2 int8 data.
// After the final layer, run HaltingHead on the hidden state.
// ============================================================
OosiHaltResult oosi_forward_one(OosiGenCtx *ctx, int token_id) {
    const MambaWeights *w = ctx->mamb;
    const OosiWeights  *q = ctx->oosi;
    int d_model = w->d_model;

    // 1. Token embedding lookup (float32 from MAMB)
    const ssm_f32 *emb = w->embed + (uint32_t)token_id * d_model;
    for (int i = 0; i < d_model; i++) ctx->x_buf[i] = emb[i];

    // 2. Run through each Mamba layer
    //    For layers that have OOSI int8 x_proj/dt_proj, we patch the
    //    forward pass to use quantized matmul instead of float32.
    for (int l = 0; l < w->n_layers_actual && l < q->n_layer; l++) {
        const MambaLayerWeights    *lw = &w->layers[l];
        MambaLayerState            *ls = &ctx->state.layers[l];
        const OosiLayerWeightsQ8   *qw = &q->layers[l];

        int d_inner = lw->d_inner;
        int d_state = lw->d_state;
        int d_conv  = lw->d_conv;
        int dt_rank = lw->dt_rank;

        // Scratch layout: [d_model | 2*d_inner | d_inner | dt_rank+2*d_state | d_inner]
        ssm_f32 *x_norm   = ctx->scratch;
        ssm_f32 *x_and_z  = x_norm   + d_model;
        ssm_f32 *x_conv   = x_and_z  + 2 * d_inner;
        ssm_f32 *xBCdt    = x_conv   + d_inner;
        ssm_f32 *dt_full  = xBCdt    + (dt_rank + 2 * d_state);

        // a. RMSNorm
        mamba_rmsnorm(ctx->x_buf, lw->norm_weight, x_norm, d_model, 1e-5f);

        // b. in_proj: float32
        mamba_matmul(lw->in_proj, x_norm, x_and_z, 2 * d_inner, d_model);
        const ssm_f32 *z_gate   = x_and_z;
        const ssm_f32 *x_expand = x_and_z + d_inner;

        // c. Depthwise conv1d
        mamba_conv1d_step(
            lw->conv_weight, lw->conv_bias,
            ls->conv_buf, &ls->conv_pos,
            x_expand, x_conv, d_inner, d_conv);

        // d. SiLU
        for (int i = 0; i < d_inner; i++) x_conv[i] = mamba_silu(x_conv[i]);

        // e. x_proj: INT8 (OOSI v2) — replaces float32 matmul
        oosi_dequant_matvec(
            qw->x_proj_q8, qw->x_proj_scale,
            x_conv, xBCdt,
            qw->x_out_rows, d_inner);

        const ssm_f32 *dt_raw = xBCdt;
        const ssm_f32 *B_vec  = xBCdt + dt_rank;
        const ssm_f32 *C_vec  = xBCdt + dt_rank + d_state;

        // f. dt_proj: INT8 (OOSI v2) — replaces float32 matmul
        oosi_dequant_matvec(
            qw->dt_proj_q8, qw->dt_proj_scale,
            dt_raw, dt_full,
            d_inner, dt_rank);

        // g. Add dt_proj_bias + softplus (bias kept float32 in OOSI)
        for (int i = 0; i < d_inner; i++) {
            dt_full[i] += qw->dt_proj_bias[i];
            dt_full[i] = mamba_softplus(dt_full[i]);
        }

        // h. Selective SSM step (float32)
        ssm_f32 *y_ssm = x_and_z + d_inner;
        for (int i = 0; i < d_inner; i++) {
            ssm_f32 dt_i = dt_full[i];
            ssm_f32 y_i  = 0.0f;
            for (int j = 0; j < d_state; j++) {
                ssm_f32 a_log = lw->A_log[i * d_state + j];
                // ZOH: dA = exp(dt * (-exp(A_log)))  — same as mamba_block.c
                ssm_f32 dA = oosi_expf(dt_i * (-oosi_expf(a_log)));
                ssm_f32 dB = dt_i * B_vec[j];
                ssm_f32 *hij = &ls->h[i * d_state + j];
                *hij = dA * (*hij) + dB * x_conv[i];
                y_i += C_vec[j] * (*hij);
            }
            y_ssm[i] = y_i + lw->D[i] * x_conv[i];
        }

        // i. SiLU gate
        for (int i = 0; i < d_inner; i++) y_ssm[i] *= mamba_silu(z_gate[i]);

        // j. out_proj: float32
        mamba_matmul(lw->out_proj, y_ssm, ctx->x_out_buf, d_model, d_inner);

        // k. Residual
        for (int i = 0; i < d_model; i++) ctx->x_out_buf[i] += ctx->x_buf[i];

        // Swap buffers
        ssm_f32 *tmp = ctx->x_buf;
        ctx->x_buf = ctx->x_out_buf;
        ctx->x_out_buf = tmp;
    }

    // 3. Final RMSNorm
    mamba_rmsnorm(ctx->x_buf, w->final_norm, ctx->x_out_buf, d_model, 1e-5f);

    // 4. LM head → logits
    mamba_matmul(w->lm_head, ctx->x_out_buf, ctx->logits, w->vocab_size, d_model);

    // 5. Sample next token
    int next_token;
    if (ctx->temperature <= 0.0f) {
        next_token = oosi_sample_greedy(ctx->logits, w->vocab_size);
    } else {
        // Temperature scale
        ssm_f32 inv_temp = 1.0f / ctx->temperature;
        for (int i = 0; i < w->vocab_size; i++) ctx->logits[i] *= inv_temp;
        oosi_softmax(ctx->logits, w->vocab_size);
        next_token = oosi_sample_topp(ctx->logits, w->vocab_size,
                                      ctx->top_p, &ctx->rng_state);
    }

    // 6. HaltingHead — run on final hidden state (x_out_buf = normed state)
    int pos = ctx->tokens_generated;
    float halt_p = oosi_halting_head_forward(
        &ctx->halt_head,
        ctx->x_out_buf,  // normed hidden state [d_model]
        pos,
        ctx->halt_h1, ctx->halt_h2, ctx->halt_buf);

    ctx->tokens_generated++;

    OosiHaltResult result;
    result.token     = next_token;
    result.halt_prob = halt_p;
    result.halted    = (halt_p >= ctx->halt_threshold) ? 1 : 0;
    result.loop      = pos;
    return result;
}

// ============================================================
// oosi_generate — adaptive halting loop
// ============================================================
int oosi_generate(
    OosiGenCtx   *ctx,
    const int    *prompt_tokens,
    int           prompt_len,
    OosiTokenCb   output_cb,
    void         *userdata)
{
    oosi_gen_ctx_reset(ctx);

    // Feed prompt tokens (no output callback — just build state)
    for (int i = 0; i < prompt_len; i++) {
        oosi_forward_one(ctx, prompt_tokens[i]);
        ctx->tokens_generated--;  // don't count prompt toward budget
    }
    ctx->tokens_generated = 0;

    // Seed the generation loop with the last prompt token
    int last_tok = (prompt_len > 0) ? prompt_tokens[prompt_len - 1] : 1;  // 1 = BOS fallback

    // Generate with adaptive halting
    int generated = 0;
    while (generated < ctx->max_tokens) {
        OosiHaltResult r = oosi_forward_one(ctx, last_tok);
        generated++;

        if (output_cb) output_cb(r.token, &r, userdata);

        // Feed generated token back as input for next step
        last_tok = r.token;

        if (r.halted) break;
    }

    return generated;
}
