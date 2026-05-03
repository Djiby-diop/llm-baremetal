/* oo_net_core.c — OO Bare-Metal Network Stack */
#include "oo_net_core.h"

/* ── Global state ─────────────────────────────────────────────────── */
OoNetState g_oo_net;
static EFI_SIMPLE_NETWORK_PROTOCOL *g_snp = NULL;
static UINT8 g_tx_buf[OO_NET_MTU + 4];
static UINT8 g_rx_buf[OO_NET_MTU + 4];

/* ── Packet structs ───────────────────────────────────────────────── */
typedef struct __attribute__((packed)) { UINT8 dst[6]; UINT8 src[6]; UINT16 etype; } EthHdr;
typedef struct __attribute__((packed)) {
    UINT8 ver_ihl; UINT8 tos; UINT16 len; UINT16 id; UINT16 frag;
    UINT8 ttl; UINT8 proto; UINT16 csum; UINT32 src; UINT32 dst;
} Ip4Hdr;
typedef struct __attribute__((packed)) { UINT16 sport; UINT16 dport; UINT16 len; UINT16 csum; } UdpHdr;
typedef struct __attribute__((packed)) {
    UINT8 op; UINT8 htype; UINT8 hlen; UINT8 hops;
    UINT32 xid; UINT16 secs; UINT16 flags;
    UINT32 ciaddr; UINT32 yiaddr; UINT32 siaddr; UINT32 giaddr;
    UINT8 chaddr[16]; UINT8 sname[64]; UINT8 file[128];
    UINT32 cookie; UINT8 opts[128];
} DhcpMsg;

/* ── Utilities ────────────────────────────────────────────────────── */
static void net_zero(void *p, int n) { for(int i=0;i<n;i++) ((UINT8*)p)[i]=0; }
static void net_copy(void *d, const void *s, int n) { for(int i=0;i<n;i++) ((UINT8*)d)[i]=((const UINT8*)s)[i]; }

static UINT16 ip_checksum(const void *data, int len) {
    const UINT16 *p = (const UINT16*)data;
    UINT32 sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const UINT8*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (UINT16)~sum;
}

static UINT32 rdtsc_lo(void) {
    UINT32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo;
}

UINT32 oo_net_crc32(const void *data, int len) {
    static UINT32 t[256]; static int ready=0;
    if (!ready) { for(int i=0;i<256;i++){UINT32 c=i;for(int j=0;j<8;j++)c=(c&1)?(0xEDB88320^(c>>1)):(c>>1);t[i]=c;} ready=1; }
    const UINT8 *p=(const UINT8*)data; UINT32 crc=0xFFFFFFFF;
    for(int i=0;i<len;i++) crc=t[(crc^p[i])&0xFF]^(crc>>8);
    return crc^0xFFFFFFFF;
}

void oo_net_print_ip(UINT32 ip_le) {
    Print(L"%d.%d.%d.%d",
        ip_le&0xFF, (ip_le>>8)&0xFF,
        (ip_le>>16)&0xFF, (ip_le>>24)&0xFF);
}

