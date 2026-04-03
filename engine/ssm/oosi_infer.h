#ifndef OOSI_INFER_H
#define OOSI_INFER_H

// oosi_infer.h — OOSI v2 inference with adaptive halting — bare-metal freestanding
//
// This module wraps the Mamba SSM engine to use OOSI v2 quantized weights
// for the projection layers (x_proj, dt_proj via int8) and runs the HaltingHead
// after each token to decide whether to stop generation.
//
// The HaltingHead is a 3-layer MLP that takes [hidden_state | position] and
// outputs a halt probability P in [0,1]. When P >= halt_threshold, generation stops.
//
// Architecture (Mamba-2.8B):
//   d_model=2560, n_layer=64, vocab=50282, d_inner=5120, dt_rank=160, d_state=16
//   HaltingHead: Linear(2561→512) → GELU → Linear(512→64) → GELU → Linear(64→1) → Sigmoid
//
// Key design decision: OOSI v2 stores ONLY x_proj + dt_proj as int8.
// All other weights (in_proj, A_log, D, out_proj, embed, final_norm, lm_head)
// are NOT in this binary — they remain in the float32 MAMB binary.
//
// This file provides:
//   1. oosi_halting_head_forward() — run the MLP halt predictor
//   2. oosi_block_forward_q8()    — Mamba block with int8 x_proj + dt_proj
//   3. oosi_generate()            — adaptive halting generation loop

#include "ssm_types.h"
#include "oosi_loader.h"
#include "mamba_weights.h"
#include "ssm_infer.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// HaltingHead layout (from Python sorted state_dict keys)
//   net.0.bias    : [512]
//   net.0.weight  : [512 * d_input]   d_input=2561
//   net.2.bias    : [64]
//   net.2.weight  : [64 * 512]
//   net.4.bias    : [1]
//   net.4.weight  : [1 * 64]
// ============================================================
typedef struct {
    const ssm_f32 *l0_bias;    // [512]
    const ssm_f32 *l0_weight;  // [512 * d_input]
    const ssm_f32 *l2_bias;    // [64]
    const ssm_f32 *l2_weight;  // [64 * 512]
    const ssm_f32 *l4_bias;    // [1]
    const ssm_f32 *l4_weight;  // [1 * 64]
    int d_input;               // 2561 = d_model + 1
} OosiHaltHead;

// Halting result (one token)
typedef struct {
    int     token;      // sampled token id
    float   halt_prob;  // P(halt) from HaltingHead
    int     halted;     // 1 if P >= threshold
    int     loop;       // current reasoning loop index
} OosiHaltResult;

// Generation context (caller-owned, no malloc)
typedef struct {
    const OosiWeights  *oosi;       // OOSI v2 quantized weights
    const MambaWeights *mamb;       // MAMB float32 weights (in_proj, A_log, D, etc.)
    OosiHaltHead        halt_head;  // parsed HaltingHead pointers

    // Recurrent state (reset per sequence)
    MambaState          state;

    // Scratch buffers (caller must provide)
    ssm_f32 *x_buf;        // [d_model]
    ssm_f32 *x_out_buf;    // [d_model]
    ssm_f32 *scratch;      // [8 * d_inner] — larger scratch for q8 ops
    ssm_f32 *logits;       // [vocab_size]
    ssm_f32 *halt_buf;     // [d_model + 1] for HaltingHead input
    ssm_f32 *halt_h1;      // [512]
    ssm_f32 *halt_h2;      // [64]

    // Config
    float halt_threshold;  // default 0.7 (from training: 88.6% acc@0.7)
    float temperature;
    float top_p;
    uint32_t rng_state;

    int tokens_generated;
    int max_tokens;
} OosiGenCtx;

// ============================================================
// API
// ============================================================

// Parse HaltingHead pointers from OosiWeights.halt_data.
// Must be called after oosi_load().
SsmStatus oosi_halt_head_parse(OosiHaltHead *head, const OosiWeights *w);

// Run HaltingHead: takes hidden state [d_model] + position scalar → P in [0,1].
float oosi_halting_head_forward(
    const OosiHaltHead *head,
    const ssm_f32      *hidden,    // [d_model]
    int                 position,  // token position (0-based)
    ssm_f32            *h1_buf,   // [512] scratch
    ssm_f32            *h2_buf,   // [64]  scratch
    ssm_f32            *inp_buf   // [d_model+1] scratch
);

// Initialize generation context (no malloc — caller provides all buffers).
SsmStatus oosi_gen_ctx_init(
    OosiGenCtx         *ctx,
    const OosiWeights  *oosi,
    const MambaWeights *mamb,
    ssm_f32 *x_buf,
    ssm_f32 *x_out_buf,
    ssm_f32 *scratch,   // [8 * d_inner]
    ssm_f32 *logits,    // [vocab_size]
    ssm_f32 *halt_buf,  // [d_model+1]
    ssm_f32 *halt_h1,   // [512]
    ssm_f32 *halt_h2,   // [64]
    float halt_threshold,
    float temperature,
    float top_p,
    uint32_t seed,
    int max_tokens
);

// Reset recurrent state (call between sequences).
void oosi_gen_ctx_reset(OosiGenCtx *ctx);

// Forward one token using OOSI v2 (int8 x_proj/dt_proj).
// Returns the sampled next token and halt decision.
OosiHaltResult oosi_forward_one(OosiGenCtx *ctx, int token_id);

// Generate up to max_tokens tokens with adaptive halting.
// Calls output_cb(token_id, halt_result) for each token.
// Stops when halted or max_tokens reached.
// Returns total tokens generated.
typedef void (*OosiTokenCb)(int token_id, const OosiHaltResult *result, void *userdata);
int oosi_generate(
    OosiGenCtx   *ctx,
    const int    *prompt_tokens,
    int           prompt_len,
    OosiTokenCb   output_cb,
    void         *userdata
);

#ifdef __cplusplus
}
#endif

#endif // OOSI_INFER_H
