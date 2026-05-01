#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diop_engine.h"
#include "diop_native_types.h"
#include "layout_plan_support.h"
#include "model_planning_support.h"
#include "legacy_gguf_fallback.h"
#include "runtime_transition_support.h"

typedef struct {
    char model_path[512];
    int is_loaded;
    char gguf_core_source[32];
    GgufSummary summary;
    TensorPlanSummary tensor_plan;
    Llama2LayoutPlanSummary layout_plan;
    RuntimePreparationState runtime_state;
    RuntimeContext runtime_context;
    char response_buffer[8192];
    char error_message[256];
} EngineContext;

static void zero_summary(GgufSummary *summary);
static void zero_tensor_plan(TensorPlanSummary *plan);
static void zero_layout_plan(Llama2LayoutPlanSummary *plan);

static void zero_summary(GgufSummary *summary) {
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
}
static void zero_tensor_plan(TensorPlanSummary *plan) {
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
}

static void escape_json_string(const char *src, char *dst, size_t capacity) {
    size_t i = 0;
    size_t pos = 0;
    if (!dst || capacity == 0) return;
    dst[0] = 0;
    if (!src) return;

    while (src[i] && pos + 2 < capacity) {
        char c = src[i++];
        if (c == '\\' || c == '"') {
            if (pos + 2 >= capacity) break;
            dst[pos++] = '\\';
            dst[pos++] = c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            if (pos + 2 >= capacity) break;
            dst[pos++] = '\\';
            dst[pos++] = (c == '\n') ? 'n' : (c == '\r') ? 'r' : 't';
        } else if ((unsigned char)c < 0x20) {
            dst[pos++] = ' ';
        } else {
            dst[pos++] = c;
        }
    }
    dst[pos] = 0;
}

static const char *engine_not_initialized_response(void) {
    return "{\"summary\":\"Native engine not initialized\",\"artifacts\":[],\"risks\":[\"engine_not_initialized\"],\"recommendations\":[\"Call diop_engine_init with a valid GGUF path first.\"]}";
}

static const char *build_stage_payload(
    EngineContext *ctx,
    const char *summary_text,
    const char *artifacts_json,
    const char *risks_json,
    const char *recommendations_json
) {
    if (!ctx || !ctx->is_loaded) {
        return engine_not_initialized_response();
    }

    snprintf(
        ctx->response_buffer,
        sizeof(ctx->response_buffer),
        "{"
        "\"summary\":\"%s\","
        "\"artifacts\":[%s],"
        "\"risks\":[%s],"
        "\"recommendations\":[%s]"
        "}",
        summary_text,
        artifacts_json,
        risks_json,
        recommendations_json
    );
    return ctx->response_buffer;
}

static const char *build_inspect_payload(EngineContext *ctx) {
    char safe_model[256];
    char safe_arch[96];
    char safe_name[128];
    char safe_tokenizer[96];

    escape_json_string(ctx->model_path, safe_model, sizeof(safe_model));
    escape_json_string(ctx->summary.architecture, safe_arch, sizeof(safe_arch));
    escape_json_string(ctx->summary.name, safe_name, sizeof(safe_name));
    escape_json_string(ctx->summary.tokenizer_model, safe_tokenizer, sizeof(safe_tokenizer));

    snprintf(
        ctx->response_buffer,
        sizeof(ctx->response_buffer),
        "{"
        "\"summary\":\"Native engine inspected GGUF metadata and identified the model envelope.\","
        "\"artifacts\":["
        "{"
        "\"name\":\"gguf_model_summary\","
        "\"type\":\"model_metadata\","
        "\"content\":{"
        "\"model_path\":\"%s\","
        "\"gguf_core_source\":\"%s\","
        "\"architecture\":\"%s\","
        "\"name\":\"%s\","
        "\"tokenizer_model\":\"%s\","
        "\"version\":%u,"
        "\"tensor_count\":%llu,"
        "\"kv_count\":%llu,"
        "\"file_size\":%llu,"
        "\"context_length\":%llu,"
        "\"embedding_length\":%llu,"
        "\"block_count\":%llu,"
        "\"head_count\":%llu,"
        "\"head_count_kv\":%llu,"
        "\"vocab_size\":%llu,"
        "\"file_type\":%llu"
        "}"
        "}"
        "],"
        "\"risks\":[\"metadata inspection only\"],"
        "\"recommendations\":["
        "\"Build a tensor role map before attempting weight placement.\","
        "\"Use plan_load to verify the model matches the transitional llama-compatible layout.\""
        "]"
        "}",
        safe_model,
        ctx->gguf_core_source,
        safe_arch[0] ? safe_arch : "unknown",
        safe_name[0] ? safe_name : "unknown",
        safe_tokenizer[0] ? safe_tokenizer : "unknown",
        ctx->summary.version,
        (unsigned long long)ctx->summary.tensor_count,
        (unsigned long long)ctx->summary.kv_count,
        (unsigned long long)ctx->summary.file_size,
        (unsigned long long)ctx->summary.context_length,
        (unsigned long long)ctx->summary.embedding_length,
        (unsigned long long)ctx->summary.block_count,
        (unsigned long long)ctx->summary.head_count,
        (unsigned long long)ctx->summary.head_count_kv,
        (unsigned long long)ctx->summary.vocab_size,
        (unsigned long long)ctx->summary.file_type
    );
    return ctx->response_buffer;
}

