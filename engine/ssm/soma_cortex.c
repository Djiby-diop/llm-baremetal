// soma_cortex.c — SomaMind Phase J: oo-model Cortex Loader
//
// Thin orchestration layer over oosi_v3_*. The cortex runs a small OOSS
// model to produce routing metadata before the big Mamba-2.8B runs.
//
// Freestanding C11 — no libc.

#include "soma_cortex.h"
#include "oosi_v3_loader.h"
#include "oosi_v3_infer.h"

// ─── Helpers ────────────────────────────────────────────────────────────────

static void ctx_memset(void *dst, int val, int n) {
    unsigned char *p = (unsigned char *)dst;
    for (int i = 0; i < n; i++) p[i] = (unsigned char)val;
}

static void ctx_strlcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// ─── soma_cortex_init ───────────────────────────────────────────────────────

void soma_cortex_init(SomaCortexCtx *ctx) {
    if (!ctx) return;
    ctx_memset(ctx, 0, sizeof(*ctx));
}

// ─── soma_cortex_load ───────────────────────────────────────────────────────

int soma_cortex_load(SomaCortexCtx *ctx,
                     const void *buf, unsigned long long len,
                     const char *model_name) {
    if (!ctx || !buf || len == 0) return -1;

    // Parse weights (zero-copy into buf)
    SsmStatus st = oosi_v3_load(&ctx->weights, buf, len);
    if (st != SSM_OK) return -1;

    st = oosi_v3_validate(&ctx->weights);
    if (st != SSM_OK) return -1;

    // Runtime buffer dimensions
    int D  = ctx->weights.d_model;
    int Di = ctx->weights.d_inner;
    int S  = ctx->weights.d_state;
    int Dc = ctx->weights.d_conv;
    int Dt = ctx->weights.dt_rank;
    int N  = ctx->weights.n_layer;
    int V  = ctx->weights.vocab_size;
    int Hd = (int)ctx->weights.halt_d_input;

    // Buffers must be pre-allocated by caller (god file) and set on ctx
    // before calling this function. We just initialize the gen_ctx here.
    if (!ctx->scratch || !ctx->logits || !ctx->h_state ||
        !ctx->conv_buf || !ctx->conv_pos ||
        !ctx->halt_h1 || !ctx->halt_h2 || !ctx->halt_buf)
        return -1;

    // Zero-init recurrent state
    int hst_floats = N * Di * S;
    for (int i = 0; i < hst_floats; i++) ctx->h_state[i] = 0.0f;
    int conv_floats = N * Di * Dc;
    for (int i = 0; i < conv_floats; i++) ctx->conv_buf[i] = 0.0f;
    for (int i = 0; i < N; i++) ctx->conv_pos[i] = 0;

    st = oosi_v3_gen_ctx_init(
        &ctx->ctx, &ctx->weights,
        ctx->scratch, ctx->logits,
        ctx->h_state, ctx->conv_buf, ctx->conv_pos,
        ctx->halt_h1, ctx->halt_h2, ctx->halt_buf,
        0.80f,      // halt_threshold (fast path: generous)
        0.60f,      // temperature (lower = more deterministic routing)
        0.85f,      // top_p
        0xC0C0C000u,
        SOMA_CORTEX_MAX_TOKENS
    );
    if (st != SSM_OK) return -1;

    ctx->loaded = 1;
    ctx->enabled = 1;
    ctx->total_calls = 0;
    ctx->total_flagged = 0;
    ctx_strlcpy(ctx->model_name, model_name ? model_name : "?", SOMA_CORTEX_MODEL_LEN);

    (void)D; (void)Dt; (void)Hd; // suppress unused warnings
    return 0;
}

// ─── soma_cortex_run ────────────────────────────────────────────────────────

// Map token IDs → ASCII chars for prefix building (minimal, no BPE table)
static void decode_token_simple(int tok, char *out, int *len_out) {
    // For compact models (15M-130M), vocabulary is often byte-level
    // (tokens 3..258 = bytes 0..255 with offset 3).
    // We output printable ASCII only; skip control chars.
    if (tok >= 3 && tok < 259) {
        int b = tok - 3;
        if (b >= 32 && b < 127) {
            out[0] = (char)b;
            *len_out = 1;
            return;
        }
    }
    // Space token heuristic (common in BPE: token 220 in GPT-NeoX = space)
    if (tok == 220 || tok == 50118) { out[0] = ' '; *len_out = 1; return; }
    // Newline
    if (tok == 50279 || tok == 198) { out[0] = '\n'; *len_out = 1; return; }
    *len_out = 0;
}

