// oosi_v3_loader.h — OOSI v3 full standalone Mamba weight format
//
// OOSI v3 includes ALL Mamba block weights (in_proj, out_proj, conv, norm,
// A_log, D, x_proj, dt_proj, embedding, lm_head, HaltingHead).
// No MAMB float32 binary required — fully self-contained.
//
// Magic: 0x4F4F5333 ("OOS3"), version 3
//
// Memory: caller provides the entire binary as a single contiguous buffer.
// oosi_v3_load() sets up pointers into that buffer (zero-copy).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ssm_infer.h"   // ssm_f32, ssm_q8, SsmStatus, SSM_MAX_LAYERS

#define OOSI_V3_MAGIC    0x4F4F5333u   // "OOS3"
#define OOSI_V3_VERSION  3u

// ============================================================
// Header (40 bytes, little-endian)
// ============================================================
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t d_model;
    uint32_t n_layer;
    uint32_t d_state;
    uint32_t d_conv;
    uint32_t expand;
    uint32_t vocab_size;
    uint32_t dt_rank;
    uint32_t halt_d_input;
} OosiV3Header;

// ============================================================
// Per-layer weight pointers (all into the mapped binary)
// ============================================================
typedef struct {
    // RMSNorm (float32, d_model)
    const ssm_f32 *norm_weight;

    // in_proj: int8 quantized [2*d_inner × d_model]
    const ssm_f32 *in_proj_scale;   // [2*d_inner]
    const ssm_q8  *in_proj_q8;      // [2*d_inner * d_model]

    // conv1d (float32)
    const ssm_f32 *conv_weight;     // [d_inner * d_conv]
    const ssm_f32 *conv_bias;       // [d_inner]

    // x_proj: int8 quantized [(dt_rank+2*d_state) × d_inner]
    const ssm_f32 *x_proj_scale;
    const ssm_q8  *x_proj_q8;
    int            x_out_rows;      // = dt_rank + 2*d_state

    // dt_proj: int8 quantized [d_inner × dt_rank]
    const ssm_f32 *dt_proj_scale;
    const ssm_q8  *dt_proj_q8;
    const ssm_f32 *dt_proj_bias;    // [d_inner]

    // SSM params (float32)
    const ssm_f32 *A_log;           // [d_inner * d_state]
    const ssm_f32 *D;               // [d_inner]

    // out_proj: int8 quantized [d_model × d_inner]
    const ssm_f32 *out_proj_scale;
    const ssm_q8  *out_proj_q8;
} OosiV3LayerWeights;

// ============================================================
// Complete v3 weights struct
// ============================================================
typedef struct {
    OosiV3Header header;

    // Derived dimensions (computed from header)
    int d_model;
    int n_layer;
    int d_state;
    int d_conv;
    int expand;
    int vocab_size;
    int dt_rank;
    int d_inner;    // = d_model * expand
    int halt_d_input;

    // Per-layer weights
    OosiV3LayerWeights layers[SSM_MAX_LAYERS];

    // Global weights
    const ssm_f32 *final_norm;      // [d_model]
    const ssm_f32 *embed_scale;     // [vocab_size]
    const ssm_q8  *embed_q8;        // [vocab_size * d_model]
    const ssm_f32 *lm_head_scale;   // [vocab_size]
    const ssm_q8  *lm_head_q8;      // [vocab_size * d_model]

    // HaltingHead (float32 MLP)
    const ssm_f32 *halt_data;
    uint32_t       halt_bytes;

    // Source buffer (must stay alive)
    const void    *raw_buf;
    uint64_t       raw_len;
} OosiV3Weights;

// ============================================================
// Loader
// ============================================================

// Parse the binary in-place (zero-copy).
// buf must remain valid for the lifetime of weights.
SsmStatus oosi_v3_load(
    OosiV3Weights  *out,
    const void     *buf,
    uint64_t        len
);

// Validate loaded weights (sanity checks).
SsmStatus oosi_v3_validate(const OosiV3Weights *w);

// Print config summary via callback.
typedef void (*OosiV3PrintFn)(const char *msg);
void oosi_v3_print_config(const OosiV3Weights *w, OosiV3PrintFn fn);

#ifdef __cplusplus
}
#endif
