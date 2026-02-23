#pragma once

/*
 * Metabion: Metabolism Metrics
 *
 * System exposes metabolism: tokens/s, flops/W, cache hit rate.
 * LLM or external tools adjust load to stay in optimal metabolic zone.
 * Organism that knows its own pulse.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    METABION_MODE_OFF   = 0,
    METABION_MODE_TRACK = 1,  /* collect metrics */
    METABION_MODE_GUIDE = 2,  /* suggest adjustments */
} MetabionMode;

typedef struct {
    uint64_t tokens_per_sec;
    uint64_t flops_per_watt;  /* best-effort */
    uint32_t cache_hit_milli; /* 0..1000 = hit rate % */
    uint32_t mem_bandwidth_mb;
} MetabionSample;

typedef struct {
    MetabionMode mode;
    MetabionSample last;
    uint32_t samples_count;
} MetabionEngine;

void metabion_init(MetabionEngine *e);
void metabion_set_mode(MetabionEngine *e, MetabionMode mode);

void metabion_feed(MetabionEngine *e, const MetabionSample *s);
void metabion_get_last(MetabionEngine *e, MetabionSample *out);

const char *metabion_mode_name_ascii(MetabionMode mode);

#ifdef __cplusplus
}
#endif
