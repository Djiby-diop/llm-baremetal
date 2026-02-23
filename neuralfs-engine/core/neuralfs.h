#pragma once

/*
 * NeuralFS: Neural File System
 *
 * No folders, no filenames. Store as vectors (embeddings).
 * To find a document: describe it to the LLM. Kernel returns by semantic proximity.
 * First OS where "search" is a conversation.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEURALFS_EMBED_DIM 64
#define NEURALFS_QUERY_MAX 128

typedef enum {
    NEURALFS_MODE_OFF  = 0,
    NEURALFS_MODE_INDEX = 1,  /* index existing blobs */
    NEURALFS_MODE_QUERY = 2,  /* enable semantic search */
} NeuralfsMode;

typedef struct {
    uint8_t vec[NEURALFS_EMBED_DIM];  /* quantized embedding */
    uint32_t blob_id;
    uint32_t score;  /* match score 0..255 */
} NeuralfsMatch;

typedef struct {
    NeuralfsMode mode;
    uint32_t blobs_indexed;
    uint32_t queries_done;
} NeuralfsEngine;

void neuralfs_init(NeuralfsEngine *e);
void neuralfs_set_mode(NeuralfsEngine *e, NeuralfsMode mode);

/* Index a blob (id, raw bytes). LLM produces embedding; stored internally. */
void neuralfs_index(NeuralfsEngine *e, uint32_t blob_id, const void *data, uint32_t len);

/* Query by semantic proximity. query text -> LLM -> match top N. */
uint32_t neuralfs_query(NeuralfsEngine *e, const char *query, NeuralfsMatch *out, uint32_t max_out);

const char *neuralfs_mode_name_ascii(NeuralfsMode mode);

#ifdef __cplusplus
}
#endif
