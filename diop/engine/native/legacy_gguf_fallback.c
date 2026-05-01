#include "legacy_gguf_fallback.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    GGUF_TYPE_UINT8 = 0,
    GGUF_TYPE_INT8 = 1,
    GGUF_TYPE_UINT16 = 2,
    GGUF_TYPE_INT16 = 3,
    GGUF_TYPE_UINT32 = 4,
    GGUF_TYPE_INT32 = 5,
    GGUF_TYPE_FLOAT32 = 6,
    GGUF_TYPE_BOOL = 7,
    GGUF_TYPE_STRING = 8,
    GGUF_TYPE_ARRAY = 9,
    GGUF_TYPE_UINT64 = 10,
    GGUF_TYPE_INT64 = 11,
    GGUF_TYPE_FLOAT64 = 12
} gguf_type;

typedef enum {
    GGML_TYPE_F32 = 0,
    GGML_TYPE_F16 = 1,
    GGML_TYPE_Q4_0 = 2,
    GGML_TYPE_Q4_1 = 3,
    GGML_TYPE_Q5_0 = 6,
    GGML_TYPE_Q5_1 = 7,
    GGML_TYPE_Q8_0 = 8
} ggml_type;

typedef enum {
    ROLE_NONE = 0,
    ROLE_TOK_EMBD,
    ROLE_OUTPUT,
    ROLE_RMS_FINAL,
    ROLE_ATTN_NORM,
    ROLE_WQ,
    ROLE_WK,
    ROLE_WV,
    ROLE_WO,
    ROLE_FFN_NORM,
    ROLE_FFN_GATE,
    ROLE_FFN_DOWN,
    ROLE_FFN_UP
} tensor_role;

static int read_exact(FILE *f, void *dst, size_t nbytes) {
    if (!f || !dst) return 0;
    return fread(dst, 1, nbytes, f) == nbytes;
}

static int skip_exact(FILE *f, uint64_t nbytes) {
    if (!f) return 0;
    return _fseeki64(f, (long long)nbytes, SEEK_CUR) == 0;
}

static int read_u32(FILE *f, uint32_t *out) {
    return out && read_exact(f, out, sizeof(*out));
}

static int read_u64(FILE *f, uint64_t *out) {
    return out && read_exact(f, out, sizeof(*out));
}

static int key_equals(const char *key, uint32_t key_len, const char *literal) {
    uint32_t i = 0;
    if (!key || !literal) return 0;
    while (literal[i] && i < key_len) {
        if (key[i] != literal[i]) return 0;
        i++;
    }
    return literal[i] == 0 && i == key_len;
}

static int read_string_trunc(FILE *f, char *dst, size_t capacity) {
    uint64_t length = 0;
    uint64_t to_read = 0;
    if (!dst || capacity == 0) return 0;
    dst[0] = 0;
    if (!read_u64(f, &length)) return 0;
    to_read = length;
    if (to_read > (uint64_t)(capacity - 1)) to_read = (uint64_t)(capacity - 1);
    if (to_read > 0 && !read_exact(f, dst, (size_t)to_read)) return 0;
    dst[(size_t)to_read] = 0;
    if (length > to_read) return skip_exact(f, length - to_read);
    return 1;
}

