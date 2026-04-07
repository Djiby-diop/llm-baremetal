/* soma_swarm_net.h — Phase Y: Distributed Swarm Consensus
 *
 * Extends the local 4-agent swarm (Phase E) with multi-instance coordination.
 * On bare-metal, "network" = shared FAT region at a fixed offset on the USB image,
 * or a fixed physical memory address when running multiple OO instances.
 *
 * Architecture:
 *   - Each OO instance writes its vote record to a 512-byte "peer slot"
 *   - Up to SWARM_NET_PEERS peers (8 by default)
 *   - Peer slots are at: FAT_PEER_BASE + peer_id * SWARM_NET_SLOT_SIZE
 *   - Consensus: weighted average of all valid peer votes
 *   - Validity: peer slot has valid magic + timestamp within MAX_PEER_AGE_MS
 *
 * On single-machine (no real peers): only local slot is written,
 * giving identical behavior to Phase E swarm. Phase Y is additive.
 */

#ifndef SOMA_SWARM_NET_H
#define SOMA_SWARM_NET_H

#include "soma_dna.h"
#include "oosi_v3_infer.h"

#define SWARM_NET_MAGIC       0x534E4554U  /* "SNET" */
#define SWARM_NET_VERSION     1
#define SWARM_NET_PEERS       8
#define SWARM_NET_SLOT_SIZE   512          /* bytes per peer slot */
#define SWARM_NET_TOTAL_SIZE  (SWARM_NET_PEERS * SWARM_NET_SLOT_SIZE)
#define SWARM_NET_MAX_TOKEN   4            /* max tokens per peer vote */
#define SWARM_NET_MAX_AGE_MS  5000         /* stale peer threshold (ms) */

/* Peer domain specializations */
#define SWARM_DOMAIN_SYSTEM   0
#define SWARM_DOMAIN_CODE     1
#define SWARM_DOMAIN_REASON   2
#define SWARM_DOMAIN_CREATIVE 3
#define SWARM_DOMAIN_SAFETY   4
#define SWARM_DOMAIN_META     5
#define SWARM_DOMAIN_GENERAL  6

/* ── Peer vote record (fits in 512 bytes) ──────────────────────────────────── */
typedef struct __attribute__((packed)) {
    unsigned int   magic;                      /* SWARM_NET_MAGIC */
    unsigned int   version;
    unsigned int   peer_id;                    /* 0..SWARM_NET_PEERS-1 */
    unsigned int   domain_mask;                /* which domains this peer handles */
    unsigned int   generation;                 /* DNA generation */
    unsigned int   dna_hash;
    unsigned int   timestamp_lo;              /* ms since boot (lo 32 bits) */
    unsigned int   timestamp_hi;
    int            voted_tokens[SWARM_NET_MAX_TOKEN];
    float          voted_probs[SWARM_NET_MAX_TOKEN];
    int            n_votes;                    /* valid entries in voted_tokens[] */
    float          confidence;                 /* 0..1 */
    float          temperature;               /* peer's current temperature */
    int            total_turns;               /* turns since last reset */
    int            fitness;                   /* scaled fitness * 10000 */
    unsigned char  _pad[512 - 4*12 - 4*6 - 1*4 - SWARM_NET_MAX_TOKEN*(4+4)]; /* pad to 512 */
    unsigned int   crc32;                      /* simple checksum (last field) */
} SomaSwarmNetSlot;

/* ── Network swarm context ──────────────────────────────────────────────────── */
typedef struct {
    int             enabled;
    int             initialized;
    int             my_peer_id;            /* this instance's peer slot index */
    int             n_active_peers;        /* peers with valid recent votes */
    unsigned long long base_addr;          /* physical address of peer slot region */
    int             use_fixed_addr;        /* 1=fixed phys addr, 0=FAT file */

    /* Per-peer state cache */
    SomaSwarmNetSlot peers[SWARM_NET_PEERS];
    int              peer_valid[SWARM_NET_PEERS];
    unsigned int     last_sync_tick;       /* SMB tick of last peer sync */

    /* Stats */
    unsigned int  total_net_votes;
    unsigned int  total_consensus_events;
    unsigned int  total_stale_peers;
    unsigned int  total_disagreements;    /* local vs net consensus differs */
} SomaSwarmNetCtx;

/* ── Result ─────────────────────────────────────────────────────────────────── */
typedef struct {
    int   consensus_token;      /* final net-consensus token */
    float consensus_conf;       /* confidence 0..1 */
    int   n_peers_used;         /* how many peers contributed */
    int   local_agreed;         /* 1 if local swarm agreed with net consensus */
    int   domain_winner;        /* domain that drove the consensus */
} SomaSwarmNetResult;

/* ── API ─────────────────────────────────────────────────────────────────────── */

void soma_swarm_net_init(SomaSwarmNetCtx *ctx, int my_peer_id,
                         unsigned long long base_addr, int use_fixed_addr);

/* Publish local vote to peer slot (called after local swarm vote) */
void soma_swarm_net_publish(SomaSwarmNetCtx *ctx,
                            const int *tokens, const float *probs, int n,
                            float confidence, const SomaDNA *dna,
                            unsigned int timestamp_ms);

/* Read all peer slots and compute network consensus */
SomaSwarmNetResult soma_swarm_net_consensus(SomaSwarmNetCtx *ctx,
                                             const ssm_f32 *raw_logits,
                                             int vocab_size,
                                             unsigned int now_ms);

/* Print peer status table */
void soma_swarm_net_print_status(const SomaSwarmNetCtx *ctx);

/* Simple CRC32 for slot integrity */
unsigned int soma_swarm_net_crc32(const void *data, int len);

#endif /* SOMA_SWARM_NET_H */
