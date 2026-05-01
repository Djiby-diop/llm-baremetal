#pragma once

/*
 * Collectivion: Collective Consciousness
 *
 * Multiple OO instances share a thought stream. DNA params, token streams
 * and decisions broadcast via Ghost OO-NET channel. Swarm smarter than parts.
 */

#include <stdint.h>
#include <oo_proto.h>
#include "../ghost-engine/core/ghost.h"
#include "../ghost-engine/core/oo_net_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COLLECTIVION_PEER_MAX 8

typedef enum {
    COLLECTIVION_MODE_OFF = 0,
    COLLECTIVION_MODE_PULSE = 1,
    COLLECTIVION_MODE_RECOVERY = 2,
    COLLECTIVION_MODE_TELEPATHY = 3, /* Cognitive Sync Mode */
} CollectivionMode;

typedef enum {
    COLLECT_SYNC_LOCAL = 0,
    COLLECT_SYNC_REMOTE = 1,
} CollectivionSyncType;

typedef struct {
    uint32_t object_id;
    char     name[32];
    float    priority;
    float    success_rate;
} CollectivionThoughtPacket;

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

/* ── Proto-Bridge: Official OOMessage operations ────────────────── */

/* Wrap and send an official OOMessage via Ghost */
void collectivion_send_msg(CollectivionEngine *e, GhostEngine *ghost,
                           OOEvent kind, const void *payload, uint32_t plen);

/* Specialized emitter for vital signals (Heartbeat/Thermic) */
void collectivion_emit_vital(CollectivionEngine *e, GhostEngine *ghost,
                             uint8_t severity, const char *reason);

const char *collectivion_mode_name_ascii(CollectivionMode mode);

/* --- Telepathic Operations (V1 Mutation) --- */

/* 
 * Broadcast a cognitive object to all peers over the network.
 * Returns 1 if broadcast was successful, 0 otherwise.
 */
int collectivion_broadcast_thought(
    CollectivionEngine *e,
    GhostEngine *ghost,
    uint32_t obj_id,
    const char *name,
    float priority,
    float success_rate
);

/*
 * Process an incoming thought packet and prepare it for SomaMind injection.
 */
int collectivion_receive_thought(
    CollectivionEngine *e,
    const void *payload,
    uint32_t plen,
    CollectivionThoughtPacket *out
);

#ifdef __cplusplus
}
#endif

