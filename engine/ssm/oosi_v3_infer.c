// oosi_v3_infer.c — Full standalone Mamba SSM inference (OOSI v3)
//
// Uses OOSI v3 binary for ALL weights — no MAMB float32 binary needed.
// All linear layers are int8-quantized; non-linear weights stay float32.
//
// Freestanding C11 — no libc, no UEFI headers.

#include "oosi_v3_loader.h"
#include "oosi_v3_infer.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Forward decls for helpers (defined below) ─────────────────────────────
static void   _v3_rmsnorm(const ssm_f32 *x, const ssm_f32 *w,
                          ssm_f32 *out, int d, ssm_f32 eps);
static void   _v3_matvec_q8(const ssm_q8 *q8, const ssm_f32 *scale,
                             const ssm_f32 *x, ssm_f32 *y,
                             int out_rows, int in_cols);
static ssm_f32 _v3_silu(ssm_f32 x);
static ssm_f32 _v3_softplus(ssm_f32 x);
static ssm_f32 _v3_expf(ssm_f32 x);
static void   _v3_softmax(ssm_f32 *x, int n);
static int    _v3_argmax(const ssm_f32 *x, int n);
static int    _v3_sample_topp(const ssm_f32 *probs, int n, ssm_f32 top_p,
                              uint32_t *rng);
static void   _v3_conv1d_step(const ssm_f32 *wt, const ssm_f32 *bias,
                              ssm_f32 *conv_buf, int *conv_pos,
                              const ssm_f32 *x_in, ssm_f32 *y,
                              int d_inner, int d_conv);

// ── Scratch buffer helper — caller provides one large flat buffer ──────────
// Layout (all f32 unless noted):
//   [0 ..D-1]           x_norm        (RMSNorm output)
//   [D ..D+2Di-1]       x_and_z       (in_proj output: x_expand then z_gate)
//   [D+2Di ..D+3Di-1]   x_conv        (after conv1d + SiLU)
//   [D+3Di ..D+3Di+Dt+2S-1]  xBCdt   (x_proj output)
//   [D+3Di+Dt+2S ..]    dt_full       (dt_proj output)
//   y_ssm reuses x_and_z[0..Di-1] (x_expand consumed after conv1d)
// Total: D + 3*Di + Dt + 2*S + Di = D + 4*Di + Dt + 2*S
//   = 2560 + 4*5120 + 160 + 32 = 2560 + 20480 + 192 = 23232 floats = ~91 KB

// ============================================================
// oosi_v3_gen_ctx_init
// ============================================================
SsmStatus oosi_v3_gen_ctx_init(
    OosiV3GenCtx      *ctx,
    const OosiV3Weights *w,
    ssm_f32  *scratch,       // [D + 4*d_inner + dt_rank + 2*d_state]
    ssm_f32  *logits,        // [vocab_size]
    ssm_f32  *h_state,       // [n_layer * d_inner * d_state]  SSM state
    ssm_f32  *conv_buf,      // [n_layer * d_inner * d_conv]   conv state
    int      *conv_pos,      // [n_layer]                      conv positions
    ssm_f32  *halt_h1,       // [512]  HaltingHead buffer
    ssm_f32  *halt_h2,       // [64]
    ssm_f32  *halt_buf,      // [1 + halt_d_input]
    float    halt_threshold,
    float    temperature,
    float    top_p,
    uint32_t seed,
    int      max_tokens
) {
    if (!ctx || !w) return SSM_ERR_BADCONFIG;
    if (!scratch || !logits || !h_state || !conv_buf || !conv_pos) return SSM_ERR_NOMEM;

    ctx->w              = w;
    ctx->scratch        = scratch;
    ctx->logits         = logits;
    ctx->h_state        = h_state;
    ctx->conv_buf       = conv_buf;
    ctx->conv_pos       = conv_pos;
    ctx->halt_h1        = halt_h1;
    ctx->halt_h2        = halt_h2;
    ctx->halt_buf       = halt_buf;
    ctx->halt_threshold = (halt_threshold > 0.0f) ? halt_threshold : 0.7f;
    ctx->temperature    = temperature;
    ctx->top_p          = top_p;
    ctx->repetition_penalty = 1.3f;  // default: moderate penalty
    ctx->rng_state      = seed ^ 0xDEADBEEFu;
    ctx->max_tokens     = (max_tokens > 0) ? max_tokens : 64;
    ctx->tokens_generated = 0;

    // Parse HaltingHead from v3 binary
    SsmStatus s = oosi_v3_halt_parse(&ctx->halt_head, w);
    if (s != SSM_OK) return s;

    oosi_v3_gen_ctx_reset(ctx);
    return SSM_OK;
}

