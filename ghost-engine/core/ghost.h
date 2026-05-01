#pragma once

/*
 * Ghost: Ghost in the Boot (Inter-OO Communication)
 *
 * Two Operating Organisms talk without TCP/IP. Optical pulses via keyboard
 * LEDs, or audio via PC speaker. Tokens exchanged over invisible channel.
 * OO-NET packets (32 bytes) transmitted byte-by-byte via LED blink encoding.
 */

#include <stdint.h>
#include "oo_net_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GHOST_MODE_OFF   = 0,
    GHOST_MODE_SEND  = 1,  /* emit tokens/packets */
    GHOST_MODE_RECV  = 2,  /* listen for tokens/packets */
} GhostMode;

typedef enum {
    GHOST_CHANNEL_LED   = 0,  /* keyboard LEDs (caps/num/scroll) */
    GHOST_CHANNEL_PCSPK = 1,  /* PC speaker beeps */
} GhostChannel;

#define GHOST_RING_MAX    16  /* power of 2 — ring buffer for received tokens */
#define GHOST_PKT_RX_MAX   4  /* max queued received OO-NET packets */

typedef struct {
    GhostMode    mode;
    GhostChannel channel;
    uint32_t     tokens_sent;
    uint32_t     tokens_recv;
    /* Token receive ring buffer */
    uint32_t     ring[GHOST_RING_MAX];
    unsigned int ring_head;
    unsigned int ring_tail;
    unsigned int ring_len;
    /* OO-NET packet layer */
    uint32_t     pkts_sent;
    uint32_t     pkts_recv;
    uint8_t      tx_seq;                           /* monotonic TX sequence */
    OoNetPacket  pkt_rx_queue[GHOST_PKT_RX_MAX];   /* received packet queue */
    uint8_t      pkt_rx_head;
    uint8_t      pkt_rx_tail;
    uint8_t      pkt_rx_len;
} GhostEngine;

void ghost_init(GhostEngine *e);
void ghost_set_mode(GhostEngine *e, GhostMode mode);
void ghost_set_channel(GhostEngine *e, GhostChannel ch);

/* Token-level API (existing) */
void     ghost_send_token(GhostEngine *e, uint32_t token);
uint32_t ghost_recv_token(GhostEngine *e);
void     ghost_observe(GhostEngine *e, uint16_t ch);
void     ghost_led_encode(GhostEngine *e, uint8_t *led_bits);
void     ghost_led_write(uint8_t led_bits);

/* OO-NET packet API (new) */

/* Transmit a full OO-NET packet (32 bytes) via LED blink encoding.
 * Each byte serialized as 8 LED toggles (1 bit = 1 Caps-lock state change).
 * On USB-only hardware: falls back to ring-buffer loopback (same machine). */
void ghost_send_packet(GhostEngine *e, const OoNetPacket *pkt);

/* Poll receive queue. Returns 1 and fills *out if a packet is ready, else 0. */
int ghost_recv_packet(GhostEngine *e, OoNetPacket *out);

/* Inject a received raw 32-byte frame (called when sniffing LED/SPK channel). */
void ghost_inject_frame(GhostEngine *e, const uint8_t frame[OO_NET_PKT_SIZE]);

const char *ghost_mode_name_ascii(GhostMode mode);

#ifdef __cplusplus
}
#endif

