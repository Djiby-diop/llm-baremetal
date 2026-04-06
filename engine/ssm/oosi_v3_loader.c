// oosi_v3_loader.c — Parse OOSI v3 binary (zero-copy weight mapping)
//
// Freestanding C11: no libc, no UEFI headers. Safe for bare-metal.

#include "oosi_v3_loader.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Minimal uint32 read helper (little-endian, no alignment requirement) ──
static uint32_t _rd32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// ── Advance pointer safely ─────────────────────────────────────────────────
static const uint8_t *_adv(const uint8_t *p, const uint8_t *end, uint64_t n,
                            SsmStatus *st)
{
    if (*st != SSM_OK) return p;
    if ((uint64_t)(end - p) < n) { *st = SSM_ERR_BADCONFIG; return p; }
    return p + n;
}

// ── Size helpers ───────────────────────────────────────────────────────────
#define SZ_F32(n)  ((uint64_t)(n) * sizeof(ssm_f32))
#define SZ_Q8(n)   ((uint64_t)(n) * sizeof(ssm_q8))

// ============================================================
// oosi_v3_load
// ============================================================
SsmStatus oosi_v3_load(OosiV3Weights *out, const void *buf, uint64_t len) {
    if (!out || !buf || len < sizeof(OosiV3Header)) return SSM_ERR_BADCONFIG;

    const uint8_t *p   = (const uint8_t *)buf;
    const uint8_t *end = p + len;
    SsmStatus st = SSM_OK;

    // ── Header ────────────────────────────────────────────────────────────
    uint32_t magic   = _rd32(p + 0);
    uint32_t version = _rd32(p + 4);
    if (magic != OOSI_V3_MAGIC)    return SSM_ERR_BADCONFIG;
    if (version != OOSI_V3_VERSION) return SSM_ERR_BADCONFIG;

    out->header.magic        = magic;
    out->header.version      = version;
    out->header.d_model      = _rd32(p + 8);
    out->header.n_layer      = _rd32(p + 12);
    out->header.d_state      = _rd32(p + 16);
    out->header.d_conv       = _rd32(p + 20);
    out->header.expand       = _rd32(p + 24);
    out->header.vocab_size   = _rd32(p + 28);
    out->header.dt_rank      = _rd32(p + 32);
    out->header.halt_d_input = _rd32(p + 36);
    p += 40;

    // ── Derived dims ──────────────────────────────────────────────────────
    out->d_model      = (int)out->header.d_model;
    out->n_layer      = (int)out->header.n_layer;
    out->d_state      = (int)out->header.d_state;
    out->d_conv       = (int)out->header.d_conv;
    out->expand       = (int)out->header.expand;
    out->vocab_size   = (int)out->header.vocab_size;
    out->dt_rank      = (int)out->header.dt_rank;
    out->d_inner      = out->d_model * out->expand;
    out->halt_d_input = (int)out->header.halt_d_input;
    out->raw_buf      = buf;
    out->raw_len      = len;

    int D = out->d_model, N = out->n_layer, S = out->d_state;
    int Di = out->d_inner, Dc = out->d_conv, Dt = out->dt_rank;
    int V = out->vocab_size;

    if (N > SSM_MAX_LAYERS) return SSM_ERR_BADCONFIG;

    // ── Per-layer weights ─────────────────────────────────────────────────
    for (int l = 0; l < N; l++) {
        OosiV3LayerWeights *lw = &out->layers[l];

        // norm_weight f32[d_model]
        lw->norm_weight = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(D), &st);

        // in_proj scale f32[2*d_inner] + int8[2*d_inner * d_model]
        lw->in_proj_scale = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(2*Di), &st);
        lw->in_proj_q8 = (const ssm_q8 *)p;
        p = _adv(p, end, SZ_Q8(2*Di * D), &st);

        // conv f32[d_inner * d_conv] + bias f32[d_inner]
        lw->conv_weight = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(Di * Dc), &st);
        lw->conv_bias = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(Di), &st);

        // x_proj scale f32[dt_rank+2*d_state] + int8[(dt_rank+2*d_state)*d_inner]
        int x_out = Dt + 2 * S;
        lw->x_out_rows = x_out;
        lw->x_proj_scale = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(x_out), &st);
        lw->x_proj_q8 = (const ssm_q8 *)p;
        p = _adv(p, end, SZ_Q8(x_out * Di), &st);

        // dt_proj scale f32[d_inner] + int8[d_inner * dt_rank]
        lw->dt_proj_scale = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(Di), &st);
        lw->dt_proj_q8 = (const ssm_q8 *)p;
        p = _adv(p, end, SZ_Q8(Di * Dt), &st);
        lw->dt_proj_bias = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(Di), &st);

        // A_log f32[d_inner * d_state], D f32[d_inner]
        lw->A_log = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(Di * S), &st);
        lw->D = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(Di), &st);

        // out_proj scale f32[d_model] + int8[d_model * d_inner]
        lw->out_proj_scale = (const ssm_f32 *)p;
        p = _adv(p, end, SZ_F32(D), &st);
        lw->out_proj_q8 = (const ssm_q8 *)p;
        p = _adv(p, end, SZ_Q8(D * Di), &st);

        if (st != SSM_OK) return st;
    }

    // ── Global weights ────────────────────────────────────────────────────
    // final_norm f32[d_model]
    out->final_norm = (const ssm_f32 *)p;
    p = _adv(p, end, SZ_F32(D), &st);

    // embed scale f32[vocab] + int8[vocab * d_model]
    out->embed_scale = (const ssm_f32 *)p;
    p = _adv(p, end, SZ_F32(V), &st);
    out->embed_q8 = (const ssm_q8 *)p;
    p = _adv(p, end, SZ_Q8((uint64_t)V * D), &st);

    // lm_head scale f32[vocab] + int8[vocab * d_model]
    out->lm_head_scale = (const ssm_f32 *)p;
    p = _adv(p, end, SZ_F32(V), &st);
    out->lm_head_q8 = (const ssm_q8 *)p;
    p = _adv(p, end, SZ_Q8((uint64_t)V * D), &st);

    // HaltingHead: until NEGA magic or end of buffer
    if (st == SSM_OK && p < end) {
        // Check if we have a NEGA trailer for precomputed neg_exp_A
        // The HaltingHead MLP size is known: 512*halt_d + 512 + 64*512 + 64 + 64 + 1
        // = halt_d*512 + 512 + 32768 + 64 + 64 + 1 = halt_d*512 + 33409 floats
        // But we parse adaptively: scan for NEGA magic (0x4E454741)
        int halt_d = (int)out->header.halt_d_input;
        uint64_t halt_floats = (uint64_t)halt_d * 512 + 512
                             + (uint64_t)64 * 512 + 64
                             + 64 + 1;
        uint64_t halt_size = halt_floats * sizeof(ssm_f32);
        if ((uint64_t)(end - p) >= halt_size) {
            out->halt_data  = (const ssm_f32 *)p;
            out->halt_bytes = (uint32_t)halt_size;
            p += halt_size;
        } else {
            out->halt_data  = (const ssm_f32 *)p;
            out->halt_bytes = (uint32_t)(end - p);
            p = end;
        }
    } else {
        out->halt_data  = NULL;
        out->halt_bytes = 0;
    }

    // NEGA trailer: precomputed -exp(A_log), magic 0x4E454741
    out->neg_exp_A_data = NULL;
    if (st == SSM_OK && p + 4 <= end) {
        uint32_t nega_magic = _rd32(p);
        if (nega_magic == 0x4E454741u) {  // "NEGA"
            p += 4;
            uint64_t nega_size = (uint64_t)N * Di * S * sizeof(ssm_f32);
            if ((uint64_t)(end - p) >= nega_size) {
                out->neg_exp_A_data = (const ssm_f32 *)p;
                p += nega_size;
            }
        }
    }

    return st;
}

