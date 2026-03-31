#include "dreamion.h"

void dreamion_init(DreamionEngine *e) {
    if (!e) return;
    e->mode = DREAMION_MODE_OFF;
    e->cycles_in_dream = 0;
    e->tasks_completed = 0;
}

void dreamion_set_mode(DreamionEngine *e, DreamionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void dreamion_tick(DreamionEngine *e, uint32_t idle_cycles) {
    if (!e) return;
    if (e->mode == DREAMION_MODE_OFF) return;
    e->cycles_in_dream += idle_cycles;
}

DreamionTaskType dreamion_suggest_task(const DreamionEngine *e) {
    if (!e || e->mode == DREAMION_MODE_OFF) return DREAMION_TASK_NONE;
    return DREAMION_TASK_NONE;
}

const char *dreamion_mode_name_ascii(DreamionMode mode) {
    switch (mode) {
        case DREAMION_MODE_OFF:   return "off";
        case DREAMION_MODE_LIGHT: return "light";
        case DREAMION_MODE_DEEP:  return "deep";
        default:                  return "?";
    }
}
