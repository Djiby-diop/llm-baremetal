/* llmk_zones.c — Zone allocator implementation (Public Prototype) */
#include "llmk_zones.h"

static LlmkZone g_zones[ZONE_COUNT];

/* Simplified: carve fixed regions from the first available EFI conventional
 * memory descriptor above 1MB. In the real implementation this parses the
 * full UEFI memory map and selects optimal physical regions per zone. */
void llmk_zones_init(void *uefi_mmap, size_t mmap_size, size_t desc_size) {
    /* Prototype stub — real implementation parses EFI_MEMORY_DESCRIPTOR map */
    (void)uefi_mmap; (void)mmap_size; (void)desc_size;
}

void *llmk_zone_alloc(LlmkZoneId zone, size_t bytes) {
    if (zone >= ZONE_COUNT) return NULL;
    LlmkZone *z = &g_zones[zone];
    /* Align to 8 bytes */
    size_t aligned = (bytes + 7u) & ~7u;
    if (z->used + aligned > z->capacity) return NULL;
    void *ptr = z->base + z->used;
    z->used += aligned;
    return ptr;
}

void llmk_zone_reset(LlmkZoneId zone) {
    if (zone < ZONE_COUNT)
        g_zones[zone].used = 0;
}

void llmk_zones_print_stats(void) {
    /* In real impl: Print(L"[zones] boot=%zu/%zu model=%zu/%zu infer=%zu/%zu\r\n", ...) */
}
