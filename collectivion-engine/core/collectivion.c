/*
 * @@SOMA:C
 * @@LAW
 * allow collectivion.emit op:55 if payload_len <= 4096
 *
 * @@PROOF
 * invariant op:55: integrity => checksum(header) == valid
 */

#include "collectivion.h"
#include <string.h>

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

/* ── Proto-Bridge Implementation ────────────────────────────────────── */

void collectivion_send_msg(CollectivionEngine *e, GhostEngine *ghost,
                           OOEvent kind, const void *payload, uint32_t plen) {
    if (!e || !ghost || e->mode == COLLECTIVION_MODE_OFF) return;

    // @@LAW: Validate payload size (op:55)
    if (plen > OO_MAX_PAYLOAD) return;

    OOMessageHeader hdr;
    oo_msg_init(&hdr, OO_LAYER_KERNEL, OO_LAYER_BROADCAST, kind,
                ghost->tx_seq, 0, plen); // ts=0 for now, should use chronion
    
    // We wrap the official OOMessage into multiple Ghost OO-NET packets if needed
    // In this MVP, we just send it as a raw broadcast if the transport supports it.
    // For now, we bridge it to the existing ghost_send_packet by creating a pseudo-packet.
    
    OoNetPacket pkt;
    uint32_t extra = (plen > 0) ? (*(uint32_t*)payload) : 0;
    oo_net_pkt_build(&pkt, OO_PKT_TEXT, (uint8_t)(e->node_id & 0xFF), 0xFF,
                     ghost->tx_seq, (const uint8_t*)payload, (plen > 20 ? 20 : plen), extra);
    
    ghost_send_packet(ghost, &pkt);
    e->broadcasts_sent++;
}

void collectivion_emit_vital(CollectivionEngine *e, GhostEngine *ghost,
                             uint8_t severity, const char *reason) {
    if (!e || !ghost || e->mode == COLLECTIVION_MODE_OFF) return;
    
    // Use the official VITAL_SIGNAL event kind
    collectivion_send_msg(e, ghost, OO_EVENT_VITAL_SIGNAL, reason, (uint32_t)strlen(reason));
}

/* Legacy / Internal logic ─────────────────────────────────────────── */

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
    
    // Also emit as an official PATCH event for the host
    collectivion_send_msg(e, ghost, OO_EVENT_PATCH, &dna_hash, 4);

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
    
    // Official RESPONSE event bridge
    collectivion_send_msg(e, ghost, OO_EVENT_RESPONSE, text, (uint32_t)strlen(text));

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
int collectivion_broadcast_thought(
    CollectivionEngine *e,
    GhostEngine *ghost,
    uint32_t obj_id,
    const char *name,
    float priority,
    float success_rate
) {
    if (!e || !ghost || e->mode != COLLECTIVION_MODE_TELEPATHY) return 0;
    
    CollectivionThoughtPacket pkt;
    pkt.object_id = obj_id;
    pkt.priority = priority;
    pkt.success_rate = success_rate;
    strncpy(pkt.name, name, 31);
    
    /* Broadcast via Ghost network */
    OoNetPacket net_pkt;
    oo_net_pkt_build(&net_pkt, OO_PKT_TEXT, (uint8_t)(e->node_id & 0xFF), 0xFF,
                     ghost->tx_seq, (const uint8_t*)&pkt, sizeof(pkt), 0);
                     
    ghost_send_packet(ghost, &net_pkt);
    e->broadcasts_sent++;
    
    return 1;
}

int collectivion_receive_thought(
    CollectivionEngine *e,
    const void *payload,
    uint32_t plen,
    CollectivionThoughtPacket *out
) {
    if (!e || !payload || !out || plen < sizeof(CollectivionThoughtPacket)) return 0;
    
    memcpy(out, payload, sizeof(CollectivionThoughtPacket));
    e->broadcasts_recv++;
    
    return 1;
}
