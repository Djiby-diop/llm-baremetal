// llmk_oo_infer.c — OO Inference Bridge implementation
//
// Connects OOSI v2 SSM inference engine to the OO entity scheduler.
// Freestanding C11 — no libc, no UEFI headers required here.
//
// Roles:
//   - Holds the global OosiGenCtx singleton (g_oosi_ctx)
//   - Wraps BPE tokenizer state (global, loaded at init)
//   - Exposes llmk_oo_infer_think() for use in llmk_oo.c entity steps
//   - Decodes output tokens to ASCII, stores in OoThinkResult.text

#include "llmk_oo_infer.h"
#include "../engine/ssm/bpe_tokenizer.h"
#include "../engine/ssm/oosi_v3_loader.h"
#include "../engine/ssm/oosi_v3_infer.h"

// Freestanding NULL
#ifndef NULL
#define NULL ((void*)0)
#endif

// ============================================================
// Global state — one generation context for the entire OO system
// ============================================================

OosiGenCtx g_oosi_ctx;
int        g_oosi_ready = 0;

// ── OOSI v3 global context ──────────────────────────────────────────────────
static OosiV3GenCtx g_v3_ctx;
static int          g_v3_ready = 0;  // 1 = v3 initialized, use v3 path

// BPE tokenizer (loaded from tokenizer.bin during boot)
static BpeTokenizer  g_bpe;
static int           g_bpe_ready = 0;

// Token buffers for BPE (no heap — static arena)
// Mamba-2.8B uses GPT-NeoX tokenizer: 50280 vocabulary entries
// Each BpeVocabEntry = 256 + 4 + 4 = 264 bytes → 50280 * 264 = ~13 MB
// Too large for stack, caller must provide or use cold zone memory.
// We keep a pointer here; init function receives the pre-allocated buffer.
static BpeVocabEntry *g_bpe_vocab = NULL;

// ============================================================
// Token generation callback (accumulates into OoThinkResult)
// ============================================================

typedef struct {
    OoThinkResult  *out;
    const BpeTokenizer *tok;
    int             text_pos;  // current write position in out->text
    int             final_loop_set;  // whether we've updated final_halt_prob
} _ThinkCallbackCtx;

static void _think_cb(int token_id, const OosiHaltResult *result, void *userdata) {
    _ThinkCallbackCtx *cb = (_ThinkCallbackCtx *)userdata;
    OoThinkResult *r = cb->out;

    // Decode token to text
    char tbuf[64];
    int n = 0;
    if (cb->tok && g_bpe_ready) {
        n = bpe_decode_token(cb->tok, token_id, tbuf, sizeof(tbuf));
    } else {
        // Byte-level fallback: just emit printable ASCII
        if (token_id >= 32 && token_id < 127) {
            tbuf[0] = (char)token_id;
            tbuf[1] = '\0';
            n = 1;
        } else {
            n = 0;
        }
    }

    // Append to out->text (leave 1 byte for null terminator)
    for (int i = 0; i < n && cb->text_pos < (int)sizeof(r->text) - 1; i++) {
        r->text[cb->text_pos++] = tbuf[i];
    }
    r->text[cb->text_pos] = '\0';

    // Update stats
    r->tokens_generated++;
    r->loops_taken = result->loop + 1;
    r->final_halt_prob = result->halt_prob;
    if (result->halted) {
        r->halted_naturally = 1;
    }
}

// ============================================================
// llmk_oo_infer_init
// ============================================================

SsmStatus llmk_oo_infer_init(
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
    int max_tokens
) {
    if (!oosi) return SSM_ERR_BADCONFIG;
    // mamb may be NULL when using OOSI-only mode (int8 projections only)

    SsmStatus s = oosi_gen_ctx_init(
        &g_oosi_ctx,
        oosi, mamb,
        x_buf, x_out_buf, scratch, logits, halt_buf, halt_h1, halt_h2,
        halt_threshold, temperature, top_p, seed, max_tokens
    );
    if (s != SSM_OK) return s;

    g_oosi_ready = 1;
    return SSM_OK;
}

