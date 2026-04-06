// soma_router.h — SomaMind Confidence-Gated Router
//
// Routes input to the appropriate engine:
//   - Reflex arc (instant, no inference)
//   - SomaMind internal (fast, 16M)
//   - External battery (slow, 2.8B Mamba / LLaMA2)
//
// Freestanding C11 — no libc.

#pragma once

#include "ssm_infer.h"  // ssm_f32

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Routing decision
// ============================================================
typedef enum {
    SOMA_ROUTE_REFLEX   = 0,  // Instant response from reflex table
    SOMA_ROUTE_INTERNAL = 1,  // SomaMind handles it (fast, confident)
    SOMA_ROUTE_EXTERNAL = 2,  // Escalate to external battery (Mamba/LLaMA2)
    SOMA_ROUTE_DUAL     = 3,  // Both: speculative decoding pipeline
} SomaRoute;

// ============================================================
// Domain classification
// ============================================================
typedef enum {
    SOMA_DOMAIN_UNKNOWN = 0,
    SOMA_DOMAIN_SYSTEM  = 1,  // Boot, recovery, OO internals
    SOMA_DOMAIN_POLICY  = 2,  // D+, safety, pressure
    SOMA_DOMAIN_CHAT    = 3,  // General conversation
    SOMA_DOMAIN_CODE    = 4,  // Programming, technical
    SOMA_DOMAIN_MATH    = 5,  // Arithmetic, reasoning
    SOMA_DOMAIN_CREATIVE = 6, // Stories, ideas, exploration
} SomaDomain;

// ============================================================
// Routing context (persistent across session)
// ============================================================
typedef struct {
    float confidence_threshold;  // Below this → escalate (default 0.85)
    float reflex_hit_rate;       // Stats: % of inputs handled by reflex
    int   total_routed;
    int   reflex_count;
    int   internal_count;
    int   external_count;
    int   soma_model_ready;      // 1 if SomaMind model is loaded
    int   external_model_ready;  // 1 if Mamba/LLaMA2 is loaded
} SomaRouterCtx;

// ============================================================
// Routing result
// ============================================================
typedef struct {
    SomaRoute  route;
    SomaDomain domain;
    float      confidence;      // Pre-inference estimate (from reflex/keyword)
    const char *reflex_response; // Non-NULL if SOMA_ROUTE_REFLEX
    int        reflex_response_len;
} SomaRouteResult;

// ============================================================
// API
// ============================================================

// Initialize router with defaults
void soma_router_init(SomaRouterCtx *ctx);

// Classify domain from raw input text
SomaDomain soma_classify_domain(const char *input, int len);

// Compute routing decision
SomaRouteResult soma_route(SomaRouterCtx *ctx, const char *input, int len);

// Update confidence threshold (live tuning)
void soma_router_set_threshold(SomaRouterCtx *ctx, float threshold);

// Post-inference feedback: was the routing correct?
// Call after generation to update stats and reflex table.
void soma_router_feedback(SomaRouterCtx *ctx, SomaRoute route_used,
                          float actual_confidence);

// Print router stats
typedef void (*SomaPrintFn)(const char *msg);
void soma_router_print_stats(const SomaRouterCtx *ctx, SomaPrintFn fn);

#ifdef __cplusplus
}
#endif
