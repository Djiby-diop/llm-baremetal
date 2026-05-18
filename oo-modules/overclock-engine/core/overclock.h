#pragma once

#include <stdint.h>

/**
 * OVERCLOCK Engine — Phase 11
 * 
 * Maximizes hardware throughput for Neural Inference.
 */

typedef struct {
    int has_avx512;
    int has_avx2;
    int core_count;
    int cache_locked;
} OverclockCtx;

/**
 * Initializes the Overclock engine and detects CPU acceleration features.
 */
void overclock_init(OverclockCtx *ctx);

/**
 * Tunes the CPU for Jitter-Free performance (Disables C-States, Turbo Boost locking).
 */
void overclock_tune_cpu(OverclockCtx *ctx);

/**
 * Partition the L3 Cache to protect model weights (Intel RDT / CAT).
 */
void overclock_lock_cache(OverclockCtx *ctx);

/**
 * Reads the current CPU temperature in Celsius.
 */
int overclock_get_cpu_temp(void);
