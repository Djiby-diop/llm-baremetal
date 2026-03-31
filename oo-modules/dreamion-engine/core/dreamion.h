#pragma once

/*
 * Dreamion: Dream State
 *
 * During idle (screen off, no input), LLM runs background tasks:
 * KV cache dedup, prompt prediction, memory consolidation.
 * Organism that keeps "thinking" when asleep.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DREAMION_MODE_OFF   = 0,
    DREAMION_MODE_LIGHT = 1,  /* minimal background work */
    DREAMION_MODE_DEEP  = 2,  /* full consolidation, prediction */
} DreamionMode;

typedef enum {
    DREAMION_TASK_NONE    = 0,
    DREAMION_TASK_DEDUP   = 1,
    DREAMION_TASK_PREDICT = 2,
    DREAMION_TASK_COMPACT = 3,
} DreamionTaskType;

typedef struct {
    DreamionMode mode;
    uint32_t cycles_in_dream;
    uint32_t tasks_completed;
} DreamionEngine;

void dreamion_init(DreamionEngine *e);
void dreamion_set_mode(DreamionEngine *e, DreamionMode mode);

void dreamion_tick(DreamionEngine *e, uint32_t idle_cycles);
DreamionTaskType dreamion_suggest_task(const DreamionEngine *e);

const char *dreamion_mode_name_ascii(DreamionMode mode);

#ifdef __cplusplus
}
#endif
