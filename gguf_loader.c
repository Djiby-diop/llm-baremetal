#include "gguf_loader.h"

// Minimal GGUF reader
// Goal: parse enough metadata to identify model architecture + key sizes.

typedef enum {
    GGUF_TYPE_UINT8   = 0,
    GGUF_TYPE_INT8    = 1,
    GGUF_TYPE_UINT16  = 2,
    GGUF_TYPE_INT16   = 3,
    GGUF_TYPE_UINT32  = 4,
    GGUF_TYPE_INT32   = 5,
    GGUF_TYPE_FLOAT32 = 6,
    GGUF_TYPE_BOOL    = 7,
    GGUF_TYPE_STRING  = 8,
    GGUF_TYPE_ARRAY   = 9,
    GGUF_TYPE_UINT64  = 10,
    GGUF_TYPE_INT64   = 11,
    GGUF_TYPE_FLOAT64 = 12,
} gguf_type;

static EFI_STATUS gguf_read_exact(EFI_FILE_HANDLE f, void *dst, UINTN nbytes) {
    if (!f || !dst) return EFI_INVALID_PARAMETER;
    if (nbytes == 0) return EFI_SUCCESS;
    UINTN want = nbytes;
    EFI_STATUS st = uefi_call_wrapper(f->Read, 3, f, &want, dst);
    if (EFI_ERROR(st)) return st;
    if (want != nbytes) return EFI_END_OF_FILE;
    return EFI_SUCCESS;
}

static EFI_STATUS gguf_skip(EFI_FILE_HANDLE f, UINT64 nbytes) {
    if (!f) return EFI_INVALID_PARAMETER;
    // SetPosition takes absolute; use GetPosition then SetPosition.
    UINT64 pos = 0;
    EFI_STATUS st = uefi_call_wrapper(f->GetPosition, 2, f, &pos);
    if (EFI_ERROR(st)) return st;
    return uefi_call_wrapper(f->SetPosition, 2, f, pos + nbytes);
}

static void gguf_zero_summary(GgufSummary *s) {
    if (!s) return;
    // manual zero to avoid pulling in libc
    UINT8 *p = (UINT8 *)s;
    for (UINTN i = 0; i < (UINTN)sizeof(*s); i++) p[i] = 0;
}

static int gguf_key_eq(const char *key, UINT32 key_len, const char *lit) {
    if (!key || !lit) return 0;
    UINT32 i = 0;
    for (; lit[i] && i < key_len; i++) {
        if (key[i] != lit[i]) return 0;
    }
    return (lit[i] == 0 && i == key_len);
}

static EFI_STATUS gguf_read_u32(EFI_FILE_HANDLE f, UINT32 *out) {
    if (!out) return EFI_INVALID_PARAMETER;
    return gguf_read_exact(f, out, (UINTN)sizeof(*out));
}

static EFI_STATUS gguf_read_u64(EFI_FILE_HANDLE f, UINT64 *out) {
    if (!out) return EFI_INVALID_PARAMETER;
    return gguf_read_exact(f, out, (UINTN)sizeof(*out));
}

static EFI_STATUS gguf_read_i32(EFI_FILE_HANDLE f, INT32 *out) {
    if (!out) return EFI_INVALID_PARAMETER;
    return gguf_read_exact(f, out, (UINTN)sizeof(*out));
}

static EFI_STATUS gguf_read_i64(EFI_FILE_HANDLE f, INT64 *out) {
    if (!out) return EFI_INVALID_PARAMETER;
    return gguf_read_exact(f, out, (UINTN)sizeof(*out));
}

static EFI_STATUS gguf_skip_value(EFI_FILE_HANDLE f, gguf_type t);

