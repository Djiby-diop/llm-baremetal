// Mamba weight loader — freestanding bare-metal
// Zero-copy: all weight pointers point directly into the cold memory zone.

#include "mamba_weights.h"

// ============================================================
// Layout helpers
// ============================================================
static uint64_t f32_bytes(uint64_t n) { return n * sizeof(ssm_f32); }

// Advance pointer by n f32 elements, check bounds
static const ssm_f32 *ptr_advance(
    const ssm_f32 **cursor,
    const ssm_f32  *base,
    uint64_t        base_size,
    uint64_t        n_elems,
    SsmStatus      *err)
{
    if (*err != SSM_OK) return *cursor;
    uint64_t offset = (uint64_t)(*cursor - base) * sizeof(ssm_f32);
    uint64_t need   = n_elems * sizeof(ssm_f32);
    if (offset + need > base_size) {
        *err = SSM_ERR_BADWEIGHT;
        return *cursor;
    }
    const ssm_f32 *result = *cursor;
    *cursor += n_elems;
    return result;
}

// ============================================================
// Expected weight size calculation
// ============================================================
uint64_t mamba_weights_expected_size(
    int d_model, int n_layers, int vocab_size,
    int d_state, int d_conv, int expand, int dt_rank)
{
    int d_inner = d_model * expand;
    uint64_t sz = sizeof(MambaFileHeader);

    // Embedding table
    sz += f32_bytes((uint64_t)vocab_size * d_model);

    // Per-layer weights
    for (int l = 0; l < n_layers; l++) {
        sz += f32_bytes((uint64_t)2 * d_inner * d_model); // in_proj
        sz += f32_bytes((uint64_t)d_inner * d_conv);       // conv_weight
        sz += f32_bytes((uint64_t)d_inner);                // conv_bias
        sz += f32_bytes((uint64_t)(dt_rank + 2*d_state) * d_inner); // x_proj
        sz += f32_bytes((uint64_t)d_inner * dt_rank);     // dt_proj_weight
        sz += f32_bytes((uint64_t)d_inner);                // dt_proj_bias
        sz += f32_bytes((uint64_t)d_inner * d_state);     // A_log
        sz += f32_bytes((uint64_t)d_inner);                // D
        sz += f32_bytes((uint64_t)d_model * d_inner);     // out_proj
        sz += f32_bytes((uint64_t)d_model);               // norm_weight
    }

    // Final norm + lm_head
    sz += f32_bytes((uint64_t)d_model);                   // final_norm
    sz += f32_bytes((uint64_t)vocab_size * d_model);      // lm_head

    return sz;
}

