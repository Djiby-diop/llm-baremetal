/*
 * Synaption: Synaptic Memory - core implementation.
 * Tracks memory blocks and access hints to manage "neural importance" heat maps.
 */

#include "synaption.h"

#define MAX_SYNAPTION_BLOCKS 128

static SynaptionBlock s_blocks[MAX_SYNAPTION_BLOCKS];
static uint32_t s_block_count = 0;

void synaption_init(SynaptionEngine *e) {
    if (!e) return;
    e->mode = SYNAPTION_MODE_OFF;
    e->blocks_tracked = 0;
    e->promotions = 0;
    s_block_count = 0;
}

void synaption_set_mode(SynaptionEngine *e, SynaptionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void synaption_register(SynaptionEngine *e, void *base, uint64_t size, SynaptionTier tier) {
    if (!e || !base) return;
    if (e->mode == SYNAPTION_MODE_OFF) return;
    
    // Ensure we don't register twice
    for (uint32_t i = 0; i < s_block_count; i++) {
        if (s_blocks[i].base == base) {
            return;
        }
    }
    
    if (s_block_count < MAX_SYNAPTION_BLOCKS) {
        s_blocks[s_block_count].base = base;
        s_blocks[s_block_count].size = size;
        s_blocks[s_block_count].tier = tier;
        s_blocks[s_block_count].access_count = 0;
        s_block_count++;
        e->blocks_tracked++;
    }
}

void synaption_touch(SynaptionEngine *e, void *ptr) {
    if (!e || !ptr) return;
    if (e->mode == SYNAPTION_MODE_OFF) return;
    
    // Find block containing the pointer
    for (uint32_t i = 0; i < s_block_count; i++) {
        uint8_t *b_start = (uint8_t *)s_blocks[i].base;
        uint8_t *b_end = b_start + s_blocks[i].size;
        uint8_t *p = (uint8_t *)ptr;
        
        if (p >= b_start && p < b_end) {
            s_blocks[i].access_count++;
            
            // Auto-promote if accessed frequently
            if (e->mode == SYNAPTION_MODE_PIN) {
                if (s_blocks[i].tier == SYNAPTION_TIER_COLD && s_blocks[i].access_count > 10) {
                    s_blocks[i].tier = SYNAPTION_TIER_WARM;
                    e->promotions++;
                } else if (s_blocks[i].tier == SYNAPTION_TIER_WARM && s_blocks[i].access_count > 100) {
                    s_blocks[i].tier = SYNAPTION_TIER_HOT;
                    e->promotions++;
                }
            }
            return;
        }
    }
}

const char *synaption_mode_name_ascii(SynaptionMode mode) {
    switch (mode) {
        case SYNAPTION_MODE_OFF:   return "off";
        case SYNAPTION_MODE_TRACK: return "track";
        case SYNAPTION_MODE_PIN:   return "pin";
        default:                   return "?";
    }
}

/* Prune cold blocks: called when hot_count < half of total_count */
void synaption_prune(SynaptionEngine *e, int hot_count, int total_count) {
    if (!e) return;
    if (e->mode != SYNAPTION_MODE_PIN) return;
    
    if (hot_count < (total_count / 2)) {
        for (uint32_t i = 0; i < s_block_count; i++) {
            // Decay access counts over time
            s_blocks[i].access_count /= 2;
            
            if (s_blocks[i].access_count == 0 && s_blocks[i].tier > SYNAPTION_TIER_COLD) {
                s_blocks[i].tier = SYNAPTION_TIER_COLD; // Demote
            }
        }
    }
}
