/*
 * Evolvion: Self-Evolving Kernel - core implementation.
 * Stub: records needs. LLM codegen + JIT wired by main binary.
 */

#include "evolvion.h"

static uint32_t simple_hash(const char *p) {
    uint32_t h = 0;
    while (*p) { h = h * 31 + (uint8_t)*p++; }
    return h;
}

void evolvion_init(EvolvionEngine *e) {
    if (!e) return;
    e->mode = EVOLVION_MODE_OFF;
    e->needs_recorded = 0;
    e->codegen_attempts = 0;
    e->jit_successes = 0;
}

void evolvion_set_mode(EvolvionEngine *e, EvolvionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void evolvion_record_need(EvolvionEngine *e, EvolvionNeedType type, const char *desc) {
    if (!e || !desc) return;
    if (e->mode == EVOLVION_MODE_OFF) return;
    e->needs_recorded++;
    /* hash for dedup; main binary can call LLM with desc when LIVE */
    (void)simple_hash(desc);
}

const char *evolvion_mode_name_ascii(EvolvionMode mode) {
    switch (mode) {
        case EVOLVION_MODE_OFF:   return "off";
        case EVOLVION_MODE_LEARN: return "learn";
        case EVOLVION_MODE_LIVE:  return "live";
        default:                  return "?";
    }
}