// Safety score: use top-1 logit probability as a rough confidence proxy.
// Low confidence → model is confused → treat as low-safety.
static int compute_safety_score(const float *logits, int vocab_size) {
    if (!logits || vocab_size <= 0) return SOMA_CORTEX_SAFE_DEFAULT;
    float max_l = logits[0];
    float second = logits[0];
    for (int i = 1; i < vocab_size; i++) {
        if (logits[i] > max_l) { second = max_l; max_l = logits[i]; }
        else if (logits[i] > second) second = logits[i];
    }
    // Margin between top-1 and top-2 (in units of 0-100)
    float margin = max_l - second;
    if (margin < 0.0f) margin = 0.0f;
    if (margin > 10.0f) margin = 10.0f;
    return (int)(margin * 10.0f);  // 0..100
}

// Simple domain heuristic on first token logit peak
// Maps token-id ranges → SomaDomain.
// This works best when the cortex is trained for classification
// (oo-model's policy_safety + system_reasoning dataset families).
// Fallback is token-range heuristic on the compact BPE vocab.
static int infer_domain(int first_token, int vocab_size) {
    if (vocab_size <= 0 || first_token < 0) return 0; // UNKNOWN
    // Rough heuristic: partition vocab into 7 equal bands
    int band = (first_token * 7) / (vocab_size > 0 ? vocab_size : 1);
    if (band > 6) band = 6;
    return band;
}

SomaCortexResult soma_cortex_run(SomaCortexCtx *ctx, const char *prompt) {
    SomaCortexResult res;
    ctx_memset(&res, 0, sizeof(res));
    res.safety_score = SOMA_CORTEX_SAFE_DEFAULT;

    if (!ctx || !ctx->loaded || !ctx->enabled || !prompt) return res;

    ctx->total_calls++;

    // Reset recurrent state for fresh inference
    oosi_v3_gen_ctx_reset(&ctx->ctx);

    // Tokenize via byte encoding (no external BPE table required):
    // encode each ASCII byte as (byte + 3) to match compact model convention.
    int tok[256];
    int tok_len = 0;
    for (int i = 0; prompt[i] && tok_len < 255; i++)
        tok[tok_len++] = (unsigned char)prompt[i] + 3;
    if (tok_len == 0) { tok[0] = 3; tok_len = 1; }  // BOS

    // Feed prompt (all but last token)
    for (int i = 0; i < tok_len - 1; i++)
        oosi_v3_forward_one(&ctx->ctx, tok[i]);
    ctx->ctx.tokens_generated = 0;

    // Generate SOMA_CORTEX_MAX_TOKENS tokens
    int last = tok[tok_len - 1];
    res.n_tokens = 0;
    int prefix_pos = 0;

    for (int t = 0; t < SOMA_CORTEX_MAX_TOKENS; t++) {
        OosiV3HaltResult r = oosi_v3_forward_one(&ctx->ctx, last);

        res.token_ids[res.n_tokens++] = r.token;

        // Decode to prefix
        char cb[4]; int cl = 0;
        decode_token_simple(r.token, cb, &cl);
        for (int ci = 0; ci < cl && prefix_pos < SOMA_CORTEX_PREFIX_LEN - 1; ci++)
            res.prefix[prefix_pos++] = cb[ci];

        // On first token: derive domain + safety from logit distribution
        if (t == 0) {
            res.safety_score = compute_safety_score(
                ctx->ctx.logits, ctx->weights.vocab_size);
            res.domain = infer_domain(r.token, ctx->weights.vocab_size);
            res.domain_conf = 50 + res.safety_score / 5;
            if (res.domain_conf > 100) res.domain_conf = 100;
        }

        last = r.token;
        if (r.halted || r.token == 0) break;
    }
    res.prefix[prefix_pos] = 0;
    res.prefix_len = prefix_pos;

    // Flag safety
    if (res.safety_score < SOMA_CORTEX_SAFE_THRESHOLD) {
        res.safety_flagged = 1;
        ctx->total_flagged++;
    }

    res.valid = 1;
    return res;
}

// ─── soma_cortex_print_stats ────────────────────────────────────────────────

void soma_cortex_print_stats(const SomaCortexCtx *ctx) {
    // Stats printed by REPL handler in god file using Print(L"...").
    // This function exists for API completeness.
    (void)ctx;
}
