#pragma once

/*
 * Collectivion: Collective Consciousness
 *
 * Multiple OO instances share a thought stream. DNA params, token streams
 * and decisions broadcast via Ghost OO-NET channel. Swarm smarter than parts.
 */

#include <stdint.h>
#include "../ghost-engine/core/ghost.h"
#include "../ghost-engine/core/oo_net_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COLLECTIVION_PEER_MAX 8

typedef enum {
    COLLECTIVION_MODE_OFF     = 0,
    COLLECTIVION_MODE_PASSIVE = 1,  /* receive only */
    COLLECTIVION_MODE_ACTIVE  = 2,  /* send and receive */
} CollectivionMode;

typedef struct {
    uint32_t node_id;
    uint32_t last_seen_seq;
    uint32_t dna_hash;        /* last known DNA hash from this peer */
} CollectivionPeer;

typedef struct {
    CollectivionMode mode;
    uint32_t node_id;
    uint32_t broadcasts_sent;
    uint32_t broadcasts_recv;
    uint32_t dna_merges;      /* number of incoming DNA deltas applied */
    CollectivionPeer peers[COLLECTIVION_PEER_MAX];
} CollectivionEngine;

void collectivion_init(CollectivionEngine *e);
void collectivion_set_mode(CollectivionEngine *e, CollectivionMode mode);
void collectivion_set_node_id(CollectivionEngine *e, uint32_t id);

/* Broadcast raw data via Ghost TX */
void collectivion_broadcast(CollectivionEngine *e, const void *data, uint32_t len);
/* Poll Ghost RX ring for incoming data */
uint32_t collectivion_poll(CollectivionEngine *e, void *buf, uint32_t cap);

/* High-level OO-NET operations — require ghost pointer */

/* Send HELLO to announce presence */
void collectivion_send_hello(CollectivionEngine *e, GhostEngine *ghost);

/* Broadcast a DNA delta (temperature + topp nudge) to all peers */
void collectivion_sync_dna(CollectivionEngine *e, GhostEngine *ghost,
                           uint32_t dna_hash, float delta_temp, float delta_topp);

/* Broadcast a text fragment (max 20 chars) */
void collectivion_send_text(CollectivionEngine *e, GhostEngine *ghost,
                            const char *text);

/* Receive and process all pending OO-NET packets from Ghost RX queue.
 * Fills *delta_temp / *delta_topp with any received DNA_SYNC delta.
 * Returns number of packets processed. */
int collectivion_recv_all(CollectivionEngine *e, GhostEngine *ghost,
                          float *delta_temp, float *delta_topp);

const char *collectivion_mode_name_ascii(CollectivionMode mode);

#ifdef __cplusplus
}
#endif

