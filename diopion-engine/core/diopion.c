
#include "diopion.h"

void diopion_init(DiopionEngine *e) {
    if (!e) return;
    e->mode = DIOPION_MODE_OFF;
    e->profile = DIOPION_PROFILE_NONE;

    // Conservative defaults: small burst to keep responses snappy.
    e->params.burst_turns_default = 1;
    e->params.burst_max_gen_tokens = 128;
    e->params.burst_top_k = 40;
    e->params.burst_temp_milli = 900;

    e->params.bio_temp_jitter_milli = 120;

    e->bursts_started = 0;
}

void diopion_set_mode(DiopionEngine *e, DiopionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void diopion_set_profile(DiopionEngine *e, DiopionProfile profile) {
    if (!e) return;
    e->profile = profile;

    // Profile presets (v0.1)
    if (profile == DIOPION_PROFILE_ANIMAL) {
        e->params.burst_turns_default = 1;
        e->params.burst_max_gen_tokens = 96;
        e->params.burst_top_k = 40;
        e->params.burst_temp_milli = 950;
    } else if (profile == DIOPION_PROFILE_VEGETAL) {
        // Vegetal focuses on quick suspend/resume workflows (implemented in REPL glue).
        e->params.burst_turns_default = 1;
        e->params.burst_max_gen_tokens = 64;
        e->params.burst_top_k = 30;
        e->params.burst_temp_milli = 800;
    } else if (profile == DIOPION_PROFILE_GEOM) {
        // Geometric: slightly more deterministic, good for compact outputs.
        e->params.burst_turns_default = 1;
        e->params.burst_max_gen_tokens = 96;
        e->params.burst_top_k = 20;
        e->params.burst_temp_milli = 750;
    } else if (profile == DIOPION_PROFILE_BIO) {
        // Bio: exploration/mutation.
        e->params.burst_turns_default = 1;
        e->params.burst_max_gen_tokens = 128;
        e->params.burst_top_k = 60;
        e->params.burst_temp_milli = 1050;
    }
}

const char *diopion_mode_name_ascii(DiopionMode mode) {
    if (mode == DIOPION_MODE_OFF) return "off";
    if (mode == DIOPION_MODE_OBSERVE) return "observe";
    if (mode == DIOPION_MODE_ENFORCE) return "enforce";
    return "?";
}

const char *diopion_profile_name_ascii(DiopionProfile p) {
    if (p == DIOPION_PROFILE_NONE) return "none";
    if (p == DIOPION_PROFILE_ANIMAL) return "animal";
    if (p == DIOPION_PROFILE_VEGETAL) return "vegetal";
    if (p == DIOPION_PROFILE_GEOM) return "geom";
    if (p == DIOPION_PROFILE_BIO) return "bio";
    return "?";
}
