#pragma once
/* oo_evolution_bridge.h — Connects evolution-baremetal genetic engine
 * to the LoRA self-improvement loop.
 *
 * Data flow:
 *   evolution-baremetal receives scored gradient signals
 *   → validates via DNA identity check
 *   → if accepted: triggers oo_lora_backward_step()
 *   → if fitness > threshold: persists to NVMe (permanent genome)
 *
 * The "genes" in this system are LoRA B-matrix deltas.
 * The "DNA hash" is the SHA-like identity of the weight blob.
 */

#include <efi.h>
#include <efilib.h>
#include "../self_improve/oo_lora.h"

/* Fitness threshold: 0-100, above this = permanent genome */
#define EVO_FITNESS_THRESHOLD  75

typedef struct {
    UINT32 generation;    /* incremented each accepted mutation */
    UINT32 rejected;      /* rejected mutation count (immune rejections) */
    UINT32 accepted;      /* accepted mutation count */
    float  fitness_score; /* last D+ fitness score */
} oo_evo_stats_t;

/* Public API */
void oo_evo_init(void);

/* Called after inference: apply scored gradient to LoRA adapter.
 * grad: error signal for layer l, projection p (q=0, k=1, v=2).
 * score: 0.0-1.0 quality signal from D+ engine.
 * Returns: 1 if mutation was accepted, 0 if rejected by immune system. */
int  oo_evo_apply_gradient(UINT32 layer_idx, UINT32 proj_idx,
                           const float *grad, float score);

/* Called periodically: evaluate fitness and persist if good enough */
void oo_evo_evaluate(void);

/* Get current stats */
const oo_evo_stats_t *oo_evo_stats(void);

/* Print + REPL */
void oo_evo_print(void);
int  oo_evo_repl_cmd(const char *cmd);
     /* /evol_status /evol_stats /evol_step /evol_genome */
