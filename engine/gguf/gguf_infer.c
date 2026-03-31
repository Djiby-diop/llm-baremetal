#include "gguf_infer.h"

// GGUF tensor types (ggml_type)
// Phase 2: support common quant types by dequantizing to float32 at load.
typedef enum {
    GGML_TYPE_F32  = 0,
    GGML_TYPE_F16  = 1,
    GGML_TYPE_Q4_0 = 2,
    GGML_TYPE_Q4_1 = 3,
    GGML_TYPE_Q5_0 = 6,
    GGML_TYPE_Q5_1 = 7,
    GGML_TYPE_Q8_0 = 8,
} ggml_type;

// Quant block sizes (as in llama.cpp ggml-common.h)
#define LLMK_QK4_0 32
#define LLMK_QK4_1 32
#define LLMK_QK5_0 32
#define LLMK_QK5_1 32
#define LLMK_QK8_0 32

typedef struct __attribute__((packed)) {
    UINT16 d; // ggml_half
    UINT8  qs[LLMK_QK4_0 / 2];
} llmk_block_q4_0;

typedef struct __attribute__((packed)) {
    UINT16 d; // ggml_half
    UINT16 m; // ggml_half
    UINT8  qs[LLMK_QK4_1 / 2];
} llmk_block_q4_1;

typedef struct __attribute__((packed)) {
    UINT16 d; // ggml_half
    UINT8  qh[4];
    UINT8  qs[LLMK_QK5_0 / 2];
} llmk_block_q5_0;

typedef struct __attribute__((packed)) {
    UINT16 d; // ggml_half
    UINT16 m; // ggml_half
    UINT8  qh[4];
    UINT8  qs[LLMK_QK5_1 / 2];
} llmk_block_q5_1;

typedef struct __attribute__((packed)) {
    UINT16 d; // ggml_half
    INT8   qs[LLMK_QK8_0];
} llmk_block_q8_0;

#define LLMK_Q8_0_BLOCK_BYTES ((UINT64)sizeof(llmk_block_q8_0))

typedef struct {
    UINT64 offset;      // relative to data section start
    UINT32 type;        // ggml_type
    UINT32 n_dims;
    UINT64 dims[4];
    int present;
} LlmkGgufTensorRef;

