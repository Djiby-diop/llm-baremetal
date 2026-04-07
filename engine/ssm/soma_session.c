// soma_session.c — SomaMind Phase N: Session Fitness & DNA Evolution
//
// Freestanding C11 — no libc, no malloc.

#include "soma_session.h"

// ─── Integer helpers ────────────────────────────────────────────────────────

static int ss_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float ss_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Integer square root (for confidence RMS — not used, but kept for future)
// static int ss_isqrt(int n) {
//     if (n <= 0) return 0;
//     int x = n, y = (n + 1) >> 1;
//     while (y < x) { x = y; y = (x + n/x) >> 1; }
//     return x;
// }

// String helpers for status
static int ss_strnlen(const char *s, int max) {
    int n = 0; while (n < max && s[n]) n++; return n;
}
static int ss_itoa(int v, char *buf, int pos, int cap) {
    if (pos >= cap - 2) return pos;
    if (v < 0) { if (pos < cap-1) buf[pos++] = '-'; v = -v; }
    if (v == 0) { if (pos < cap-1) buf[pos++] = '0'; return pos; }
    char tmp[12]; int n = 0;
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = n-1; i >= 0 && pos < cap-1; i--) buf[pos++] = tmp[i];
    return pos;
}
static int ss_ftoa2(float f, char *buf, int pos, int cap) {
    if (f < 0.0f) { if (pos < cap-1) buf[pos++] = '-'; f = -f; }
    int w = (int)f, fr = (int)((f - (float)w) * 100.0f + 0.5f);
    if (fr >= 100) { w++; fr -= 100; }
    pos = ss_itoa(w, buf, pos, cap);
    if (pos < cap-1) buf[pos++] = '.';
    if (pos < cap-1) buf[pos++] = (char)('0' + fr / 10);
    if (pos < cap-1) buf[pos++] = (char)('0' + fr % 10);
    return pos;
}
static int ss_puts(char *buf, int pos, int cap, const char *s) {
    int len = ss_strnlen(s, cap);
    for (int i = 0; i < len && pos < cap-1; i++) buf[pos++] = s[i];
    return pos;
}

// ─── soma_session_init ──────────────────────────────────────────────────────

void soma_session_init(SomaSessionCtx *s) {
    if (!s) return;
    s->turns_total        = 0;
    s->turns_reflex       = 0;
    s->turns_internal     = 0;
    s->turns_external     = 0;
    s->cortex_calls       = 0;
    s->cortex_flagged     = 0;
    s->warden_escalations = 0;
    s->sentinel_trips     = 0;
    s->immunion_reactions = 0;
    s->conf_sum_x1000     = 0;
    s->conf_count         = 0;
    s->fitness_score      = 50;
    s->mutation_magnitude = 0.05f;
}

// ─── soma_session_record ────────────────────────────────────────────────────

void soma_session_record(SomaSessionCtx *s,
                         int route,
                         int confidence_x1000,
                         int cortex_flag,
                         int warden_esc) {
    if (!s) return;

    s->turns_total++;

    // Route tracking
    switch (route) {
        case 0: s->turns_reflex++;    break;  // SOMA_ROUTE_REFLEX
        case 1: s->turns_internal++;  break;  // SOMA_ROUTE_INTERNAL
        case 2: s->turns_external++;  break;  // SOMA_ROUTE_EXTERNAL
        case 3:                               // SOMA_ROUTE_DUAL
            s->turns_internal++;
            s->turns_external++;
            break;
    }

    // Confidence running sum (clamped to sensible range)
    if (confidence_x1000 > 0) {
        int c = ss_clamp(confidence_x1000, 0, 1000);
        s->conf_sum_x1000 += c;
        s->conf_count++;
    }

    if (cortex_flag)  s->cortex_flagged++;
    if (warden_esc)   s->warden_escalations++;
}

// ─── soma_session_immunion_record ────────────────────────────────────────────

void soma_session_immunion_record(SomaSessionCtx *s, int reactions_delta) {
    if (!s || reactions_delta <= 0) return;
    s->immunion_reactions += reactions_delta;
}

// ─── soma_session_score ─────────────────────────────────────────────────────
//
// Fitness scoring rubric (0-100):
//
//   Base: 50
//   +2 per turn (productive session), cap +20 (10+ turns)
//   +3 per successful reflex (fast, confident)
//   +5 per internal turn  (SomaMind handled without escalation)
//   -5 per external escalation (needed big model = SomaMind insufficient)
//   -8 per cortex safety flag  (unsafe content encountered)
//  -12 per warden escalation   (pressure event)
//  -20 if sentinel tripped at any point
//   +10 if avg confidence > 0.80
//   -10 if avg confidence < 0.40
//
// Result: clamped [0, 100]

