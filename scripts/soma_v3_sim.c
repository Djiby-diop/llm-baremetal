#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Mock EFI / Bare-metal env */
#define Print(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define L ""
#define CHAR16 char

#include "../engine/ssm/core/soma_mind.c"
#include "../engine/ssm/soma_logic.c"
#include "../engine/ssm/soma_router.c"

/* Mock OOSI V3 */
OosiV3HaltResult oosi_v3_forward_one(OosiV3GenCtx *ctx, int token_id) {
    OosiV3HaltResult r = { .token = 10, .halt_prob = 0.1f, .halted = 0, .loop = 0 };
    static int steps = 0;
    if (steps++ % 5 == 0) r.halted = 1; /* Halt every 5 tokens */
    return r;
}

int main() {
    SomaMindCtx m;
    SomaRouterCtx r;
    SomaLogicCtx l;
    
    printf("--- SomaMind V3 Stress Simulation ---\n");
    
    soma_router_init(&r);
    soma_logic_init(&l);
    soma_mind_init(&m, &r, (OosiV3GenCtx*)1, &l); /* Mock core pointer */
    
    /* 1. Spawn Objects */
    soma_mind_spawn(&m, "SOLAR_SYSTEM_CHECK", 2.0f);
    soma_mind_spawn(&m, "LUNAR_CREATIVE_DREAM", 1.5f);
    soma_mind_spawn(&m, "Si A alors B. A est vrai. B est faux.", 3.0f); /* CONTRADICTION */
    
    /* 2. Simulation Loop */
    for (int i = 0; i < 30; i++) {
        printf("\n[Pulse %d]\n", i);
        
        /* Dynamic Telemetry Stress */
        float temp = 35.0f + (i % 20);
        float pressure = (i % 10) / 10.0f;
        soma_mind_update_telemetry(&m, temp, pressure);
        
        int processed = soma_mind_pulse(&m);
        if (!processed) {
            printf("No objects to process. Simulation complete.\n");
            break;
        }
        
        /* Stats */
        printf("  Pulses: %ld  WeightAdj: %.4f  Temp: %.1fC  Pressure: %.1f\n", 
               (long)m.total_pulses, m.plasticity.weight_adj, temp, pressure);
        
        for (int j = 0; j < SOMA_MIND_OBJECT_MAX; j++) {
            if (m.objects[j].state != SOMA_OBJ_FREE) {
                printf("    ID:%d [%s] State:%d Prio:%.2f Cost:%.2f\n", 
                       m.objects[j].id, m.objects[j].name, m.objects[j].state, 
                       m.objects[j].priority, m.objects[j].cost_estimate);
            }
        }
    }
    
    return 0;
}
