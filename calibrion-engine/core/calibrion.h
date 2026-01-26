#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Calibrion: auto-tuning sampling engine (adaptive temp/top_k/top_p)
// Made in Senegal 🇸🇳

typedef enum {
    CALIBRION_MODE_OFF     = 0,
    CALIBRION_MODE_OBSERVE = 1,
    CALIBRION_MODE_ENFORCE = 2,
} CalibrionMode;

typedef enum {
    CALIBRION_STRATEGY_NONE     = 0,
    CALIBRION_STRATEGY_ENTROPY  = 1,  // adapt based on output entropy
    CALIBRION_STRATEGY_LENGTH   = 2,  // adapt based on response length
    CALIBRION_STRATEGY_QUALITY  = 3,  // adapt based on repetition/coherence
    CALIBRION_STRATEGY_HYBRID   = 4,  // combine multiple signals
} CalibrionStrategy;

// Target range for sampling knobs (stored as milli-units for integer math)
typedef struct {
    uint32_t temp_min_milli;     // e.g. 100 = 0.1
    uint32_t temp_max_milli;     // e.g. 1500 = 1.5
    uint32_t top_k_min;
    uint32_t top_k_max;
    uint32_t top_p_min_milli;    // e.g. 800 = 0.8
    uint32_t top_p_max_milli;    // e.g. 990 = 0.99
} CalibrionBounds;

// Running stats for adaptive decisions
typedef struct {
    uint32_t samples;
    uint32_t total_tokens;
    uint32_t total_repeats;
    uint32_t short_responses;   // below target length
    uint32_t long_responses;    // above target length
    uint32_t avg_entropy_milli; // rolling average entropy * 1000
} CalibrionStats;

typedef struct {
    CalibrionMode mode;
    CalibrionStrategy strategy;
    CalibrionBounds bounds;
    CalibrionStats stats;

    // Current recommendation (milli-units)
    uint32_t rec_temp_milli;
    uint32_t rec_top_k;
    uint32_t rec_top_p_milli;

    uint32_t calibrations_done;
} CalibrionEngine;

void calibrion_init(CalibrionEngine *e);
void calibrion_set_mode(CalibrionEngine *e, CalibrionMode mode);
void calibrion_set_strategy(CalibrionEngine *e, CalibrionStrategy strategy);
void calibrion_set_bounds(CalibrionEngine *e, const CalibrionBounds *bounds);

const char *calibrion_mode_name_ascii(CalibrionMode mode);
const char *calibrion_strategy_name_ascii(CalibrionStrategy strategy);

// Feed a generation result to update stats
void calibrion_feed(CalibrionEngine *e, uint32_t tokens_generated, uint32_t repeats, uint32_t entropy_milli);

// Get current recommendation (call after feed)
void calibrion_get_recommendation(CalibrionEngine *e, uint32_t *out_temp_milli, uint32_t *out_top_k, uint32_t *out_top_p_milli);

// Reset stats
void calibrion_reset_stats(CalibrionEngine *e);

#ifdef __cplusplus
}
#endif
