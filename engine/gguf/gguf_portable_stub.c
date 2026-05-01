#include "gguf_portable.h"

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

typedef struct {
    const LlmkPortableReader *reader;
    uint64_t offset;
    uint64_t file_size;
} PortableCursor;

static void portable_zero(void *dst, size_t nbytes) {
    size_t i = 0;
    uint8_t *p = (uint8_t *)dst;
    if (!dst || nbytes == 0) return;
    for (i = 0; i < nbytes; i++) p[i] = 0;
}

static LlmkPortableStatus portable_read_exact(PortableCursor *cursor, void *dst, size_t nbytes) {
    size_t got = 0;
    if (!cursor || !cursor->reader || !cursor->reader->read_at || (!dst && nbytes > 0)) {
        return LLMK_PORTABLE_INVALID_ARGUMENT;
    }
    if (nbytes == 0) {
        return LLMK_PORTABLE_OK;
    }
    if (cursor->file_size > 0 && cursor->offset + (uint64_t)nbytes > cursor->file_size) {
        return LLMK_PORTABLE_IO_ERROR;
    }
    got = cursor->reader->read_at(cursor->reader->user_data, cursor->offset, dst, nbytes);
    if (got != nbytes) {
        return LLMK_PORTABLE_IO_ERROR;
    }
    cursor->offset += (uint64_t)nbytes;
    return LLMK_PORTABLE_OK;
}

static LlmkPortableStatus portable_skip(PortableCursor *cursor, uint64_t nbytes) {
    if (!cursor) return LLMK_PORTABLE_INVALID_ARGUMENT;
    if (cursor->file_size > 0 && cursor->offset + nbytes > cursor->file_size) {
        return LLMK_PORTABLE_IO_ERROR;
    }
    cursor->offset += nbytes;
    return LLMK_PORTABLE_OK;
}

static LlmkPortableStatus portable_read_u32(PortableCursor *cursor, uint32_t *out) {
    if (!out) return LLMK_PORTABLE_INVALID_ARGUMENT;
    return portable_read_exact(cursor, out, sizeof(*out));
}

static LlmkPortableStatus portable_read_u64(PortableCursor *cursor, uint64_t *out) {
    if (!out) return LLMK_PORTABLE_INVALID_ARGUMENT;
    return portable_read_exact(cursor, out, sizeof(*out));
}

static int portable_key_eq(const char *key, uint32_t key_len, const char *literal) {
    uint32_t i = 0;
    if (!key || !literal) return 0;
    while (literal[i] && i < key_len) {
        if (key[i] != literal[i]) return 0;
        i++;
    }
    return literal[i] == 0 && i == key_len;
}

