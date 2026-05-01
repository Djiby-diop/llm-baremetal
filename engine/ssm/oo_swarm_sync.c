/*
 * oo_swarm_sync.c — OO Swarm Sync Protocol Implementation
 * Phase O: Multi-instance swarm. Freestanding C11. No libc.
 */
#include "oo_swarm_sync.h"

/* ── freestanding helpers ────────────────────────────────────── */
static void _ss_memset(void *d, int v, unsigned int n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)v;
}

/* float → uint8 packing for probability (0.0–1.0 → 0–200) */
static unsigned char _prob_pack(float p) {
    int v = (int)(p * 200.0f);
    if (v < 0)   v = 0;
    if (v > 200) v = 200;
    return (unsigned char)v;
}
static float _prob_unpack(unsigned char u) { return (float)u / 200.0f; }

extern void oo_uart_print_str(const char *s);

/* ── API ─────────────────────────────────────────────────────── */

const char *oo_swarm_sync_msg_name(SwarmMsgType t) {
    switch (t) {
    case SWARM_MSG_HELLO:          return "HELLO";
    case SWARM_MSG_DNA_BROADCAST:  return "DNA_BROADCAST";
    case SWARM_MSG_KV_PARTIAL:     return "KV_PARTIAL";
    case SWARM_MSG_CONSENSUS_VOTE: return "CONSENSUS_VOTE";
    case SWARM_MSG_ACK:            return "ACK";
    case SWARM_MSG_STATUS:         return "STATUS";
    default:                       return "UNKNOWN";
    }
}

void oo_swarm_sync_init(OoSwarmSync *s, OoSwarmNode *node) {
    _ss_memset(s, 0, sizeof(*s));
    s->node = node;
    s->seq  = 0;
}

void oo_swarm_sync_send_hello(OoSwarmSync *s, unsigned int now_ms,
                               unsigned char out_buf[32]) {
    unsigned char payload[20];
    _ss_memset(payload, 0, sizeof(payload));
    payload[0] = (unsigned char)SWARM_MSG_HELLO;
    payload[1] = (unsigned char)(s->node->node_id & 0xFF);
    /* dna_hash in bytes 2..5 */
    unsigned int dh = s->node->dna_hash;
    payload[2] = (unsigned char)(dh >> 24);
    payload[3] = (unsigned char)(dh >> 16);
    payload[4] = (unsigned char)(dh >> 8);
    payload[5] = (unsigned char)(dh);
    payload[6] = (unsigned char)(s->node->n_active_peers & 0xFF);
    (void)now_ms;

    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, OO_PKT_HELLO,
                     (unsigned char)(s->node->node_id),
                     0xFF,  /* broadcast */
                     s->seq++,
                     payload, sizeof(payload),
                     s->node->dna_hash);
    oo_net_pkt_to_bytes(&pkt, out_buf);
    s->packets_sent++;
    s->node->hellos_sent++;
}

void oo_swarm_sync_send_dna(OoSwarmSync *s,
                             unsigned int dna_hash,
                             float delta_temp, float delta_topp,
                             unsigned char out_buf[32]) {
    unsigned char payload[20];
    _ss_memset(payload, 0, sizeof(payload));
    payload[0] = (unsigned char)SWARM_MSG_DNA_BROADCAST;

    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, OO_PKT_DNA_SYNC,
                     (unsigned char)(s->node->node_id),
                     0xFF, /* broadcast */
                     s->seq++,
                     payload, sizeof(payload),
                     dna_hash);
    /* Overwrite with delta encoding */
    oo_net_pkt_set_dna(&pkt, dna_hash, delta_temp, delta_topp);
    oo_net_pkt_to_bytes(&pkt, out_buf);
    s->packets_sent++;
}

