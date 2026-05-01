#include "memorion.h"

void memorion_init(MemorionEngine *e) {
    if (!e) return;
    e->mode = MEMORION_MODE_ON;
    e->manifests_written = 0;
    e->checks_done = 0;
}

void memorion_set_mode(MemorionEngine *e, MemorionMode mode) {
    if (!e) return;
    e->mode = mode;
}

const char *memorion_mode_name_ascii(MemorionMode mode) {
    switch (mode) {
        case MEMORION_MODE_OFF: return "off";
        case MEMORION_MODE_ON:  return "on";
        default:                return "?";
    }
}

// Simple FNV-1a hash algorithm for fast, bare-metal memory integrity checks
static uint32_t calculate_checksum(const uint8_t *data, uint32_t size) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t memorion_write_manifest(MemorionEngine *e, const void *data, uint32_t size) {
    if (!e || !data || size == 0) return 0;
    if (e->mode == MEMORION_MODE_OFF) return 0;
    
    // In a full implementation, this would write to UEFI persistent storage or NeuralFS
    uint32_t checksum = calculate_checksum((const uint8_t*)data, size);
    
    e->manifests_written++;
    return checksum;
}

int memorion_check_integrity(MemorionEngine *e, const void *data, uint32_t size, uint32_t expected_checksum) {
    if (!e || !data || size == 0) return 0;
    if (e->mode == MEMORION_MODE_OFF) return 1; // Assume OK if disabled
    
    uint32_t actual = calculate_checksum((const uint8_t*)data, size);
    e->checks_done++;
    
    return (actual == expected_checksum) ? 1 : 0;
}
