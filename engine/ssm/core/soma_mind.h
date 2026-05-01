#pragma once

/*
 * OO-SomaMind V1: Foundation
 * 
 * Fusion of SSM (Neural Core), Object Runtime and Adaptive Halting.
 * Designed for bare-metal x86_64 performance and D+ governance.
 */

#include "ssm_types.h"
#include "oosi_v3_infer.h"
#include "soma_router.h"
#include "soma_logic.h"
#include "../../../oo-modules/cellion-engine/core/cellion.h"
#include "../../../oo-modules/collectivion-engine/core/collectivion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOMA_MIND_OBJECT_MAX    32    /* Increased for graph depth */
#define SOMA_MIND_LINK_MAX      4     /* Max links per object */
#define SOMA_MIND_LATENT_DIM    128
#define SOMA_MIND_NAME_LEN      32

typedef enum {
    SOMA_OBJ_FREE     = 0,
    SOMA_OBJ_THINKING = 1,
    SOMA_OBJ_WAITING  = 2,  /* Waiting for tool result */
    SOMA_OBJ_DORMANT  = 3,
    SOMA_OBJ_RESOLVED = 4,
    SOMA_OBJ_CRITIC   = 5,  /* Self-reflection state */
} SomaMindObjectState;

typedef enum {
    SOMA_TOOL_NONE   = 0,
    SOMA_TOOL_IO     = 1,
    SOMA_TOOL_BUS    = 2,
    SOMA_TOOL_MEMORY = 3,
    SOMA_TOOL_MATH   = 4,
    SOMA_TOOL_VISION = 5,
    SOMA_TOOL_SYNC   = 6,
} SomaMindToolType;

typedef struct {
    uint32_t            object_id;
    SomaMindToolType    type;
    int                 success;
    char                payload[128];
} SomaMindToolResult;

typedef struct {
    uint32_t            id;
    char                name[SOMA_MIND_NAME_LEN];
    SomaMindObjectState state;
    float               priority;
    float               cost_estimate; /* Compute budget needed */
    float               latent[SOMA_MIND_LATENT_DIM];
    
    /* Object Graph (V2) */
    uint32_t            links[SOMA_MIND_LINK_MAX];
    uint32_t            parent_id;
    
    /* Metadata */
    uint32_t            creator_id;
    uint32_t            ts_created;
    float               success_rate; /* V3 Plasticity feedback */
} SomaMindObject;

typedef struct {
    float               learning_rate;
    float               weight_adj;
    uint32_t            total_updates;
} SomaNeuralPlasticity;

typedef enum {
    SOMA_ENGINE_LUNAR = 0, /* Neural/Intuitive (SSM) */
    SOMA_ENGINE_SOLAR = 1, /* Symbolic/Logical (Syllogism) */
} SomaMindEngineType;

typedef struct {
    int                 active;
    SomaRouterCtx      *router;
    OosiV3GenCtx       *core;
    SomaLogicCtx       *logic;        /* Solar Engine Core (V2) */
    CellionEngine      *vision;       /* Vision Organ (V1 Organ) */
    CollectivionEngine *comm;         /* Comm Organ (V1 Organ) */
    GhostEngine        *ghost;        /* Transport Layer */
    
    SomaMindObject      objects[SOMA_MIND_OBJECT_MAX];
    uint32_t            next_object_id;
    
    /* Scheduler Stats (V2) */
    uint64_t            total_pulses;
    float               energy_budget;
    
    /* V3 Memory & Plasticity */
    SomaNeuralPlasticity plasticity;
    uint32_t            memory_nodes;
    float               memory_pressure;
    
    /* Physical awareness */
    float               core_temp;    /* Received from calibrion */
    float               halt_pressure; /* Sum of halting signals */
} SomaMindCtx;

/* --- V2: Dual Engine & Graph Operations --- */

/* Establish a link between two cognitive objects */
void soma_mind_link(SomaMindCtx *m, uint32_t parent_id, uint32_t child_id);

/* The Fusion Gate: Arbitrate between Solar and Lunar engines */
SomaMindEngineType soma_mind_fusion_gate(SomaMindCtx *m, SomaMindObject *obj);

/* --- V3: Plasticity & Reflection --- */

/* Apply feedback to the mind's weights (simplified plasticity) */
void soma_mind_apply_feedback(SomaMindCtx *m, uint32_t obj_id, float success);

/* Trigger a reflection cycle on a resolved object */
void soma_mind_reflect(SomaMindCtx *m, uint32_t obj_id);

/* --- Lifecycle --- */

void soma_mind_init(SomaMindCtx *m, SomaRouterCtx *router, 
                    OosiV3GenCtx *core, SomaLogicCtx *logic,
                    CellionEngine *vision,
                    CollectivionEngine *comm,
                    GhostEngine *ghost);

/* Create a new cognitive object from a prompt or observation */
SomaMindObject* soma_mind_spawn(SomaMindCtx *m, const char *name, float priority);

/* Find an object by ID */
SomaMindObject* soma_mind_find(SomaMindCtx *m, uint32_t id);

/* --- The Cognitive Loop --- */

/* 
 * One 'pulse' of the SomaMind. 
 * 1. Select the highest priority object.
 * 2. Feed its latent state to the Neural Core.
 * 3. Evaluate halting/continuation.
 * 4. Trigger tools if needed.
 * Returns number of objects processed.
 */
int soma_mind_pulse(SomaMindCtx *m);

/* Inject physical telemetry to influence halting adaptivity */
void soma_mind_update_telemetry(SomaMindCtx *m, float temp, float pressure);

/* --- Tool Integration --- */

/* Resolve a tool result into the mind */
void soma_mind_provide_result(SomaMindCtx *m, const SomaMindToolResult *res);

#ifdef __cplusplus
}
#endif
