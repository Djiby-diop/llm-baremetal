#pragma once
/*
 * oo-net — Bare-Metal Network Stack for OO
 * ==========================================
 * No OS. Direct NIC MMIO access.
 *
 * Architecture:
 *   Layer 0: NIC driver (Intel e1000/e1000e, virtio-net for QEMU)
 *   Layer 1: Ethernet II frames
 *   Layer 2: ARP + IPv4
 *   Layer 3: UDP (for swarm communication)
 *   Layer 4: OO Swarm Protocol (OSP) — minimal fixed-size datagrams
 *
 * OO Swarm Protocol (OSP):
 *   - Fixed 512-byte datagrams
 *   - DNA-addressed: each OO instance identified by DNA u32
 *   - No handshake: fire-and-forget UDP, receiver reconstructs state
 *   - Use cases: KV cache broadcast, heartbeat, collective voting
 *
 * Supported NICs:
 *   - Intel e1000 (0x8086:0x100E) — QEMU default
 *   - Intel e1000e (0x8086:0x10D3)
 *   - VirtIO-net (0x1AF4:0x1000) — optional
 *
 * NOVEL: No AI inference system has ever used bare-metal UDP
 * as a swarm coordination protocol without an OS kernel.
 */

#ifndef OO_NET_H
#define OO_NET_H

#include <stdint.h>

#define OO_NET_MTU          1500
#define OO_NET_OSP_SIZE     512
#define OO_NET_RX_RING      32
#define OO_NET_TX_RING      16

/* ── MAC address ────────────────────────────────────────────────── */
typedef struct { uint8_t b[6]; } OoMac;
typedef struct { uint8_t b[4]; } OoIpv4;

/* ── NIC type ────────────────────────────────────────────────────── */
typedef enum {
    OO_NIC_NONE     = 0,
    OO_NIC_E1000    = 1,  /* Intel e1000 */
    OO_NIC_E1000E   = 2,  /* Intel e1000e */
    OO_NIC_VIRTIO   = 3,  /* VirtIO-net */
} OoNicType;

/* ── NIC descriptor ──────────────────────────────────────────────── */
typedef struct {
    OoNicType  type;
    uint64_t   mmio_base;     /* BAR0 MMIO address */
    OoMac      mac;
    int        initialized;
    uint32_t   rx_errors;
    uint32_t   tx_errors;
    uint64_t   rx_bytes;
    uint64_t   tx_bytes;
} OoNicDriver;

/* ── OSP (OO Swarm Protocol) datagram ───────────────────────────── */
#define OSP_MAGIC    0x4F535057u  /* "OSPW" */
typedef enum {
    OSP_MSG_HEARTBEAT    = 0,
    OSP_MSG_KV_FRAGMENT  = 1,  /* partial KV cache broadcast */
    OSP_MSG_VOTE         = 2,  /* collective decision */
    OSP_MSG_HALT_QUERY   = 3,  /* ask swarm: should I halt? */
    OSP_MSG_HALT_RESP    = 4,
    OSP_MSG_DNA_ANNOUNCE = 5,  /* "I exist" broadcast */
} OspMsgType;

typedef struct {
    uint32_t    magic;
    uint32_t    sender_dna;
    uint32_t    receiver_dna;  /* 0xFFFFFFFF = broadcast */
    uint32_t    seq;
    uint8_t     msg_type;
    uint8_t     flags;
    uint16_t    payload_len;
    uint8_t     payload[OSP_PAYLOAD_SIZE];
    uint32_t    checksum;
} __attribute__((packed)) OspDatagram;
#define OSP_PAYLOAD_SIZE  (OO_NET_OSP_SIZE - 20)

/* ── Network context ─────────────────────────────────────────────── */
typedef struct {
    int         enabled;
    OoNicDriver nic;
    OoMac       my_mac;
    OoIpv4      my_ip;
    OoIpv4      broadcast_ip;
    uint16_t    osp_port;     /* default: 9999 */
    uint32_t    my_dna;
    uint32_t    seq;
    /* receive buffer */
    uint8_t     rx_buf[OO_NET_RX_RING][OO_NET_MTU];
    int         rx_head, rx_tail;
    /* stats */
    uint32_t    osp_sent;
    uint32_t    osp_recv;
    uint32_t    arp_sent;
} OoNetCtx;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * oo_net_probe() — scan PCI bus for supported NIC, returns 1 if found
 */
int  oo_net_probe(OoNicDriver *nic);

/**
 * oo_net_init() — initialize NIC, configure DMA rings
 */
int  oo_net_init(OoNetCtx *ctx, uint32_t my_dna,
                  uint8_t ip[4], uint16_t osp_port);

/**
 * oo_net_send_osp() — send OSP datagram (broadcast or unicast)
 */
int  oo_net_send_osp(OoNetCtx *ctx, OspMsgType type,
                      uint32_t dest_dna, const void *payload, int len);

/**
 * oo_net_poll() — check for incoming datagrams, returns count
 */
int  oo_net_poll(OoNetCtx *ctx);

/**
 * oo_net_recv_osp() — dequeue one received OSP datagram
 */
int  oo_net_recv_osp(OoNetCtx *ctx, OspDatagram *out);

/**
 * oo_net_heartbeat() — broadcast heartbeat (call every ~1s from tick)
 */
void oo_net_heartbeat(OoNetCtx *ctx);

void oo_net_print(const OoNetCtx *ctx);

#endif /* OO_NET_H */
