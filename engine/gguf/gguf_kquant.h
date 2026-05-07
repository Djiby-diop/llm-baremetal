/* gguf_kquant.h — K-quant dequantisation for OO bare-metal
 * ===========================================================
 * Unlocks Q4_K, Q5_K, Q6_K GGUF models on x86_64 bare-metal.
 * These types use "super-block" scaling (k-quants) and require
 * a separate dequantisation pass before inference.
 *
 * Design:
 *  - Freestanding C11. No libc. UEFI-safe.
 *  - No dynamic allocation: caller provides output float buffer.
 *  - Block sizes follow the GGUF spec (256 elements per super-block).
 *  - Math adapted from the public GGUF specification and ggml paper.
 *
 * See also: gguf_infer.h (Q4_0/Q5_0/Q8_0 path, already active)
 */

#ifndef A8324F49_30FC_49AC_94F8_F859274E5565
#define A8324F49_30FC_49AC_94F8_F859274E5565
#ifndef GGUF_KQUANT_H
#define GGUF_KQUANT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Block geometry (GGUF spec) ─────────────────────────────────── */

#define OO_KQUANT_BLOCK_SIZE   256   /* elements per super-block       */
#define OO_Q4K_SUBBLOCK        32    /* elements per sub-block in Q4_K */
#define OO_Q5K_SUBBLOCK        32
#define OO_Q6K_SUBBLOCK        16

/* Bytes per super-block for each K_* type */
#define OO_Q4K_BLOCK_BYTES     144   /* 2×f16 scale/min + 12B scales + 128B data */
#define OO_Q5K_BLOCK_BYTES     176   /* 2×f16 + 12B scales + 32B high-bits + 128B data */
#define OO_Q6K_BLOCK_BYTES     210   /* f16 scale + 208B data (4-bit low + 2-bit high) */

/* ── Wire format structs (packed, little-endian) ─────────────────── */

/* Q4_K super-block: 256 elements, 4 bits each.
 * Layout: d(f16) min(f16) scales[12] qs[128] */
typedef struct __attribute__((packed)) {
    uint16_t d;          /* super-block scale (f16)  */
    uint16_t dmin;       /* super-block min   (f16)  */
    uint8_t  scales[12]; /* 6-bit sub-block scales   */
    uint8_t  qs[128];    /* 4-bit quantised values   */
} OoQ4KBlock;

/* Q5_K super-block: 256 elements, 5 bits each.
 * Layout: d(f16) min(f16) scales[12] qh[32] qs[128] */
typedef struct __attribute__((packed)) {
    uint16_t d;
    uint16_t dmin;
    uint8_t  scales[12];
    uint8_t  qh[32];     /* high bit for each element */
    uint8_t  qs[128];
} OoQ5KBlock;

/* Q6_K super-block: 256 elements, 6 bits each.
 * Layout: ql[128] qh[64] scales[16] d(f16) */
typedef struct __attribute__((packed)) {
    uint8_t  ql[128];    /* low 4 bits  */
    uint8_t  qh[64];     /* high 2 bits */
    int8_t   scales[16]; /* sub-block scales (i8) */
    uint16_t d;          /* super-block scale (f16) */
} OoQ6KBlock;

/* ── F16 → F32 conversion (software, no FPU dependency) ──────────── */

static inline float oo_f16_to_f32(uint16_t h)
{
    /* IEEE-754 half-precision → single-precision */
    uint32_t s = (uint32_t)(h & 0x8000u) << 16;
    uint32_t e = (uint32_t)(h & 0x7C00u);
    uint32_t m = (uint32_t)(h & 0x03FFu);
    uint32_t v;
    if (e == 0x7C00u) {           /* Inf / NaN */
        v = s | 0x7F800000u | (m << 13);
    } else if (e == 0) {          /* subnormal → normalize */
        if (m == 0) { v = s; }
        else {
            e = 0x38800000u;
            m <<= 1;
            while (!(m & 0x0400u)) { e -= 0x00800000u; m <<= 1; }
            m &= 0x03FFu;
            v = s | e | (m << 13);
        }
    } else {
        v = s | ((e + 0x1C000u) << 13) | (m << 13);
    }
    float out;
    __builtin_memcpy(&out, &v, 4);
    return out;
}

/* ── Dequantisation API ──────────────────────────────────────────── */

/* Dequantise n_blocks super-blocks of Q4_K into out[].
 * out must have space for n_blocks * OO_KQUANT_BLOCK_SIZE floats. */
void oo_dequant_q4_k(const OoQ4KBlock *blocks, size_t n_blocks, float *out);

/* Dequantise n_blocks super-blocks of Q5_K. */
void oo_dequant_q5_k(const OoQ5KBlock *blocks, size_t n_blocks, float *out);

/* Dequantise n_blocks super-blocks of Q6_K. */
void oo_dequant_q6_k(const OoQ6KBlock *blocks, size_t n_blocks, float *out);

/* ── GGUF type-id constants for K_* quants (from spec) ──────────── */
#define OO_GGUF_TYPE_Q4_K   12
#define OO_GGUF_TYPE_Q5_K   13
#define OO_GGUF_TYPE_Q6_K   14

/* Returns 1 if the given GGUF file_type value is a K_* quant. */
static inline int oo_gguf_is_kquant(uint32_t file_type)
{
    return file_type == OO_GGUF_TYPE_Q4_K ||
           file_type == OO_GGUF_TYPE_Q5_K ||
           file_type == OO_GGUF_TYPE_Q6_K;
}

/* Bytes per super-block for a given K_* GGUF type id.
 * Returns 0 for unknown types. */
static inline uint32_t oo_kquant_block_bytes(uint32_t gguf_type)
{
    switch (gguf_type) {
    case OO_GGUF_TYPE_Q4_K: return OO_Q4K_BLOCK_BYTES;
    case OO_GGUF_TYPE_Q5_K: return OO_Q5K_BLOCK_BYTES;
    case OO_GGUF_TYPE_Q6_K: return OO_Q6K_BLOCK_BYTES;
    default: return 0;
    }
}

#ifdef __cplusplus
}
#endif
#endif /* GGUF_KQUANT_H */


#endif /* A8324F49_30FC_49AC_94F8_F859274E5565 */
