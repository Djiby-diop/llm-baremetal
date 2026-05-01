/*
 * oo_swarm_node.c — OO Swarm Node Implementation
 * Phase O: Multi-instance swarm coordination.
 * Freestanding C11. No libc.
 */
#include "oo_swarm_node.h"

/* ── freestanding helpers ─────────────────────────────────────── */
static void _sn_memset(void *dst, int v, unsigned int n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)v;
}

/* Simple UART stub — replaced by actual oo_uart_print_str at link time */
extern void oo_uart_print_str(const char *s);
extern void oo_uart_print_hex32(unsigned int v);

static void _sn_print(const char *s) { oo_uart_print_str(s); }
static void _sn_hex(unsigned int v)  { oo_uart_print_hex32(v); }

/* ── state machine helpers ───────────────────────────────────── */
const char *oo_swarm_node_state_name(OoNodeState s) {
    switch (s) {
    case OO_NODE_INIT:     return "INIT";
    case OO_NODE_ACTIVE:   return "ACTIVE";
    case OO_NODE_SYNCING:  return "SYNCING";
    case OO_NODE_ISOLATED: return "ISOLATED";
    case OO_NODE_DEGRADED: return "DEGRADED";
    default:               return "UNKNOWN";
    }
}

/* Count peers seen within isolation window */
static int _count_active_peers(OoSwarmNode *n, unsigned int now_ms) {
    int cnt = 0;
    for (int i = 0; i < OO_NODE_PEER_MAX; i++) {
        if (n->peers[i].peer_id == 0xFFu) continue;
        unsigned int age = now_ms - n->peers[i].last_seen_ms;
        if (age < OO_NODE_ISOLATION_MS) cnt++;
    }
    return cnt;
}

/* ── public API ──────────────────────────────────────────────── */

void oo_swarm_node_init(OoSwarmNode *n, unsigned int node_id,
                        unsigned int dna_hash,
                        SomaSwarmNetCtx *net, unsigned int now_ms) {
    _sn_memset(n, 0, sizeof(*n));
    n->node_id      = node_id;
    n->dna_hash     = dna_hash;
    n->boot_ts_ms   = now_ms;
    n->net          = net;
    n->state        = OO_NODE_INIT;

    /* Mark all peer slots empty */
    for (int i = 0; i < OO_NODE_PEER_MAX; i++)
        n->peers[i].peer_id = 0xFFu;  /* sentinel = no peer */

    _sn_print("[swarm] node_init id=");
    _sn_hex(node_id);
    _sn_print(" dna=");
    _sn_hex(dna_hash);
    _sn_print("\n");
}

void oo_swarm_node_broadcast_dna(OoSwarmNode *n, unsigned int now_ms) {
    if (!n->net || !n->net->enabled) return;

    /* Build a minimal SomaDNA-like struct for soma_swarm_net_publish.
       We only have dna_hash here; probs/tokens are empty for DNA-only broadcast. */
    int    empty_tokens[4] = {0, 0, 0, 0};
    float  empty_probs[4]  = {0.0f, 0.0f, 0.0f, 0.0f};

    soma_swarm_net_publish(n->net,
                           empty_tokens, empty_probs, 0,
                           1.0f,         /* confidence = full (DNA only) */
                           (const SomaDNA *)0,
                           now_ms);

    n->last_sync_ms = now_ms;
    n->syncs_sent++;
}

void oo_swarm_node_peer_seen(OoSwarmNode *n, unsigned int peer_id,
                             unsigned int dna_hash, int degraded,
                             unsigned int now_ms) {
    if (peer_id >= OO_NODE_PEER_MAX) return;

    OoSwarmPeer *p = &n->peers[peer_id];
    p->peer_id       = peer_id;
    p->dna_hash      = dna_hash;
    p->last_seen_ms  = now_ms;
    p->degraded      = degraded;
}

int oo_swarm_node_quorum_degraded(const OoSwarmNode *n) {
    int total = 0, deg = 0;
    for (int i = 0; i < OO_NODE_PEER_MAX; i++) {
        if (n->peers[i].peer_id == 0xFFu) continue;
        total++;
        if (n->peers[i].degraded) deg++;
    }
    /* 2/3 quorum: deg * 3 >= total * 2 */
    if (total == 0) return 0;
    return (deg * 3 >= total * 2);
}

int oo_swarm_node_tick(OoSwarmNode *n, unsigned int now_ms,
                       unsigned int local_dna, int degraded) {
    n->dna_hash = local_dna;

    /* Refresh active peer count */
    n->n_active_peers = _count_active_peers(n, now_ms);

    /* ── State transitions ── */
    OoNodeState prev = n->state;

    if (n->state == OO_NODE_INIT) {
        /* First tick: broadcast hello + transition to SYNCING */
        if (n->net && n->net->enabled) {
            oo_swarm_node_broadcast_dna(n, now_ms);
            n->state = OO_NODE_SYNCING;
        } else {
            /* No net — go straight to ACTIVE solo */
            n->state = OO_NODE_ACTIVE;
        }
    } else if (n->n_active_peers == 0 &&
               (now_ms - n->boot_ts_ms) > OO_NODE_ISOLATION_MS) {
        n->state = OO_NODE_ISOLATED;
        if (prev != OO_NODE_ISOLATED) n->isolations++;
    } else if (degraded) {
        n->state = OO_NODE_DEGRADED;
    } else if (n->n_active_peers > 0) {
        n->state = OO_NODE_ACTIVE;
    }

    /* ── Periodic DNA publish ── */
    int synced = 0;
    if (n->net && n->net->enabled &&
        (now_ms - n->last_sync_ms) >= OO_NODE_SYNC_INTERVAL_MS) {
        oo_swarm_node_broadcast_dna(n, now_ms);
        synced = 1;
    }

    /* Log state changes */
    if (n->state != prev) {
        _sn_print("[swarm] node=");
        _sn_hex(n->node_id);
        _sn_print(" ");
        _sn_print(oo_swarm_node_state_name(prev));
        _sn_print("→");
        _sn_print(oo_swarm_node_state_name(n->state));
        _sn_print("\n");
    }

    return synced;
}

void oo_swarm_node_print_status(const OoSwarmNode *n) {
    _sn_print("[swarm_status] node=");
    _sn_hex(n->node_id);
    _sn_print(" state=");
    _sn_print(oo_swarm_node_state_name(n->state));
    _sn_print(" peers=");
    _sn_hex((unsigned int)n->n_active_peers);
    _sn_print(" dna=");
    _sn_hex(n->dna_hash);
    _sn_print(" syncs=");
    _sn_hex(n->syncs_sent);
    _sn_print("\n");
}