/* ── SNP Init ─────────────────────────────────────────────────────── */
int oo_net_init_best_effort(void) {
    net_zero(&g_oo_net, sizeof(g_oo_net));
    EFI_GUID snp_guid = EFI_SIMPLE_NETWORK_PROTOCOL;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &snp_guid, NULL, &count, &handles);
    if (EFI_ERROR(st) || count == 0) return 0;

    for (UINTN i = 0; i < count; i++) {
        EFI_SIMPLE_NETWORK_PROTOCOL *snp = NULL;
        st = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i], &snp_guid, (void**)&snp);
        if (EFI_ERROR(st) || !snp) continue;
        if (snp->Mode->State == EfiSimpleNetworkStopped) {
            st = uefi_call_wrapper(snp->Start, 1, snp);
            if (EFI_ERROR(st)) continue;
        }
        if (snp->Mode->State == EfiSimpleNetworkStarted) {
            st = uefi_call_wrapper(snp->Initialize, 3, snp, 0, 0);
            if (EFI_ERROR(st)) continue;
        }
        /* Enable broadcast + unicast receive */
        uefi_call_wrapper(snp->ReceiveFilters, 6, snp,
            EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
            EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
            0, FALSE, 0, NULL);

        net_copy(g_oo_net.mac, snp->Mode->CurrentAddress.Addr, 6);
        g_snp = snp;
        g_oo_net.eth_ready = 1;

        /* Build hostname "oo-XXYY" from last 2 MAC bytes */
        UINT8 *m = g_oo_net.mac;
        g_oo_net.hostname[0]='o'; g_oo_net.hostname[1]='o';
        g_oo_net.hostname[2]='-';
        const char hex[]="0123456789abcdef";
        g_oo_net.hostname[3]=hex[(m[4]>>4)&0xF];
        g_oo_net.hostname[4]=hex[m[4]&0xF];
        g_oo_net.hostname[5]=hex[(m[5]>>4)&0xF];
        g_oo_net.hostname[6]=hex[m[5]&0xF];
        g_oo_net.hostname[7]=0;
        break;
    }
    uefi_call_wrapper(BS->FreePool, 1, handles);
    return g_oo_net.eth_ready;
}

/* ── Low-level TX ─────────────────────────────────────────────────── */
static int snp_tx(const void *frame, UINTN frame_len) {
    if (!g_snp) return 0;
    /* Wait for TX buffer available */
    for (int retry = 0; retry < 200; retry++) {
        void *txbuf = NULL;
        EFI_STATUS st2 = uefi_call_wrapper(g_snp->GetStatus, 3, g_snp, NULL, &txbuf);
        if (!EFI_ERROR(st2)) break;
        uefi_call_wrapper(BS->Stall, 1, 1000);
    }
    EFI_STATUS st = uefi_call_wrapper(g_snp->Transmit, 7, g_snp,
        0, frame_len, (void*)frame, NULL, NULL, NULL);
    if (!EFI_ERROR(st)) { g_oo_net.tx_frames++; return 1; }
    return 0;
}

/* ── Low-level RX ─────────────────────────────────────────────────── */
static int snp_rx(void *buf, UINTN *len) {
    if (!g_snp) return 0;
    EFI_STATUS st = uefi_call_wrapper(g_snp->Receive, 7, g_snp,
        NULL, len, buf, NULL, NULL, NULL);
    if (!EFI_ERROR(st)) { g_oo_net.rx_frames++; return 1; }
    return 0;
}

/* ── UDP Send ─────────────────────────────────────────────────────── */
int oo_net_udp_send(UINT32 dst_ip, UINT16 dst_port, UINT16 src_port,
                    const void *payload, UINT16 plen) {
    if (!g_snp) return 0;
    net_zero(g_tx_buf, sizeof(g_tx_buf));

    /* Ethernet */
    EthHdr *eth = (EthHdr*)g_tx_buf;
    if (dst_ip == OO_IP_BCAST) {
        for(int i=0;i<6;i++) eth->dst[i]=0xFF;
    } else {
        /* Use ARP cache or fallback to broadcast */
        int found = 0;
        for (int i=0;i<OO_NET_ARP_CACHE_MAX;i++) {
            if (g_oo_net.arp[i].valid && g_oo_net.arp[i].ip == dst_ip) {
                net_copy(eth->dst, g_oo_net.arp[i].mac, 6);
                found = 1; break;
            }
        }
        if (!found) for(int i=0;i<6;i++) eth->dst[i]=0xFF; /* broadcast fallback */
    }
    net_copy(eth->src, g_oo_net.mac, 6);
    eth->etype = OO_HTONS(0x0800);

    /* IP */
    Ip4Hdr *ip = (Ip4Hdr*)(g_tx_buf + 14);
    ip->ver_ihl = 0x45; ip->tos = 0;
    UINT16 ip_len = (UINT16)(20 + 8 + plen);
    ip->len = OO_HTONS(ip_len);
    ip->id = OO_HTONS((UINT16)rdtsc_lo());
    ip->frag = 0; ip->ttl = 64; ip->proto = 17;
    ip->src = OO_HTONL(g_oo_net.ip_ready ? g_oo_net.ip : 0);
    ip->dst = OO_HTONL(dst_ip);
    ip->csum = 0;
    ip->csum = ip_checksum(ip, 20);

    /* UDP */
    UdpHdr *udp = (UdpHdr*)(g_tx_buf + 14 + 20);
    udp->sport = OO_HTONS(src_port);
    udp->dport = OO_HTONS(dst_port);
    udp->len   = OO_HTONS((UINT16)(8 + plen));
    udp->csum  = 0; /* optional for IPv4 */

    net_copy(g_tx_buf + 14 + 20 + 8, payload, plen);
    return snp_tx(g_tx_buf, 14 + ip_len);
}

