#ifndef SSM_TYPES_H
#define SSM_TYPES_H

// SSM / Mamba bare-metal types — freestanding, no libc
// Compatible with UEFI (-ffreestanding, -mno-red-zone)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// ============================================================
// Mamba architecture constants
// Updated for Mamba-2.8B: d_model=2560, n_layer=64, vocab=50282
// ============================================================
#define SSM_MAX_D_MODEL     2560   // max hidden dim (2560 for Mamba-2.8B)
#define SSM_MAX_D_STATE     16     // SSM state size (N)
#define SSM_MAX_D_CONV      4      // depthwise conv kernel size
#define SSM_MAX_D_INNER     5120   // max expanded inner dim (2560*2 for Mamba-2.8B)
#define SSM_MAX_LAYERS      64     // max Mamba layers (64 for Mamba-2.8B)
#define SSM_MAX_VOCAB       51200  // max vocabulary size (50282 for Mamba-2.8B, aligned)
#define SSM_MAX_SEQ         2048   // max sequence length

// ============================================================
// Weight precision
// ============================================================
typedef float    ssm_f32;
typedef uint16_t ssm_f16;  // half — load only, compute in f32
typedef int8_t   ssm_q8;   // Q8_0 quantized

typedef enum {
    SSM_DTYPE_F32 = 0,
    SSM_DTYPE_F16 = 1,
    SSM_DTYPE_Q8_0 = 2,
    SSM_DTYPE_Q4_K = 3,
} SsmDtype;

// ============================================================
// Mamba layer weights (all pointers into a contiguous buffer)
// ============================================================
typedef struct {
    // in_proj: projects input x → (z, x_expanded) — shape [2*d_inner, d_model]
    const ssm_f32 *in_proj;

    // conv1d: depthwise conv on x — shape [d_inner, 1, d_conv]
    const ssm_f32 *conv_weight;
    const ssm_f32 *conv_bias;   // may be NULL

    // x_proj: projects x → (dt, B, C) — shape [dt_rank + 2*d_state, d_inner]
    const ssm_f32 *x_proj;

    // dt_proj: projects dt → d_inner — shape [d_inner, dt_rank]
    const ssm_f32 *dt_proj_weight;
    const ssm_f32 *dt_proj_bias;

    // SSM parameters
    const ssm_f32 *A_log;   // log(-A), shape [d_inner, d_state]
    const ssm_f32 *D;       // skip connection, shape [d_inner]

    // out_proj: [d_model, d_inner]
    const ssm_f32 *out_proj;

    // layer norm (RMSNorm)
    const ssm_f32 *norm_weight; // shape [d_model]

    // dims for this layer
    int d_model;
    int d_inner;   // d_model * expand (typically 2)
    int d_state;   // N (typically 16)
    int d_conv;    // typically 4
    int dt_rank;   // typically ceil(d_model / 16)
} MambaLayerWeights;

// ============================================================
// Full model weights
// ============================================================
typedef struct {
    // Token embedding table: [vocab_size, d_model]
    const ssm_f32 *embed;

    // Per-layer weights
    MambaLayerWeights layers[SSM_MAX_LAYERS];
    int n_layers;

    // Final norm + LM head
    const ssm_f32 *final_norm;  // [d_model]
    const ssm_f32 *lm_head;     // [vocab_size, d_model] (or tied to embed)

    // Model config
    int d_model;
    int vocab_size;
    int n_layers_actual;

    SsmDtype dtype;

    // Raw memory buffer (owned externally)
    const void *data;
    uint64_t    data_size;
} MambaWeights;

// ============================================================
// SSM recurrent state (per layer, per sequence position)
// The key advantage over KV-cache: O(1) memory, not O(seq_len)
// ============================================================
typedef struct {
    // h: recurrent hidden state, shape [d_inner, d_state]
    // Stored as flat array: h[i*d_state + j]
    ssm_f32 h[SSM_MAX_D_INNER * SSM_MAX_D_STATE];

    // conv_state: circular buffer for depthwise conv, shape [d_inner, d_conv]
    ssm_f32 conv_buf[SSM_MAX_D_INNER * SSM_MAX_D_CONV];
    int     conv_pos;  // circular buffer position
} MambaLayerState;

typedef struct {
    MambaLayerState layers[SSM_MAX_LAYERS];
    int n_layers;
} MambaState;

// ============================================================
// Inference result
// ============================================================
typedef struct {
    int   token;       // sampled token id
    float logit_max;   // raw max logit (for confidence)
    float temperature; // temperature used
} SsmToken;

// ============================================================
// Error codes
// ============================================================
typedef enum {
    SSM_OK            = 0,
    SSM_ERR_NOMEM     = -1,
    SSM_ERR_BADWEIGHT = -2,
    SSM_ERR_BADCONFIG = -3,
    SSM_ERR_OVERFLOW  = -4,
} SsmStatus;

#ifdef __cplusplus
}
#endif

#endif // SSM_TYPES_H