// -----------------------------------------------------------------------------
// Minimal serial debug (COM1) so QEMU -serial file captures diagnostics.
// OVMF typically exposes COM1 at 0x3F8.
// -----------------------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)
static __inline__ UINT8 llmk_inb(UINT16 port) {
    UINT8 ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static __inline__ void llmk_outb(UINT16 port, UINT8 val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void llmk_serial_putc(UINT8 c) {
    const UINT16 COM1 = 0x3F8;
    const UINT16 LSR = (UINT16)(COM1 + 5);
    // Wait for THR empty (bit 5). Bounded spin to avoid hangs on platforms without a UART.
    for (UINT32 spin = 0; spin < 200000; spin++) {
        if (llmk_inb(LSR) & 0x20) {
            llmk_outb(COM1, c);
            return;
        }
    }
}

static void llmk_serial_write_ascii(const char *s) {
    if (!s) return;
    for (UINTN i = 0; s[i]; i++) {
        UINT8 c = (UINT8)s[i];
        if (c == '\n') llmk_serial_putc('\r');
        llmk_serial_putc(c);
    }
}

static void llmk_serial_write_char16(const CHAR16 *s) {
    if (!s) return;
    for (UINTN i = 0; s[i]; i++) {
        CHAR16 wc = s[i];
        UINT8 c = (wc >= 0x20 && wc < 0x7f) ? (UINT8)wc : (UINT8)'?';
        if (c == '\n') llmk_serial_putc('\r');
        llmk_serial_putc(c);
    }
}
#else
static void llmk_serial_write_ascii(const char *s) { (void)s; }
static void llmk_serial_write_char16(const CHAR16 *s) { (void)s; }
#endif

static void llmk_dbg_print_both(const CHAR16 *msg) {
    if (!msg) return;
    Print(msg);
    llmk_serial_write_char16(msg);
}

struct LlmkGgufPlan {
    UINT32 version;
    UINT64 tensor_count;
    UINT64 kv_count;

    UINT64 data_start;      // absolute file position
    UINT64 max_src_cols;    // for row-buffer sizing
    UINT64 max_row_raw_bytes; // largest encoded row (for quant/F16/F32)

    // Global tensors
    LlmkGgufTensorRef tok_embd;
    LlmkGgufTensorRef output;
    LlmkGgufTensorRef rms_final;

    // Per-layer tensors (arrays sized by n_layers)
    int n_layers;
    LlmkGgufTensorRef *attn_norm;
    LlmkGgufTensorRef *wq;
    LlmkGgufTensorRef *wk;
    LlmkGgufTensorRef *wv;
    LlmkGgufTensorRef *wo;
    LlmkGgufTensorRef *ffn_norm;
    LlmkGgufTensorRef *ffn_gate;
    LlmkGgufTensorRef *ffn_down;
    LlmkGgufTensorRef *ffn_up;
};

static EFI_STATUS gguf_read_exact(EFI_FILE_HANDLE f, void *dst, UINTN nbytes) {
    if (!f || (!dst && nbytes)) return EFI_INVALID_PARAMETER;
    if (nbytes == 0) return EFI_SUCCESS;
    UINTN want = nbytes;
    EFI_STATUS st = uefi_call_wrapper(f->Read, 3, f, &want, dst);
    if (EFI_ERROR(st)) return st;
    if (want != nbytes) return EFI_END_OF_FILE;
    return EFI_SUCCESS;
}

static EFI_STATUS gguf_get_pos(EFI_FILE_HANDLE f, UINT64 *out_pos) {
    if (!f || !out_pos) return EFI_INVALID_PARAMETER;
    UINT64 pos = 0;
    EFI_STATUS st = uefi_call_wrapper(f->GetPosition, 2, f, &pos);
    if (EFI_ERROR(st)) return st;
    *out_pos = pos;
    return EFI_SUCCESS;
}

static EFI_STATUS gguf_seek(EFI_FILE_HANDLE f, UINT64 pos) {
    if (!f) return EFI_INVALID_PARAMETER;
    return uefi_call_wrapper(f->SetPosition, 2, f, pos);
}

static EFI_STATUS gguf_skip(EFI_FILE_HANDLE f, UINT64 nbytes) {
    UINT64 pos = 0;
    EFI_STATUS st = gguf_get_pos(f, &pos);
    if (EFI_ERROR(st)) return st;
    return gguf_seek(f, pos + nbytes);
}

static EFI_STATUS gguf_read_u32(EFI_FILE_HANDLE f, UINT32 *out) {
    return gguf_read_exact(f, out, (UINTN)sizeof(*out));
}

static EFI_STATUS gguf_read_u64(EFI_FILE_HANDLE f, UINT64 *out) {
    return gguf_read_exact(f, out, (UINTN)sizeof(*out));
}

static int llmk_str_eq_n(const char *a, const char *b, UINT32 n) {
    if (!a || !b) return 0;
    for (UINT32 i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 0;
    }
    return 1;
}

static int llmk_key_eq(const char *key, UINT32 key_len, const char *lit) {
    if (!key || !lit) return 0;
    UINT32 i = 0;
    for (; lit[i] && i < key_len; i++) {
        if (key[i] != lit[i]) return 0;
    }
    return (lit[i] == 0 && i == key_len);
}

// KV types (metadata)
typedef enum {
    GGUF_KV_UINT8   = 0,
    GGUF_KV_INT8    = 1,
    GGUF_KV_UINT16  = 2,
    GGUF_KV_INT16   = 3,
    GGUF_KV_UINT32  = 4,
    GGUF_KV_INT32   = 5,
    GGUF_KV_FLOAT32 = 6,
    GGUF_KV_BOOL    = 7,
    GGUF_KV_STRING  = 8,
    GGUF_KV_ARRAY   = 9,
    GGUF_KV_UINT64  = 10,
    GGUF_KV_INT64   = 11,
    GGUF_KV_FLOAT64 = 12,
} gguf_kv_type;

static EFI_STATUS gguf_skip_kv_value(EFI_FILE_HANDLE f, gguf_kv_type t) {
    if (!f) return EFI_INVALID_PARAMETER;

    switch (t) {
        case GGUF_KV_UINT8:
        case GGUF_KV_INT8:
        case GGUF_KV_BOOL:
            return gguf_skip(f, 1);
        case GGUF_KV_UINT16:
        case GGUF_KV_INT16:
            return gguf_skip(f, 2);
        case GGUF_KV_UINT32:
        case GGUF_KV_INT32:
        case GGUF_KV_FLOAT32:
            return gguf_skip(f, 4);
        case GGUF_KV_UINT64:
        case GGUF_KV_INT64:
        case GGUF_KV_FLOAT64:
            return gguf_skip(f, 8);
        case GGUF_KV_STRING: {
            UINT64 n = 0;
            EFI_STATUS st = gguf_read_u64(f, &n);
            if (EFI_ERROR(st)) return st;
            return gguf_skip(f, n);
        }
        case GGUF_KV_ARRAY: {
            UINT32 elem_t_u32 = 0;
            UINT64 n = 0;
            EFI_STATUS st = gguf_read_u32(f, &elem_t_u32);
            if (EFI_ERROR(st)) return st;
            st = gguf_read_u64(f, &n);
            if (EFI_ERROR(st)) return st;

            gguf_kv_type elem_t = (gguf_kv_type)elem_t_u32;
            if (elem_t == GGUF_KV_STRING) {
                // array of strings: iterate
                for (UINT64 i = 0; i < n; i++) {
                    st = gguf_skip_kv_value(f, GGUF_KV_STRING);
                    if (EFI_ERROR(st)) return st;
                }
                return EFI_SUCCESS;
            }

            UINT64 elem_size = 0;
            switch (elem_t) {
                case GGUF_KV_UINT8:
                case GGUF_KV_INT8:
                case GGUF_KV_BOOL: elem_size = 1; break;
                case GGUF_KV_UINT16:
                case GGUF_KV_INT16: elem_size = 2; break;
                case GGUF_KV_UINT32:
                case GGUF_KV_INT32:
                case GGUF_KV_FLOAT32: elem_size = 4; break;
                case GGUF_KV_UINT64:
                case GGUF_KV_INT64:
                case GGUF_KV_FLOAT64: elem_size = 8; break;
                default:
                    return EFI_UNSUPPORTED;
            }
            return gguf_skip(f, n * elem_size);
        }
        default:
            return EFI_UNSUPPORTED;
    }
}

static int llmk_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static int llmk_parse_u32(const char *s, int *io_i, int *out_val) {
    if (!s || !io_i || !out_val) return 0;
    int i = *io_i;
    if (!llmk_is_digit(s[i])) return 0;
    int v = 0;
    while (llmk_is_digit(s[i])) {
        v = v * 10 + (s[i] - '0');
        i++;
        if (v > 1000000) break;
    }
    *io_i = i;
    *out_val = v;
    return 1;
}

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
    ROLE_FFN_UP,
} LlmkTensorRole;

static int llmk_parse_role(const char *name, int *out_layer, LlmkTensorRole *out_role) {
    if (!name || !out_layer || !out_role) return 0;
    *out_layer = -1;
    *out_role = ROLE_NONE;

    // Global tensors
    if (name[0] == 't') {
        // token_embd.weight
        const char *lit = "token_embd.weight";
        int k = 0;
        for (; lit[k]; k++) {
            if (name[k] != lit[k]) break;
        }
        if (lit[k] == 0 && name[k] == 0) {
            *out_role = ROLE_TOK_EMBD;
            return 1;
        }
    }
    if (name[0] == 'o') {
        // output.weight
        const char *lit = "output.weight";
        int k = 0;
        for (; lit[k]; k++) {
            if (name[k] != lit[k]) break;
        }
        if (lit[k] == 0 && name[k] == 0) {
            *out_role = ROLE_OUTPUT;
            return 1;
        }

        // output_norm.weight (final)
        lit = "output_norm.weight";
        k = 0;
        for (; lit[k]; k++) {
            if (name[k] != lit[k]) break;
        }
        if (lit[k] == 0 && name[k] == 0) {
            *out_role = ROLE_RMS_FINAL;
            return 1;
        }
    }
    if (name[0] == 'n') {
        // norm.weight (final)
        const char *lit = "norm.weight";
        int k = 0;
        for (; lit[k]; k++) {
            if (name[k] != lit[k]) break;
        }
        if (lit[k] == 0 && name[k] == 0) {
            *out_role = ROLE_RMS_FINAL;
            return 1;
        }
    }

    // Layer tensors: blk.<L>.<...>
    if (!(name[0] == 'b' && name[1] == 'l' && name[2] == 'k' && name[3] == '.')) {
        return 0;
    }
    int i = 4;
    int layer = 0;
    if (!llmk_parse_u32(name, &i, &layer)) return 0;
    if (name[i] != '.') return 0;
    i++;

    const char *rest = name + i;

    // attn_norm.weight
    if (llmk_str_eq_n(rest, "attn_norm.weight", 16) && rest[16] == 0) {
        *out_layer = layer;
        *out_role = ROLE_ATTN_NORM;
        return 1;
    }
    // ffn_norm.weight
    if (llmk_str_eq_n(rest, "ffn_norm.weight", 15) && rest[15] == 0) {
        *out_layer = layer;
        *out_role = ROLE_FFN_NORM;
        return 1;
    }

    // attn_q.weight / attn_k.weight / attn_v.weight / attn_output.weight
    if (llmk_str_eq_n(rest, "attn_q.weight", 13) && rest[13] == 0) {
        *out_layer = layer;
        *out_role = ROLE_WQ;
        return 1;
    }
    if (llmk_str_eq_n(rest, "attn_k.weight", 13) && rest[13] == 0) {
        *out_layer = layer;
        *out_role = ROLE_WK;
        return 1;
    }
    if (llmk_str_eq_n(rest, "attn_v.weight", 13) && rest[13] == 0) {
        *out_layer = layer;
        *out_role = ROLE_WV;
        return 1;
    }
    if (llmk_str_eq_n(rest, "attn_output.weight", 18) && rest[18] == 0) {
        *out_layer = layer;
        *out_role = ROLE_WO;
        return 1;
    }

    // ffn gate/up/down
    if (llmk_str_eq_n(rest, "ffn_gate.weight", 15) && rest[15] == 0) {
        *out_layer = layer;
        *out_role = ROLE_FFN_GATE;
        return 1;
    }
    if (llmk_str_eq_n(rest, "ffn_up.weight", 13) && rest[13] == 0) {
        *out_layer = layer;
        *out_role = ROLE_FFN_UP;
        return 1;
    }
    if (llmk_str_eq_n(rest, "ffn_down.weight", 15) && rest[15] == 0) {
        *out_layer = layer;
        *out_role = ROLE_FFN_DOWN;
        return 1;
    }

    return 0;
}

static void llmk_zero_plan(LlmkGgufPlan *p) {
    if (!p) return;
    UINT8 *b = (UINT8 *)p;
    for (UINTN i = 0; i < (UINTN)sizeof(*p); i++) b[i] = 0;
}

static EFI_STATUS llmk_alloc_layer_arrays(LlmkGgufPlan *p, int n_layers) {
    if (!p || n_layers <= 0) return EFI_INVALID_PARAMETER;

    p->n_layers = n_layers;

    UINTN bytes = (UINTN)n_layers * (UINTN)sizeof(LlmkGgufTensorRef);
    EFI_STATUS st;

    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->attn_norm);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->wq);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->wk);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->wv);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->wo);
    if (EFI_ERROR(st)) return st;

    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->ffn_norm);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->ffn_gate);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->ffn_down);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, bytes, (void **)&p->ffn_up);
    if (EFI_ERROR(st)) return st;

    // zero
    for (int i = 0; i < n_layers; i++) {
        p->attn_norm[i].present = 0;
        p->wq[i].present = 0;
        p->wk[i].present = 0;
        p->wv[i].present = 0;
        p->wo[i].present = 0;
        p->ffn_norm[i].present = 0;
        p->ffn_gate[i].present = 0;
        p->ffn_down[i].present = 0;
        p->ffn_up[i].present = 0;
    }

    return EFI_SUCCESS;
}

