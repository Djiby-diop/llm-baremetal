#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diop_native_types.h"
#include "model_planning_support.h"
#include "runtime_transition_support.h"

static void print_usage(void) {
    printf("Usage: runtime_transition_cli <path-to-model.gguf> [prompt] [max_tokens]\n");
}

static int parse_int_best_effort(const char *s, int fallback) {
    long v;
    char *end = NULL;
    if (!s || !s[0]) return fallback;
    v = strtol(s, &end, 10);
    if (end == s) return fallback;
    if (v < 0) v = 0;
    if (v > 4096) v = 4096;
    return (int)v;
}

int main(int argc, char **argv) {
    const char *model_path = NULL;
    const char *prompt = "Hello from runtime_transition_cli";
    int max_tokens = 128;

    GgufSummary summary;
    TensorPlanSummary tensor_plan;
    Llama2LayoutPlanSummary layout_plan;
    RuntimePreparationState runtime_state;
    RuntimeContext runtime_context;
    char core_source[32];
    char error_message[256];
    char generation_summary[256];

    memset(&summary, 0, sizeof(summary));
    memset(&tensor_plan, 0, sizeof(tensor_plan));
    memset(&layout_plan, 0, sizeof(layout_plan));
    zero_runtime_state(&runtime_state);
    zero_runtime_context(&runtime_context);
    memset(core_source, 0, sizeof(core_source));
    memset(error_message, 0, sizeof(error_message));
    memset(generation_summary, 0, sizeof(generation_summary));

    if (argc < 2 || !argv[1] || !argv[1][0]) {
        print_usage();
        return 1;
    }
    model_path = argv[1];
    if (argc >= 3 && argv[2] && argv[2][0]) {
        prompt = argv[2];
    }
    if (argc >= 4 && argv[3] && argv[3][0]) {
        max_tokens = parse_int_best_effort(argv[3], max_tokens);
    }

    if (!load_model_plans(
            model_path,
            &summary,
            &tensor_plan,
            &layout_plan,
            core_source,
            sizeof(core_source),
            error_message,
            sizeof(error_message))) {
        printf("runtime_transition_cli: failed: %s\n", error_message[0] ? error_message : "unknown error");
        return 2;
    }

    prepare_runtime_state(&layout_plan, &runtime_state);
    ensure_runtime_context(&runtime_state, &runtime_context);
    execute_generation_step(&runtime_context, prompt, max_tokens, generation_summary, sizeof(generation_summary));

    printf("model_path=%s\n", model_path);
    printf("gguf_core_source=%s\n", core_source);
    printf("selected_backend=%s\n", runtime_state.backend_name[0] ? runtime_state.backend_name : backend_name(runtime_state.backend));
    printf("ready_for_transition_runtime=%u\n", runtime_state.ready_for_transition_runtime);
    printf("ready_for_float_runtime=%u\n", runtime_state.ready_for_float_runtime);
    printf("ready_for_q8_runtime=%u\n", runtime_state.ready_for_q8_runtime);
    printf("reserved_runtime_bytes=%llu\n", (unsigned long long)runtime_state.reserved_runtime_bytes);
    printf("preparation_reason=%s\n", runtime_state.reason[0] ? runtime_state.reason : "");

    printf("runtime_allocated=%u\n", runtime_context.is_allocated);
    printf("runtime_step_counter=%u\n", runtime_context.step_counter);
    printf("runtime_status=%s\n", runtime_context.status[0] ? runtime_context.status : "");
    printf("executor_name=%s\n", runtime_context.executor_name[0] ? runtime_context.executor_name : "");
    printf("bridge_status=%s\n", runtime_context.bridge_status[0] ? runtime_context.bridge_status : "");
    printf("last_prompt_excerpt=%s\n", runtime_context.last_prompt_excerpt[0] ? runtime_context.last_prompt_excerpt : "");
    printf("generation_summary=%s\n", generation_summary[0] ? generation_summary : "");
    return 0;
}
