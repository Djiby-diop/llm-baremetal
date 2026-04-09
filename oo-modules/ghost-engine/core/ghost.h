#pragma once

/*
 * Ghost: Ghost in the Boot (Inter-OO Communication)
 *
 * Two Operating Organisms talk without TCP/IP. Optical pulses via keyboard
 * LEDs, or audio via PC speaker. Tokens exchanged over invisible channel.
 * Unhackable by classical means.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GHOST_MODE_OFF   = 0,
    GHOST_MODE_SEND  = 1,  /* emit tokens */
    GHOST_MODE_RECV  = 2,  /* listen for tokens */
} GhostMode;

typedef enum {
    GHOST_CHANNEL_LED   = 0,  /* keyboard LEDs (caps/num/scroll) */
    GHOST_CHANNEL_PCSPK = 1,  /* PC speaker beeps */
} GhostChannel;

#define GHOST_RING_MAX 16  /* power of 2 — ring buffer for received tokens */

typedef struct {
    GhostMode    mode;
    GhostChannel channel;
    uint32_t     tokens_sent;
    uint32_t     tokens_recv;
    /* Receive ring buffer */
    uint32_t     ring[GHOST_RING_MAX];
    unsigned int ring_head;  /* read index */
    unsigned int ring_tail;  /* write index */
    unsigned int ring_len;   /* items available */
} GhostEngine;

void ghost_init(GhostEngine *e);
void ghost_set_mode(GhostEngine *e, GhostMode mode);
void ghost_set_channel(GhostEngine *e, GhostChannel ch);

/* Send one token (encoded as LED pattern or tone). */
void ghost_send_token(GhostEngine *e, uint32_t token);

/* Poll: receive one token if available. Returns token or 0xFFFFFFFF if none. */
uint32_t ghost_recv_token(GhostEngine *e);

/* Observe a bus channel (called on every message) */
void ghost_observe(GhostEngine *e, uint16_t ch);

/* Encode recent observed channels as a 3-bit LED pattern */
void ghost_led_encode(GhostEngine *e, uint8_t *led_bits);

/* Write LED pattern to PS/2 8042 keyboard controller (no-op on USB-only HW) */
void ghost_led_write(uint8_t led_bits);

const char *ghost_mode_name_ascii(GhostMode mode);

#ifdef __cplusplus
}
#endif
