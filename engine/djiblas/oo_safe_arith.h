/* oo_safe_arith.h — Safe integer arithmetic for OO baremetal
 * =============================================================
 * Thin adaptation layer over arithmion-safe (jtckdint, ISC licence).
 * Provides OO-namespaced checked arithmetic macros (oo_ckd_*) and
 * GGUF-specific helpers to compute tensor byte sizes without overflow.
 *
 * Works in freestanding C11 (UEFI) and hosted builds.
 * Backed by: external/arithmion-safe (jtckdint.h)
 *
 * Usage:
 *   uint64_t bytes;
 *   if (oo_gguf_tensor_bytes(elems, sizeof_elem, &bytes))
 *       return LLMK_PORTABLE_INVALID_ARGUMENT; // overflow
 */

#ifndef FBDD45AF_F51C_47B6_90C4_2BB7C9774E6E
#define FBDD45AF_F51C_47B6_90C4_2BB7C9774E6E
#ifndef OO_SAFE_ARITH_H
#define OO_SAFE_ARITH_H

#include "../../external/arithmion-safe/jtckdint.h"
#include <stdint.h>
#include <stddef.h>

/* ── OO-namespaced wrappers (expand to ckd_* from C23 / jtckdint) ── */

/* oo_ckd_add(res_ptr, a, b): *res = a+b, returns 1 on overflow */
#define oo_ckd_add(res, a, b)  ckd_add((res), (a), (b))
/* oo_ckd_sub(res_ptr, a, b): *res = a-b, returns 1 on underflow */
#define oo_ckd_sub(res, a, b)  ckd_sub((res), (a), (b))
/* oo_ckd_mul(res_ptr, a, b): *res = a*b, returns 1 on overflow */
#define oo_ckd_mul(res, a, b)  ckd_mul((res), (a), (b))

/* ── GGUF tensor size helpers ──────────────────────────────────────── */

/* Compute byte size of a tensor: bytes = elem_count * elem_bytes.
 * Returns 0 on success, 1 on overflow.
 * Use before any AllocatePages / malloc call in the GGUF loader. */
static inline int oo_gguf_tensor_bytes(uint64_t elem_count,
                                        uint32_t elem_bytes,
                                        uint64_t *out_bytes)
{
    uint64_t eb = (uint64_t)elem_bytes;
    return (int)oo_ckd_mul(out_bytes, elem_count, eb);
}

/* Compute byte size of a block-quantised tensor.
 * blocks = ceil(elem_count / block_size).
 * Returns 0 on success, 1 on overflow (any intermediate step). */
static inline int oo_gguf_kquant_bytes(uint64_t elem_count,
                                        uint32_t block_size,
                                        uint32_t bytes_per_block,
                                        uint64_t *out_bytes)
{
    if (!block_size) return 1;
    /* blocks = (elem_count + block_size - 1) / block_size  (ceiling) */
    uint64_t tmp;
    uint64_t bs64 = (uint64_t)block_size;
    if (oo_ckd_add(&tmp, elem_count, bs64 - 1u)) return 1;
    uint64_t blocks = tmp / bs64;
    return (int)oo_ckd_mul(out_bytes, blocks, (uint64_t)bytes_per_block);
}

/* Safe cast: uint64 → size_t (host builds). Returns 1 if truncation. */
static inline int oo_u64_to_size(uint64_t v, size_t *out)
{
    *out = (size_t)v;
    return (sizeof(size_t) < 8) && (v > (uint64_t)(size_t)-1);
}

#endif /* OO_SAFE_ARITH_H */


#endif /* FBDD45AF_F51C_47B6_90C4_2BB7C9774E6E */
