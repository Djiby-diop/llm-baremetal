/*
 * oo_swarm_node.h — OO Swarm Node Identity + State Machine
 *
 * Phase O: Multi-instance swarm coordination.
 * Each bare-metal OO instance is a SwarmNode with a unique ID.
 * Nodes communicate via soma_swarm_net (shared physical memory or FAT file).
 *
 * Freestanding C11. No libc. No OS.
 */
#ifndef OO_SWARM_NODE_H
#define OO_SWARM_NODE_H

#include "soma_swarm_net.h"   /* SomaSwarmNetCtx, SomaSwarmNetResult */

/* ── State machine ──────────────────────────────────────────────── */
typedef enum {
    OO_NODE_INIT     = 0,   /* just booted, not yet synced with peers */
    OO_NODE_ACTIVE   = 1,   /* healthy, synced, participating in consensus */
    OO_NODE_SYNCING  = 2,   /* catching up after isolation or restart */
    OO_NODE_ISOLATED = 3,   /* no peer heard for > OO_NODE_ISOLATION_MS */
    OO_NODE_DEGRADED = 4,   /* local D+ verdict is non-ALLOW */
} OoNodeState;

#define OO_NODE_PEER_MAX         8
#define OO_NODE_ISOLATION_MS  8000u   /* 8 s without peer → ISOLATED */
#define OO_NODE_SYNC_INTERVAL_MS 500u /* publish DNA every 500 ms */

/* Per-peer bookkeeping inside the node */
typedef struct {
    unsigned int  peer_id;
    unsigned int  dna_hash;
    unsigned int  last_seen_ms;
    int           degraded;    /* 1 if peer reported non-ALLOW */
} OoSwarmPeer;

/* Main swarm node struct */
typedef struct {
    OoNodeState       state;
    unsigned int      node_id;          /* 0..SWARM_NET_PEERS-1  */
    unsigned int      dna_hash;         /* current local DNA hash */
    unsigned int      boot_ts_ms;       /* timestamp at init      */
    unsigned int      last_sync_ms;     /* last successful publish */
    unsigned int      last_hello_ms;    /* last hello broadcast    */
    int               n_active_peers;
    OoSwarmPeer       peers[OO_NODE_PEER_MAX];
    SomaSwarmNetCtx  *net;             /* shared-mem swarm net     */

    /* stats */
    unsigned int      syncs_sent;
    unsigned int      hellos_sent;
    unsigned int      isolations;
} OoSwarmNode;

/* ── API ─────────────────────────────────────────────────────────── */

/*
 * oo_swarm_node_init() — initialize swarm node.
 *   node_id   : 0..7
 *   dna_hash  : current DNA hash of this instance
 *   net       : already-initialized SomaSwarmNetCtx (may be NULL if swarm disabled)
 *   now_ms    : current timestamp in ms (from RDTSC / boot counter)
 */
void oo_swarm_node_init(OoSwarmNode *n, unsigned int node_id,
                        unsigned int dna_hash,
                        SomaSwarmNetCtx *net, unsigned int now_ms);

/*
 * oo_swarm_node_tick() — call every N ms from main inference loop.
 *   now_ms      : current timestamp
 *   local_dna   : current DNA hash (may change between ticks)
 *   degraded    : 1 if local D+ verdict is non-ALLOW
 *
 * Returns 1 if a sync was published this tick, 0 otherwise.
 */
int oo_swarm_node_tick(OoSwarmNode *n, unsigned int now_ms,
                       unsigned int local_dna, int degraded);

/*
 * oo_swarm_node_broadcast_dna() — immediately publish DNA + state to peers.
 * Called on DNA evolution, not just on periodic tick.
 */
void oo_swarm_node_broadcast_dna(OoSwarmNode *n, unsigned int now_ms);

/*
 * oo_swarm_node_peer_seen() — update a peer's last-seen timestamp.
 * Called when a heartbeat / packet is received from peer_id.
 */
void oo_swarm_node_peer_seen(OoSwarmNode *n, unsigned int peer_id,
                             unsigned int dna_hash, int degraded,
                             unsigned int now_ms);

/*
 * oo_swarm_node_quorum_degraded() — returns 1 if >= 2/3 of active
 * peers are in degraded state (triggers global emergency escalation).
 */
int oo_swarm_node_quorum_degraded(const OoSwarmNode *n);

/* State name for logging / UART output */
const char *oo_swarm_node_state_name(OoNodeState s);

/* Print node status to UART (uses oo_uart_print_str equivalent) */
void oo_swarm_node_print_status(const OoSwarmNode *n);

#endif /* OO_SWARM_NODE_H */
