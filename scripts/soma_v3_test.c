#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Mock EFI / Bare-metal env */
#define Print(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define L ""
#define CHAR16 char

/* Forward declarations to satisfy soma_mind.c */
typedef int SomaDomain;
typedef struct { int dummy; } SomaRouterCtx;
typedef struct { int dummy; } SomaLogicCtx;
typedef struct { int dummy; } OosiV3GenCtx;

/* We'll include the C files directly but we need to mock the external functions they call */
#define soma_classify_domain(a, b) 0 
#define soma_router_init(a)
#define soma_logic_init(a)
#define soma_logic_scan(a, b) (SomaLogicResult){0}

#include "../engine/ssm/core/soma_mind.c"

/* Mock OOSI V3 */
OosiV3HaltResult oosi_v3_forward_one(OosiV3GenCtx *ctx, int token_id) {
    OosiV3HaltResult r = { .token = 10, .halt_prob = 0.1f, .halted = 0, .loop = 0 };
    static int steps = 0;
    if (steps++ % 5 == 0) r.halted = 1; 
    return r;
}

int main() {
    SomaMindCtx m;
    printf("--- SomaMind V3 Basic Logic Test ---\n");
    
    soma_mind_init(&m, NULL, (OosiV3GenCtx*)1, NULL);
    
    SomaMindObject *o1 = soma_mind_spawn(&m, "TEST_OBJECT", 2.0f);
    if (!o1) { printf("Spawn failed\n"); return 1; }
    
    for (int i = 0; i < 10; i++) {
        soma_mind_pulse(&m);
        printf("Pulse %d: Obj %d State %d Prio %.2f WeightAdj %.4f\n", 
               i, o1->id, o1->state, o1->priority, m.plasticity.weight_adj);
        if (o1->state == SOMA_OBJ_RESOLVED) break;
    }
    
    printf("Success.\n");
    return 0;
}
