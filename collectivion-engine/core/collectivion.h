#pragma once

/*
 * Collectivion: Collective Consciousness
 *
 * Multiple OO instances share a thought stream. Partial KV caches or
 * decisions broadcast via Ghost/IPC. Swarm smarter than the sum of parts.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COLLECTIVION_PEER_MAX 8

typedef enum {
    COLLECTIVION_MODE_OFF  = 0,
    COLLECTIVION_MODE_PASSIVE = 1,  /* receive only */
    COLLECTIVION_MODE_ACTIVE  = 2,  /* send and receive */
} CollectivionMode;

typedef struct {
    uint32_t node_id;
    uint32_t last_seen_cycle;
} CollectivionPeer;

typedef struct {
    CollectivionMode mode;
    uint32_t node_id;
    uint32_t broadcasts_sent;
    uint32_t broadcasts_recv;
    CollectivionPeer peers[COLLECTIVION_PEER_MAX];
} CollectivionEngine;

void collectivion_init(CollectivionEngine *e);
void collectivion_set_mode(CollectivionEngine *e, CollectivionMode mode);
void collectivion_set_node_id(CollectivionEngine *e, uint32_t id);

void collectivion_broadcast(CollectivionEngine *e, const void *data, uint32_t len);
uint32_t collectivion_poll(CollectivionEngine *e, void *buf, uint32_t cap);

const char *collectivion_mode_name_ascii(CollectivionMode mode);

#ifdef __cplusplus
}
#endif
