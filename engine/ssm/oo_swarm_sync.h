/*
 * oo_swarm_sync.h — OO Swarm Sync Protocol (DNA broadcast + consensus)
 *
 * Wraps soma_swarm_net + oo_net_packet into a higher-level sync API.
 * Phase O: Multi-instance swarm. Freestanding C11. No libc.
 *
 * Message types exchanged:
 *   SWARM_MSG_HELLO           — announce presence, my node_id + dna_hash
 *   SWARM_MSG_DNA_BROADCAST   — push DNA delta to all peers
 *   SWARM_MSG_KV_PARTIAL      — share a partial KV token vote for consensus
 *   SWARM_MSG_CONSENSUS_VOTE  — submit final consensus token ID
 *   SWARM_MSG_ACK             — acknowledge receipt
 *   SWARM_MSG_STATUS          — report node state + D+ verdict to peers
 */
#ifndef OO_SWARM_SYNC_H
#define OO_SWARM_SYNC_H

#include "oo_swarm_node.h"
#include "../llama2/oo_net_packet.h"   /* OoNetPacket, OO_PKT_* */

/* ── Sync message types (carried in OoNetPacket.payload[0]) ──── */
typedef enum {
    SWARM_MSG_HELLO          = 0x10,
    SWARM_MSG_DNA_BROADCAST  = 0x11,
    SWARM_MSG_KV_PARTIAL     = 0x12,
    SWARM_MSG_CONSENSUS_VOTE = 0x13,
    SWARM_MSG_ACK            = 0x14,
    SWARM_MSG_STATUS         = 0x15,
} SwarmMsgType;

/* Status flags packed in STATUS message */
#define SWARM_STATUS_DEGRADED   0x01u   /* D+ non-ALLOW */
#define SWARM_STATUS_EMERGENCY  0x02u   /* D+ EMERGENCY */
#define SWARM_STATUS_ISOLATED   0x04u   /* no peers for > isolation window */

/* Sync context — one per node */
typedef struct {
    OoSwarmNode         *node;      /* back-pointer to node identity   */
    unsigned char        seq;       /* rolling sequence counter        */
    unsigned int         last_ack_ms;
    unsigned int         packets_sent;
    unsigned int         packets_recv;
    unsigned int         consensus_count;
} OoSwarmSync;

/* ── API ─────────────────────────────────────────────────────── */

void oo_swarm_sync_init(OoSwarmSync *s, OoSwarmNode *node);

/*
 * oo_swarm_sync_send_hello() — broadcast HELLO to all peers (0xFF dst).
 * Call at boot or after isolation recovery.
 * Writes packet bytes to out_buf[OO_NET_PKT_SIZE].
 */
void oo_swarm_sync_send_hello(OoSwarmSync *s, unsigned int now_ms,
                               unsigned char out_buf[32]);

/*
 * oo_swarm_sync_send_dna() — broadcast DNA delta via DNA_SYNC packet.
 * delta_temp / delta_topp: difference from base DNA params.
 */
void oo_swarm_sync_send_dna(OoSwarmSync *s,
                             unsigned int dna_hash,
                             float delta_temp, float delta_topp,
                             unsigned char out_buf[32]);

/*
 * oo_swarm_sync_send_status() — broadcast local node status (D+ verdict flags).
 * status_flags: bitmask of SWARM_STATUS_* flags.
 */
void oo_swarm_sync_send_status(OoSwarmSync *s,
                                unsigned char status_flags,
                                unsigned char out_buf[32]);

/*
 * oo_swarm_sync_send_vote() — send a consensus token vote.
 * token_id : proposed next token
 * prob     : local confidence (0.0–1.0, packed as uint8 * 200)
 */
void oo_swarm_sync_send_vote(OoSwarmSync *s,
                              int token_id, float prob,
                              unsigned char out_buf[32]);

/*
 * oo_swarm_sync_recv() — process a received 32-byte packet.
 * Updates node->peers, increments stats.
 * Returns SwarmMsgType of the processed message, or -1 if invalid.
 */
int oo_swarm_sync_recv(OoSwarmSync *s,
                       const unsigned char pkt_buf[32],
                       unsigned int now_ms);

/*
 * oo_swarm_sync_run_consensus() — query soma_swarm_net for net-consensus token.
 * Returns the consensus SomaSwarmNetResult.
 */
SomaSwarmNetResult oo_swarm_sync_run_consensus(OoSwarmSync *s,
                                                const float *logits,
                                                int vocab_size,
                                                unsigned int now_ms);

const char *oo_swarm_sync_msg_name(SwarmMsgType t);

#endif /* OO_SWARM_SYNC_H */
