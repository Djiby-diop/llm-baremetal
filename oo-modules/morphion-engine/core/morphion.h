#pragma once

#include <stdint.h>

/**
 * MORPHION Engine — The Self-Repairing Organ
 * 
 * Heals the Organism by rewriting failing code at runtime.
 */

typedef struct {
    uint32_t repairs_count;
    int      healing_active;
} MorphionCtx;

typedef struct {
    uint32_t organ_id;
    uint32_t error_code;
    void*    failing_addr;
} MorphionTrappedError;

/**
 * Initializes the Morphion organ.
 */
void morphion_init(MorphionCtx *ctx);

/**
 * Traps a system error and prepares a diagnostic for the Cortex.
 */
void morphion_trap_error(MorphionCtx *ctx, MorphionTrappedError *err);

/**
 * Applies a binary patch or hot-swaps a function pointer to heal an organ.
 */
int morphion_heal_organ(MorphionCtx *ctx, uint32_t organ_id, void *new_func_addr);
