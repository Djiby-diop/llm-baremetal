/*
 * Synaption: Synaptic Memory - core implementation.
 * Stub: tracks blocks and access hints. Actual pinning by main binary.
 */

#include "synaption.h"

void synaption_init(SynaptionEngine *e) {
    if (!e) return;
    e->mode = SYNAPTION_MODE_OFF;
    e->blocks_tracked = 0;
    e->promotions = 0;
}

void synaption_set_mode(SynaptionEngine *e, SynaptionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void synaption_register(SynaptionEngine *e, void *base, uint64_t size, SynaptionTier tier) {
    if (!e || !base) return;
    if (e->mode == SYNAPTION_MODE_OFF) return;
    e->blocks_tracked++;
    (void)size;
    (void)tier;
}

void synaption_touch(SynaptionEngine *e, void *ptr) {
    if (!e || !ptr) return;
    if (e->mode == SYNAPTION_MODE_OFF) return;
    /* main binary can use this to drive heat map / promotion */
    (void)ptr;
}

const char *synaption_mode_name_ascii(SynaptionMode mode) {
    switch (mode) {
        case SYNAPTION_MODE_OFF:   return "off";
        case SYNAPTION_MODE_TRACK: return "track";
        case SYNAPTION_MODE_PIN:   return "pin";
        default:                   return "?";
    }
}

/* Prune cold blocks: called when hot_count < half of total_count */
void synaption_prune(SynaptionEngine *e, int hot_count, int total_count) {
    if (!e) return;
    (void)hot_count; (void)total_count;
    /* stub: v0.2 will evict cold blocks to free arena space */
}
