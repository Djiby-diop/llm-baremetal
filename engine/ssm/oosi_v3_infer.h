// oosi_v3_infer.h — OOSI v3 full standalone inference API
// Freestanding C11 — no libc, no UEFI headers.

#pragma once
#include "oosi_v3_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// HaltingHead state (pointers into v3 binary)
// ============================================================
typedef struct {
    const ssm_f32 *w0;   // [512 × d_input]
    const ssm_f32 *b0;   // [512]
    const ssm_f32 *w2;   // [64 × 512]
    const ssm_f32 *b2;   // [64]
    const ssm_f32 *w4;   // [64]
    const ssm_f32 *b4;   // [1]
    int d_input;
    int ready;
} OosiV3HaltHead;

// ============================================================
// Generation context
// ============================================================
typedef struct {
    const OosiV3Weights *w;

    // Scratch buffers (all caller-allocated)
    ssm_f32 *scratch;   // [D + 4*d_inner + dt_rank + 2*d_state + 2*D]
    ssm_f32 *logits;    // [vocab_size]
    ssm_f32 *h_state;   // [n_layer * d_inner * d_state]  recurrent state
    ssm_f32 *conv_buf;  // [n_layer * d_inner * d_conv]   conv ring buffer
    int     *conv_pos;  // [n_layer]                       conv write pos

    // HaltingHead buffers
    OosiV3HaltHead halt_head;
    ssm_f32 *halt_h1;   // [512]
    ssm_f32 *halt_h2;   // [64]
    ssm_f32 *halt_buf;  // [halt_d_input]

    // Sampling params
    float    halt_threshold;
    float    temperature;
    float    top_p;
    uint32_t rng_state;

    // Generation state
    int max_tokens;
    int tokens_generated;

    // Debug: raw logits before masking/softmax (first 5 + min/max)
    ssm_f32 dbg_raw_logits[5];
    ssm_f32 dbg_raw_min;
    ssm_f32 dbg_raw_max;
} OosiV3GenCtx;

// ============================================================
// Per-token result
// ============================================================
typedef struct {
    int   token;
    float halt_prob;
    int   halted;
    int   loop;
} OosiV3HaltResult;

typedef void (*OosiV3TokenCb)(int token_id, const OosiV3HaltResult *r, void *ud);

// ============================================================
// API
// ============================================================

SsmStatus oosi_v3_gen_ctx_init(
    OosiV3GenCtx        *ctx,
    const OosiV3Weights *w,
    ssm_f32  *scratch,
    ssm_f32  *logits,
    ssm_f32  *h_state,
    ssm_f32  *conv_buf,
    int      *conv_pos,
    ssm_f32  *halt_h1,
    ssm_f32  *halt_h2,
    ssm_f32  *halt_buf,
    float     halt_threshold,
    float     temperature,
    float     top_p,
    uint32_t  seed,
    int       max_tokens
);

void oosi_v3_gen_ctx_reset(OosiV3GenCtx *ctx);

SsmStatus oosi_v3_halt_parse(OosiV3HaltHead *head, const OosiV3Weights *w);

float oosi_v3_halt_forward(OosiV3HaltHead *head, const ssm_f32 *hidden,
                           ssm_f32 *h1, ssm_f32 *h2, ssm_f32 *buf,
                           int d_model);

OosiV3HaltResult oosi_v3_forward_one(OosiV3GenCtx *ctx, int token_id);

int oosi_v3_generate(
    OosiV3GenCtx   *ctx,
    const int      *prompt_tokens,
    int             prompt_len,
    OosiV3TokenCb   output_cb,
    void           *userdata
);

// ============================================================
// Memory budget helpers (for arena sizing)
// ============================================================

// Scratch buffer size in floats needed for given dims
static inline uint64_t oosi_v3_scratch_floats(int d_model, int d_inner,
                                               int dt_rank, int d_state) {
    // x_norm(D) + x_and_z(2Di) + x_conv(Di) + xBCdt(Dt+2S) + dt_full(Di)
    // + x_cur(D) + x_out(D) = D + 2Di + Di + (Dt+2S) + Di + D + D
    // = 3D + 4Di + Dt + 2S
    return (uint64_t)(3*d_model + 4*d_inner + dt_rank + 2*d_state) * sizeof(ssm_f32);
}

// SSM hidden state size in bytes
static inline uint64_t oosi_v3_h_state_bytes(int n_layer, int d_inner, int d_state) {
    return (uint64_t)n_layer * d_inner * d_state * sizeof(ssm_f32);
}

// Conv ring buffer size in bytes
static inline uint64_t oosi_v3_conv_buf_bytes(int n_layer, int d_inner, int d_conv) {
    return (uint64_t)n_layer * d_inner * d_conv * sizeof(ssm_f32);
}

// Conv pos array size in bytes
static inline uint64_t oosi_v3_conv_pos_bytes(int n_layer) {
    return (uint64_t)n_layer * sizeof(int);
}

#ifdef __cplusplus
}
#endif
