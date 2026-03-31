// Mamba block implementation — freestanding bare-metal
// No libc, no heap. All state is caller-managed.
// Compile: gcc -ffreestanding -fno-stack-protector -O2 -msse2

#include "mamba_block.h"

// ============================================================
// Math primitives (no libm)
// ============================================================

static ssm_f32 ssm_expf_approx(ssm_f32 x) {
    // Fast exp approximation valid for x in [-20, 20]
    // Based on: e^x = 2^(x/ln2), then bit-hack for 2^n
    if (x < -20.0f) return 0.0f;
    if (x >  20.0f) return 485165195.0f;
    // Horner form of e^x for |x| < ln2/2, then range reduce
    // Simple piecewise: good enough for SSM dt discretization
    int n = (int)(x * 1.4426950f); // x / ln(2)
    ssm_f32 r = x - (ssm_f32)n * 0.6931472f;
    ssm_f32 e = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f + r * 0.04166667f)));
    // Multiply by 2^n via bit manipulation on IEEE 754
    union { ssm_f32 f; uint32_t i; } u;
    u.f = e;
    u.i += (uint32_t)((n + 127) - 127) << 23;
    return u.f;
}

static ssm_f32 ssm_logf_approx(ssm_f32 x) {
    // ln(x) = ln(m * 2^e) = ln(m) + e*ln(2), m in [1,2)
    if (x <= 0.0f) return -20.0f;
    union { ssm_f32 f; uint32_t i; } u;
    u.f = x;
    int e = (int)((u.i >> 23) & 0xFF) - 127;
    u.i = (u.i & 0x007FFFFF) | 0x3F800000; // mantissa in [1,2)
    ssm_f32 m = u.f;
    // ln(m) for m in [1,2): Pade approx
    ssm_f32 t = (m - 1.0f) / (m + 1.0f);
    ssm_f32 t2 = t * t;
    ssm_f32 ln_m = 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * 0.2f));
    return ln_m + (ssm_f32)e * 0.6931472f;
}

// ============================================================
// RMSNorm
// ============================================================
ssm_f32 mamba_rms_inv(const ssm_f32 *x, int n, ssm_f32 eps) {
    ssm_f32 sum = 0.0f;
    for (int i = 0; i < n; i++) sum += x[i] * x[i];
    sum = sum / (ssm_f32)n + eps;
    // 1/sqrt via Newton-Raphson
    union { ssm_f32 f; uint32_t i; } u;
    u.f = sum;
    u.i = 0x5F375A86 - (u.i >> 1);
    ssm_f32 y = u.f;
    y = y * (1.5f - 0.5f * sum * y * y);
    y = y * (1.5f - 0.5f * sum * y * y);
    return y;
}

void mamba_rmsnorm(const ssm_f32 *x, const ssm_f32 *weight, ssm_f32 *y, int n, ssm_f32 eps) {
    ssm_f32 inv = mamba_rms_inv(x, n, eps);
    for (int i = 0; i < n; i++) y[i] = x[i] * inv * weight[i];
}

// ============================================================
// Matrix-vector multiply y = W*x, W:[rows,cols]
// ============================================================
void mamba_matmul(const ssm_f32 *W, const ssm_f32 *x, ssm_f32 *y, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        ssm_f32 acc = 0.0f;
        const ssm_f32 *row = W + i * cols;
        for (int j = 0; j < cols; j++) acc += row[j] * x[j];
        y[i] = acc;
    }
}

// ============================================================
// Depthwise conv1d — single step, circular buffer
// ============================================================
void mamba_conv1d_step(
    const ssm_f32 *weight,
    const ssm_f32 *bias,
          ssm_f32 *conv_buf,
          int     *conv_pos,
    const ssm_f32 *x_in,
          ssm_f32 *x_out,
    int d_inner, int d_conv)
{
    // Insert new input into circular buffer
    int pos = *conv_pos;
    for (int i = 0; i < d_inner; i++) {
        conv_buf[i * d_conv + pos] = x_in[i];
    }
    *conv_pos = (pos + 1) % d_conv;

    // Compute convolution output for each channel
    for (int i = 0; i < d_inner; i++) {
        ssm_f32 acc = 0.0f;
        const ssm_f32 *w = weight + i * d_conv;
        const ssm_f32 *buf = conv_buf + i * d_conv;
        // Causal: most recent input is at pos, oldest at (pos+1)%d_conv
        for (int k = 0; k < d_conv; k++) {
            int idx = (pos - k + d_conv) % d_conv;
            acc += w[k] * buf[idx];
        }
        if (bias) acc += bias[i];
        x_out[i] = acc;
    }
}

