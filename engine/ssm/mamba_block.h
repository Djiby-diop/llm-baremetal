#ifndef MAMBA_BLOCK_H
#define MAMBA_BLOCK_H

// Mamba block — freestanding bare-metal implementation
// One Mamba block = selective SSM + depthwise conv + SiLU gate
// No heap allocation: all state passed as pointers.

#include "ssm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Core Mamba block forward pass (single token, recurrent mode)
//
// Input:  x_in [d_model]  — current token embedding
// Output: x_out[d_model]  — transformed embedding
// state:  MambaLayerState — updated in place (O(1) memory!)
//
// Scratch: caller provides temp buffer of >= 4*d_inner floats
// ============================================================
void mamba_block_forward(
    const MambaLayerWeights *w,
    MambaLayerState         *state,
    const ssm_f32           *x_in,   // [d_model]
    ssm_f32                 *x_out,  // [d_model]
    ssm_f32                 *scratch // [4 * d_inner] temp buffer
);

// ============================================================
// SSM selective scan (core algorithm — Algorithm 2 in Mamba paper)
//
// x:  [d_inner]         — input after convolution
// dt: [d_inner]         — discretization step (softplus)
// A:  [d_inner,d_state] — state transition matrix (log form)
// B:  [d_state]         — input projection
// C:  [d_state]         — output projection
// h:  [d_inner,d_state] — hidden state (updated in place)
// y:  [d_inner]         — output
// ============================================================
void mamba_ssm_step(
    const ssm_f32 *x,    // [d_inner]
    const ssm_f32 *dt,   // [d_inner]
    const ssm_f32 *A_log,// [d_inner * d_state]
    const ssm_f32 *B,    // [d_state]
    const ssm_f32 *C,    // [d_state]
    const ssm_f32 *D,    // [d_inner] skip
          ssm_f32 *h,    // [d_inner * d_state] in/out
          ssm_f32 *y     // [d_inner] out
);

// ============================================================
// Depthwise conv1d (causal, circular buffer, single step)
// Updates conv_buf in place, returns convolved output.
// ============================================================
void mamba_conv1d_step(
    const ssm_f32 *weight,    // [d_inner * d_conv]
    const ssm_f32 *bias,      // [d_inner] or NULL
          ssm_f32 *conv_buf,  // [d_inner * d_conv] circular buffer
          int     *conv_pos,  // circular position (updated)
    const ssm_f32 *x_in,      // [d_inner] input
          ssm_f32 *x_out,     // [d_inner] output
    int d_inner, int d_conv
);

// ============================================================
// Activation functions (inline — no libc math needed)
// ============================================================
static inline ssm_f32 mamba_silu(ssm_f32 x) {
    // SiLU(x) = x * sigmoid(x) = x / (1 + exp(-x))
    // Fast approximation safe for bare-metal
    ssm_f32 e;
    if (x >= 0.0f) {
        // exp(-x) for x >= 0: use series for small x, clamp for large
        e = 1.0f / (1.0f + (x < 20.0f ? (1.0f - x + x*x*0.5f - x*x*x*(1.0f/6.0f)) : 0.0f));
    } else {
        ssm_f32 ex = (-x < 20.0f) ? (1.0f + (-x) + (-x)*(-x)*0.5f) : 1e9f;
        e = ex / (1.0f + ex);
    }
    return x * e;
}

static inline ssm_f32 mamba_softplus(ssm_f32 x) {
    // softplus(x) = log(1 + exp(x))
    // For large x: softplus(x) ≈ x
    if (x > 20.0f) return x;
    if (x < -20.0f) return 0.0f;
    // Approximation: ln(1+e^x) via Taylor for |x| < 1, else x + ln(1+e^-x)
    if (x >= 0.0f) {
        return x + 0.6931472f + x * 0.5f * (1.0f - x * (1.0f/12.0f));
    }
    ssm_f32 ex = 1.0f + x + x*x*0.5f + x*x*x*(1.0f/6.0f);
    if (ex < 0.0f) ex = 0.0f;
    return ex > 0.0f ? ex : 0.0f; // ln(1+e^x) ≈ e^x for x << 0
}

static inline ssm_f32 mamba_rmsnorm_elem(ssm_f32 x, ssm_f32 rms_inv, ssm_f32 w) {
    return x * rms_inv * w;
}

// Compute RMS norm scale factor for a vector
ssm_f32 mamba_rms_inv(const ssm_f32 *x, int n, ssm_f32 eps);

// Apply RMSNorm: y[i] = x[i] * rms_inv * weight[i]
void mamba_rmsnorm(const ssm_f32 *x, const ssm_f32 *weight, ssm_f32 *y, int n, ssm_f32 eps);

// Matrix-vector multiply: y = W * x, W is [rows x cols], x is [cols]
void mamba_matmul(const ssm_f32 *W, const ssm_f32 *x, ssm_f32 *y, int rows, int cols);

#ifdef __cplusplus
}
#endif

#endif // MAMBA_BLOCK_H
