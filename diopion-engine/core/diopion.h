#pragma once

#include <stdint.h>

// Diopion: complementary engine to Djibion.
// Philosophy: speed, mutation, and bursty exploration ("chaos controlled").
// - Freestanding friendly (no malloc / no libc requirements).
// - Does NOT override Djibion safety gates; it only proposes/applies runtime tuning.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DIOPION_OK = 0,
    DIOPION_ERR_INVALID = -1,
} DiopionStatus;

typedef enum {
    DIOPION_MODE_OFF = 0,
    DIOPION_MODE_OBSERVE = 1,
    DIOPION_MODE_ENFORCE = 2,
} DiopionMode;

typedef enum {
    DIOPION_PROFILE_NONE = 0,
    DIOPION_PROFILE_ANIMAL = 1,
    DIOPION_PROFILE_VEGETAL = 2,
    DIOPION_PROFILE_GEOM = 3,
    DIOPION_PROFILE_BIO = 4,
} DiopionProfile;

typedef struct {
    // Burst defaults used by /diopion_burst when args are omitted.
    // Temperature is expressed in milli-units (e.g. 900 => 0.900).
    uint32_t burst_turns_default;
    uint32_t burst_max_gen_tokens;
    uint32_t burst_top_k;
    uint32_t burst_temp_milli;

    // Mutation amplitude for BIO profile (best-effort). In milli-units.
    uint32_t bio_temp_jitter_milli;
} DiopionParams;

typedef struct {
    DiopionMode mode;
    DiopionProfile profile;
    DiopionParams params;

    // Diagnostics
    uint32_t bursts_started;
} DiopionEngine;

void diopion_init(DiopionEngine *e);
void diopion_set_mode(DiopionEngine *e, DiopionMode mode);
void diopion_set_profile(DiopionEngine *e, DiopionProfile profile);

const char *diopion_mode_name_ascii(DiopionMode mode);
const char *diopion_profile_name_ascii(DiopionProfile p);

#ifdef __cplusplus
}
#endif
