#include "metabion.h"

void metabion_init(MetabionEngine *e) {
    if (!e) return;
    e->mode = METABION_MODE_OFF;
    e->last.tokens_per_sec = 0;
    e->last.flops_per_watt = 0;
    e->last.cache_hit_milli = 0;
    e->last.mem_bandwidth_mb = 0;
    e->samples_count = 0;
}

void metabion_set_mode(MetabionEngine *e, MetabionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void metabion_feed(MetabionEngine *e, const MetabionSample *s) {
    if (!e || !s) return;
    if (e->mode == METABION_MODE_OFF) return;
    e->last = *s;
    e->samples_count++;
}

void metabion_get_last(MetabionEngine *e, MetabionSample *out) {
    if (!e || !out) return;
    *out = e->last;
}

const char *metabion_mode_name_ascii(MetabionMode mode) {
    switch (mode) {
        case METABION_MODE_OFF:   return "off";
        case METABION_MODE_TRACK: return "track";
        case METABION_MODE_GUIDE: return "guide";
        default:                  return "?";
    }
}
