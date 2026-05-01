#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Mock EFI / Bare-metal env macros */
#define Print(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define L 
#define CHAR16 char

/* Standard project includes */
#include "soma_router.h"
#include "soma_logic.h"
#include "oosi_v3_infer.h"
#include "soma_mind.h"

/* Mock the Neural Core since we don't want to link full Mamba for a logic test */
OosiV3HaltResult oosi_v3_forward_one(OosiV3GenCtx *ctx, int token_id) {
    OosiV3HaltResult r = { .token = 10, .halt_prob = 0.1f, .halted = 0, .loop = 0 };
    static int steps = 0;
    if (steps++ % 3 == 0) r.halted = 1; /* Quick resolution */
    return r;
}

int main() {
    SomaMindCtx m;
    SomaRouterCtx r;
    SomaLogicCtx l;
    
    printf("--- SomaMind V3 Verification ---\n");
    
    /* 1. Logic & Router Mock Init */
    memset(&r, 0, sizeof(r));
    memset(&l, 0, sizeof(l));
    
    /* 2. SomaMind Init */
    soma_mind_init(&m, &r, (OosiV3GenCtx*)1, &l);
    
    /* 3. Scenario: Spawn a thought */
    SomaMindObject *o1 = soma_mind_spawn(&m, "THINK_ABOUT_SYSTEM", 5.0f);
    printf("Object spawned: ID %d, Name: %s\n", o1->id, o1->name);
    
    /* 4. Pulse Loop */
    for (int i = 0; i < 15; i++) {
        int processed = soma_mind_pulse(&m);
        if (!processed) break;
        
        printf("[Pulse %d] ObjState: %d Prio: %.2f WeightAdj: %.5f\n", 
               i, o1->state, o1->priority, m.plasticity.weight_adj);
        
        if (o1->state == SOMA_OBJ_RESOLVED) {
            printf("Thought Resolved successfully.\n");
            break;
        }
    }
    
    printf("Simulation Done. Plasticity Updates: %d\n", m.plasticity.total_updates);
    return 0;
}
