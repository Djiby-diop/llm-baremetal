#include "soma_mind.h"
#include "soma_dream.h"
#include <string.h>

#define SOMA_MIND_JOURNAL_MAX   64

typedef struct {
    uint32_t ts;
    uint32_t object_id;
    char     action[32];
} SomaCausalEntry;

static SomaCausalEntry g_journal[SOMA_MIND_JOURNAL_MAX];
static uint32_t        g_journal_head = 0;

static void _log_causal(uint32_t id, const char *action) {
    g_journal[g_journal_head].ts = 0; // TODO: Get TSC
    g_journal[g_journal_head].object_id = id;
    strncpy(g_journal[g_journal_head].action, action, 31);
    g_journal_head = (g_journal_head + 1) % SOMA_MIND_JOURNAL_MAX;
}

void soma_mind_init(SomaMindCtx *m, SomaRouterCtx *router, 
                    OosiV3GenCtx *core, SomaLogicCtx *logic,
                    CellionEngine *vision,
                    CollectivionEngine *comm,
                    GhostEngine *ghost) {
    if (!m) return;
    memset(m, 0, sizeof(SomaMindCtx));
    m->router = router;
    m->core = core;
    m->logic = logic;
    m->vision = vision;
    m->comm = comm;
    m->ghost = ghost;
    m->next_object_id = 1;
    m->active = 1;
    m->energy_budget = 1.0f;
    
    /* V3 Plasticity Default */
    m->plasticity.learning_rate = 0.001f;
    m->plasticity.weight_adj = 0.0f;
}

SomaMindObject* soma_mind_spawn(SomaMindCtx *m, const char *name, float priority) {
    if (!m) return NULL;
    
    for (int i = 0; i < SOMA_MIND_OBJECT_MAX; i++) {
        if (m->objects[i].state == SOMA_OBJ_FREE) {
            SomaMindObject *obj = &m->objects[i];
            obj->id = m->next_object_id++;
            strncpy(obj->name, name, SOMA_MIND_NAME_LEN - 1);
            obj->state = SOMA_OBJ_THINKING;
            obj->priority = priority;
            obj->cost_estimate = 0.1f;
            memset(obj->latent, 0, sizeof(obj->latent));
            memset(obj->links, 0, sizeof(obj->links));
            obj->parent_id = 0;
            return obj;
        }
    }
    return NULL;
}

SomaMindObject* soma_mind_find(SomaMindCtx *m, uint32_t id) {
    if (!m || id == 0) return NULL;
    for (int i = 0; i < SOMA_MIND_OBJECT_MAX; i++) {
        if (m->objects[i].id == id && m->objects[i].state != SOMA_OBJ_FREE) {
            return &m->objects[i];
        }
    }
    return NULL;
}

void soma_mind_link(SomaMindCtx *m, uint32_t parent_id, uint32_t child_id) {
    SomaMindObject *p = soma_mind_find(m, parent_id);
    SomaMindObject *c = soma_mind_find(m, child_id);
    if (!p || !c) return;
    
    c->parent_id = parent_id;
    for (int i = 0; i < SOMA_MIND_LINK_MAX; i++) {
        if (p->links[i] == 0) {
            p->links[i] = child_id;
            break;
        }
    }
}

SomaMindEngineType soma_mind_fusion_gate(SomaMindCtx *m, SomaMindObject *obj) {
    if (!m->router) return SOMA_ENGINE_LUNAR;
    
    /* V2 Fusion Gate: Check object name/context for logical keywords */
    SomaDomain d = soma_classify_domain(obj->name, (int)strlen(obj->name));
    
    if (d == SOMA_DOMAIN_MATH || d == SOMA_DOMAIN_SYSTEM || d == SOMA_DOMAIN_POLICY) {
        return SOMA_ENGINE_SOLAR;
    }
    return SOMA_ENGINE_LUNAR;
}

void soma_mind_update_telemetry(SomaMindCtx *m, float temp, float pressure) {
    if (!m) return;
    m->core_temp = temp;
    m->halt_pressure = pressure;
    
    if (m->core) {
        float base_threshold = 0.7f;
        float adj = (temp > 45.0f) ? (temp - 45.0f) * 0.02f : 0.0f;
        adj += pressure * 0.15f;
        float new_thresh = base_threshold - adj;
        if (new_thresh < 0.2f) new_thresh = 0.2f;
        m->core->halt_threshold = new_thresh;
    }
}

