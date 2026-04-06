// soma_dream.h — SomaMind Dreaming Engine
//
// "Dreaming" = offline consolidation of Synaptic Memory Bus records.
//
// After N turns (or on /soma_dream command), the engine scans all SMB
// slots and extracts patterns:
//   - Dominant domain (what kind of queries does OO handle most?)
//   - Average confidence across sessions
//   - Core preference (Solar vs Lunar) per domain
//   - Gist token frequency (what tokens does OO generate most?)
//
// A DreamSummary is produced and can be applied to the DNA to bias
// the next session's sampling parameters.
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_smb.h"    // SomaSmbCtx, SomaSmbSlot, SomaDomain
#include "soma_dna.h"    // SomaDNA

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Dream result — extracted patterns from SMB
// ============================================================
typedef struct {
    // Dominant patterns
    SomaDomain  dominant_domain;      // Most frequent domain in SMB
    int         domain_counts[6];     // Counts per SomaDomain value (0..5)
    float       avg_confidence;       // Mean confidence across all slots
    float       avg_relevance;        // Mean remaining relevance
    int         solar_count;          // Slots where Solar core was used
    int         lunar_count;          // Slots where Lunar core was used
    int         total_slots;          // Non-empty slots analyzed

    // Gist token frequency (top-3 most common first tokens)
    uint16_t    top_gist_tokens[3];
    int         top_gist_counts[3];

    // Recommended DNA bias adjustment (delta applied to DNA)
    float       recommended_bias_delta;   // +/- adjustment to cognition_bias
    float       recommended_temp_delta;   // +/- adjustment to temperature_solar

    // Meta
    int         dream_quality;   // 0-100: how much data was available
    uint32_t    dream_hash;      // FNV-1a of this dream state
} SomaDreamSummary;

// ============================================================
// Dreaming context (keeps history of last 4 dreams)
// ============================================================
#define SOMA_DREAM_HISTORY  4

typedef struct {
    SomaDreamSummary history[SOMA_DREAM_HISTORY];
    int              history_head;  // Ring write pointer
    int              history_count; // 0..SOMA_DREAM_HISTORY
    int              total_dreams;
    int              dna_adjustments; // Times DNA was updated from dream
} SomaDreamCtx;

// ============================================================
// API
// ============================================================

// Initialize dreaming context
void soma_dream_init(SomaDreamCtx *ctx);

// Run a dream cycle: scan SMB, produce DreamSummary.
// Does NOT modify DNA — call soma_dream_apply() for that.
SomaDreamSummary soma_dream_run(SomaDreamCtx *ctx, const SomaSmbCtx *smb);

// Apply dream recommendations to DNA (adjusts cognition_bias + temps).
// safe = 1: clamp changes to ±0.05 per dream (conservative).
// safe = 0: apply full recommended deltas.
void soma_dream_apply(SomaDreamCtx *ctx, SomaDNA *dna,
                      const SomaDreamSummary *dream, int safe);

// Check if a dream cycle is recommended (enough turns since last dream).
// Returns 1 if a dream should be triggered, 0 otherwise.
int soma_dream_should_run(const SomaDreamCtx *ctx,
                          const SomaSmbCtx *smb,
                          int min_turns_since_last);

// Print dream summary
typedef void (*SomaDreamPrintFn)(const char *msg);
void soma_dream_print(const SomaDreamSummary *dream, SomaDreamPrintFn fn);

#ifdef __cplusplus
}
#endif
