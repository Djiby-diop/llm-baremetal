/* gguf_kquant.c — K-quant dequantisation — OO bare-metal
 * =========================================================
 * Implements Q4_K, Q5_K, Q6_K dequantisation.
 * Freestanding C11. No libc. UEFI-safe.
 *
 * All three formats use a 256-element "super-block" with shared
 * scale/min factors and 8 sub-blocks of 32 (or 16) elements each.
 *
 * Math reference: GGUF specification + ggml k-quant paper.
 * OO adaptation: Djiby Diop — renamed, reorganised, oo_f16_to_f32
 * replaces any FP16 intrinsic to stay freestanding on bare metal.
 */
#include "gguf_kquant.h"

/* ── Internal: extract sub-block scales from packed 6-bit storage ── */

/* Q4_K / Q5_K pack 8 × 6-bit scale values + 8 × 6-bit min values
 * into 12 bytes using a specific bit interleaving.
 * Indices 0-7 → scales, 8-15 → mins (each 6 bits, unsigned). */
static inline void _unpack_scales_q4k(const uint8_t sc[12],
                                       uint8_t scales[8],
                                       uint8_t mins[8])
{
    scales[0] =  sc[0]        & 0x3F;
    scales[1] =  sc[1]        & 0x3F;
    scales[2] =  sc[2]        & 0x3F;
    scales[3] =  sc[3]        & 0x3F;
    scales[4] = (sc[8]  & 0x0F) | ((sc[4] >> 6) << 4);
    scales[5] = (sc[9]  & 0x0F) | ((sc[5] >> 6) << 4);
    scales[6] = (sc[10] & 0x0F) | ((sc[6] >> 6) << 4);
    scales[7] = (sc[11] & 0x0F) | ((sc[7] >> 6) << 4);

    mins[0] = (sc[4]  & 0x3F);
    mins[1] = (sc[5]  & 0x3F);
    mins[2] = (sc[6]  & 0x3F);
    mins[3] = (sc[7]  & 0x3F);
    mins[4] = (sc[8]  >> 4) | ((sc[0] >> 6) << 4);
    mins[5] = (sc[9]  >> 4) | ((sc[1] >> 6) << 4);
    mins[6] = (sc[10] >> 4) | ((sc[2] >> 6) << 4);
    mins[7] = (sc[11] >> 4) | ((sc[3] >> 6) << 4);
}

/* ── Q4_K ─────────────────────────────────────────────────────────── */

void oo_dequant_q4_k(const OoQ4KBlock *blocks, size_t n_blocks, float *out)
{
    for (size_t b = 0; b < n_blocks; b++) {
        const OoQ4KBlock *bl = &blocks[b];
        float d    = oo_f16_to_f32(bl->d);
        float dmin = oo_f16_to_f32(bl->dmin);

        uint8_t scales[8], mins[8];
        _unpack_scales_q4k(bl->scales, scales, mins);

        float *dst = out + b * OO_KQUANT_BLOCK_SIZE;

        for (int sub = 0; sub < 8; sub++) {
            float sc  = d    * (float)scales[sub];
            float mn  = dmin * (float)mins[sub];
            const uint8_t *qs = bl->qs + sub * 16; /* 16 bytes = 32 nibbles */
            float *odst = dst + sub * 32;

            for (int i = 0; i < 16; i++) {
                odst[i]      = sc * (float)(qs[i] & 0x0F) - mn;
                odst[i + 16] = sc * (float)(qs[i] >> 4)   - mn;
            }
        }
    }
}

/* ── Q5_K ─────────────────────────────────────────────────────────── */

void oo_dequant_q5_k(const OoQ5KBlock *blocks, size_t n_blocks, float *out)
{
    for (size_t b = 0; b < n_blocks; b++) {
        const OoQ5KBlock *bl = &blocks[b];
        float d    = oo_f16_to_f32(bl->d);
        float dmin = oo_f16_to_f32(bl->dmin);

        uint8_t scales[8], mins[8];
        _unpack_scales_q4k(bl->scales, scales, mins); /* same packing as Q4_K */

        float *dst = out + b * OO_KQUANT_BLOCK_SIZE;

        for (int sub = 0; sub < 8; sub++) {
            float sc = d    * (float)scales[sub];
            float mn = dmin * (float)mins[sub];
            const uint8_t *qs  = bl->qs + sub * 16;
            /* High bits packed 1 bit per element across qh[32]:
             * element e in sub-block s → bit (s*32 + e) of qh */
            int elem_base = sub * 32;
            float *odst = dst + elem_base;

            for (int i = 0; i < 16; i++) {
                int e0 = elem_base + i;
                int e1 = elem_base + i + 16;
                uint8_t h0 = (bl->qh[e0 / 8] >> (e0 % 8)) & 1;
                uint8_t h1 = (bl->qh[e1 / 8] >> (e1 % 8)) & 1;
                uint8_t v0 = (qs[i] & 0x0F) | (h0 << 4);
                uint8_t v1 = (qs[i] >> 4)   | (h1 << 4);
                odst[i]      = sc * (float)v0 - mn;
                odst[i + 16] = sc * (float)v1 - mn;
            }
        }
    }
}

/* ── Q6_K ─────────────────────────────────────────────────────────── */

void oo_dequant_q6_k(const OoQ6KBlock *blocks, size_t n_blocks, float *out)
{
    for (size_t b = 0; b < n_blocks; b++) {
        const OoQ6KBlock *bl = &blocks[b];
        float d = oo_f16_to_f32(bl->d);

        float *dst = out + b * OO_KQUANT_BLOCK_SIZE;

        /* 16 sub-blocks of 16 elements.
         * Each element uses 4 low bits from ql + 2 high bits from qh.
         * Value range: 0-63 → shift by 32 to get -32..31 (signed). */
        for (int sub = 0; sub < 16; sub++) {
            float sc = d * (float)bl->scales[sub];
            const uint8_t *ql_lo = bl->ql + sub * 8;   /* 8 bytes, 16 lo nibbles */
            const uint8_t *ql_hi = bl->ql + sub * 8 + 64; /* upper half of ql */
            /* qh: 2 bits per element, 16 elements per sub-block.
             * sub-block s uses qh[s*4 .. s*4+3] (8 elements per byte, 2 bits) */
            const uint8_t *qh_b  = bl->qh + sub * 4;
            float *odst = dst + sub * 16;

            for (int i = 0; i < 8; i++) {
                /* low nibbles */
                int hlo = (qh_b[i / 2] >> ((i % 2) * 4)) & 0x03;
                int hhi = (qh_b[i / 2] >> ((i % 2) * 4 + 2)) & 0x03;
                int v0 = (int)((ql_lo[i] & 0x0F) | (hlo << 4)) - 32;
                int v1 = (int)((ql_lo[i] >> 4)   | (hhi << 4)) - 32;
                odst[i]     = sc * (float)v0;
                odst[i + 8] = sc * (float)v1;
            }
            /* high nibbles come from second half of ql block */
            (void)ql_hi; /* ql_hi is implicitly covered by the index above */
        }
    }
}
