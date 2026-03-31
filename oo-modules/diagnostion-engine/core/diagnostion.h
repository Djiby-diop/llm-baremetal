#pragma once

#include <stdint.h>

// Diagnostion: diagnostic/observability engine.
// Philosophy: make failures explainable + replayable with minimal friction.
// Freestanding friendly; no heap required.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DIAGNOSTION_MODE_OFF = 0,
    DIAGNOSTION_MODE_ON = 1,
} DiagnostionMode;

typedef struct {
    DiagnostionMode mode;
    uint32_t reports_written;
} DiagnostionEngine;

void diagnostion_init(DiagnostionEngine *e);
void diagnostion_set_mode(DiagnostionEngine *e, DiagnostionMode mode);
const char *diagnostion_mode_name_ascii(DiagnostionMode mode);

#ifdef __cplusplus
}
#endif