void llmk_gguf_free_plan(LlmkGgufPlan *plan) {
    if (!plan) return;
    if (plan->attn_norm) uefi_call_wrapper(BS->FreePool, 1, plan->attn_norm);
    if (plan->wq) uefi_call_wrapper(BS->FreePool, 1, plan->wq);
    if (plan->wk) uefi_call_wrapper(BS->FreePool, 1, plan->wk);
    if (plan->wv) uefi_call_wrapper(BS->FreePool, 1, plan->wv);
    if (plan->wo) uefi_call_wrapper(BS->FreePool, 1, plan->wo);
    if (plan->ffn_norm) uefi_call_wrapper(BS->FreePool, 1, plan->ffn_norm);
    if (plan->ffn_gate) uefi_call_wrapper(BS->FreePool, 1, plan->ffn_gate);
    if (plan->ffn_down) uefi_call_wrapper(BS->FreePool, 1, plan->ffn_down);
    if (plan->ffn_up) uefi_call_wrapper(BS->FreePool, 1, plan->ffn_up);
    uefi_call_wrapper(BS->FreePool, 1, plan);
}

static float llmk_f16_to_f32(UINT16 h) {
    // IEEE-754 half to float conversion.
    UINT32 sign = ((UINT32)h & 0x8000u) << 16;
    UINT32 exp = (h >> 10) & 0x1Fu;
    UINT32 mant = (UINT32)h & 0x03FFu;

    UINT32 f;
    if (exp == 0) {
        if (mant == 0) {
            f = sign;
        } else {
            // subnormal
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x03FFu;
            UINT32 exp_f = (UINT32)(exp + (127 - 15));
            f = sign | (exp_f << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        // inf/nan
        f = sign | 0x7F800000u | (mant << 13);
    } else {
        UINT32 exp_f = exp + (127 - 15);
        f = sign | (exp_f << 23) | (mant << 13);
    }

    float out;
    *(UINT32 *)&out = f;
    return out;
}

static UINT64 llmk_align_up_u64(UINT64 x, UINT64 a) {
    if (a == 0) return x;
    return (x + (a - 1)) / a * a;
}

static EFI_STATUS llmk_q8_0_matrix_bytes(UINT64 rows, UINT64 cols, UINT64 *out_bytes) {
    if (!out_bytes) return EFI_INVALID_PARAMETER;
    *out_bytes = 0;
    if (cols == 0 || rows == 0) return EFI_INVALID_PARAMETER;
    if ((cols % 32ULL) != 0) return EFI_INCOMPATIBLE_VERSION;
    UINT64 blocks = cols / 32ULL;
    *out_bytes = rows * blocks * LLMK_Q8_0_BLOCK_BYTES;
    return EFI_SUCCESS;
}

static int llmk_tensor_is_q8_0_2d(const LlmkGgufTensorRef *t) {
    if (!t || !t->present) return 0;
    if (t->type != GGML_TYPE_Q8_0) return 0;
    if (t->n_dims < 2) return 0;
    return 1;
}

static EFI_STATUS llmk_copy_tensor_q8_0_matrix(
    EFI_FILE_HANDLE f,
    UINT64 abs_pos,
    const LlmkGgufTensorRef *t,
    void *dst,
    UINT64 rows,
    UINT64 cols
) {
    if (!f || !t || !dst) return EFI_INVALID_PARAMETER;
    if (!llmk_tensor_is_q8_0_2d(t)) return EFI_UNSUPPORTED;

    // Require exact dims match. (Transpose of block-quant data would require dequant+requant.)
    if (t->dims[0] != cols || t->dims[1] != rows) return EFI_UNSUPPORTED;

    UINT64 bytes = 0;
    EFI_STATUS st = llmk_q8_0_matrix_bytes(rows, cols, &bytes);
    if (EFI_ERROR(st)) return st;

    st = gguf_seek(f, abs_pos);
    if (EFI_ERROR(st)) return st;
    if (bytes == 0) return EFI_SUCCESS;
    return gguf_read_exact(f, dst, (UINTN)bytes);
}

int llmk_gguf_plan_supports_q8_0_blob(const LlmkGgufPlan *plan, int shared_classifier) {
    if (!plan) return 0;
    if (!llmk_tensor_is_q8_0_2d(&plan->tok_embd)) return 0;
    if (!shared_classifier) {
        if (!llmk_tensor_is_q8_0_2d(&plan->output)) return 0;
    }
    for (int l = 0; l < plan->n_layers; l++) {
        if (!llmk_tensor_is_q8_0_2d(&plan->wq[l])) return 0;
        if (!llmk_tensor_is_q8_0_2d(&plan->wk[l])) return 0;
        if (!llmk_tensor_is_q8_0_2d(&plan->wv[l])) return 0;
        if (!llmk_tensor_is_q8_0_2d(&plan->wo[l])) return 0;
        if (!llmk_tensor_is_q8_0_2d(&plan->ffn_gate[l])) return 0;
        if (!llmk_tensor_is_q8_0_2d(&plan->ffn_down[l])) return 0;
        if (!llmk_tensor_is_q8_0_2d(&plan->ffn_up[l])) return 0;
    }
    // Norm vectors can be F16/F32 (loaded as float32), so no strict check here.
    if (!plan->rms_final.present) return 0;
    return 1;
}

EFI_STATUS llmk_gguf_calc_llama2_q8_0_blob_bytes(
    const LlmkGgufPlan *plan,
    int dim,
    int hidden_dim,
    int n_layers,
    int n_heads,
    int n_kv_heads,
    int vocab_size,
    int seq_len,
    int shared_classifier,
    UINT64 *out_bytes
) {
    if (!plan || !out_bytes) return EFI_INVALID_PARAMETER;
    *out_bytes = 0;
    if (!llmk_gguf_plan_supports_q8_0_blob(plan, shared_classifier)) return EFI_UNSUPPORTED;
    if (dim <= 0 || hidden_dim <= 0 || n_layers <= 0 || n_heads <= 0 || n_kv_heads <= 0 || vocab_size <= 0 || seq_len <= 0) return EFI_INVALID_PARAMETER;

    UINT64 dim_u = (UINT64)dim;
    UINT64 hid_u = (UINT64)hidden_dim;
    UINT64 lay_u = (UINT64)n_layers;
    UINT64 vocab_u = (UINT64)vocab_size;

    UINT64 kv_dim = (UINT64)((dim_u * (UINT64)n_kv_heads) / (UINT64)n_heads);
    UINT64 head_size = dim_u / (UINT64)n_heads;

    // Blob layout mirrors llama2.c float order, but with Q8_0 matrices stored as blocks.
    // We align each section to 16 bytes to keep float arrays aligned.
    UINT64 off = 0;
    const UINT64 A = 16;

    UINT64 bytes = 0;

    // token_embedding_table (Q8_0): [vocab, dim]
    off = llmk_align_up_u64(off, A);
    EFI_STATUS st = llmk_q8_0_matrix_bytes(vocab_u, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += bytes;

    // rms_att_weight (F32): [n_layers, dim]
    off = llmk_align_up_u64(off, A);
    off += lay_u * dim_u * 4ULL;

    // wq (Q8_0): per-layer [dim, dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(dim_u, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // wk (Q8_0): per-layer [kv_dim, dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(kv_dim, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // wv (Q8_0): per-layer [kv_dim, dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(kv_dim, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // wo (Q8_0): per-layer [dim, dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(dim_u, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // rms_ffn_weight (F32): [n_layers, dim]
    off = llmk_align_up_u64(off, A);
    off += lay_u * dim_u * 4ULL;

    // w1 (Q8_0): per-layer [hidden_dim, dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(hid_u, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // w2 (Q8_0): per-layer [dim, hidden_dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(dim_u, hid_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // w3 (Q8_0): per-layer [hidden_dim, dim]
    off = llmk_align_up_u64(off, A);
    st = llmk_q8_0_matrix_bytes(hid_u, dim_u, &bytes);
    if (EFI_ERROR(st)) return st;
    off += lay_u * bytes;

    // rms_final_weight (F32): [dim]
    off = llmk_align_up_u64(off, A);
    off += dim_u * 4ULL;

    // freq_cis_real + imag (F32 zeros): [seq_len * head_size / 2] each
    off = llmk_align_up_u64(off, A);
    off += (UINT64)seq_len * head_size / 2ULL * 4ULL;
    off += (UINT64)seq_len * head_size / 2ULL * 4ULL;

    // wcls (Q8_0): [vocab, dim] if not shared
    if (!shared_classifier) {
        off = llmk_align_up_u64(off, A);
        st = llmk_q8_0_matrix_bytes(vocab_u, dim_u, &bytes);
        if (EFI_ERROR(st)) return st;
        off += bytes;
    }

    *out_bytes = off;
    return EFI_SUCCESS;
}

// Forward decl (used by the Q8_0 blob loader before its definition).
static EFI_STATUS llmk_load_tensor_1d(
    EFI_FILE_HANDLE f,
    UINT64 abs_off,
    const LlmkGgufTensorRef *t,
    float *dst,
    UINT64 n,
    void *tmp,
    UINT64 tmp_bytes
);

EFI_STATUS llmk_gguf_load_into_llama2_q8_0_blob(
    EFI_FILE_HANDLE f,
    const LlmkGgufPlan *plan,
    void *blob,
    UINT64 blob_bytes,
    int dim,
    int hidden_dim,
    int n_layers,
    int n_heads,
    int n_kv_heads,
    int vocab_size,
    int seq_len,
    int shared_classifier
) {
    if (!f || !plan || !blob) return EFI_INVALID_PARAMETER;
    if (!llmk_gguf_plan_supports_q8_0_blob(plan, shared_classifier)) return EFI_UNSUPPORTED;
    if (dim <= 0 || hidden_dim <= 0 || n_layers <= 0 || n_heads <= 0 || n_kv_heads <= 0 || vocab_size <= 0 || seq_len <= 0) return EFI_INVALID_PARAMETER;
    if (plan->n_layers != n_layers) return EFI_INCOMPATIBLE_VERSION;

    UINT64 need = 0;
    EFI_STATUS st = llmk_gguf_calc_llama2_q8_0_blob_bytes(plan, dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len, shared_classifier, &need);
    if (EFI_ERROR(st)) return st;
    if (blob_bytes < need) return EFI_BUFFER_TOO_SMALL;

    UINT8 *p = (UINT8 *)blob;
    const UINT64 A = 16;

    UINT64 dim_u = (UINT64)dim;
    UINT64 hid_u = (UINT64)hidden_dim;
    UINT64 lay_u = (UINT64)n_layers;
    UINT64 vocab_u = (UINT64)vocab_size;
    UINT64 kv_dim = (UINT64)((dim_u * (UINT64)n_kv_heads) / (UINT64)n_heads);
    UINT64 head_size = dim_u / (UINT64)n_heads;

    // Reusable row buffer for 1D tensors (norm weights may be F16).
    UINT64 row_buf_bytes = plan->max_src_cols * 4ULL + plan->max_row_raw_bytes;
    if (row_buf_bytes < 4096) row_buf_bytes = 4096;
    void *row_buf = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)row_buf_bytes, (void **)&row_buf);
    if (EFI_ERROR(st) || !row_buf) return EFI_OUT_OF_RESOURCES;

    EFI_STATUS ret = EFI_SUCCESS;
#define LLMK_BAIL(_st) do { ret = (_st); goto done; } while (0)

    // token_embedding_table (Q8_0)
    {
        UINT64 bytes = 0;
        st = llmk_q8_0_matrix_bytes(vocab_u, dim_u, &bytes);
        if (EFI_ERROR(st)) LLMK_BAIL(st);
        UINT64 off = (UINT64)(p - (UINT8 *)blob);
        UINT64 aligned = llmk_align_up_u64(off, A);
        p += (UINTN)(aligned - off);
        UINT64 abs = plan->data_start + plan->tok_embd.offset;
        st = llmk_copy_tensor_q8_0_matrix(f, abs, &plan->tok_embd, p, vocab_u, dim_u);
        if (EFI_ERROR(st)) LLMK_BAIL(st);
        p += (UINTN)bytes;
    }

    // rms_att_weight (F32) [n_layers, dim]
    {
        UINT64 off = (UINT64)(p - (UINT8 *)blob);
        UINT64 aligned = llmk_align_up_u64(off, A);
        p += (UINTN)(aligned - off);
        float *dst = (float *)p;
        for (int l = 0; l < n_layers; l++) {
            UINT64 abs = plan->data_start + plan->attn_norm[l].offset;
            st = llmk_load_tensor_1d(f, abs, &plan->attn_norm[l], dst + (UINTN)l * (UINTN)dim, (UINT64)dim, row_buf, row_buf_bytes);
            if (EFI_ERROR(st)) LLMK_BAIL(st);
        }
        p += (UINTN)(lay_u * dim_u * 4ULL);
    }

    // Helper macro: load per-layer Q8_0 2D tensor batches
#define LLMK_LOAD_Q8_LAYER_TENSOR(arr_field, rows_u, cols_u) do { \
        UINT64 bytes_one = 0; \
        st = llmk_q8_0_matrix_bytes((rows_u), (cols_u), &bytes_one); \
        if (EFI_ERROR(st)) LLMK_BAIL(st); \
        UINT64 off = (UINT64)(p - (UINT8 *)blob); \
        UINT64 aligned = llmk_align_up_u64(off, A); \
        p += (UINTN)(aligned - off); \
        for (int l = 0; l < n_layers; l++) { \
            UINT64 abs = plan->data_start + plan->arr_field[l].offset; \
            st = llmk_copy_tensor_q8_0_matrix(f, abs, &plan->arr_field[l], p + (UINTN)l * (UINTN)bytes_one, (rows_u), (cols_u)); \
            if (EFI_ERROR(st)) LLMK_BAIL(st); \
        } \
        p += (UINTN)(lay_u * bytes_one); \
    } while (0)

    // wq/wk/wv/wo
    LLMK_LOAD_Q8_LAYER_TENSOR(wq, dim_u, dim_u);
    LLMK_LOAD_Q8_LAYER_TENSOR(wk, kv_dim, dim_u);
    LLMK_LOAD_Q8_LAYER_TENSOR(wv, kv_dim, dim_u);
    LLMK_LOAD_Q8_LAYER_TENSOR(wo, dim_u, dim_u);

    // rms_ffn_weight (F32)
    {
        UINT64 off = (UINT64)(p - (UINT8 *)blob);
        UINT64 aligned = llmk_align_up_u64(off, A);
        p += (UINTN)(aligned - off);
        float *dst = (float *)p;
        for (int l = 0; l < n_layers; l++) {
            UINT64 abs = plan->data_start + plan->ffn_norm[l].offset;
            st = llmk_load_tensor_1d(f, abs, &plan->ffn_norm[l], dst + (UINTN)l * (UINTN)dim, (UINT64)dim, row_buf, row_buf_bytes);
            if (EFI_ERROR(st)) LLMK_BAIL(st);
        }
        p += (UINTN)(lay_u * dim_u * 4ULL);
    }

    // w1/w2/w3
    LLMK_LOAD_Q8_LAYER_TENSOR(ffn_gate, hid_u, dim_u);
    LLMK_LOAD_Q8_LAYER_TENSOR(ffn_down, dim_u, hid_u);
    LLMK_LOAD_Q8_LAYER_TENSOR(ffn_up, hid_u, dim_u);

#undef LLMK_LOAD_Q8_LAYER_TENSOR

    // rms_final_weight (F32)
    {
        UINT64 off = (UINT64)(p - (UINT8 *)blob);
        UINT64 aligned = llmk_align_up_u64(off, A);
        p += (UINTN)(aligned - off);
        float *dst = (float *)p;
        UINT64 abs = plan->data_start + plan->rms_final.offset;
        st = llmk_load_tensor_1d(f, abs, &plan->rms_final, dst, dim_u, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) LLMK_BAIL(st);
        p += (UINTN)(dim_u * 4ULL);
    }

    // freq_cis_real + imag: zeros (kept for compatibility with existing layout accounting)
    {
        UINT64 off = (UINT64)(p - (UINT8 *)blob);
        UINT64 aligned = llmk_align_up_u64(off, A);
        p += (UINTN)(aligned - off);
        float *dst = (float *)p;
        UINT64 n = (UINT64)seq_len * head_size / 2ULL;
        for (UINT64 i = 0; i < n; i++) dst[i] = 0.0f;
        dst += n;
        for (UINT64 i = 0; i < n; i++) dst[i] = 0.0f;
        p += (UINTN)(n * 4ULL * 2ULL);
    }

    // wcls (Q8_0) if not shared
    if (!shared_classifier) {
        UINT64 bytes = 0;
        st = llmk_q8_0_matrix_bytes(vocab_u, dim_u, &bytes);
        if (EFI_ERROR(st)) LLMK_BAIL(st);
        UINT64 off = (UINT64)(p - (UINT8 *)blob);
        UINT64 aligned = llmk_align_up_u64(off, A);
        p += (UINTN)(aligned - off);
        UINT64 abs = plan->data_start + plan->output.offset;
        st = llmk_copy_tensor_q8_0_matrix(f, abs, &plan->output, p, vocab_u, dim_u);
        if (EFI_ERROR(st)) LLMK_BAIL(st);
        p += (UINTN)bytes;
    }

done:
    if (row_buf) uefi_call_wrapper(BS->FreePool, 1, row_buf);
#undef LLMK_LOAD_Q8_LAYER_TENSOR
#undef LLMK_BAIL
    return ret;
}

