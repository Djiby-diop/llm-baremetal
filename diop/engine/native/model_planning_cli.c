#include <stdio.h>
#include <string.h>

#include "diop_native_types.h"
#include "model_planning_support.h"

static void print_usage(void) {
    printf("Usage: model_planning_cli <path-to-model.gguf>\n");
}

int main(int argc, char **argv) {
    GgufSummary summary;
    TensorPlanSummary tensor_plan;
    Llama2LayoutPlanSummary layout_plan;
    char core_source[32];
    char error_message[256];

    memset(&summary, 0, sizeof(summary));
    memset(&tensor_plan, 0, sizeof(tensor_plan));
    memset(&layout_plan, 0, sizeof(layout_plan));
    memset(core_source, 0, sizeof(core_source));
    memset(error_message, 0, sizeof(error_message));

    if (argc < 2 || !argv[1] || !argv[1][0]) {
        print_usage();
        return 1;
    }

    if (!load_model_plans(
            argv[1],
            &summary,
            &tensor_plan,
            &layout_plan,
            core_source,
            sizeof(core_source),
            error_message,
            sizeof(error_message))) {
        printf("model_planning_cli: failed: %s\n", error_message[0] ? error_message : "unknown error");
        return 2;
    }

    printf("model_path=%s\n", argv[1]);
    printf("gguf_core_source=%s\n", core_source);
    printf("architecture=%s\n", summary.architecture[0] ? summary.architecture : "unknown");
    printf("name=%s\n", summary.name[0] ? summary.name : "unknown");
    printf("tensor_count=%llu\n", (unsigned long long)summary.tensor_count);
    printf("kv_count=%llu\n", (unsigned long long)summary.kv_count);
    printf("context_length=%llu\n", (unsigned long long)summary.context_length);
    printf("embedding_length=%llu\n", (unsigned long long)summary.embedding_length);
    printf("block_count=%llu\n", (unsigned long long)summary.block_count);
    printf("head_count=%llu\n", (unsigned long long)summary.head_count);
    printf("head_count_kv=%llu\n", (unsigned long long)summary.head_count_kv);
    printf("vocab_size=%llu\n", (unsigned long long)summary.vocab_size);
    printf("recognized_tensor_count=%llu\n", (unsigned long long)tensor_plan.recognized_tensor_count);
    printf("detected_layers=%u\n", tensor_plan.detected_layers);
    printf("fully_mapped_llama=%u\n", tensor_plan.fully_mapped_llama);
    printf("q8_0_count=%u\n", tensor_plan.q8_0_count);
    printf("float_layout_compatible=%u\n", layout_plan.float_layout_compatible);
    printf("q8_candidate=%u\n", layout_plan.q8_candidate);
    printf("float_total_bytes=%llu\n", (unsigned long long)layout_plan.float_total_bytes);
    return 0;
}
