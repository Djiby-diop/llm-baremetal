#pragma once

/*
 * Pheromion: Pheromone Trails
 *
 * Hot execution paths leave traces (counters). Most-travelled paths get
 * prefetch, layout, branch prediction. Ant colony optimization.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHEROMION_SLOT_MAX 64

typedef enum {
    PHEROMION_MODE_OFF   = 0,
    PHEROMION_MODE_TRACE = 1,  /* record path hits */
    PHEROMION_MODE_BOOST = 2,  /* apply prefetch/layout hints */
} PheromionMode;

typedef struct {
    uint32_t path_id;
    uint32_t hit_count;
} PheromionSlot;

typedef struct {
    PheromionMode mode;
    PheromionSlot slots[PHEROMION_SLOT_MAX];
} PheromionEngine;

void pheromion_init(PheromionEngine *e);
void pheromion_set_mode(PheromionEngine *e, PheromionMode mode);

void pheromion_touch(PheromionEngine *e, uint32_t path_id);
uint32_t pheromion_top_path(const PheromionEngine *e);

const char *pheromion_mode_name_ascii(PheromionMode mode);

#ifdef __cplusplus
}
#endif
