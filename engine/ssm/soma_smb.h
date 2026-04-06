// soma_smb.h — SomaMind Synaptic Memory Bus
//
// A ring buffer of compressed interaction records — short-term memory
// across REPL turns within a session. No dynamic allocation: fixed-size
// slots, oldest entry overwritten when full.
//
// Each slot stores:
//   - Input hash (FNV-1a of prompt text)
//   - Domain classifier result
//   - Routing decision (REFLEX/INTERNAL/EXTERNAL/DUAL)
//   - Confidence at first token
//   - Dual core result (Solar/Lunar/Fusion)
//   - Compressed gist: top-8 output token IDs (8 bytes)
//   - Relevance score (decays each turn — forgetting)
//   - Turn number (session-global counter)
//
// Retrieval: find most relevant slot for current input via hash-proximity
// + domain match + recency weighting. Used to bias DNA params before
// inference (SomaMind "remembers" how it handled similar queries).
//
// Freestanding C11 — no libc.

#pragma once

#include "soma_router.h"   // SomaDomain, SomaRouteType
#include "soma_dual.h"     // SomaCoreUsed
#include "ssm_types.h"     // ssm_f32, uint32_t, uint8_t

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================
#define SOMA_SMB_CAPACITY   32   // Max memory slots (power of 2)
#define SOMA_SMB_GIST_LEN    8   // Compressed output: first N token IDs (uint16_t)
#define SOMA_SMB_DECAY      0.85f // Relevance decay per turn

// ============================================================
// One memory slot
// ============================================================
typedef struct {
    uint32_t    input_hash;                  // FNV-1a of raw input text
    uint8_t     domain;                      // SomaDomain at inference time
    uint8_t     route;                       // SomaRouteType used
    uint8_t     core_used;                   // SomaCoreUsed (Solar/Lunar/Fusion)
    uint8_t     pad;
    float       confidence;                  // First-token confidence
    float       relevance;                   // [0,1] — decays over turns
    uint32_t    turn;                        // Session turn number
    uint16_t    gist[SOMA_SMB_GIST_LEN];     // Top N output token IDs
    uint8_t     gist_len;                    // How many gist tokens are valid
    uint8_t     _pad[3];
} SomaSmbSlot;

// ============================================================
// Synaptic Memory Bus context
// ============================================================
typedef struct {
    SomaSmbSlot slots[SOMA_SMB_CAPACITY];
    int         head;           // Write pointer (ring)
    int         count;          // How many slots are filled (0..SOMA_SMB_CAPACITY)
    uint32_t    turn;           // Current session turn counter
    // Stats
    int         total_writes;
    int         total_hits;     // Retrieval found a relevant match
    int         total_misses;   // No relevant match found
} SomaSmbCtx;

// ============================================================
// Retrieval result
// ============================================================
typedef struct {
    int         found;          // 1 if a matching slot was found
    int         slot_idx;       // Index into ctx->slots
    float       match_score;    // [0,1] quality of match
    const SomaSmbSlot *slot;    // Pointer to matched slot (NULL if !found)
} SomaSmbMatch;

// ============================================================
// API
// ============================================================

// Initialize / reset memory bus
void soma_smb_init(SomaSmbCtx *ctx);

// Write a new interaction record into the ring buffer.
// Call AFTER inference completes.
void soma_smb_write(SomaSmbCtx *ctx,
                    uint32_t input_hash,
                    SomaDomain domain,
                    SomaRoute route,
                    SomaCoreUsed core_used,
                    float confidence,
                    const uint16_t *out_tokens,
                    int n_out_tokens);

// Advance turn counter + decay all relevance scores.
// Call ONCE per REPL turn (before or after inference).
void soma_smb_tick(SomaSmbCtx *ctx);

// Query: find best matching slot for current input.
// Matching considers: hash similarity, domain match, recency.
// Returns SomaSmbMatch with found=0 if nothing relevant.
SomaSmbMatch soma_smb_query(const SomaSmbCtx *ctx,
                             uint32_t input_hash,
                             SomaDomain domain);

// Compute FNV-1a hash of a null-terminated C string (freestanding)
uint32_t soma_smb_hash(const char *text, int len);

// Print memory bus status (inline UEFI-safe integer formatting)
typedef void (*SomaSmbPrintFn)(const char *msg);
void soma_smb_print_stats(const SomaSmbCtx *ctx, SomaSmbPrintFn fn);

#ifdef __cplusplus
}
#endif
