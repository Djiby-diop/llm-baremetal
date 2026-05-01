#ifndef MODEL_PLANNING_SUPPORT_H
#define MODEL_PLANNING_SUPPORT_H

#include <stddef.h>

#include "diop_native_types.h"

int load_model_plans(
    const char *model_path,
    GgufSummary *summary,
    TensorPlanSummary *tensor_plan,
    Llama2LayoutPlanSummary *layout_plan,
    char *gguf_core_source,
    size_t gguf_core_source_capacity,
    char *error_message,
    size_t error_capacity
);

#endif
