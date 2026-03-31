#ifndef SSM_INFER_H
#define SSM_INFER_H

// SSM inference engine — freestanding bare-metal Mamba inference
// Single-token recurrent mode: O(1) memory, no KV cache needed.
// Compatible with UEFI (-ffreestanding, -mno-red-zone, no libc)

#include "ssm_types.h"
#include "mamba_block.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Inference context — caller allocates, no heap
// ============================================================
typedef struct {
    const MambaWeights *weights;
    MambaState          state;          // recurrent state (O(1) memory!)

    // Scratch buffers (caller provides)
    ssm_f32 *logits;        // [vocab_size]
    ssm_f32 *x_buf;         // [d_model] current token embedding
    ssm_f32 *x_out_buf;     // [d_model] layer output
    ssm_f32 *scratch;       // [4 * d_inner_max] temp for mamba_block

    // Sampling config
    float    temperature;
    float    top_p;
    uint32_t rng_state;     // simple LCG RNG (no rand() needed)

    // Stats
    uint32_t tokens_generated;
} SsmCtx;

// ============================================================
// API
// ============================================================

// Initialize context. All buffers must be pre-allocated by caller.
// Returns SSM_OK or error.
SsmStatus ssm_ctx_init(
    SsmCtx             *ctx,
    const MambaWeights *weights,
    ssm_f32            *logits_buf,   // [vocab_size]
    ssm_f32            *x_buf,        // [d_model]
    ssm_f32            *x_out_buf,    // [d_model]
    ssm_f32            *scratch_buf,  // [4 * d_inner_max]
    float               temperature,
    float               top_p,
    uint32_t            seed
);

// Reset recurrent state (start new sequence)
void ssm_ctx_reset(SsmCtx *ctx);

// Run one forward pass for a single token.
// Returns the next token id.
int ssm_forward_one(SsmCtx *ctx, int token_id);

// Sample from logits with temperature + top-p
int ssm_sample(SsmCtx *ctx);

// Compute softmax in-place on logits[0..n]
void ssm_softmax(ssm_f32 *x, int n);

// Apply temperature scaling
void ssm_temperature_scale(ssm_f32 *logits, int n, float temp);

// ============================================================
// Buffer size helpers (use to allocate static buffers)
// ============================================================
#define SSM_LOGITS_BUF_SIZE(vocab)  ((vocab) * sizeof(ssm_f32))
#define SSM_X_BUF_SIZE(d_model)     ((d_model) * sizeof(ssm_f32))
#define SSM_SCRATCH_SIZE(d_inner)   (4 * (d_inner) * sizeof(ssm_f32))

// Total static RAM needed for inference (no heap):
// = logits + x_buf + x_out_buf + scratch + MambaState
// For a 130M Mamba: ~16MB weights + ~2MB state = well within COLD zone
#define SSM_INFER_RAM_ESTIMATE(vocab, d_model, d_inner) \
    (SSM_LOGITS_BUF_SIZE(vocab) + 2*SSM_X_BUF_SIZE(d_model) + \
     SSM_SCRATCH_SIZE(d_inner) + sizeof(MambaState))

#ifdef __cplusplus
}
#endif

#endif // SSM_INFER_H
