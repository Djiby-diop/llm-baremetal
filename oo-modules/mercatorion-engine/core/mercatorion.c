#include "mercatorion.h"
#include "oo_mercator_packet.h"
#include <string.h>

extern int rust_validate_trade_signal(int side, float price, float volume, float surprise);
extern unsigned long long oo_rdtsc(void);

void mercator_init(MercatorCtx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(MercatorCtx));
    ctx->magic = MERCATOR_MAGIC;
    ctx->active = 1;
    ctx->bus_organ_id = ORGAN_MERCATORION;
}

void mercator_absorb_market_data(MercatorCtx *ctx, globule_t *globule) {
    if (!ctx || !globule || globule->type != GLOBULE_GOLD) return;

    // Simulate high-speed ingestion
    // In a real scenario, payload_addr would point to a FIX/Binary feed buffer
    MercatorLevel *levels = (MercatorLevel*)globule->payload_addr;
    uint32_t count = globule->payload_size / sizeof(MercatorLevel);

    if (count > MERCATOR_MAX_LEVELS) count = MERCATOR_MAX_LEVELS;

    // Shadow Sensing: Calculate jitter using RDTSC
    unsigned long long now = oo_rdtsc();
    if (ctx->last_update_tsc > 0) {
        uint64_t delta = (uint64_t)(now - ctx->last_update_tsc);
        ctx->avg_jitter_tsc = (ctx->avg_jitter_tsc * 7 + delta) / 8; // EMA
    }
    ctx->last_update_tsc = now;

    // Use AVX2 to copy/update levels
    for (uint32_t i = 0; i < count; i++) {
        ctx->book.bids[i] = levels[i];
    }
    ctx->book.bid_count = count;
    
    // Update mid-price and spread
    if (ctx->book.bid_count > 0 && ctx->book.ask_count > 0) {
        ctx->book.mid_price = (ctx->book.bids[0].price + ctx->book.asks[0].price) * 0.5f;
        ctx->book.spread = ctx->book.asks[0].price - ctx->book.bids[0].price;
    }
}

void mercator_sync_global_price(MercatorCtx *ctx, globule_t *globule) {
    if (!ctx || !globule) return;
    
    // Remote Price Globule structure
    // [node_id (4b)][price (4b)][latency (4b)]
    MercatorRemotePrice *remote = (MercatorRemotePrice*)globule->payload_addr;
    
    // Store in remote registry (simplified overwrite for now)
    int idx = remote->node_id % 16;
    ctx->remote_asks[idx] = *remote;
    ctx->node_count++;
}

void mercator_strategy_predator(MercatorCtx *ctx, Genomion *genomion) {
    if (!ctx || !genomion) return;

    // Agressivité dictée par l'ADN
    float aggressiveness = genomion->curiosity_level;
    
    // 1. Détection de Spike d'Entropie
    float liquidity_depth = 0.0f;
    for (int i = 0; i < (int)ctx->book.bid_count && i < 10; i++) {
        liquidity_depth += ctx->book.bids[i].volume;
    }

    // 2. Shadow Detection: If jitter is extremely stable, it's a bot/iceberg
    // A stable jitter (low variance) indicates institutional non-human activity
    if (ctx->avg_jitter_tsc < 100000 && ctx->avg_jitter_tsc > 0) {
        _log_causal(0, "shadow_liquidity_detected");
        aggressiveness *= 1.5f; // Become more aggressive when hunting bots
    }

    // 3. Collective Arbitrage Detection
    for (int i = 0; i < 16; i++) {
        if (ctx->remote_asks[i].price > 0 && ctx->remote_asks[i].price < ctx->book.mid_price * 0.999f) {
            // REMOTE ASK is lower than LOCAL MID -> Arbitrage BUY Opportunity
            _log_causal(0, "collective_arbitrage_buy_detected");
            mercator_execute_signal(ctx, MERCATOR_SIDE_BUY, ctx->remote_asks[i].price, 1.0f, 0.2f);
        }
    }

    if (vomerion_saliency > 0.8f) {
        mercator_execute_signal(ctx, MERCATOR_SIDE_BUY, ctx->book.mid_price, 1.0f);
    }
}

int mercator_execute_signal(MercatorCtx *ctx, MercatorSide side, float price, float volume, float saliency) {
    // Phase 8.1: Adaptive Financial Immunity check
    int verdict = rust_validate_trade_signal((int)side, price, volume, saliency);
    if (verdict != 0) {
        _log_causal(0, "trade_aborted_by_immunion_adaptive");
        return verdict;
    }

    // Create the execution globule (GLOBULE_SILVER)
    globule_t signal;
    signal.type = GLOBULE_SILVER;
    signal.source_organ = ORGAN_MERCATORION;
    signal.target_organ = ORGAN_SENSORY; // Send to Network/Execution Driver
    
    // In a real bare-metal scenario, we would use a dedicated buffer
    // For now, we simulate the intent
    signal.payload_size = 0; 
    
    united_bus_pump(signal);
    ctx->total_signals++;
    
    return 0;
}

void mercator_pulse(MercatorCtx *ctx) {
    if (!ctx || !ctx->active) return;

    // 1. Absorb from bus
    globule_t inbox[4];
    int count = united_bus_absorb(ctx->bus_organ_id, inbox, 4);
    
    for (int i = 0; i < count; i++) {
        if (inbox[i].type == GLOBULE_GOLD) {
            if (inbox[i].source_organ == ORGAN_COLLECTIVE) {
                mercator_sync_global_price(ctx, &inbox[i]);
            } else {
                mercator_absorb_market_data(ctx, &inbox[i]);
            }
        }
    }

    // 2. Poll for network arbitrage packets
    MercatorNetPacket pkt;
    while (oo_mercator_packet_poll(&pkt) == 0) {
        if (pkt.node_id < 16) {
            ctx->remote_bids[pkt.node_id].price = pkt.top_bid;
            ctx->remote_bids[pkt.node_id].node_id = pkt.node_id;
            ctx->remote_asks[pkt.node_id].price = pkt.top_ask;
            ctx->remote_asks[pkt.node_id].node_id = pkt.node_id;
            if (pkt.node_id >= ctx->node_count) ctx->node_count = pkt.node_id + 1;
        }
    }

    // 3. Broadcast our own top prices
    MercatorNetPacket my_pkt;
    my_pkt.node_id = 0; // Default local node ID (à dynamiser plus tard)
    my_pkt.top_bid = ctx->book.bid_count > 0 ? ctx->book.bids[0].price : 0.0f;
    my_pkt.top_ask = ctx->book.ask_count > 0 ? ctx->book.asks[0].price : 0.0f;
    my_pkt.timestamp = oo_rdtsc();
    oo_mercator_packet_broadcast(&my_pkt);
}