static UINT32 llmk_u32_le(const UINT8 b[4]) {
    return ((UINT32)b[0]) | ((UINT32)b[1] << 8) | ((UINT32)b[2] << 16) | ((UINT32)b[3] << 24);
}

static int llmk_type_supported(UINT32 t) {
    return t == GGML_TYPE_F32 || t == GGML_TYPE_F16 ||
           t == GGML_TYPE_Q4_0 || t == GGML_TYPE_Q4_1 ||
           t == GGML_TYPE_Q5_0 || t == GGML_TYPE_Q5_1 ||
           t == GGML_TYPE_Q8_0;
}

static EFI_STATUS llmk_row_raw_bytes(UINT32 t, UINT64 cols, UINT64 *out_bytes) {
    if (!out_bytes) return EFI_INVALID_PARAMETER;
    *out_bytes = 0;

    if (t == GGML_TYPE_F32) {
        *out_bytes = cols * 4ULL;
        return EFI_SUCCESS;
    }
    if (t == GGML_TYPE_F16) {
        *out_bytes = cols * 2ULL;
        return EFI_SUCCESS;
    }

    // quantized: require cols multiple of 32
    if (cols == 0 || (cols % 32ULL) != 0) return EFI_INCOMPATIBLE_VERSION;
    UINT64 nb = cols / 32ULL;

    if (t == GGML_TYPE_Q4_0) {
        *out_bytes = nb * (UINT64)sizeof(llmk_block_q4_0);
        return EFI_SUCCESS;
    }
    if (t == GGML_TYPE_Q4_1) {
        *out_bytes = nb * (UINT64)sizeof(llmk_block_q4_1);
        return EFI_SUCCESS;
    }
    if (t == GGML_TYPE_Q5_0) {
        *out_bytes = nb * (UINT64)sizeof(llmk_block_q5_0);
        return EFI_SUCCESS;
    }
    if (t == GGML_TYPE_Q5_1) {
        *out_bytes = nb * (UINT64)sizeof(llmk_block_q5_1);
        return EFI_SUCCESS;
    }
    if (t == GGML_TYPE_Q8_0) {
        *out_bytes = nb * (UINT64)sizeof(llmk_block_q8_0);
        return EFI_SUCCESS;
    }

    return EFI_UNSUPPORTED;
}

