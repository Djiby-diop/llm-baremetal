#pragma once

/*
 * Phase 5 (Zig): Metabolism Profiles
 *
 * This is the fallback (balanced) profile used when Zig is not available.
 * The build can generate metabion_profile.h from tools/metabion_profile_gen.zig.
 */

#define METABION_PROFILE_NAME "balanced"

// Default sampling knobs (used only as defaults; repl.cfg can override).
#define METABION_DEFAULT_TEMPERATURE    0.85f
#define METABION_DEFAULT_MIN_P          0.05f
#define METABION_DEFAULT_TOP_P          0.95f
#define METABION_DEFAULT_TOP_K          80
#define METABION_DEFAULT_REPEAT_PENALTY 1.15f
#define METABION_DEFAULT_NO_REPEAT_NGRAM 4
#define METABION_DEFAULT_MAX_GEN_TOKENS 160
#define METABION_DEFAULT_STATS_ENABLED  1
#define METABION_DEFAULT_STOP_ON_YOU    1
#define METABION_DEFAULT_STOP_ON_DOUBLE_NL 0

// Metabion engine default mode:
// 0=off, 1=track, 2=guide
#define METABION_DEFAULT_METABION_MODE 1