// ============================================================
// llmk_oo_infer_init_v3 — OOSI v3 self-contained format
// ============================================================

SsmStatus llmk_oo_infer_init_v3(
    const OosiV3Weights *w,
    ssm_f32 *scratch,
    ssm_f32 *logits,
    ssm_f32 *h_state,
    ssm_f32 *conv_buf,
    int     *conv_pos,
    ssm_f32 *halt_h1,
    ssm_f32 *halt_h2,
    ssm_f32 *halt_out,
    float    halt_threshold,
    float    temperature,
    float    top_p,
    uint32_t seed,
    int      max_tokens
) {
    if (!w) return SSM_ERR_BADCONFIG;

    SsmStatus s = oosi_v3_gen_ctx_init(
        &g_v3_ctx, w,
        scratch, logits, h_state, conv_buf, conv_pos,
        halt_h1, halt_h2, halt_out,
        halt_threshold, temperature, top_p, seed, max_tokens
    );
    if (s != SSM_OK) return s;

    g_v3_ready   = 1;
    g_oosi_ready = 1;  // unified ready flag for llmk_oo_infer_is_ready()
    return SSM_OK;
}

// ============================================================
// llmk_oo_infer_tokenizer_init
//
// Call after llmk_oo_infer_init, with the raw tokenizer.bin data.
// vocab_buf: caller-allocated buffer of size [vocab_buf_size] BpeVocabEntry
// ============================================================

SsmStatus llmk_oo_infer_tokenizer_init(
    BpeVocabEntry *vocab_buf,
    int            vocab_buf_size,
    const void    *tokenizer_bin,
    uint64_t       tokenizer_bin_len
) {
    if (!vocab_buf || !tokenizer_bin) return SSM_ERR_BADCONFIG;

    g_bpe_vocab = vocab_buf;
    BpeStatus bs = bpe_load(
        &g_bpe,
        vocab_buf, vocab_buf_size,
        tokenizer_bin, tokenizer_bin_len
    );
    if (bs != BPE_OK) {
        g_bpe_ready = 0;
        return SSM_ERR_BADWEIGHT;
    }

    g_bpe_ready = 1;
    return SSM_OK;
}

// ============================================================
// llmk_oo_infer_tokenize
// ============================================================

int llmk_oo_infer_tokenize(
    const char *text,
    int        *out_tokens,
    int         max_out
) {
    if (!text || !out_tokens || max_out <= 0) return -1;

    if (g_bpe_ready) {
        return bpe_encode(&g_bpe, text, /*add_bos=*/1, out_tokens, max_out);
    }

    // Fallback: byte-level encoding (token_id = ASCII code)
    int n = 0;
    // BOS token
    if (n < max_out) out_tokens[n++] = 1;
    for (int i = 0; text[i] != '\0' && n < max_out; i++) {
        out_tokens[n++] = (unsigned char)text[i];
    }
    return n;
}

// ============================================================
// llmk_oo_infer_decode_token
// ============================================================

int llmk_oo_infer_decode_token(
    int   token_id,
    char *buf,
    int   buf_cap
) {
    if (!buf || buf_cap <= 0) return 0;

    if (g_bpe_ready) {
        return bpe_decode_token(&g_bpe, token_id, buf, buf_cap);
    }

    // Fallback: printable ASCII pass-through
    if (token_id >= 32 && token_id < 127 && buf_cap >= 2) {
        buf[0] = (char)token_id;
        buf[1] = '\0';
        return 1;
    }
    buf[0] = '\0';
    return 0;
}

// ============================================================
// llmk_oo_infer_think
// ============================================================

