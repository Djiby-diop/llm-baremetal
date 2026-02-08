#include "orchestrion.h"

// Simple strlen for freestanding
static int orch_strlen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void orch_strcpy_cap(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = 0;
}

void orchestrion_init(OrchestrionEngine *e) {
    if (!e) return;
    e->mode = ORCHESTRION_MODE_OFF;
    e->workflows_run = 0;
    e->steps_executed = 0;
    e->errors = 0;
    orchestrion_pipeline_clear(e);
}

void orchestrion_set_mode(OrchestrionEngine *e, OrchestrionMode mode) {
    if (!e) return;
    e->mode = mode;
}

const char *orchestrion_mode_name_ascii(OrchestrionMode mode) {
    switch (mode) {
        case ORCHESTRION_MODE_OFF:     return "off";
        case ORCHESTRION_MODE_OBSERVE: return "observe";
        case ORCHESTRION_MODE_ENFORCE: return "enforce";
        default:                       return "?";
    }
}

const char *orchestrion_state_name_ascii(OrchestrionState state) {
    switch (state) {
        case ORCHESTRION_STATE_IDLE:    return "idle";
        case ORCHESTRION_STATE_RUNNING: return "running";
        case ORCHESTRION_STATE_PAUSED:  return "paused";
        case ORCHESTRION_STATE_ERROR:   return "error";
        default:                        return "?";
    }
}

void orchestrion_pipeline_clear(OrchestrionEngine *e) {
    if (!e) return;
    e->pipeline.step_count = 0;
    e->pipeline.current_step = 0;
    e->pipeline.state = ORCHESTRION_STATE_IDLE;
    e->pipeline.loops_done = 0;
    e->pipeline.loops_max = 0;
    e->pipeline.delay_ms = 0;
    for (int i = 0; i < ORCHESTRION_MAX_STEPS; i++) {
        e->pipeline.steps[i][0] = 0;
    }
}

int orchestrion_pipeline_add_step(OrchestrionEngine *e, const char *step) {
    if (!e || !step) return 0;
    if (e->pipeline.step_count >= ORCHESTRION_MAX_STEPS) return 0;
    orch_strcpy_cap(e->pipeline.steps[e->pipeline.step_count], ORCHESTRION_STEP_LEN, step);
    e->pipeline.step_count++;
    return 1;
}

int orchestrion_pipeline_start(OrchestrionEngine *e, uint32_t loops) {
    if (!e) return 0;
    if (e->pipeline.step_count == 0) return 0;
    e->pipeline.current_step = 0;
    e->pipeline.loops_done = 0;
    e->pipeline.loops_max = loops;
    e->pipeline.state = ORCHESTRION_STATE_RUNNING;
    e->workflows_run++;
    return 1;
}

void orchestrion_pipeline_pause(OrchestrionEngine *e) {
    if (!e) return;
    if (e->pipeline.state == ORCHESTRION_STATE_RUNNING) {
        e->pipeline.state = ORCHESTRION_STATE_PAUSED;
    }
}

void orchestrion_pipeline_resume(OrchestrionEngine *e) {
    if (!e) return;
    if (e->pipeline.state == ORCHESTRION_STATE_PAUSED) {
        e->pipeline.state = ORCHESTRION_STATE_RUNNING;
    }
}

void orchestrion_pipeline_stop(OrchestrionEngine *e) {
    if (!e) return;
    e->pipeline.state = ORCHESTRION_STATE_IDLE;
    e->pipeline.current_step = 0;
}

const char *orchestrion_pipeline_next_step(OrchestrionEngine *e) {
    if (!e) return 0;
    if (e->pipeline.state != ORCHESTRION_STATE_RUNNING) return 0;
    if (e->pipeline.step_count == 0) return 0;

    if (e->pipeline.current_step >= e->pipeline.step_count) {
        // End of pipeline
        e->pipeline.loops_done++;
        if (e->pipeline.loops_max > 0 && e->pipeline.loops_done >= e->pipeline.loops_max) {
            e->pipeline.state = ORCHESTRION_STATE_IDLE;
            return 0;
        }
        e->pipeline.current_step = 0;
    }

    const char *step = e->pipeline.steps[e->pipeline.current_step];
    e->pipeline.current_step++;
    e->steps_executed++;
    return step;
}
