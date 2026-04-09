// soma_warden.c — SomaMind Phase M: Warden Pressure Signal Bridge
//
// Freestanding C11 — no libc, no malloc.

#include "soma_warden.h"

// ─── Helpers ────────────────────────────────────────────────────────────────

static int w_strnlen(const char *s, int max) {
    int n = 0; while (n < max && s[n]) n++; return n;
}

// Write integer into buf at pos, return new pos
static int w_itoa(int v, char *buf, int pos, int cap) {
    if (pos >= cap - 2) return pos;
    if (v < 0) { if (pos < cap-1) buf[pos++] = '-'; v = -v; }
    if (v == 0) { if (pos < cap-1) buf[pos++] = '0'; return pos; }
    char tmp[12]; int n = 0;
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = n-1; i >= 0 && pos < cap-1; i--) buf[pos++] = tmp[i];
    return pos;
}

// Write UINT64 decimal (simplified — max ~18 digits)
static int w_u64toa(UINT64 v, char *buf, int pos, int cap) {
    if (v == 0) { if (pos < cap-1) buf[pos++] = '0'; return pos; }
    char tmp[20]; int n = 0;
    while (v > 0 && n < 19) { tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for (int i = n-1; i >= 0 && pos < cap-1; i--) buf[pos++] = tmp[i];
    return pos;
}

// Write float as "N.NN" (2 decimal places)
static int w_ftoa2(float f, char *buf, int pos, int cap) {
    if (f < 0.0f) { if (pos < cap-1) buf[pos++] = '-'; f = -f; }
    int whole = (int)f;
    int frac  = (int)((f - (float)whole) * 100.0f + 0.5f);
    if (frac >= 100) { whole++; frac -= 100; }
    pos = w_itoa(whole, buf, pos, cap);
    if (pos < cap-1) buf[pos++] = '.';
    // Two digits
    if (pos < cap-1) buf[pos++] = (char)('0' + frac / 10);
    if (pos < cap-1) buf[pos++] = (char)('0' + frac % 10);
    return pos;
}

static int w_puts(char *buf, int pos, int cap, const char *s) {
    int len = w_strnlen(s, cap);
    for (int i = 0; i < len && pos < cap-1; i++) buf[pos++] = s[i];
    return pos;
}

// ─── Router thresholds per pressure level ───────────────────────────────────

static float pressure_to_threshold(int level) {
    switch (level) {
        case SOMA_PRESSURE_NONE:     return 0.85f;
        case SOMA_PRESSURE_LOW:      return 0.80f;
        case SOMA_PRESSURE_HIGH:     return 0.70f;
        case SOMA_PRESSURE_CRITICAL: return 0.20f;
        default:                     return 0.85f;
    }
}

// Cortex safety floor per pressure level (minimum accepted safety score)
static int pressure_to_safety_floor(int level) {
    switch (level) {
        case SOMA_PRESSURE_NONE:     return 0;
        case SOMA_PRESSURE_LOW:      return 10;
        case SOMA_PRESSURE_HIGH:     return 30;
        case SOMA_PRESSURE_CRITICAL: return 50;
        default:                     return 0;
    }
}

// ─── soma_warden_init ────────────────────────────────────────────────────────

void soma_warden_init(SomaWardenCtx *w) {
    if (!w) return;
    w->pressure_level          = SOMA_PRESSURE_NONE;
    w->pressure_prev           = SOMA_PRESSURE_NONE;
    w->total_updates           = 0;
    w->violations_since_reset  = 0;
    w->escalation_count        = 0;
    w->relief_count            = 0;
    w->last_sentinel_tripped   = 0;
    w->last_sentinel_error     = 0;
    w->last_dt_cycles          = 0;
    w->last_budget_cycles      = 0;
    w->last_hot_free_mib       = 0;
    w->applied_threshold       = 0.85f;
    w->last_update_turn        = 0;
    w->cortex_safety_floor     = 0;
    w->last_immunion_reactions = 0;
    w->immunion_escalations    = 0;
    // Phase I: D+ gate
    oo_dplus_gate_init(&w->dplus);
    w->dplus_tok_s      = 0;
    w->dplus_resonance  = 0;
    w->emergency_halt   = 0;
}

// ─── soma_warden_update ──────────────────────────────────────────────────────

int soma_warden_update(SomaWardenCtx *w,
                       const LlmkSentinel *sentinel,
                       const LlmkZones    *zones,
                       SomaRouterCtx      *router,
                       int                 turn) {
    if (!w) return SOMA_PRESSURE_NONE;

    w->total_updates++;
    w->last_update_turn = turn;
    w->pressure_prev    = w->pressure_level;

    // ── 1. Assess sentinel state ─────────────────────────────────────────
    int new_level = SOMA_PRESSURE_NONE;

    if (sentinel) {
        w->last_sentinel_tripped = sentinel->tripped ? 1 : 0;
        w->last_sentinel_error   = (int)sentinel->last_error;
        w->last_dt_cycles        = sentinel->last_dt_cycles;
        w->last_budget_cycles    = sentinel->last_budget_cycles;

        // Tripped → CRITICAL unconditionally
        if (sentinel->tripped) {
            new_level = SOMA_PRESSURE_CRITICAL;
        }
        // Budget exceeded (error=BUDGET) → CRITICAL
        else if (sentinel->last_error == LLMK_ERR_BUDGET) {
            new_level = SOMA_PRESSURE_CRITICAL;
        }
        // Decode cycles > SOMA_WARDEN_CYCLE_HIGH_FRAC% of budget → HIGH
        else if (sentinel->cfg.max_cycles_decode > 0 &&
                 sentinel->last_dt_cycles > 0) {
            UINT64 pct = (sentinel->last_dt_cycles * 100ULL) /
                         sentinel->cfg.max_cycles_decode;
            if (pct >= 100) {
                new_level = SOMA_PRESSURE_CRITICAL;
            } else if (pct >= SOMA_WARDEN_CYCLE_HIGH_FRAC) {
                if (new_level < SOMA_PRESSURE_HIGH)
                    new_level = SOMA_PRESSURE_HIGH;
            } else if (pct >= 60) {
                if (new_level < SOMA_PRESSURE_LOW)
                    new_level = SOMA_PRESSURE_LOW;
            }
        }
        // Allocation error → HIGH
        if (sentinel->last_error == LLMK_ERR_ALLOC &&
                new_level < SOMA_PRESSURE_HIGH) {
            new_level = SOMA_PRESSURE_HIGH;
        }
    }

    // ── 2. Assess memory pressure ────────────────────────────────────────
    if (zones && new_level < SOMA_PRESSURE_CRITICAL) {
        // Check HOT zone free space (arena index 1 = LLMK_ARENA_ACTIVATIONS)
        // We use a simple heuristic: compare used vs capacity
        // llmk_zones exposes: zones->arenas[N].used, zones->arenas[N].capacity
        UINT64 hot_used     = 0;
        UINT64 hot_capacity = 0;
        // Arena 1 = HOT (activations), arena 3 = SCRATCH
        for (int ai = 1; ai <= 3; ai++) {
            if (ai < LLMK_ARENA_COUNT) {
                hot_used     += zones->arenas[ai].cursor;
                hot_capacity += zones->arenas[ai].size;
            }
        }
        if (hot_capacity > 0) {
            UINT64 hot_free_bytes = (hot_used < hot_capacity)
                                    ? (hot_capacity - hot_used) : 0;
            UINT64 hot_free_mib   = hot_free_bytes / (1024ULL * 1024ULL);
            w->last_hot_free_mib  = hot_free_mib;

            if (hot_free_mib < (SOMA_WARDEN_MEM_HIGH_MIB / 4)) {
                // < 16 MiB → CRITICAL
                if (new_level < SOMA_PRESSURE_CRITICAL)
                    new_level = SOMA_PRESSURE_CRITICAL;
            } else if (hot_free_mib < SOMA_WARDEN_MEM_HIGH_MIB) {
                // < 64 MiB → HIGH
                if (new_level < SOMA_PRESSURE_HIGH)
                    new_level = SOMA_PRESSURE_HIGH;
            }
        }
    }

    // ── 3. Violation hysteresis ──────────────────────────────────────────
    // Don't snap immediately to NONE — require some calm turns
    if (new_level >= SOMA_PRESSURE_HIGH) {
        w->violations_since_reset++;
    } else if (new_level == SOMA_PRESSURE_NONE &&
               w->pressure_level >= SOMA_PRESSURE_HIGH) {
        // Gradual relief: stay at LOW for one more update
        if (w->violations_since_reset > 0) {
            w->violations_since_reset--;
            new_level = SOMA_PRESSURE_LOW;
        }
    } else {
        // Mild: decay violations slowly
        if (w->violations_since_reset > 0 &&
                (turn % 4 == 0))
            w->violations_since_reset--;
    }

    // ── 4. Track escalation / relief ────────────────────────────────────
    if (new_level > w->pressure_level)
        w->escalation_count++;
    else if (new_level < w->pressure_level)
        w->relief_count++;

    w->pressure_level      = new_level;
    w->cortex_safety_floor = pressure_to_safety_floor(new_level);

    // ── 5. Apply to router ───────────────────────────────────────────────
    if (router) {
        float thr = pressure_to_threshold(new_level);
        w->applied_threshold = thr;
        soma_router_set_threshold(router, thr);

        // Under CRITICAL: also disable external model routing
        // by temporarily marking it unavailable
        if (new_level == SOMA_PRESSURE_CRITICAL) {
            router->external_model_ready = 0;
        }
        // Restore external model readiness when pressure drops
        // (actual model readiness is re-checked each inference cycle
        //  by the god file, so this is a one-turn suppression only)
    }

    // ── 6. D+ Live Gate evaluation ───────────────────────────────────────
    {
        int mem_mib = (int)(w->last_hot_free_mib);
        DPlusVerdict dv = oo_dplus_gate_evaluate(
            &w->dplus,
            new_level,                  // pressure
            w->last_sentinel_tripped,   // sentinel tripped
            mem_mib,                    // free MiB
            w->dplus_tok_s,             // tok/s
            w->dplus_resonance,         // resonance score
            turn
        );

        // D+ EMERGENCY: set halt flag + tighten router to near-zero
        if (dv == DPLUS_EMERGENCY && !w->emergency_halt) {
            w->emergency_halt = 1;
            if (router) {
                soma_router_set_threshold(router, 0.05f);
                router->external_model_ready = 0;
            }
        }
        // D+ QUARANTINE or above: block external model if not already
        else if (dv >= DPLUS_QUARANTINE && router) {
            router->external_model_ready = 0;
        }
        // D+ relief — clear emergency halt if verdict recovered
        if (dv < DPLUS_FORBID && w->emergency_halt) {
            w->emergency_halt = 0;
        }
    }

    return new_level;
}

// ─── soma_warden_reset ───────────────────────────────────────────────────────

void soma_warden_reset(SomaWardenCtx *w, SomaRouterCtx *router) {
    if (!w) return;
    w->pressure_level         = SOMA_PRESSURE_NONE;
    w->pressure_prev          = SOMA_PRESSURE_NONE;
    w->violations_since_reset = 0;
    w->last_sentinel_tripped  = 0;
    w->last_sentinel_error    = 0;
    w->cortex_safety_floor    = 0;
    w->applied_threshold      = 0.85f;
    if (router) {
        soma_router_set_threshold(router, 0.85f);
    }
}

// ─── soma_warden_immunion_sync ───────────────────────────────────────────────
//
// Phase P: bridge ImmunionEngine reactions into warden pressure.
// Each new immunion reaction counts as a violation. If any reactions
// occurred, pressure is raised to at least HIGH immediately (no hysteresis)
// because a triggered immune reaction indicates a real threat event.

int soma_warden_immunion_sync(SomaWardenCtx *w,
                              const ImmunionEngine *imm,
                              SomaRouterCtx *router,
                              int turn) {
    if (!w || !imm) return 0;

    int snapshot = (int)imm->reactions_triggered;
    int new_reactions = snapshot - w->last_immunion_reactions;
    if (new_reactions <= 0) return 0;

    w->last_immunion_reactions = snapshot;
    w->immunion_escalations   += new_reactions;

    // Count each reaction as a warden violation
    w->violations_since_reset += new_reactions;

    // Ensure at least HIGH pressure — bypass normal hysteresis
    if (w->pressure_level < SOMA_PRESSURE_HIGH) {
        w->pressure_level      = SOMA_PRESSURE_HIGH;
        w->escalation_count++;
        w->cortex_safety_floor = pressure_to_safety_floor(SOMA_PRESSURE_HIGH);
        if (router) {
            float thr = pressure_to_threshold(SOMA_PRESSURE_HIGH);
            w->applied_threshold = thr;
            soma_router_set_threshold(router, thr);
        }
    }

    (void)turn;
    return new_reactions;
}

// ─── soma_warden_status_str ──────────────────────────────────────────────────

static const char *pressure_name(int level) {
    switch (level) {
        case SOMA_PRESSURE_NONE:     return "NONE";
        case SOMA_PRESSURE_LOW:      return "LOW";
        case SOMA_PRESSURE_HIGH:     return "HIGH";
        case SOMA_PRESSURE_CRITICAL: return "CRITICAL";
        default:                     return "?";
    }
}

int soma_warden_status_str(const SomaWardenCtx *w, char *buf, int buflen) {
    if (!w || !buf || buflen < 4) return 0;
    int p = 0;
    p = w_puts(buf, p, buflen, pressure_name(w->pressure_level));
    p = w_puts(buf, p, buflen, " thresh=");
    p = w_ftoa2(w->applied_threshold, buf, p, buflen);
    p = w_puts(buf, p, buflen, " viol=");
    p = w_itoa(w->violations_since_reset, buf, p, buflen);
    p = w_puts(buf, p, buflen, " esc=");
    p = w_itoa(w->escalation_count, buf, p, buflen);
    p = w_puts(buf, p, buflen, " mem=");
    p = w_u64toa(w->last_hot_free_mib, buf, p, buflen);
    p = w_puts(buf, p, buflen, "MiB");
    if (w->last_sentinel_tripped) {
        p = w_puts(buf, p, buflen, " [TRIPPED]");
    }
    if (p < buflen) buf[p] = '\0';
    return p;
}

// ─── soma_warden_set_dplus_inputs ────────────────────────────────────────────

void soma_warden_set_dplus_inputs(SomaWardenCtx *w, int tok_s, int resonance) {
    if (!w) return;
    w->dplus_tok_s     = tok_s;
    w->dplus_resonance = resonance;
}

// ─── soma_warden_dplus_status_str ────────────────────────────────────────────

int soma_warden_dplus_status_str(const SomaWardenCtx *w, char *buf, int buflen) {
    if (!w || !buf || buflen < 4) return 0;
    int n = oo_dplus_gate_status_str(&w->dplus, buf, buflen);
    // Append emergency halt flag if set
    if (w->emergency_halt && n < buflen - 16) {
        n = w_puts(buf, n, buflen, " [EMERGENCY_HALT]");
    }
    if (n < buflen) buf[n] = '\0';
    return n;
}

// ─── soma_warden_dplus_reset ─────────────────────────────────────────────────

void soma_warden_dplus_reset(SomaWardenCtx *w, SomaRouterCtx *router) {
    if (!w) return;
    oo_dplus_gate_reset(&w->dplus);
    w->emergency_halt = 0;
    // Restore router threshold to NONE level
    if (router) {
        float thr = pressure_to_threshold(SOMA_PRESSURE_NONE);
        w->applied_threshold = thr;
        soma_router_set_threshold(router, thr);
    }
}
