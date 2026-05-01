/*
 * NeuralFS: Neural File System - core implementation.
 * Basic fallback: Uses FNV-1a hash to simulate embeddings for now.
 * In a full system, this would call the LLM for vector generation.
 */

#include "neuralfs.h"

// Very simple static store for the simulator
#define MAX_INDEXED_BLOBS 256
static struct {
    uint32_t blob_id;
    uint8_t vec[NEURALFS_EMBED_DIM];
} s_blob_index[MAX_INDEXED_BLOBS];

static uint32_t s_indexed_count = 0;

static void fake_embedding(const void *data, uint32_t len, uint8_t *vec) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 16777619;
    }
    // Fill the vector with variations of the hash
    for (int i = 0; i < NEURALFS_EMBED_DIM; i++) {
        vec[i] = (uint8_t)((hash >> (i % 24)) & 0xFF);
    }
}

void neuralfs_init(NeuralfsEngine *e) {
    if (!e) return;
    e->mode = NEURALFS_MODE_OFF;
    e->blobs_indexed = 0;
    e->queries_done = 0;
    s_indexed_count = 0;
}

void neuralfs_set_mode(NeuralfsEngine *e, NeuralfsMode mode) {
    if (!e) return;
    e->mode = mode;
}

void neuralfs_index(NeuralfsEngine *e, uint32_t blob_id, const void *data, uint32_t len) {
    if (!e) return;
    if (e->mode == NEURALFS_MODE_OFF) return;
    
    if (s_indexed_count < MAX_INDEXED_BLOBS) {
        s_blob_index[s_indexed_count].blob_id = blob_id;
        fake_embedding(data, len, s_blob_index[s_indexed_count].vec);
        s_indexed_count++;
        e->blobs_indexed++;
    }
}

// Simple dot-product distance
static uint32_t vec_distance(const uint8_t *v1, const uint8_t *v2) {
    uint32_t score = 0;
    for (int i = 0; i < NEURALFS_EMBED_DIM; i++) {
        int diff = (int)v1[i] - (int)v2[i];
        if (diff < 0) diff = -diff;
        score += (uint32_t)(255 - diff); // Higher is better
    }
    return score / NEURALFS_EMBED_DIM; // Normalize to 0-255
}

uint32_t neuralfs_query(NeuralfsEngine *e, const char *query, NeuralfsMatch *out, uint32_t max_out) {
    if (!e || !query || !out || max_out == 0) return 0;
    if (e->mode != NEURALFS_MODE_QUERY) return 0;
    e->queries_done++;

    uint8_t qvec[NEURALFS_EMBED_DIM];
    uint32_t qlen = 0;
    while(query[qlen]) qlen++;
    fake_embedding(query, qlen, qvec);

    uint32_t found = 0;
    for (uint32_t i = 0; i < s_indexed_count && found < max_out; i++) {
        uint32_t score = vec_distance(qvec, s_blob_index[i].vec);
        if (score > 128) { // Arbitrary threshold
            out[found].blob_id = s_blob_index[i].blob_id;
            out[found].score = score;
            for (int k = 0; k < NEURALFS_EMBED_DIM; k++) {
                out[found].vec[k] = s_blob_index[i].vec[k];
            }
            found++;
        }
    }
    return found;
}

const char *neuralfs_mode_name_ascii(NeuralfsMode mode) {
    switch (mode) {
        case NEURALFS_MODE_OFF:    return "off";
        case NEURALFS_MODE_INDEX:  return "index";
        case NEURALFS_MODE_QUERY:  return "query";
        default:                   return "?";
    }
}
