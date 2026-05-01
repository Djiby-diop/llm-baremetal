#ifndef LAYOUT_PLAN_SUPPORT_H
#define LAYOUT_PLAN_SUPPORT_H

#include "diop_native_types.h"

void zero_layout_plan(Llama2LayoutPlanSummary *plan);
void finalize_layout_plan(
    Llama2LayoutPlanSummary *layout,
    const GgufSummary *summary,
    const TensorPlanSummary *tensor_plan
);

#endif
