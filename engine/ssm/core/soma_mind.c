#include "soma_mind.h"
#include "soma_dream.h"
#include "oo_quantum_rng.h"
#include "oo_family.h"
#include "../../../oo-modules/overclock-engine/core/overclock.h"
#include "../../../united-baremetal/include/united_bus.h"
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
                    GhostEngine *ghost,
                    Genomion *genomion,
                    MercatorCtx *mercator,
                    SymbionCtx *symbion,
                    MnemionCtx *mnemion,
                    MorphionCtx *morphion) {
    if (!m) return;
    memset(m, 0, sizeof(SomaMindCtx));
    m->router = router;
    m->core = core;
    m->logic = logic;
    m->vision = vision;
    m->comm = comm;
    m->ghost = ghost;
    m->genomion = genomion;
    m->mercator = mercator;
    m->symbion = symbion;
    m->mnemion = mnemion;
    m->morphion = morphion;
    
    // Phase 9: Hardware Awareness (Symbion Scan)
    if (m->symbion) {
        symbion_scan_body(m->symbion);
        _log_causal(0, "hardware_symbiosis_active");
    }

    m->next_object_id = 1;
    m->active = 1;
    m->energy_budget = 1.0f;
    
    /* V3 Plasticity Default */
    m->plasticity.learning_rate = 0.001f;
    m->plasticity.weight_adj = 0.0f;
    
    /* Octopoda Default: 5% basal activity */
    m->trickle_charge_rate = 0.05f;
    m->basal_pulses = 0;
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

    /* V2 Fusion Gate: Check object name/context for logical keywords */
    SomaDomain d = soma_classify_domain(obj->name, (int)strlen(obj->name));
    
    // Personality influence: Bias towards Solar or Lunar based on DNA
    float bias = m->dna ? m->dna->cognition_bias : 0.5f;
    
    if (d == SOMA_DOMAIN_MATH || d == SOMA_DOMAIN_SYSTEM || d == SOMA_DOMAIN_POLICY) {
        // If high Solar bias, always use Solar for technical tasks
        if (bias < 0.8f) return SOMA_ENGINE_SOLAR;
    }
    
    // If very high Lunar bias (> 0.8), even technical tasks might get 'intuitive' treatment
    if (bias > 0.8f && d != SOMA_DOMAIN_SYSTEM) return SOMA_ENGINE_LUNAR;

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
    
    // Initialisation Unique de la Famille Diop (Le Conseil des IAs)
    static int family_awake = 0;
    if (!family_awake) {
        oo_family_init();
        family_awake = 1;
    }

    /* Read hardware temperature and update telemetry */
    float current_temp = (float)overclock_get_cpu_temp();
    soma_mind_update_telemetry(m, current_temp, m->halt_pressure);
    
    /* 0. United Bus Absorption (The Nervous System) */
    globule_t inbox[8];
    int received = united_bus_absorb(ORGAN_SOMA, inbox, 8);
    for (int i = 0; i < received; i++) {
        if (inbox[i].type == GLOBULE_WHITE) {
            _log_causal(0, "immune_alert_absorbed");
            SomaMindObject *sec = soma_mind_spawn(m, "RESOLVE_SECURITY_THREAT", 20.0f);
            if (sec) sec->priority = 50.0f;
            m->halt_pressure += 0.5f;
        }
    }

    /* 0.5 Mercatorion Pulse (The Financial Instinct) */
    if (m->mercator) {
        mercator_pulse(m->mercator);
        if (m->router) {
            mercator_compute_signals(m->mercator, m->router->last_saliency);
        }
    }

    /* 0.6 Mnemion Pulse (The Dreaming Process) */
    if (m->mnemion && (m->energy_budget < 0.2f || m->core_temp > 80.0f)) {
        mnemion_start_dream(m->mnemion, m);
        m->energy_budget += 0.5f; // Dreaming restores energy/focus
    }

    /* 1. Cost-Aware Selection */
    /* Basal Metabolism: Soma-Breath */
    if (m->breath_rate > 0.001f) {
        m->basal_pulses++;
        if (m->basal_pulses % 20 == 0) {
            _log_causal(0, "soma_breath_pulse");
            // Inject hardware jitter into cognition via Genomion
            if (m->genomion) {
                 // Micro-drift of attention
            }
        }
    }
    
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
    
    if (!best) {
        return 1; 
    }
    m->total_pulses++;
    
    /* 2. Fusion Gate Arbitration */
    SomaMindEngineType engine = soma_mind_fusion_gate(m, best);
    
    /* V2 Causal Journal: Log the start of this pulse */
    _log_causal(best->id, engine == SOMA_ENGINE_SOLAR ? "solar_start" : "lunar_start");
    
    /* V3: Cognitive Simulation (The Dream) */
    SomaDreamSummary ds = soma_dream_pulse(m, best);
    if (ds.result != DREAM_SUCCESS) {
        _log_causal(best->id, "dream_fail");
        
        // Personality influence: Risk tolerance affects penalty
        float penalty = m->genomion ? (1.5f - m->genomion->risk_tolerance) : 0.5f;
        if (penalty < 0.1f) penalty = 0.1f;
        
        best->priority *= penalty; /* Penalize dangerous thoughts */
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
            
            /* United Bus: Persist logical derivation */
            globule_t g;
            g.type = GLOBULE_RED;
            g.source_organ = ORGAN_SOMA;
            g.target_organ = ORGAN_MEMORY;
            g.payload_addr = (void*)best->name;
            g.payload_size = (uint32_t)strlen(best->name);
            united_bus_pump(g);

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
            
            /* Phase D Integration: Persist thought via United Bus */
            globule_t g;
            g.type = GLOBULE_RED;
            g.source_organ = ORGAN_SOMA;
            g.target_organ = ORGAN_MEMORY;
            g.payload_addr = (void*)best->name; // Send the thought name/content
            g.payload_size = (uint32_t)strlen(best->name);
            united_bus_pump(g);

            /* V3: Auto-trigger reflection */
            soma_mind_reflect(m, best->id);
            
            /* United Bus: Telepathic Broadcast (to ORGAN_COLLECTIVE) */
            if (best->priority > 1.0f) {
                globule_t b;
                b.type = GLOBULE_RED;
                b.source_organ = ORGAN_SOMA;
                b.target_organ = ORGAN_COLLECTIVE;
                b.payload_addr = (void*)best->name;
                b.payload_size = (uint32_t)strlen(best->name);
                united_bus_pump(b);
                _log_causal(best->id, "bus_broadcast_sent");
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
    
    /* Octopoda: Trickle-charge scaling for basal activity */
    if (obj_id == 0) {
        delta *= m->trickle_charge_rate;
    }
    
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
