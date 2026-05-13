/* djiblas.h — Custom BLAS Kernels for Bare-Metal LLM (Public Prototype)
 *
 * Hand-optimized matrix operations for Transformer inference.
 * No BLAS library — everything is self-contained.
 * Detects AVX2 at runtime via CPUID and selects optimal path.
 */
#ifndef DJIBLAS_H
#define DJIBLAS_H

#include <stdint.h>
#include <stddef.h>

/* Matrix-vector multiply: y = A * x  (row-major A, shape [m x k]) */
void djiblas_matvec_f32(const float *A, const float *x, float *y,
                        int m, int k);

/* Same but A is Q4_0 quantized (GGUF format) */
void djiblas_matvec_q4k_f32(const void *A_q4, const float *x, float *y,
                             int m, int k);

/* Scaled dot-product attention (single head, causal mask)
 *   Q: [seq_len x head_dim]
 *   K: [cache_len x head_dim]
 *   V: [cache_len x head_dim]
 *   out: [seq_len x head_dim]
 */
void djiblas_attention_f32(const float *Q, const float *K, const float *V,
                           float *out, int seq_len, int cache_len,
                           int head_dim, float scale);

/* RMS normalization in-place */
void djiblas_rmsnorm_f32(float *x, const float *weight, int d, float eps);

/* SiLU activation: x = x * sigmoid(x) */
void djiblas_silu_f32(float *x, int n);

/* Rotary position embeddings (RoPE) */
void djiblas_rope_f32(float *q, float *k, int head_dim, int pos,
                      float theta);

#endif /* DJIBLAS_H */
