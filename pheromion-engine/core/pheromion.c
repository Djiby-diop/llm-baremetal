#include "pheromion.h"

void pheromion_init(PheromionEngine *e) {
    if (!e) return;
    e->mode = PHEROMION_MODE_OFF;
    for (int i = 0; i < PHEROMION_SLOT_MAX; i++) {
        e->slots[i].path_id = 0xFFFFFFFFU;
        e->slots[i].hit_count = 0;
    }
}

void pheromion_set_mode(PheromionEngine *e, PheromionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void pheromion_touch(PheromionEngine *e, uint32_t path_id) {
    if (!e) return;
    if (e->mode == PHEROMION_MODE_OFF) return;
    for (int i = 0; i < PHEROMION_SLOT_MAX; i++) {
        if (e->slots[i].path_id == path_id) {
            e->slots[i].hit_count++;
            return;
        }
        if (e->slots[i].path_id == 0xFFFFFFFFU) {
            e->slots[i].path_id = path_id;
            e->slots[i].hit_count = 1;
            return;
        }
    }
}

uint32_t pheromion_top_path(const PheromionEngine *e) {
    if (!e) return 0xFFFFFFFFU;
    uint32_t best = 0xFFFFFFFFU;
    uint32_t best_count = 0;
    for (int i = 0; i < PHEROMION_SLOT_MAX; i++) {
        if (e->slots[i].path_id != 0xFFFFFFFFU && e->slots[i].hit_count > best_count) {
            best_count = e->slots[i].hit_count;
            best = e->slots[i].path_id;
        }
    }
    return best;
}

const char *pheromion_mode_name_ascii(PheromionMode mode) {
    switch (mode) {
        case PHEROMION_MODE_OFF:   return "off";
        case PHEROMION_MODE_TRACE: return "trace";
        case PHEROMION_MODE_BOOST: return "boost";
        default:                   return "?";
    }
}
