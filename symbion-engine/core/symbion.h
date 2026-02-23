#pragma once

/*
 * Symbion: Symbiotic Drivers
 *
 * Drivers as partners, not black boxes. LLM observes hardware reactions
 * (latency, retries) and suggests tuning: freq, feature toggles.
 * Hardware and software adapt together.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYMBION_MODE_OFF   = 0,
    SYMBION_MODE_WATCH = 1,  /* observe, no changes */
    SYMBION_MODE_ADAPT = 2,  /* apply suggestions */
} SymbionMode;

typedef struct {
    uint32_t latency_raw;
    uint32_t retries;
    uint8_t stress;  /* 0..100 */
} SymbionSample;

typedef struct {
    SymbionMode mode;
    uint32_t samples_taken;
    uint32_t adaptations_applied;
} SymbionEngine;

void symbion_init(SymbionEngine *e);
void symbion_set_mode(SymbionEngine *e, SymbionMode mode);

void symbion_feed(SymbionEngine *e, const SymbionSample *s);
uint8_t symbion_suggest_throttle(const SymbionEngine *e);

const char *symbion_mode_name_ascii(SymbionMode mode);

#ifdef __cplusplus
}
#endif
