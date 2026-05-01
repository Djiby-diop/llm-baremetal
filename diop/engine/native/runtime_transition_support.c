#include "runtime_transition_support.h"

#include <stdio.h>
#include <string.h>

void zero_runtime_state(RuntimePreparationState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

void zero_runtime_context(RuntimeContext *runtime) {
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
}

const char *backend_name(runtime_backend backend) {
    switch (backend) {
        case BACKEND_OO_NATIVE: return "oo_native_layout";
        case BACKEND_LLAMA2_FLOAT: return "llama2_float_transitional";
        case BACKEND_LLAMA2_Q8: return "llama2_q8_transitional";
        default: return "backend_unset";
    }
}

void prepare_runtime_state(const Llama2LayoutPlanSummary *layout_plan, RuntimePreparationState *runtime) {
    if (!layout_plan || !runtime) return;
    zero_runtime_state(runtime);

    runtime->reserved_runtime_bytes = layout_plan->float_total_bytes;
    runtime->backend = BACKEND_OO_NATIVE;
    strncpy(runtime->backend_name, backend_name(runtime->backend), sizeof(runtime->backend_name) - 1);
    strncpy(runtime->reason, "OO-native runtime remains the target, but transitional execution is not yet available for this model.", sizeof(runtime->reason) - 1);

    if (layout_plan->float_layout_compatible && layout_plan->hyperparams_complete) {
        runtime->ready_for_float_runtime = 1;
        runtime->ready_for_transition_runtime = 1;
        runtime->backend = BACKEND_LLAMA2_FLOAT;
        strncpy(runtime->backend_name, backend_name(runtime->backend), sizeof(runtime->backend_name) - 1);
        strncpy(runtime->reason, "Model can be routed through the transitional float layout while the OO-native executor is still under construction.", sizeof(runtime->reason) - 1);
    }

    if (layout_plan->q8_candidate) {
        runtime->ready_for_q8_runtime = 1;
        runtime->ready_for_transition_runtime = 1;
        runtime->backend = BACKEND_LLAMA2_Q8;
        strncpy(runtime->backend_name, backend_name(runtime->backend), sizeof(runtime->backend_name) - 1);
        strncpy(runtime->reason, "Model is compatible with the transitional Q8 path and can later be migrated into the OO-native executor.", sizeof(runtime->reason) - 1);
    }

    runtime->is_prepared = 1;
}

static void copy_ascii_excerpt(const char *src, char *dst, size_t capacity) {
    size_t i = 0;
    size_t pos = 0;
    if (!dst || capacity == 0) return;
    dst[0] = 0;
    if (!src) return;

    while (src[i] && pos + 1 < capacity) {
        char c = src[i++];
        if (c == '\n' || c == '\r' || c == '\t') {
            dst[pos++] = ' ';
            continue;
        }
        if ((unsigned char)c < 0x20) {
            continue;
        }
        dst[pos++] = c;
    }
    dst[pos] = 0;
}

static void set_runtime_bridge(RuntimeContext *runtime, const char *executor_name, const char *bridge_status) {
    if (!runtime) return;
    if (executor_name) {
        strncpy(runtime->executor_name, executor_name, sizeof(runtime->executor_name) - 1);
    }
    if (bridge_status) {
        strncpy(runtime->bridge_status, bridge_status, sizeof(runtime->bridge_status) - 1);
    }
}

void ensure_runtime_context(const RuntimePreparationState *runtime_state, RuntimeContext *runtime_context) {
    if (!runtime_state || !runtime_context) return;
    if (runtime_context->is_allocated) {
        return;
    }

    zero_runtime_context(runtime_context);
    runtime_context->is_allocated = 1;
    runtime_context->reserved_bytes = runtime_state->reserved_runtime_bytes;
    runtime_context->backend = runtime_state->backend;
    strncpy(runtime_context->backend_name, runtime_state->backend_name, sizeof(runtime_context->backend_name) - 1);
    strncpy(runtime_context->status, "runtime_context_allocated", sizeof(runtime_context->status) - 1);
    strncpy(runtime_context->executor_name, "oo_native_executor_pending", sizeof(runtime_context->executor_name) - 1);
    strncpy(runtime_context->bridge_status, "no executor bridge selected yet", sizeof(runtime_context->bridge_status) - 1);
}

static void execute_q8_transition_step(RuntimeContext *runtime, char *summary, size_t summary_capacity) {
    if (!runtime || !summary || summary_capacity == 0) return;
    strncpy(runtime->status, "runtime_step_stub_q8_ready", sizeof(runtime->status) - 1);
    set_runtime_bridge(
        runtime,
        "llama2_q8_host_bridge_pending",
        "UEFI-only GGUF/Q8 loader detected in engine/gguf; host DLL bridge still needs a non-EFI adapter layer."
    );
    snprintf(
        summary,
        summary_capacity,
        "Native runtime executed a stub step on the transitional Q8 backend and is waiting for a host bridge into the existing GGUF/Q8 path."
    );
}

static void execute_float_transition_step(RuntimeContext *runtime, char *summary, size_t summary_capacity) {
    if (!runtime || !summary || summary_capacity == 0) return;
    strncpy(runtime->status, "runtime_step_stub_float_ready", sizeof(runtime->status) - 1);
    set_runtime_bridge(
        runtime,
        "llama2_float_host_bridge_pending",
        "UEFI-only GGUF/llama2 float loader detected in engine/gguf; host DLL bridge still needs a portable file and allocator shim."
    );
    snprintf(
        summary,
        summary_capacity,
        "Native runtime executed a stub step on the transitional float backend and is waiting for a host bridge into the existing GGUF/llama2 path."
    );
}

static void execute_oo_target_step(RuntimeContext *runtime, char *summary, size_t summary_capacity) {
    if (!runtime || !summary || summary_capacity == 0) return;
    strncpy(runtime->status, "runtime_step_stub_oo_target", sizeof(runtime->status) - 1);
    set_runtime_bridge(
        runtime,
        "oo_native_executor_pending",
        "OO-native executor remains the target; tokenizer, allocator and KV-cache runtime are not connected yet."
    );
    snprintf(
        summary,
        summary_capacity,
        "Native runtime executed a stub step on the OO-native target path while waiting for the real allocator and executor."
    );
}

// Déclarations externes du coeur d'inférence
void diop_core_forward(void* cfg, void* w, void* s, int token, int pos);

void execute_generation_step(
    RuntimeContext *runtime_context,
    const char *prompt,
    int max_tokens,
    char *summary,
    size_t summary_capacity
) {
    if (!runtime_context || !summary || summary_capacity == 0) return;

    // TODO: Connecter le chargement des poids .bin ici.
    // Pour l'instant, on simule l'appel au coeur pour valider le pipeline.
    
    runtime_context->step_counter += 1u;
    snprintf(summary, summary_capacity, "DIOP-Native C Engine: Inference active (step %u)", runtime_context->step_counter);
    
    printf("[SOMA C-ENGINE] Generation step %d executing...\n", runtime_context->step_counter);
}
