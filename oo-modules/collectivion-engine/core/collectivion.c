#include "collectivion.h"

void collectivion_init(CollectivionEngine *e) {
    if (!e) return;
    e->mode            = COLLECTIVION_MODE_OFF;
    e->node_id         = 0;
    e->broadcasts_sent = 0;
    e->broadcasts_recv = 0;
    e->dna_merges      = 0;
    for (int i = 0; i < COLLECTIVION_PEER_MAX; i++) {
        e->peers[i].node_id       = 0xFFFFFFFFU;
        e->peers[i].last_seen_seq = 0;
        e->peers[i].dna_hash      = 0;
    }
}

void collectivion_set_mode(CollectivionEngine *e, CollectivionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void collectivion_set_node_id(CollectivionEngine *e, uint32_t id) {
    if (!e) return;
    e->node_id = id;
}

/* Legacy token-level broadcast (kept for backward compat) */
void collectivion_broadcast(CollectivionEngine *e, const void *data, uint32_t len) {
    (void)e; (void)data; (void)len;
    /* No-op: use collectivion_send_text / collectivion_sync_dna instead */
}

uint32_t collectivion_poll(CollectivionEngine *e, void *buf, uint32_t cap) {
    (void)e; (void)buf; (void)cap;
    return 0;  /* Use collectivion_recv_all instead */
}

/* ── OO-NET high-level operations ────────────────────────────────────── */

static void col_send_pkt(CollectivionEngine *e, GhostEngine *ghost,
                         OoNetPktType type,
                         const uint8_t *payload, uint32_t plen,
                         uint32_t extra) {
    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, type,
                     (uint8_t)(e->node_id & 0xFF),
                     0xFF,  /* broadcast */
                     ghost->tx_seq,
                     payload, plen, extra);
    ghost_send_packet(ghost, &pkt);
    e->broadcasts_sent++;
}

void collectivion_send_hello(CollectivionEngine *e, GhostEngine *ghost) {
    if (!e || !ghost) return;
    if (e->mode == COLLECTIVION_MODE_OFF) return;
    uint8_t payload[4];
    payload[0] = (uint8_t)( e->node_id        & 0xFF);
    payload[1] = (uint8_t)((e->node_id >>  8) & 0xFF);
    payload[2] = (uint8_t)((e->node_id >> 16) & 0xFF);
    payload[3] = (uint8_t)((e->node_id >> 24) & 0xFF);
    col_send_pkt(e, ghost, OO_PKT_HELLO, payload, 4, 0);
}

void collectivion_sync_dna(CollectivionEngine *e, GhostEngine *ghost,
                           uint32_t dna_hash, float delta_temp, float delta_topp) {
    if (!e || !ghost) return;
    if (e->mode != COLLECTIVION_MODE_ACTIVE) return;
    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, OO_PKT_DNA_SYNC,
                     (uint8_t)(e->node_id & 0xFF), 0xFF,
                     ghost->tx_seq, 0, 0, dna_hash);
    oo_net_pkt_set_dna(&pkt, dna_hash, delta_temp, delta_topp);
    ghost_send_packet(ghost, &pkt);
    e->broadcasts_sent++;
}

void collectivion_send_text(CollectivionEngine *e, GhostEngine *ghost,
                            const char *text) {
    if (!e || !ghost || !text) return;
    if (e->mode == COLLECTIVION_MODE_OFF) return;
    OoNetPacket pkt;
    oo_net_pkt_build(&pkt, OO_PKT_TEXT,
                     (uint8_t)(e->node_id & 0xFF), 0xFF,
                     ghost->tx_seq, 0, 0, 0);
    oo_net_pkt_set_text(&pkt, text);
    ghost_send_packet(ghost, &pkt);
    e->broadcasts_sent++;
}

int collectivion_recv_all(CollectivionEngine *e, GhostEngine *ghost,
                          float *delta_temp, float *delta_topp) {
    if (!e || !ghost) return 0;
    if (e->mode == COLLECTIVION_MODE_OFF) return 0;
    int count = 0;
    OoNetPacket pkt;
    while (ghost_recv_packet(ghost, &pkt)) {
        count++;
        e->broadcasts_recv++;
        /* Update peer table */
        uint8_t src = pkt.src_id;
        CollectivionPeer *slot = 0;
        for (int i = 0; i < COLLECTIVION_PEER_MAX; i++) {
            if (e->peers[i].node_id == (uint32_t)src) { slot = &e->peers[i]; break; }
            if (e->peers[i].node_id == 0xFFFFFFFFU && !slot) slot = &e->peers[i];
        }
        if (slot) {
            slot->node_id       = (uint32_t)src;
            slot->last_seen_seq = (uint32_t)pkt.seq;
        }
        /* Handle DNA_SYNC */
        if (pkt.type == (uint8_t)OO_PKT_DNA_SYNC) {
            uint32_t dh = 0; float dt = 0.f, dp = 0.f;
            oo_net_pkt_get_dna(&pkt, &dh, &dt, &dp);
            if (slot) slot->dna_hash = dh;
            if (delta_temp) *delta_temp += dt;
            if (delta_topp) *delta_topp += dp;
            e->dna_merges++;
        }
    }
    return count;
}

const char *collectivion_mode_name_ascii(CollectivionMode mode) {
    switch (mode) {
        case COLLECTIVION_MODE_OFF:     return "off";
        case COLLECTIVION_MODE_PASSIVE: return "passive";
        case COLLECTIVION_MODE_ACTIVE:  return "active";
        default:                        return "?";
    }
}