// ============================================================
// oosi_v3_validate
// ============================================================
SsmStatus oosi_v3_validate(const OosiV3Weights *w) {
    if (!w) return SSM_ERR_BADCONFIG;
    if (w->d_model <= 0 || w->n_layer <= 0 || w->vocab_size <= 0)
        return SSM_ERR_BADCONFIG;
    if (w->d_inner != w->d_model * w->expand) return SSM_ERR_BADCONFIG;
    if (!w->final_norm || !w->embed_q8 || !w->lm_head_q8)
        return SSM_ERR_BADCONFIG;
    for (int l = 0; l < w->n_layer; l++) {
        const OosiV3LayerWeights *lw = &w->layers[l];
        if (!lw->norm_weight || !lw->in_proj_q8 || !lw->out_proj_q8)
            return SSM_ERR_BADCONFIG;
    }
    return SSM_OK;
}

// ============================================================
// oosi_v3_print_config
// ============================================================
static void _v3_print_int(OosiV3PrintFn fn, const char *label, int val) {
    char buf[64];
    int pos = 0;
    while (label[pos]) buf[pos++] = label[pos];
    buf[pos++] = '=';
    // simple itoa
    if (val == 0) { buf[pos++] = '0'; }
    else {
        char tmp[16]; int n = 0, v = val;
        while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
        for (int i = n-1; i >= 0; i--) buf[pos++] = tmp[i];
    }
    buf[pos++] = '\n'; buf[pos] = 0;
    fn(buf);
}

void oosi_v3_print_config(const OosiV3Weights *w, OosiV3PrintFn fn) {
    if (!w || !fn) return;
    fn("[OOSI v3] Config:\n");
    _v3_print_int(fn, "  d_model",    w->d_model);
    _v3_print_int(fn, "  n_layer",    w->n_layer);
    _v3_print_int(fn, "  d_inner",    w->d_inner);
    _v3_print_int(fn, "  d_state",    w->d_state);
    _v3_print_int(fn, "  d_conv",     w->d_conv);
    _v3_print_int(fn, "  dt_rank",    w->dt_rank);
    _v3_print_int(fn, "  vocab_size", w->vocab_size);
}
