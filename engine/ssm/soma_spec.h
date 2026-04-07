// soma_spec.h — SomaMind Speculative Decoding Engine
//
// Strategy: Solar core drafts SPEC_DRAFT_N tokens (greedy argmax, temp=0)
// from the current logit buffer.  Main model (Mamba-2.8B) then verifies
// each draft token in one forward pass per token.
//
// Acceptance rule (token-level):
//   p_draft(t) = softmax(logits_draft)[t]
//   p_verify(t)= softmax(logits_verify)[t]
//   r ~ Uniform(0,1)
//   if r < min(1, p_verify(t)/p_draft(t)) → accept
//   else → reject, sample from corrected distribution
//
// Net effect: when Solar draft is confident and matches Mamba, multiple
// tokens are committed per Mamba forward pass → speedup.
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "ssm_types.h"   // ssm_f32, uint32_t
#include "soma_dna.h"    // SomaDNA (temperature thresholds)

#ifdef __cplusplus
extern "C" {
#endif

#define SPEC_DRAFT_N      8     // Max draft tokens per cycle
#define SPEC_VOCAB_MAX    50282 // Upper bound; actual set at init

// ─────────────────────────────────────────────────────────────
// Per-cycle speculative result
// ─────────────────────────────────────────────────────────────
typedef struct {
    int      draft_tokens[SPEC_DRAFT_N];   // Solar-drafted token IDs
    float    draft_probs[SPEC_DRAFT_N];    // p_draft for each
    int      accepted_tokens[SPEC_DRAFT_N];// Tokens that passed verify
    int      n_draft;                      // Tokens drafted this cycle
    int      n_accepted;                   // Tokens accepted after verify
    int      first_rejection;             // Index of first rejected token (-1=all accepted)
    float    speedup_ratio;               // n_accepted / n_draft (0-1)
} SomaSpecResult;

// ─────────────────────────────────────────────────────────────
// Session stats
// ─────────────────────────────────────────────────────────────
typedef struct {
    int     total_cycles;
    int     total_drafted;
    int     total_accepted;
    int     total_rejected;
    int     full_accepts;    // Cycles where all N drafts accepted
    float   avg_speedup;     // Running average of speedup_ratio
    float   avg_speedup_acc;
} SomaSpecStats;

// ─────────────────────────────────────────────────────────────
// Speculative context
// ─────────────────────────────────────────────────────────────
typedef struct {
    SomaSpecStats stats;
    int     enabled;
    int     vocab_size;
    float   accept_threshold;  // Min p_verify/p_draft ratio to auto-accept (default 0.8)
    ssm_f32 *work_buf;         // Scratch [vocab_size] — caller provides
} SomaSpecCtx;

// ─────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────

void soma_spec_init(SomaSpecCtx *ctx, ssm_f32 *work_buf, int vocab_size);

// Draft N tokens greedily (argmax, temp=0) from current raw logits.
// Returns count of tokens drafted (≤ SPEC_DRAFT_N).
int soma_spec_draft(SomaSpecCtx *ctx,
                    const ssm_f32 *raw_logits,
                    int n_draft,
                    int *out_tokens,
                    float *out_probs);

// Verify one draft token against verifier logits.
// Returns 1 (accept) or 0 (reject).
// On reject, fills *corrected_token with sample from corrected dist.
int soma_spec_verify_one(SomaSpecCtx *ctx,
                         const ssm_f32 *verify_logits,
                         int draft_token,
                         float draft_prob,
                         uint32_t *rng,
                         int *corrected_token);

// Full cycle: draft then verify in-place (single verifier logit snapshot).
// verify_logits = logits from main model after feeding the prompt token.
// Returns SomaSpecResult with accepted sequence.
SomaSpecResult soma_spec_cycle(SomaSpecCtx *ctx,
                               const ssm_f32 *draft_logits,
                               const ssm_f32 *verify_logits,
                               int n_draft,
                               uint32_t *rng);

// Print stats
typedef void (*SomaSpecPrintFn)(const char *msg);
void soma_spec_print_stats(const SomaSpecCtx *ctx, SomaSpecPrintFn fn);

#ifdef __cplusplus
}
#endif
