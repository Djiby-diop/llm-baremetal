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

typedef struct {
    int last_error; /* 0 = ok */
} CellionEngine;

enum {
    CELLION_OK = 0,
    CELLION_ERR_INVALID = -1,
    CELLION_ERR_TRUNCATED = -2,
    CELLION_ERR_NOT_FOUND = -3,
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

#ifdef __cplusplus
}
#endif
