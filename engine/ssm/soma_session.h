// soma_session.h — SomaMind Phase N: Session Fitness & DNA Evolution
//
// Tracks per-session quality metrics across all SomaMind subsystems
// and computes a fitness score that drives adaptive DNA mutation:
//
//   HIGH fitness (80-100) → small mutations (keep what works, 0.01)
//   MID  fitness (40-79)  → normal mutations (explore gently, 0.03-0.05)
//   LOW  fitness (0-39)   → large mutations (big change needed, 0.08-0.12)
//
// Also syncs runtime stats (router, cortex, warden) back into SomaDNA
// persistent counters (total_interactions, avg_confidence, escalations).
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_dna.h"
#include "soma_router.h"
#include "soma_warden.h"
#include "soma_cortex.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Session metrics
// ============================================================
typedef struct {
    // Turn counters
    int turns_total;            // Total inference turns this session
    int turns_reflex;           // Handled by reflex table
    int turns_internal;         // Handled by SomaMind internally
    int turns_external;         // Escalated to Mamba/LLaMA2

    // Quality signals
    int cortex_calls;           // Total cortex pre-routing calls
    int cortex_flagged;         // Cortex safety flags raised
    int warden_escalations;     // Warden pressure escalations
    int sentinel_trips;         // Sentinel budget/OOB trips
    int immunion_reactions;     // Immunion threat reactions (Phase P)

    // Running confidence average (fixed-point: x1000)
    int conf_sum_x1000;         // Sum of confidence * 1000
    int conf_count;             // Number of samples

    // Computed at score time
    int  fitness_score;         // 0-100
    float mutation_magnitude;   // 0.01-0.12
} SomaSessionCtx;

// ============================================================
// API
// ============================================================

// Initialize / reset session context (call at boot or after /session_reset)
void soma_session_init(SomaSessionCtx *s);

// Record one inference turn's outcome. Call after each turn.
// route: SOMA_ROUTE_* value
// confidence_x1000: confidence * 1000 (integer, e.g. 850 for 0.85)
// cortex_flagged: 1 if cortex raised safety flag this turn
// warden_escalated: 1 if warden escalated pressure this turn
void soma_session_record(SomaSessionCtx *s,
                         int route,
                         int confidence_x1000,
                         int cortex_flagged,
                         int warden_escalated);

// Compute fitness score (0-100) from accumulated session metrics.
// Stores result in s->fitness_score and s->mutation_magnitude.
int soma_session_score(SomaSessionCtx *s, const SomaWardenCtx *warden);

// Phase P: Record immunion reactions delta for this session.
// reactions_delta: new reactions since last call (from immunion.reactions_triggered delta).
// Penalty applied in soma_session_score(): -6 per reaction.
void soma_session_immunion_record(SomaSessionCtx *s, int reactions_delta);

// Sync session stats back into DNA persistent counters.
// Call before evolve or at session shutdown.
void soma_session_sync_dna(const SomaSessionCtx *s,
                           const SomaRouterCtx *router,
                           SomaDNA *dna);

// Evolve DNA using scored mutation magnitude.
// Uses soma_dna_mutate internally with rng.
// Returns new generation number.
int soma_session_evolve_dna(SomaSessionCtx *s,
                            SomaDNA *dna,
                            uint32_t *rng);

// Write status string into buf (max buflen chars).
// e.g. "turns=12 fit=74 mag=0.03 esc=1 flag=0"
int soma_session_status_str(const SomaSessionCtx *s, char *buf, int buflen);

#ifdef __cplusplus
}
#endif