static int skip_value(FILE *f, gguf_type t) {
    uint32_t elem_t_u32 = 0;
    uint64_t count = 0;
    uint64_t elem_size = 0;
    switch (t) {
        case GGUF_TYPE_UINT8:
        case GGUF_TYPE_INT8:
        case GGUF_TYPE_BOOL:
            return skip_exact(f, 1);
        case GGUF_TYPE_UINT16:
        case GGUF_TYPE_INT16:
            return skip_exact(f, 2);
        case GGUF_TYPE_UINT32:
        case GGUF_TYPE_INT32:
        case GGUF_TYPE_FLOAT32:
            return skip_exact(f, 4);
        case GGUF_TYPE_UINT64:
        case GGUF_TYPE_INT64:
        case GGUF_TYPE_FLOAT64:
            return skip_exact(f, 8);
        case GGUF_TYPE_STRING:
            if (!read_u64(f, &count)) return 0;
            return skip_exact(f, count);
        case GGUF_TYPE_ARRAY:
            if (!read_u32(f, &elem_t_u32)) return 0;
            if (!read_u64(f, &count)) return 0;
            if ((gguf_type)elem_t_u32 == GGUF_TYPE_STRING) {
                uint64_t i = 0;
                for (i = 0; i < count; i++) {
                    if (!skip_value(f, GGUF_TYPE_STRING)) return 0;
                }
                return 1;
            }
            switch ((gguf_type)elem_t_u32) {
                case GGUF_TYPE_UINT8:
                case GGUF_TYPE_INT8:
                case GGUF_TYPE_BOOL: elem_size = 1; break;
                case GGUF_TYPE_UINT16:
                case GGUF_TYPE_INT16: elem_size = 2; break;
                case GGUF_TYPE_UINT32:
                case GGUF_TYPE_INT32:
                case GGUF_TYPE_FLOAT32: elem_size = 4; break;
                case GGUF_TYPE_UINT64:
                case GGUF_TYPE_INT64:
                case GGUF_TYPE_FLOAT64: elem_size = 8; break;
                default: return 0;
            }
            return skip_exact(f, count * elem_size);
        default:
            return 0;
    }
}

static int capture_integer_value(FILE *f, gguf_type t, uint64_t *out) {
    uint32_t v32 = 0;
    uint64_t v64 = 0;
    if (!out) return 0;
    if (t == GGUF_TYPE_UINT32) {
        if (!read_u32(f, &v32)) return 0;
        *out = (uint64_t)v32;
        return 1;
    }
    if (t == GGUF_TYPE_UINT64) {
        if (!read_u64(f, &v64)) return 0;
        *out = v64;
        return 1;
    }
    return 0;
}

static int parse_u32_ascii(const char *s, int *index, uint32_t *out) {
    uint32_t value = 0;
    int i = *index;
    int has_digit = 0;
    if (!s || !index || !out) return 0;
    while (s[i] >= '0' && s[i] <= '9') {
        has_digit = 1;
        value = (value * 10u) + (uint32_t)(s[i] - '0');
        i++;
    }
    if (!has_digit) return 0;
    *index = i;
    *out = value;
    return 1;
}