// ============================================================
// oosi_v3_gen_ctx_reset
// ============================================================
void oosi_v3_gen_ctx_reset(OosiV3GenCtx *ctx) {
    if (!ctx || !ctx->w) return;
    int N  = ctx->w->n_layer;
    int Di = ctx->w->d_inner;
    int S  = ctx->w->d_state;
    int Dc = ctx->w->d_conv;

    // Zero SSM hidden state h[l][i][j]
    for (int i = 0; i < N * Di * S; i++) ctx->h_state[i] = 0.0f;
    // Zero conv ring buffers
    for (int i = 0; i < N * Di * Dc; i++) ctx->conv_buf[i] = 0.0f;
    for (int i = 0; i < N; i++) ctx->conv_pos[i] = 0;
    ctx->tokens_generated = 0;
    ctx->rep_count = 0;
}

// ============================================================
// oosi_v3_halt_parse  (analogous to oosi_halt_head_parse)
// ============================================================
SsmStatus oosi_v3_halt_parse(OosiV3HaltHead *head, const OosiV3Weights *w) {
    if (!head || !w || !w->halt_data || w->halt_bytes == 0) {
        // No HaltingHead — disable halting
        head->ready = 0;
        return SSM_OK;
    }

    int d = w->halt_d_input;
    const ssm_f32 *p = w->halt_data;
    uint32_t rem = w->halt_bytes;

#define NEED_F32(n) do { if (rem < (n)*4u) { head->ready=0; return SSM_OK; } } while(0)
#define SKIP_F32(n) do { p += (n); rem -= (n)*4u; } while(0)

    NEED_F32(d * 512 + 512);
    head->w0 = p; p += d * 512; rem -= d * 512 * 4;
    head->b0 = p; p += 512;     rem -= 512 * 4;

    NEED_F32(512 * 64 + 64);
    head->w2 = p; p += 512 * 64; rem -= 512 * 64 * 4;
    head->b2 = p; p += 64;       rem -= 64 * 4;

    NEED_F32(64 + 1);
    head->w4 = p; p += 64;  rem -= 64 * 4;
    head->b4 = p;
    head->d_input = d;
    head->ready   = 1;
    return SSM_OK;
#undef NEED_F32
#undef SKIP_F32
}

// ============================================================
// oosi_v3_halt_forward
// ============================================================
static float _gelu(float x) {
    // Approximation: x * sigmoid(1.702 * x)
    float a = 1.702f * x;
    float s = (a >= 0.0f) ? (1.0f / (1.0f + _v3_expf(-a)))
                           : (_v3_expf(a) / (1.0f + _v3_expf(a)));
    return x * s;
}

float oosi_v3_halt_forward(OosiV3HaltHead *head, const ssm_f32 *hidden,
                           ssm_f32 *h1, ssm_f32 *h2, ssm_f32 *buf,
                           int d_model) {
    if (!head || !head->ready) return 0.0f;
    int d = head->d_input;
    // Build input: [hidden[0..d_model-1] | 0.0] if d_model+1 == d
    for (int i = 0; i < d_model && i < d; i++) buf[i] = hidden[i];
    if (d > d_model) buf[d_model] = 0.0f;  // halt-position feature

    // Layer 0: h1 = GELU(w0 @ buf + b0)
    for (int i = 0; i < 512; i++) {
        float acc = head->b0[i];
        const ssm_f32 *row = head->w0 + i * d;
        for (int j = 0; j < d; j++) acc += row[j] * buf[j];
        h1[i] = _gelu(acc);
    }
    // Layer 2: h2 = GELU(w2 @ h1 + b2)
    for (int i = 0; i < 64; i++) {
        float acc = head->b2[i];
        const ssm_f32 *row = head->w2 + i * 512;
        for (int j = 0; j < 512; j++) acc += row[j] * h1[j];
        h2[i] = _gelu(acc);
    }
    // Layer 4: scalar = sigmoid(w4 @ h2 + b4)
    float out = head->b4[0];
    for (int j = 0; j < 64; j++) out += head->w4[j] * h2[j];
    // sigmoid
    return (out >= 0.0f) ? (1.0f / (1.0f + _v3_expf(-out)))
                         : (_v3_expf(out) / (1.0f + _v3_expf(out)));
}

