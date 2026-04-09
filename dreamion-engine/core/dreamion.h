#pragma once

/*
 * Dreamion — Dream State Engine
 *
 * During idle (no user input), OO enters a dream state and runs
 * background consolidation tasks:
 *
 *   LIGHT mode (idle > 500 ticks):
 *     - KV cache dedup: scan token-pair ring buffer, flag duplicates
 *     - Journal compaction: summarize old entries into gist tokens
 *     - Prompt pre-warming: predict likely next user input
 *
 *   DEEP mode (idle > 2000 ticks):
 *     - Synthetic self-play: generate training pairs from memory
 *     - DNA mutation hint: adjust cognition_bias from dominant domain
 *     - Memory consolidation: merge soma_dream summaries into long-term
 *     - JSONL flush: write outputs to OO_DREAM.BIN (training pipeline)
 *
 * Integration:
 *   - soma_dream.h (SSM layer): provides SomaDreamSummary
 *   - soma_dna.h:  DNA mutation via recommended_bias_delta
 *   - soma_smb.h:  SMB slot scan for consolidation
 *
 * Freestanding C11 — no libc, no malloc.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ─────────────────────────────────────────────────────── */
#define DREAMION_MEMORY_RING_SIZE   64   /* recent inference sessions recorded */
#define DREAMION_SYNTH_BUF_SIZE     128  /* max tokens in a synthetic sequence */
#define DREAMION_PRED_RING_SIZE     8    /* predicted prompt slots */
#define DREAMION_JSONL_LINE_MAX     512  /* max bytes per JSONL line */
#define DREAMION_JSONL_BUF_LINES    32   /* lines buffered before flush */

/* ── Thresholds for auto mode transitions ───────────────────────── */
#define DREAMION_IDLE_LIGHT_THRESH  500U   /* ticks → enter LIGHT */
#define DREAMION_IDLE_DEEP_THRESH   2000U  /* ticks → enter DEEP  */
#define DREAMION_WAKE_THRESH        10U    /* ticks with input → wake */

/* ── Enumerations ───────────────────────────────────────────────── */
typedef enum {
    DREAMION_MODE_OFF   = 0,
    DREAMION_MODE_LIGHT = 1,
    DREAMION_MODE_DEEP  = 2,
} DreamionMode;

typedef enum {
    DREAMION_TASK_NONE        = 0,
    DREAMION_TASK_DEDUP       = 1,  /* KV cache dedup scan */
    DREAMION_TASK_COMPACT     = 2,  /* journal entry compaction */
    DREAMION_TASK_PREDICT     = 3,  /* prompt pre-warming */
    DREAMION_TASK_SYNTH       = 4,  /* synthetic self-play generation */
    DREAMION_TASK_DNA_MUTATE  = 5,  /* DNA bias adjustment */
    DREAMION_TASK_CONSOLIDATE = 6,  /* long-term memory merge */
    DREAMION_TASK_FLUSH_JSONL = 7,  /* flush training data to file */
} DreamionTaskType;

/* One recorded inference session (written by dreamion_record_inference) */
typedef struct {
    uint16_t prompt_tokens[32];   /* first 32 tokens of the prompt */
    uint16_t output_tokens[32];   /* first 32 tokens of the output */
    uint8_t  prompt_len;          /* actual prompt token count (capped 32) */
    uint8_t  output_len;          /* actual output token count (capped 32) */
    float    halt_prob;           /* final halt_prob at end of generation */
    float    confidence;          /* soma routing confidence */
    uint8_t  domain;              /* SomaDomain value (0..5) */
    uint8_t  used_solar;          /* 1 if Solar core was used */
    uint8_t  valid;               /* 0 = empty slot */
    uint8_t  _pad;
} DreamionMemory;

/* One predicted "likely next prompt" */
typedef struct {
    uint16_t tokens[16];
    uint8_t  len;
    float    confidence;
} DreamionPrediction;

/* Accumulated dedup result from one LIGHT cycle */
typedef struct {
    uint32_t pairs_scanned;
    uint32_t pairs_deduped;
    uint32_t tokens_freed;
} DreamionDedupResult;

/* Synthetic training pair produced during DEEP mode */
typedef struct {
    uint16_t input_tokens[DREAMION_SYNTH_BUF_SIZE / 2];
    uint16_t output_tokens[DREAMION_SYNTH_BUF_SIZE / 2];
    uint8_t  input_len;
    uint8_t  output_len;
    float    quality;  /* 0.0-1.0: estimated pair quality */
} DreamionSynthPair;

