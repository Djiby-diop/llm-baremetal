#ifndef RUNTIME_TRANSITION_SUPPORT_H
#define RUNTIME_TRANSITION_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "diop_native_types.h"

typedef enum {
    BACKEND_UNSET = 0,
    BACKEND_OO_NATIVE = 1,
    BACKEND_LLAMA2_FLOAT = 2,
    BACKEND_LLAMA2_Q8 = 3
} runtime_backend;

typedef struct {
    uint32_t is_prepared;
    uint32_t ready_for_transition_runtime;
    uint32_t ready_for_float_runtime;
    uint32_t ready_for_q8_runtime;
    uint64_t reserved_runtime_bytes;
    runtime_backend backend;
    char backend_name[48];
    char reason[160];
} RuntimePreparationState;

typedef struct {
    uint32_t is_allocated;
    uint32_t step_counter;
    uint64_t reserved_bytes;
    runtime_backend backend;
    char backend_name[48];
    char status[96];
    char executor_name[64];
    char bridge_status[160];
    char last_prompt_excerpt[192];
} RuntimeContext;

void zero_runtime_state(RuntimePreparationState *state);
void zero_runtime_context(RuntimeContext *runtime);
const char *backend_name(runtime_backend backend);
void prepare_runtime_state(const Llama2LayoutPlanSummary *layout_plan, RuntimePreparationState *runtime);
void ensure_runtime_context(const RuntimePreparationState *runtime_state, RuntimeContext *runtime_context);
void execute_generation_step(
    RuntimeContext *runtime_context,
    const char *prompt,
    int max_tokens,
    char *summary,
    size_t summary_capacity
);

#endif
