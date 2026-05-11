#pragma once
/* oo_lora.h — LoRA adapter integration for self-improvement loop
 * Connects evolution-baremetal genetic mutation pipeline to the
 * inference loop in soma_boot.c — closing the self-improvement cycle.
 *
 * Architecture:
 *   Inference (forward pass) → delta_activations
 *   → LoRA adapter (rank-8 A×B matrices applied in-place)
 *   → D+ scoring → if score > threshold → persist adapter to NVMe
 *   → next boot loads updated adapter
 */

#include <efi.h>
#include <efilib.h>

/* LoRA hyper-parameters */
#define LORA_MAX_RANK       8     /* low-rank decomposition dimension */
#define LORA_MAX_LAYERS    32     /* max transformer layers to adapt */
#define LORA_ALPHA          1.0f  /* scaling factor */
#define LORA_DROPOUT        0.0f  /* no dropout on bare-metal */
#define LORA_SCORE_MIN      0.65f /* D+ minimum score to accept a patch */

/* Adapter for one projection matrix (e.g. Wq, Wk, Wv) */
typedef struct {
    UINT32  in_dim;
    UINT32  out_dim;
    UINT32  rank;
    float  *A;   /* [in_dim × rank]  — random init */
    float  *B;   /* [rank  × out_dim] — zero init   */
    float   scale;
} oo_lora_adapter_t;

/* Full LoRA state (one per model) */
typedef struct {
    oo_lora_adapter_t layers[LORA_MAX_LAYERS][3]; /* 3 = Wq,Wk,Wv */
    UINT32  n_layers;
    float   learning_rate;
    UINT64  step_count;
    float   last_score;
    UINT8   dirty;   /* 1 = needs persist to NVMe */
} oo_lora_state_t;

/* Public API */
int   oo_lora_init(oo_lora_state_t *st, UINT32 n_layers,
                   UINT32 dim, UINT32 rank);
void  oo_lora_forward(oo_lora_adapter_t *a, const float *x,
                      float *out, UINT32 n);
void  oo_lora_backward_step(oo_lora_state_t *st, const float *grad,
                            UINT32 layer_idx, UINT32 proj_idx);
float oo_lora_score(const oo_lora_state_t *st);   /* D+ heuristic */
int   oo_lora_persist(const oo_lora_state_t *st,
                      const char *nvme_path);       /* save to NVMe */
int   oo_lora_load(oo_lora_state_t *st,
                   const char *nvme_path);           /* load from NVMe */
void  oo_lora_apply_to_model(oo_lora_state_t *st,
                             void *model_weights);   /* merge into base */
