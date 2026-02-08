#pragma once

#include <stdint.h>

// Djibion: meta-engine of coherence for llm-baremetal.
// - Runs freestanding (no malloc / no libc requirements).
// - Provides: Bio-Code parsing (ATCG), intent struct, triangulated validation,
//   and a decision/verdict to gate actions.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DJIBION_OK = 0,
    DJIBION_ERR_INVALID = -1,
    DJIBION_ERR_TRUNCATED = -2,
} DjibionStatus;

typedef enum {
    DJIBION_VERDICT_ALLOW = 0,
    DJIBION_VERDICT_TRANSFORM = 1,
    DJIBION_VERDICT_REJECT = 2,
    DJIBION_VERDICT_FREEZE = 3,
} DjibionVerdict;

typedef enum {
    DJIBION_MODE_OFF = 0,
    DJIBION_MODE_OBSERVE = 1,
    DJIBION_MODE_ENFORCE = 2,
} DjibionMode;

typedef enum {
    DJIBION_ACT_NONE = 0,

    DJIBION_ACT_FS_WRITE = 10,
    DJIBION_ACT_FS_APPEND = 11,
    DJIBION_ACT_FS_RM = 12,
    DJIBION_ACT_FS_CP = 13,
    DJIBION_ACT_FS_MV = 14,

    DJIBION_ACT_SNAP_LOAD = 20,
    DJIBION_ACT_SNAP_SAVE = 21,

    DJIBION_ACT_OO_EXEC = 30,
    DJIBION_ACT_OO_AUTO = 31,

    DJIBION_ACT_OO_SAVE = 32,
    DJIBION_ACT_OO_LOAD = 33,

    DJIBION_ACT_AUTORUN = 40,

    // Writes to persistent configuration (e.g. repl.cfg setters)
    DJIBION_ACT_CFG_WRITE = 50,
} DjibionAction;

typedef enum {
    DJIBION_INTENT_NONE = 0,
    DJIBION_INTENT_MEMORY_BIND = 1,
    DJIBION_INTENT_IO_WRITE = 2,
    DJIBION_INTENT_IO_DELETE = 3,
    DJIBION_INTENT_RESUME = 4,
    DJIBION_INTENT_PLAN = 5,
} DjibionIntentType;

typedef struct {
    uint8_t ok;      // 0/1
    uint8_t score;   // 0..100
} DjibionCheck;

typedef struct {
    DjibionCheck sense;
    DjibionCheck structure;
    DjibionCheck reality;
} DjibionTriangle;

typedef struct {
    DjibionIntentType type;
    uint8_t ttl;     // 0..100 (best-effort)
    uint8_t scope;   // 0=local 1=global (best-effort)
    uint32_t hash;   // stable hash of the biocode string
} DjibionIntent;

typedef struct {
    // Hard limits
    uint32_t max_fs_write_bytes;

    // Snapshot size limit (total bytes written for /snap_save)
    uint32_t max_snap_bytes;

    uint32_t max_oo_cycles;

    // Policy knobs
    uint8_t allow_fs_delete;
    uint8_t allow_fs_write;

    uint8_t allow_snap_load;
    uint8_t allow_snap_save;

    uint8_t allow_cfg_write;

    uint8_t allow_autorun;
    uint8_t allow_oo_exec;
    uint8_t allow_oo_auto;
    uint8_t allow_oo_persist;

    // Optional: restrict FS mutations to this prefix (ASCII, '\\' paths).
    // Empty => no prefix restriction.
    char fs_mut_prefix[64];
} DjibionLaws;

typedef struct {
    DjibionMode mode;
    DjibionLaws laws;

    // Diagnostics counters
    uint32_t decisions_total;
    uint32_t decisions_rejected;
    uint32_t decisions_transformed;
} DjibionEngine;

typedef struct {
    DjibionVerdict verdict;
    DjibionTriangle tri;
    uint8_t risk; // 0..100

    // ASCII short message (best-effort). Always NUL-terminated.
    char reason[96];

    // Optional transformed path (e.g. prefix enforced). NUL-terminated.
    char transformed_arg0[160];
} DjibionDecision;

void djibion_init(DjibionEngine *e);
void djibion_set_mode(DjibionEngine *e, DjibionMode mode);

// Bio-Code: accepts strings like "ATG-CGA-TTA" (ignores '-' and spaces).
// Returns DJIBION_OK and fills intent on success.
DjibionStatus djibion_biocode_to_intent(const char *biocode, DjibionIntent *out_intent);

// Main decision function.
// - arg0 is usually a path or label.
// - arg1 is usually a size/bytes or counter.
DjibionStatus djibion_decide(DjibionEngine *e,
                             DjibionAction act,
                             const char *arg0,
                             uint32_t arg1,
                             DjibionDecision *out);

#ifdef __cplusplus
}
#endif
