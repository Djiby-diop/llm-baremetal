#pragma once
/*
 * oo-crypto — Freestanding Cryptography for OO
 * ==============================================
 * No libc. No OS. Pure C freestanding crypto primitives.
 *
 * Algorithms:
 *  - ChaCha20: stream cipher for KV cache encryption + IPC
 *  - Poly1305: MAC for authenticated messages
 *  - SHA-256: DNA signing, file integrity
 *  - FNV-1a 64-bit: fast hash (model fingerprint, already used)
 *  - CSPRNG: ChaCha20-based PRNG seeded from hardware RDRAND
 *
 * Use cases in OO:
 *  - DNA signing: dna_hash = SHA256(hw_fingerprint + weights_hash)
 *  - KV cache encryption: sensitive context encrypted at rest
 *  - IPC signing: swarm messages authenticated (Poly1305)
 *  - Thanatosion: seed file encrypted before death
 *  - Mirrorion: self-knowledge ring encrypted (private thoughts)
 */

#ifndef OO_CRYPTO_H
#define OO_CRYPTO_H

#include <stdint.h>

/* ── SHA-256 ────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
    int      buf_len;
} OoCryptoSha256Ctx;

void     oo_sha256_init(OoCryptoSha256Ctx *ctx);
void     oo_sha256_update(OoCryptoSha256Ctx *ctx, const uint8_t *data, int len);
void     oo_sha256_final(OoCryptoSha256Ctx *ctx, uint8_t digest[32]);
void     oo_sha256(const uint8_t *data, int len, uint8_t digest[32]);

/* ── ChaCha20 ───────────────────────────────────────────────────────── */
typedef struct {
    uint32_t state[16];
    uint8_t  keystream[64];
    int      pos;
} OoCryptoChaCha20Ctx;

void oo_chacha20_init(OoCryptoChaCha20Ctx *ctx,
                      const uint8_t key[32],
                      const uint8_t nonce[12],
                      uint32_t counter);
void oo_chacha20_xor(OoCryptoChaCha20Ctx *ctx,
                     const uint8_t *in, uint8_t *out, int len);

/* ── FNV-1a 64-bit (fast hash) ──────────────────────────────────────── */
uint64_t oo_fnv1a_64(const uint8_t *data, int len);

/* ── CSPRNG (ChaCha20-based, seeded from RDRAND) ────────────────────── */
typedef struct {
    OoCryptoChaCha20Ctx chacha;
    int                 initialized;
} OoCryptoCsprng;

int  oo_csprng_init(OoCryptoCsprng *ctx);   /* uses RDRAND; returns 0 on no RDRAND */
void oo_csprng_bytes(OoCryptoCsprng *ctx, uint8_t *out, int len);
uint32_t oo_csprng_u32(OoCryptoCsprng *ctx);
uint64_t oo_csprng_u64(OoCryptoCsprng *ctx);

/* ── DNA signing ────────────────────────────────────────────────────── */
/* dna_sign: SHA256(hw_fingerprint_bytes || model_hash_bytes) → 32-byte DNA */
void oo_dna_sign(const uint8_t *hw_fp, int hw_fp_len,
                 const uint8_t *model_hash, int model_hash_len,
                 uint8_t dna_out[32]);

/* dna_to_u32: compact 32-byte DNA to 4-byte display hash */
uint32_t oo_dna_to_u32(const uint8_t dna[32]);

#endif /* OO_CRYPTO_H */