// ============================================================
// Load weights — zero copy into cold memory
// ============================================================
SsmStatus mamba_weights_load(
    MambaWeights *weights,
    const void   *data,
    uint64_t      data_size)
{
    if (!weights || !data || data_size < sizeof(MambaFileHeader))
        return SSM_ERR_BADWEIGHT;

    const MambaFileHeader *hdr = (const MambaFileHeader *)data;

    // Validate magic
    if (hdr->magic != MAMBA_MAGIC) return SSM_ERR_BADWEIGHT;

    int d_model    = hdr->d_model;
    int n_layers   = hdr->n_layers;
    int vocab_size = hdr->vocab_size;
    int d_state    = hdr->d_state  > 0 ? hdr->d_state  : 16;
    int d_conv     = hdr->d_conv   > 0 ? hdr->d_conv   : 4;
    int expand     = hdr->expand   > 0 ? hdr->expand   : 2;
    int dt_rank    = hdr->dt_rank  > 0 ? hdr->dt_rank  : ((d_model + 15) / 16);
    int d_inner    = d_model * expand;

    if (d_model <= 0 || n_layers <= 0 || vocab_size <= 0) return SSM_ERR_BADCONFIG;
    if (n_layers > SSM_MAX_LAYERS) return SSM_ERR_OVERFLOW;
    if (d_model  > SSM_MAX_D_MODEL) return SSM_ERR_OVERFLOW;

    weights->d_model         = d_model;
    weights->vocab_size      = vocab_size;
    weights->n_layers_actual = n_layers;
    weights->dtype           = SSM_DTYPE_F32;
    weights->data            = data;
    weights->data_size       = data_size;

    // Cursor starts after header
    const ssm_f32 *cursor = (const ssm_f32 *)((const char*)data + sizeof(MambaFileHeader));
    const ssm_f32 *base   = (const ssm_f32 *)data;
    uint64_t base_size    = data_size;
    SsmStatus err         = SSM_OK;

    // Embedding table
    weights->embed = ptr_advance(&cursor, base, base_size,
                                 (uint64_t)vocab_size * d_model, &err);

    // Per-layer weights
    for (int l = 0; l < n_layers && err == SSM_OK; l++) {
        MambaLayerWeights *lw = &weights->layers[l];
        lw->d_model  = d_model;
        lw->d_inner  = d_inner;
        lw->d_state  = d_state;
        lw->d_conv   = d_conv;
        lw->dt_rank  = dt_rank;

        lw->in_proj        = ptr_advance(&cursor, base, base_size, (uint64_t)2*d_inner*d_model, &err);
        lw->conv_weight    = ptr_advance(&cursor, base, base_size, (uint64_t)d_inner*d_conv, &err);
        lw->conv_bias      = ptr_advance(&cursor, base, base_size, (uint64_t)d_inner, &err);
        lw->x_proj         = ptr_advance(&cursor, base, base_size, (uint64_t)(dt_rank+2*d_state)*d_inner, &err);
        lw->dt_proj_weight = ptr_advance(&cursor, base, base_size, (uint64_t)d_inner*dt_rank, &err);
        lw->dt_proj_bias   = ptr_advance(&cursor, base, base_size, (uint64_t)d_inner, &err);
        lw->A_log          = ptr_advance(&cursor, base, base_size, (uint64_t)d_inner*d_state, &err);
        lw->D              = ptr_advance(&cursor, base, base_size, (uint64_t)d_inner, &err);
        lw->out_proj       = ptr_advance(&cursor, base, base_size, (uint64_t)d_model*d_inner, &err);
        lw->norm_weight    = ptr_advance(&cursor, base, base_size, (uint64_t)d_model, &err);
    }

    // Final norm + lm head
    if (err == SSM_OK) {
        weights->final_norm = ptr_advance(&cursor, base, base_size, (uint64_t)d_model, &err);
        weights->lm_head    = ptr_advance(&cursor, base, base_size, (uint64_t)vocab_size*d_model, &err);
    }

    return err;
}

// ============================================================
// Validate
// ============================================================
SsmStatus mamba_weights_validate(const MambaWeights *w) {
    if (!w || !w->data) return SSM_ERR_BADWEIGHT;
    if (w->d_model <= 0 || w->vocab_size <= 0) return SSM_ERR_BADCONFIG;
    if (w->n_layers_actual <= 0 || w->n_layers_actual > SSM_MAX_LAYERS) return SSM_ERR_OVERFLOW;
    if (!w->embed || !w->final_norm || !w->lm_head) return SSM_ERR_BADWEIGHT;
    for (int l = 0; l < w->n_layers_actual; l++) {
        const MambaLayerWeights *lw = &w->layers[l];
        if (!lw->in_proj || !lw->A_log || !lw->D || !lw->out_proj) return SSM_ERR_BADWEIGHT;
    }
    return SSM_OK;
}

// ============================================================
// Print config (debug)
// ============================================================
static void mamba_itoa(int v, char *buf, int n) {
    if (n <= 0) return;
    if (v < 0) { buf[0] = '-'; mamba_itoa(-v, buf+1, n-1); return; }
    int i = 0;
    do { buf[i++] = '0' + (v % 10); v /= 10; } while (v && i < n-1);
    buf[i] = 0;
    // Reverse
    for (int a = 0, b = i-1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
}

void mamba_weights_print_config(const MambaWeights *w, MambaPrintFn fn) {
    if (!w || !fn) return;
    char buf[64];
    fn("[Mamba] d_model="); mamba_itoa(w->d_model, buf, 16); fn(buf);
    fn(" n_layers="); mamba_itoa(w->n_layers_actual, buf, 16); fn(buf);
    fn(" vocab="); mamba_itoa(w->vocab_size, buf, 16); fn(buf);
    if (w->n_layers_actual > 0) {
        fn(" d_inner="); mamba_itoa(w->layers[0].d_inner, buf, 16); fn(buf);
        fn(" d_state="); mamba_itoa(w->layers[0].d_state, buf, 16); fn(buf);
    }
    fn("\r\n");
}