static EFI_STATUS llmk_read_row_as_f32(
    EFI_FILE_HANDLE f,
    UINT32 type,
    UINT64 cols,
    void *raw_buf,
    UINT64 raw_cap_bytes,
    float *out_f32
) {
    if (!f || !out_f32) return EFI_INVALID_PARAMETER;
    if (!llmk_type_supported(type)) return EFI_UNSUPPORTED;

    UINT64 need = 0;
    EFI_STATUS st = llmk_row_raw_bytes(type, cols, &need);
    if (EFI_ERROR(st)) return st;
    if (need > raw_cap_bytes) return EFI_OUT_OF_RESOURCES;

    if (type == GGML_TYPE_F32) {
        return gguf_read_exact(f, out_f32, (UINTN)need);
    }

    if (!raw_buf) return EFI_INVALID_PARAMETER;
    st = gguf_read_exact(f, raw_buf, (UINTN)need);
    if (EFI_ERROR(st)) return st;

    if (type == GGML_TYPE_F16) {
        const UINT16 *h = (const UINT16 *)raw_buf;
        for (UINT64 i = 0; i < cols; i++) out_f32[i] = llmk_f16_to_f32(h[i]);
        return EFI_SUCCESS;
    }

    // Dequantize blocks
    UINT64 nb = cols / 32ULL;
    if (type == GGML_TYPE_Q4_0) {
        const llmk_block_q4_0 *x = (const llmk_block_q4_0 *)raw_buf;
        for (UINT64 bi = 0; bi < nb; bi++) {
            const float d = llmk_f16_to_f32(x[bi].d);
            for (int j = 0; j < 16; j++) {
                const int x0 = (int)(x[bi].qs[j] & 0x0F) - 8;
                const int x1 = (int)(x[bi].qs[j] >> 4) - 8;
                out_f32[bi * 32ULL + (UINT64)j + 0ULL] = (float)x0 * d;
                out_f32[bi * 32ULL + (UINT64)j + 16ULL] = (float)x1 * d;
            }
        }
        return EFI_SUCCESS;
    }

    if (type == GGML_TYPE_Q4_1) {
        const llmk_block_q4_1 *x = (const llmk_block_q4_1 *)raw_buf;
        for (UINT64 bi = 0; bi < nb; bi++) {
            const float d = llmk_f16_to_f32(x[bi].d);
            const float m = llmk_f16_to_f32(x[bi].m);
            for (int j = 0; j < 16; j++) {
                const int x0 = (int)(x[bi].qs[j] & 0x0F);
                const int x1 = (int)(x[bi].qs[j] >> 4);
                out_f32[bi * 32ULL + (UINT64)j + 0ULL] = (float)x0 * d + m;
                out_f32[bi * 32ULL + (UINT64)j + 16ULL] = (float)x1 * d + m;
            }
        }
        return EFI_SUCCESS;
    }

    if (type == GGML_TYPE_Q5_0) {
        const llmk_block_q5_0 *x = (const llmk_block_q5_0 *)raw_buf;
        for (UINT64 bi = 0; bi < nb; bi++) {
            const float d = llmk_f16_to_f32(x[bi].d);
            const UINT32 qh = llmk_u32_le(x[bi].qh);
            for (int j = 0; j < 16; j++) {
                const UINT8 xh_0 = (UINT8)(((qh >> (j + 0)) << 4) & 0x10);
                const UINT8 xh_1 = (UINT8)(((qh >> (j + 12))     ) & 0x10);

                const int x0 = (int)(((x[bi].qs[j] & 0x0F) | xh_0) - 16);
                const int x1 = (int)(((x[bi].qs[j] >> 4)   | xh_1) - 16);

                out_f32[bi * 32ULL + (UINT64)j + 0ULL] = (float)x0 * d;
                out_f32[bi * 32ULL + (UINT64)j + 16ULL] = (float)x1 * d;
            }
        }
        return EFI_SUCCESS;
    }

    if (type == GGML_TYPE_Q5_1) {
        const llmk_block_q5_1 *x = (const llmk_block_q5_1 *)raw_buf;
        for (UINT64 bi = 0; bi < nb; bi++) {
            const float d = llmk_f16_to_f32(x[bi].d);
            const float m = llmk_f16_to_f32(x[bi].m);
            const UINT32 qh = llmk_u32_le(x[bi].qh);
            for (int j = 0; j < 16; j++) {
                const UINT8 xh_0 = (UINT8)(((qh >> (j + 0)) << 4) & 0x10);
                const UINT8 xh_1 = (UINT8)(((qh >> (j + 12))     ) & 0x10);

                const int x0 = (int)((x[bi].qs[j] & 0x0F) | xh_0);
                const int x1 = (int)((x[bi].qs[j] >> 4)   | xh_1);

                out_f32[bi * 32ULL + (UINT64)j + 0ULL] = (float)x0 * d + m;
                out_f32[bi * 32ULL + (UINT64)j + 16ULL] = (float)x1 * d + m;
            }
        }
        return EFI_SUCCESS;
    }

    if (type == GGML_TYPE_Q8_0) {
        const llmk_block_q8_0 *x = (const llmk_block_q8_0 *)raw_buf;
        for (UINT64 bi = 0; bi < nb; bi++) {
            const float d = llmk_f16_to_f32(x[bi].d);
            for (int j = 0; j < 32; j++) {
                out_f32[bi * 32ULL + (UINT64)j] = (float)x[bi].qs[j] * d;
            }
        }
        return EFI_SUCCESS;
    }

    return EFI_UNSUPPORTED;
}

static EFI_STATUS llmk_read_tensor_row_f16(EFI_FILE_HANDLE f, UINT16 *row, UINT64 n_elems) {
    if (n_elems == 0) return EFI_SUCCESS;
    if (n_elems > 0x7FFFFFFFu) return EFI_OUT_OF_RESOURCES;
    return gguf_read_exact(f, row, (UINTN)(n_elems * 2ULL));
}