static const char *build_plan_payload(EngineContext *ctx) {
    snprintf(
        ctx->response_buffer,
        sizeof(ctx->response_buffer),
        "{"
        "\"summary\":\"Native engine built a tensor routing plan and estimated the transitional runtime layout.\","
        "\"artifacts\":["
        "{"
        "\"name\":\"gguf_tensor_plan\","
        "\"type\":\"tensor_plan\","
        "\"content\":{"
        "\"data_start\":%llu,"
        "\"recognized_tensor_count\":%llu,"
        "\"max_tensor_offset\":%llu,"
        "\"token_embedding_present\":%u,"
        "\"output_present\":%u,"
        "\"rms_final_present\":%u,"
        "\"attn_norm_count\":%u,"
        "\"wq_count\":%u,"
        "\"wk_count\":%u,"
        "\"wv_count\":%u,"
        "\"wo_count\":%u,"
        "\"ffn_norm_count\":%u,"
        "\"ffn_gate_count\":%u,"
        "\"ffn_down_count\":%u,"
        "\"ffn_up_count\":%u,"
        "\"q8_0_count\":%u,"
        "\"supported_type_count\":%u,"
        "\"detected_layers\":%u,"
        "\"fully_mapped_llama\":%u"
        "}"
        "},"
        "{"
        "\"name\":\"llama2_layout_plan\","
        "\"type\":\"layout_plan\","
        "\"content\":{"
        "\"hyperparams_complete\":%u,"
        "\"shared_classifier\":%u,"
        "\"float_layout_compatible\":%u,"
        "\"q8_candidate\":%u,"
        "\"dim\":%llu,"
        "\"hidden_dim\":%llu,"
        "\"n_layers\":%llu,"
        "\"n_heads\":%llu,"
        "\"n_kv_heads\":%llu,"
        "\"vocab_size\":%llu,"
        "\"seq_len\":%llu,"
        "\"kv_dim\":%llu,"
        "\"head_size\":%llu,"
        "\"float_weights_bytes\":%llu,"
        "\"float_kv_cache_bytes\":%llu,"
        "\"float_total_bytes\":%llu"
        "}"
        "}"
        "],"
        "\"risks\":["
        "\"weight placement is still planned, not executed\","
        "\"the final OO-native runtime has not replaced the transitional llama layout yet\""
        "],"
        "\"recommendations\":["
        "\"Prepare a runtime context only after validating tensor completeness.\","
        "\"Replace this transitional layout progressively with the OO-native memory map.\""
        "]"
        "}",
        (unsigned long long)ctx->tensor_plan.data_start,
        (unsigned long long)ctx->tensor_plan.recognized_tensor_count,
        (unsigned long long)ctx->tensor_plan.max_tensor_offset,
        ctx->tensor_plan.token_embedding_present,
        ctx->tensor_plan.output_present,
        ctx->tensor_plan.rms_final_present,
        ctx->tensor_plan.attn_norm_count,
        ctx->tensor_plan.wq_count,
        ctx->tensor_plan.wk_count,
        ctx->tensor_plan.wv_count,
        ctx->tensor_plan.wo_count,
        ctx->tensor_plan.ffn_norm_count,
        ctx->tensor_plan.ffn_gate_count,
        ctx->tensor_plan.ffn_down_count,
        ctx->tensor_plan.ffn_up_count,
        ctx->tensor_plan.q8_0_count,
        ctx->tensor_plan.supported_type_count,
        ctx->tensor_plan.detected_layers,
        ctx->tensor_plan.fully_mapped_llama,
        ctx->layout_plan.hyperparams_complete,
        ctx->layout_plan.shared_classifier,
        ctx->layout_plan.float_layout_compatible,
        ctx->layout_plan.q8_candidate,
        (unsigned long long)ctx->layout_plan.dim,
        (unsigned long long)ctx->layout_plan.hidden_dim,
        (unsigned long long)ctx->layout_plan.n_layers,
        (unsigned long long)ctx->layout_plan.n_heads,
        (unsigned long long)ctx->layout_plan.n_kv_heads,
        (unsigned long long)ctx->layout_plan.vocab_size,
        (unsigned long long)ctx->layout_plan.seq_len,
        (unsigned long long)ctx->layout_plan.kv_dim,
        (unsigned long long)ctx->layout_plan.head_size,
        (unsigned long long)ctx->layout_plan.float_weights_bytes,
        (unsigned long long)ctx->layout_plan.float_kv_cache_bytes,
        (unsigned long long)ctx->layout_plan.float_total_bytes
    );
    return ctx->response_buffer;
}

