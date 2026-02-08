#include "memorion.h"

void memorion_init(MemorionEngine *e) {
    if (!e) return;
    e->mode = MEMORION_MODE_ON;
    e->manifests_written = 0;
    e->checks_done = 0;
}

void memorion_set_mode(MemorionEngine *e, MemorionMode mode) {
    if (!e) return;
    e->mode = mode;
}

const char *memorion_mode_name_ascii(MemorionMode mode) {
    switch (mode) {
        case MEMORION_MODE_OFF: return "off";
        case MEMORION_MODE_ON:  return "on";
        default:                return "?";
    }
}
