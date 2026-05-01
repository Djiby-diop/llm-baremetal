#include "symbion.h"

// Basic moving average state
static uint32_t s_total_latency = 0;
static uint32_t s_total_retries = 0;
static uint8_t s_last_stress = 0;

void symbion_init(SymbionEngine *e) {
    if (!e) return;
    e->mode = SYMBION_MODE_OFF;
    e->samples_taken = 0;
    e->adaptations_applied = 0;
    s_total_latency = 0;
    s_total_retries = 0;
    s_last_stress = 0;
}

void symbion_set_mode(SymbionEngine *e, SymbionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void symbion_feed(SymbionEngine *e, const SymbionSample *s) {
    if (!e || !s) return;
    if (e->mode == SYMBION_MODE_OFF) return;
    e->samples_taken++;
    s_total_latency += s->latency_raw;
    s_total_retries += s->retries;
    s_last_stress = s->stress;
}

uint8_t symbion_suggest_throttle(const SymbionEngine *e) {
    if (!e || e->mode != SYMBION_MODE_ADAPT) return 0;
    
    // Very simple adaptation heuristic: if stress is high or there are many retries, suggest throttle
    if (e->samples_taken == 0) return 0;
    
    uint32_t avg_retries = s_total_retries / e->samples_taken;
    
    if (s_last_stress > 80 || avg_retries > 5) {
        return 1; // Suggest throttle
    }
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

/* Update active symbol count from Hebbian order */
void symbion_on_hebbian_order(SymbionEngine *e, int hot_count) {
    if (!e) return;
    if (e->mode == SYMBION_MODE_ADAPT && hot_count > 100) {
        // High cognitive load -> adapt
        e->adaptations_applied++;
    }
}
