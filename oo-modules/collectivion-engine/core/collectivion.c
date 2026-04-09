#include "collectivion.h"
#include "../../ghost-engine/core/ghost.h"

/* Forward-declared ghost singleton (declared in soma_mind.c) */
extern GhostEngine g_ghost;

void collectivion_init(CollectivionEngine *e) {
    if (!e) return;
    e->mode = COLLECTIVION_MODE_OFF;
    e->node_id = 0;
    e->broadcasts_sent = 0;
    e->broadcasts_recv = 0;
    for (int i = 0; i < COLLECTIVION_PEER_MAX; i++)
        e->peers[i].node_id = 0xFFFFFFFFU;
}

void collectivion_set_mode(CollectivionEngine *e, CollectivionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void collectivion_set_node_id(CollectivionEngine *e, uint32_t id) {
    if (!e) return;
    e->node_id = id;
}

void collectivion_broadcast(CollectivionEngine *e, const void *data, uint32_t len) {
    if (!e) return;
    if (e->mode != COLLECTIVION_MODE_ACTIVE) return;
    e->broadcasts_sent++;
    /* Route through ghost LED channel: encode first token of payload */
    if (len >= 4) {
        const uint32_t *tok = (const uint32_t *)data;
        ghost_send_token(&g_ghost, *tok);
        uint8_t led = 0;
        ghost_led_encode(&g_ghost, &led);
        ghost_led_write(led);
    }
}

uint32_t collectivion_poll(CollectivionEngine *e, void *buf, uint32_t cap) {
    if (!e || !buf || cap == 0) return 0;
    if (e->mode == COLLECTIVION_MODE_OFF) return 0;
    /* Drain one token from ghost receive ring */
    uint32_t tok = ghost_recv_token(&g_ghost);
    if (tok == 0xFFFFFFFFU) return 0;
    if (cap >= 4) {
        *(uint32_t *)buf = tok;
        e->broadcasts_recv++;
        return 4;
    }
    return 0;
}

const char *collectivion_mode_name_ascii(CollectivionMode mode) {
    switch (mode) {
        case COLLECTIVION_MODE_OFF:    return "off";
        case COLLECTIVION_MODE_PASSIVE: return "passive";
        case COLLECTIVION_MODE_ACTIVE:  return "active";
        default:                        return "?";
    }
}
