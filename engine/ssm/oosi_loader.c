// oosi_loader.c — OOSI v2 (OO SSM Int8) weight loader — bare-metal freestanding
//
// Loads the OOSI v2 binary produced by oo-model/scripts/export_int8.py.
// Zero-copy: all weight pointers point directly into the cold memory zone.
// No libc, no malloc, no file I/O — UEFI compatible.

#include "oosi_loader.h"

// ============================================================
// Bounds-checked cursor advance (returns pointer, advances cursor)
// Returns NULL and sets *err on overflow.
// ============================================================
static const void *oosi_advance(
    const uint8_t **cursor,
    const uint8_t  *base,
    uint64_t        base_size,
    uint64_t        n_bytes,
    SsmStatus      *err)
{
    if (*err != SSM_OK) return *cursor;
    uint64_t offset = (uint64_t)(*cursor - base);
    if (offset + n_bytes > base_size) {
        *err = SSM_ERR_BADWEIGHT;
        return *cursor;
    }
    const void *result = *cursor;
    *cursor += n_bytes;
    return result;
}

// ============================================================
// oosi_load — parse OOSI v2 binary (zero copy)
// ============================================================
SsmStatus oosi_load(
    OosiWeights *out,
    const void  *data,
    uint64_t     data_size)
{
    if (!out || !data || data_size < sizeof(OosiFileHeader))
        return SSM_ERR_BADWEIGHT;

    const OosiFileHeader *hdr = (const OosiFileHeader *)data;

    if (hdr->magic   != OOSI_MAGIC)   return SSM_ERR_BADWEIGHT;
    if (hdr->version != OOSI_VERSION)  return SSM_ERR_BADCONFIG;

    int d_model    = (int)hdr->d_model;
    int n_layer    = (int)hdr->n_layer;
    int d_state    = (int)hdr->d_state;
    int expand     = (int)hdr->expand;
    int vocab_size = (int)hdr->vocab_size;
    int halt_d_in  = (int)hdr->halt_d_input;

    if (d_model <= 0 || n_layer <= 0 || vocab_size <= 0) return SSM_ERR_BADCONFIG;
    if (n_layer > SSM_MAX_LAYERS)   return SSM_ERR_OVERFLOW;
    if (d_model > SSM_MAX_D_MODEL)  return SSM_ERR_OVERFLOW;

    int d_inner  = d_model * expand;
    int dt_rank  = (d_model + 15) / 16;   // ceil(d_model / 16)
    int x_out    = dt_rank + 2 * d_state; // rows of x_proj

    out->header       = *hdr;
    out->d_model      = d_model;
    out->n_layer      = n_layer;
    out->d_state      = d_state;
    out->d_conv       = (int)hdr->d_conv;
    out->expand       = expand;
    out->vocab_size   = vocab_size;
    out->halt_d_input = halt_d_in;
    out->data         = data;
    out->data_size    = data_size;

    const uint8_t *base   = (const uint8_t *)data;
    const uint8_t *cursor = base + sizeof(OosiFileHeader);
    SsmStatus err = SSM_OK;

    // --------------------------------------------------------
    // Per-layer: x_proj (int8+scale) + dt_proj (int8+scale) + dt_proj_bias (f32)
    // --------------------------------------------------------
    for (int l = 0; l < n_layer && err == SSM_OK; l++) {
        OosiLayerWeightsQ8 *lw = &out->layers[l];
        lw->d_inner    = d_inner;
        lw->dt_rank    = dt_rank;
        lw->d_state    = d_state;
        lw->x_out_rows = x_out;

        // x_proj: scale[x_out] f32 + int8[x_out * d_inner]
        lw->x_proj_scale = (const ssm_f32 *)oosi_advance(
            &cursor, base, data_size, (uint64_t)x_out * sizeof(ssm_f32), &err);
        lw->x_proj_q8    = (const ssm_q8  *)oosi_advance(
            &cursor, base, data_size, (uint64_t)x_out * d_inner, &err);

        // dt_proj: scale[d_inner] f32 + int8[d_inner * dt_rank]
        lw->dt_proj_scale = (const ssm_f32 *)oosi_advance(
            &cursor, base, data_size, (uint64_t)d_inner * sizeof(ssm_f32), &err);
        lw->dt_proj_q8    = (const ssm_q8  *)oosi_advance(
            &cursor, base, data_size, (uint64_t)d_inner * dt_rank, &err);

        // dt_proj_bias: float32[d_inner]
        lw->dt_proj_bias = (const ssm_f32 *)oosi_advance(
            &cursor, base, data_size, (uint64_t)d_inner * sizeof(ssm_f32), &err);
    }

    if (err != SSM_OK) return err;

    // --------------------------------------------------------
    // Embedding: scale[vocab_size] f32 + int8[vocab_size * d_model]
    // --------------------------------------------------------
    out->embed_scale = (const ssm_f32 *)oosi_advance(
        &cursor, base, data_size, (uint64_t)vocab_size * sizeof(ssm_f32), &err);
    out->embed_q8    = (const ssm_q8  *)oosi_advance(
        &cursor, base, data_size, (uint64_t)vocab_size * d_model, &err);

    if (err != SSM_OK) return err;

    // --------------------------------------------------------
    // HaltingHead: all remaining bytes are float32 halt weights
    // Layout (sorted keys from Python):
    //   net.0.bias    [512]
    //   net.0.weight  [512 * halt_d_in]
    //   net.2.bias    [64]
    //   net.2.weight  [64 * 512]
    //   net.4.bias    [1]
    //   net.4.weight  [1 * 64]
    // --------------------------------------------------------
    uint64_t remaining = data_size - (uint64_t)(cursor - base);
    out->halt_data  = (const ssm_f32 *)cursor;
    out->halt_bytes = (uint32_t)remaining;
    // Advance cursor to end (no bounds error — it's the tail)
    cursor += remaining;
    (void)cursor;

    return SSM_OK;
}

