#pragma once

/*
 * Cellion: Wasm "Stem Cells"
 *
 * Minimal, non-executing Wasm parser.
 * Extracts a named custom section (id=0) payload for hot-load configuration deltas.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CELLION_STATE_DORMANT = 0,
    CELLION_STATE_PARSING = 1,
    CELLION_STATE_ERRORED = 2,
    CELLION_STATE_PERCEIVING = 3, /* Vision Mode */
} CellionState;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    void    *buffer;
} CellionVisualCortex;

typedef struct {
    int      objects_detected;
    float    confidence;
    char     label[32];
} CellionPerceptionResult;

typedef struct {
    uint32_t      cell_id;
    CellionState  state;
    int           last_error;
    
    CellionVisualCortex cortex;
} CellionEngine;

enum {
    CELLION_OK = 0,
    CELLION_ERR_INVALID = -1,
    CELLION_ERR_TRUNCATED = -2,
    CELLION_ERR_NOT_FOUND = -3,
    CELLION_ERR_OVERFLOW = -4,
};

void cellion_init(CellionEngine *e);

/*
 * Find a custom section by name.
 *
 * On success returns CELLION_OK and sets out_data/out_len to the custom section's data bytes
 * (excluding the name field). Pointers refer to the original wasm buffer.
 */
int cellion_wasm_find_custom_section(
    CellionEngine *e,
    const uint8_t *wasm,
    size_t wasm_len,
    const char *custom_name_ascii,
    const uint8_t **out_data,
    size_t *out_len
);

/* --- Vision Operations (V1 Mutation) --- */

/* 
 * Perform visual perception on a frame-buffer using a WASM filter.
 * Returns 1 if perception succeeded, 0 otherwise.
 */
int cellion_perceive(
    CellionEngine *e, 
    const uint8_t *filter_wasm, 
    size_t wasm_len,
    CellionPerceptionResult *out
);

#ifdef __cplusplus
}
#endif