int soma_mind_pulse(SomaMindCtx *m) {
    if (!m || !m->active) return 0;
    
    /* 1. Cost-Aware Selection */
    SomaMindObject *best = NULL;
    for (int i = 0; i < SOMA_MIND_OBJECT_MAX; i++) {
        if (m->objects[i].state == SOMA_OBJ_THINKING) {
            /* V2 Priority: priority / (cost + 0.1) */
            float score = m->objects[i].priority / (m->objects[i].cost_estimate + 0.01f);
            if (!best || score > (best->priority / (best->cost_estimate + 0.01f))) {
                best = &m->objects[i];
            }
        }
    }
    
    if (!best) return 0;
    m->total_pulses++;
    
    /* 2. Fusion Gate Arbitration */
    SomaMindEngineType engine = soma_mind_fusion_gate(m, best);
    
    /* V2 Causal Journal: Log the start of this pulse */
    _log_causal(best->id, engine == SOMA_ENGINE_SOLAR ? "solar_start" : "lunar_start");
    
    /* V3: Cognitive Simulation (The Dream) */
    SomaDreamSummary ds = soma_dream_pulse(m, best);
    if (ds.result != DREAM_SUCCESS) {
        _log_causal(best->id, "dream_fail");
        best->priority *= 0.5f; /* Penalize dangerous thoughts */
        best->state = SOMA_OBJ_DORMANT;
        return 1;
    }
    _log_causal(best->id, "dream_success");
    
    if (engine == SOMA_ENGINE_SOLAR && m->logic) {
        /* Solar Engine: Logical Syllogism */
        SomaLogicResult lr = soma_logic_scan(m->logic, best->name);
        if (lr.triggered) {
            _log_causal(best->id, "solar_derive");
            if (lr.contradiction) {
                _log_causal(best->id, "solar_conflict");
                /* V2: Contradiction Handling - Spawn emergency object */
                SomaMindObject *conflict = soma_mind_spawn(m, "RESOLVE_CONTRADICTION", 10.0f);
                if (conflict) {
                    soma_mind_link(m, best->id, conflict->id);
                    strncpy(conflict->name, lr.contradiction_fact, SOMA_MIND_NAME_LEN - 1);
                }
            }
            best->state = SOMA_OBJ_RESOLVED;
            if (lr.derived_count > 0) {
                SomaMindObject *child = soma_mind_spawn(m, lr.derived[0], best->priority * 1.1f);
                if (child) soma_mind_link(m, best->id, child->id);
            }
        } else {
            /* Fallback to Lunar if Solar fails to derive */
            engine = SOMA_ENGINE_LUNAR;
        }
    }
    
    if (engine == SOMA_ENGINE_LUNAR && m->core) {
        /* Lunar Engine: Neural Generation (SSM) */
        OosiV3HaltResult r = oosi_v3_forward_one(m->core, 1);
        if (r.halted) {
            best->state = SOMA_OBJ_RESOLVED;
            /* V3: Auto-trigger reflection */
            soma_mind_reflect(m, best->id);
            
            /* V1 Organ: Telepathic Broadcast */
            if (m->comm && m->ghost && best->priority > 1.0f) {
                collectivion_broadcast_thought(m->comm, m->ghost, 
                                             best->id, best->name, 
                                             best->priority, best->success_rate);
                _log_causal(best->id, "telepathy_broadcast");
            }
        } else {
            best->priority *= 0.98f;
            best->cost_estimate += 0.05f; 
            if (best->priority < 0.05f) best->state = SOMA_OBJ_DORMANT;
        }
    }
    
    if (best->state == SOMA_OBJ_CRITIC) {
        /* V3 Critic Pulse: Evaluate the object's outcome */
        _log_causal(best->id, "critic_pulse");
        
        float success = 0.8f;
        
        /* V1 Organ: Visual Verification */
        if (m->vision && m->vision->cortex.buffer) {
            CellionPerceptionResult pr;
            if (cellion_perceive(m->vision, NULL, 0, &pr)) {
                _log_causal(best->id, "visual_verify");
                /* If vision detects a critical error, lower success rate */
                if (pr.objects_detected > 0) success = 0.2f;
            }
        }
        
        best->success_rate = success;
        soma_mind_apply_feedback(m, best->id, best->success_rate);
        best->state = SOMA_OBJ_RESOLVED;
    }
    
    return 1;
}

void soma_mind_apply_feedback(SomaMindCtx *m, uint32_t obj_id, float success) {
    if (!m) return;
    m->plasticity.total_updates++;
    
    /* Plasticity: adjust global weight bias based on success */
    float delta = (success - 0.5f) * m->plasticity.learning_rate;
    m->plasticity.weight_adj += delta;
    
    _log_causal(obj_id, "plasticity_apply");
}

void soma_mind_reflect(SomaMindCtx *m, uint32_t obj_id) {
    SomaMindObject *obj = soma_mind_find(m, obj_id);
    if (obj && obj->state == SOMA_OBJ_RESOLVED) {
        obj->state = SOMA_OBJ_CRITIC;
        obj->priority += 0.5f; /* Give it a bit more priority to finish reflection */
    }
}

void soma_mind_provide_result(SomaMindCtx *m, const SomaMindToolResult *res) {
    if (!m || !res) return;
    SomaMindObject *obj = soma_mind_find(m, res->object_id);
    if (obj) {
        obj->priority += 2.0f;
        obj->state = SOMA_OBJ_THINKING;
        obj->cost_estimate = 0.1f; /* Reset cost after external feedback */
    }
}
