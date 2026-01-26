#include "calibrion.h"

void calibrion_init(CalibrionEngine *e) {
    if (!e) return;
    e->mode = CALIBRION_MODE_OFF;
    e->strategy = CALIBRION_STRATEGY_NONE;

    // Default bounds
    e->bounds.temp_min_milli = 100;    // 0.1
    e->bounds.temp_max_milli = 1500;   // 1.5
    e->bounds.top_k_min = 1;
    e->bounds.top_k_max = 100;
    e->bounds.top_p_min_milli = 800;   // 0.8
    e->bounds.top_p_max_milli = 990;   // 0.99

    calibrion_reset_stats(e);

    // Default recommendation: middle of range
    e->rec_temp_milli = 700;   // 0.7
    e->rec_top_k = 40;
    e->rec_top_p_milli = 900;  // 0.9

    e->calibrations_done = 0;
}

void calibrion_set_mode(CalibrionEngine *e, CalibrionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void calibrion_set_strategy(CalibrionEngine *e, CalibrionStrategy strategy) {
    if (!e) return;
    e->strategy = strategy;
}

void calibrion_set_bounds(CalibrionEngine *e, const CalibrionBounds *bounds) {
    if (!e || !bounds) return;
    e->bounds = *bounds;
}

const char *calibrion_mode_name_ascii(CalibrionMode mode) {
    switch (mode) {
        case CALIBRION_MODE_OFF:     return "off";
        case CALIBRION_MODE_OBSERVE: return "observe";
        case CALIBRION_MODE_ENFORCE: return "enforce";
        default:                     return "?";
    }
}

const char *calibrion_strategy_name_ascii(CalibrionStrategy strategy) {
    switch (strategy) {
        case CALIBRION_STRATEGY_NONE:    return "none";
        case CALIBRION_STRATEGY_ENTROPY: return "entropy";
        case CALIBRION_STRATEGY_LENGTH:  return "length";
        case CALIBRION_STRATEGY_QUALITY: return "quality";
        case CALIBRION_STRATEGY_HYBRID:  return "hybrid";
        default:                         return "?";
    }
}

void calibrion_reset_stats(CalibrionEngine *e) {
    if (!e) return;
    e->stats.samples = 0;
    e->stats.total_tokens = 0;
    e->stats.total_repeats = 0;
    e->stats.short_responses = 0;
    e->stats.long_responses = 0;
    e->stats.avg_entropy_milli = 0;
}

void calibrion_feed(CalibrionEngine *e, uint32_t tokens_generated, uint32_t repeats, uint32_t entropy_milli) {
    if (!e) return;
    if (e->mode == CALIBRION_MODE_OFF) return;

    e->stats.samples++;
    e->stats.total_tokens += tokens_generated;
    e->stats.total_repeats += repeats;

    // Rolling average entropy
    if (e->stats.samples == 1) {
        e->stats.avg_entropy_milli = entropy_milli;
    } else {
        // Simple exponential moving average (alpha ~ 0.2)
        e->stats.avg_entropy_milli = (e->stats.avg_entropy_milli * 4 + entropy_milli) / 5;
    }

    // Length classification (target: 20-60 tokens)
    if (tokens_generated < 20) e->stats.short_responses++;
    else if (tokens_generated > 60) e->stats.long_responses++;

    // Update recommendation based on strategy
    if (e->mode == CALIBRION_MODE_ENFORCE) {
        uint32_t new_temp = e->rec_temp_milli;
        uint32_t new_top_k = e->rec_top_k;

        switch (e->strategy) {
            case CALIBRION_STRATEGY_ENTROPY:
                // Low entropy -> increase temp; high entropy -> decrease temp
                if (e->stats.avg_entropy_milli < 500) {
                    new_temp += 50;
                } else if (e->stats.avg_entropy_milli > 1500) {
                    if (new_temp > 100) new_temp -= 50;
                }
                break;

            case CALIBRION_STRATEGY_LENGTH:
                // Too short -> increase temp/top_k; too long -> decrease
                if (e->stats.short_responses > e->stats.long_responses) {
                    new_temp += 30;
                    new_top_k += 5;
                } else if (e->stats.long_responses > e->stats.short_responses) {
                    if (new_temp > 100) new_temp -= 30;
                    if (new_top_k > 5) new_top_k -= 5;
                }
                break;

            case CALIBRION_STRATEGY_QUALITY:
                // High repeats -> increase temp/top_k
                if (repeats > 3) {
                    new_temp += 100;
                    new_top_k += 10;
                }
                break;

            case CALIBRION_STRATEGY_HYBRID:
                // Combine signals
                if (e->stats.avg_entropy_milli < 500 || repeats > 2) {
                    new_temp += 50;
                }
                if (e->stats.short_responses > e->stats.samples / 2) {
                    new_top_k += 5;
                }
                break;

            default:
                break;
        }

        // Clamp to bounds
        if (new_temp < e->bounds.temp_min_milli) new_temp = e->bounds.temp_min_milli;
        if (new_temp > e->bounds.temp_max_milli) new_temp = e->bounds.temp_max_milli;
        if (new_top_k < e->bounds.top_k_min) new_top_k = e->bounds.top_k_min;
        if (new_top_k > e->bounds.top_k_max) new_top_k = e->bounds.top_k_max;

        e->rec_temp_milli = new_temp;
        e->rec_top_k = new_top_k;
        e->calibrations_done++;
    }
}

void calibrion_get_recommendation(CalibrionEngine *e, uint32_t *out_temp_milli, uint32_t *out_top_k, uint32_t *out_top_p_milli) {
    if (!e) return;
    if (out_temp_milli) *out_temp_milli = e->rec_temp_milli;
    if (out_top_k) *out_top_k = e->rec_top_k;
    if (out_top_p_milli) *out_top_p_milli = e->rec_top_p_milli;
}