void oo_swarm_sync_send_status(OoSwarmSync *s,
                                unsigned char status_flags,
                                unsigned char out_buf[32]) {
    unsigned char payload[20];
    _ss_memset(payload, 0, sizeof(payload));
    payload[0] = (unsigned char)SWARM_MSG_STATUS;
    payload[1] = status_flags;
    payload[2] = (unsigned char)(s->node->state & 0xFF);

    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, OO_PKT_PING,  /* re-use PING type for status */
                     (unsigned char)(s->node->node_id),
                     0xFF, s->seq++,
                     payload, sizeof(payload),
                     s->node->dna_hash);
    oo_net_pkt_to_bytes(&pkt, out_buf);
    s->packets_sent++;
}

void oo_swarm_sync_send_vote(OoSwarmSync *s,
                              int token_id, float prob,
                              unsigned char out_buf[32]) {
    unsigned char payload[20];
    _ss_memset(payload, 0, sizeof(payload));
    payload[0] = (unsigned char)SWARM_MSG_CONSENSUS_VOTE;
    /* token_id in bytes 1..4 (big-endian) */
    payload[1] = (unsigned char)((unsigned int)token_id >> 24);
    payload[2] = (unsigned char)((unsigned int)token_id >> 16);
    payload[3] = (unsigned char)((unsigned int)token_id >> 8);
    payload[4] = (unsigned char)((unsigned int)token_id);
    payload[5] = _prob_pack(prob);

    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, OO_PKT_TOKEN,
                     (unsigned char)(s->node->node_id),
                     0xFF, s->seq++,
                     payload, sizeof(payload),
                     0);
    oo_net_pkt_to_bytes(&pkt, out_buf);
    s->packets_sent++;
}

int oo_swarm_sync_recv(OoSwarmSync *s,
                       const unsigned char pkt_buf[32],
                       unsigned int now_ms) {
    OoNetPacket pkt;
    if (!oo_net_pkt_from_bytes(&pkt, pkt_buf)) return -1;
    if (!oo_net_pkt_valid(&pkt)) return -1;

    unsigned int src = pkt.src_id;
    if (src == s->node->node_id) return -1;  /* ignore own packets */

    /* Read message sub-type from payload[0] */
    SwarmMsgType mtype = (SwarmMsgType)(pkt.payload[0]);
    s->packets_recv++;

    switch (mtype) {
    case SWARM_MSG_HELLO: {
        unsigned int peer_dna =
            ((unsigned int)pkt.payload[2] << 24) |
            ((unsigned int)pkt.payload[3] << 16) |
            ((unsigned int)pkt.payload[4] << 8)  |
            (unsigned int)pkt.payload[5];
        oo_swarm_node_peer_seen(s->node, src, peer_dna, 0, now_ms);
        break;
    }
    case SWARM_MSG_DNA_BROADCAST: {
        unsigned int peer_dna;
        float dt, dp;
        oo_net_pkt_get_dna(&pkt, &peer_dna, &dt, &dp);
        oo_swarm_node_peer_seen(s->node, src, peer_dna, 0, now_ms);
        break;
    }
    case SWARM_MSG_STATUS: {
        unsigned char flags = pkt.payload[1];
        int degraded = (flags & (SWARM_STATUS_DEGRADED | SWARM_STATUS_EMERGENCY)) ? 1 : 0;
        oo_swarm_node_peer_seen(s->node, src, pkt.extra, degraded, now_ms);
        break;
    }
    case SWARM_MSG_CONSENSUS_VOTE:
        /* soma_swarm_net handles consensus aggregation separately */
        oo_swarm_node_peer_seen(s->node, src, pkt.extra, 0, now_ms);
        break;
    case SWARM_MSG_ACK:
        s->last_ack_ms = now_ms;
        break;
    default:
        break;
    }

    return (int)mtype;
}

SomaSwarmNetResult oo_swarm_sync_run_consensus(OoSwarmSync *s,
                                                const float *logits,
                                                int vocab_size,
                                                unsigned int now_ms) {
    SomaSwarmNetResult empty;
    _ss_memset(&empty, 0, sizeof(empty));

    if (!s->node->net || !s->node->net->enabled) return empty;

    SomaSwarmNetResult r =
        soma_swarm_net_consensus(s->node->net, logits, vocab_size, now_ms);
    s->consensus_count++;
    return r;
}