// ============================================================
// oosi_v3_forward_one  — full Mamba block forward pass
// ============================================================
OosiV3HaltResult oosi_v3_forward_one(OosiV3GenCtx *ctx, int token_id) {
    const OosiV3Weights *w = ctx->w;
    int D  = w->d_model, N = w->n_layer, S = w->d_state;
    int Di = w->d_inner,  Dc = w->d_conv, Dt = w->dt_rank;

    // Scratch layout
    ssm_f32 *x_norm  = ctx->scratch;
    ssm_f32 *x_and_z = x_norm  + D;
    ssm_f32 *x_conv  = x_and_z + 2 * Di;
    ssm_f32 *xBCdt   = x_conv  + Di;
    ssm_f32 *dt_full = xBCdt   + (Dt + 2 * S);

    // x_cur: current residual stream (points into scratch after dt_full)
    ssm_f32 *x_cur = dt_full + Di;   // [D]
    ssm_f32 *x_out = x_cur + D;      // [D]

    // 1. Token embedding lookup (int8 dequant)
    {
        const ssm_q8  *row   = w->embed_q8    + (uint64_t)token_id * D;
        const ssm_f32  scale = w->embed_scale[token_id];
        for (int i = 0; i < D; i++)
            x_cur[i] = (ssm_f32)row[i] * (scale / 127.0f);
    }

    // 2. Mamba layers
    for (int l = 0; l < N; l++) {
        const OosiV3LayerWeights *lw = &w->layers[l];
        ssm_f32 *hs   = ctx->h_state + (uint64_t)l * Di * S;   // [Di * S]
        ssm_f32 *cbufl = ctx->conv_buf + (uint64_t)l * Di * Dc; // [Di * Dc]
        int     *cpos  = &ctx->conv_pos[l];

        // a. RMSNorm
        _v3_rmsnorm(x_cur, lw->norm_weight, x_norm, D, 1e-5f);

        // b. in_proj (int8): [2*Di × D] → x_and_z [2*Di]
        //    PyTorch layout: first Di = x (→conv→SSM), second Di = z (gate)
        _v3_matvec_q8(lw->in_proj_q8, lw->in_proj_scale,
                      x_norm, x_and_z, 2*Di, D);
        const ssm_f32 *x_expand = x_and_z;           // first Di = x
        const ssm_f32 *z_gate   = x_and_z + Di;      // second Di = z (gate)

        // c. Depthwise conv1d step
        _v3_conv1d_step(lw->conv_weight, lw->conv_bias,
                        cbufl, cpos, x_expand, x_conv, Di, Dc);

        // d. SiLU
        for (int i = 0; i < Di; i++) x_conv[i] = _v3_silu(x_conv[i]);

        // e. x_proj (int8): [(Dt+2S) × Di] → xBCdt
        _v3_matvec_q8(lw->x_proj_q8, lw->x_proj_scale,
                      x_conv, xBCdt, Dt + 2*S, Di);
        const ssm_f32 *dt_raw = xBCdt;
        const ssm_f32 *B_vec  = xBCdt + Dt;
        const ssm_f32 *C_vec  = xBCdt + Dt + S;

        // f. dt_proj (int8): [Di × Dt] → dt_full + bias + softplus
        _v3_matvec_q8(lw->dt_proj_q8, lw->dt_proj_scale,
                      dt_raw, dt_full, Di, Dt);
        for (int i = 0; i < Di; i++) {
            dt_full[i] += lw->dt_proj_bias[i];
            dt_full[i] = _v3_softplus(dt_full[i]);
        }

        // g. Selective SSM step (ZOH discretisation)
        //    y_ssm reuses x_expand slot (first half, no longer needed after conv)
        //    z_gate (second half) stays intact for gating step (h).
        ssm_f32 *y_ssm = x_and_z;  // safe: x_expand already consumed by conv1d
        for (int i = 0; i < Di; i++) {
            ssm_f32 dt_i = dt_full[i];
            ssm_f32 y_i  = 0.0f;
            for (int j = 0; j < S; j++) {
                ssm_f32 a_log = lw->A_log[i * S + j];
                ssm_f32 dA = _v3_expf(dt_i * (-_v3_expf(a_log)));
                ssm_f32 dB = dt_i * B_vec[j];
                ssm_f32 *hij = &hs[i * S + j];
                *hij = dA * (*hij) + dB * x_conv[i];
                y_i += C_vec[j] * (*hij);
            }
            y_ssm[i] = y_i + lw->D[i] * x_conv[i];
        }

        // h. SiLU gate
        for (int i = 0; i < Di; i++) y_ssm[i] *= _v3_silu(z_gate[i]);

        // i. out_proj (int8): [D × Di] → x_out
        _v3_matvec_q8(lw->out_proj_q8, lw->out_proj_scale,
                      y_ssm, x_out, D, Di);

        // j. Residual
        for (int i = 0; i < D; i++) x_cur[i] = x_out[i] + x_cur[i];
    }

    // 3. Final RMSNorm
    _v3_rmsnorm(x_cur, w->final_norm, x_out, D, 1e-5f);

    // 4. LM head (int8): [V × D] → logits
    _v3_matvec_q8(w->lm_head_q8, w->lm_head_scale,
                  x_out, ctx->logits, w->vocab_size, D);

    // Debug: save raw logits before masking/softmax
    {
        int V = w->vocab_size;
        for (int qi = 0; qi < 5 && qi < V; qi++)
            ctx->dbg_raw_logits[qi] = ctx->logits[qi];
        ssm_f32 rmin = ctx->logits[2], rmax = ctx->logits[2]; // skip 0,1 (will be masked)
        for (int qi = 3; qi < V; qi++) {
            if (ctx->logits[qi] < rmin) rmin = ctx->logits[qi];
            if (ctx->logits[qi] > rmax) rmax = ctx->logits[qi];
        }
        ctx->dbg_raw_min = rmin;
        ctx->dbg_raw_max = rmax;
    }

    // 5. Mask special tokens to prevent degenerate output
    //    GPT-NeoX vocab: 0=<|endoftext|>, 1=<|padding|>, 2..50256=BPE tokens
    //    Tokens >= 50257 are padding/unused added to align vocab size
    ctx->logits[0] = -1.0e9f;
    ctx->logits[1] = -1.0e9f;
    {
        int real_vocab = 50257;  // GPT-NeoX standard BPE vocab boundary
        if (real_vocab < w->vocab_size) {
            for (int i = real_vocab; i < w->vocab_size; i++)
                ctx->logits[i] = -1.0e9f;
        }
    }

    // 5b. Repetition penalty: penalize tokens already generated
    if (ctx->repetition_penalty > 1.0f && ctx->rep_count > 0) {
        ssm_f32 rp = ctx->repetition_penalty;
        for (int ri = 0; ri < ctx->rep_count && ri < 128; ri++) {
            int tid = ctx->rep_history[ri];
            if (tid >= 0 && tid < w->vocab_size) {
                if (ctx->logits[tid] > 0.0f)
                    ctx->logits[tid] /= rp;
                else
                    ctx->logits[tid] *= rp;
            }
        }
    }

    // 6. Sample next token
    int next_token;
    if (ctx->temperature <= 0.0f) {
        next_token = _v3_argmax(ctx->logits, w->vocab_size);
    } else {
        ssm_f32 inv_t = 1.0f / ctx->temperature;
        for (int i = 0; i < w->vocab_size; i++) ctx->logits[i] *= inv_t;
        _v3_softmax(ctx->logits, w->vocab_size);
        next_token = _v3_sample_topp(ctx->logits, w->vocab_size,
                                     ctx->top_p, &ctx->rng_state);
    }

    // Track generated token for repetition penalty
    if (ctx->rep_count < 128) {
        ctx->rep_history[ctx->rep_count++] = next_token;
    }

    // 7. HaltingHead
    int pos = ctx->tokens_generated++;
    float halt_p = oosi_v3_halt_forward(
        &ctx->halt_head, x_out,
        ctx->halt_h1, ctx->halt_h2, ctx->halt_buf, D);

    OosiV3HaltResult r;
    r.token     = next_token;
    r.halt_prob = halt_p;
    r.halted    = (halt_p >= ctx->halt_threshold) ? 1 : 0;
    r.loop      = pos;
    return r;
}

