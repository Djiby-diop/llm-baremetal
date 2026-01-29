#pragma once

#include <efi.h>
#include <efilib.h>
#include <stdint.h>

// Minimal GGUF inference loader.
//
// Two modes:
//  1) Float32 layout: dequantizes GGUF tensors into the existing llama2.c contiguous float layout.
//  2) Q8_0 blob: loads GGUF Q8_0 tensors without dequantizing, for true RAM savings.
//
// NOTE: This does NOT implement GGUF tokenizer support. It assumes a matching
// `tokenizer.bin` is present on the boot volume and that its vocab size matches
// the GGUF metadata `llama.vocab_size`.

typedef struct LlmkGgufPlan LlmkGgufPlan;

// Build a plan by parsing GGUF header/KV + tensor table.
// On success, outputs model hyperparameters and whether output.weight exists.
EFI_STATUS llmk_gguf_build_plan(
    EFI_FILE_HANDLE f,
    LlmkGgufPlan **out_plan,
    int *out_dim,
    int *out_hidden_dim,
    int *out_n_layers,
    int *out_n_heads,
    int *out_n_kv_heads,
    int *out_vocab_size,
    int *out_seq_len,
    int *out_has_output_weight
);

// Load tensors referenced by plan into the llama2.c weight layout buffer.
//
// Layout expected (per existing `.bin` mapping code):
// token_embedding_table | rms_att | wq | wk | wv | wo | rms_ffn | w1 | w2 | w3 | rms_final | freq_cis_real | freq_cis_imag | (optional) wcls
EFI_STATUS llmk_gguf_load_into_llama2_layout(
    EFI_FILE_HANDLE f,
    const LlmkGgufPlan *plan,
    float *weights_mem,
    int dim,
    int hidden_dim,
    int n_layers,
    int n_heads,
    int n_kv_heads,
    int vocab_size,
    int seq_len,
    int shared_classifier
);

// Returns 1 if the plan can be loaded into the Q8_0 blob layout (i.e., all required 2D tensors are Q8_0).
// Norm vectors may be F16/F32 and will be dequantized to float32.
int llmk_gguf_plan_supports_q8_0_blob(const LlmkGgufPlan *plan, int shared_classifier);

// Computes the required blob size (bytes) for the Q8_0 blob layout.
EFI_STATUS llmk_gguf_calc_llama2_q8_0_blob_bytes(
    const LlmkGgufPlan *plan,
    int dim,
    int hidden_dim,
    int n_layers,
    int n_heads,
    int n_kv_heads,
    int vocab_size,
    int seq_len,
    int shared_classifier,
    UINT64 *out_bytes
);

// Loads weights into the Q8_0 blob layout. The caller must allocate `blob_bytes` as returned by
// llmk_gguf_calc_llama2_q8_0_blob_bytes().
EFI_STATUS llmk_gguf_load_into_llama2_q8_0_blob(
    EFI_FILE_HANDLE f,
    const LlmkGgufPlan *plan,
    void *blob,
    UINT64 blob_bytes,
    int dim,
    int hidden_dim,
    int n_layers,
    int n_heads,
    int n_kv_heads,
    int vocab_size,
    int seq_len,
    int shared_classifier
);

void llmk_gguf_free_plan(LlmkGgufPlan *plan);
