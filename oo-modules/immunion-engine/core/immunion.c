#include "immunion.h"

#define MAX_IMMUNION_PATTERNS 64

static ImmunionPattern s_patterns[MAX_IMMUNION_PATTERNS];
static uint32_t s_pattern_count = 0;

void immunion_init(ImmunionEngine *e) {
    if (!e) return;
    e->mode = IMMUNION_MODE_OFF;
    e->patterns_recorded = 0;
    e->reactions_triggered = 0;
    s_pattern_count = 0;
}

void immunion_set_mode(ImmunionEngine *e, ImmunionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void immunion_record(ImmunionEngine *e, ImmunionThreatType type, uint32_t pattern_hash, uint8_t severity) {
    if (!e) return;
    if (e->mode == IMMUNION_MODE_OFF) return;
    
    // Check if it already exists to avoid duplicates
    for (uint32_t i = 0; i < s_pattern_count; i++) {
        if (s_patterns[i].pattern_hash == pattern_hash && s_patterns[i].type == type) {
            if (severity > s_patterns[i].severity) {
                s_patterns[i].severity = severity; // Upgrade severity
            }
            return;
        }
    }
    
    if (s_pattern_count < MAX_IMMUNION_PATTERNS) {
        s_patterns[s_pattern_count].type = type;
        s_patterns[s_pattern_count].pattern_hash = pattern_hash;
        s_patterns[s_pattern_count].severity = severity;
        s_pattern_count++;
        e->patterns_recorded++;
    }
}

uint8_t immunion_check(ImmunionEngine *e, ImmunionThreatType type, uint32_t pattern_hash) {
    if (!e) return 0;
    if (e->mode != IMMUNION_MODE_ACT) return 0;
    
    for (uint32_t i = 0; i < s_pattern_count; i++) {
        if (s_patterns[i].pattern_hash == pattern_hash && s_patterns[i].type == type) {
            e->reactions_triggered++;
            return s_patterns[i].severity; // Return severity as reaction level
        }
    }
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