// ============================================================
// SSM selective scan — single step (Algorithm 2, Mamba paper)
// Discretizes A,B with ZOH, updates hidden state h, computes y
// ============================================================
void mamba_ssm_step(
    const ssm_f32 *x,
    const ssm_f32 *dt,
    const ssm_f32 *A_log,
    const ssm_f32 *B,
    const ssm_f32 *C,
    const ssm_f32 *D,
          ssm_f32 *h,    // [d_inner * d_state] in/out
          ssm_f32 *y     // [d_inner] out
)
{
    // For each inner dim i and state dim j:
    //   dt_i = softplus(dt[i])               — ensure positive
    //   A_i  = exp(dt_i * (-exp(A_log[i,j]))) — ZOH discretization
    //   B_j  = dt_i * B[j]                   — input projection
    //   h[i,j] = A_i * h[i,j] + B_j * x[i]  — state update
    //   y[i]  = sum_j(C[j] * h[i,j]) + D[i]*x[i] — output

    // Get d_inner and d_state from context (passed via array sizes)
    // We iterate assuming h layout: h[i*d_state + j]
    // A_log layout: A_log[i*d_state + j]
    // d_inner and d_state must be inferred — use fixed max or pass as global
    // For generality, caller sets these; here we use a local approach:
    // The caller (mamba_block_forward) knows the dims from MambaLayerWeights.
    // We accept them as part of the outer function. See mamba_block_forward.
    (void)x; (void)dt; (void)A_log; (void)B; (void)C; (void)D; (void)h; (void)y;
    // Full implementation in mamba_block_forward with dim context.
}

// ============================================================
// Full Mamba block forward — single token, recurrent mode
// ============================================================
void mamba_block_forward(
    const MambaLayerWeights *w,
    MambaLayerState         *state,
    const ssm_f32           *x_in,
          ssm_f32           *x_out,
          ssm_f32           *scratch)
{
    int d_model  = w->d_model;
    int d_inner  = w->d_inner;
    int d_state  = w->d_state;
    int d_conv   = w->d_conv;
    int dt_rank  = w->dt_rank;

    // Scratch layout: scratch[0..4*d_inner]
    ssm_f32 *x_norm    = scratch;                   // [d_model]
    ssm_f32 *x_and_z   = scratch + d_model;         // [2*d_inner]
    ssm_f32 *x_conv    = scratch + d_model + 2*d_inner; // [d_inner]
    ssm_f32 *xBCdt     = x_conv + d_inner;          // [dt_rank + 2*d_state]

    // 1. RMSNorm
    mamba_rmsnorm(x_in, w->norm_weight, x_norm, d_model, 1e-5f);

    // 2. in_proj: x_norm → [z, x_expanded] (shape [2*d_inner])
    mamba_matmul(w->in_proj, x_norm, x_and_z, 2 * d_inner, d_model);
    const ssm_f32 *z_gate   = x_and_z;          // [d_inner]
    const ssm_f32 *x_expand = x_and_z + d_inner;// [d_inner]

    // 3. Depthwise conv1d on x_expand
    mamba_conv1d_step(
        w->conv_weight, w->conv_bias,
        state->conv_buf, &state->conv_pos,
        x_expand, x_conv,
        d_inner, d_conv);

    // 4. SiLU activation on conv output
    for (int i = 0; i < d_inner; i++) x_conv[i] = mamba_silu(x_conv[i]);

    // 5. x_proj: x_conv → [dt_raw, B, C] (shape [dt_rank + 2*d_state])
    mamba_matmul(w->x_proj, x_conv, xBCdt, dt_rank + 2 * d_state, d_inner);

    const ssm_f32 *dt_raw = xBCdt;
    const ssm_f32 *B_vec  = xBCdt + dt_rank;
    const ssm_f32 *C_vec  = xBCdt + dt_rank + d_state;

    // 6. dt_proj: dt_raw → dt [d_inner] + softplus
    // Reuse x_and_z as dt buffer (after in_proj it's no longer needed raw)
    ssm_f32 *dt_full = x_and_z; // [d_inner]
    mamba_matmul(w->dt_proj_weight, dt_raw, dt_full, d_inner, dt_rank);
    for (int i = 0; i < d_inner; i++) {
        dt_full[i] += w->dt_proj_bias[i];
        dt_full[i] = mamba_softplus(dt_full[i]);
    }

    // 7. Selective SSM step (inlined for dimension access)
    ssm_f32 *y_ssm = x_and_z + d_inner; // [d_inner]
    for (int i = 0; i < d_inner; i++) {
        ssm_f32 dt_i = dt_full[i];
        ssm_f32 y_i  = 0.0f;
        for (int j = 0; j < d_state; j++) {
            ssm_f32 a_log = w->A_log[i * d_state + j];
            // ZOH: dA = exp(dt * A), A = -exp(A_log) (negative definite)
            ssm_f32 dA = ssm_expf_approx(dt_i * (-ssm_expf_approx(a_log)));
            // dB = dt * B (Euler for input matrix)
            ssm_f32 dB = dt_i * B_vec[j];
            // Update hidden state
            ssm_f32 *hij = &state->h[i * d_state + j];
            *hij = dA * (*hij) + dB * x_conv[i];
            // Accumulate output
            y_i += C_vec[j] * (*hij);
        }
        // Skip connection D
        y_ssm[i] = y_i + w->D[i] * x_conv[i];
    }

    // 8. Gate with z (SiLU gate)
    for (int i = 0; i < d_inner; i++) y_ssm[i] *= mamba_silu(z_gate[i]);

    // 9. out_proj: y_ssm [d_inner] → x_out [d_model]
    mamba_matmul(w->out_proj, y_ssm, x_out, d_model, d_inner);

    // 10. Residual connection
    for (int i = 0; i < d_model; i++) x_out[i] += x_in[i];
}