// ============================================================
// oosi_v3_generate
// ============================================================
int oosi_v3_generate(
    OosiV3GenCtx      *ctx,
    const int         *prompt_tokens,
    int                prompt_len,
    OosiV3TokenCb      output_cb,
    void              *userdata
) {
    oosi_v3_gen_ctx_reset(ctx);

    // Feed prompt (build SSM state, no output)
    for (int i = 0; i < prompt_len; i++) {
        oosi_v3_forward_one(ctx, prompt_tokens[i]);
        ctx->tokens_generated--;
    }
    ctx->tokens_generated = 0;

    int last_tok = (prompt_len > 0) ? prompt_tokens[prompt_len - 1] : 1;

    int generated = 0;
    while (generated < ctx->max_tokens) {
        OosiV3HaltResult r = oosi_v3_forward_one(ctx, last_tok);
        generated++;
        if (output_cb) output_cb(r.token, &r, userdata);
        last_tok = r.token;
        if (r.halted) break;
        if (r.token == 0) break;  // EOS
    }
    return generated;
}

// ============================================================
// Math helpers
// ============================================================

static void _v3_rmsnorm(const ssm_f32 *x, const ssm_f32 *w,
                        ssm_f32 *out, int d, ssm_f32 eps) {
    ssm_f32 ss = 0.0f;
    for (int i = 0; i < d; i++) ss += x[i] * x[i];
    ssm_f32 v = ss / (ssm_f32)d + eps;
    // Fast inverse sqrt: Quake III magic number + Newton-Raphson refinement
    uint32_t bits;
    __builtin_memcpy(&bits, &v, 4);
    bits = 0x5f3759dfU - (bits >> 1);
    ssm_f32 y;
    __builtin_memcpy(&y, &bits, 4);
    y = y * (1.5f - 0.5f * v * y * y);
    y = y * (1.5f - 0.5f * v * y * y);
    y = y * (1.5f - 0.5f * v * y * y);
    for (int i = 0; i < d; i++) out[i] = x[i] * y * w[i];
}

