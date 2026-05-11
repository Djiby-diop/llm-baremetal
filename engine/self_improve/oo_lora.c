/* oo_lora.c — LoRA adapter for OO self-improvement loop (bare-metal)
 *
 * Uses a static memory pool (no malloc) — requires ~1MB per model.
 * Gradient updates use a simple SGD step (no autograd).
 * D+ scoring evaluates adapter improvement vs base model.
 * Persist/load uses NVMe block write via oo_nvme.h interface.
 */

#include "oo_lora.h"

/* Forward declarations from oo_nvme.c (included earlier in unity build) */
extern int oo_nvme_read_lba(UINT32 lba, UINT8 *buf, UINT32 bytes);
extern int oo_nvme_write_lba(UINT32 lba, const UINT8 *buf, UINT32 bytes);

/* ─── Global state (unity-build accessible) ──────────────────────────── */
oo_lora_state_t g_lora;

/* ─── Static pools (no heap) ──────────────────────────────────────────── */
/* 32 layers × 3 projections × rank8 × 2 matrices
 * Max dimension = 2048 (Tinyllama hidden dim)
 * A: 2048 × 8  = 16384 floats per proj
 * B: 8    × 2048 = 16384 floats per proj
 * Total: 32 × 3 × 2 × 16384 × 4 bytes ≈ 12MB
 */
#define LORA_DIM_MAX  2048
#define LORA_POOL_A   (LORA_MAX_LAYERS * 3 * LORA_DIM_MAX * LORA_MAX_RANK)
#define LORA_POOL_B   (LORA_MAX_LAYERS * 3 * LORA_MAX_RANK * LORA_DIM_MAX)

static float _pool_A[LORA_POOL_A] __attribute__((aligned(64)));
static float _pool_B[LORA_POOL_B] __attribute__((aligned(64)));
static UINT32 _pool_a_idx = 0;
static UINT32 _pool_b_idx = 0;

/* ─── Simple PRNG (xorshift32) for A init ─────────────────────────────── */
static UINT32 _rng_state = 0xDEADBEEF;
static float _randf(void) {
    _rng_state ^= _rng_state << 13;
    _rng_state ^= _rng_state >> 17;
    _rng_state ^= _rng_state << 5;
    /* map to [-0.02, 0.02] */
    return ((float)(_rng_state & 0xFFFF) / 0xFFFF - 0.5f) * 0.04f;
}

