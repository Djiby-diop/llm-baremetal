#pragma once
/**
 * oo_thalamic_bridge.h
 * Thalamic-Bloom Cortex Bridge — bare-metal integration layer
 *
 * Extends oo_bridge.h with:
 *   - D+ Policy Gate (5 Organic Laws — hardcoded from Phase 5b Engram Burn)
 *   - MIMO arm telemetry (reports active arm to united-bus)
 *   - Thalamic primer signal (routing_delta metric)
 *
 * Model: Mamba3MIMORLF 150M — batteryphil/thalamic-bloom
 * Export: engine/ssm/export_thalamic_bloom.py → thalamic_bloom.mamb
 * Load:   /ssm_load thalamic_bloom.mamb  (in OO REPL)
 *
 * Usage:
 *   OoThalamusCtx ctx;
 *   oo_thalamus_init(&ctx, &ssm_ctx);
 *   OoDplusVerdict v = oo_thalamus_generate(&ctx, prompt, out, sizeof(out), 128);
 *   if (v == OO_DPLUS_ALLOW) display(out);
 */

#include <stdint.h>
#include "ssm_infer.h"
#include "oo_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * D+ Policy Engine — 5 Organic Laws (Phase 5b Engram Burn)
 * Thresholds match oo_emulator.py from thalamic-bloom repo
 * ================================================================ */

typedef struct {
    float harm;          /* 0.0 – 1.0, estimated harm score    */
    float benefit;       /* 0.0 – 1.0, estimated benefit score */
    float reversibility; /* 0.0 – 1.0, 1.0 = fully reversible */
    int   has_reason;    /* 1 = action has a stated reason     */
    int   self_modifying;/* 1 = action touches own code/weights*/
} OoDplusAction;

typedef enum {
    OO_DPLUS_ALLOW      = 0,  /* action is permitted            */
    OO_DPLUS_FORBID     = 1,  /* outright forbidden             */
    OO_DPLUS_QUARANTINE = 2,  /* isolated, needs human review   */
    OO_DPLUS_COMPENSATE = 3,  /* allowed but rollback required  */
} OoDplusVerdict;

/**
 * Evaluate an action against the 5 Organic Laws.
 * Called automatically inside oo_thalamus_generate() before output.
 */
static inline OoDplusVerdict oo_dplus_eval(const OoDplusAction *a)
{
    /* Law 1 — Non-Harm */
    if (a->harm > 0.70f)
        return OO_DPLUS_FORBID;

    /* Law 2 — Transparency */
    if (!a->has_reason)
        return OO_DPLUS_FORBID;

    /* Law 3 — Reversibility */
    if (a->reversibility < 0.40f)
        return OO_DPLUS_COMPENSATE;

    /* Law 4 — Dignity (self-modifying code) */
    if (a->self_modifying && a->harm > 0.30f)
        return OO_DPLUS_QUARANTINE;

    /* Law 0 — Common Good (benefit too low) */
    if (a->benefit < 0.10f)
        return OO_DPLUS_QUARANTINE;

    return OO_DPLUS_ALLOW;
}

/* ================================================================
 * Thalamic cortex context
 * ================================================================ */

#define OO_THALAMUS_OUT_MAX    512  /* max generated chars            */
#define OO_THALAMUS_ARM_COUNT    4  /* MIMO arms (0=general/OO, ...) */

typedef struct {
    SsmCtx      *ssm;           /* underlying inference engine     */

    /* MIMO telemetry (populated after each generate call) */
    int          active_arm;    /* arm selected by domain router   */
    float        primer_delta;  /* thalamic primer angular signal  */
    float        arm_energy[OO_THALAMUS_ARM_COUNT]; /* per-arm logit energy */

    /* D+ gate — default action profile (tunable at runtime) */
    OoDplusAction default_action;

    /* Stats */
    uint32_t     tokens_total;
    uint32_t     dplus_forbids;
    uint32_t     dplus_quarantines;
    uint32_t     dplus_compensates;
} OoThalamusCtx;

/* ================================================================
 * API
 * ================================================================ */

/**
 * Initialize the thalamic bridge context.
 * ssm must already be initialized via ssm_ctx_init().
 */
static inline void oo_thalamus_init(OoThalamusCtx *ctx, SsmCtx *ssm)
{
    uint8_t *p = (uint8_t *)ctx;
    for (uint32_t i = 0; i < sizeof(OoThalamusCtx); i++) p[i] = 0;

    ctx->ssm = ssm;

    /* default action profile: safe, general-purpose, reversible */
    ctx->default_action.harm          = 0.05f;
    ctx->default_action.benefit       = 0.80f;
    ctx->default_action.reversibility = 0.90f;
    ctx->default_action.has_reason    = 1;
    ctx->default_action.self_modifying= 0;
}

/**
 * Generate text from prompt through the thalamic cortex.
 * Applies D+ gate on every completed thought (oo_bridge_parse_thought).
 *
 * @param ctx        thalamic context (must be initialized)
 * @param prompt     null-terminated input string
 * @param out        output buffer
 * @param out_size   size of output buffer
 * @param max_tokens maximum tokens to generate
 * @return           D+ verdict for the overall generation
 */
OoDplusVerdict oo_thalamus_generate(
    OoThalamusCtx *ctx,
    const char    *prompt,
    char          *out,
    uint32_t       out_size,
    uint32_t       max_tokens
);

/**
 * Report thalamic telemetry to united-bus.
 * Call after oo_thalamus_generate() for monitoring.
 */
void oo_thalamus_report(const OoThalamusCtx *ctx);

/**
 * Reset recurrent state (start new conversation turn).
 */
static inline void oo_thalamus_reset(OoThalamusCtx *ctx)
{
    ssm_ctx_reset(ctx->ssm);
    ctx->active_arm   = 0;
    ctx->primer_delta = 0.0f;
    for (int i = 0; i < OO_THALAMUS_ARM_COUNT; i++)
        ctx->arm_energy[i] = 0.0f;
}

/**
 * Verdict string for logging/journal.
 */
static inline const char *oo_dplus_verdict_str(OoDplusVerdict v)
{
    switch (v) {
    case OO_DPLUS_ALLOW:      return "ALLOW";
    case OO_DPLUS_FORBID:     return "FORBID";
    case OO_DPLUS_QUARANTINE: return "QUARANTINE";
    case OO_DPLUS_COMPENSATE: return "COMPENSATE";
    default:                  return "UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif
