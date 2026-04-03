#ifndef LLMK_OO_INFER_H
#define LLMK_OO_INFER_H

// llmk_oo_infer.h — OO Inference Bridge: connects OosiGenCtx to LlmkOo entities
//
// This is the glue layer between:
//   - llmk_oo.c   (entity manager, goal/agenda system)
//   - oosi_infer.c (adaptive halting SSM inference engine)
//
// Design philosophy:
//   - No UEFI headers required (freestanding C11)
//   - No malloc, no libc
//   - One global OosiGenCtx (loaded at boot from OOSI v2 binary)
//   - The OO kernel calls llmk_oo_infer_think() on each entity step
//
// Memory layout (provided by boot allocator in cold/warm zone):
//   OosiGenCtx ctx
//   ssm_f32    x_buf      [d_model]            = 2560 * 4 = 10 KB
//   ssm_f32    x_out_buf  [d_model]            = 10 KB
//   ssm_f32    scratch    [8 * d_inner]         = 8*5120*4 = 160 KB
//   ssm_f32    logits     [vocab_size]          = 50282*4 ≈ 197 KB
//   ssm_f32    halt_buf   [d_model+1]           = ~10 KB
//   ssm_f32    halt_h1    [512]                 = 2 KB
//   ssm_f32    halt_h2    [64]                  = 0.25 KB
//   Total warm buffers: ~390 KB (fits in warm zone)

#include "oosi_infer.h"
#include "../engine/ssm/bpe_tokenizer.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Global inference context (singleton, initialized at boot)
// ============================================================
extern OosiGenCtx g_oosi_ctx;
extern int        g_oosi_ready;  // 1 if context initialized and ready

// ============================================================
// Inference result from one "think" call
// ============================================================
typedef struct {
    int   tokens_generated;  // how many tokens were produced
    int   loops_taken;       // reasoning loops before halt
    float final_halt_prob;   // P(halt) at last token
    int   halted_naturally;  // 1 = halted by HaltingHead, 0 = hit max_tokens
    char  text[512];         // decoded text (ASCII only, truncated to 511 chars)
} OoThinkResult;

// ============================================================
// Tokenizer initialization (separate from inference init)
//
// Call after llmk_oo_infer_init() to enable BPE tokenization.
// vocab_buf: caller-allocated BpeVocabEntry[vocab_buf_size] in cold zone
//            (Mamba-2.8B: vocab_buf_size=50280 → ~13 MB)
// tokenizer_bin: raw tokenizer.bin data loaded from EFI volume
// ============================================================
SsmStatus llmk_oo_infer_tokenizer_init(
    BpeVocabEntry *vocab_buf,
    int            vocab_buf_size,
    const void    *tokenizer_bin,
    uint64_t       tokenizer_bin_len
);

// ============================================================
// Boot-time initialization
//
// Call once after loading:
//   - MAMB weights (float32) into cold zone
//   - OOSI v2 weights (int8)  into cold zone
//   - Warm buffers allocated by boot memory manager
//
// Returns SSM_OK on success.
// ============================================================
SsmStatus llmk_oo_infer_init(
    const OosiWeights  *oosi,
    const MambaWeights *mamb,
    ssm_f32 *x_buf,         // [d_model]
    ssm_f32 *x_out_buf,     // [d_model]
    ssm_f32 *scratch,       // [8 * d_inner]
    ssm_f32 *logits,        // [vocab_size]
    ssm_f32 *halt_buf,      // [d_model + 1]
    ssm_f32 *halt_h1,       // [512]
    ssm_f32 *halt_h2,       // [64]
    float halt_threshold,   // 0.7 recommended
    float temperature,      // 0.8 recommended
    float top_p,            // 0.9 recommended
    uint32_t seed,
    int max_tokens          // 64 recommended for bare-metal
);

// ============================================================
// Think — run SSM inference for one OO entity reasoning step
//
// prompt_tokens: tokenized goal/context (caller must tokenize)
// prompt_len:    number of tokens in prompt
// out:           output structure (filled by this function)
//
// The function:
//   1. Feeds prompt tokens to build recurrent state
//   2. Generates tokens with adaptive halting
//   3. Decodes tokens to ASCII in out->text
//   4. Returns number of tokens generated
//
// Thread-safety: NOT thread-safe (single global context).
// ============================================================
int llmk_oo_infer_think(
    const int       *prompt_tokens,
    int              prompt_len,
    OoThinkResult   *out
);

// ============================================================
// Simple tokenizer bridge
//
// Maps an ASCII string to a sequence of token IDs using the
// BPE tokenizer loaded from the OOSI binary.
// Returns number of tokens written, or -1 on error.
// max_out: maximum tokens to write (to prevent overflow)
// ============================================================
int llmk_oo_infer_tokenize(
    const char *text,
    int        *out_tokens,
    int         max_out
);

// ============================================================
// Decode a single token ID to ASCII (bare-metal safe)
// Writes at most buf_cap-1 chars + null terminator.
// Returns chars written.
// ============================================================
int llmk_oo_infer_decode_token(
    int   token_id,
    char *buf,
    int   buf_cap
);

// ============================================================
// Status / diagnostics
// ============================================================
int llmk_oo_infer_is_ready(void);

typedef void (*LlmkOoInferPrintFn)(const char *msg);
void llmk_oo_infer_print_status(LlmkOoInferPrintFn fn);

#ifdef __cplusplus
}
#endif

#endif // LLMK_OO_INFER_H