static const char *build_prepare_payload(EngineContext *ctx) {
    if (!ctx->runtime_state.is_prepared) {
        prepare_runtime_state(&ctx->layout_plan, &ctx->runtime_state);
    }
    ensure_runtime_context(&ctx->runtime_state, &ctx->runtime_context);

    snprintf(
        ctx->response_buffer,
        sizeof(ctx->response_buffer),
        "{"
        "\"summary\":\"Native engine prepared the next runtime stage and classified backend readiness.\","
        "\"artifacts\":["
        "{"
        "\"name\":\"runtime_readiness\","
        "\"type\":\"runtime_plan\","
        "\"content\":{"
        "\"ready_for_transition_runtime\":%u,"
        "\"ready_for_float_runtime\":%u,"
        "\"ready_for_q8_runtime\":%u,"
        "\"fully_mapped_llama\":%u,"
        "\"hyperparams_complete\":%u,"
        "\"float_layout_compatible\":%u,"
        "\"q8_candidate\":%u,"
        "\"estimated_runtime_bytes\":%llu,"
        "\"gguf_core_source\":\"%s\","
        "\"next_backend\":\"%s\","
        "\"prepared\":%u,"
        "\"preparation_reason\":\"%s\","
        "\"runtime_context_allocated\":%u,"
        "\"runtime_status\":\"%s\","
        "\"executor_name\":\"%s\","
        "\"bridge_status\":\"%s\""
        "}"
        "},"
        "{"
        "\"name\":\"portable_core_contract\","
        "\"type\":\"extraction_plan\","
        "\"content\":{"
        "\"target_header\":\"engine/gguf/gguf_portable.h\","
        "\"target_stub\":\"engine/gguf/gguf_portable_stub.c\","
        "\"goal\":\"extract GGUF summary and runtime planning into a host/UEFI/bare-metal neutral core\","
        "\"next_phase\":\"migrate logic from gguf_loader.c and gguf_infer.c into this portable contract\""
        "}"
        "}"
        "],"
        "\"risks\":["
        "\"runtime preparation currently stops before actual tensor allocation\","
        "\"backend selection is still transitional until the OO-native runtime replaces llama-compatible execution\""
        "],"
        "\"recommendations\":["
        "\"Introduce a persistent runtime context separate from inspection metadata.\","
        "\"Route generate_step through the future OO-native backend once the runtime allocator exists.\""
        "]"
        "}",
        ctx->runtime_state.ready_for_transition_runtime,
        ctx->runtime_state.ready_for_float_runtime,
        ctx->runtime_state.ready_for_q8_runtime,
        ctx->tensor_plan.fully_mapped_llama,
        ctx->layout_plan.hyperparams_complete,
        ctx->layout_plan.float_layout_compatible,
        ctx->layout_plan.q8_candidate,
        (unsigned long long)ctx->runtime_state.reserved_runtime_bytes,
        ctx->gguf_core_source,
        ctx->runtime_state.backend_name,
        ctx->runtime_state.is_prepared,
        ctx->runtime_state.reason,
        ctx->runtime_context.is_allocated,
        ctx->runtime_context.status,
        ctx->runtime_context.executor_name,
        ctx->runtime_context.bridge_status
    );
    return ctx->response_buffer;
}