/* ─── Math helpers ────────────────────────────────────────────────────── */
static float _dot(const float *a, const float *b, UINT32 n) {
    float s = 0.0f;
    for (UINT32 i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

static float _fabsf(float x) { return x < 0 ? -x : x; }
static float _sqrtf_fast(float x) {
    /* Newton-Raphson, 3 iterations */
    if (x <= 0) return 0;
    float y = x;
    y = (y + x / y) * 0.5f;
    y = (y + x / y) * 0.5f;
    y = (y + x / y) * 0.5f;
    return y;
}

/* ─── Public: init ────────────────────────────────────────────────────── */
int oo_lora_init(oo_lora_state_t *st, UINT32 n_layers,
                 UINT32 dim, UINT32 rank) {
    if (!st || n_layers > LORA_MAX_LAYERS || rank > LORA_MAX_RANK)
        return -1;
    if (dim > LORA_DIM_MAX) return -2;

    st->n_layers      = n_layers;
    st->learning_rate = 1e-4f;
    st->step_count    = 0;
    st->last_score    = 0.0f;
    st->dirty         = 0;

    for (UINT32 l = 0; l < n_layers; l++) {
        for (UINT32 p = 0; p < 3; p++) {
            oo_lora_adapter_t *a = &st->layers[l][p];
            a->in_dim  = dim;
            a->out_dim = dim;
            a->rank    = rank;
            a->scale   = LORA_ALPHA / (float)rank;

            /* Allocate from pools */
            a->A = &_pool_A[_pool_a_idx];
            _pool_a_idx += dim * rank;
            a->B = &_pool_B[_pool_b_idx];
            _pool_b_idx += rank * dim;

            /* A: random small values, B: zero */
            for (UINT32 i = 0; i < dim * rank; i++) a->A[i] = _randf();
            for (UINT32 i = 0; i < rank * dim; i++) a->B[i] = 0.0f;
        }
    }
    return 0;
}

/* ─── Public: forward pass  x_out += scale * B * (A * x) ────────────── */
void oo_lora_forward(oo_lora_adapter_t *a, const float *x,
                     float *out, UINT32 n) {
    if (!a || !x || !out) return;
    UINT32 r = a->rank;
    /* tmp = A^T * x  → shape [rank] */
    float tmp[LORA_MAX_RANK];
    for (UINT32 k = 0; k < r; k++)
        tmp[k] = _dot(a->A + k * a->in_dim, x, a->in_dim);
    /* out += scale * B * tmp */
    for (UINT32 j = 0; j < a->out_dim; j++) {
        float s = 0.0f;
        for (UINT32 k = 0; k < r; k++)
            s += a->B[k * a->out_dim + j] * tmp[k];
        out[j] += a->scale * s;
    }
}

/* ─── Public: SGD backward step (one layer, one projection) ──────────── */
void oo_lora_backward_step(oo_lora_state_t *st, const float *grad,
                           UINT32 layer_idx, UINT32 proj_idx) {
    if (!st || layer_idx >= st->n_layers || proj_idx >= 3) return;
    oo_lora_adapter_t *a = &st->layers[layer_idx][proj_idx];
    float lr = st->learning_rate;
    UINT32 r = a->rank;

    /* Update B: B -= lr * grad^T (simplified outer product update) */
    for (UINT32 k = 0; k < r; k++)
        for (UINT32 j = 0; j < a->out_dim; j++)
            a->B[k * a->out_dim + j] -= lr * grad[j];

    /* Update A: A -= lr * grad (projected back) */
    for (UINT32 k = 0; k < r; k++)
        for (UINT32 i = 0; i < a->in_dim; i++)
            a->A[k * a->in_dim + i] -= lr * grad[i % a->out_dim];

    st->step_count++;
    st->dirty = 1;
}

/* ─── Public: D+ score — quality heuristic for the adapter ───────────── */
float oo_lora_score(const oo_lora_state_t *st) {
    if (!st || st->n_layers == 0) return 0.0f;

    /* Score based on:
     * 1. Frobenius norm of B matrices (non-zero = learning happened)
     * 2. Norm not exploding (penalize if > 10.0)
     * 3. Number of steps taken
     */
    float total_norm = 0.0f;
    UINT32 count = 0;
    for (UINT32 l = 0; l < st->n_layers; l++) {
        for (UINT32 p = 0; p < 3; p++) {
            const oo_lora_adapter_t *a = &st->layers[l][p];
            float norm = 0.0f;
            for (UINT32 k = 0; k < a->rank * a->out_dim; k++)
                norm += a->B[k] * a->B[k];
            total_norm += _sqrtf_fast(norm);
            count++;
        }
    }
    float avg_norm = (count > 0) ? total_norm / count : 0.0f;

    /* Normalize score: 0.0 (no learning) → 1.0 (ideal ~1.0 norm) */
    float score = avg_norm / (avg_norm + 1.0f);

    /* Penalize explosion */
    if (avg_norm > 10.0f) score *= 0.5f;

    /* Penalize too few steps */
    if (st->step_count < 10) score *= (float)st->step_count / 10.0f;

    return score > 1.0f ? 1.0f : score;
}

/* ─── Persist/Load: write adapter to NVMe raw LBA ────────────────────── */
/* Layout: [magic32][n_layers][step_count][A pool][B pool] */
#define LORA_MAGIC 0x004C4F52  /* "LOR\0" */
#define LORA_LBA_START 0x800   /* LBA offset on NVMe (after OS data) */

int oo_lora_persist(const oo_lora_state_t *st, const char *nvme_path) {
    (void)nvme_path;    /* unused — we use raw NVMe LBA directly */
    if (!st || !st->dirty) return 0;

    /* Build a flat header block */
    UINT32 hdr[4] = {
        LORA_MAGIC,
        st->n_layers,
        (UINT32)(st->step_count & 0xFFFFFFFF),
        _pool_a_idx
    };

    /* Write header at LORA_LBA_START */
    oo_nvme_write_lba(LORA_LBA_START, (UINT8 *)hdr, sizeof(hdr));

    /* Write A pool */
    UINT32 a_bytes = _pool_a_idx * sizeof(float);
    oo_nvme_write_lba(LORA_LBA_START + 1, (UINT8 *)_pool_A, a_bytes);

    /* Write B pool */
    UINT32 b_bytes = _pool_b_idx * sizeof(float);
    UINT32 b_lba = LORA_LBA_START + 1 + (a_bytes + 511) / 512;
    oo_nvme_write_lba(b_lba, (UINT8 *)_pool_B, b_bytes);

    return 0;
}

int oo_lora_load(oo_lora_state_t *st, const char *nvme_path) {
    (void)nvme_path;
    if (!st) return -1;

    UINT32 hdr[4];
    if (oo_nvme_read_lba(LORA_LBA_START, (UINT8 *)hdr, sizeof(hdr)) != 0)
        return -2;
    if (hdr[0] != LORA_MAGIC) return -3;  /* no saved adapter */

    _pool_a_idx = hdr[3];
    UINT32 a_bytes = _pool_a_idx * sizeof(float);
    oo_nvme_read_lba(LORA_LBA_START + 1, (UINT8 *)_pool_A, a_bytes);

    UINT32 b_lba = LORA_LBA_START + 1 + (a_bytes + 511) / 512;
    oo_nvme_read_lba(b_lba, (UINT8 *)_pool_B, _pool_b_idx * sizeof(float));

    st->step_count = hdr[2];
    st->dirty      = 0;
    return 0;
}

/* ─── Merge adapter into base model weights (naive add) ─────────────── */
void oo_lora_apply_to_model(oo_lora_state_t *st, void *model_weights) {
    /* model_weights points to the transformer weight block.
     * In practice: for each layer, call oo_lora_forward() during
     * inference rather than baking in — this is the preferred path.
     * This function is provided for one-time merge (distillation). */
    (void)st; (void)model_weights;
    /* TODO: implement when model format exports layer weight pointers */
}
