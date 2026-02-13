#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Orchestrion: workflow runner engine (sequences, macros, pipelines)


typedef enum {
    ORCHESTRION_MODE_OFF     = 0,
    ORCHESTRION_MODE_OBSERVE = 1,
    ORCHESTRION_MODE_ENFORCE = 2,
} OrchestrionMode;

typedef enum {
    ORCHESTRION_STATE_IDLE    = 0,
    ORCHESTRION_STATE_RUNNING = 1,
    ORCHESTRION_STATE_PAUSED  = 2,
    ORCHESTRION_STATE_ERROR   = 3,
} OrchestrionState;

#define ORCHESTRION_MAX_STEPS 32
#define ORCHESTRION_STEP_LEN  128

typedef struct {
    char steps[ORCHESTRION_MAX_STEPS][ORCHESTRION_STEP_LEN];
    uint32_t step_count;
    uint32_t current_step;
    OrchestrionState state;
    uint32_t loops_done;
    uint32_t loops_max;       // 0 = run once
    uint32_t delay_ms;        // delay between steps (0 = no delay)
} OrchestrionPipeline;

typedef struct {
    OrchestrionMode mode;
    OrchestrionPipeline pipeline;
    uint32_t workflows_run;
    uint32_t steps_executed;
    uint32_t errors;
} OrchestrionEngine;

void orchestrion_init(OrchestrionEngine *e);
void orchestrion_set_mode(OrchestrionEngine *e, OrchestrionMode mode);
const char *orchestrion_mode_name_ascii(OrchestrionMode mode);
const char *orchestrion_state_name_ascii(OrchestrionState state);

void orchestrion_pipeline_clear(OrchestrionEngine *e);
int  orchestrion_pipeline_add_step(OrchestrionEngine *e, const char *step);
int  orchestrion_pipeline_start(OrchestrionEngine *e, uint32_t loops);
void orchestrion_pipeline_pause(OrchestrionEngine *e);
void orchestrion_pipeline_resume(OrchestrionEngine *e);
void orchestrion_pipeline_stop(OrchestrionEngine *e);
const char *orchestrion_pipeline_next_step(OrchestrionEngine *e);

#ifdef __cplusplus
}
#endif
