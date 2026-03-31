#pragma once

/*
 * Morphion: Morphological Boot
 *
 * At boot, LLM inspects hardware (CPUID, ACPI) and chooses a skeleton:
 * which modules to load, order, params. Same firmware morphs per platform.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MORPHION_SKELETON_MAX 16

typedef enum {
    MORPHION_MODE_OFF   = 0,
    MORPHION_MODE_PROBE = 1,  /* probe only, no morph */
    MORPHION_MODE_MORPH = 2,  /* adapt boot skeleton */
} MorphionMode;

typedef struct {
    uint32_t vendor_ebx;
    uint32_t vendor_ecx;
    uint32_t vendor_edx;
    uint32_t features_ebx;
} MorphionProbe;

typedef struct {
    MorphionMode mode;
    MorphionProbe probe;
    uint32_t modules_to_load[MORPHION_SKELETON_MAX];
    uint32_t module_count;
} MorphionEngine;

void morphion_init(MorphionEngine *e);
void morphion_set_mode(MorphionEngine *e, MorphionMode mode);

void morphion_probe(MorphionEngine *e);
uint32_t morphion_suggest_load_order(MorphionEngine *e, uint32_t *out, uint32_t cap);

const char *morphion_mode_name_ascii(MorphionMode mode);

#ifdef __cplusplus
}
#endif
