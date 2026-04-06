// soma_dual.h — SomaMind Dual Core Engine (☀ Solar + 🌙 Lunar)
//
// Samples the SAME logits twice with different parameters:
//   ☀ Solar: low temperature, tight nucleus → deterministic/logical
//   🌙 Lunar: high temperature, wide nucleus → creative/exploratory
//
// Fusion: picks Solar when confidence is high, Lunar when exploring.
// Works on any existing logit buffer — no second forward pass needed.
//
// Freestanding C11 — no libc.

#pragma once

#include "soma_dna.h"
#include "ssm_types.h"  // ssm_f32, uint32_t

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Which core was selected for this token
// ============================================================
typedef enum {
    SOMA_CORE_SOLAR  = 0,  // Logical, deterministic
    SOMA_CORE_LUNAR  = 1,  // Creative, exploratory
    SOMA_CORE_FUSION = 2,  // Blend (future: speculative decoding)
} SomaCoreUsed;

// ============================================================
// Per-token dual sample result
// ============================================================
typedef struct {
    int          solar_token;    // Token from Solar pass
    int          lunar_token;    // Token from Lunar pass
    int          selected_token; // Final decision
    SomaCoreUsed core_used;
    float        confidence;     // max(softmax(logits)) — [0, 1]
    float        solar_prob;     // Probability of solar_token under Solar dist
    float        lunar_prob;     // Probability of lunar_token under Lunar dist
} SomaDualResult;

// ============================================================
// Session stats (one per REPL session)
// ============================================================
typedef struct {
    int   total_tokens;
    int   solar_chosen;
    int   lunar_chosen;
    int   agreements;       // Solar == Lunar (high confidence)
    int   disagreements;    // Solar != Lunar (divergent cores)
    float avg_confidence;
    float avg_confidence_acc; // running sum for avg
} SomaDualStats;

// ============================================================
// Dual Core context
// ============================================================
typedef struct {
    SomaDualStats stats;
    ssm_f32  *work_buf;   // Scratch: copy of logits [vocab_size]
    int       vocab_size; // Set on init
    int       ready;
} SomaDualCtx;

// ============================================================
// API
// ============================================================

// Initialize dual context. work_buf must be [vocab_size] floats (caller provides).
void soma_dual_init(SomaDualCtx *ctx, ssm_f32 *work_buf, int vocab_size);

// Main function: sample Solar + Lunar from the same raw logits.
// raw_logits: unmodified logit vector (will be read but not modified).
// dna: current DNA (temperature_solar/lunar, top_p_solar/lunar, cognition_bias, confidence_threshold).
// rng: shared RNG state (updated in-place).
SomaDualResult soma_dual_sample(SomaDualCtx *ctx,
                                const ssm_f32 *raw_logits,
                                int vocab_size,
                                const SomaDNA *dna,
                                uint32_t *rng);

// Compute confidence = max(softmax(logits)) without modifying buffer.
// This is the model's certainty about its top-1 prediction.
float soma_dual_confidence(const ssm_f32 *logits, int vocab_size);

// Print dual core stats
typedef void (*SomaPrintFn2)(const char *msg);
void soma_dual_print_stats(const SomaDualCtx *ctx, SomaPrintFn2 fn);

#ifdef __cplusplus
}
#endif