DIOP_EXPORT DiopEngineState diop_engine_init(const char *model_path) {
    EngineContext *ctx = NULL;
    printf("[SOMA C-ENGINE] Initializing native GGUF bridge...\n");

    if (!model_path) return NULL;

    ctx = (EngineContext *)calloc(1, sizeof(EngineContext));
    if (!ctx) return NULL;

    strncpy(ctx->model_path, model_path, sizeof(ctx->model_path) - 1);
    ctx->model_path[sizeof(ctx->model_path) - 1] = 0;
    strncpy(ctx->gguf_core_source, "uninitialized", sizeof(ctx->gguf_core_source) - 1);

    printf("[SOMA C-ENGINE] Inspecting model file: %s\n", ctx->model_path);
    if (!load_model_plans(
            ctx->model_path,
            &ctx->summary,
            &ctx->tensor_plan,
            &ctx->layout_plan,
            ctx->gguf_core_source,
            sizeof(ctx->gguf_core_source),
            ctx->error_message,
            sizeof(ctx->error_message))) {
        printf("[SOMA C-ENGINE] Model planning failed: %s\n", ctx->error_message[0] ? ctx->error_message : "unknown error");
        free(ctx);
        return NULL;
    }
    zero_runtime_state(&ctx->runtime_state);
    zero_runtime_context(&ctx->runtime_context);

    ctx->is_loaded = 1;
    printf("[SOMA C-ENGINE] GGUF summary loaded: arch=%s layers=%llu dim=%llu\n",
           ctx->summary.architecture[0] ? ctx->summary.architecture : "unknown",
           (unsigned long long)ctx->summary.block_count,
           (unsigned long long)ctx->summary.embedding_length);
    return (DiopEngineState)ctx;
}

DIOP_EXPORT const char *diop_engine_inspect_model(DiopEngineState state) {
    EngineContext *ctx = (EngineContext *)state;
    if (!ctx || !ctx->is_loaded) {
        return engine_not_initialized_response();
    }
    return build_inspect_payload(ctx);
}

DIOP_EXPORT const char *diop_engine_plan_load(DiopEngineState state) {
    EngineContext *ctx = (EngineContext *)state;
    if (!ctx || !ctx->is_loaded) {
        return engine_not_initialized_response();
    }
    return build_plan_payload(ctx);
}

DIOP_EXPORT const char *diop_engine_prepare_runtime(DiopEngineState state) {
    EngineContext *ctx = (EngineContext *)state;
    if (!ctx || !ctx->is_loaded) {
        return engine_not_initialized_response();
    }
    return build_prepare_payload(ctx);
}

