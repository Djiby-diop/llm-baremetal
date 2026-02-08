#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEMORION_MODE_OFF = 0,
    MEMORION_MODE_ON  = 1,
} MemorionMode;

typedef struct {
    MemorionMode mode;
    uint32_t manifests_written;
    uint32_t checks_done;
} MemorionEngine;

void memorion_init(MemorionEngine *e);
void memorion_set_mode(MemorionEngine *e, MemorionMode mode);
const char *memorion_mode_name_ascii(MemorionMode mode);

#ifdef __cplusplus
}
#endif
