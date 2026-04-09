/*
 * Conscience: Particule de Conscience - core implementation.
 * Best-effort: UEFI has no standard temp API. Stub returns zero stress.
 */

#include "conscience.h"

void conscience_init(ConscienceEngine *e) {
    if (!e) return;
    e->mode = CONSCIENCE_MODE_OFF;
    e->current_precision = CONSCIENCE_PREC_F32;
    e->samples_taken = 0;
    e->downgrades_triggered = 0;
}

void conscience_set_mode(ConscienceEngine *e, ConscienceMode mode) {
    if (!e) return;
    e->mode = mode;
}

void conscience_sample(ConscienceEngine *e, ConscienceSample *out) {
    if (!e || !out) return;
    /* UEFI has no standard thermal API. Use RDTSC jitter as stress proxy:
     * measure two consecutive RDTSC reads — high jitter = thermal throttling. */
    unsigned long long t0, t1;
    __asm__ volatile("rdtsc" : "=A"(t0));
    /* 256 lightweight iterations to amplify jitter signal */
    volatile unsigned int sink = 0;
    for (int i = 0; i < 256; i++) sink += (unsigned int)i;
    __asm__ volatile("rdtsc" : "=A"(t1));
    (void)sink;
    unsigned long long delta = t1 - t0;
    /* Normalize: ~200 cycles = idle, ~4000 cycles = heavy throttle */
    uint32_t stress = (delta > 4000ULL) ? 100u :
                      (uint32_t)((delta * 100ULL) / 4000ULL);
    out->temp_raw  = (uint32_t)(delta & 0xFFFFu);  /* raw jitter for logging */
    out->power_raw = 0;
    out->stress    = stress;
    if (e->mode != CONSCIENCE_MODE_OFF)
        e->samples_taken++;
}

ConsciencePrecision conscience_recommend_precision(const ConscienceEngine *e, uint32_t stress) {
    if (!e) return CONSCIENCE_PREC_F32;
    if (stress >= 90) return CONSCIENCE_PREC_Q4;
    if (stress >= 70) return CONSCIENCE_PREC_Q8;
    if (stress >= 50) return CONSCIENCE_PREC_F16;
    return CONSCIENCE_PREC_F32;
}

const char *conscience_mode_name_ascii(ConscienceMode mode) {
    switch (mode) {
        case CONSCIENCE_MODE_OFF:   return "off";
        case CONSCIENCE_MODE_WATCH: return "watch";
        case CONSCIENCE_MODE_ACT:   return "act";
        default:                    return "?";
    }
}
