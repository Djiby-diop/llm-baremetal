/*
 * Ghost: Ghost in the Boot - core implementation.
 * LED/speaker covert channel for inter-OO token communication.
 * Platform: PS/2 8042 I/O ports (0x60 cmd, 0x64 data) + PC speaker (0x61).
 * USB-only hardware: LED write silently no-ops (8042 absent).
 */

#include "ghost.h"

/* ── Freestanding I/O port helpers ─────────────────────────────────────── */
#ifndef __GHOST_IO__
#define __GHOST_IO__
static inline void ghost_outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}
static inline unsigned char ghost_inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
static inline void ghost_io_wait(void) {
    /* brief delay via POST port */
    __asm__ volatile("outb %al, $0x80");
}
#endif

void ghost_init(GhostEngine *e) {
    if (!e) return;
    e->mode         = GHOST_MODE_OFF;
    e->channel      = GHOST_CHANNEL_LED;
    e->tokens_sent  = 0;
    e->tokens_recv  = 0;
    e->ring_head    = 0;
    e->ring_tail    = 0;
    e->ring_len     = 0;
    e->pkts_sent    = 0;
    e->pkts_recv    = 0;
    e->tx_seq       = 0;
    e->pkt_rx_head  = 0;
    e->pkt_rx_tail  = 0;
    e->pkt_rx_len   = 0;
    for (int i = 0; i < GHOST_RING_MAX; i++) e->ring[i] = 0;
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
    /* Drain one slot from ring if available */
    if (e->ring_len > 0) {
        uint32_t tok = e->ring[e->ring_head & (GHOST_RING_MAX - 1)];
        e->ring_head++;
        e->ring_len--;
        e->tokens_recv++;
        return tok;
    }
    return 0xFFFFFFFFU;
}

const char *ghost_mode_name_ascii(GhostMode mode) {
    switch (mode) {
        case GHOST_MODE_OFF:  return "off";
        case GHOST_MODE_SEND: return "send";
        case GHOST_MODE_RECV: return "recv";
        default:              return "?";
    }
}

/* Passive observation: silently log every bus channel into a 3-slot ring */
void ghost_observe(GhostEngine *e, uint16_t ch) {
    if (!e) return;
    unsigned int slot = e->ring_tail & (GHOST_RING_MAX - 1);
    e->ring[slot] = (uint32_t)ch;
    e->ring_tail++;
    if (e->ring_len < GHOST_RING_MAX)
        e->ring_len++;
    else
        e->ring_head++;  /* overwrite oldest */
}

/* Encode last 3 observed channels as 3-bit LED pattern (Num/Caps/Scroll) */
void ghost_led_encode(GhostEngine *e, uint8_t *led_bits) {
    if (!e || !led_bits) return;
    uint8_t bits = 0;
    for (int i = 0; i < 3 && i < (int)e->ring_len; i++) {
        unsigned int idx = (e->ring_tail - 1 - i) & (GHOST_RING_MAX - 1);
        uint32_t ch = e->ring[idx];
        bits |= (uint8_t)((ch & 1u) << i);  /* LSB of channel id → LED bit i */
    }
    *led_bits = bits & 0x07u;  /* Scroll=bit0, Num=bit1, Caps=bit2 */
}

/* Write LED pattern to PS/2 8042 keyboard controller.
 * Protocol: send 0xED command byte, wait ACK (0xFA), send LED byte.
 * Silent no-op on USB-only hardware (no 8042 present → status never clears). */
void ghost_led_write(uint8_t led_bits) {
    /* Wait for 8042 input buffer empty (bit 1 of status reg 0x64) */
    unsigned int retries = 0x10000u;
    while ((ghost_inb(0x64) & 0x02u) && --retries) ghost_io_wait();
    if (!retries) return;   /* 8042 absent or busy — silent no-op */

    ghost_outb(0x60, 0xED);  /* Set LEDs command */
    ghost_io_wait();

    /* Wait for ACK */
    retries = 0x10000u;
    while ((ghost_inb(0x64) & 0x01u) == 0 && --retries) ghost_io_wait();
    if (ghost_inb(0x60) != 0xFA) return;  /* no ACK — abort */

    /* Send LED byte */
    retries = 0x10000u;
    while ((ghost_inb(0x64) & 0x02u) && --retries) ghost_io_wait();
    ghost_outb(0x60, led_bits & 0x07u);
}

/* ── OO-NET Packet layer ──────────────────────────────────────────────────
 *
 * Physical encoding: each byte of the 32-byte frame is transmitted as 8
 * successive LED writes, toggling Caps-Lock bit (bit 2) to represent each
 * data bit (MSB first). The receiving OO machine reads its keyboard LED
 * status register on a polling loop.
 *
 * On USB-only hardware (no PS/2 8042): ghost_led_write() is a no-op, so
 * ghost_send_packet() falls back to *loopback* — it injects the packet
 * directly into the local receive queue. Useful for single-machine testing.
 */

static void ghost_pkt_enqueue_rx(GhostEngine *e, const OoNetPacket *pkt) {
    if (e->pkt_rx_len >= GHOST_PKT_RX_MAX) return;  /* queue full — drop */
    uint8_t slot = e->pkt_rx_tail % GHOST_PKT_RX_MAX;
    e->pkt_rx_queue[slot] = *pkt;
    e->pkt_rx_tail++;
    e->pkt_rx_len++;
    e->pkts_recv++;
}

void ghost_send_packet(GhostEngine *e, const OoNetPacket *pkt) {
    if (!e || !pkt) return;
    if (e->mode == GHOST_MODE_OFF) return;

    uint8_t frame[OO_NET_PKT_SIZE];
    oo_net_pkt_to_bytes(pkt, frame);

    /* Detect 8042 presence: try one LED write and see if status clears.
     * If retries exhaust immediately → USB-only → loopback. */
    unsigned int probe = 0x1000u;
    while ((ghost_inb(0x64) & 0x02u) && --probe) ghost_io_wait();
    int has_8042 = (probe > 0);

    if (has_8042) {
        /* Transmit 32 bytes × 8 bits via Caps-Lock LED toggle */
        for (int b = 0; b < OO_NET_PKT_SIZE; b++) {
            uint8_t byte = frame[b];
            for (int bit = 7; bit >= 0; bit--) {
                uint8_t led = (uint8_t)(((byte >> bit) & 1u) << 2); /* Caps bit */
                ghost_led_write(led);
                /* brief inter-bit delay (~1 µs at 1GHz) */
                for (volatile int d = 0; d < 200; d++) {}
            }
        }
    } else {
        /* Loopback: inject into own RX queue */
        ghost_pkt_enqueue_rx(e, pkt);
    }

    e->pkts_sent++;
    e->tx_seq++;
}

int ghost_recv_packet(GhostEngine *e, OoNetPacket *out) {
    if (!e || !out) return 0;
    if (e->pkt_rx_len == 0) return 0;
    uint8_t slot = e->pkt_rx_head % GHOST_PKT_RX_MAX;
    *out = e->pkt_rx_queue[slot];
    e->pkt_rx_head++;
    e->pkt_rx_len--;
    return 1;
}

void ghost_inject_frame(GhostEngine *e, const uint8_t frame[OO_NET_PKT_SIZE]) {
    if (!e || !frame) return;
    OoNetPacket pkt;
    if (oo_net_pkt_from_bytes(&pkt, frame))
        ghost_pkt_enqueue_rx(e, &pkt);
}
