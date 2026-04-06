// soma_meta.h — SomaMind Meta-Evolution Engine
//
// Watches live performance metrics (confidence, Solar/Lunar balance,
// reflex hit rate) and auto-mutates DNA parameters via hill-climbing
// when fitness stagnates.
//
// Loop:
//   observe → score fitness → if better: keep DNA
//                             if worse:  mutate (small random step)
//
// Fitness function:
//   F = w1 * avg_confidence
//     + w2 * reflex_rate          (fast answers = good)
//     + w3 * (1 - disagreement_rate) (cores agree = confident model)
//     + w4 * memory_hit_rate      (memory being useful)
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_dna.h"
#include "soma_router.h"    // SomaRouterCtx (for reflex stats)
#include "soma_dual.h"      // SomaDualCtx (for agreement stats)
#include "soma_smb.h"       // SomaSmbCtx (for memory hit stats)

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Fitness record (one per scored generation)
// ============================================================
typedef struct {
    float   score;              // Overall fitness [0, 1]
    float   confidence_contrib; // avg_confidence component
    float   reflex_contrib;     // reflex hit rate component
    float   agreement_contrib;  // core agreement rate
    float   memory_contrib;     // SMB hit rate
    int     generation;         // DNA generation number
    uint32_t dna_hash;          // Hash of evaluated DNA
} SomaMetaFitness;

// ============================================================
// Meta-Evolution context
// ============================================================
#define SOMA_META_HISTORY  8   // Keep last 8 fitness scores

typedef struct {
    SomaMetaFitness history[SOMA_META_HISTORY];
    int             history_head;
    int             history_count;

    float   best_score;         // Best fitness seen this session
    uint32_t best_dna_hash;     // DNA hash that produced best_score
    int     stagnation_count;   // Turns without improvement
    int     mutations_applied;
    int     total_evaluations;

    // Weights for fitness components
    float   w_confidence;  // Default: 0.40
    float   w_reflex;      // Default: 0.25
    float   w_agreement;   // Default: 0.20
    float   w_memory;      // Default: 0.15
} SomaMetaCtx;

// ============================================================
// API
// ============================================================

// Initialize meta-evolution context with default weights
void soma_meta_init(SomaMetaCtx *ctx);

// Score current DNA against live metrics.
// All stat structs may be NULL (contrib will be 0 for that component).
SomaMetaFitness soma_meta_score(SomaMetaCtx *ctx,
                                const SomaDNA *dna,
                                const SomaRouterCtx *router,
                                const SomaDualCtx *dual,
                                const SomaSmbCtx *smb);

// Evolve: compare new fitness to history. If stagnating, mutate DNA.
// Returns 1 if DNA was mutated, 0 if kept as-is.
// stagnation_threshold: how many evals without improvement triggers mutation.
int soma_meta_evolve(SomaMetaCtx *ctx,
                     SomaDNA *dna,
                     const SomaMetaFitness *fitness,
                     int stagnation_threshold);

// Full cycle: score + evolve in one call.
// Returns 1 if mutation was triggered.
int soma_meta_cycle(SomaMetaCtx *ctx,
                    SomaDNA *dna,
                    const SomaRouterCtx *router,
                    const SomaDualCtx *dual,
                    const SomaSmbCtx *smb,
                    int stagnation_threshold);

// Print fitness history
typedef void (*SomaMetaPrintFn)(const char *msg);
void soma_meta_print_stats(const SomaMetaCtx *ctx, SomaMetaPrintFn fn);

#ifdef __cplusplus
}
#endif