/* JSONL flush buffer */
typedef struct {
    char     lines[DREAMION_JSONL_BUF_LINES][DREAMION_JSONL_LINE_MAX];
    uint32_t line_count;
    uint32_t total_flushed;
} DreamionJsonlBuf;

/* Statistics */
typedef struct {
    uint32_t total_dream_cycles;
    uint32_t light_cycles;
    uint32_t deep_cycles;
    uint32_t dedup_pairs;
    uint32_t compact_entries;
    uint32_t synth_pairs_generated;
    uint32_t dna_mutations_suggested;
    uint32_t jsonl_lines_flushed;
    uint32_t wakes;
    float    last_dna_bias_delta;
    float    last_dna_temp_delta;
} DreamionStats;

/* Main engine struct */
typedef struct {
    /* Mode */
    DreamionMode  mode;
    uint32_t      idle_ticks;       /* incremented by dreamion_tick() */
    uint32_t      active_ticks;     /* input ticks (for wake detection) */
    int           awake;            /* 1 = fully awake, 0 = dreaming */

    /* Task scheduling */
    DreamionTaskType  current_task;
    uint32_t          task_phase;   /* sub-step within current task */

    /* Memory ring (recent inference sessions) */
    DreamionMemory    memory[DREAMION_MEMORY_RING_SIZE];
    uint32_t          memory_head;  /* write pointer */
    uint32_t          memory_count; /* how many valid entries */

    /* Predictions */
    DreamionPrediction predictions[DREAMION_PRED_RING_SIZE];
    uint32_t           pred_count;

    /* DEEP mode output */
    DreamionSynthPair  last_synth;   /* most recent synthetic pair */
    DreamionDedupResult last_dedup;

    /* DNA mutation hints (applied by caller to SomaDNA) */
    float  pending_bias_delta;   /* set by TASK_DNA_MUTATE */
    float  pending_temp_delta;
    int    pending_dna_ready;    /* 1 = caller should apply these */

    /* JSONL buffer */
    DreamionJsonlBuf  jsonl;

    /* Stats */
    DreamionStats     stats;

    /* Config overrides */
    uint32_t  idle_light_thresh;  /* default: DREAMION_IDLE_LIGHT_THRESH */
    uint32_t  idle_deep_thresh;   /* default: DREAMION_IDLE_DEEP_THRESH  */
} DreamionEngine;

/* ── Public API ─────────────────────────────────────────────────── */

/* Lifecycle */
void dreamion_init(DreamionEngine *e);
void dreamion_set_mode(DreamionEngine *e, DreamionMode mode);
void dreamion_wake(DreamionEngine *e);

/* Feed idle/active ticks — auto-transitions modes */
void dreamion_tick(DreamionEngine *e, uint32_t idle_cycles);
void dreamion_tick_active(DreamionEngine *e, uint32_t active_cycles);

/* Record an inference session for consolidation */
void dreamion_record_inference(DreamionEngine *e,
                                const uint16_t *prompt_toks, uint8_t prompt_len,
                                const uint16_t *output_toks, uint8_t output_len,
                                float halt_prob, float confidence,
                                uint8_t domain, uint8_t used_solar);

/* Step one dream task (call repeatedly in idle loop) */
DreamionTaskType dreamion_step(DreamionEngine *e);

/* Query */
DreamionTaskType dreamion_suggest_task(const DreamionEngine *e);
int              dreamion_has_dna_mutation(const DreamionEngine *e);
int              dreamion_pop_dna_mutation(DreamionEngine *e,
                                           float *out_bias, float *out_temp);

/* JSONL flush — writes buffered lines to *dst (EFI file handle via callback) */
typedef void (*DreamionFlushCb)(const char *line, uint32_t len, void *userdata);
uint32_t dreamion_flush_jsonl(DreamionEngine *e,
                               DreamionFlushCb cb, void *userdata);

/* Diagnostics */
void dreamion_print_status(const DreamionEngine *e,
                           void (*print_fn)(const char *));
const char *dreamion_mode_name_ascii(DreamionMode mode);
const char *dreamion_task_name_ascii(DreamionTaskType t);

#ifdef __cplusplus
}
#endif