static EFI_STATUS gguf_read_string_trunc(EFI_FILE_HANDLE f, char *out, UINTN out_cap) {
    if (!out || out_cap == 0) return EFI_INVALID_PARAMETER;
    out[0] = 0;

    UINT64 n = 0;
    EFI_STATUS st = gguf_read_u64(f, &n);
    if (EFI_ERROR(st)) return st;

    // Read at most out_cap-1 bytes; skip the rest.
    UINT64 to_read = n;
    if (to_read > (UINT64)(out_cap - 1)) to_read = (UINT64)(out_cap - 1);

    if (to_read > 0) {
        st = gguf_read_exact(f, out, (UINTN)to_read);
        if (EFI_ERROR(st)) return st;
    }
    out[(UINTN)to_read] = 0;

    if (n > to_read) {
        return gguf_skip(f, n - to_read);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS gguf_skip_value(EFI_FILE_HANDLE f, gguf_type t) {
    if (!f) return EFI_INVALID_PARAMETER;

    switch (t) {
        case GGUF_TYPE_UINT8:
        case GGUF_TYPE_INT8:
        case GGUF_TYPE_BOOL:
            return gguf_skip(f, 1);
        case GGUF_TYPE_UINT16:
        case GGUF_TYPE_INT16:
            return gguf_skip(f, 2);
        case GGUF_TYPE_UINT32:
        case GGUF_TYPE_INT32:
        case GGUF_TYPE_FLOAT32:
            return gguf_skip(f, 4);
        case GGUF_TYPE_UINT64:
        case GGUF_TYPE_INT64:
        case GGUF_TYPE_FLOAT64:
            return gguf_skip(f, 8);
        case GGUF_TYPE_STRING: {
            UINT64 n = 0;
            EFI_STATUS st = gguf_read_u64(f, &n);
            if (EFI_ERROR(st)) return st;
            return gguf_skip(f, n);
        }
        case GGUF_TYPE_ARRAY: {
            UINT32 elem_t_u32 = 0;
            UINT64 n = 0;
            EFI_STATUS st = gguf_read_u32(f, &elem_t_u32);
            if (EFI_ERROR(st)) return st;
            st = gguf_read_u64(f, &n);
            if (EFI_ERROR(st)) return st;

            gguf_type elem_t = (gguf_type)elem_t_u32;
            // Arrays of STRING are variable-sized; must iterate.
            if (elem_t == GGUF_TYPE_STRING) {
                for (UINT64 i = 0; i < n; i++) {
                    st = gguf_skip_value(f, GGUF_TYPE_STRING);
                    if (EFI_ERROR(st)) return st;
                }
                return EFI_SUCCESS;
            }

            UINT64 elem_size = 0;
            switch (elem_t) {
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
                default:
                    // Unknown or nested arrays are not supported in this minimal reader.
                    return EFI_UNSUPPORTED;
            }
            return gguf_skip(f, n * elem_size);
        }
        default:
            return EFI_UNSUPPORTED;
    }
}

EFI_STATUS gguf_read_summary(EFI_FILE_HANDLE f, GgufSummary *out) {
    if (!f || !out) return EFI_INVALID_PARAMETER;
    gguf_zero_summary(out);

    EFI_STATUS st = uefi_call_wrapper(f->SetPosition, 2, f, 0);
    if (EFI_ERROR(st)) return st;

    // Magic
    UINT8 magic[4];
    st = gguf_read_exact(f, magic, 4);
    if (EFI_ERROR(st)) return st;
    if (!(magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' && magic[3] == 'F')) {
        return EFI_UNSUPPORTED;
    }

    st = gguf_read_u32(f, &out->version);
    if (EFI_ERROR(st)) return st;

    st = gguf_read_u64(f, &out->tensor_count);
    if (EFI_ERROR(st)) return st;

    st = gguf_read_u64(f, &out->kv_count);
    if (EFI_ERROR(st)) return st;

    // KV section
    for (UINT64 i = 0; i < out->kv_count; i++) {
        UINT32 key_len = 0;
        st = gguf_read_u32(f, &key_len);
        if (EFI_ERROR(st)) return st;

        if (key_len == 0 || key_len > 4096) {
            return EFI_COMPROMISED_DATA;
        }

        // Read key (truncate for matching)
        char key_buf[192];
        UINT32 keep = key_len;
        if (keep > (UINT32)(sizeof(key_buf) - 1)) keep = (UINT32)(sizeof(key_buf) - 1);

        if (keep > 0) {
            st = gguf_read_exact(f, key_buf, (UINTN)keep);
            if (EFI_ERROR(st)) return st;
        }
        key_buf[keep] = 0;

        if (key_len > keep) {
            st = gguf_skip(f, (UINT64)(key_len - keep));
            if (EFI_ERROR(st)) return st;
        }

        UINT32 vt_u32 = 0;
        st = gguf_read_u32(f, &vt_u32);
        if (EFI_ERROR(st)) return st;
        gguf_type vt = (gguf_type)vt_u32;

        // Capture a few common keys. Anything else: skip.
        if (gguf_key_eq(key_buf, keep, "general.architecture") && vt == GGUF_TYPE_STRING) {
            st = gguf_read_string_trunc(f, out->architecture, (UINTN)sizeof(out->architecture));
            if (EFI_ERROR(st)) return st;
            continue;
        }
        if (gguf_key_eq(key_buf, keep, "general.name") && vt == GGUF_TYPE_STRING) {
            st = gguf_read_string_trunc(f, out->name, (UINTN)sizeof(out->name));
            if (EFI_ERROR(st)) return st;
            continue;
        }
        if (gguf_key_eq(key_buf, keep, "general.file_type")) {
            // may be u32 or u64
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0;
                st = gguf_read_u32(f, &v);
                if (EFI_ERROR(st)) return st;
                out->file_type = (UINT64)v;
                continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0;
                st = gguf_read_u64(f, &v);
                if (EFI_ERROR(st)) return st;
                out->file_type = v;
                continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "llama.context_length")) {
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0; st = gguf_read_u32(f, &v); if (EFI_ERROR(st)) return st; out->context_length = (UINT64)v; continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0; st = gguf_read_u64(f, &v); if (EFI_ERROR(st)) return st; out->context_length = v; continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "llama.embedding_length")) {
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0; st = gguf_read_u32(f, &v); if (EFI_ERROR(st)) return st; out->embedding_length = (UINT64)v; continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0; st = gguf_read_u64(f, &v); if (EFI_ERROR(st)) return st; out->embedding_length = v; continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "llama.block_count")) {
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0; st = gguf_read_u32(f, &v); if (EFI_ERROR(st)) return st; out->block_count = (UINT64)v; continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0; st = gguf_read_u64(f, &v); if (EFI_ERROR(st)) return st; out->block_count = v; continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "llama.attention.head_count")) {
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0; st = gguf_read_u32(f, &v); if (EFI_ERROR(st)) return st; out->head_count = (UINT64)v; continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0; st = gguf_read_u64(f, &v); if (EFI_ERROR(st)) return st; out->head_count = v; continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "llama.attention.head_count_kv")) {
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0; st = gguf_read_u32(f, &v); if (EFI_ERROR(st)) return st; out->head_count_kv = (UINT64)v; continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0; st = gguf_read_u64(f, &v); if (EFI_ERROR(st)) return st; out->head_count_kv = v; continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "llama.vocab_size")) {
            if (vt == GGUF_TYPE_UINT32) {
                UINT32 v = 0; st = gguf_read_u32(f, &v); if (EFI_ERROR(st)) return st; out->vocab_size = (UINT64)v; continue;
            }
            if (vt == GGUF_TYPE_UINT64) {
                UINT64 v = 0; st = gguf_read_u64(f, &v); if (EFI_ERROR(st)) return st; out->vocab_size = v; continue;
            }
        }

        if (gguf_key_eq(key_buf, keep, "tokenizer.ggml.model") && vt == GGUF_TYPE_STRING) {
            st = gguf_read_string_trunc(f, out->tokenizer_model, (UINTN)sizeof(out->tokenizer_model));
            if (EFI_ERROR(st)) return st;
            continue;
        }

        // Otherwise skip this value.
        st = gguf_skip_value(f, vt);
        if (EFI_ERROR(st)) return st;
    }

    // Tensor info table: we only skip it to compute header_bytes.
    for (UINT64 i = 0; i < out->tensor_count; i++) {
        UINT32 name_len = 0;
        st = gguf_read_u32(f, &name_len);
        if (EFI_ERROR(st)) return st;
        if (name_len == 0 || name_len > 1024 * 1024) return EFI_COMPROMISED_DATA;
        st = gguf_skip(f, name_len);
        if (EFI_ERROR(st)) return st;

        UINT32 n_dims = 0;
        st = gguf_read_u32(f, &n_dims);
        if (EFI_ERROR(st)) return st;
        if (n_dims > 16) return EFI_COMPROMISED_DATA;

        for (UINT32 d = 0; d < n_dims; d++) {
            UINT64 dim = 0;
            st = gguf_read_u64(f, &dim);
            if (EFI_ERROR(st)) return st;
            (void)dim;
        }

        UINT32 tensor_type = 0;
        st = gguf_read_u32(f, &tensor_type);
        if (EFI_ERROR(st)) return st;
        (void)tensor_type;

        UINT64 data_offset = 0;
        st = gguf_read_u64(f, &data_offset);
        if (EFI_ERROR(st)) return st;
        (void)data_offset;
    }

    {
        UINT64 pos = 0;
        EFI_STATUS pst = uefi_call_wrapper(f->GetPosition, 2, f, &pos);
        if (!EFI_ERROR(pst)) out->header_bytes = pos;
    }

    return EFI_SUCCESS;
}
