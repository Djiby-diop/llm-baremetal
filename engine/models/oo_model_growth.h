/* oo_model_growth.h — OO Model Self-Expansion Engine  Phase 5F
 * ==============================================================
 * OO models grow automatically on the NVMe/ESP disk:
 *
 * Growth pipeline:
 *   1. MONITOR — track usage: topics, query complexity, failure modes
 *   2. ANALYZE — identify knowledge gaps via oracle feedback
 *   3. ACQUIRE — download larger/specialized checkpoints via HTTP
 *   4. MERGE   — apply LoRA delta or quantize to fit constraints
 *   5. VALIDATE — run benchmark before/after
 *   6. APPROVE  — human-in-the-loop D+ gate
 *   7. ACTIVATE — hot-swap model weights in DIOP engine
 *
 * Storage layout on NVMe/ESP:
 *   /models/                  — active model weights
 *   /models/cache/            — downloaded candidates
 *   /models/lora/             — LoRA deltas
 *   /models/growth.log        — growth history
 *   /models/growth.cfg        — growth configuration
 *
 * Size constraints:
 *   - RAM limit: configurable (default: 75% of available RAM)
 *   - Disk limit: configurable (default: 50% of data partition)
 *   - Growth step: prefer smaller deltas (LoRA) over full replacements
 *
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include <efilib.h>
#include "../models/oo_diop_model.h"
#include "../network/oo_netboot.h"

#define OO_GROWTH_LOG_PATH    "\\models\\growth.log"
#define OO_GROWTH_CFG_PATH    "\\models\\growth.cfg"
#define OO_GROWTH_CACHE_PATH  "\\models\\cache\\"
#define OO_GROWTH_LORA_PATH   "\\models\\lora\\"

#define OO_GROWTH_MAX_CANDIDATES  8
#define OO_GROWTH_TOPIC_SLOTS     32

/* Growth trigger reasons */
typedef enum {
    OO_GROWTH_REASON_NONE       = 0,
    OO_GROWTH_REASON_LOW_CONF   = 1, /* Model confidence < threshold */
    OO_GROWTH_REASON_REPEAT_FAIL= 2, /* Same topic fails 3+ times */
    OO_GROWTH_REASON_USER_REQ   = 3, /* User explicitly requests upgrade */
    OO_GROWTH_REASON_SCHEDULED  = 4, /* Periodic scheduled upgrade */
    OO_GROWTH_REASON_ORACLE     = 5, /* Oracle recommends upgrade */
    OO_GROWTH_REASON_CODEBASE   = 6, /* Codebase grew, model needs update */
} OoGrowthReason;

/* Growth action types */
typedef enum {
    OO_GROWTH_ACT_NONE      = 0,
    OO_GROWTH_ACT_DOWNLOAD  = 1, /* Download full model */
    OO_GROWTH_ACT_LORA      = 2, /* Apply LoRA delta */
    OO_GROWTH_ACT_QUANTIZE  = 3, /* Re-quantize for size/speed */
    OO_GROWTH_ACT_FINE_TUNE = 4, /* In-context fine-tune (future) */
    OO_GROWTH_ACT_MERGE     = 5, /* Merge two model variants */
} OoGrowthAction;

typedef struct {
    CHAR8   topic[64];
    UINT32  hit_count;
    UINT32  fail_count;
    float   avg_confidence;
} OoTopicStat;

/* A candidate model for growth */
typedef struct {
    CHAR8          name[128];
    CHAR8          url[256];
    CHAR8          format[8];    /* "bin" or "gguf" */
    UINT64         size_bytes;
    UINT32         dim;
    float          expected_improvement;
    float          dplus_score;
    OoGrowthAction action;
} OoGrowthCandidate;

typedef struct {
    int            initialized;
    /* Config */
    float          ram_limit_pct;     /* default 0.75 */
    float          disk_limit_pct;    /* default 0.50 */
    float          min_confidence_threshold; /* below this → trigger growth */
    UINT32         fail_count_trigger;       /* default 3 */
    int            auto_approve;      /* 0 = always need human approval */
    /* Stats */
    UINT32         total_growths;
    UINT32         total_downloads;
    UINT64         total_bytes_downloaded;
    UINT64         total_bytes_saved;     /* from compression/quant */
    /* Topic tracking */
    OoTopicStat    topics[OO_GROWTH_TOPIC_SLOTS];
    UINT32         n_topics;
    /* Current run */
    OoGrowthCandidate candidates[OO_GROWTH_MAX_CANDIDATES];
    int              n_candidates;
    int              pending_approval;   /* 1 = waiting for human /growth_approve */
    OoGrowthCandidate pending;           /* the one awaiting approval */
    OoGrowthReason   last_reason;
} OoModelGrowth;

/* Lifecycle */
void oo_growth_init(OoModelGrowth *g);

/* Monitor: record a query result to update topic stats */
void oo_growth_record(OoModelGrowth *g, const CHAR8 *topic,
                       float confidence, int failed);

/* Analyze: check if growth is warranted */
OoGrowthReason oo_growth_should_grow(OoModelGrowth *g,
                                      const OoDiopModel *model);

/* Acquire: build candidate list from HuggingFace / oracle recommendation */
EFI_STATUS oo_growth_find_candidates(OoModelGrowth *g,
                                      OoGrowthReason reason);

/* Validate candidate (runs bench before/after) */
float oo_growth_validate(OoModelGrowth *g, OoDiopModel *model,
                          const OoGrowthCandidate *cand);

/* Present to human for approval (REPL prompt) */
void oo_growth_request_approval(OoModelGrowth *g, int cand_idx);

/* Apply approved growth (download + swap) */
EFI_STATUS oo_growth_apply(OoModelGrowth *g, OoDiopModel *model,
                             EFI_FILE_HANDLE Root);

/* Full autonomous pipeline (runs all steps, pauses at approval) */
EFI_STATUS oo_growth_run_pipeline(OoModelGrowth *g, OoDiopModel *model,
                                   EFI_FILE_HANDLE Root);

/* Log growth event to disk */
void oo_growth_log(OoModelGrowth *g, EFI_FILE_HANDLE Root,
                   const CHAR8 *event);

/* Print status */
void oo_growth_print_status(const OoModelGrowth *g);

/* REPL */
int oo_growth_repl_cmd(OoModelGrowth *g, OoDiopModel *model,
                        const char *cmd, EFI_FILE_HANDLE Root);

extern OoModelGrowth g_growth;
