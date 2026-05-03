/* oo_net_core.h — OO Bare-Metal Network Stack
 * Phase N: Ethernet + DHCP + UDP + BootSwarm Discovery
 *
 * Usage (soma_boot.c):
 *   oo_net_init_best_effort();
 *   oo_net_dhcp_best_effort(5);
 *   oo_net_boot_announce(dna_gen, fitness, n_layers);
 */
#ifndef OO_NET_CORE_H
#define OO_NET_CORE_H

#ifdef UEFI_BUILD
#include <efi.h>
#include <efilib.h>
#include <efinet.h>
#else
#include "efi_compat.h"
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */
#define OO_NET_MAGIC          0x4F4F4E54U   /* "OONT" */
#define OO_NET_VERSION        1
#define OO_NET_SWARM_PORT     42420
#define OO_NET_DHCP_CLIENT    68
#define OO_NET_DHCP_SERVER    67
#define OO_NET_MTU            1514
#define OO_NET_HOSTNAME_MAX   32
#define OO_NET_ARP_CACHE_MAX  8
#define OO_NET_DHCP_TIMEOUT_S 6            /* seconds to wait for DHCP */

/* Byte-order helpers (x86 little-endian → big-endian network) */
#define OO_HTONS(x) ((UINT16)(((UINT16)(x) >> 8) | ((UINT16)(x) << 8)))
#define OO_NTOHS(x) OO_HTONS(x)
#define OO_HTONL(x) ((UINT32)(          \
    (((UINT32)(x)) >> 24)             | \
    ((((UINT32)(x)) >> 8)  & 0xFF00U) | \
    ((((UINT32)(x)) << 8)  & 0xFF0000U)| \
    (((UINT32)(x)) << 24)))
#define OO_NTOHL(x) OO_HTONL(x)

/* IPv4 helper macros */
#define OO_IPV4(a,b,c,d) ((UINT32)((a)|((b)<<8)|((c)<<16)|((d)<<24))) /* host order LE */
#define OO_IP_BCAST       0xFFFFFFFFU

/* ── ARP cache entry ───────────────────────────────────────────────────── */
typedef struct {
    UINT32  ip;
    UINT8   mac[6];
    UINT8   valid;
    UINT8   _pad;
} OoArpEntry;

/* ── Global Network State ──────────────────────────────────────────────── */
typedef struct {
    /* Link layer */
    UINT8   mac[6];
    UINT8   _pad[2];

    /* IP layer (host byte order) */
    UINT32  ip;
    UINT32  gateway;
    UINT32  subnet;
    UINT32  dns;

    /* Status flags */
    int     eth_ready;      /* SNP started + initialized */
    int     ip_ready;       /* DHCP success or static set */
    int     wifi_ready;     /* EFI WiFi2 (future) */

    /* Identification */
    char    hostname[OO_NET_HOSTNAME_MAX]; /* "oo-XXXX" from MAC */

    /* ARP cache */
    OoArpEntry arp[OO_NET_ARP_CACHE_MAX];

    /* Stats */
    UINT32  tx_frames;
    UINT32  rx_frames;
    UINT32  dhcp_attempts;
    UINT32  dhcp_xid;       /* last DHCP transaction ID */
} OoNetState;

/* ── BootSwarm Announce Packet ─────────────────────────────────────────── */
#define OO_BOOT_ANNOUNCE_MAGIC  0x4F4F424FU  /* "OOBO" */
typedef struct __attribute__((packed)) {
    UINT32  magic;
    UINT32  version;
    UINT8   mac[6];
    UINT16  port;
    UINT32  ip;
    UINT16  n_layers;       /* transformer layers this OO can serve */
    UINT16  domain_mask;    /* SWARM_DOMAIN_* bitfield */
    UINT32  dna_gen;
    UINT32  fitness;        /* x10000 */
    UINT32  model_hash;     /* quick hash of loaded model name */
    UINT32  ram_mb;         /* approximate free RAM in MB */
    UINT32  crc32;
} OoBootAnnounce;           /* 44 bytes */

/* ── Global instance ───────────────────────────────────────────────────── */
extern OoNetState g_oo_net;

/* ── API ───────────────────────────────────────────────────────────────── */

/* Init: find EFI SNP, start + initialize the first NIC found.
 * Returns 1 on success, 0 on failure (no NIC, no SNP). */
int  oo_net_init_best_effort(void);

/* DHCP: send DISCOVER, wait for OFFER+ACK. timeout_s = max wait seconds.
 * Returns 1 if IP acquired, 0 on timeout/failure. */
int  oo_net_dhcp_best_effort(int timeout_s);

/* Static IP fallback (set manually, no DHCP needed). */
void oo_net_set_static(UINT32 ip_le, UINT32 gw_le, UINT32 mask_le);

/* Send UDP datagram. dst_ip in host byte order (little-endian on x86).
 * Returns 1 on success. */
int  oo_net_udp_send(UINT32 dst_ip, UINT16 dst_port, UINT16 src_port,
                     const void *payload, UINT16 payload_len);

/* Poll for incoming UDP on a given port. Non-blocking (returns 0 if none).
 * src_ip/src_port filled if non-NULL. buf_len = capacity of buf. */
int  oo_net_udp_recv_poll(UINT16 port, void *buf, UINT16 buf_len,
                          UINT32 *src_ip, UINT16 *src_port, UINT16 *recv_len);

/* Broadcast BootSwarm ANNOUNCE to 255.255.255.255:OO_NET_SWARM_PORT.
 * Called once after DHCP succeeds. */
int  oo_net_boot_announce(UINT32 dna_gen, UINT32 fitness,
                          UINT16 n_layers, UINT32 model_hash);

/* Listen for BootSwarm ANNOUNCEs from peers (non-blocking poll).
 * Fills out and returns 1 if a valid announce was received. */
int  oo_net_swarm_recv_poll(OoBootAnnounce *out);

/* Print current network status to console. */
void oo_net_print_status(void);

/* Print IP address in dotted decimal. */
void oo_net_print_ip(UINT32 ip_le);

/* Simple CRC32 (no stdlib). */
UINT32 oo_net_crc32(const void *data, int len);

#endif /* OO_NET_CORE_H */