/* ── UDP Receive (poll) ────────────────────────────────────────────── */
int oo_net_udp_recv_poll(UINT16 port, void *buf, UINT16 buf_len,
                         UINT32 *src_ip, UINT16 *src_port, UINT16 *recv_len) {
    if (!g_snp) return 0;
    UINTN sz = sizeof(g_rx_buf);
    if (!snp_rx(g_rx_buf, &sz)) return 0;
    if (sz < 14+20+8) return 0;
    EthHdr  *eth = (EthHdr*)g_rx_buf;
    if (OO_NTOHS(eth->etype) != 0x0800) return 0;
    Ip4Hdr  *ip  = (Ip4Hdr*)(g_rx_buf + 14);
    if ((ip->ver_ihl & 0xF0) != 0x40) return 0;
    if (ip->proto != 17) return 0;
    UdpHdr  *udp = (UdpHdr*)(g_rx_buf + 14 + 20);
    if (OO_NTOHS(udp->dport) != port) return 0;
    UINT16 plen = (UINT16)(OO_NTOHS(udp->len) - 8);
    if (plen > buf_len) plen = buf_len;
    net_copy(buf, g_rx_buf + 14 + 20 + 8, plen);
    if (src_ip)   *src_ip   = OO_NTOHL(ip->src);
    if (src_port) *src_port = OO_NTOHS(udp->sport);
    if (recv_len) *recv_len = plen;
    return 1;
}

/* ── DHCP Client ──────────────────────────────────────────────────── */
static void dhcp_build_discover(DhcpMsg *d, UINT32 xid, const UINT8 *mac) {
    net_zero(d, sizeof(*d));
    d->op = 1; d->htype = 1; d->hlen = 6; d->hops = 0;
    d->xid = OO_HTONL(xid);
    d->flags = OO_HTONS(0x8000); /* broadcast flag */
    net_copy(d->chaddr, mac, 6);
    d->cookie = OO_HTONL(0x63825363);
    /* Options: DHCP type = DISCOVER */
    d->opts[0]=53; d->opts[1]=1; d->opts[2]=1; /* DHCP Discover */
    d->opts[3]=55; d->opts[4]=4; /* Parameter request list */
    d->opts[5]=1;  /* subnet */
    d->opts[6]=3;  /* router */
    d->opts[7]=6;  /* DNS */
    d->opts[8]=51; /* lease time */
    d->opts[9]=255; /* end */
}

static int dhcp_parse_offer(const DhcpMsg *d, UINT32 xid,
                             UINT32 *yip, UINT32 *mask, UINT32 *gw) {
    if (OO_NTOHL(d->cookie) != 0x63825363) return 0;
    if (OO_NTOHL(d->xid) != xid) return 0;
    if (d->op != 2) return 0;
    *yip  = OO_NTOHL(d->yiaddr);
    *mask = 0; *gw = 0;
    /* Parse options */
    const UINT8 *o = d->opts;
    while (*o != 255 && o < d->opts + 128) {
        UINT8 code = *o++;
        if (code == 0) continue;
        UINT8 len = *o++;
        if (code == 1 && len >= 4) { /* subnet */
            UINT32 v = ((UINT32)o[0]<<24)|((UINT32)o[1]<<16)|((UINT32)o[2]<<8)|o[3];
            /* store as host LE */
            *mask = OO_NTOHL(v);
        }
        if (code == 3 && len >= 4) { /* router */
            UINT32 v = ((UINT32)o[0]<<24)|((UINT32)o[1]<<16)|((UINT32)o[2]<<8)|o[3];
            *gw = OO_NTOHL(v);
        }
        o += len;
    }
    return 1;
}

