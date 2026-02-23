#pragma once

/*
 * Synaption: Synaptic Memory Management
 *
 * Map RAM by "neural importance". Hot AI data in L1/L2-adjacent regions.
 * Manages "memories" and "reflexes", not files.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNAPTION_MODE_OFF   = 0,
    SYNAPTION_MODE_TRACK = 1,  /* track access heat, no re-layout */
    SYNAPTION_MODE_PIN   = 2,  /* pin hot blocks to fast regions */
} SynaptionMode;

typedef enum {
    SYNAPTION_TIER_COLD  = 0,  /* rarely accessed */
    SYNAPTION_TIER_WARM  = 1,  /* moderate access */
    SYNAPTION_TIER_HOT   = 2,  /* KV cache, weights in use */
    SYNAPTION_TIER_REFLEX = 3, /* L1-adjacent, always hot */
} SynaptionTier;

typedef struct {
    void *base;
    uint64_t size;
    SynaptionTier tier;
    uint32_t access_count;
} SynaptionBlock;

typedef struct {
    SynaptionMode mode;
    uint32_t blocks_tracked;
    uint32_t promotions;
} SynaptionEngine;

void synaption_init(SynaptionEngine *e);
void synaption_set_mode(SynaptionEngine *e, SynaptionMode mode);

/* Register a block for tracking. tier hint for initial placement. */
void synaption_register(SynaptionEngine *e, void *base, uint64_t size, SynaptionTier tier);

/* Signal access (called by hot paths). Updates heat map. */
void synaption_touch(SynaptionEngine *e, void *ptr);

const char *synaption_mode_name_ascii(SynaptionMode mode);

#ifdef __cplusplus
}
#endif
