/*
 * Ghost: Ghost in the Boot - core implementation.
 * Stub: LED/speaker control via EFI/ACPI. Main binary wires platform I/O.
 */

#include "ghost.h"

void ghost_init(GhostEngine *e) {
    if (!e) return;
    e->mode = GHOST_MODE_OFF;
    e->channel = GHOST_CHANNEL_LED;
    e->tokens_sent = 0;
    e->tokens_recv = 0;
}

void ghost_set_mode(GhostEngine *e, GhostMode mode) {
    if (!e) return;
    e->mode = mode;
}

void ghost_set_channel(GhostEngine *e, GhostChannel ch) {
    if (!e) return;
    e->channel = ch;
}

void ghost_send_token(GhostEngine *e, uint32_t token) {
    if (!e) return;
    if (e->mode != GHOST_MODE_SEND) return;
    e->tokens_sent++;
    (void)token;
}

uint32_t ghost_recv_token(GhostEngine *e) {
    if (!e) return 0xFFFFFFFFU;
    if (e->mode != GHOST_MODE_RECV) return 0xFFFFFFFFU;
    return 0xFFFFFFFFU;  /* none available in stub */
}

const char *ghost_mode_name_ascii(GhostMode mode) {
    switch (mode) {
        case GHOST_MODE_OFF:  return "off";
        case GHOST_MODE_SEND: return "send";
        case GHOST_MODE_RECV: return "recv";
        default:              return "?";
    }
}