DIOP_EXPORT const char *diop_engine_generate(DiopEngineState state, const char *prompt, int max_tokens) {
    EngineContext *ctx = (EngineContext *)state;
    char safe_prompt[256];
    char safe_model[256];
    char safe_arch[96];
    char safe_name[128];
    char safe_tokenizer[96];
    char generation_summary[256];

    if (!ctx || !ctx->is_loaded) {
        return engine_not_initialized_response();
    }

    escape_json_string(prompt ? prompt : "", safe_prompt, sizeof(safe_prompt));
    escape_json_string(ctx->model_path, safe_model, sizeof(safe_model));
    escape_json_string(ctx->summary.architecture, safe_arch, sizeof(safe_arch));
    escape_json_string(ctx->summary.name, safe_name, sizeof(safe_name));
    escape_json_string(ctx->summary.tokenizer_model, safe_tokenizer, sizeof(safe_tokenizer));
    if (!ctx->runtime_state.is_prepared) {
        prepare_runtime_state(&ctx->layout_plan, &ctx->runtime_state);
    }
    ensure_runtime_context(&ctx->runtime_state, &ctx->runtime_context);
    execute_generation_step(&ctx->runtime_context, prompt ? prompt : "", max_tokens, generation_summary, sizeof(generation_summary));

    snprintf(
        ctx->response_buffer,
        sizeof(ctx->response_buffer),
        "{"
        "\"summary\":\"%s\","
        "\"artifacts\":["
        "{"
        "\"name\":\"runtime_generation_stub\","
        "\"type\":\"generation_bridge\","
        "\"content\":{"
        "\"prompt_excerpt\":\"%s\","
        "\"requested_max_tokens\":%d,"
        "\"engine_mode\":\"prepared_runtime_stub\","
        "\"gguf_core_source\":\"%s\","
        "\"selected_backend\":\"%s\","
        "\"runtime_prepared\":%u,"
        "\"runtime_step_counter\":%u,"
        "\"runtime_status\":\"%s\","
        "\"executor_name\":\"%s\","
        "\"bridge_status\":\"%s\","
        "\"last_prompt_excerpt\":\"%s\","
        "\"next_step\":\"replace generate with oo_native_generate_step once the runtime allocator exists\""
        "}"
        "},"
        "{"
        "\"name\":\"gguf_model_summary\","
        "\"type\":\"model_metadata\","
        "\"content\":{"
        "\"model_path\":\"%s\","
        "\"architecture\":\"%s\","
        "\"name\":\"%s\","
        "\"tokenizer_model\":\"%s\","
        "\"version\":%u,"
        "\"tensor_count\":%llu,"
        "\"kv_count\":%llu,"
        "\"file_size\":%llu,"
        "\"context_length\":%llu,"
        "\"embedding_length\":%llu,"
        "\"block_count\":%llu,"
        "\"head_count\":%llu,"
        "\"head_count_kv\":%llu,"
        "\"vocab_size\":%llu,"
        "\"file_type\":%llu"
        "}"
        "},"
        "{"
        "\"name\":\"gguf_tensor_plan\","
        "\"type\":\"tensor_plan\","
        "\"content\":{"
        "\"data_start\":%llu,"
        "\"recognized_tensor_count\":%llu,"
        "\"max_tensor_offset\":%llu,"
        "\"token_embedding_present\":%u,"
        "\"output_present\":%u,"
        "\"rms_final_present\":%u,"
        "\"attn_norm_count\":%u,"
        "\"wq_count\":%u,"
        "\"wk_count\":%u,"
        "\"wv_count\":%u,"
        "\"wo_count\":%u,"
        "\"ffn_norm_count\":%u,"
        "\"ffn_gate_count\":%u,"
        "\"ffn_down_count\":%u,"
        "\"ffn_up_count\":%u,"
        "\"q8_0_count\":%u,"
        "\"supported_type_count\":%u,"
        "\"detected_layers\":%u,"
        "\"fully_mapped_llama\":%u"
        "}"
        "},"
        "{"
        "\"name\":\"llama2_layout_plan\","
        "\"type\":\"layout_plan\","
        "\"content\":{"
        "\"hyperparams_complete\":%u,"
        "\"shared_classifier\":%u,"
        "\"float_layout_compatible\":%u,"
        "\"q8_candidate\":%u,"
        "\"dim\":%llu,"
        "\"hidden_dim\":%llu,"
        "\"n_layers\":%llu,"
        "\"n_heads\":%llu,"
        "\"n_kv_heads\":%llu,"
        "\"vocab_size\":%llu,"
        "\"seq_len\":%llu,"
        "\"kv_dim\":%llu,"
        "\"head_size\":%llu,"
        "\"float_weights_bytes\":%llu,"
        "\"float_kv_cache_bytes\":%llu,"
        "\"float_total_bytes\":%llu"
        "}"
        "},"
        "{"
        "\"name\":\"runtime_readiness\","
        "\"type\":\"runtime_plan\","
        "\"content\":{"
        "\"ready_for_transition_runtime\":%u,"
        "\"ready_for_float_runtime\":%u,"
        "\"ready_for_q8_runtime\":%u,"
        "\"prepared\":%u,"
        "\"selected_backend\":\"%s\","
        "\"reserved_runtime_bytes\":%llu,"
        "\"runtime_context_allocated\":%u,"
        "\"executor_name\":\"%s\","
        "\"bridge_status\":\"%s\""
        "}"
        "},"
        "{"
        "\"name\":\"portable_core_contract\","
        "\"type\":\"extraction_plan\","
        "\"content\":{"
        "\"target_header\":\"engine/gguf/gguf_portable.h\","
        "\"target_stub\":\"engine/gguf/gguf_portable_stub.c\","
        "\"goal\":\"extract GGUF summary and runtime planning into a host/UEFI/bare-metal neutral core\","
        "\"next_phase\":\"migrate logic from gguf_loader.c and gguf_infer.c into this portable contract\""
        "}"
        "}"
        "],"
        "\"risks\":["
        "\"native engine currently performs metadata loading only\"," 
        "\"gguf tensor execution path is not yet connected to llama2 inference\""
        "],"
        "\"recommendations\":[" 
        "\"Connect this engine context to the existing engine/gguf and engine/llama2 execution path.\","
        "\"Add a host-side tensor planner so DIOP can validate a model before inference.\""
        "]"
        "}",
        generation_summary,
        safe_prompt,
        max_tokens,
        ctx->gguf_core_source,
        ctx->runtime_state.backend_name,
        ctx->runtime_state.is_prepared,
        ctx->runtime_context.step_counter,
        ctx->runtime_context.status,
        ctx->runtime_context.executor_name,
        ctx->runtime_context.bridge_status,
        ctx->runtime_context.last_prompt_excerpt,
        safe_model,
        safe_arch[0] ? safe_arch : "unknown",
        safe_name[0] ? safe_name : "unknown",
        safe_tokenizer[0] ? safe_tokenizer : "unknown",
        ctx->summary.version,
        (unsigned long long)ctx->summary.tensor_count,
        (unsigned long long)ctx->summary.kv_count,
        (unsigned long long)ctx->summary.file_size,
        (unsigned long long)ctx->summary.context_length,
        (unsigned long long)ctx->summary.embedding_length,
        (unsigned long long)ctx->summary.block_count,
        (unsigned long long)ctx->summary.head_count,
        (unsigned long long)ctx->summary.head_count_kv,
        (unsigned long long)ctx->summary.vocab_size,
        (unsigned long long)ctx->summary.file_type,
        (unsigned long long)ctx->tensor_plan.data_start,
        (unsigned long long)ctx->tensor_plan.recognized_tensor_count,
        (unsigned long long)ctx->tensor_plan.max_tensor_offset,
        ctx->tensor_plan.token_embedding_present,
        ctx->tensor_plan.output_present,
        ctx->tensor_plan.rms_final_present,
        ctx->tensor_plan.attn_norm_count,
        ctx->tensor_plan.wq_count,
        ctx->tensor_plan.wk_count,
        ctx->tensor_plan.wv_count,
        ctx->tensor_plan.wo_count,
        ctx->tensor_plan.ffn_norm_count,
        ctx->tensor_plan.ffn_gate_count,
        ctx->tensor_plan.ffn_down_count,
        ctx->tensor_plan.ffn_up_count,
        ctx->tensor_plan.q8_0_count,
        ctx->tensor_plan.supported_type_count,
        ctx->tensor_plan.detected_layers,
        ctx->tensor_plan.fully_mapped_llama,
        ctx->layout_plan.hyperparams_complete,
        ctx->layout_plan.shared_classifier,
        ctx->layout_plan.float_layout_compatible,
        ctx->layout_plan.q8_candidate,
        (unsigned long long)ctx->layout_plan.dim,
        (unsigned long long)ctx->layout_plan.hidden_dim,
        (unsigned long long)ctx->layout_plan.n_layers,
        (unsigned long long)ctx->layout_plan.n_heads,
        (unsigned long long)ctx->layout_plan.n_kv_heads,
        (unsigned long long)ctx->layout_plan.vocab_size,
        (unsigned long long)ctx->layout_plan.seq_len,
        (unsigned long long)ctx->layout_plan.kv_dim,
        (unsigned long long)ctx->layout_plan.head_size,
        (unsigned long long)ctx->layout_plan.float_weights_bytes,
        (unsigned long long)ctx->layout_plan.float_kv_cache_bytes,
        (unsigned long long)ctx->layout_plan.float_total_bytes,
        ctx->runtime_state.ready_for_transition_runtime,
        ctx->runtime_state.ready_for_float_runtime,
        ctx->runtime_state.ready_for_q8_runtime,
        ctx->runtime_state.is_prepared,
        ctx->runtime_state.backend_name,
        (unsigned long long)ctx->runtime_state.reserved_runtime_bytes,
        ctx->runtime_context.is_allocated,
        ctx->runtime_context.executor_name,
        ctx->runtime_context.bridge_status
    );

    printf("[SOMA C-ENGINE] Returning GGUF metadata payload to Python.\n");
    return ctx->response_buffer;
}

DIOP_EXPORT void diop_engine_free(DiopEngineState state) {
    EngineContext *ctx = (EngineContext *)state;
    if (!ctx) return;
    printf("[SOMA C-ENGINE] Releasing engine context for %s\n", ctx->model_path);
    free(ctx);
}
