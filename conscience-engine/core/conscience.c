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
    out->temp_raw = 0;
    out->power_raw = 0;
    out->stress = 0;
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
