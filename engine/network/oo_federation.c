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
    return 0;
}
