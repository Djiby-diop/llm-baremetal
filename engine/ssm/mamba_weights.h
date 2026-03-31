#ifndef MAMBA_WEIGHTS_H
#define MAMBA_WEIGHTS_H

// Mamba weight loader — freestanding bare-metal
// Loads Mamba 130M weights from a flat binary blob in COLD memory zone.
// No malloc, no libc. Pointer arithmetic into a pre-loaded buffer.

#include "ssm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Mamba model config (stored at start of weight file)
// ============================================================
typedef struct {
    uint32_t magic;        // 0x4D414D42 ('MAMB') or 0x67676666 (gguf)
    uint32_t version;
    int32_t  d_model;
    int32_t  n_layers;
    int32_t  vocab_size;
    int32_t  d_state;      // SSM state size N (default 16)
    int32_t  d_conv;       // conv kernel size (default 4)
    int32_t  expand;       // d_inner = d_model * expand (default 2)
    int32_t  dt_rank;      // delta_t rank (default ceil(d_model/16))
    int32_t  pad[7];       // future use
} MambaFileHeader;

#define MAMBA_MAGIC      0x4D414D42u  // 'MAMB'
#define MAMBA_VERSION_1  1

// ============================================================
// Load weights from a flat binary (exported from Python)
// All weight tensors are stored in F32 row-major order.
//
// data:      pointer to raw weight bytes (in COLD zone)
// data_size: byte size of the buffer
// weights:   output structure (pointers into data — zero copy)
//
// Returns SSM_OK or error. Does NOT allocate memory.
// ============================================================
SsmStatus mamba_weights_load(
    MambaWeights   *weights,
    const void     *data,
    uint64_t        data_size
);

// ============================================================
// Validate weights (sanity check before inference)
// ============================================================
SsmStatus mamba_weights_validate(const MambaWeights *weights);

// ============================================================
// Compute total weight size for a given config
// (Useful to verify the file is complete before loading)
// ============================================================
uint64_t mamba_weights_expected_size(
    int d_model, int n_layers, int vocab_size,
    int d_state, int d_conv, int expand, int dt_rank
);

// ============================================================
// Print config (debug, uses bare-metal print callback)
// ============================================================
typedef void (*MambaPrintFn)(const char *msg);
void mamba_weights_print_config(const MambaWeights *w, MambaPrintFn print_fn);

#ifdef __cplusplus
}
#endif

#endif // MAMBA_WEIGHTS_H