static void dhcp_build_request(DhcpMsg *d, UINT32 xid,
                                const UINT8 *mac, UINT32 yip) {
    dhcp_build_discover(d, xid, mac);
    /* Override options: REQUEST */
    d->opts[2] = 3; /* DHCP Request (not Discover) */
    /* Option 50: Requested IP */
    d->opts[9]=50; d->opts[10]=4;
    d->opts[11]=(yip>>24)&0xFF; d->opts[12]=(yip>>16)&0xFF;
    d->opts[13]=(yip>>8)&0xFF;  d->opts[14]=yip&0xFF;
    d->opts[15]=255;
}

int oo_net_dhcp_best_effort(int timeout_s) {
    if (!g_snp || !g_oo_net.eth_ready) return 0;
    g_oo_net.dhcp_attempts++;
    g_oo_net.dhcp_xid = rdtsc_lo() ^ 0xDEAD4200U;
    UINT32 xid = g_oo_net.dhcp_xid;

    /* Send DHCP DISCOVER */
    DhcpMsg disc;
    dhcp_build_discover(&disc, xid, g_oo_net.mac);
    oo_net_udp_send(OO_IP_BCAST, OO_NET_DHCP_SERVER, OO_NET_DHCP_CLIENT,
                    &disc, (UINT16)sizeof(disc));

    /* Wait for OFFER */
    UINT32 yip=0, mask=0, gw=0;
    int got_offer = 0;
    UINTN total_us = (UINTN)timeout_s * 1000000UL;
    UINTN waited  = 0;
    while (waited < total_us) {
        UINTN sz = sizeof(g_rx_buf);
        if (snp_rx(g_rx_buf, &sz) && sz >= 14+20+8+(int)sizeof(DhcpMsg)) {
            UdpHdr *u = (UdpHdr*)(g_rx_buf+14+20);
            if (OO_NTOHS(u->dport) == OO_NET_DHCP_CLIENT) {
                DhcpMsg *rep = (DhcpMsg*)(g_rx_buf+14+20+8);
                if (dhcp_parse_offer(rep, xid, &yip, &mask, &gw)) {
                    got_offer = 1; break;
                }
            }
        }
        uefi_call_wrapper(BS->Stall, 1, 10000); /* 10ms */
        waited += 10000;
    }
    if (!got_offer) return 0;

    /* Send DHCP REQUEST */
    DhcpMsg req;
    dhcp_build_request(&req, xid, g_oo_net.mac, yip);
    oo_net_udp_send(OO_IP_BCAST, OO_NET_DHCP_SERVER, OO_NET_DHCP_CLIENT,
                    &req, (UINT16)sizeof(req));

    /* Wait for ACK (type 5) */
    int got_ack = 0;
    waited = 0;
    while (waited < 3000000UL) {
        UINTN sz = sizeof(g_rx_buf);
        if (snp_rx(g_rx_buf, &sz) && sz >= 14+20+8+(int)sizeof(DhcpMsg)) {
            UdpHdr *u = (UdpHdr*)(g_rx_buf+14+20);
            if (OO_NTOHS(u->dport) == OO_NET_DHCP_CLIENT) {
                DhcpMsg *rep = (DhcpMsg*)(g_rx_buf+14+20+8);
                /* Check xid + op */
                if (rep->op == 2 && OO_NTOHL(rep->xid) == xid) {
                    /* Check option 53 = 5 (ACK) */
                    UINT8 *o = rep->opts;
                    while (*o != 255 && o < rep->opts+128) {
                        UINT8 code = *o++; UINT8 len = *o++;
                        if (code == 53 && len==1 && *o == 5) { got_ack=1; break; }
                        o += len;
                    }
                    if (got_ack) break;
                }
            }
        }
        uefi_call_wrapper(BS->Stall, 1, 10000);
        waited += 10000;
    }
    if (!got_ack) return 0;

    g_oo_net.ip      = yip;
    g_oo_net.subnet  = mask;
    g_oo_net.gateway = gw;
    g_oo_net.ip_ready = 1;
    return 1;
}