static EFI_STATUS llmk_read_tensor_row_f32(EFI_FILE_HANDLE f, float *row, UINT64 n_elems) {
    if (n_elems == 0) return EFI_SUCCESS;
    if (n_elems > 0x7FFFFFFFu) return EFI_OUT_OF_RESOURCES;
    return gguf_read_exact(f, row, (UINTN)(n_elems * 4ULL));
}

static EFI_STATUS llmk_load_tensor_1d(
    EFI_FILE_HANDLE f,
    UINT64 abs_pos,
    const LlmkGgufTensorRef *t,
    float *dst,
    UINT64 n_elems,
    void *row_buf_bytes,
    UINT64 row_buf_cap_bytes
) {
    if (!t || !t->present) return EFI_NOT_FOUND;
    if (t->n_dims != 1) return EFI_INCOMPATIBLE_VERSION;
    if (t->dims[0] != n_elems) return EFI_INCOMPATIBLE_VERSION;
    if (!llmk_type_supported(t->type)) return EFI_UNSUPPORTED;

    EFI_STATUS st = gguf_seek(f, abs_pos);
    if (EFI_ERROR(st)) return st;

    st = llmk_read_row_as_f32(f, t->type, n_elems, row_buf_bytes, row_buf_cap_bytes, dst);
    return st;
}

static EFI_STATUS llmk_load_tensor_2d(
    EFI_FILE_HANDLE f,
    UINT64 abs_pos,
    const LlmkGgufTensorRef *t,
    float *dst,
    UINT64 dst_rows,
    UINT64 dst_cols,
    void *row_buf_bytes,
    UINT64 row_buf_cap_bytes
) {
    if (!t || !t->present) return EFI_NOT_FOUND;
    if (t->n_dims != 2) return EFI_INCOMPATIBLE_VERSION;
    if (!llmk_type_supported(t->type)) return EFI_UNSUPPORTED;

    // GGML storage order: dims[0] is the fastest-changing dimension.
    // Interpret as row-major matrix with rows=dims[1], cols=dims[0].
    UINT64 src_cols = t->dims[0];
    UINT64 src_rows = t->dims[1];

    int mode = 0; // 1=direct, 2=transpose
    if (src_rows == dst_rows && src_cols == dst_cols) mode = 1;
    else if (src_rows == dst_cols && src_cols == dst_rows) mode = 2;
    else return EFI_INCOMPATIBLE_VERSION;

    EFI_STATUS st = gguf_seek(f, abs_pos);
    if (EFI_ERROR(st)) return st;

    for (UINT64 r = 0; r < src_rows; r++) {
        // Dequantize/read to a temporary float row, then copy/transpose into dst.
        // Note: we rely on the caller to provide a buffer >= src_cols floats.
        float *rowf = (float *)row_buf_bytes;
        UINT8 *raw = (UINT8 *)row_buf_bytes + (UINTN)(src_cols * 4ULL);
        UINT64 raw_cap = row_buf_cap_bytes;
        if ((UINT64)(src_cols * 4ULL) >= raw_cap) return EFI_OUT_OF_RESOURCES;
        raw_cap -= (UINT64)(src_cols * 4ULL);

        st = llmk_read_row_as_f32(f, t->type, src_cols, raw, raw_cap, rowf);
        if (EFI_ERROR(st)) return st;

        if (mode == 1) {
            float *out = dst + r * dst_cols;
            for (UINT64 c = 0; c < src_cols; c++) out[c] = rowf[c];
        } else {
            for (UINT64 c = 0; c < src_cols; c++) {
                dst[c * dst_cols + r] = rowf[c];
            }
        }
    }

    return EFI_SUCCESS;
}

