#include "mnemion.h"
#include <string.h>

void mnemion_init(MnemionCtx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(MnemionCtx));
    ctx->learning_rate = 0.01f;
}

void mnemion_start_dream(MnemionCtx *ctx, SomaMindCtx *m) {
    if (!ctx || !m || ctx->is_dreaming) return;
    
    ctx->is_dreaming = 1;
    _log_causal(0, "mnemion_dream_cycle_started");

    // High-speed replay of the journal
    for (int i = 0; i < SOMA_MIND_JOURNAL_MAX; i++) {
        // We simulate the "re-thinking" of past events
        // In a real implementation, we would feed the journal entries
        // back into the SSM (Mamba) to update LoRA weights.
        
        if (m->router) {
            // "Imagine" the event again
            mnemion_refine_wisdom(ctx, m->router->last_saliency);
        }
    }

    ctx->dream_count++;
    ctx->is_dreaming = 0;
    _log_causal(0, "mnemion_wisdom_crystallized");
}

void mnemion_refine_wisdom(MnemionCtx *ctx, float saliency) {
    // If an event was very surprising (high saliency), 
    // we increase the learning rate for that "memory".
    if (saliency > 0.9f) {
        ctx->learning_rate *= 1.1f;
    } else {
        ctx->learning_rate *= 0.99f; // Decay for boring events
    }
}
