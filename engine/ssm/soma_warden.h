// soma_warden.h — SomaMind Phase M: Warden Pressure Signal Bridge
//
// Reads llmk_sentinel state (cycle budget, errors, tripped flag) and
// llmk_zones free space, computes a pressure level, and feeds it back
// into soma_router routing decisions.
//
// Pressure levels:
//   0  NONE     — all nominal
//   1  LOW      — light load; minor router adjustments
//   2  HIGH     — near budget / low memory; tighten thresholds
//   3  CRITICAL — sentinel tripped or OOM; force reflex-only mode
//
// Effect on routing (applied via soma_router_set_threshold):
//   NONE     → threshold = 0.85 (default)
//   LOW      → threshold = 0.80 (accept slightly lower confidence)
//   HIGH     → threshold = 0.70 (reflex preferred, external rarely)
//   CRITICAL → threshold = 0.20 (almost always reflex; external blocked)
//
// Additionally exposes pressure to cortex (safety override) and journal.
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_router.h"
#include "../../core/llmk_sentinel.h"
#include "../../core/llmk_zones.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Pressure levels
// ============================================================
#define SOMA_PRESSURE_NONE     0
#define SOMA_PRESSURE_LOW      1
#define SOMA_PRESSURE_HIGH     2
#define SOMA_PRESSURE_CRITICAL 3

// ============================================================
// Configuration thresholds
// ============================================================

// Fraction of decode budget used before we consider it HIGH pressure.
// e.g. 0.85 → if last_dt_cycles > 85% of max_cycles_decode → HIGH
#define SOMA_WARDEN_CYCLE_HIGH_FRAC     85  // percent

// Memory free threshold in MiB below which we set HIGH pressure.
// This checks the HOT zone (activations + KV cache).
#define SOMA_WARDEN_MEM_HIGH_MIB        64

// Number of consecutive violations before escalating one level.
#define SOMA_WARDEN_VIOLATIONS_ESCALATE 3

// ============================================================
// Context
// ============================================================
typedef struct {
    int  pressure_level;            // SOMA_PRESSURE_*
    int  pressure_prev;             // Level from previous update

    // Counters
    int  total_updates;
    int  violations_since_reset;    // Consecutive high/critical events
    int  escalation_count;          // How many times pressure escalated
    int  relief_count;              // How many times pressure decreased

    // Last sentinel snapshot
    int  last_sentinel_tripped;     // 1 if sentinel.tripped was set
    int  last_sentinel_error;       // LlmkError value
    UINT64 last_dt_cycles;          // Last measured decode cycles
    UINT64 last_budget_cycles;      // Budget at last update

    // Memory snapshot (MiB)
    UINT64 last_hot_free_mib;

    // Applied router threshold (overrides normal)
    float applied_threshold;        // The threshold we set last update

    // Turn counter (matches g_soma_turn or inference count)
    int  last_update_turn;

    // Cortex safety override (-1 = no override, else min safety score)
    int  cortex_safety_floor;       // Raised under pressure
} SomaWardenCtx;

// ============================================================
// API
// ============================================================

// Initialize warden context to safe defaults.
void soma_warden_init(SomaWardenCtx *w);

// Update warden: read sentinel + zones state, compute pressure,
// apply adjustments to router. Call once per inference turn.
// Returns new pressure level (SOMA_PRESSURE_*).
int soma_warden_update(SomaWardenCtx *w,
                       const LlmkSentinel *sentinel,
                       const LlmkZones    *zones,
                       SomaRouterCtx      *router,
                       int                 turn);

// Reset pressure to NONE (e.g. after /sentinel_reset).
void soma_warden_reset(SomaWardenCtx *w, SomaRouterCtx *router);

// Return a one-line ASCII status string (written into buf, max buflen).
// e.g. "NONE thresh=0.85 viol=0 mem=1024MiB"
int soma_warden_status_str(const SomaWardenCtx *w, char *buf, int buflen);

#ifdef __cplusplus
}
#endif
