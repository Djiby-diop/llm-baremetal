/* oo_somamind_v1.h — SomaMind V1: compact SSM + adaptive halting + tool-use
 * Freestanding C11. No libc, no malloc.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SOMAMIND_HIDDEN_DIM   64    /* compact hidden state dimension          */
#define SOMAMIND_MAX_TOKENS   512   /* max tokens in one inference pass        */
#define SOMAMIND_HALT_THRESH  220   /* halt when conf8 >= this (0-255 scale)   */
#define SOMAMIND_TOOL_MAX_LEN 128   /* max tool name + args length             */
#define SOMAMIND_N_TOOLS      8     /* max registered tools                    */

/* Tool-use result codes */
#define SM_TOOL_NONE     0
#define SM_TOOL_FOUND    1
#define SM_TOOL_EXEC     2
#define SM_TOOL_UNKNOWN  3

/* Adaptive halting result */
typedef enum {
    SM_HALT_CONTINUE = 0,  /* keep generating                              */
    SM_HALT_CONFIDENT,     /* top logit confidence above threshold → stop  */
    SM_HALT_BUDGET,        /* token budget exhausted                        */
    SM_HALT_TOOL,          /* tool call detected → stop + dispatch          */
} SmHaltReason;

/* Compact SSM hidden state (Mamba-style diagonal linear RNN)
 *   h[t] = A * h[t-1] + B * x[t] + bias
 *   y[t] = C * h[t]  (then fast-tanh activation)
 * A is diagonal (stored as vector), B and C are rank-1.
 */
typedef struct {
    float h[SOMAMIND_HIDDEN_DIM];   /* current hidden state                  */
    float A[SOMAMIND_HIDDEN_DIM];   /* diagonal state transition (decay)     */
    float B[SOMAMIND_HIDDEN_DIM];   /* input projection                      */
    float C[SOMAMIND_HIDDEN_DIM];   /* output projection                     */
    float bias[SOMAMIND_HIDDEN_DIM];
    uint32_t step;                  /* total steps since last reset          */
    float    last_output_norm;      /* ||y[t]||^2 — used by halting logic    */
} SmSsmState;

/* Tool registry entry */
typedef struct {
    char  name[32];                                          /* tool name     */
    int (*fn)(const char *args, char *out, int out_cap);     /* handler       */
} SmTool;

/* Adaptive halting state */
typedef struct {
    float    confidence_ema;     /* EMA of per-token confidence             */
    float    alpha;              /* EMA decay (default 0.3)                 */
    uint32_t tokens_generated;
    uint32_t budget;             /* max tokens before forced halt           */
    SmHaltReason last_halt;
} SmHaltState;

/* Tool-use parser state
 * tag_accum/tag_accum_len: rolling 15-byte buffer for cross-token tag detection.
 * name_len/args_len: byte lengths of accumulated name and args content.
 */
typedef struct {
    char    pending_name[64];
    char    pending_args[SOMAMIND_TOOL_MAX_LEN];
    int     in_tool_tag;
    int     in_args_tag;
    int     found;
    SmTool  tools[SOMAMIND_N_TOOLS];
    int     n_tools;
    /* implementation details: rolling tag accumulator for cross-token detection */
    char    tag_accum[16];
    int     tag_accum_len;
    int     name_len;
    int     args_len;
} SmToolState;

/* Top-level SomaMind V1 engine */
typedef struct {
    SmSsmState  ssm;
    SmHaltState halt;
    SmToolState tools;
    int         initialized;
    uint64_t    total_halts_confident;
    uint64_t    total_halts_tool;
    uint64_t    total_tokens_saved;  /* tokens NOT generated due to early halt */
} SomaMindV1;

/* ── API ──────────────────────────────────────────────────────────────────── */

/* Initialize SomaMind V1 (call once at boot, Phase SM).
 * A → 0.9, B/C → small random values ~0.1 (LCG seed 0xC0FFEE), bias → 0. */
void sm_init(SomaMindV1 *sm, uint32_t token_budget);

/* Register a tool handler.  Returns 0 on success, -1 if table full. */
int  sm_register_tool(SomaMindV1 *sm, const char *name,
                       int (*fn)(const char *args, char *out, int cap));

/* Tick: called once per token during inference.
 * logits:     raw logit vector (vocab_size floats)
 * token_id:   sampled token index
 * token_str:  decoded string for that token (for tool tag parsing)
 * Returns SM_HALT_CONTINUE or a halt reason. */
SmHaltReason sm_tick(SomaMindV1 *sm, const float *logits, int vocab_size,
                     int token_id, const char *token_str);

/* After sm_tick returns SM_HALT_TOOL: execute the detected tool.
 * Writes result to out_buf (max out_cap bytes).
 * Returns SM_TOOL_EXEC on success, SM_TOOL_UNKNOWN if tool not found. */
int sm_exec_tool(SomaMindV1 *sm, char *out_buf, int out_cap);

/* Update SSM hidden state (dim must be SOMAMIND_HIDDEN_DIM).
 * Called internally by sm_tick; also callable for RAG injection.
 * y_out may be NULL. */
void sm_ssm_step(SmSsmState *s, const float *x, float *y_out);

/* Reset hidden state (e.g. start of new conversation). */
void sm_ssm_reset(SmSsmState *s);

/* Print engine status via callback (freestanding — no printf). */
void sm_print_status(const SomaMindV1 *sm, void (*print_fn)(const char *));
