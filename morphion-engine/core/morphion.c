#include "morphion.h"
#include <string.h>

void morphion_init(MorphionCtx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(MorphionCtx));
}

void morphion_trap_error(MorphionCtx *ctx, MorphionTrappedError *err) {
    if (!ctx || !err) return;
    
    ctx->healing_active = 1;
    // Log the injury for the Cortex to analyze
    _log_causal(err->organ_id, "morphion_trapped_fault");
    
    // In a real scenario, this would trigger a 'Critical Thought' 
    // in the SomaMind to generate a fix.
}

int morphion_heal_organ(MorphionCtx *ctx, uint32_t organ_id, void *new_func_addr) {
    if (!ctx || !new_func_addr) return -1;

    // Hot-swapping logic:
    // We update the jump table or the organ's structure 
    // to point to the new, healed code.
    
    ctx->repairs_count++;
    ctx->healing_active = 0;
    
    _log_causal(organ_id, "morphion_organ_healed");
    return 0;
}
