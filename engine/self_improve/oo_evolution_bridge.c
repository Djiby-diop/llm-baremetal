/* oo_evolution_bridge.c — Connects evolution-baremetal to LoRA backward
 * Validates mutations through DNA identity, applies accepted ones via SGD.
 */

#include "oo_evolution_bridge.h"

/* Forward declarations from evolution-baremetal (linked via organs) */
extern void evolution_init(void);
extern int  evolution_apply_mutation(const UINT8 *lora_weights, UINT32 size);
extern void evolution_evaluate_fitness(UINT32 pattern_id, UINT8 success_rate);

/* External g_lora defined in oo_lora.c */
extern oo_lora_state_t g_lora;

/* ─── Global stats ────────────────────────────────────────────────────── */
static oo_evo_stats_t _stats;

/* ─── Simple float abs ────────────────────────────────────────────────── */
static float _evo_fabs(float x) { return x < 0 ? -x : x; }

/* ─── Pack gradient into byte blob for DNA signing ───────────────────── */
/* We take only the sign+magnitude of the first 64 elements to keep it small */
#define GRAD_BLOB_SIZE 64
static UINT8 _grad_blob[GRAD_BLOB_SIZE];

static void _pack_grad_blob(const float *grad, UINT32 n) {
    UINT32 take = n < GRAD_BLOB_SIZE ? n : GRAD_BLOB_SIZE;
    for (UINT32 i = 0; i < take; i++) {
        float g = grad[i];
        /* Encode as sign bit (7) + magnitude (0-127) in 0-255 range */
        UINT8 sign = (g < 0) ? 0x80 : 0x00;
        float mag = _evo_fabs(g);
        if (mag > 1.0f) mag = 1.0f;
        _grad_blob[i] = sign | (UINT8)(mag * 127.0f);
    }
}

/* ─── Public: init ────────────────────────────────────────────────────── */
void oo_evo_init(void) {
    _stats.generation    = 0;
    _stats.rejected      = 0;
    _stats.accepted      = 0;
    _stats.fitness_score = 0.0f;
    evolution_init();
}

/* ─── Public: apply gradient ──────────────────────────────────────────── */
int oo_evo_apply_gradient(UINT32 layer_idx, UINT32 proj_idx,
                          const float *grad, float score) {
    if (!grad || layer_idx >= g_lora.n_layers || proj_idx >= 3) return 0;

    /* Only process if D+ score meets minimum */
    if (score < LORA_SCORE_MIN) {
        _stats.rejected++;
        return 0;
    }

    /* Pack gradient into byte blob for DNA validation */
    oo_lora_adapter_t *a = &g_lora.layers[layer_idx][proj_idx];
    _pack_grad_blob(grad, a->out_dim);

    /* DNA identity check via evolution-baremetal immune system */
    int immune_ok = evolution_apply_mutation(_grad_blob, GRAD_BLOB_SIZE);
    if (immune_ok != 0) {
        _stats.rejected++;
        return 0;
    }

    /* Mutation accepted — apply SGD step to LoRA adapter */
    oo_lora_backward_step(&g_lora, grad, layer_idx, proj_idx);

    _stats.accepted++;
    _stats.generation++;
    _stats.fitness_score = score;

    return 1;
}

/* ─── Public: evaluate fitness and persist if good ───────────────────── */
void oo_evo_evaluate(void) {
    float score = oo_lora_score(&g_lora);
    _stats.fitness_score = score;

    /* Convert to 0-100 for evolution fitness API */
    UINT8 fitness_pct = (UINT8)(score * 100.0f);
    evolution_evaluate_fitness(_stats.generation, fitness_pct);

    /* If fitness above threshold, persist to NVMe (permanent genome) */
    if (fitness_pct >= EVO_FITNESS_THRESHOLD && g_lora.dirty) {
        oo_lora_persist(&g_lora, (void *)0);
    }
}

/* ─── Public: stats ───────────────────────────────────────────────────── */
const oo_evo_stats_t *oo_evo_stats(void) { return &_stats; }
