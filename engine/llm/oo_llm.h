/* oo_llm.h — LLM Inference Engine Interface (Public Prototype)
 *
 * Transformer-based inference running on bare hardware.
 * Supports Llama-2 architecture, GGUF model format, BPE tokenizer.
 * Uses custom AVX2 BLAS kernels — no BLAS library, no OS.
 */
#ifndef OO_LLM_H
#define OO_LLM_H

#include <stdint.h>
#include <stddef.h>

#define OO_LLM_MAX_SEQ_LEN   2048
#define OO_LLM_MAX_VOCAB     32000
#define OO_LLM_MAX_LAYERS    32

typedef struct {
    uint32_t n_layers;
    uint32_t d_model;
    uint32_t n_heads;
    uint32_t n_kv_heads;
    uint32_t vocab_size;
    uint32_t max_seq_len;
    float    rope_theta;
} OoLlmConfig;

typedef struct {
    OoLlmConfig cfg;
    void       *weights;    /* Allocated in ZONE_MODEL */
    void       *kv_cache;   /* Allocated in ZONE_INFERENCE */
    int         loaded;
} OoLlmModel;

typedef struct {
    float temperature;
    float top_p;
    int   max_new_tokens;
    int   seed;
} OoLlmSampleParams;

/* Load GGUF model file from EFI filesystem into ZONE_MODEL */
int oo_llm_load(OoLlmModel *m, const char *gguf_path);

/* Run a single forward pass — returns next token id */
int oo_llm_forward(OoLlmModel *m, const int *tokens, int n_tokens);

/* Sample from logits using temperature + top-p */
int oo_llm_sample(OoLlmModel *m, const OoLlmSampleParams *p);

/* Generate up to max_new_tokens, calling cb(token) for each output */
void oo_llm_generate(OoLlmModel *m, const char *prompt,
                     const OoLlmSampleParams *p,
                     void (*cb)(int token, void *user), void *user);

/* Free model (reset ZONE_MODEL) */
void oo_llm_free(OoLlmModel *m);

#endif /* OO_LLM_H */
