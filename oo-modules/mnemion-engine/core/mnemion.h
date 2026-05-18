#pragma once

#include <stdint.h>
#include "../ssm/core/soma_mind.h"

/**
 * MNEMION Engine — The Dreaming Organ
 * 
 * Compresses short-term experiences into long-term wisdom.
 */

typedef struct {
    uint32_t dream_count;
    float    learning_rate;
    int      is_dreaming;
} MnemionCtx;

/**
 * Initializes the Mnemion organ.
 */
void mnemion_init(MnemionCtx *ctx);

/**
 * Triggers a "Dream Cycle". 
 * Analyzes the SomaJournal and updates the Genomion/LoRA weights.
 */
void mnemion_start_dream(MnemionCtx *ctx, SomaMindCtx *m);

/**
 * Updates the learning rate based on the "surprise" (saliency) of recent events.
 */
void mnemion_refine_wisdom(MnemionCtx *ctx, float saliency);