static void _v3_matvec_q8(const ssm_q8 *q8, const ssm_f32 *scale,
                          const ssm_f32 *x, ssm_f32 *y,
                          int out_rows, int in_cols) {
    const ssm_f32 inv127 = 1.0f / 127.0f;
    for (int i = 0; i < out_rows; i++) {
        const ssm_q8 *row = q8 + (uint64_t)i * in_cols;
        ssm_f32 acc = 0.0f;
        for (int j = 0; j < in_cols; j++)
            acc += (ssm_f32)row[j] * x[j];
        y[i] = acc * scale[i] * inv127;
    }
}

static ssm_f32 _v3_silu(ssm_f32 x) {
    // x * sigmoid(x)
    ssm_f32 s = (x >= 0.0f) ? (1.0f / (1.0f + _v3_expf(-x)))
                             : (_v3_expf(x) / (1.0f + _v3_expf(x)));
    return x * s;
}

/* Natural log: log(x) via bit manipulation + polynomial. */
static ssm_f32 _v3_logf(ssm_f32 x) {
    if (x <= 0.0f) return -88.0f;
    uint32_t bits;
    __builtin_memcpy(&bits, &x, 4);
    int e = (int)((bits >> 23) & 0xFFu) - 127;
    bits = (bits & 0x007FFFFFu) | 0x3F800000u;   /* mantissa in [1, 2) */
    ssm_f32 m;
    __builtin_memcpy(&m, &bits, 4);
    /* log(m) for m in [1,2): Horner, degree-7 minimax around ln(m) */
    ssm_f32 y = m - 1.0f;
    ssm_f32 lm = y * (1.0f - y * (0.5f - y * (0.333333f
               - y * (0.25f - y * (0.2f - y * (0.166667f - y * 0.142857f))))));
    return lm + (ssm_f32)e * 0.693147180559945f;
}

static ssm_f32 _v3_softplus(ssm_f32 x) {
    /* softplus(x) = log(1 + exp(x))
     * For x > 20  : ≈ x  (exp dominates)
     * For x < -20 : ≈ exp(x) ≈ 0  */
    if (x >  20.0f) return x;
    if (x < -20.0f) return _v3_expf(x);
    return _v3_logf(1.0f + _v3_expf(x));
}