int llmk_oo_infer_think(
    const int     *prompt_tokens,
    int            prompt_len,
    OoThinkResult *out
) {
    if (!g_oosi_ready) return -1;
    if (!prompt_tokens || prompt_len <= 0 || !out) return -1;

    // Clear result
    out->tokens_generated = 0;
    out->loops_taken      = 0;
    out->final_halt_prob  = 0.0f;
    out->halted_naturally = 0;
    out->text[0]          = '\0';

    // ── OOSI v3 path ────────────────────────────────────────
    if (g_v3_ready) {
        oosi_v3_gen_ctx_reset(&g_v3_ctx);

        _ThinkCallbackCtx cb_ctx = {
            .out            = out,
            .tok            = g_bpe_ready ? &g_bpe : NULL,
            .text_pos       = 0,
            .final_loop_set = 0,
        };

        // v3 uses OosiV3HaltResult — same fields as OosiHaltResult,
        // so we wrap with a thin adapter callback
        typedef struct { _ThinkCallbackCtx *inner; } _V3AdaptCtx;
        _V3AdaptCtx adapt = { &cb_ctx };

        void v3_cb_adapter(int tid, const OosiV3HaltResult *rv, void *ud) {
            _V3AdaptCtx *a = (_V3AdaptCtx *)ud;
            // Cast v3 result to v2-compatible struct (same binary layout)
            OosiHaltResult rv2;
            rv2.halt_prob = rv->halt_prob;
            rv2.halted    = rv->halted;
            rv2.loop      = rv->loop;
            _think_cb(tid, &rv2, a->inner);
        }

        int n = oosi_v3_generate(
            &g_v3_ctx,
            prompt_tokens, prompt_len,
            v3_cb_adapter, &adapt
        );
        return n;
    }

    // ── OOSI v2 path (legacy) ────────────────────────────────
    oosi_gen_ctx_reset(&g_oosi_ctx);

    _ThinkCallbackCtx cb_ctx = {
        .out          = out,
        .tok          = g_bpe_ready ? &g_bpe : NULL,
        .text_pos     = 0,
        .final_loop_set = 0,
    };

    int n = oosi_generate(
        &g_oosi_ctx,
        prompt_tokens, prompt_len,
        _think_cb, &cb_ctx
    );

    return n;
}

// ============================================================
// Status helpers
// ============================================================

int llmk_oo_infer_is_ready(void) {
    return g_oosi_ready;
}

void llmk_oo_infer_print_status(LlmkOoInferPrintFn fn) {
    if (!fn) return;

    if (!g_oosi_ready) {
        fn("[oo_infer] NOT READY — call llmk_oo_infer_init() first\n");
        return;
    }

    // Small status string (no sprintf — freestanding)
    char buf[128];
    buf[0] = '\0';
    int pos = 0;

    // Simple manual itoa helper
    #define _APPEND_STR(s) do { \
        const char *_s = (s); \
        while (*_s && pos < (int)sizeof(buf) - 1) buf[pos++] = *_s++; \
        buf[pos] = '\0'; \
    } while(0)

    #define _APPEND_INT(v) do { \
        int _v = (v); \
        if (_v < 0) { buf[pos++] = '-'; _v = -_v; } \
        char _tmp[16]; int _ti = 0; \
        if (_v == 0) { _tmp[_ti++] = '0'; } \
        else { while (_v > 0) { _tmp[_ti++] = '0' + (_v % 10); _v /= 10; } } \
        for (int _k = _ti-1; _k >= 0 && pos < (int)sizeof(buf)-1; _k--) buf[pos++] = _tmp[_k]; \
        buf[pos] = '\0'; \
    } while(0)

    _APPEND_STR("[oo_infer] ready | bpe=");
    _APPEND_INT(g_bpe_ready);
    _APPEND_STR(" vocab=");
    _APPEND_INT(g_bpe_ready ? g_bpe.vocab_size : 0);
    _APPEND_STR(" max_tok=");
    _APPEND_INT(g_oosi_ctx.max_tokens);
    _APPEND_STR("\n");

    fn(buf);

    #undef _APPEND_STR
    #undef _APPEND_INT
}
