#pragma once

/*
 * SomaDream: Internal Cognitive Simulator
 * 
 * Allows the SomaMind to 'dream' (simulate) actions before execution.
 * Bridges the bare-metal kernel with a virtualized environment.
 */

#include "soma_mind.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DREAM_SUCCESS = 0,
    DREAM_PRESSURE_HIGH = 1,
    DREAM_POLICY_DENY = 2,
    DREAM_COLLISION = 3,
    DREAM_TIMEOUT = 4,
} SomaDreamResult;

typedef struct {
    SomaDreamResult result;
    int             ticks_taken;
    float           final_pressure;
    char            fail_reason[64];
} SomaDreamSummary;

/* Initialize the internal simulator */
void soma_dream_init(void);

/* 
 * Run a 'dream' for a specific cognitive object.
 * Simulates the impact of its intention on the system.
 */
SomaDreamSummary soma_dream_pulse(SomaMindCtx *m, SomaMindObject *obj);

#ifdef __cplusplus
}
#endif
