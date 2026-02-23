#include "symbion.h"

void symbion_init(SymbionEngine *e) {
    if (!e) return;
    e->mode = SYMBION_MODE_OFF;
    e->samples_taken = 0;
    e->adaptations_applied = 0;
}

void symbion_set_mode(SymbionEngine *e, SymbionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void symbion_feed(SymbionEngine *e, const SymbionSample *s) {
    if (!e || !s) return;
    if (e->mode == SYMBION_MODE_OFF) return;
    e->samples_taken++;
}

uint8_t symbion_suggest_throttle(const SymbionEngine *e) {
    if (!e || e->mode != SYMBION_MODE_ADAPT) return 0;
    return 0;
}

const char *symbion_mode_name_ascii(SymbionMode mode) {
    switch (mode) {
        case SYMBION_MODE_OFF:   return "off";
        case SYMBION_MODE_WATCH: return "watch";
        case SYMBION_MODE_ADAPT: return "adapt";
        default:                 return "?";
    }
}
