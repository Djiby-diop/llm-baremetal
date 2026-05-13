/* llmk_zones.h — OO Bare-Metal Zone Allocator (Public Prototype)
 *
 * Three dedicated memory zones, no libc, no OS:
 *   ZONE_BOOT      — early scratch (UEFI phase)
 *   ZONE_MODEL     — large contiguous region for model weights
 *   ZONE_INFERENCE — KV-cache + activations, O(1) reset between requests
 */
#ifndef LLMK_ZONES_H
#define LLMK_ZONES_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    ZONE_BOOT      = 0,
    ZONE_MODEL     = 1,
    ZONE_INFERENCE = 2,
    ZONE_COUNT     = 3
} LlmkZoneId;

typedef struct {
    uint8_t *base;
    size_t   capacity;
    size_t   used;
    int      id;
} LlmkZone;

/* Initialize all zones from UEFI memory map */
void llmk_zones_init(void *uefi_mmap, size_t mmap_size, size_t desc_size);

/* Allocate from a specific zone (bump allocator, 8-byte aligned) */
void *llmk_zone_alloc(LlmkZoneId zone, size_t bytes);

/* Reset a zone — O(1), just resets the bump pointer */
void llmk_zone_reset(LlmkZoneId zone);

/* Print zone statistics (to UEFI ConOut) */
void llmk_zones_print_stats(void);

#endif /* LLMK_ZONES_H */