void oo_net_set_static(UINT32 ip, UINT32 gw, UINT32 mask) {
    g_oo_net.ip      = ip;
    g_oo_net.gateway = gw;
    g_oo_net.subnet  = mask;
    g_oo_net.ip_ready = 1;
}

/* ── BootSwarm Announce ────────────────────────────────────────────── */
int oo_net_boot_announce(UINT32 dna_gen, UINT32 fitness,
                         UINT16 n_layers, UINT32 model_hash) {
    if (!g_oo_net.eth_ready) return 0;
    OoBootAnnounce a;
    net_zero(&a, sizeof(a));
    a.magic      = OO_HTONL(OO_BOOT_ANNOUNCE_MAGIC);
    a.version    = OO_HTONL(OO_NET_VERSION);
    net_copy(a.mac, g_oo_net.mac, 6);
    a.port       = OO_HTONS(OO_NET_SWARM_PORT);
    a.ip         = OO_HTONL(g_oo_net.ip);
    a.n_layers   = OO_HTONS(n_layers);
    a.domain_mask= OO_HTONS(0x7F);
    a.dna_gen    = OO_HTONL(dna_gen);
    a.fitness    = OO_HTONL(fitness);
    a.model_hash = OO_HTONL(model_hash);
    a.ram_mb     = 0;
    a.crc32      = 0;
    a.crc32      = OO_HTONL(oo_net_crc32(&a, (int)sizeof(a)-4));
    return oo_net_udp_send(OO_IP_BCAST, OO_NET_SWARM_PORT, OO_NET_SWARM_PORT,
                           &a, (UINT16)sizeof(a));
}

int oo_net_swarm_recv_poll(OoBootAnnounce *out) {
    UINT16 rlen = 0;
    if (!oo_net_udp_recv_poll(OO_NET_SWARM_PORT,
                              out, (UINT16)sizeof(*out),
                              NULL, NULL, &rlen)) return 0;
    if (rlen < (UINT16)sizeof(*out)) return 0;
    if (OO_NTOHL(out->magic) != OO_BOOT_ANNOUNCE_MAGIC) return 0;
    return 1;
}

/* ── Status ────────────────────────────────────────────────────────── */
void oo_net_print_status(void) {
    Print(L"\r\n[OO-NET] eth=%d  ip=%d\r\n",
          g_oo_net.eth_ready, g_oo_net.ip_ready);
    if (g_oo_net.eth_ready) {
        UINT8 *m = g_oo_net.mac;
        Print(L"  MAC : %02x:%02x:%02x:%02x:%02x:%02x\r\n",
              m[0],m[1],m[2],m[3],m[4],m[5]);
        Print(L"  Host: %a\r\n", g_oo_net.hostname);
    }
    if (g_oo_net.ip_ready) {
        Print(L"  IP  : "); oo_net_print_ip(g_oo_net.ip);     Print(L"\r\n");
        Print(L"  GW  : "); oo_net_print_ip(g_oo_net.gateway); Print(L"\r\n");
        Print(L"  Mask: "); oo_net_print_ip(g_oo_net.subnet);  Print(L"\r\n");
    }
    Print(L"  TX=%d RX=%d DHCP_tries=%d\r\n\r\n",
          g_oo_net.tx_frames, g_oo_net.rx_frames, g_oo_net.dhcp_attempts);
}
