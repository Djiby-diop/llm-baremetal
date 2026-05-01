#include "model_planning_support.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "layout_plan_support.h"
#include "legacy_gguf_fallback.h"
#include "../../../engine/gguf/gguf_portable.h"

typedef struct {
    FILE *file;
    uint64_t file_size;
} HostFileReader;

static void zero_summary(GgufSummary *summary) {
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
}

static void zero_tensor_plan(TensorPlanSummary *plan) {
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
}

static size_t host_reader_read_at(void *user_data, uint64_t offset, void *dst, size_t nbytes) {
    HostFileReader *reader = (HostFileReader *)user_data;
    if (!reader || !reader->file || (!dst && nbytes > 0)) return 0;
    if (_fseeki64(reader->file, (long long)offset, SEEK_SET) != 0) return 0;
    return fread(dst, 1, nbytes, reader->file);
}

static uint64_t host_reader_size(void *user_data) {
    HostFileReader *reader = (HostFileReader *)user_data;
    if (!reader) return 0;
    return reader->file_size;
}

static void copy_portable_summary(GgufSummary *dst, const LlmkPortableGgufSummary *src) {
    if (!dst || !src) return;
    zero_summary(dst);
    dst->version = src->version;
    dst->tensor_count = src->tensor_count;
    dst->kv_count = src->kv_count;
    dst->file_size = src->file_size;
    dst->context_length = src->context_length;
    dst->embedding_length = src->embedding_length;
    dst->block_count = src->block_count;
    dst->head_count = src->head_count;
    dst->head_count_kv = src->head_count_kv;
    dst->vocab_size = src->vocab_size;
    dst->file_type = src->file_type;
    strncpy(dst->architecture, src->architecture, sizeof(dst->architecture) - 1);
    strncpy(dst->name, src->name, sizeof(dst->name) - 1);
    strncpy(dst->tokenizer_model, src->tokenizer_model, sizeof(dst->tokenizer_model) - 1);
}

static void copy_portable_tensor_plan(TensorPlanSummary *dst, const LlmkPortableTensorPlan *src) {
    if (!dst || !src) return;
    zero_tensor_plan(dst);
    dst->recognized_tensor_count = src->recognized_tensor_count;
    dst->token_embedding_present = src->token_embedding_present;
    dst->output_present = src->output_present;
    dst->rms_final_present = src->rms_final_present;
    dst->attn_norm_count = src->attn_norm_count;
    dst->wq_count = src->wq_count;
    dst->wk_count = src->wk_count;
    dst->wv_count = src->wv_count;
    dst->wo_count = src->wo_count;
    dst->ffn_norm_count = src->ffn_norm_count;
    dst->ffn_gate_count = src->ffn_gate_count;
    dst->ffn_down_count = src->ffn_down_count;
    dst->ffn_up_count = src->ffn_up_count;
    dst->q8_0_count = src->q8_0_count;
    dst->supported_type_count = src->supported_type_count;
    dst->detected_layers = src->detected_layers;
    dst->fully_mapped_llama = src->fully_mapped_llama;
}

static void copy_portable_runtime_plan(Llama2LayoutPlanSummary *dst, const LlmkPortableRuntimePlan *src) {
    if (!dst || !src) return;
    zero_layout_plan(dst);
    dst->hyperparams_complete = src->hyperparams_complete;
    dst->shared_classifier = src->shared_classifier;
    dst->float_layout_compatible = src->float_layout_compatible;
    dst->q8_candidate = src->q8_candidate;
    dst->dim = src->dim;
    dst->hidden_dim = src->hidden_dim;
    dst->n_layers = src->n_layers;
    dst->n_heads = src->n_heads;
    dst->n_kv_heads = src->n_kv_heads;
    dst->vocab_size = src->vocab_size;
    dst->seq_len = src->seq_len;
    dst->kv_dim = src->kv_dim;
    dst->head_size = src->head_size;
    dst->float_weights_bytes = src->float_weights_bytes;
    dst->float_kv_cache_bytes = src->float_kv_cache_bytes;
    dst->float_total_bytes = src->float_total_bytes;
}

static int try_read_with_portable_core(
    const char *model_path,
    GgufSummary *summary,
    TensorPlanSummary *tensor_plan,
    Llama2LayoutPlanSummary *layout_plan,
    char *error_message,
    size_t error_capacity
) {
    FILE *file = NULL;
    HostFileReader host_reader;
    LlmkPortableReader reader;
    LlmkPortableGgufSummary portable_summary;
    LlmkPortableTensorPlan portable_tensor_plan;
    LlmkPortableRuntimePlan portable_runtime_plan;
    LlmkPortableStatus status;

    if (!model_path || !summary || !tensor_plan || !layout_plan) return 0;

    file = fopen(model_path, "rb");
    if (!file) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Unable to open model file");
        }
        return 0;
    }
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    host_reader.file = file;
    host_reader.file_size = (uint64_t)_ftelli64(file);

    reader.user_data = &host_reader;
    reader.read_at = host_reader_read_at;
    reader.size = host_reader_size;

    memset(&portable_summary, 0, sizeof(portable_summary));
    memset(&portable_tensor_plan, 0, sizeof(portable_tensor_plan));
    memset(&portable_runtime_plan, 0, sizeof(portable_runtime_plan));

    status = llmk_portable_gguf_build_runtime_plan(
        &reader,
        &portable_summary,
        &portable_tensor_plan,
        &portable_runtime_plan
    );
    fclose(file);

    if (status != LLMK_PORTABLE_OK) {
        if (error_message && error_capacity > 0) {
            snprintf(error_message, error_capacity, "Portable GGUF core returned status %d", (int)status);
        }
        return 0;
    }

    copy_portable_summary(summary, &portable_summary);
    copy_portable_tensor_plan(tensor_plan, &portable_tensor_plan);
    copy_portable_runtime_plan(layout_plan, &portable_runtime_plan);
    return 1;
}

int load_model_plans(
    const char *model_path,
    GgufSummary *summary,
    TensorPlanSummary *tensor_plan,
    Llama2LayoutPlanSummary *layout_plan,
    char *gguf_core_source,
    size_t gguf_core_source_capacity,
    char *error_message,
    size_t error_capacity
) {
    if (!model_path || !summary || !tensor_plan || !layout_plan) return 0;
    if (gguf_core_source && gguf_core_source_capacity > 0) {
        snprintf(gguf_core_source, gguf_core_source_capacity, "%s", "uninitialized");
    }

    if (try_read_with_portable_core(model_path, summary, tensor_plan, layout_plan, error_message, error_capacity)) {
        if (gguf_core_source && gguf_core_source_capacity > 0) {
            snprintf(gguf_core_source, gguf_core_source_capacity, "%s", "portable_core");
        }
        return 1;
    }

    if (!read_summary_from_legacy_gguf(model_path, summary, tensor_plan, error_message, error_capacity)) {
        return 0;
    }

    if (gguf_core_source && gguf_core_source_capacity > 0) {
        snprintf(gguf_core_source, gguf_core_source_capacity, "%s", "legacy_fallback");
    }
    finalize_layout_plan(layout_plan, summary, tensor_plan);
    return 1;
}
