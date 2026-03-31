#include "immunion.h"

void immunion_init(ImmunionEngine *e) {
    if (!e) return;
    e->mode = IMMUNION_MODE_OFF;
    e->patterns_recorded = 0;
    e->reactions_triggered = 0;
}

void immunion_set_mode(ImmunionEngine *e, ImmunionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void immunion_record(ImmunionEngine *e, ImmunionThreatType type, uint32_t pattern_hash, uint8_t severity) {
    if (!e) return;
    if (e->mode == IMMUNION_MODE_OFF) return;
    e->patterns_recorded++;
    (void)type;
    (void)pattern_hash;
    (void)severity;
}

uint8_t immunion_check(ImmunionEngine *e, ImmunionThreatType type, uint32_t pattern_hash) {
    if (!e) return 0;
    if (e->mode != IMMUNION_MODE_ACT) return 0;
    (void)type;
    (void)pattern_hash;
    return 0;
}

const char *immunion_mode_name_ascii(ImmunionMode mode) {
    switch (mode) {
        case IMMUNION_MODE_OFF:    return "off";
        case IMMUNION_MODE_RECORD: return "record";
        case IMMUNION_MODE_ACT:    return "act";
        default:                   return "?";
    }
}
