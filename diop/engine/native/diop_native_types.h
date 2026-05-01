#ifndef DIOP_NATIVE_TYPES_H
#define DIOP_NATIVE_TYPES_H

#include <stdint.h>

typedef struct {
    uint32_t version;
    uint64_t tensor_count;
    uint64_t kv_count;
    uint64_t file_size;
    uint64_t context_length;
    uint64_t embedding_length;
    uint64_t block_count;
    uint64_t head_count;
    uint64_t head_count_kv;
    uint64_t vocab_size;
    uint64_t file_type;
    char architecture[64];
    char name[96];
    char tokenizer_model[64];
} GgufSummary;

typedef struct {
    uint64_t data_start;
    uint64_t recognized_tensor_count;
    uint64_t max_tensor_offset;
    uint32_t token_embedding_present;
    uint32_t output_present;
    uint32_t rms_final_present;
    uint32_t attn_norm_count;
    uint32_t wq_count;
    uint32_t wk_count;
    uint32_t wv_count;
    uint32_t wo_count;
    uint32_t ffn_norm_count;
    uint32_t ffn_gate_count;
    uint32_t ffn_down_count;
    uint32_t ffn_up_count;
    uint32_t q8_0_count;
    uint32_t supported_type_count;
    uint32_t detected_layers;
    uint32_t fully_mapped_llama;
} TensorPlanSummary;

typedef struct {
    uint32_t hyperparams_complete;
    uint32_t shared_classifier;
    uint32_t float_layout_compatible;
    uint32_t q8_candidate;
    uint64_t dim;
    uint64_t hidden_dim;
    uint64_t n_layers;
    uint64_t n_heads;
    uint64_t n_kv_heads;
    uint64_t vocab_size;
    uint64_t seq_len;
    uint64_t kv_dim;
    uint64_t head_size;
    uint64_t float_weights_bytes;
    uint64_t float_kv_cache_bytes;
    uint64_t float_total_bytes;
} Llama2LayoutPlanSummary;

#endif