static int portable_str_eq(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int portable_parse_u32_ascii(const char *s, int *index, uint32_t *out) {
    uint32_t value = 0;
    int i = 0;
    int has_digit = 0;
    if (!s || !index || !out) return 0;
    i = *index;
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

static int portable_parse_tensor_role(const char *name, uint32_t *out_layer, tensor_role *out_role) {
    int i = 0;
    uint32_t layer = 0;
    const char *rest = NULL;
    if (!name || !out_layer || !out_role) return 0;
    *out_layer = 0;
    *out_role = ROLE_NONE;

    if (portable_str_eq(name, "token_embd.weight")) {
        *out_role = ROLE_TOK_EMBD;
        return 1;
    }
    if (portable_str_eq(name, "output.weight")) {
        *out_role = ROLE_OUTPUT;
        return 1;
    }
    if (portable_str_eq(name, "output_norm.weight") || portable_str_eq(name, "norm.weight")) {
        *out_role = ROLE_RMS_FINAL;
        return 1;
    }

    if (!(name[0] == 'b' && name[1] == 'l' && name[2] == 'k' && name[3] == '.')) return 0;
    i = 4;
    if (!portable_parse_u32_ascii(name, &i, &layer)) return 0;
    if (name[i] != '.') return 0;
    rest = name + i + 1;

    if (portable_str_eq(rest, "attn_norm.weight")) { *out_layer = layer; *out_role = ROLE_ATTN_NORM; return 1; }
    if (portable_str_eq(rest, "ffn_norm.weight")) { *out_layer = layer; *out_role = ROLE_FFN_NORM; return 1; }
    if (portable_str_eq(rest, "attn_q.weight")) { *out_layer = layer; *out_role = ROLE_WQ; return 1; }
    if (portable_str_eq(rest, "attn_k.weight")) { *out_layer = layer; *out_role = ROLE_WK; return 1; }
    if (portable_str_eq(rest, "attn_v.weight")) { *out_layer = layer; *out_role = ROLE_WV; return 1; }
    if (portable_str_eq(rest, "attn_output.weight")) { *out_layer = layer; *out_role = ROLE_WO; return 1; }
    if (portable_str_eq(rest, "ffn_gate.weight")) { *out_layer = layer; *out_role = ROLE_FFN_GATE; return 1; }
    if (portable_str_eq(rest, "ffn_down.weight")) { *out_layer = layer; *out_role = ROLE_FFN_DOWN; return 1; }
    if (portable_str_eq(rest, "ffn_up.weight")) { *out_layer = layer; *out_role = ROLE_FFN_UP; return 1; }
    return 0;
}

static int portable_type_supported_for_llama(uint32_t tensor_type) {
    return tensor_type == GGML_TYPE_F32 ||
           tensor_type == GGML_TYPE_F16 ||
           tensor_type == GGML_TYPE_Q4_0 ||
           tensor_type == GGML_TYPE_Q4_1 ||
           tensor_type == GGML_TYPE_Q5_0 ||
           tensor_type == GGML_TYPE_Q5_1 ||
           tensor_type == GGML_TYPE_Q8_0;
}

static void portable_register_tensor_role(
    LlmkPortableTensorPlan *plan,
    tensor_role role,
    uint32_t layer,
    uint32_t tensor_type
) {
    if (!plan) return;
    plan->recognized_tensor_count++;
    if (layer + 1u > plan->detected_layers) plan->detected_layers = layer + 1u;
    if (portable_type_supported_for_llama(tensor_type)) plan->supported_type_count++;
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

static void portable_finalize_tensor_plan(
    LlmkPortableTensorPlan *plan,
    const LlmkPortableGgufSummary *summary
) {
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

static void portable_finalize_runtime_plan(
    LlmkPortableRuntimePlan *runtime_plan,
    const LlmkPortableGgufSummary *summary,
    const LlmkPortableTensorPlan *tensor_plan
) {
    uint64_t dim = 0;
    uint64_t hidden = 0;
    uint64_t layers = 0;
    uint64_t heads = 0;
    uint64_t kv_heads = 0;
    uint64_t vocab = 0;
    uint64_t seq = 0;
    uint64_t kv_dim = 0;
    uint64_t head_size = 0;
    uint64_t weights_floats = 0;
    uint64_t kv_cache_floats = 0;
    uint64_t freq_cis_floats = 0;

    if (!runtime_plan || !summary || !tensor_plan) return;
    portable_zero(runtime_plan, sizeof(*runtime_plan));

    dim = summary->embedding_length;
    hidden = summary->embedding_length ? (summary->embedding_length * 8u) / 3u : 0;
    layers = summary->block_count;
    heads = summary->head_count;
    kv_heads = summary->head_count_kv ? summary->head_count_kv : summary->head_count;
    vocab = summary->vocab_size;
    seq = summary->context_length;

    if (dim > 0 && hidden > 0 && layers > 0 && heads > 0 && kv_heads > 0 && vocab > 0 && seq > 0) {
        runtime_plan->hyperparams_complete = 1;
        runtime_plan->shared_classifier = tensor_plan->output_present ? 0u : 1u;
        kv_dim = (dim * kv_heads) / heads;
        head_size = dim / heads;
        weights_floats =
            (vocab * dim) +
            (layers * dim) +
            (layers * dim * dim) +
            (layers * dim * kv_dim) +
            (layers * dim * kv_dim) +
            (layers * dim * dim) +
            (layers * dim) +
            (layers * dim * hidden) +
            (layers * hidden * dim) +
            (layers * dim * hidden) +
            dim;
        freq_cis_floats = seq * head_size;
        if (!runtime_plan->shared_classifier) {
            weights_floats += vocab * dim;
        }
        kv_cache_floats = layers * seq * kv_dim * 2u;

        runtime_plan->dim = dim;
        runtime_plan->hidden_dim = hidden;
        runtime_plan->n_layers = layers;
        runtime_plan->n_heads = heads;
        runtime_plan->n_kv_heads = kv_heads;
        runtime_plan->vocab_size = vocab;
        runtime_plan->seq_len = seq;
        runtime_plan->kv_dim = kv_dim;
        runtime_plan->head_size = head_size;
        runtime_plan->float_weights_bytes = weights_floats * sizeof(float);
        runtime_plan->float_kv_cache_bytes = kv_cache_floats * sizeof(float);
        runtime_plan->float_total_bytes = (weights_floats + kv_cache_floats + freq_cis_floats) * sizeof(float);
        runtime_plan->float_layout_compatible = tensor_plan->fully_mapped_llama;
        runtime_plan->q8_candidate =
            tensor_plan->fully_mapped_llama &&
            tensor_plan->q8_0_count >= (1u + (runtime_plan->shared_classifier ? 0u : 1u) + (uint32_t)(layers * 7u));
    }
}

static LlmkPortableStatus portable_read_string_trunc(PortableCursor *cursor, char *dst, size_t capacity) {
    uint64_t length = 0;
    uint64_t to_read = 0;
    LlmkPortableStatus status;

    if (!dst || capacity == 0) return LLMK_PORTABLE_INVALID_ARGUMENT;
    dst[0] = 0;

    status = portable_read_u64(cursor, &length);
    if (status != LLMK_PORTABLE_OK) return status;

    to_read = length;
    if (to_read > (uint64_t)(capacity - 1)) {
        to_read = (uint64_t)(capacity - 1);
    }
    if (to_read > 0) {
        status = portable_read_exact(cursor, dst, (size_t)to_read);
        if (status != LLMK_PORTABLE_OK) return status;
    }
    dst[(size_t)to_read] = 0;
    if (length > to_read) {
        return portable_skip(cursor, length - to_read);
    }
    return LLMK_PORTABLE_OK;
}

static LlmkPortableStatus portable_skip_value(PortableCursor *cursor, gguf_type value_type) {
    uint32_t elem_type_u32 = 0;
    uint64_t count = 0;
    uint64_t elem_size = 0;
    LlmkPortableStatus status;

    if (!cursor) return LLMK_PORTABLE_INVALID_ARGUMENT;

    switch (value_type) {
        case GGUF_TYPE_UINT8:
        case GGUF_TYPE_INT8:
        case GGUF_TYPE_BOOL:
            return portable_skip(cursor, 1);
        case GGUF_TYPE_UINT16:
        case GGUF_TYPE_INT16:
            return portable_skip(cursor, 2);
        case GGUF_TYPE_UINT32:
        case GGUF_TYPE_INT32:
        case GGUF_TYPE_FLOAT32:
            return portable_skip(cursor, 4);
        case GGUF_TYPE_UINT64:
        case GGUF_TYPE_INT64:
        case GGUF_TYPE_FLOAT64:
            return portable_skip(cursor, 8);
        case GGUF_TYPE_STRING: {
            status = portable_read_u64(cursor, &count);
            if (status != LLMK_PORTABLE_OK) return status;
            return portable_skip(cursor, count);
        }
        case GGUF_TYPE_ARRAY:
            status = portable_read_u32(cursor, &elem_type_u32);
            if (status != LLMK_PORTABLE_OK) return status;
            status = portable_read_u64(cursor, &count);
            if (status != LLMK_PORTABLE_OK) return status;
            if ((gguf_type)elem_type_u32 == GGUF_TYPE_STRING) {
                uint64_t i = 0;
                for (i = 0; i < count; i++) {
                    status = portable_skip_value(cursor, GGUF_TYPE_STRING);
                    if (status != LLMK_PORTABLE_OK) return status;
                }
                return LLMK_PORTABLE_OK;
            }
            switch ((gguf_type)elem_type_u32) {
                case GGUF_TYPE_UINT8:
                case GGUF_TYPE_INT8:
                case GGUF_TYPE_BOOL:
                    elem_size = 1;
                    break;
                case GGUF_TYPE_UINT16:
                case GGUF_TYPE_INT16:
                    elem_size = 2;
                    break;
                case GGUF_TYPE_UINT32:
                case GGUF_TYPE_INT32:
                case GGUF_TYPE_FLOAT32:
                    elem_size = 4;
                    break;
                case GGUF_TYPE_UINT64:
                case GGUF_TYPE_INT64:
                case GGUF_TYPE_FLOAT64:
                    elem_size = 8;
                    break;
                default:
                    return LLMK_PORTABLE_UNSUPPORTED;
            }
            return portable_skip(cursor, count * elem_size);
        default:
            return LLMK_PORTABLE_UNSUPPORTED;
    }
}

static LlmkPortableStatus portable_capture_u64(
    PortableCursor *cursor,
    gguf_type value_type,
    uint64_t *out_value
) {
    uint32_t value_u32 = 0;
    uint64_t value_u64 = 0;
    LlmkPortableStatus status;

    if (!cursor || !out_value) return LLMK_PORTABLE_INVALID_ARGUMENT;

    if (value_type == GGUF_TYPE_UINT32) {
        status = portable_read_u32(cursor, &value_u32);
        if (status != LLMK_PORTABLE_OK) return status;
        *out_value = (uint64_t)value_u32;
        return LLMK_PORTABLE_OK;
    }
    if (value_type == GGUF_TYPE_UINT64) {
        status = portable_read_u64(cursor, &value_u64);
        if (status != LLMK_PORTABLE_OK) return status;
        *out_value = value_u64;
        return LLMK_PORTABLE_OK;
    }
    return portable_skip_value(cursor, value_type);
}

LlmkPortableStatus llmk_portable_gguf_read_summary(
    const LlmkPortableReader *reader,
    LlmkPortableGgufSummary *out_summary
) {
    PortableCursor cursor;
    LlmkPortableStatus status;
    uint8_t magic[4];
    uint64_t i = 0;

    if (!reader || !reader->read_at || !out_summary) {
        return LLMK_PORTABLE_INVALID_ARGUMENT;
    }

    portable_zero(out_summary, sizeof(*out_summary));
    portable_zero(&cursor, sizeof(cursor));
    cursor.reader = reader;
    cursor.file_size = reader->size ? reader->size(reader->user_data) : 0;
    out_summary->file_size = cursor.file_size;
    out_summary->header_bytes = 0;

    status = portable_read_exact(&cursor, magic, sizeof(magic));
    if (status != LLMK_PORTABLE_OK) return status;
    if (!(magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' && magic[3] == 'F')) {
        return LLMK_PORTABLE_UNSUPPORTED;
    }

    status = portable_read_u32(&cursor, &out_summary->version);
    if (status != LLMK_PORTABLE_OK) return status;
    status = portable_read_u64(&cursor, &out_summary->tensor_count);
    if (status != LLMK_PORTABLE_OK) return status;
    status = portable_read_u64(&cursor, &out_summary->kv_count);
    if (status != LLMK_PORTABLE_OK) return status;

    for (i = 0; i < out_summary->kv_count; i++) {
        uint32_t key_len = 0;
        uint32_t keep = 0;
        uint32_t value_type_u32 = 0;
        char key_buf[192];
        gguf_type value_type;

        status = portable_read_u32(&cursor, &key_len);
        if (status != LLMK_PORTABLE_OK) return status;
        if (key_len == 0 || key_len > 4096) {
            return LLMK_PORTABLE_UNSUPPORTED;
        }

        keep = key_len;
        if (keep > (uint32_t)(sizeof(key_buf) - 1)) {
            keep = (uint32_t)(sizeof(key_buf) - 1);
        }
        if (keep > 0) {
            status = portable_read_exact(&cursor, key_buf, keep);
            if (status != LLMK_PORTABLE_OK) return status;
        }
        key_buf[keep] = 0;

        if (key_len > keep) {
            status = portable_skip(&cursor, (uint64_t)(key_len - keep));
            if (status != LLMK_PORTABLE_OK) return status;
        }

        status = portable_read_u32(&cursor, &value_type_u32);
        if (status != LLMK_PORTABLE_OK) return status;
        value_type = (gguf_type)value_type_u32;

        if (portable_key_eq(key_buf, keep, "general.architecture") && value_type == GGUF_TYPE_STRING) {
            status = portable_read_string_trunc(&cursor, out_summary->architecture, sizeof(out_summary->architecture));
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "general.name") && value_type == GGUF_TYPE_STRING) {
            status = portable_read_string_trunc(&cursor, out_summary->name, sizeof(out_summary->name));
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "tokenizer.ggml.model") && value_type == GGUF_TYPE_STRING) {
            status = portable_read_string_trunc(&cursor, out_summary->tokenizer_model, sizeof(out_summary->tokenizer_model));
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "general.file_type")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->file_type);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "llama.context_length")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->context_length);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "llama.embedding_length")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->embedding_length);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "llama.block_count")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->block_count);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "llama.attention.head_count")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->head_count);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "llama.attention.head_count_kv")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->head_count_kv);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }
        if (portable_key_eq(key_buf, keep, "llama.vocab_size")) {
            status = portable_capture_u64(&cursor, value_type, &out_summary->vocab_size);
            if (status != LLMK_PORTABLE_OK) return status;
            continue;
        }

        status = portable_skip_value(&cursor, value_type);
        if (status != LLMK_PORTABLE_OK) return status;
    }

    for (i = 0; i < out_summary->tensor_count; i++) {
        uint32_t name_len = 0;
        uint32_t n_dims = 0;
        uint32_t d = 0;

        status = portable_read_u32(&cursor, &name_len);
        if (status != LLMK_PORTABLE_OK) return status;
        if (name_len == 0 || name_len > 1024u * 1024u) {
            return LLMK_PORTABLE_UNSUPPORTED;
        }
        status = portable_skip(&cursor, (uint64_t)name_len);
        if (status != LLMK_PORTABLE_OK) return status;

        status = portable_read_u32(&cursor, &n_dims);
        if (status != LLMK_PORTABLE_OK) return status;
        if (n_dims > 16) {
            return LLMK_PORTABLE_UNSUPPORTED;
        }
        for (d = 0; d < n_dims; d++) {
            uint64_t dim = 0;
            status = portable_read_u64(&cursor, &dim);
            if (status != LLMK_PORTABLE_OK) return status;
        }

        status = portable_skip(&cursor, sizeof(uint32_t) + sizeof(uint64_t));
        if (status != LLMK_PORTABLE_OK) return status;
    }

    out_summary->header_bytes = cursor.offset;
    return LLMK_PORTABLE_OK;
}

