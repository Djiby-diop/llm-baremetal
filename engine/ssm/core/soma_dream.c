#include "soma_dream.h"
#include <string.h>

/* Internal constants mirrored from oo-sim */
#define SIM_RAM_MAX  (8 * 1024 * 1024)
#define SIM_TICK_MAX 20

typedef struct {
    int used;
    int max;
} InternalSimPressure;

static InternalSimPressure g_sim_pressure;

void soma_dream_init(void) {
    g_sim_pressure.used = 0;
    g_sim_pressure.max  = SIM_RAM_MAX;
}

SomaDreamSummary soma_dream_pulse(SomaMindCtx *m, SomaMindObject *obj) {
    SomaDreamSummary summary;
    memset(&summary, 0, sizeof(summary));
    
    if (!m || !obj) {
        summary.result = DREAM_TIMEOUT;
        return summary;
    }

    /* Seed simulation with current mind telemetry */
    int current_pressure_pct = (int)(m->memory_pressure * 100.0f);
    g_sim_pressure.used = (SIM_RAM_MAX * current_pressure_pct) / 100;
    
    /* Determine task class from object name/domain */
    const char *safety_class = "normal";
    SomaDomain domain = soma_classify_domain(obj->name, (int)strlen(obj->name));
    if (domain == SOMA_DOMAIN_SYSTEM || domain == SOMA_DOMAIN_POLICY) {
        safety_class = "experimental";
    }

    /* Run internal ticks */
    int ticks = 0;
    while (ticks < SIM_TICK_MAX) {
        ticks++;
        
        /* 1. Policy Check (D+) */
        if (strcmp(safety_class, "experimental") == 0) {
            /* If we are under stress in the dream, D+ denies experimental */
            if (g_sim_pressure.used > (SIM_RAM_MAX * 0.7f)) {
                summary.result = DREAM_POLICY_DENY;
                strncpy(summary.fail_reason, "D+ DENY: critical pressure in dream", 63);
                break;
            }
        }
        
        /* 2. Physical Impact Simulation */
        /* Each tick of 'thinking/acting' consumes RAM in the dream */
        g_sim_pressure.used += 1024 * 1024; /* 1MB per tick for intense tasks */
        
        if (g_sim_pressure.used >= SIM_RAM_MAX) {
            summary.result = DREAM_PRESSURE_HIGH;
            strncpy(summary.fail_reason, "SYSTEM PANIC: OOM in dream", 63);
            break;
        }
        
        /* Simulation completed normally for this object? 
           In V3, we assume an object needs 5 ticks to 'resolve'. */
        if (ticks >= 5) {
            summary.result = DREAM_SUCCESS;
            break;
        }
    }
    
    summary.ticks_taken = ticks;
    summary.final_pressure = (float)g_sim_pressure.used / (float)SIM_RAM_MAX;
    
    return summary;
}
