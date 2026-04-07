/* soma_swarm_net.c — Phase Y: Distributed Swarm Consensus Implementation */

#include "soma_swarm_net.h"
#include <efi.h>
#include <efilib.h>

/* ── CRC32 (bare-metal, no stdlib) ──────────────────────────────────────────── */

static unsigned int crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_init_table(void) {
    for (int i = 0; i < 256; i++) {
        unsigned int c = (unsigned int)i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

unsigned int soma_swarm_net_crc32(const void *data, int len) {
    if (!crc32_table_ready) crc32_init_table();
    const unsigned char *p = (const unsigned char *)data;
    unsigned int crc = 0xFFFFFFFFU;
    for (int i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ── Memory helpers (bare-metal, no memset/memcpy from stdlib) ─────────────── */

static void net_zero(void *dst, int n) {
    unsigned char *p = (unsigned char *)dst;
    for (int i = 0; i < n; i++) p[i] = 0;
}

static void net_memcpy(void *dst, const void *src, int n) {
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

/* ── Float abs/min helpers ─────────────────────────────────────────────────── */

static float net_fabsf(float x) { return x < 0.0f ? -x : x; }
static float net_fminf(float a, float b) { return a < b ? a : b; }
static float net_fmaxf(float a, float b) { return a > b ? a : b; }

/* ── Slot read/write via physical address ──────────────────────────────────── */

static SomaSwarmNetSlot *slot_ptr(SomaSwarmNetCtx *ctx, int peer_id) {
    if (!ctx->use_fixed_addr || !ctx->base_addr) return 0;
    if (peer_id < 0 || peer_id >= SWARM_NET_PEERS) return 0;
    return (SomaSwarmNetSlot *)(ctx->base_addr + (unsigned long long)peer_id * SWARM_NET_SLOT_SIZE);
}

static void slot_write(SomaSwarmNetCtx *ctx, int peer_id, const SomaSwarmNetSlot *s) {
    SomaSwarmNetSlot *dst = slot_ptr(ctx, peer_id);
    if (dst) {
        net_memcpy(dst, s, sizeof(SomaSwarmNetSlot));
    }
    /* Also keep local cache */
    net_memcpy(&ctx->peers[peer_id], s, sizeof(SomaSwarmNetSlot));
}

static int slot_read(SomaSwarmNetCtx *ctx, int peer_id, SomaSwarmNetSlot *out) {
    SomaSwarmNetSlot *src = slot_ptr(ctx, peer_id);
    if (src) {
        net_memcpy(out, src, sizeof(SomaSwarmNetSlot));
    } else {
        /* Fall back to local cache (single-machine mode) */
        net_memcpy(out, &ctx->peers[peer_id], sizeof(SomaSwarmNetSlot));
    }
    return 1;
}

/* ── Slot validation ────────────────────────────────────────────────────────── */

static int slot_valid(const SomaSwarmNetSlot *s, unsigned int now_ms) {
    if (s->magic != SWARM_NET_MAGIC) return 0;
    if (s->version != SWARM_NET_VERSION) return 0;
    if (s->n_votes <= 0 || s->n_votes > SWARM_NET_MAX_TOKEN) return 0;
    /* Timestamp staleness check */
    unsigned int ts = s->timestamp_lo;
    unsigned int age = (now_ms >= ts) ? (now_ms - ts) : (0xFFFFFFFFU - ts + now_ms);
    if (age > SWARM_NET_MAX_AGE_MS) return 0;
    /* CRC check: compute over slot minus last 4 bytes */
    unsigned int expected_crc = soma_swarm_net_crc32(s, (int)(sizeof(*s) - 4));
    if (s->crc32 != expected_crc) return 0;
    return 1;
}

/* ── Init ───────────────────────────────────────────────────────────────────── */

void soma_swarm_net_init(SomaSwarmNetCtx *ctx, int my_peer_id,
                         unsigned long long base_addr, int use_fixed_addr) {
    net_zero(ctx, sizeof(*ctx));
    ctx->my_peer_id     = (my_peer_id >= 0 && my_peer_id < SWARM_NET_PEERS) ? my_peer_id : 0;
    ctx->base_addr      = base_addr;
    ctx->use_fixed_addr = use_fixed_addr;
    ctx->initialized    = 1;
    /* enabled=0 by default — user enables via /swarm_net on */
}

/* ── Publish local vote ─────────────────────────────────────────────────────── */

void soma_swarm_net_publish(SomaSwarmNetCtx *ctx,
                            const int *tokens, const float *probs, int n,
                            float confidence, const SomaDNA *dna,
                            unsigned int timestamp_ms) {
    if (!ctx->initialized) return;

    SomaSwarmNetSlot s;
    net_zero(&s, sizeof(s));

    s.magic        = SWARM_NET_MAGIC;
    s.version      = SWARM_NET_VERSION;
    s.peer_id      = (unsigned int)ctx->my_peer_id;
    s.domain_mask  = dna ? dna->domain_mask : 0x7F;
    s.generation   = dna ? dna->generation  : 0;
    s.dna_hash     = dna ? dna->parent_hash : 0;
    s.timestamp_lo = timestamp_ms;
    s.timestamp_hi = 0;
    s.n_votes      = n < SWARM_NET_MAX_TOKEN ? n : SWARM_NET_MAX_TOKEN;
    s.confidence   = confidence;
    s.temperature  = dna ? dna->temperature_solar : 0.7f;
    s.total_turns  = dna ? (int)dna->total_interactions : 0;
    s.fitness      = (int)(confidence * 10000.0f);

    for (int i = 0; i < s.n_votes; i++) {
        s.voted_tokens[i] = tokens[i];
        s.voted_probs[i]  = probs[i];
    }

    /* Compute CRC over slot minus last 4 bytes */
    s.crc32 = soma_swarm_net_crc32(&s, (int)(sizeof(s) - 4));

    slot_write(ctx, ctx->my_peer_id, &s);
    ctx->total_net_votes++;
}

/* ── Consensus computation ──────────────────────────────────────────────────── */

SomaSwarmNetResult soma_swarm_net_consensus(SomaSwarmNetCtx *ctx,
                                             const ssm_f32 *raw_logits,
                                             int vocab_size,
                                             unsigned int now_ms) {
    SomaSwarmNetResult res;
    net_zero(&res, sizeof(res));
    res.consensus_token = -1;

    if (!ctx->enabled || !ctx->initialized) {
        return res;
    }

    /* Accumulate weighted votes across peers */
    /* Score array: token_id → accumulated weighted probability */
    /* Use a fixed-size top-K table to avoid large stack allocation */
    #define NET_TOP_K 64
    int   top_tokens[NET_TOP_K];
    float top_scores[NET_TOP_K];
    int   top_count = 0;
    net_zero(top_tokens, sizeof(top_tokens));
    net_zero(top_scores, sizeof(top_scores));

    int n_valid = 0;
    ctx->n_active_peers = 0;

    for (int p = 0; p < SWARM_NET_PEERS; p++) {
        SomaSwarmNetSlot slot;
        if (!slot_read(ctx, p, &slot)) continue;
        if (!slot_valid(&slot, now_ms)) {
            if (p != ctx->my_peer_id) ctx->total_stale_peers++;
            continue;
        }

        ctx->peer_valid[p] = 1;
        n_valid++;
        ctx->n_active_peers++;

        /* Weight by confidence and fitness */
        float weight = slot.confidence * net_fmaxf(0.1f, (float)slot.fitness / 10000.0f);

        for (int v = 0; v < slot.n_votes; v++) {
            int tok = slot.voted_tokens[v];
            if (tok < 0 || tok >= vocab_size) continue;
            float prob = slot.voted_probs[v] * weight;

            /* Update top-K table */
            int found = 0;
            for (int k = 0; k < top_count; k++) {
                if (top_tokens[k] == tok) {
                    top_scores[k] += prob;
                    found = 1;
                    break;
                }
            }
            if (!found && top_count < NET_TOP_K) {
                top_tokens[top_count] = tok;
                top_scores[top_count] = prob;
                top_count++;
            }
        }
    }

    if (n_valid == 0 || top_count == 0) {
        return res;
    }

    /* Pick token with highest accumulated score */
    int best_idx = 0;
    float best_score = top_scores[0];
    for (int k = 1; k < top_count; k++) {
        if (top_scores[k] > best_score) {
            best_score = top_scores[k];
            best_idx = k;
        }
    }

    res.consensus_token = top_tokens[best_idx];
    res.n_peers_used    = n_valid;
    res.consensus_conf  = net_fminf(1.0f, best_score / (float)n_valid);

    /* Check if local (my_peer_id) agreed */
    SomaSwarmNetSlot *my_slot = &ctx->peers[ctx->my_peer_id];
    if (my_slot->n_votes > 0 && my_slot->voted_tokens[0] == res.consensus_token) {
        res.local_agreed = 1;
    } else {
        ctx->total_disagreements++;
    }

    ctx->total_consensus_events++;
    return res;
    #undef NET_TOP_K
}

/* ── Status print ───────────────────────────────────────────────────────────── */

void soma_swarm_net_print_status(const SomaSwarmNetCtx *ctx) {
    Print(L"\r\n[SwarmNet] enabled=%d  my_peer=%d  active=%d/%d\r\n",
          ctx->enabled, ctx->my_peer_id, ctx->n_active_peers, SWARM_NET_PEERS);
    Print(L"  net_votes=%d  consensus=%d  stale=%d  disagreements=%d\r\n",
          ctx->total_net_votes, ctx->total_consensus_events,
          ctx->total_stale_peers, ctx->total_disagreements);
    Print(L"  base_addr=0x%llX  use_fixed=%d\r\n\r\n",
          ctx->base_addr, ctx->use_fixed_addr);
    for (int p = 0; p < SWARM_NET_PEERS; p++) {
        const SomaSwarmNetSlot *s = &ctx->peers[p];
        if (s->magic != SWARM_NET_MAGIC) continue;
        Print(L"  peer[%d] gen=%d conf=%d/100 turns=%d tok=%d\r\n",
              p, s->generation,
              (int)(s->confidence * 100.0f),
              s->total_turns,
              s->n_votes > 0 ? s->voted_tokens[0] : -1);
    }
}