int soma_session_score(SomaSessionCtx *s, const SomaWardenCtx *warden) {
    if (!s) return 50;

    int score = 50;

    // Turn productivity bonus (up to +20)
    int turn_bonus = s->turns_total * 2;
    if (turn_bonus > 20) turn_bonus = 20;
    score += turn_bonus;

    // Reflex hits: fast, good
    score += s->turns_reflex * 3;
    if (score > 90) score = 90;  // cap partial bonus

    // Internal handling: SomaMind confident
    score += s->turns_internal * 5;
    if (score > 95) score = 95;

    // Escalations: penalty (SomaMind couldn't handle)
    score -= s->turns_external * 5;

    // Cortex safety flags: penalty
    score -= s->cortex_flagged * 8;

    // Warden pressure escalations: penalty
    score -= s->warden_escalations * 12;

    // Phase P: Immunion reactions: threat events detected
    score -= s->immunion_reactions * 6;

    // Sentinel trips
    if (warden && warden->last_sentinel_tripped)
        score -= 20;
    if (s->sentinel_trips > 0)
        score -= s->sentinel_trips * 20;

    // Confidence adjustment
    if (s->conf_count > 0) {
        int avg_conf = s->conf_sum_x1000 / s->conf_count;  // 0-1000
        if (avg_conf > 800)       score += 10;
        else if (avg_conf < 400)  score -= 10;
    }

    score = ss_clamp(score, 0, 100);
    s->fitness_score = score;

    // Mutation magnitude: inversely proportional to fitness
    // High fitness → keep it (small mutation)
    // Low fitness  → explore (large mutation)
    float mag;
    if (score >= 80)      mag = 0.01f;
    else if (score >= 60) mag = 0.03f;
    else if (score >= 40) mag = 0.05f;
    else if (score >= 20) mag = 0.08f;
    else                  mag = 0.12f;

    s->mutation_magnitude = mag;
    return score;
}

// ─── soma_session_sync_dna ──────────────────────────────────────────────────

void soma_session_sync_dna(const SomaSessionCtx *s,
                           const SomaRouterCtx *router,
                           SomaDNA *dna) {
    if (!s || !dna) return;

    // Increment total_interactions by turns this session
    dna->total_interactions += (uint32_t)s->turns_total;

    // Sync escalation counter
    dna->escalations += (uint32_t)s->turns_external;

    // Sync reflex / internal hits
    dna->successful_reflexes  += (uint32_t)s->turns_reflex;
    dna->successful_internals += (uint32_t)s->turns_internal;

    // Update running avg_confidence
    if (s->conf_count > 0) {
        float session_avg = (float)s->conf_sum_x1000 / (float)(s->conf_count * 1000);
        session_avg = ss_clampf(session_avg, 0.0f, 1.0f);

        if (dna->total_interactions <= (uint32_t)s->turns_total) {
            // First session: just set it
            dna->avg_confidence = session_avg;
        } else {
            // Exponential moving average: weight = 0.2 for new session
            dna->avg_confidence = ss_clampf(
                dna->avg_confidence * 0.8f + session_avg * 0.2f,
                0.0f, 1.0f);
        }
    }

    // Sync router confidence threshold back to DNA
    if (router) {
        dna->confidence_threshold = ss_clampf(
            router->confidence_threshold, 0.3f, 0.99f);
    }

    // Update domain_mask from router readiness
    if (router) {
        // Keep existing mask; enable all if external model was available
        if (router->external_model_ready)
            dna->domain_mask |= 0x3F;
    }
}

// ─── soma_session_evolve_dna ────────────────────────────────────────────────

int soma_session_evolve_dna(SomaSessionCtx *s,
                            SomaDNA *dna,
                            uint32_t *rng) {
    if (!s || !dna || !rng) return -1;

    // Ensure score is fresh
    soma_session_score(s, NULL);

    // Sync stats first
    soma_session_sync_dna(s, NULL, dna);

    // Evolve with scored magnitude
    soma_dna_mutate(dna, rng, s->mutation_magnitude);

    return (int)dna->generation;
}

// ─── soma_session_status_str ────────────────────────────────────────────────

int soma_session_status_str(const SomaSessionCtx *s, char *buf, int buflen) {
    if (!s || !buf || buflen < 4) return 0;
    int p = 0;
    p = ss_puts(buf, p, buflen, "turns=");
    p = ss_itoa(s->turns_total, buf, p, buflen);
    p = ss_puts(buf, p, buflen, " fit=");
    p = ss_itoa(s->fitness_score, buf, p, buflen);
    p = ss_puts(buf, p, buflen, " mag=");
    p = ss_ftoa2(s->mutation_magnitude, buf, p, buflen);
    p = ss_puts(buf, p, buflen, " esc=");
    p = ss_itoa(s->warden_escalations, buf, p, buflen);
    p = ss_puts(buf, p, buflen, " flag=");
    p = ss_itoa(s->cortex_flagged, buf, p, buflen);
    p = ss_puts(buf, p, buflen, " reflex=");
    p = ss_itoa(s->turns_reflex, buf, p, buflen);
    if (s->immunion_reactions > 0) {
        p = ss_puts(buf, p, buflen, " imm=");
        p = ss_itoa(s->immunion_reactions, buf, p, buflen);
    }
    if (s->conf_count > 0) {
        int avg = s->conf_sum_x1000 / s->conf_count;  // 0-1000
        p = ss_puts(buf, p, buflen, " conf=0.");
        p = ss_itoa(avg / 10, buf, p, buflen);  // 2 digits (0-99)
    }
    if (p < buflen) buf[p] = '\0';
    return p;
}
