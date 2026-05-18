#pragma once

#include "../../../united-baremetal/include/united_bus.h"
#include <stdint.h>

/**
 * MERCATORION Engine — The Sovereign Trading Organ
 * 
 * Optimized for bare-metal execution with AVX2/AVX512.
 * Directly interfaces with the United-Bus for market data and execution.
 */

#define MERCATOR_MAX_LEVELS 128
#define MERCATOR_MAGIC      0x4D455243u // "MERC"

typedef enum {
    MERCATOR_SIDE_BUY  = 0,
    MERCATOR_SIDE_SELL = 1
} MercatorSide;

typedef struct {
    float price;
    float volume;
    uint64_t timestamp;
} MercatorLevel;

typedef struct {
    MercatorLevel bids[MERCATOR_MAX_LEVELS];
    MercatorLevel asks[MERCATOR_MAX_LEVELS];
    uint32_t      bid_count;
    uint32_t      ask_count;
    float         spread;
    float         mid_price;
} MercatorOrderBook;

typedef struct {
    float    price;
    uint32_t node_id;
    uint32_t latency_ms;
} MercatorRemotePrice;

typedef struct {
    uint32_t      magic;
    int           active;
    
    // The sensory input organ
    MercatorOrderBook book;
    
    // Remote prices from Collectivion
    MercatorRemotePrice remote_bids[16]; // Max 16 nodes
    MercatorRemotePrice remote_asks[16];
    uint32_t            node_count;
    
    // Performance metrics
    uint64_t      total_signals;
    uint64_t      total_execution_ns;
    
    // Shadow Sensing
    uint64_t      last_update_tsc;
    uint64_t      avg_jitter_tsc;

    // United-Bus Integration
    uint8_t       bus_organ_id;
} MercatorCtx;

/* --- Lifecycle --- */

void mercator_init(MercatorCtx *ctx);
void mercator_pulse(MercatorCtx *ctx);

/* --- Market Data Ingestion --- */

/**
 * Absorbs market data from GLOBULE_GOLD and updates the internal book.
 * This is where AVX2 optimization happens to keep the book sorted.
 */
void mercator_absorb_market_data(MercatorCtx *ctx, globule_t *globule);

/* --- Strategy & Execution --- */

/**
 * Computes trade signals using Vomerion (Neural-Strand) surprise levels.
 * If a signal is strong, it pumps a GLOBULE_SILVER onto the bus.
 */
void mercator_compute_signals(MercatorCtx *ctx, float vomerion_saliency);

/**
 * Executes a trade signal by broadcasting it to the relevant execution gateway.
 */
int mercator_execute_signal(MercatorCtx *ctx, MercatorSide side, float price, float volume, float saliency);
