/* desktop_display: bus-native HUD state that reuses OO innovations
 * from SomaMind, D+, Collectivion, Ghost-style eventing and UEFI REPL output.
 */
#include "module_desktop_display.h"
#include "oo-modules/module_registry.h"
#include "oo-bus/hermes/oo_bus_router.h"
#include "oo-kernel/hal/pmu.h"
#include "oo-engine/ssm/oo_somamind_v1.h"

extern SomaMindV1 g_somamind;

DesktopDisplayEngine g_desktop_display;

static void dd_zero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}

static void dd_copy_ascii(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

const char *desktop_display_node_state_name(uint32_t st) {
    switch ((OoDisplayNodeState)st) {
        case OO_DISPLAY_NODE_ACTIVE:    return "ACTIVE";
        case OO_DISPLAY_NODE_SYNCING:   return "SYNCING";
        case OO_DISPLAY_NODE_DEGRADED:  return "DEGRADED";
        case OO_DISPLAY_NODE_ISOLATED:  return "ISOLATED";
        case OO_DISPLAY_NODE_EMERGENCY: return "EMERGENCY";
        default:                        return "UNKNOWN";
    }
}

static uint32_t dd_derive_node_state(const OoBootCtx *ctx) {
    uint8_t pct = ctx->pressure.overall_pct;
    if (ctx->sentinel.tripped || pct >= 95) return OO_DISPLAY_NODE_EMERGENCY;
    if (ctx->pressure.deny_new_allocs || pct >= 80) return OO_DISPLAY_NODE_ISOLATED;
    if (pct >= 60) return OO_DISPLAY_NODE_DEGRADED;
    if (pct >= 30) return OO_DISPLAY_NODE_SYNCING;
    return OO_DISPLAY_NODE_ACTIVE;
}

static void dd_publish(DesktopDisplayEngine *eng) {
    OoBusDesktopDisplaySnapshot snap;
    snap.node_state        = eng->node_state;
    snap.pressure_pct      = eng->pressure_pct;
    snap.temp_celsius      = eng->temp_celsius;
    snap.splitbrain_phase  = eng->splitbrain_phase;
    snap.sentinel_tripped  = eng->sentinel_tripped;
    snap.dplus_denies      = eng->dplus_denies;
    snap.consensus_winner  = eng->consensus_winner;
    snap.divergence_score  = eng->divergence_score;
    snap.somamind_ready    = eng->somamind_ready;
    snap.render_generation = eng->render_generation;
    snap.publish_count     = eng->publish_count + 1u;
    snap.last_channel      = eng->last_channel;
    eng->publish_count     = snap.publish_count;
    oo_bus_post(OO_MOD_CHANNEL(OO_MOD_DESKTOP_DISPLAY), &snap, (uint32_t)sizeof(snap));
}

static void dd_sync_from_ctx(DesktopDisplayEngine *eng, OoBootCtx *ctx) {
    PmuSample s;
    pmu_sample(&ctx->pmu_state, &s);

    eng->node_state              = dd_derive_node_state(ctx);
    eng->pressure_pct            = ctx->pressure.overall_pct;
    eng->pressure_level          = ctx->pressure.level;
    eng->temp_scale              = ctx->pressure.temperature_scale;
    eng->deny_allocs             = ctx->pressure.deny_new_allocs;
    eng->splitbrain_phase        = ctx->splitbrain.phase;
    eng->sentinel_tripped        = ctx->sentinel.tripped ? 1u : 0u;
    eng->dplus_mode              = ctx->dplus_policy.mode;
    eng->dplus_evals             = ctx->dplus_stats.total_evals;
    eng->dplus_denies            = ctx->dplus_stats.deny_count;
    eng->dplus_audits            = ctx->dplus_stats.audit_count;
    eng->temp_celsius            = s.temp_celsius;
    eng->temp_valid              = s.temp_valid;
    eng->stress                  = s.stress;
    eng->somamind_ready          = g_somamind.initialized ? 1u : 0u;
    eng->somamind_tools          = (uint32_t)g_somamind.tools.n_tools;
    eng->somamind_last_halt      = (uint32_t)g_somamind.halt.last_halt;
    eng->somamind_tokens_generated = g_somamind.halt.tokens_generated;
    eng->somamind_tokens_saved   = g_somamind.total_tokens_saved;
}

void desktop_display_mod_init(void *e, const OoBootCtx *ctx) {
    DesktopDisplayEngine *eng = (DesktopDisplayEngine *)e;
    dd_zero(eng, (uint32_t)sizeof(*eng));
    eng->boot_phase = 6;
    eng->boot_ok = 1;
    eng->render_generation = 1;
    if (ctx) {
        eng->node_state       = dd_derive_node_state(ctx);
        eng->pressure_pct     = ctx->pressure.overall_pct;
        eng->pressure_level   = ctx->pressure.level;
        eng->temp_scale       = ctx->pressure.temperature_scale;
        eng->deny_allocs      = ctx->pressure.deny_new_allocs;
        eng->splitbrain_phase = ctx->splitbrain.phase;
        eng->sentinel_tripped = ctx->sentinel.tripped ? 1u : 0u;
        eng->dplus_mode       = ctx->dplus_policy.mode;
        eng->dplus_evals      = ctx->dplus_stats.total_evals;
        eng->dplus_denies     = ctx->dplus_stats.deny_count;
        eng->dplus_audits     = ctx->dplus_stats.audit_count;
    }
    eng->somamind_ready = g_somamind.initialized ? 1u : 0u;
    eng->somamind_tools = (uint32_t)g_somamind.tools.n_tools;
    dd_copy_ascii(eng->last_pattern, sizeof(eng->last_pattern), "boot");
    dd_publish(eng);
}

void desktop_display_mod_tick(void *e, OoBootCtx *ctx) {
    DesktopDisplayEngine *eng = (DesktopDisplayEngine *)e;
    if (!eng || !ctx) return;
    dd_sync_from_ctx(eng, ctx);
    eng->render_generation++;
    dd_publish(eng);
}

void desktop_display_mod_handle(void *e, uint16_t ch, const void *payload, uint32_t len) {
    DesktopDisplayEngine *eng = (DesktopDisplayEngine *)e;
    if (!eng) return;

    eng->last_channel = ch;

    switch (ch) {
        case OO_CH_BOOT_PHASE: {
            const OoBusBootPhase *bp = (const OoBusBootPhase *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusBootPhase)) return;
            eng->boot_phase = bp->phase;
            eng->boot_ok = bp->ok;
            break;
        }
        case OO_CH_MEM_PRESSURE: {
            const OoBusMemPressure *mp = (const OoBusMemPressure *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusMemPressure)) return;
            eng->pressure_pct = mp->overall_pct;
            eng->pressure_level = mp->level;
            eng->temp_scale = mp->temperature_scale;
            eng->deny_allocs = mp->deny_new_allocs;
            break;
        }
        case OO_CH_DPLUS_VERDICT: {
            const OoBusDplusVerdict *dv = (const OoBusDplusVerdict *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusDplusVerdict)) return;
            eng->last_dplus_verdict = dv->verdict;
            eng->policy_events++;
            dd_copy_ascii(eng->last_pattern, sizeof(eng->last_pattern), dv->pattern);
            break;
        }
        case OO_CH_SENTINEL_TRIP: {
            const OoBusSentinelTrip *trip = (const OoBusSentinelTrip *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusSentinelTrip)) return;
            eng->sentinel_tripped = 1;
            eng->sentinel_trip_count++;
            eng->last_trip_code = trip->error_code;
            break;
        }
        case OO_CH_INFERENCE_STEP: {
            const OoBusInferenceStep *step = (const OoBusInferenceStep *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusInferenceStep)) return;
            eng->inference_pos = step->pos;
            eng->tokens_generated = step->tokens_generated;
            eng->cycles_elapsed = step->cycles_elapsed;
            break;
        }
        case OO_CH_CONSENSUS: {
            const OoBusConsensus *c = (const OoBusConsensus *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusConsensus)) return;
            eng->swarm_events++;
            eng->consensus_winner = c->winner;
            eng->divergence_score = c->divergence_score;
            eng->last_output_len = c->output_len;
            break;
        }
        case OO_CH_IDLE_START: {
            const OoBusIdleStart *idle = (const OoBusIdleStart *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusIdleStart)) return;
            eng->idle = 1;
            eng->idle_turn = idle->turn;
            break;
        }
        case OO_CH_IDLE_END: {
            const OoBusIdleEnd *idle = (const OoBusIdleEnd *)payload;
            if (!payload || len < (uint32_t)sizeof(OoBusIdleEnd)) return;
            eng->idle = 0;
            eng->idle_turn = idle->turn;
            eng->last_input_len = idle->input_len;
            break;
        }
        case OO_MOD_CHANNEL(OO_MOD_DIOPION):
            eng->policy_events++;
            dd_copy_ascii(eng->last_pattern, sizeof(eng->last_pattern), "diopion-bridge");
            break;
        default:
            return;
    }

    eng->render_generation++;
    dd_publish(eng);
}