EFI_STATUS llmk_gguf_build_plan(
    EFI_FILE_HANDLE f,
    LlmkGgufPlan **out_plan,
    int *out_dim,
    int *out_hidden_dim,
    int *out_n_layers,
    int *out_n_heads,
    int *out_n_kv_heads,
    int *out_vocab_size,
    int *out_seq_len,
    int *out_has_output_weight
) {
    if (!f || !out_plan || !out_dim || !out_hidden_dim || !out_n_layers || !out_n_heads || !out_n_kv_heads || !out_vocab_size || !out_seq_len || !out_has_output_weight) {
        return EFI_INVALID_PARAMETER;
    }
    *out_plan = NULL;

    EFI_STATUS st = gguf_seek(f, 0);
    if (EFI_ERROR(st)) return st;

    UINT8 magic[4];
    st = gguf_read_exact(f, magic, 4);
    if (EFI_ERROR(st)) return st;
    if (!(magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' && magic[3] == 'F')) {
        CHAR16 msg[160];
        SPrint(msg, sizeof(msg), L"GGUF: bad magic: %02x %02x %02x %02x\r\n",
               (unsigned)magic[0], (unsigned)magic[1], (unsigned)magic[2], (unsigned)magic[3]);
        llmk_dbg_print_both(msg);
        return EFI_UNSUPPORTED;
    }

    UINT32 version = 0;
    UINT64 n_tensors = 0;
    UINT64 n_kv = 0;
    st = gguf_read_u32(f, &version);
    if (EFI_ERROR(st)) return st;
    st = gguf_read_u64(f, &n_tensors);
    if (EFI_ERROR(st)) return st;
    st = gguf_read_u64(f, &n_kv);
    if (EFI_ERROR(st)) return st;

    // Extract required hyperparams
    UINT64 dim = 0;
    UINT64 hidden = 0;
    UINT64 n_layers = 0;
    UINT64 n_heads = 0;
    UINT64 n_kv_heads = 0;
    UINT64 vocab = 0;
    UINT64 ctx = 0;

    int debug_prints_left = 12;

    // Unconditional marker so we can see gguf_infer.c diagnostics in the QEMU serial log.
    {
        CHAR16 msg[160];
        SPrint(msg, sizeof(msg), L"GGUF: build_plan start v=%u tensors=%lu kv=%lu\r\n", (unsigned)version, n_tensors, n_kv);
        llmk_dbg_print_both(msg);
    }

    // KV section
    // NOTE: GGUF key length is a uint32 (see gguf_loader.c). Only string lengths are uint64.
    for (UINT64 i = 0; i < n_kv; i++) {
        UINT32 key_len32 = 0;
        st = gguf_read_u32(f, &key_len32);
        if (EFI_ERROR(st)) return st;
        if (key_len32 == 0 || key_len32 > 4096) {
            UINT64 pos = 0;
            (void)gguf_get_pos(f, &pos);
            CHAR16 msg[256];
            SPrint(msg, sizeof(msg), L"GGUF: COMPROMISED_DATA: bad key_len=%u at kv[%lu] (pos=%lu)\r\n",
                   (unsigned)key_len32, i, pos);
            llmk_dbg_print_both(msg);
            return EFI_COMPROMISED_DATA;
        }

        char key_buf[192];
        UINT32 keep = key_len32;
        if (keep > (UINT32)(sizeof(key_buf) - 1)) keep = (UINT32)(sizeof(key_buf) - 1);

        if (keep > 0) {
            st = gguf_read_exact(f, key_buf, (UINTN)keep);
            if (EFI_ERROR(st)) return st;
        }
        key_buf[keep] = 0;

        if (key_len32 > keep) {
            st = gguf_skip(f, (UINT64)(key_len32 - keep));
            if (EFI_ERROR(st)) return st;
        }

        UINT32 vt_u32 = 0;
        st = gguf_read_u32(f, &vt_u32);
        if (EFI_ERROR(st)) return st;
        gguf_kv_type vt = (gguf_kv_type)vt_u32;

        // Helper to read u32/u64 into UINT64
        UINT64 tmp64 = 0;
        int matched = 0;

        if (llmk_key_eq(key_buf, keep, "llama.embedding_length")) {
            matched = 1;
        } else if (llmk_key_eq(key_buf, keep, "llama.feed_forward_length")) {
            matched = 2;
        } else if (llmk_key_eq(key_buf, keep, "llama.block_count")) {
            matched = 3;
        } else if (llmk_key_eq(key_buf, keep, "llama.attention.head_count")) {
            matched = 4;
        } else if (llmk_key_eq(key_buf, keep, "llama.attention.head_count_kv")) {
            matched = 5;
        } else if (llmk_key_eq(key_buf, keep, "llama.vocab_size")) {
            matched = 6;
        } else if (llmk_key_eq(key_buf, keep, "llama.context_length")) {
            matched = 7;
        }

        if (matched) {
            if (vt == GGUF_KV_UINT32) {
                UINT32 v = 0;
                st = gguf_read_u32(f, &v);
                if (EFI_ERROR(st)) return st;
                tmp64 = (UINT64)v;
            } else if (vt == GGUF_KV_UINT64) {
                UINT64 v = 0;
                st = gguf_read_u64(f, &v);
                if (EFI_ERROR(st)) return st;
                tmp64 = v;
            } else {
                // unexpected type
                st = gguf_skip_kv_value(f, vt);
                if (EFI_ERROR(st)) {
                    Print(L"GGUF: failed to skip matched key '%a' value type=%u (status=%r)\r\n", key_buf, (unsigned)vt_u32, st);
                    return st;
                }
                continue;
            }

            switch (matched) {
                case 1: dim = tmp64; break;
                case 2: hidden = tmp64; break;
                case 3: n_layers = tmp64; break;
                case 4: n_heads = tmp64; break;
                case 5: n_kv_heads = tmp64; break;
                case 6: vocab = tmp64; break;
                case 7: ctx = tmp64; break;
            }
            continue;
        }

        // Unhandled key
        st = gguf_skip_kv_value(f, vt);
        if (EFI_ERROR(st)) {
            Print(L"GGUF: failed to skip key '%a' value type=%u (status=%r)\r\n", key_buf, (unsigned)vt_u32, st);
            return st;
        }
    }

    // Some GGUF files (including common llama.cpp exports) may omit llama.vocab_size.
    // In that case, we infer vocab from the token embedding tensor dims during the tensor table scan.
    if (dim == 0 || hidden == 0 || n_layers == 0 || n_heads == 0 || ctx == 0) {
        if (debug_prints_left-- > 0) {
            CHAR16 msg[256];
            SPrint(msg, sizeof(msg), L"GGUF: missing hyperparams after KV scan: dim=%lu hidden=%lu layers=%lu heads=%lu kv_heads=%lu vocab=%lu ctx=%lu\r\n",
                   dim, hidden, n_layers, n_heads, n_kv_heads, vocab, ctx);
            llmk_dbg_print_both(msg);
        }
        return EFI_UNSUPPORTED;
    }
    if (n_kv_heads == 0) n_kv_heads = n_heads;

    if (n_layers > 512 || n_heads > 512 || n_kv_heads > 512) {
        UINT64 pos = 0;
        (void)gguf_get_pos(f, &pos);
        CHAR16 msg[320];
        SPrint(msg, sizeof(msg), L"GGUF: COMPROMISED_DATA: insane hyperparams layers=%lu heads=%lu kv_heads=%lu (pos=%lu)\r\n",
               n_layers, n_heads, n_kv_heads, pos);
        llmk_dbg_print_both(msg);
        return EFI_COMPROMISED_DATA;
    }

    // Allocate plan
    LlmkGgufPlan *plan = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)sizeof(LlmkGgufPlan), (void **)&plan);
    if (EFI_ERROR(st) || !plan) return EFI_OUT_OF_RESOURCES;
    llmk_zero_plan(plan);
    plan->version = version;
    plan->tensor_count = n_tensors;
    plan->kv_count = n_kv;

    st = llmk_alloc_layer_arrays(plan, (int)n_layers);
    if (EFI_ERROR(st)) {
        llmk_gguf_free_plan(plan);
        return st;
    }

    // Tensor table
    UINT64 max_cols = 0;
    UINT64 max_raw_row = 0;
    int all_supported_types = 1;

    for (UINT64 ti = 0; ti < n_tensors; ti++) {
        // NOTE: GGUF tensor name length is a uint32 (see gguf_loader.c).
        UINT32 name_len32 = 0;
        st = gguf_read_u32(f, &name_len32);
        if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }
        if (name_len32 == 0 || name_len32 > 1024U * 1024U) {
            UINT64 pos = 0;
            (void)gguf_get_pos(f, &pos);
            CHAR16 msg[320];
            SPrint(msg, sizeof(msg), L"GGUF: COMPROMISED_DATA: bad tensor name_len=%u at tensor[%lu] (pos=%lu)\r\n",
                   (unsigned)name_len32, ti, pos);
            llmk_dbg_print_both(msg);
            llmk_gguf_free_plan(plan);
            return EFI_COMPROMISED_DATA;
        }

        char name_buf[160];
        UINT32 keep = name_len32;
        if (keep > (UINT32)(sizeof(name_buf) - 1)) keep = (UINT32)(sizeof(name_buf) - 1);
        if (keep > 0) {
            st = gguf_read_exact(f, name_buf, (UINTN)keep);
            if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }
        }
        name_buf[keep] = 0;
        if (name_len32 > keep) {
            st = gguf_skip(f, (UINT64)(name_len32 - keep));
            if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }
        }

        UINT32 n_dims_u32 = 0;
        st = gguf_read_u32(f, &n_dims_u32);
        if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }
        if (n_dims_u32 == 0 || n_dims_u32 > 16) {
            UINT64 pos = 0;
            (void)gguf_get_pos(f, &pos);
            CHAR16 msg[320];
            SPrint(msg, sizeof(msg), L"GGUF: COMPROMISED_DATA: bad n_dims=%u at tensor[%lu] (pos=%lu)\r\n",
                   (unsigned)n_dims_u32, ti, pos);
            llmk_dbg_print_both(msg);
            llmk_gguf_free_plan(plan);
            return EFI_COMPROMISED_DATA;
        }

        UINT64 dims_arr[4];
        for (int k = 0; k < 4; k++) dims_arr[k] = 0;

        for (UINT32 d = 0; d < n_dims_u32; d++) {
            UINT64 dd = 0;
            st = gguf_read_u64(f, &dd);
            if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }
            if (d < 4) dims_arr[d] = dd;
        }

        UINT32 ttype = 0;
        st = gguf_read_u32(f, &ttype);
        if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }
        UINT64 off = 0;
        st = gguf_read_u64(f, &off);
        if (EFI_ERROR(st)) { llmk_gguf_free_plan(plan); return st; }

        // Track max cols for buffering (dims[0])
        if (dims_arr[0] > max_cols) max_cols = dims_arr[0];

        int layer = -1;
        LlmkTensorRole role = ROLE_NONE;
        if (llmk_parse_role(name_buf, &layer, &role)) {
            // Only allow types we can dequantize.
            if (!llmk_type_supported(ttype)) {
                all_supported_types = 0;
                if (debug_prints_left-- > 0) {
                    CHAR16 name16[164];
                    for (UINTN k = 0; k + 1 < (UINTN)(sizeof(name16) / sizeof(name16[0])) && name_buf[k]; k++) {
                        name16[k] = (CHAR16)((UINT8)name_buf[k]);
                        name16[k + 1] = 0;
                    }
                    CHAR16 msg[320];
                    SPrint(msg, sizeof(msg), L"GGUF: unsupported ggml_type=%u for tensor '%s' dims=[%lu,%lu,%lu,%lu]\r\n",
                           (unsigned)ttype, name16, dims_arr[0], dims_arr[1], dims_arr[2], dims_arr[3]);
                    llmk_dbg_print_both(msg);
                }
            }

            // Validate row shape constraints for quant types (cols multiple-of-32) and track max raw bytes.
            {
                UINT64 need = 0;
                EFI_STATUS st_need = llmk_row_raw_bytes(ttype, dims_arr[0], &need);
                if (EFI_ERROR(st_need)) {
                    all_supported_types = 0;
                    if (debug_prints_left-- > 0) {
                        CHAR16 name16[164];
                        for (UINTN k = 0; k + 1 < (UINTN)(sizeof(name16) / sizeof(name16[0])) && name_buf[k]; k++) {
                            name16[k] = (CHAR16)((UINT8)name_buf[k]);
                            name16[k + 1] = 0;
                        }
                        CHAR16 msg[320];
                        SPrint(msg, sizeof(msg), L"GGUF: unsupported row layout for tensor '%s' type=%u cols=%lu (status=%r)\r\n",
                               name16, (unsigned)ttype, dims_arr[0], st_need);
                        llmk_dbg_print_both(msg);
                    }
                } else {
                    if (need > max_raw_row) max_raw_row = need;
                }
            }

            LlmkGgufTensorRef tr;
            tr.offset = off;
            tr.type = ttype;
            tr.n_dims = n_dims_u32;
            tr.present = 1;
            for (int k = 0; k < 4; k++) tr.dims[k] = dims_arr[k];

            if (role == ROLE_TOK_EMBD) {
                // Infer vocab size if missing from KV.
                if (vocab == 0 && n_dims_u32 == 2) {
                    // GGML storage order: dims[0] is fastest-changing.
                    // Token embedding is typically [dim, vocab] or [vocab, dim].
                    if (dims_arr[0] == dim) vocab = dims_arr[1];
                    else if (dims_arr[1] == dim) vocab = dims_arr[0];
                }
                plan->tok_embd = tr;
            } else if (role == ROLE_OUTPUT) {
                plan->output = tr;
            } else if (role == ROLE_RMS_FINAL) {
                plan->rms_final = tr;
            } else if (layer >= 0 && layer < plan->n_layers) {
                switch (role) {
                    case ROLE_ATTN_NORM: plan->attn_norm[layer] = tr; break;
                    case ROLE_WQ: plan->wq[layer] = tr; break;
                    case ROLE_WK: plan->wk[layer] = tr; break;
                    case ROLE_WV: plan->wv[layer] = tr; break;
                    case ROLE_WO: plan->wo[layer] = tr; break;
                    case ROLE_FFN_NORM: plan->ffn_norm[layer] = tr; break;
                    case ROLE_FFN_GATE: plan->ffn_gate[layer] = tr; break;
                    case ROLE_FFN_DOWN: plan->ffn_down[layer] = tr; break;
                    case ROLE_FFN_UP: plan->ffn_up[layer] = tr; break;
                    default: break;
                }
            }
        }
    }

    if (!all_supported_types) {
        llmk_gguf_free_plan(plan);
        return EFI_UNSUPPORTED;
    }

    // Record data section start
    UINT64 data_start = 0;
    st = gguf_get_pos(f, &data_start);
    if (EFI_ERROR(st)) {
        llmk_gguf_free_plan(plan);
        return st;
    }
    plan->data_start = data_start;
    plan->max_src_cols = max_cols;
    plan->max_row_raw_bytes = max_raw_row;

    // Sanity: required tensors
    if (!plan->tok_embd.present || !plan->rms_final.present) {
        llmk_gguf_free_plan(plan);
        return EFI_NOT_FOUND;
    }

    if (vocab == 0) {
        if (debug_prints_left-- > 0) {
            CHAR16 msg[256];
            SPrint(msg, sizeof(msg), L"GGUF: vocab_size unknown (KV missing and token_embd dims did not match dim=%lu).\r\n", dim);
            llmk_dbg_print_both(msg);
            if (plan->tok_embd.present) {
                SPrint(msg, sizeof(msg), L"GGUF: token_embd dims=[%lu,%lu,%lu,%lu] type=%u\r\n",
                       plan->tok_embd.dims[0], plan->tok_embd.dims[1], plan->tok_embd.dims[2], plan->tok_embd.dims[3], (unsigned)plan->tok_embd.type);
                llmk_dbg_print_both(msg);
            }
        }
        llmk_gguf_free_plan(plan);
        return EFI_UNSUPPORTED;
    }
    for (int l = 0; l < plan->n_layers; l++) {
        if (!plan->attn_norm[l].present || !plan->wq[l].present || !plan->wk[l].present || !plan->wv[l].present || !plan->wo[l].present ||
            !plan->ffn_norm[l].present || !plan->ffn_gate[l].present || !plan->ffn_down[l].present || !plan->ffn_up[l].present) {
            llmk_gguf_free_plan(plan);
            return EFI_NOT_FOUND;
        }
    }

    *out_dim = (int)dim;
    *out_hidden_dim = (int)hidden;
    *out_n_layers = (int)n_layers;
    *out_n_heads = (int)n_heads;
    *out_n_kv_heads = (int)n_kv_heads;
    *out_vocab_size = (int)vocab;
    *out_seq_len = (int)ctx;
    *out_has_output_weight = plan->output.present ? 1 : 0;

    *out_plan = plan;
    return EFI_SUCCESS;
}

