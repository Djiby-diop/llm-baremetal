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

// ============================================================
// Core Memory Functions
// ============================================================
// Writes a memory manifest (state snapshot) and calculates its checksum.
uint32_t memorion_write_manifest(MemorionEngine *e, const void *data, uint32_t size);

// Verifies the integrity of a previously written memory manifest.
// Returns 1 if valid, 0 if corrupted.
int memorion_check_integrity(MemorionEngine *e, const void *data, uint32_t size, uint32_t expected_checksum);
#ifdef __cplusplus
}
#endif
