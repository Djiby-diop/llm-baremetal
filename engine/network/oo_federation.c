/* oo_federation.c — OO Federation Protocol  Phase 4E
 * =====================================================
 * Peer discovery, patch sharing, capability exchange.
 * Uses EFI HTTP for data transfer (leverages oo_netboot internals).
 * Freestanding C11. No libc.
 */
#include "oo_federation.h"
#include "oo_netboot.h"
#include <efi.h>
#include <efilib.h>

/* Global */
OoFedCtx g_federation;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static UINTN _f_strlen(const CHAR8 *s){UINTN n=0;if(!s)return 0;while(s[n])n++;return n;}
static void  _f_strlcpy(CHAR8*d,const CHAR8*s,UINTN c){
    UINTN i=0;if(!d||!s||c==0)return;while(i+1<c&&s[i]){d[i]=s[i];i++;}d[i]=0;}
static void  _f_memset(void*d,UINT8 v,UINTN n){for(UINTN i=0;i<n;i++)((UINT8*)d)[i]=v;}
static int   _f_cstrcmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}
static int   _f_strncmp8(const CHAR8*a,const CHAR8*b,UINTN n){
    for(UINTN i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}

/* Very simple int → decimal string */
static void _f_itoa(UINT32 v, CHAR8 *buf, UINTN cap){
    if(!buf||cap<2){return;}
    CHAR8 tmp[12]; int n=0;
    if(v==0){buf[0]='0';buf[1]=0;return;}
    while(v&&n<11){tmp[n++]=(CHAR8)('0'+(v%10));v/=10;}
    if(n>=(int)cap)n=(int)cap-1;
    for(int i=0;i<n;i++) buf[i]=tmp[n-1-i];
    buf[n]=0;
}

/* JSON escape into buf, return written length */
static UINTN _f_json_escape(const CHAR8 *s, CHAR8 *dst, UINTN cap) {
    UINTN w=0;
    for(UINTN i=0;s[i]&&w+3<cap;i++){
        if(s[i]=='"'||s[i]=='\\'){if(w+2<cap){dst[w++]='\\';dst[w++]=s[i];}}
        else if(s[i]=='\n'){if(w+2<cap){dst[w++]='\\';dst[w++]='n';}}
        else if(s[i]=='\r'){if(w+2<cap){dst[w++]='\\';dst[w++]='r';}}
        else dst[w++]=s[i];
    }
    dst[w]=0; return w;
}

/* Build HTTP URL: http://<ip>:<port><path> */
static void _f_build_url(CHAR8 *dst, UINTN cap,
                          const CHAR8 *ip, UINT16 port, const CHAR8 *path) {
    UINTN p=0;
    const CHAR8 *pfx=(const CHAR8*)"http://";
    UINTN pfl=_f_strlen(pfx);
    for(UINTN i=0;i<pfl&&p<cap-1;i++) dst[p++]=pfx[i];
    UINTN ipl=_f_strlen(ip);
    for(UINTN i=0;i<ipl&&p<cap-1;i++) dst[p++]=ip[i];
    if(port!=80){
        dst[p++]=':';
        CHAR8 pstr[6]; _f_itoa(port, pstr, 6);
        UINTN pl=_f_strlen(pstr);
        for(UINTN i=0;i<pl&&p<cap-1;i++) dst[p++]=pstr[i];
    }
    UINTN pthl=_f_strlen(path);
    for(UINTN i=0;i<pthl&&p<cap-1;i++) dst[p++]=path[i];
    dst[p]=0;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void oo_fed_init(OoFedCtx *ctx, const CHAR8 *self_node_id) {
    _f_memset(ctx, 0, sizeof(*ctx));
    if(self_node_id) _f_strlcpy(ctx->self_node_id, self_node_id, OO_FED_NODE_ID_LEN);
    else _f_strlcpy(ctx->self_node_id, (const CHAR8*)"oo-unknown", OO_FED_NODE_ID_LEN);

    /* Self capabilities — everything built in */
    ctx->self_caps = OO_FED_CAP_INFERENCE | OO_FED_CAP_NETBOOT |
                     OO_FED_CAP_SELFIMPROVE | OO_FED_CAP_VOICE;
    ctx->initialized = 1;
    Print(L"[federation] Initialized node: %a caps=0x%x\r\n",
          ctx->self_node_id, ctx->self_caps);
}

/* ── Peer management ─────────────────────────────────────────────────────── */
int oo_fed_add_peer(OoFedCtx *ctx, const CHAR8 *ip, UINT16 port,
                    const CHAR8 *node_id) {
    if (!ctx || !ip) return -1;
    /* Check duplicate */
    for (int i = 0; i < ctx->n_peers; i++) {
        if (_f_strncmp8(ctx->peers[i].ip, ip, OO_FED_IP_LEN) == 0 &&
            ctx->peers[i].port == port)
            return i; /* already known */
    }
    if (ctx->n_peers >= OO_FED_MAX_PEERS) {
        Print(L"[federation] Peer table full\r\n"); return -1;
    }
    int idx = ctx->n_peers++;
    OoFedPeer *p = &ctx->peers[idx];
    _f_memset(p, 0, sizeof(*p));
    p->active = 1;
    _f_strlcpy(p->ip, ip, OO_FED_IP_LEN);
    p->port = port ? port : OO_FED_PORT;
    if (node_id) _f_strlcpy(p->node_id, node_id, OO_FED_NODE_ID_LEN);
    else         _f_strlcpy(p->node_id, (const CHAR8*)"oo-?", OO_FED_NODE_ID_LEN);
    Print(L"[federation] Peer added [%d]: %a:%u id=%a\r\n",
          idx, p->ip, (UINT32)p->port, p->node_id);
    return idx;
}

void oo_fed_remove_peer(OoFedCtx *ctx, int idx) {
    if (!ctx || idx < 0 || idx >= ctx->n_peers) return;
    _f_memset(&ctx->peers[idx], 0, sizeof(OoFedPeer));
    /* Compact */
    for (int i = idx; i < ctx->n_peers - 1; i++)
        ctx->peers[i] = ctx->peers[i+1];
    ctx->n_peers--;
    Print(L"[federation] Peer %d removed\r\n", idx);
}

void oo_fed_print_peers(const OoFedCtx *ctx) {
    if (!ctx) return;
    Print(L"\r\n  [Federation Peers] self=%a caps=0x%x n=%d\r\n",
          ctx->self_node_id, ctx->self_caps, ctx->n_peers);
    if (ctx->n_peers == 0) { Print(L"  No peers\r\n\r\n"); return; }
    for (int i = 0; i < ctx->n_peers; i++) {
        const OoFedPeer *p = &ctx->peers[i];
        Print(L"  [%d] %a:%u id=%a caps=0x%x ping=%ums sent=%u recv=%u\r\n",
              i, p->ip, (UINT32)p->port, p->node_id,
              p->caps, p->ping_ms,
              p->patches_shared, p->patches_received);
    }
    Print(L"  Total: sent=%u recv=%u syncs=%u\r\n\r\n",
          ctx->total_patches_sent, ctx->total_patches_recv, ctx->syncs);
}

/* ── Discovery ───────────────────────────────────────────────────────────── */
/*
 * Real UDP broadcast requires EFI_UDP4_PROTOCOL.
 * For Phase 4E: we broadcast via HTTP to well-known addresses.
 * A future phase will use EFI_UDP4_PROTOCOL for true LAN broadcast.
 *
 * Discovery message: HTTP GET /oo/ping?node=<id>&caps=<hex>
 * Discovery reply:   JSON { "node_id": "oo-XXXX", "caps": 0x... }
 */
EFI_STATUS oo_fed_discover(OoFedCtx *ctx) {
    if (!ctx || !ctx->initialized) return EFI_NOT_READY;
    Print(L"[federation] Discovery: broadcasting to subnet (HTTP fallback)\r\n");
    Print(L"[federation] For LAN discovery: use /fed_add <ip> <port>\r\n");
    /* Auto-discover: try QEMU host (10.0.2.2) and local broadcast (192.168.1.255) */
    static const CHAR8 *candidates[] = {
        (const CHAR8*)"10.0.2.2",
        (const CHAR8*)"192.168.1.1",
        (const CHAR8*)"172.16.0.1",
        NULL
    };
    int found = 0;
    for (int i = 0; candidates[i]; i++) {
        /* Ping via oracle netboot HTTP */
        static CHAR8 url[128];
        _f_build_url(url, sizeof(url), candidates[i], OO_FED_PORT,
                     (const CHAR8*)"/oo/ping");
        CHAR8 resp[256];
        EFI_STATUS st = oo_netboot_http_get(&g_netboot, url,
                                             resp, sizeof(resp)-1);
        if (!EFI_ERROR(st) && _f_strlen(resp) > 0) {
            Print(L"[federation] Found peer at %a: %a\r\n", candidates[i], resp);
            oo_fed_add_peer(ctx, candidates[i], OO_FED_PORT, (const CHAR8*)"oo-discovered");
            found++;
        }
    }
    Print(L"[federation] Discovery complete: %d peers found\r\n", found);
    return EFI_SUCCESS;
}

/* ── Ping ────────────────────────────────────────────────────────────────── */
void oo_fed_ping_all(OoFedCtx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->n_peers; i++) {
        OoFedPeer *p = &ctx->peers[i];
        if (!p->active) continue;
        static CHAR8 url[128];
        _f_build_url(url, sizeof(url), p->ip, p->port, (const CHAR8*)"/oo/ping");
        UINT64 t0=0, t1=0;
        uefi_call_wrapper(BS->GetNextMonotonicCount, 1, &t0);
        CHAR8 resp[64];
        EFI_STATUS st = oo_netboot_http_get(&g_netboot, url, resp, sizeof(resp)-1);
        uefi_call_wrapper(BS->GetNextMonotonicCount, 1, &t1);
        UINT64 delta = (t1 > t0) ? (t1 - t0) : 0;
        p->ping_ms = (UINT32)(delta / 100); /* rough ms from ticks */
        if (EFI_ERROR(st)) {
            Print(L"[federation] Peer[%d] %a unreachable\r\n", i, p->ip);
        } else {
            Print(L"[federation] Peer[%d] %a alive ~%ums\r\n", i, p->ip, p->ping_ms);
        }
    }
}

/* ── Share patch ─────────────────────────────────────────────────────────── */
/*
 * POST patch JSON to /oo/patch_recv on each active peer.
 * The oracle proxy (oo-oracle-proxy.py) can relay if peers aren't running
 * their own HTTP server yet.
 */
EFI_STATUS oo_fed_share_patch(OoFedCtx *ctx, const CHAR8 *patch_json,
                               UINTN patch_len) {
    if (!ctx || !patch_json || patch_len == 0) return EFI_INVALID_PARAMETER;

    /* Wrap in federation envelope */
    static CHAR8 env[8192];
    UINTN ep = 0;
    static const CHAR8 pre1[] = "{\"from\":\"";
    for(UINTN i=0;pre1[i]&&ep<sizeof(env)-1;i++) env[ep++]=pre1[i];
    UINTN nid=_f_strlen(ctx->self_node_id);
    for(UINTN i=0;i<nid&&ep<sizeof(env)-1;i++) env[ep++]=ctx->self_node_id[i];
    static const CHAR8 pre2[] = "\",\"type\":\"patch\",\"payload\":";
    for(UINTN i=0;pre2[i]&&ep<sizeof(env)-1;i++) env[ep++]=pre2[i];
    UINTN pl=patch_len<sizeof(env)-ep-4?patch_len:sizeof(env)-ep-4;
    for(UINTN i=0;i<pl;i++) env[ep++]=patch_json[i];
    env[ep++]='}'; env[ep]=0;

    int sent = 0;
    for (int i = 0; i < ctx->n_peers; i++) {
        OoFedPeer *p = &ctx->peers[i];
        if (!p->active) continue;
        static CHAR8 url[128];
        _f_build_url(url, sizeof(url), p->ip, p->port,
                     (const CHAR8*)"/oo/patch_recv");
        CHAR8 resp[256];
        EFI_STATUS st = oo_netboot_http_post_json(&g_netboot, url,
                                                   env, resp, sizeof(resp)-1);
        if (!EFI_ERROR(st)) {
            p->patches_shared++;
            ctx->total_patches_sent++;
            sent++;
            Print(L"[federation] Patch sent to peer[%d] %a\r\n", i, p->ip);
        } else {
            Print(L"[federation] Failed peer[%d] %a: %r\r\n", i, p->ip, st);
        }
    }
    Print(L"[federation] Patch shared to %d/%d peers\r\n", sent, ctx->n_peers);
    return sent > 0 ? EFI_SUCCESS : EFI_NETWORK_UNREACHABLE;
}

/* ── Pull peer info ──────────────────────────────────────────────────────── */
EFI_STATUS oo_fed_pull_peer_info(OoFedCtx *ctx, int peer_idx) {
    if (!ctx || peer_idx < 0 || peer_idx >= ctx->n_peers) return EFI_INVALID_PARAMETER;
    OoFedPeer *p = &ctx->peers[peer_idx];
    static CHAR8 url[128];
    _f_build_url(url, sizeof(url), p->ip, p->port,
                 (const CHAR8*)"/oo/node_info");
    static CHAR8 resp[1024];
    EFI_STATUS st = oo_netboot_http_get(&g_netboot, url, resp, sizeof(resp)-1);
    if (EFI_ERROR(st)) {
        Print(L"[federation] Cannot pull peer info[%d]: %r\r\n", peer_idx, st);
        return st;
    }
    Print(L"[federation] Peer[%d] info: %a\r\n", peer_idx, resp);
    /* Parse caps= field */
    for (UINTN i = 0; resp[i]; i++) {
        if (resp[i]=='"' && resp[i+1]=='c' && resp[i+2]=='a' &&
            resp[i+3]=='p' && resp[i+4]=='s' && resp[i+5]=='"') {
            UINTN j = i + 6;
            while (resp[j] && resp[j] != ':') j++;
            if (resp[j] == ':') {
                j++;
                while (resp[j] == ' ') j++;
                UINT32 caps = 0;
                while (resp[j] >= '0' && resp[j] <= '9') {
                    caps = caps * 10 + (resp[j++] - '0');
                }
                p->caps = caps;
                Print(L"[federation] Peer caps: 0x%x\r\n", caps);
            }
            break;
        }
    }
    return EFI_SUCCESS;
}

/* ── Sync ────────────────────────────────────────────────────────────────── */
EFI_STATUS oo_fed_sync(OoFedCtx *ctx) {
    if (!ctx || !ctx->initialized) return EFI_NOT_READY;
    Print(L"[federation] Sync started (peers=%d)\r\n", ctx->n_peers);
    for (int i = 0; i < ctx->n_peers; i++) {
        OoFedPeer *p = &ctx->peers[i];
        if (!p->active) continue;
        static CHAR8 url[128];
        _f_build_url(url, sizeof(url), p->ip, p->port,
                     (const CHAR8*)"/oo/patches_pending");
        static CHAR8 resp[4096];
        EFI_STATUS st = oo_netboot_http_get(&g_netboot, url, resp, sizeof(resp)-1);
        if (!EFI_ERROR(st) && _f_strlen(resp) > 2) {
            Print(L"[federation] Patches from peer[%d]: %a\r\n", i, resp);
            /* Relay to self-improve recv handler */
            oo_si_recv_federated((void*)&g_netboot, resp);
            p->patches_received++;
            ctx->total_patches_recv++;
        }
    }
    ctx->syncs++;
    Print(L"[federation] Sync complete (syncs=%u)\r\n", ctx->syncs);
    return EFI_SUCCESS;
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
int oo_fed_repl_cmd(OoFedCtx *ctx, const char *cmd) {
    if (!cmd) return 0;

    /* /fed_status */
    if (_f_cstrcmp(cmd, "/fed_status", 11) == 0) {
        Print(L"\r\n  [Federation Status]\r\n");
        Print(L"  Initialized : %s\r\n", ctx->initialized ? L"YES" : L"NO");
        Print(L"  Self node   : %a\r\n", ctx->self_node_id);
        Print(L"  Self caps   : 0x%x\r\n", ctx->self_caps);
        Print(L"  Peers       : %d/%d\r\n", ctx->n_peers, OO_FED_MAX_PEERS);
        Print(L"  Sent        : %u patches\r\n", ctx->total_patches_sent);
        Print(L"  Received    : %u patches\r\n", ctx->total_patches_recv);
        Print(L"  Syncs       : %u\r\n\r\n", ctx->syncs);
        return 1;
    }
    /* /fed_peers */
    if (_f_cstrcmp(cmd, "/fed_peers", 10) == 0) {
        oo_fed_print_peers(ctx); return 1;
    }
    /* /fed_add <ip> [port] [node_id] */
    if (_f_cstrcmp(cmd, "/fed_add ", 9) == 0) {
        const char *p = cmd + 9;
        while (*p == ' ') p++;
        static CHAR8 ip[OO_FED_IP_LEN];
        UINTN ii = 0;
        while (*p && *p != ' ' && ii < OO_FED_IP_LEN-1) ip[ii++]=(CHAR8)*p++;
        ip[ii] = 0;
        while (*p == ' ') p++;
        UINT16 port = OO_FED_PORT;
        if (*p >= '0' && *p <= '9') {
            port = 0;
            while (*p >= '0' && *p <= '9') port = (UINT16)(port * 10 + (*p++ - '0'));
        }
        while (*p == ' ') p++;
        oo_fed_add_peer(ctx, ip, port, (const CHAR8*)p);
        return 1;
    }
    /* /fed_remove <idx> */
    if (_f_cstrcmp(cmd, "/fed_remove ", 12) == 0) {
        int idx = 0;
        const char *p = cmd + 12;
        while (*p >= '0' && *p <= '9') idx = idx*10 + (*p++ - '0');
        oo_fed_remove_peer(ctx, idx); return 1;
    }
    /* /fed_discover */
    if (_f_cstrcmp(cmd, "/fed_discover", 13) == 0) {
        oo_fed_discover(ctx); return 1;
    }
    /* /fed_ping */
    if (_f_cstrcmp(cmd, "/fed_ping", 9) == 0) {
        oo_fed_ping_all(ctx); return 1;
    }
    /* /fed_sync */
    if (_f_cstrcmp(cmd, "/fed_sync", 9) == 0) {
        oo_fed_sync(ctx); return 1;
    }
    /* /fed_share <patch_json> */
    if (_f_cstrcmp(cmd, "/fed_share ", 11) == 0) {
        const CHAR8 *json = (const CHAR8*)(cmd + 11);
        oo_fed_share_patch(ctx, json, _f_strlen(json)); return 1;
    }
    /* /fed_info <peer_idx> */
    if (_f_cstrcmp(cmd, "/fed_info ", 10) == 0) {
        int idx = 0;
        const char *p = cmd + 10;
        while (*p >= '0' && *p <= '9') idx = idx*10 + (*p++ - '0');
        oo_fed_pull_peer_info(ctx, idx); return 1;
    }
    /* /fed_join <ip> — convenience: add + pull info */
    if (_f_cstrcmp(cmd, "/fed_join ", 10) == 0) {
        const char *p = cmd + 10;
        while (*p == ' ') p++;
        static CHAR8 ip[OO_FED_IP_LEN];
        UINTN ii = 0;
        while (*p && *p != ' ' && ii < OO_FED_IP_LEN-1) ip[ii++]=(CHAR8)*p++;
        ip[ii] = 0;
        int idx = oo_fed_add_peer(ctx, ip, OO_FED_PORT, (const CHAR8*)"oo-join");
        if (idx >= 0) oo_fed_pull_peer_info(ctx, idx);
        return 1;
    }
    /* Phase 8A: /fed_udp_discover — LAN UDP broadcast */
    if (_f_cstrcmp(cmd, "/fed_udp_discover", 17) == 0) {
        oo_fed_udp_discover(ctx); return 1;
    }
    /* Phase 8C: /fed_heartbeat — send heartbeat to all peers + prune stale */
    if (_f_cstrcmp(cmd, "/fed_heartbeat", 14) == 0) {
        oo_fed_heartbeat(ctx); return 1;
    }
    /* Phase 8D: /fed_delta_push <patch_json> — compressed chunked delta push */
    if (_f_cstrcmp(cmd, "/fed_delta_push ", 16) == 0) {
        const CHAR8 *json = (const CHAR8*)(cmd + 16);
        oo_fed_delta_push(ctx, json, _f_strlen(json)); return 1;
    }
    return 0;
}

/* ── Phase 8A: UDP Broadcast Peer Discovery ─────────────────────────────────
 * Sends "OO-DISCOVER" broadcast on UDP port 8181 using oo_net_core.
 * Any OO node listening replies with its JSON capabilities.
 * Port 8181 chosen to avoid collision with DHCP (67/68) and swarm (42420).
 */
#define OO_FED_UDP_DISC_PORT 8181

EFI_STATUS oo_fed_udp_discover(OoFedCtx *ctx) {
    if (!ctx || !ctx->initialized) return EFI_NOT_READY;
    Print(L"[fed-8A] UDP broadcast discovery on port %d...\r\n", OO_FED_UDP_DISC_PORT);

    /* Build discovery message: "OO-DISCOVER|<node_id>|<caps_hex>" */
    static CHAR8 msg[128];
    UINTN mp = 0;
    const CHAR8 *pfx = (const CHAR8*)"OO-DISCOVER|";
    for (int k = 0; pfx[k] && mp < 120; k++) msg[mp++] = pfx[k];
    for (int k = 0; ctx->self_node_id[k] && mp < 120; k++) msg[mp++] = ctx->self_node_id[k];
    msg[mp++] = '|';
    /* caps as 8-char hex */
    UINT32 caps = ctx->self_caps;
    for (int shift = 28; shift >= 0; shift -= 4) {
        UINT8 nib = (caps >> shift) & 0xF;
        msg[mp++] = (CHAR8)(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
    msg[mp] = 0;

    /* Send broadcast */
    int tx = oo_net_udp_send(OO_IP_BCAST, OO_FED_UDP_DISC_PORT,
                              OO_FED_UDP_DISC_PORT,
                              msg, (UINT16)mp);
    if (!tx) {
        Print(L"[fed-8A] UDP send failed (network offline)\r\n");
        return EFI_NETWORK_UNREACHABLE;
    }
    Print(L"[fed-8A] Broadcast sent: %a\r\n", msg);

    /* Poll for replies: up to 16 packets, 50ms window */
    int found = 0;
    for (int attempt = 0; attempt < 16; attempt++) {
        static CHAR8 rbuf[256];
        UINT32 src_ip = 0; UINT16 src_port = 0, rlen = 0;
        if (!oo_net_udp_recv_poll(OO_FED_UDP_DISC_PORT, rbuf, (UINT16)(sizeof(rbuf)-1),
                                   &src_ip, &src_port, &rlen))
            break; /* no more packets */
        rbuf[rlen] = 0;
        /* Parse reply: "OO-REPLY|<node_id>|<caps_hex>" */
        if (rbuf[0]=='O' && rbuf[1]=='O' && rbuf[2]=='-' &&
            rbuf[3]=='R' && rbuf[4]=='E' && rbuf[5]=='P') {
            /* Extract node_id (between first and second '|') */
            static CHAR8 peer_id[OO_FED_NODE_ID_LEN];
            UINTN pi = 0;
            UINTN ri = 8; /* skip "OO-REPLY|" */
            while (rbuf[ri] && rbuf[ri] != '|' && pi < OO_FED_NODE_ID_LEN-1)
                peer_id[pi++] = rbuf[ri++];
            peer_id[pi] = 0;
            /* Build IP string from src_ip */
            static CHAR8 peer_ip[16];
            UINTN iip = 0;
            for (int b = 0; b < 4; b++) {
                UINT8 oct = (UINT8)((src_ip >> (b*8)) & 0xFF);
                CHAR8 tmp[4]; _f_itoa(oct, tmp, 4);
                for (int k = 0; tmp[k] && iip < 14; k++) peer_ip[iip++] = tmp[k];
                if (b < 3) peer_ip[iip++] = '.';
            }
            peer_ip[iip] = 0;
            oo_fed_add_peer(ctx, peer_ip, OO_FED_PORT, peer_id);
            found++;
            Print(L"[fed-8A] Peer discovered: %a @ %a\r\n", peer_id, peer_ip);
        }
    }
    Print(L"[fed-8A] Discovery done: %d new peer(s)\r\n", found);
    return EFI_SUCCESS;
}

/* Also: respond to incoming OO-DISCOVER packets (call from REPL loop poll) */
void oo_fed_udp_listen_tick(OoFedCtx *ctx) {
    if (!ctx || !ctx->initialized) return;
    static CHAR8 rbuf[256];
    UINT32 src_ip = 0; UINT16 src_port = 0, rlen = 0;
    if (!oo_net_udp_recv_poll(OO_FED_UDP_DISC_PORT, rbuf, (UINT16)(sizeof(rbuf)-1),
                               &src_ip, &src_port, &rlen))
        return;
    rbuf[rlen] = 0;
    /* Reply to OO-DISCOVER messages */
    if (rbuf[0]=='O' && rbuf[1]=='O' && rbuf[2]=='-' &&
        rbuf[3]=='D' && rbuf[4]=='I' && rbuf[5]=='S') {
        static CHAR8 reply[128];
        UINTN rp = 0;
        const CHAR8 *rtag = (const CHAR8*)"OO-REPLY|";
        for (int k = 0; rtag[k] && rp < 120; k++) reply[rp++] = rtag[k];
        for (int k = 0; ctx->self_node_id[k] && rp < 120; k++) reply[rp++] = ctx->self_node_id[k];
        reply[rp++] = '|';
        UINT32 caps = ctx->self_caps;
        for (int shift = 28; shift >= 0; shift -= 4) {
            UINT8 nib = (caps >> shift) & 0xF;
            reply[rp++] = (CHAR8)(nib < 10 ? '0' + nib : 'A' + nib - 10);
        }
        reply[rp] = 0;
        oo_net_udp_send(src_ip, src_port, OO_FED_UDP_DISC_PORT,
                        reply, (UINT16)rp);
    }
}

/* ── Phase 8B: HTTPS bearer token injection ──────────────────────────────────
 * Sets oracle API key for direct HTTPS oracle calls.
 * The actual TLS handshake is done by EFI_HTTP_PROTOCOL (SNP firmware handles
 * TLS for HTTPS URLs). We inject the Authorization header into g_netboot.
 */
void oo_fed_set_oracle_token(const CHAR8 *bearer_token) {
    if (!bearer_token) return;
    /* Store in g_netboot oracle config for next HTTP POST */
    _f_strlcpy(g_netboot.oracle_api_key, bearer_token, 128);
    Print(L"[fed-8B] Oracle bearer token set (%u chars)\r\n",
          (UINT32)_f_strlen(bearer_token));
}

/* ── Phase 8C: Federation Heartbeat + Stale Peer Removal ─────────────────────
 * Sends POST /oo/heartbeat to each peer with: node_id, caps, model_hash, fitness.
 * Peers not seen for OO_FED_HEARTBEAT_TIMEOUT_TICKS are removed.
 * Call from REPL loop every N turns (e.g. every 60 interactions).
 */
#define OO_FED_HEARTBEAT_TIMEOUT_MS  300000U  /* 5 minutes */

void oo_fed_heartbeat(OoFedCtx *ctx) {
    if (!ctx || !ctx->initialized) return;

    /* Build heartbeat JSON */
    static CHAR8 hb[512];
    UINTN hp = 0;
    const CHAR8 *h1 = (const CHAR8*)"{\"node_id\":\"";
    for (int k = 0; h1[k] && hp < 508; k++) hb[hp++] = h1[k];
    for (int k = 0; ctx->self_node_id[k] && hp < 508; k++) hb[hp++] = ctx->self_node_id[k];
    const CHAR8 *h2 = (const CHAR8*)"\",\"caps\":";
    for (int k = 0; h2[k] && hp < 508; k++) hb[hp++] = h2[k];
    CHAR8 capstr[12]; _f_itoa(ctx->self_caps, capstr, 12);
    for (int k = 0; capstr[k] && hp < 508; k++) hb[hp++] = capstr[k];
    /* Add uptime in ticks */
    const CHAR8 *h3 = (const CHAR8*)"}";
    for (int k = 0; h3[k] && hp < 508; k++) hb[hp++] = h3[k];
    hb[hp] = 0;

    UINT64 now = 0;
    uefi_call_wrapper(BS->GetNextMonotonicCount, 1, &now);

    int alive = 0, pruned = 0;
    for (int i = ctx->n_peers - 1; i >= 0; i--) {
        OoFedPeer *p = &ctx->peers[i];
        if (!p->active) continue;

        /* Check timeout first */
        if (p->last_seen_tick > 0) {
            UINT64 elapsed_ms = (now > p->last_seen_tick)
                ? (now - p->last_seen_tick) / 100  /* rough ms */
                : 0;
            if (elapsed_ms > OO_FED_HEARTBEAT_TIMEOUT_MS) {
                Print(L"[fed-8C] Peer[%d] %a timed out (%lums) — removing\r\n",
                      i, p->ip, (unsigned long long)elapsed_ms);
                oo_fed_remove_peer(ctx, i);
                pruned++;
                continue;
            }
        }

        /* Send heartbeat */
        static CHAR8 url[128];
        _f_build_url(url, sizeof(url), p->ip, p->port,
                     (const CHAR8*)"/oo/heartbeat");
        static CHAR8 resp[128];
        EFI_STATUS st = oo_netboot_http_post_json(&g_netboot, url,
                                                   hb, resp, sizeof(resp)-1);
        if (!EFI_ERROR(st)) {
            p->last_seen_tick = now;
            alive++;
            Print(L"[fed-8C] Peer[%d] %a alive\r\n", i, p->ip);
        } else {
            Print(L"[fed-8C] Peer[%d] %a unreachable\r\n", i, p->ip);
        }
    }
    ctx->heartbeat_count++;
    Print(L"[fed-8C] Heartbeat done: %d alive, %d pruned (total=%u)\r\n",
          alive, pruned, ctx->heartbeat_count);
}

/* ── Phase 8D: Delta Compression + Chunked Transfer ─────────────────────────
 * Splits large patch JSON into 4096-byte chunks with CRC32 per chunk.
 * Chunk envelope: {"seq":<n>,"total":<t>,"crc32":<hex>,"data":"<b64-like>"}
 * Each chunk is POSTed to /oo/patch_chunk on each peer.
 * Peers reassemble and validate before applying.
 */
#define OO_FED_CHUNK_SIZE  4096

EFI_STATUS oo_fed_delta_push(OoFedCtx *ctx, const CHAR8 *patch_json, UINTN patch_len) {
    if (!ctx || !patch_json || patch_len == 0) return EFI_INVALID_PARAMETER;

    UINT32 total_chunks = (UINT32)((patch_len + OO_FED_CHUNK_SIZE - 1) / OO_FED_CHUNK_SIZE);
    Print(L"[fed-8D] Delta push: %u bytes, %u chunk(s)\r\n",
          (UINT32)patch_len, total_chunks);

    int peers_ok = 0;
    for (int pi = 0; pi < ctx->n_peers; pi++) {
        OoFedPeer *p = &ctx->peers[pi];
        if (!p->active) continue;

        static CHAR8 url[128];
        _f_build_url(url, sizeof(url), p->ip, p->port,
                     (const CHAR8*)"/oo/patch_chunk");

        int peer_ok = 1;
        for (UINT32 seq = 0; seq < total_chunks; seq++) {
            UINTN off   = (UINTN)seq * OO_FED_CHUNK_SIZE;
            UINTN clen  = patch_len - off;
            if (clen > OO_FED_CHUNK_SIZE) clen = OO_FED_CHUNK_SIZE;

            /* CRC32 of this chunk */
            UINT32 crc = oo_net_crc32(patch_json + off, (int)clen);

            /* Build chunk JSON envelope */
            static CHAR8 env[OO_FED_CHUNK_SIZE + 256];
            UINTN ep = 0;
            const CHAR8 *e1 = (const CHAR8*)"{\"from\":\"";
            for (int k = 0; e1[k] && ep < sizeof(env)-8; k++) env[ep++] = e1[k];
            for (int k = 0; ctx->self_node_id[k] && ep < sizeof(env)-8; k++) env[ep++] = ctx->self_node_id[k];
            /* seq / total / crc fields */
            CHAR8 num[12];
            const CHAR8 *e2 = (const CHAR8*)"\",\"seq\":";
            for (int k = 0; e2[k] && ep < sizeof(env)-8; k++) env[ep++] = e2[k];
            _f_itoa(seq, num, 12); for (int k = 0; num[k] && ep < sizeof(env)-8; k++) env[ep++] = num[k];
            const CHAR8 *e3 = (const CHAR8*)",\"total\":";
            for (int k = 0; e3[k] && ep < sizeof(env)-8; k++) env[ep++] = e3[k];
            _f_itoa(total_chunks, num, 12); for (int k = 0; num[k] && ep < sizeof(env)-8; k++) env[ep++] = num[k];
            const CHAR8 *e4 = (const CHAR8*)",\"crc32\":";
            for (int k = 0; e4[k] && ep < sizeof(env)-8; k++) env[ep++] = e4[k];
            /* CRC as decimal */
            _f_itoa(crc, num, 12); for (int k = 0; num[k] && ep < sizeof(env)-8; k++) env[ep++] = num[k];
            const CHAR8 *e5 = (const CHAR8*)",\"data\":\"";
            for (int k = 0; e5[k] && ep < sizeof(env)-8; k++) env[ep++] = e5[k];
            /* Inline chunk data (JSON-escaped) */
            UINTN written = _f_json_escape(patch_json + off, env + ep, sizeof(env) - ep - 4);
            ep += written;
            env[ep++] = '"'; env[ep++] = '}'; env[ep] = 0;

            static CHAR8 resp[128];
            EFI_STATUS st = oo_netboot_http_post_json(&g_netboot, url,
                                                       env, resp, sizeof(resp)-1);
            if (EFI_ERROR(st)) {
                Print(L"[fed-8D] Chunk %u/%u failed for peer[%d]: %r\r\n",
                      seq+1, total_chunks, pi, st);
                peer_ok = 0; break;
            }
            Print(L"[fed-8D] Chunk %u/%u -> peer[%d] crc32=0x%08x OK\r\n",
                  seq+1, total_chunks, pi, crc);
        }
        if (peer_ok) {
            p->patches_shared++;
            ctx->total_patches_sent++;
            peers_ok++;
        }
    }
    Print(L"[fed-8D] Delta push done: %d/%d peers OK\r\n", peers_ok, ctx->n_peers);
    return peers_ok > 0 ? EFI_SUCCESS : EFI_NETWORK_UNREACHABLE;
}
