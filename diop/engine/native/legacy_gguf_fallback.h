#ifndef LEGACY_GGUF_FALLBACK_H
#define LEGACY_GGUF_FALLBACK_H

#include <stddef.h>

#include "diop_native_types.h"

int read_summary_from_legacy_gguf(
    const char *model_path,
    GgufSummary *summary,
    TensorPlanSummary *tensor_plan,
    char *error_message,
    size_t error_capacity
);

#endif
