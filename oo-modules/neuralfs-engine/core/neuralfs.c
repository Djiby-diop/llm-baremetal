/*
 * NeuralFS: Neural File System - core implementation.
 * Stub: no embedding/JIT. Main binary wires LLM for vector gen + search.
 */

#include "neuralfs.h"

void neuralfs_init(NeuralfsEngine *e) {
    if (!e) return;
    e->mode = NEURALFS_MODE_OFF;
    e->blobs_indexed = 0;
    e->queries_done = 0;
}

void neuralfs_set_mode(NeuralfsEngine *e, NeuralfsMode mode) {
    if (!e) return;
    e->mode = mode;
}

void neuralfs_index(NeuralfsEngine *e, uint32_t blob_id, const void *data, uint32_t len) {
    if (!e) return;
    if (e->mode == NEURALFS_MODE_OFF) return;
    e->blobs_indexed++;
    (void)blob_id;
    (void)data;
    (void)len;
}

uint32_t neuralfs_query(NeuralfsEngine *e, const char *query, NeuralfsMatch *out, uint32_t max_out) {
    if (!e || !query || !out || max_out == 0) return 0;
    if (e->mode != NEURALFS_MODE_QUERY) return 0;
    e->queries_done++;
    return 0;
}

const char *neuralfs_mode_name_ascii(NeuralfsMode mode) {
    switch (mode) {
        case NEURALFS_MODE_OFF:    return "off";
        case NEURALFS_MODE_INDEX:  return "index";
        case NEURALFS_MODE_QUERY:  return "query";
        default:                   return "?";
    }
}
