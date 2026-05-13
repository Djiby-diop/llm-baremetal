/* oo_federation.h — OO Federation Protocol  Phase 4E
 * =====================================================
 * Peer-to-peer federation for OO nodes:
 *   - Node discovery (UDP broadcast port 8181)
 *   - Peer registry (8 slots)
 *   - Model capability exchange
 *   - Patch delta sharing (integrates with self-improve Phase 3)
 *   - Knowledge sync (context windows from remote nodes)
 *
 * Transport: EFI_UDP4_PROTOCOL (discovery) + EFI_HTTP_PROTOCOL (data)
 * Format: JSON over HTTP, same oracle proxy format
 *
 * Endpoints expected on peer OO nodes:
 *   POST /oo/patch_recv    — receive patch from federation
 *   GET  /oo/node_info     — query peer capabilities
 *   POST /oo/model_delta   — share model delta
 *   GET  /oo/ping          — liveness check
 *
 * Freestanding C11. No libc. Static state.
 */
#pragma once
#include <efi.h>
#include <efilib.h>

#define OO_FED_MAX_PEERS     8
#define OO_FED_NODE_ID_LEN   16
#define OO_FED_IP_LEN        16
#define OO_FED_PORT          8181

/* Peer capability flags */
#define OO_FED_CAP_INFERENCE (1 << 0)   /* Can run LLM inference */
#define OO_FED_CAP_NETBOOT   (1 << 1)   /* Has network boot */
#define OO_FED_CAP_SELFIMPROVE (1<<2)   /* Has self-improve engine */
#define OO_FED_CAP_VOICE     (1 << 3)   /* Has voice pipeline */
#define OO_FED_CAP_DIOP      (1 << 4)   /* Has DIOP model loaded */

typedef struct {
    int    active;
    CHAR8  node_id[OO_FED_NODE_ID_LEN];
    CHAR8  ip[OO_FED_IP_LEN];
    UINT16 port;
    UINT32 caps;
    UINT32 ping_ms;
    UINT32 patches_shared;
    UINT32 patches_received;
    UINT64 last_seen_tick;
} OoFedPeer;

typedef struct {
    int        initialized;
    CHAR8      self_node_id[OO_FED_NODE_ID_LEN];
    OoFedPeer  peers[OO_FED_MAX_PEERS];
    int        n_peers;
    UINT32     self_caps;
    UINT32     total_patches_sent;
    UINT32     total_patches_recv;
    UINT32     syncs;
    UINT32     heartbeat_count;   /* Phase 8C */
} OoFedCtx;

/* Lifecycle */
void oo_fed_init(OoFedCtx *ctx, const CHAR8 *self_node_id);

/* Peer management */
int  oo_fed_add_peer(OoFedCtx *ctx, const CHAR8 *ip, UINT16 port,
                     const CHAR8 *node_id);
void oo_fed_remove_peer(OoFedCtx *ctx, int idx);
void oo_fed_print_peers(const OoFedCtx *ctx);

/* Discovery — broadcast "OO-DISCOVER" on UDP 8181 and collect replies */
EFI_STATUS oo_fed_discover(OoFedCtx *ctx);

/* Liveness — ping all peers, update ping_ms, remove dead */
void oo_fed_ping_all(OoFedCtx *ctx);

/* Share patch with all capable peers */
EFI_STATUS oo_fed_share_patch(OoFedCtx *ctx, const CHAR8 *patch_json,
                               UINTN patch_len);

/* Pull node_info from a peer */
EFI_STATUS oo_fed_pull_peer_info(OoFedCtx *ctx, int peer_idx);

/* Sync: pull pending patches from all peers */
EFI_STATUS oo_fed_sync(OoFedCtx *ctx);

/* REPL */
int oo_fed_repl_cmd(OoFedCtx *ctx, const char *cmd);

/* Phase 8A: UDP broadcast discovery + reply listener */
EFI_STATUS oo_fed_udp_discover(OoFedCtx *ctx);
void       oo_fed_udp_listen_tick(OoFedCtx *ctx);  /* call from REPL loop */

/* Phase 8B: Set oracle bearer token for HTTPS calls */
void oo_fed_set_oracle_token(const CHAR8 *bearer_token);

/* Phase 8C: Heartbeat + stale peer pruning */
void oo_fed_heartbeat(OoFedCtx *ctx);

/* Phase 8D: Delta compression + chunked push */
EFI_STATUS oo_fed_delta_push(OoFedCtx *ctx, const CHAR8 *patch_json,
                              UINTN patch_len);

/* Global singleton */
extern OoFedCtx g_federation;