static int str_eq_full(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int parse_tensor_role(const char *name, uint32_t *out_layer, tensor_role *out_role) {
    int i = 0;
    uint32_t layer = 0;
    const char *rest = NULL;
    if (!name || !out_layer || !out_role) return 0;
    *out_layer = 0;
    *out_role = ROLE_NONE;

    if (str_eq_full(name, "token_embd.weight")) { *out_role = ROLE_TOK_EMBD; return 1; }
    if (str_eq_full(name, "output.weight")) { *out_role = ROLE_OUTPUT; return 1; }
    if (str_eq_full(name, "output_norm.weight") || str_eq_full(name, "norm.weight")) { *out_role = ROLE_RMS_FINAL; return 1; }

    if (!(name[0] == 'b' && name[1] == 'l' && name[2] == 'k' && name[3] == '.')) return 0;
    i = 4;
    if (!parse_u32_ascii(name, &i, &layer)) return 0;
    if (name[i] != '.') return 0;
    rest = name + i + 1;

    if (str_eq_full(rest, "attn_norm.weight")) { *out_layer = layer; *out_role = ROLE_ATTN_NORM; return 1; }
    if (str_eq_full(rest, "ffn_norm.weight")) { *out_layer = layer; *out_role = ROLE_FFN_NORM; return 1; }
    if (str_eq_full(rest, "attn_q.weight")) { *out_layer = layer; *out_role = ROLE_WQ; return 1; }
    if (str_eq_full(rest, "attn_k.weight")) { *out_layer = layer; *out_role = ROLE_WK; return 1; }
    if (str_eq_full(rest, "attn_v.weight")) { *out_layer = layer; *out_role = ROLE_WV; return 1; }
    if (str_eq_full(rest, "attn_output.weight")) { *out_layer = layer; *out_role = ROLE_WO; return 1; }
    if (str_eq_full(rest, "ffn_gate.weight")) { *out_layer = layer; *out_role = ROLE_FFN_GATE; return 1; }
    if (str_eq_full(rest, "ffn_down.weight")) { *out_layer = layer; *out_role = ROLE_FFN_DOWN; return 1; }
    if (str_eq_full(rest, "ffn_up.weight")) { *out_layer = layer; *out_role = ROLE_FFN_UP; return 1; }
    return 0;
}

static int type_supported_for_llama(uint32_t tensor_type) {
    return tensor_type == GGML_TYPE_F32 ||
           tensor_type == GGML_TYPE_F16 ||
           tensor_type == GGML_TYPE_Q4_0 ||
           tensor_type == GGML_TYPE_Q4_1 ||
           tensor_type == GGML_TYPE_Q5_0 ||
           tensor_type == GGML_TYPE_Q5_1 ||
           tensor_type == GGML_TYPE_Q8_0;
}

static void register_tensor_role(TensorPlanSummary *plan, tensor_role role, uint32_t layer, uint64_t offset, uint32_t tensor_type) {
    if (!plan) return;
    plan->recognized_tensor_count++;
    if (offset > plan->max_tensor_offset) plan->max_tensor_offset = offset;
    if (layer + 1u > plan->detected_layers) plan->detected_layers = layer + 1u;
    if (type_supported_for_llama(tensor_type)) plan->supported_type_count++;
    if (tensor_type == GGML_TYPE_Q8_0) plan->q8_0_count++;
    switch (role) {
        case ROLE_TOK_EMBD: plan->token_embedding_present = 1; break;
        case ROLE_OUTPUT: plan->output_present = 1; break;
        case ROLE_RMS_FINAL: plan->rms_final_present = 1; break;
        case ROLE_ATTN_NORM: plan->attn_norm_count++; break;
        case ROLE_WQ: plan->wq_count++; break;
        case ROLE_WK: plan->wk_count++; break;
        case ROLE_WV: plan->wv_count++; break;
        case ROLE_WO: plan->wo_count++; break;
        case ROLE_FFN_NORM: plan->ffn_norm_count++; break;
        case ROLE_FFN_GATE: plan->ffn_gate_count++; break;
        case ROLE_FFN_DOWN: plan->ffn_down_count++; break;
        case ROLE_FFN_UP: plan->ffn_up_count++; break;
        default: break;
    }
}

static void finalize_tensor_plan(TensorPlanSummary *plan, const GgufSummary *summary) {
    uint32_t expected_layers = 0;
    if (!plan || !summary) return;
    expected_layers = (uint32_t)summary->block_count;
    if (expected_layers == 0) expected_layers = plan->detected_layers;
    plan->fully_mapped_llama =
        plan->token_embedding_present &&
        plan->rms_final_present &&
        expected_layers > 0 &&
        plan->attn_norm_count == expected_layers &&
        plan->wq_count == expected_layers &&
        plan->wk_count == expected_layers &&
        plan->wv_count == expected_layers &&
        plan->wo_count == expected_layers &&
        plan->ffn_norm_count == expected_layers &&
        plan->ffn_gate_count == expected_layers &&
        plan->ffn_down_count == expected_layers &&
        plan->ffn_up_count == expected_layers &&
        plan->supported_type_count == plan->recognized_tensor_count;
}

int read_summary_from_legacy_gguf(
    const char *model_path,
    GgufSummary *summary,
    TensorPlanSummary *tensor_plan,
    char *error_message,
    size_t error_capacity
) {
    FILE *f = NULL;
    uint8_t magic[4];
    uint64_t i = 0;

    if (!model_path || !summary || !tensor_plan) return 0;
    f = fopen(model_path, "rb");
    if (!f) {
        if (error_message && error_capacity > 0) snprintf(error_message, error_capacity, "Unable to open model file");
        return 0;
    }

    memset(summary, 0, sizeof(*summary));
    memset(tensor_plan, 0, sizeof(*tensor_plan));

    if (_fseeki64(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    summary->file_size = (uint64_t)_ftelli64(f);
    if (_fseeki64(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

    if (!read_exact(f, magic, sizeof(magic))) { fclose(f); return 0; }
    if (!(magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' && magic[3] == 'F')) {
        if (error_message && error_capacity > 0) snprintf(error_message, error_capacity, "Unsupported file: missing GGUF magic");
        fclose(f);
        return 0;
    }

    if (!read_u32(f, &summary->version) || !read_u64(f, &summary->tensor_count) || !read_u64(f, &summary->kv_count)) {
        fclose(f);
        return 0;
    }

    for (i = 0; i < summary->kv_count; i++) {
        uint32_t key_len = 0, keep = 0, value_type = 0;
        char key_buf[192];
        gguf_type t;

        if (!read_u32(f, &key_len) || key_len == 0 || key_len > 4096) { fclose(f); return 0; }
        keep = key_len;
        if (keep > (uint32_t)(sizeof(key_buf) - 1)) keep = (uint32_t)(sizeof(key_buf) - 1);
        if (keep > 0 && !read_exact(f, key_buf, keep)) { fclose(f); return 0; }
        key_buf[keep] = 0;
        if (key_len > keep && !skip_exact(f, (uint64_t)(key_len - keep))) { fclose(f); return 0; }
        if (!read_u32(f, &value_type)) { fclose(f); return 0; }
        t = (gguf_type)value_type;

        if (key_equals(key_buf, keep, "general.architecture") && t == GGUF_TYPE_STRING) { if (!read_string_trunc(f, summary->architecture, sizeof(summary->architecture))) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "general.name") && t == GGUF_TYPE_STRING) { if (!read_string_trunc(f, summary->name, sizeof(summary->name))) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "tokenizer.ggml.model") && t == GGUF_TYPE_STRING) { if (!read_string_trunc(f, summary->tokenizer_model, sizeof(summary->tokenizer_model))) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "general.file_type")) { if (!capture_integer_value(f, t, &summary->file_type) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "llama.context_length")) { if (!capture_integer_value(f, t, &summary->context_length) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "llama.embedding_length")) { if (!capture_integer_value(f, t, &summary->embedding_length) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "llama.block_count")) { if (!capture_integer_value(f, t, &summary->block_count) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "llama.attention.head_count")) { if (!capture_integer_value(f, t, &summary->head_count) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "llama.attention.head_count_kv")) { if (!capture_integer_value(f, t, &summary->head_count_kv) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (key_equals(key_buf, keep, "llama.vocab_size")) { if (!capture_integer_value(f, t, &summary->vocab_size) && !skip_value(f, t)) { fclose(f); return 0; } continue; }
        if (!skip_value(f, t)) { fclose(f); return 0; }
    }

    for (i = 0; i < summary->tensor_count; i++) {
        uint32_t name_len = 0, keep = 0, n_dims = 0, tensor_type = 0, layer = 0, d = 0;
        uint64_t data_offset = 0;
        tensor_role role = ROLE_NONE;
        char name_buf[160];

        if (!read_u32(f, &name_len) || name_len == 0 || name_len > 1024 * 1024) { fclose(f); return 0; }
        keep = name_len;
        if (keep > (uint32_t)(sizeof(name_buf) - 1)) keep = (uint32_t)(sizeof(name_buf) - 1);
        if (keep > 0 && !read_exact(f, name_buf, keep)) { fclose(f); return 0; }
        name_buf[keep] = 0;
        if (name_len > keep && !skip_exact(f, (uint64_t)(name_len - keep))) { fclose(f); return 0; }

        if (!read_u32(f, &n_dims) || n_dims == 0 || n_dims > 16) { fclose(f); return 0; }
        for (d = 0; d < n_dims; d++) {
            uint64_t dim = 0;
            if (!read_u64(f, &dim)) { fclose(f); return 0; }
        }
        if (!read_u32(f, &tensor_type) || !read_u64(f, &data_offset)) { fclose(f); return 0; }
        if (parse_tensor_role(name_buf, &layer, &role)) {
            register_tensor_role(tensor_plan, role, layer, data_offset, tensor_type);
        }
    }

    tensor_plan->data_start = (uint64_t)_ftelli64(f);
    finalize_tensor_plan(tensor_plan, summary);
    fclose(f);
    return 1;
}