EFI_STATUS llmk_gguf_load_into_llama2_layout(
    EFI_FILE_HANDLE f,
    const LlmkGgufPlan *plan,
    float *weights_mem,
    int dim,
    int hidden_dim,
    int n_layers,
    int n_heads,
    int n_kv_heads,
    int vocab_size,
    int seq_len,
    int shared_classifier
) {
    if (!f || !plan || !weights_mem) return EFI_INVALID_PARAMETER;
    if (dim <= 0 || hidden_dim <= 0 || n_layers <= 0 || n_heads <= 0 || n_kv_heads <= 0 || vocab_size <= 0 || seq_len <= 0) return EFI_INVALID_PARAMETER;
    if (plan->n_layers != n_layers) return EFI_INCOMPATIBLE_VERSION;

    int kv_dim = (dim * n_kv_heads) / n_heads;
    int head_size = dim / n_heads;

    // Allocate a reusable row buffer.
    UINT64 max_cols = plan->max_src_cols;
    if (max_cols < 1) max_cols = (UINT64)dim;

    // We need space for: one float row (src_cols floats) + worst-case raw encoded row
    // Using plan maxima ensures llmk_load_tensor_2d can place raw bytes after the float row.
    UINT64 row_buf_bytes = max_cols * 4ULL + plan->max_row_raw_bytes;
    if (row_buf_bytes < 4096) row_buf_bytes = 4096;

    void *row_buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)row_buf_bytes, (void **)&row_buf);
    if (EFI_ERROR(st) || !row_buf) return EFI_OUT_OF_RESOURCES;

    float *p = weights_mem;

    // token_embedding_table: [vocab, dim]
    {
        UINT64 abs = plan->data_start + plan->tok_embd.offset;
        st = llmk_load_tensor_2d(f, abs, &plan->tok_embd, p, (UINT64)vocab_size, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)vocab_size * (UINTN)dim;
    }

    // rms_att_weight: [n_layers, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->attn_norm[l].offset;
        st = llmk_load_tensor_1d(f, abs, &plan->attn_norm[l], p, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim;
    }

    // wq: per-layer [dim, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->wq[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->wq[l], p, (UINT64)dim, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim * (UINTN)dim;
    }

    // wk: per-layer [kv_dim, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->wk[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->wk[l], p, (UINT64)kv_dim, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim * (UINTN)kv_dim;
    }

    // wv: per-layer [kv_dim, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->wv[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->wv[l], p, (UINT64)kv_dim, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim * (UINTN)kv_dim;
    }

    // wo: per-layer [dim, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->wo[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->wo[l], p, (UINT64)dim, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim * (UINTN)dim;
    }

    // rms_ffn_weight: [n_layers, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->ffn_norm[l].offset;
        st = llmk_load_tensor_1d(f, abs, &plan->ffn_norm[l], p, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim;
    }

    // w1 (ffn_gate): [hidden_dim, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->ffn_gate[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->ffn_gate[l], p, (UINT64)hidden_dim, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim * (UINTN)hidden_dim;
    }

    // w2 (ffn_down): [dim, hidden_dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->ffn_down[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->ffn_down[l], p, (UINT64)dim, (UINT64)hidden_dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)hidden_dim * (UINTN)dim;
    }

    // w3 (ffn_up): [hidden_dim, dim]
    for (int l = 0; l < n_layers; l++) {
        UINT64 abs = plan->data_start + plan->ffn_up[l].offset;
        st = llmk_load_tensor_2d(f, abs, &plan->ffn_up[l], p, (UINT64)hidden_dim, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim * (UINTN)hidden_dim;
    }

    // rms_final_weight: [dim]
    {
        UINT64 abs = plan->data_start + plan->rms_final.offset;
        st = llmk_load_tensor_1d(f, abs, &plan->rms_final, p, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        p += (UINTN)dim;
    }

    // freq_cis_real + freq_cis_imag: unused in this fork; fill with zeros.
    {
        UINTN n = (UINTN)seq_len * (UINTN)head_size / 2;
        for (UINTN i = 0; i < n; i++) p[i] = 0.0f;
        p += n;
        for (UINTN i = 0; i < n; i++) p[i] = 0.0f;
        p += n;
    }

    // wcls
    if (!shared_classifier) {
        if (!plan->output.present) { st = EFI_NOT_FOUND; goto done; }
        UINT64 abs = plan->data_start + plan->output.offset;
        st = llmk_load_tensor_2d(f, abs, &plan->output, p, (UINT64)vocab_size, (UINT64)dim, row_buf, row_buf_bytes);
        if (EFI_ERROR(st)) goto done;
        // p += vocab*dim; (not needed)
    }

    st = EFI_SUCCESS;

done:
    if (row_buf) uefi_call_wrapper(BS->FreePool, 1, row_buf);
    return st;
}