// ============================================================
// oosi_validate
// ============================================================
SsmStatus oosi_validate(const OosiWeights *w) {
    if (!w || !w->data) return SSM_ERR_BADWEIGHT;
    if (w->d_model <= 0 || w->vocab_size <= 0 || w->n_layer <= 0)
        return SSM_ERR_BADCONFIG;
    if (w->n_layer > SSM_MAX_LAYERS)  return SSM_ERR_OVERFLOW;
    if (!w->embed_scale || !w->embed_q8) return SSM_ERR_BADWEIGHT;
    for (int l = 0; l < w->n_layer; l++) {
        const OosiLayerWeightsQ8 *lw = &w->layers[l];
        if (!lw->x_proj_scale || !lw->x_proj_q8)   return SSM_ERR_BADWEIGHT;
        if (!lw->dt_proj_scale || !lw->dt_proj_q8)  return SSM_ERR_BADWEIGHT;
        if (!lw->dt_proj_bias)                       return SSM_ERR_BADWEIGHT;
    }
    return SSM_OK;
}

// ============================================================
// oosi_dequant_row — dequantize one int8 row into float32
// out[j] = (float)q8_row[j] * scale
// ============================================================
void oosi_dequant_row(ssm_f32 *out, const ssm_q8 *q8_row, ssm_f32 scale, int n_cols) {
    for (int j = 0; j < n_cols; j++)
        out[j] = (ssm_f32)q8_row[j] * scale;
}

// ============================================================
// oosi_embed_lookup — dequantize embedding row for token_id
// out[d_model] = embed_q8[token_id, :] * embed_scale[token_id]
// ============================================================
void oosi_embed_lookup(
    const OosiWeights *w,
    int     token_id,
    ssm_f32 *out)
{
    int d = w->d_model;
    ssm_f32 scale = w->embed_scale[token_id];
    const ssm_q8 *row = w->embed_q8 + (uint64_t)token_id * d;
    oosi_dequant_row(out, row, scale, d);
}

// ============================================================
// oosi_dequant_matvec — int8 matrix × float32 vector
//
// Computes: y[i] = sum_j( (float)q8[i*in_cols + j] * scale[i] * x[j] )
//         = scale[i] * dot(q8_row_i, x)
//
// This is the key op for x_proj and dt_proj during inference.
// ============================================================
void oosi_dequant_matvec(
    const ssm_q8   *q8,
    const ssm_f32  *scale,
    const ssm_f32  *x,
    ssm_f32        *y,
    int out_rows,
    int in_cols)
{
    for (int i = 0; i < out_rows; i++) {
        const ssm_q8 *row = q8 + (uint64_t)i * in_cols;
        ssm_f32 acc = 0.0f;
        for (int j = 0; j < in_cols; j++)
            acc += (ssm_f32)row[j] * x[j];
        y[i] = acc * scale[i];
    }
}

// ============================================================
// oosi_print_config — debug print via callback
// ============================================================
static void oosi_itoa(int v, char *buf, int n) {
    if (n <= 0) return;
    if (v < 0) { if (n > 1) { buf[0] = '-'; oosi_itoa(-v, buf+1, n-1); } return; }
    int i = 0;
    do { buf[i++] = '0' + (v % 10); v /= 10; } while (v && i < n-1);
    buf[i] = 0;
    for (int a = 0, b = i-1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
}

void oosi_print_config(const OosiWeights *w, OosiPrintFn fn) {
    if (!w || !fn) return;
    char buf[32];
    fn("[OOSI] d_model="); oosi_itoa(w->d_model, buf, 16); fn(buf);
    fn(" n_layer=");       oosi_itoa(w->n_layer,  buf, 16); fn(buf);
    fn(" vocab=");         oosi_itoa(w->vocab_size, buf, 16); fn(buf);
    fn(" d_inner=");       oosi_itoa(w->d_model * w->expand, buf, 16); fn(buf);
    fn(" halt_d_in=");     oosi_itoa(w->halt_d_input, buf, 16); fn(buf);
    fn(" halt_bytes=");    oosi_itoa((int)w->halt_bytes, buf, 16); fn(buf);
    fn("\r\n");
}