// Fast exp approximation (freestanding, no libm)
// Range: x in [-88, 88]. Uses polynomial approximation.
static ssm_f32 _v3_expf(ssm_f32 x) {
    // Clamp to avoid overflow/underflow
    if (x >  88.0f) return 3.4e38f;
    if (x < -88.0f) return 0.0f;
    // Decompose: exp(x) = 2^k * exp(r)  where k = floor(x / ln2)
    // ln2 ≈ 0.693147
    const ssm_f32 ln2 = 0.693147180559945f;
    const ssm_f32 log2e = 1.44269504088896f;
    ssm_f32 z = x * log2e;
    int k = (int)z;
    if (z < 0.0f) k--;
    ssm_f32 r = x - (ssm_f32)k * ln2;
    // Polynomial approximation of exp(r) for r in [0, ln2]
    // Using degree-6 minimax polynomial
    ssm_f32 p = 1.0f + r * (1.0f + r * (0.5f + r * (0.166667f
              + r * (0.041667f + r * (0.008333f + r * 0.001389f)))));
    // Scale by 2^k
    int e = k + 127;
    if (e <= 0)   return 0.0f;
    if (e >= 255) return 3.4e38f;
    uint32_t bits = (uint32_t)e << 23;
    ssm_f32 scale;
    __builtin_memcpy(&scale, &bits, 4);
    return p * scale;
}

static void _v3_softmax(ssm_f32 *x, int n) {
    ssm_f32 max = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max) max = x[i];
    ssm_f32 sum = 0.0f;
    for (int i = 0; i < n; i++) { x[i] = _v3_expf(x[i] - max); sum += x[i]; }
    ssm_f32 inv = 1.0f / sum;
    for (int i = 0; i < n; i++) x[i] *= inv;
}

static int _v3_argmax(const ssm_f32 *x, int n) {
    int best = 0;
    for (int i = 1; i < n; i++) if (x[i] > x[best]) best = i;
    return best;
}

static int _v3_sample_topp(const ssm_f32 *probs, int n, ssm_f32 top_p,
                           uint32_t *rng) {
    // Find argmax first
    int best = _v3_argmax(probs, n);
    if (top_p <= 0.0f) return best;

    // Quick check: if best token has enough mass, return it with high prob
    *rng ^= *rng << 13;
    *rng ^= *rng >> 17;
    *rng ^= *rng << 5;
    ssm_f32 r = (*rng >> 8) * (1.0f / (1u << 24));

    // Sort-free top-p: gather top tokens by probability
    // First pass: find tokens with prob > threshold (prob[best] * 0.01)
    ssm_f32 thresh = probs[best] * 0.01f;
    ssm_f32 cumul = 0.0f;
    ssm_f32 target = r * top_p;

    // Scan in two passes:
    // Pass 1: high-probability tokens (> thresh)
    for (int i = 0; i < n; i++) {
        if (probs[i] >= thresh) {
            cumul += probs[i];
            if (cumul >= target) return i;
        }
    }
    // Pass 2: remaining tokens (< thresh)
    for (int i = 0; i < n; i++) {
        if (probs[i] < thresh) {
            cumul += probs[i];
            if (cumul >= target) return i;
        }
    }
    return best;
}

static void _v3_conv1d_step(const ssm_f32 *wt, const ssm_f32 *bias,
                            ssm_f32 *conv_buf, int *conv_pos,
                            const ssm_f32 *x_in, ssm_f32 *y,
                            int d_inner, int d_conv) {
    int pos = *conv_pos;
    for (int i = 0; i < d_inner; i++) {
        // Write x_in[i] into ring buffer position
        conv_buf[i * d_conv + pos] = x_in[i];
        // Convolve — PyTorch F.conv1d (cross-correlation):
        //   wt[0] * oldest, wt[d_conv-1] * newest
        //   idx iterates from newest (pos) to oldest (pos-d_conv+1)
        //   so flip weight index: wt[d_conv-1-k]
        ssm_f32 acc = bias ? bias[i] : 0.0f;
        for (int k = 0; k < d_conv; k++) {
            int idx = (pos - k + d_conv) % d_conv;
            acc += wt[i * d_conv + (d_conv - 1 - k)] * conv_buf[i * d_conv + idx];
        }
        y[i] = acc;
    }
    *conv_pos = (pos + 1) % d_conv;
}
