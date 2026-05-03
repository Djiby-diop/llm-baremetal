// oo_insitu_train.h — In-Situ Self-Training Engine
//
// OO trains itself internally, without leaving the bare-metal environment.
// No external OS. No reflash. No reboot.
//
// Architecture:
//   1. KV-Context Injection (RAG):
//      - NFS2 store is used as a retrievable knowledge base
//      - Before each inference, relevant NFS2 records are prepended to context
//      - "Training" = writing good examples to NFS2
//
//   2. LoRA Delta Accumulator (in-RAM weight tuning):
//      - Small rank-4 LoRA A/B matrix pair for the output projection
//      - Updated from synthetic training pairs (Dreamion JSONL)
//      - Applied at inference time: out = W*x + B*A*x
//      - Persisted via NFS2 key "oo.lora.delta_ab"
//
//   3. Autonomous Watchdog:
//      - Counts lines in DIOP_EXP.JSONL and OO_DREAM.JSONL
//      - Triggers a training cycle when threshold is reached
//      - Zero user interaction required
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Constants ─────────────────────────────────────────────────────────────

#define OIT_LORA_RANK         4       // LoRA rank
#define OIT_HIDDEN_MAX        512     // max hidden dim supported
#define OIT_TRAIN_BATCH       8       // pairs per training cycle
#define OIT_TRAIN_LR          0.001f  // learning rate
#define OIT_TRIGGER_LINES     20      // auto-trigger after N new JSONL lines
#define OIT_MAX_CTX_INJECT    3       // max NFS2 records to prepend as context
#define OIT_CTX_INJECT_MAXLEN 256     // max chars per injected context entry

// ── LoRA delta ────────────────────────────────────────────────────────────
//
// For hidden_dim D and rank R:
//   A: R x D  (down-projection, initialised ~N(0, 0.01))
//   B: D x R  (up-projection, initialised to 0)
//   delta(x) = B * A * x  (rank-R update, added to original output)

typedef struct {
    float A[OIT_LORA_RANK][OIT_HIDDEN_MAX];  // R x D
    float B[OIT_HIDDEN_MAX][OIT_LORA_RANK];  // D x R
    int   rank;
    int   hidden_dim;
    float scale;        // LoRA scale factor alpha/rank
    int   initialized;
} OitLoraState;

// ── Training pair ─────────────────────────────────────────────────────────
typedef struct {
    char input[256];
    char output[256];
    float quality;   // 0.0-1.0 from Dreamion synthetic generation
} OitPair;

// ── Watchdog state ────────────────────────────────────────────────────────
typedef struct {
    uint32_t lines_diop_last;   // last known line count in DIOP_EXP.JSONL
    uint32_t lines_dream_last;  // last known line count in OO_DREAM.JSONL
    uint32_t lines_diop_now;
    uint32_t lines_dream_now;
    uint32_t cycles_since_train;
    uint32_t total_train_cycles;
    uint32_t total_pairs_processed;
    int      training_active;   // 1 = currently in a training step
} OitWatchdog;

// ── Context injector ─────────────────────────────────────────────────────
typedef struct {
    char entries[OIT_MAX_CTX_INJECT][OIT_CTX_INJECT_MAXLEN];
    int  count;
} OitCtxInject;

// ── Main engine ───────────────────────────────────────────────────────────
typedef struct {
    OitLoraState lora;
    OitWatchdog  watchdog;
    OitCtxInject ctx_inject;
    int          enabled;
    int          rag_enabled;    // 1 = prepend NFS2 context before inference
    int          lora_enabled;   // 1 = apply LoRA delta at inference time
    int          verbose;
} OitEngine;

// ── Public API ────────────────────────────────────────────────────────────

void oit_init(OitEngine *e, int hidden_dim);

// Watchdog: call once per REPL iteration; returns 1 if training triggered
int  oit_watchdog_tick(OitEngine *e, void *root_dir);

// RAG: build context inject from NFS2 store, matching query keywords
// Must be called before inference; returns number of entries injected
int  oit_build_ctx_inject(OitEngine *e, const char *query,
                           const void *nfs2_store);   // Nfs2Store*

// RAG: format injected context as a prefix string
void oit_format_ctx_prefix(const OitEngine *e,
                            char *out, int cap);

// LoRA: apply delta to an output vector
// out_vec += LoRA_scale * B * A * in_vec
void oit_lora_apply(const OitEngine *e,
                    float *out_vec, const float *in_vec, int dim);

// Training: run one batch from ring of pairs
void oit_train_batch(OitEngine *e,
                     const OitPair *pairs, int count);

// Persistence: serialise LoRA delta to NFS2 (binary base64-ish encoding)
int  oit_lora_save(OitEngine *e, void *nfs2_store);  // Nfs2Store*
int  oit_lora_load(OitEngine *e, const void *nfs2_store);

// Manual trigger (called by /oo_train REPL command)
int  oit_train_from_jsonl(OitEngine *e, void *root_dir);

// Diagnostics
void oit_print_status(const OitEngine *e, void (*print_fn)(const char *));

#ifdef __cplusplus
}
#endif