LlmkPortableStatus llmk_portable_gguf_build_runtime_plan(
    const LlmkPortableReader *reader,
    LlmkPortableGgufSummary *io_summary,
    LlmkPortableTensorPlan *out_tensor_plan,
    LlmkPortableRuntimePlan *out_runtime_plan
) {
    PortableCursor cursor;
    LlmkPortableStatus status;
    uint8_t magic[4];
    uint64_t i = 0;

    if (!reader || !reader->read_at || !io_summary || !out_tensor_plan || !out_runtime_plan) {
        return LLMK_PORTABLE_INVALID_ARGUMENT;
    }

    portable_zero(out_tensor_plan, sizeof(*out_tensor_plan));
    portable_zero(out_runtime_plan, sizeof(*out_runtime_plan));
    portable_zero(&cursor, sizeof(cursor));
    cursor.reader = reader;
    cursor.file_size = reader->size ? reader->size(reader->user_data) : 0;

    if (io_summary->version == 0) {
        status = llmk_portable_gguf_read_summary(reader, io_summary);
        if (status != LLMK_PORTABLE_OK) return status;
    }

    status = portable_read_exact(&cursor, magic, sizeof(magic));
    if (status != LLMK_PORTABLE_OK) return status;
    if (!(magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' && magic[3] == 'F')) {
        return LLMK_PORTABLE_UNSUPPORTED;
    }

    status = portable_skip(&cursor, sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t));
    if (status != LLMK_PORTABLE_OK) return status;

    for (i = 0; i < io_summary->kv_count; i++) {
        uint32_t key_len = 0;
        uint32_t value_type_u32 = 0;
        gguf_type value_type;

        status = portable_read_u32(&cursor, &key_len);
        if (status != LLMK_PORTABLE_OK) return status;
        if (key_len == 0 || key_len > 4096) return LLMK_PORTABLE_UNSUPPORTED;
        status = portable_skip(&cursor, (uint64_t)key_len);
        if (status != LLMK_PORTABLE_OK) return status;
        status = portable_read_u32(&cursor, &value_type_u32);
        if (status != LLMK_PORTABLE_OK) return status;
        value_type = (gguf_type)value_type_u32;
        status = portable_skip_value(&cursor, value_type);
        if (status != LLMK_PORTABLE_OK) return status;
    }

    for (i = 0; i < io_summary->tensor_count; i++) {
        uint32_t name_len = 0;
        uint32_t keep = 0;
        uint32_t n_dims = 0;
        uint32_t tensor_type = 0;
        uint32_t layer = 0;
        uint64_t data_offset = 0;
        uint32_t d = 0;
        char name_buf[160];
        tensor_role role = ROLE_NONE;

        status = portable_read_u32(&cursor, &name_len);
        if (status != LLMK_PORTABLE_OK) return status;
        if (name_len == 0 || name_len > 1024u * 1024u) return LLMK_PORTABLE_UNSUPPORTED;

        keep = name_len;
        if (keep > (uint32_t)(sizeof(name_buf) - 1)) keep = (uint32_t)(sizeof(name_buf) - 1);
        if (keep > 0) {
            status = portable_read_exact(&cursor, name_buf, keep);
            if (status != LLMK_PORTABLE_OK) return status;
        }
        name_buf[keep] = 0;
        if (name_len > keep) {
            status = portable_skip(&cursor, (uint64_t)(name_len - keep));
            if (status != LLMK_PORTABLE_OK) return status;
        }

        status = portable_read_u32(&cursor, &n_dims);
        if (status != LLMK_PORTABLE_OK) return status;
        if (n_dims == 0 || n_dims > 16) return LLMK_PORTABLE_UNSUPPORTED;
        for (d = 0; d < n_dims; d++) {
            uint64_t dim = 0;
            status = portable_read_u64(&cursor, &dim);
            if (status != LLMK_PORTABLE_OK) return status;
        }
        status = portable_read_u32(&cursor, &tensor_type);
        if (status != LLMK_PORTABLE_OK) return status;
        status = portable_read_u64(&cursor, &data_offset);
        if (status != LLMK_PORTABLE_OK) return status;

        if (portable_parse_tensor_role(name_buf, &layer, &role)) {
            portable_register_tensor_role(out_tensor_plan, role, layer, tensor_type);
        }
    }

    portable_finalize_tensor_plan(out_tensor_plan, io_summary);
    portable_finalize_runtime_plan(out_runtime_plan, io_summary, out_tensor_plan);
    return LLMK_PORTABLE_OK;
}
