
static void llmk_load_repl_cfg_diopion_best_effort(DiopionEngine *e) {
    if (!e) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    int applied = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "diopion_mode")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                diopion_set_mode(e, (DiopionMode)v);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_profile")) {
            if (val && val[0]) {
                // token -> enum
                if (llmk_cfg_streq_ci(val, "animal")) diopion_set_profile(e, DIOPION_PROFILE_ANIMAL);
                else if (llmk_cfg_streq_ci(val, "vegetal")) diopion_set_profile(e, DIOPION_PROFILE_VEGETAL);
                else if (llmk_cfg_streq_ci(val, "geom") || llmk_cfg_streq_ci(val, "geometric")) diopion_set_profile(e, DIOPION_PROFILE_GEOM);
                else if (llmk_cfg_streq_ci(val, "bio") || llmk_cfg_streq_ci(val, "biological")) diopion_set_profile(e, DIOPION_PROFILE_BIO);
                else diopion_set_profile(e, DIOPION_PROFILE_NONE);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_turns")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 16) v = 16;
                e->params.burst_turns_default = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_tokens") || llmk_cfg_streq_ci(key, "diopion_burst_max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 16) v = 16;
                if (v > 1024) v = 1024;
                e->params.burst_max_gen_tokens = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_topk")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 200) v = 200;
                e->params.burst_top_k = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_temp_milli") || llmk_cfg_streq_ci(key, "diopion_burst_temp")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 50) v = 50;
                if (v > 2000) v = 2000;
                e->params.burst_temp_milli = (UINT32)v;
                applied = 1;
            }
        }
    }

    if (applied) {
        Print(L"[cfg] diopion: mode=");
        llmk_print_ascii(diopion_mode_name_ascii(e->mode));
        Print(L" profile=");
        llmk_print_ascii(diopion_profile_name_ascii(e->profile));
        Print(L"\r\n");
    }
}

static EFI_STATUS llmk_cortex_load_from_file(const CHAR16 *path16) {
    if (!g_root) return EFI_NOT_READY;
    
    EFI_FILE_HANDLE cfh = NULL;
    EFI_STATUS cst = uefi_call_wrapper(g_root->Open, 5, g_root, &cfh, path16, EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(cst)) return cst;

    EFI_FILE_INFO *cfi = NULL; UINTN cfisz = SIZE_OF_EFI_FILE_INFO + 256;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, cfisz, (void **)&cfi);
    if (!cfi) { uefi_call_wrapper(cfh->Close, 1, cfh); return EFI_OUT_OF_RESOURCES; }
    uefi_call_wrapper(cfh->GetInfo, 4, cfh, &gEfiFileInfoGuid, &cfisz, cfi);
    UINT64 csz = cfi->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, cfi);

    void *cbuf = llmk_arena_alloc(&g_zones, LLMK_ARENA_ZONE_C, csz, 64);
    if (!cbuf) { uefi_call_wrapper(cfh->Close, 1, cfh); return EFI_OUT_OF_RESOURCES; }

    UINT8 *cdst = (UINT8 *)cbuf; UINT64 crem = csz;
    while (crem > 0) {
        UINTN chunk = (crem > 4*1024*1024) ? 4*1024*1024 : (UINTN)crem;
        cst = uefi_call_wrapper(cfh->Read, 3, cfh, &chunk, cdst);
        if (EFI_ERROR(cst) || chunk == 0) { uefi_call_wrapper(cfh->Close, 1, cfh); return EFI_DEVICE_ERROR; }
        cdst += chunk; crem -= chunk;
    }
    uefi_call_wrapper(cfh->Close, 1, cfh);

    OosiV3Header *ch = (OosiV3Header *)cbuf;
    if (ch->magic != OOSI_V3_MAGIC) return EFI_UNSUPPORTED;

    int cD  = (int)ch->d_model;
    int cDi = (int)(ch->d_model * ch->expand);
    int cS  = (int)ch->d_state;
    int cDc = (int)ch->d_conv;
    int cDt = (int)ch->dt_rank;
    int cN  = (int)ch->n_layer;
    int cV  = (int)ch->vocab_size;
    int cHd = (int)ch->halt_d_input;

    UINT64 csc_b = (UINT64)(3*cD + 4*cDi + cDt + 2*cS + 4) * sizeof(float);
    UINT64 chs_b = (UINT64)cN * cDi * cS * sizeof(float);
    UINT64 ccv_b = (UINT64)cN * cDi * cDc * sizeof(float);
    UINT64 ccp_b = (UINT64)cN * sizeof(int);

    g_soma_cortex.scratch  = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, csc_b, 64);
    g_soma_cortex.logits   = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, (UINT64)cV*sizeof(float), 16);
    g_soma_cortex.h_state  = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, chs_b, 64);
    g_soma_cortex.conv_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, ccv_b, 64);
    g_soma_cortex.conv_pos = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, ccp_b, 4);
    g_soma_cortex.halt_h1  = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, 512*sizeof(float), 16);
    g_soma_cortex.halt_h2  = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, 64*sizeof(float), 16);
    g_soma_cortex.halt_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, (UINT64)(cHd+1)*sizeof(float), 16);

    if (!g_soma_cortex.scratch || !g_soma_cortex.logits || !g_soma_cortex.h_state) return EFI_OUT_OF_RESOURCES;

    char mname[64];
    llmk_copy_char16_to_ascii(mname, 64, path16);
    int cr = soma_cortex_load(&g_soma_cortex, cbuf, csz, mname);
    return (cr == 0) ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

static EFI_STATUS llmk_ssm_v3_load_from_file(const CHAR16 *path16) {
    if (!g_root) return EFI_NOT_READY;
    
    EFI_FILE_HANDLE cfh = NULL;
    EFI_STATUS cst = uefi_call_wrapper(g_root->Open, 5, g_root, &cfh, path16, EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(cst)) return cst;

    EFI_FILE_INFO *cfi = NULL; UINTN cfisz = SIZE_OF_EFI_FILE_INFO + 256;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, cfisz, (void **)&cfi);
    if (!cfi) { uefi_call_wrapper(cfh->Close, 1, cfh); return EFI_OUT_OF_RESOURCES; }
    uefi_call_wrapper(cfh->GetInfo, 4, cfh, &gEfiFileInfoGuid, &cfisz, cfi);
    UINT64 csz = cfi->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, cfi);

    void *cbuf = llmk_arena_alloc(&g_zones, LLMK_ARENA_WEIGHTS, csz, 64);
    if (!cbuf) { uefi_call_wrapper(cfh->Close, 1, cfh); return EFI_OUT_OF_RESOURCES; }

    UINT8 *cdst = (UINT8 *)cbuf; UINT64 crem = csz;
    while (crem > 0) {
        UINTN chunk = (crem > 4*1024*1024) ? 4*1024*1024 : (UINTN)crem;
        cst = uefi_call_wrapper(cfh->Read, 3, cfh, &chunk, cdst);
        if (EFI_ERROR(cst) || chunk == 0) { uefi_call_wrapper(cfh->Close, 1, cfh); return EFI_DEVICE_ERROR; }
        cdst += chunk; crem -= chunk;
    }
    uefi_call_wrapper(cfh->Close, 1, cfh);

    SsmStatus st = oosi_v3_load(&g_oosi_v3_weights, cbuf, csz);
    if (st != SSM_OK) return EFI_LOAD_ERROR;

    g_oosi_v3_valid = 1;
    Print(L"[SSM-v3] Loaded Architect brain (%d MB)\r\n", (int)(csz/(1024*1024)));
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_diop_load_model(const char *role, const char *path) {
    if (!role || !path) return EFI_INVALID_PARAMETER;

    Print(L"[DIOP] Specialized loading for role: %a from %a\r\n", role, path);

    // Convert path to CHAR16
    CHAR16 path16[192];
    ascii_to_char16(path16, path, 192);

    if (llmk_ascii_streq(role, "warden")) {
        // Load into cortex (ZONE_C)
        return llmk_cortex_load_from_file(path16);
    } else if (llmk_ascii_streq(role, "architect")) {
        // Architect uses SSM-v3 infrastructure
        return llmk_ssm_v3_load_from_file(path16);
    } else if (llmk_ascii_streq(role, "core")) {
        // Core is the main backbone
        // We need to trigger a re-load of the main model
        // This usually happens via /load or at boot, but we can bridge it
        Print(L"[DIOP] Core loading requires system reset or /ssm_load.\r\n");
        return EFI_UNSUPPORTED;
    }

    return EFI_NOT_FOUND;
}

static const CHAR16 *djibion_mode_name(DjibionMode m) {
    if (m == DJIBION_MODE_OFF) return L"off";
    if (m == DJIBION_MODE_OBSERVE) return L"observe";
    if (m == DJIBION_MODE_ENFORCE) return L"enforce";
    return L"?";
}

static const CHAR16* oo_mode_name_w(int mode, int engine_type) {
    switch(engine_type) {
        case 0: /* evolvion */
            if (mode == 0) return L"off";
            if (mode == 1) return L"stale";
            if (mode == 2) return L"live";
            break;
        case 2: /* conscience */
            if (mode == 0) return L"off";
            if (mode == 1) return L"act";
            if (mode == 2) return L"obs";
            break;
        case 8: /* collectivion */
            if (mode == 0) return L"off";
            if (mode == 1) return L"active";
            if (mode == 2) return L"passive";
            break;
        case 9: /* metabion */
            if (mode == 0) return L"off";
            if (mode == 1) return L"track";
            if (mode == 2) return L"guide";
            break;
        case 11: /* pheromion */
            if (mode == 0) return L"off";
            if (mode == 1) return L"trace";
            if (mode == 2) return L"boost";
            break;
        default:
            if (mode == 0) return L"off";
            if (mode == 1) return L"on";
            break;
    }
    return L"?";
}

// Forward decl: used by early repl.cfg loaders before definition.
static int llmk_cfg_parse_bool(const char *s, int *out);

static int djibion_should_block(const DjibionEngine *e, const DjibionDecision *d) {
    if (!e || !d) return 0;
    if (e->mode != DJIBION_MODE_ENFORCE) return 0;
    return (d->verdict == DJIBION_VERDICT_REJECT || d->verdict == DJIBION_VERDICT_FREEZE);
}
#define DIM 288
#define HIDDEN_DIM 768
#define N_LAYERS 6
#define N_HEADS 6
#define N_KV_HEADS 6
#define VOCAB_SIZE 32000
#define SEQ_LEN 256
#define MAX_TOKENS 256

// Token ids used by this tiny tokenizer export.
// NOTE: encode() currently inserts BOS=1.
#define TOKEN_BOS 1
#define TOKEN_EOS 2

static int has_suffix_repeat(const int* tokens, int n_tokens, int span) {
    if (span <= 0) return 0;
    if (n_tokens < 2 * span) return 0;
    for (int i = 0; i < span; i++) {
        if (tokens[n_tokens - span + i] != tokens[n_tokens - 2 * span + i]) return 0;
    }
    return 1;
}

// AVX2 attention helpers live in attention_avx2.c (compiled with -mavx2)
float llmk_dot_f32_avx2(const float *a, const float *b, int n);
void llmk_axpy_f32_avx2(float *dst, const float *src, float alpha, int n);
void llmk_kv_prefetch_range(const float *base, int stride, int row_len, int row_count);
void llmk_kv_slice_keys_avx2(float *out, const float *key_cache, int kv_dim, int head_size, int kv_head_idx, int pos);
void llmk_kv_slice_values_avx2(float *out, const float *value_cache, int kv_dim, int head_size, int kv_head_idx, int pos);

static int g_attn_use_avx2 = 0;
// -1=auto, 0=force SSE2, 1=force AVX2 (only allowed if auto-detected AVX2 is enabled)
static int g_attn_force = -1;

// One-shot fail-safe test harness.
static int g_test_failsafe_active = 0;
static BOOLEAN g_test_failsafe_prev_strict_budget = FALSE;
static UINT64 g_test_failsafe_prev_prefill = 0;
static UINT64 g_test_failsafe_prev_decode = 0;

static void uefi_print_utf8_decode(const unsigned char *p, int len) {
    if (!p || len <= 0) return;

    // Convert UTF-8 bytes to UTF-16 and stream to the UEFI console.
    // Uses U+FFFD replacement on invalid sequences.
    CHAR16 out[256];
    int out_len = 0;

    int i = 0;
    while (i < len) {
        UINT32 cp = 0xFFFD;
        unsigned char b0 = p[i];

        if (b0 < 0x80) {
            cp = (UINT32)b0;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 < len) {
                unsigned char b1 = p[i + 1];
                if ((b1 & 0xC0) == 0x80) {
                    cp = ((UINT32)(b0 & 0x1F) << 6) | (UINT32)(b1 & 0x3F);
                    if (cp < 0x80) cp = 0xFFFD;
                    i += 2;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 < len) {
                unsigned char b1 = p[i + 1];
                unsigned char b2 = p[i + 2];
                if (((b1 & 0xC0) == 0x80) && ((b2 & 0xC0) == 0x80)) {
                    cp = ((UINT32)(b0 & 0x0F) << 12) | ((UINT32)(b1 & 0x3F) << 6) | (UINT32)(b2 & 0x3F);
                    if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                    i += 3;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 < len) {
                unsigned char b1 = p[i + 1];
                unsigned char b2 = p[i + 2];
                unsigned char b3 = p[i + 3];
                if (((b1 & 0xC0) == 0x80) && ((b2 & 0xC0) == 0x80) && ((b3 & 0xC0) == 0x80)) {
                    cp = ((UINT32)(b0 & 0x07) << 18) | ((UINT32)(b1 & 0x3F) << 12) | ((UINT32)(b2 & 0x3F) << 6) | (UINT32)(b3 & 0x3F);
                    if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
                    i += 4;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }

        if (out_len > (int)(sizeof(out) / sizeof(out[0])) - 3) {
            out[out_len] = 0;
            uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, out);
            out_len = 0;
        }

        if (cp <= 0xFFFF) {
            out[out_len++] = (CHAR16)cp;
        } else {
            cp -= 0x10000;
            out[out_len++] = (CHAR16)(0xD800 + (cp >> 10));
            out[out_len++] = (CHAR16)(0xDC00 + (cp & 0x3FF));
        }
    }

    if (out_len > 0) {
        out[out_len] = 0;
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, out);
    }
}

// -----------------------------------------------------------------------------
// Minimal serial debug (COM1) so QEMU -serial file captures key diagnostics.
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

static __inline__ void llmk_outw(UINT16 port, UINT16 val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
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
static void llmk_serial_write_char16(const CHAR16 *s) { (void)s; }
#endif

static void llmk_shutdown_best_effort(void) {
    if (RT && RT->ResetSystem) {
        uefi_call_wrapper(RT->ResetSystem, 4, EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    }

#if defined(__x86_64__) || defined(_M_X64)
    // QEMU/Bochs fallback poweroff ports for cases where ResetSystem returns
    // but firmware does not actually power down the VM.
    llmk_outw(0x604, 0x2000);
    llmk_outw(0xB004, 0x2000);

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
#endif
}

// Some generations still contain a classic mojibake sequence for U+2019 (RIGHT SINGLE QUOTATION MARK).
// This can span token boundaries, so keep a small byte tail and repair across calls.
static unsigned char g_utf8_repair_tail[5]; // SAFE: tail buffer for cross-call UTF-8 repair; bounded by keep=5
static int g_utf8_repair_tail_len = 0;

// GOP transcript (best-effort): capture streamed UTF-8 output into an ASCII-ish ring buffer
// so we can render it later in the GOP UI.
static void llmk_tr_append_ascii_bytes(const unsigned char *bytes, int len);

// ============================================================
// llmk_decode_piece — convert raw vocab token string to printable bytes
// Handles SentencePiece conventions:
//   1. "<0xNN>" byte tokens → actual byte value
//   2. "▁" (U+2581, 3 bytes E2 96 81) leading space → ' '
//   3. Raw string → returned as-is
// out_buf must be at least 4 bytes. Returns actual byte count.
// ============================================================
static int llmk_decode_piece(const char *piece, char *out_buf, int out_size) {
    if (!piece || !out_buf || out_size <= 0) return 0;

    int len = 0;
    while (piece[len]) len++;
    if (len == 0) return 0;

    // Case 1: byte token "<0xNN>" (exactly 6 chars)
    if (len == 6 && piece[0] == '<' && piece[1] == '0' && piece[2] == 'x' && piece[5] == '>') {
        char hi = piece[3], lo = piece[4];
        int hv = (hi >= '0' && hi <= '9') ? hi-'0' : (hi >= 'a' && hi <= 'f') ? hi-'a'+10 : (hi >= 'A' && hi <= 'F') ? hi-'A'+10 : -1;
        int lv = (lo >= '0' && lo <= '9') ? lo-'0' : (lo >= 'a' && lo <= 'f') ? lo-'a'+10 : (lo >= 'A' && lo <= 'F') ? lo-'A'+10 : -1;
        if (hv >= 0 && lv >= 0 && out_size >= 1) {
            out_buf[0] = (char)((hv << 4) | lv);
            return 1;
        }
    }

    // Case 2: SentencePiece leading "▁" (U+2581 = E2 96 81) → space
    // Replace only the leading ▁; keep remaining bytes as-is
    if (len >= 3 && (unsigned char)piece[0] == 0xE2 &&
                    (unsigned char)piece[1] == 0x96 &&
                    (unsigned char)piece[2] == 0x81) {
        int rest = len - 3;
        if (out_size < 1 + rest) rest = out_size - 1;
        if (rest < 0) rest = 0;
        if (out_size >= 1) out_buf[0] = ' ';
        for (int i = 0; i < rest; i++) out_buf[1 + i] = piece[3 + i];
        return 1 + rest;
    }

    // Case 3: raw string — copy up to out_size bytes
    int copy = len < out_size ? len : out_size;
    for (int i = 0; i < copy; i++) out_buf[i] = piece[i];
    return copy;
}


static void uefi_print_utf8_bytes(const char *bytes, int len) {
    if (!bytes || len <= 0) return;

    typedef struct {
        unsigned char pat[6]; // SAFE: fixed UTF-8 pattern (6 bytes)
        unsigned char rep[3]; // SAFE: fixed UTF-8 replacement (3 bytes)
    } Mojimap;

    // Common mojibake seen in generations (CP437-ish smart punctuation).
    // Each pat is UTF-8 for the visible mojibake string; rep is UTF-8 for the intended punctuation.
    static const Mojimap maps[] = {
        // mojibake(right single quote) -> U+2019
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC3, 0x96 }, { 0xE2, 0x80, 0x99 } },
        // mojibake(left double quote) -> U+201C
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC2, 0xA3 }, { 0xE2, 0x80, 0x9C } },
        // mojibake(right double quote) -> U+201D
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC3, 0x98 }, { 0xE2, 0x80, 0x9D } },
        // mojibake(em dash) -> U+2014
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC3, 0xB6 }, { 0xE2, 0x80, 0x94 } },
        // mojibake(ellipsis) -> U+2026
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC2, 0xAA }, { 0xE2, 0x80, 0xA6 } },
    };

    const int keep = 5; // pat_len - 1
    unsigned char inbuf[512];
    unsigned char outbuf[512];

    int offset = 0;
    while (offset < len) {
        int inlen = 0;
        for (int i = 0; i < g_utf8_repair_tail_len && inlen < (int)sizeof(inbuf); i++) {
            inbuf[inlen++] = g_utf8_repair_tail[i];
        }

        int cap = (int)sizeof(inbuf) - inlen;
        int take = len - offset;
        if (take > cap) take = cap;
        for (int i = 0; i < take; i++) {
            inbuf[inlen++] = (unsigned char)bytes[offset + i];
        }
        offset += take;

        if (inlen <= 0) return;

        if (inlen <= keep) {
            g_utf8_repair_tail_len = inlen;
            for (int i = 0; i < inlen; i++) g_utf8_repair_tail[i] = inbuf[i];
            continue;
        }

        int upto = inlen - keep;
        int outlen = 0;
        int j = 0;
        while (j < upto && outlen < (int)sizeof(outbuf)) {
            int matched = 0;
            if (j + 6 <= upto) {
                for (UINTN m = 0; m < (sizeof(maps) / sizeof(maps[0])); m++) {
                    const Mojimap *mm = &maps[m];
                    if (inbuf[j + 0] == mm->pat[0] && inbuf[j + 1] == mm->pat[1] && inbuf[j + 2] == mm->pat[2] &&
                        inbuf[j + 3] == mm->pat[3] && inbuf[j + 4] == mm->pat[4] && inbuf[j + 5] == mm->pat[5]) {
                        if (outlen + 3 <= (int)sizeof(outbuf)) {
                            outbuf[outlen++] = mm->rep[0];
                            outbuf[outlen++] = mm->rep[1];
                            outbuf[outlen++] = mm->rep[2];
                        }
                        j += 6;
                        matched = 1;
                        break;
                    }
                }
            }
            if (matched) continue;
            outbuf[outlen++] = inbuf[j++];
        }

        // Save tail for boundary-spanning repair.
        g_utf8_repair_tail_len = keep;
        for (int i = 0; i < keep; i++) g_utf8_repair_tail[i] = inbuf[upto + i];

        // Decode+print processed bytes.
        llmk_tr_append_ascii_bytes(outbuf, outlen);
        uefi_print_utf8_decode(outbuf, outlen);

        // If we ever filled the buffer before consuming all of upto, drop the remainder to avoid
        // stalling. This should be extremely rare with typical tokenizer pieces.
        // (We intentionally keep this minimal and avoid heap allocations.)
        if (j < upto) {
            // best-effort: continue printing remaining bytes directly (no repair inside this chunk)
            llmk_tr_append_ascii_bytes(inbuf + j, upto - j);
            uefi_print_utf8_decode(inbuf + j, upto - j);
        }
    }
}

static void uefi_print_utf8_flush(void) {
    if (g_utf8_repair_tail_len <= 0) return;
    uefi_print_utf8_decode(g_utf8_repair_tail, g_utf8_repair_tail_len);
    g_utf8_repair_tail_len = 0;
}

// Best-effort: enable AVX state (OSXSAVE + XCR0) in UEFI so AVX/AVX2 code can run.
// Without an OS, some firmwares leave XCR0 unset; QEMU/OVMF often does.
static inline void cpuidex_u32(UINT32 leaf, UINT32 subleaf, UINT32 *eax, UINT32 *ebx, UINT32 *ecx, UINT32 *edx) {
    UINT32 a, b, c, d;
    __asm__ volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
        : "memory"
    );
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static inline UINT64 read_cr4_u64(void) {
    UINT64 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4_u64(UINT64 v) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(v) : "memory");
}

static void enable_avx_best_effort(void) {
    /* SAFE: Do NOT write CR4 from UEFI — firmware manages CR4.OSXSAVE.
     * Writing CR4 in UEFI causes #GP on many real-hardware firmware implementations.
     * We only touch XCR0 if OSXSAVE is already set by firmware (CR4 bit 18).
     * If firmware hasn't set OSXSAVE, we simply skip AVX enablement — djiblas
     * will fall back to SSE2 via CPUID detection. */
    UINT32 eax, ebx, ecx, edx;
    cpuidex_u32(1, 0, &eax, &ebx, &ecx, &edx);
    int has_xsave   = (ecx & (1u << 26)) != 0;
    int has_osxsave = (ecx & (1u << 27)) != 0; /* OS has enabled XSAVE/XGETBV via CR4.OSXSAVE */
    int has_avx_hw  = (ecx & (1u << 28)) != 0;
    if (!has_xsave || !has_osxsave || !has_avx_hw) return;

    /* SAFE: xgetbv #UD if CR4.OSXSAVE=0 (common in UEFI); check CR4 explicitly too. */
    if ((read_cr4_u64() & (1ull << 18)) == 0) return;

    UINT32 xcr0_lo, xcr0_hi;
    __asm__ volatile(
        "xgetbv"
        : "=a"(xcr0_lo), "=d"(xcr0_hi)
        : "c"(0)
        : "memory"
    );
    UINT32 new_lo = xcr0_lo | 0x7u;  /* x87 + XMM + YMM */
    if (new_lo != xcr0_lo) {
        __asm__ volatile(
            "xsetbv"
            :: "a"(new_lo), "d"(xcr0_hi), "c"(0)
            : "memory"
        );
    }
}

static void apply_no_repeat_ngram(float* logits, int vocab_size, const int* tokens, int n_tokens, int ngram) {
    if (ngram < 2) return;
    if (n_tokens < ngram - 1) return;

    int prefix_len = ngram - 1;
    int prefix_start = n_tokens - prefix_len;
    int limit = n_tokens - ngram;
    for (int i = 0; i <= limit; i++) {
        int match = 1;
        for (int j = 0; j < prefix_len; j++) {
            if (tokens[i + j] != tokens[prefix_start + j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            int banned = tokens[i + prefix_len];
            if (banned >= 0 && banned < vocab_size) {
                // Large negative value to effectively zero it after softmax.
                logits[banned] = -1.0e9f;
            }
        }
    }
}

static inline float dot_f32_sse2(const float* a, const float* b, int n) {
#if defined(__x86_64__) || defined(_M_X64)
    __m128 sum = _mm_setzero_ps();
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        sum = _mm_add_ps(sum, _mm_mul_ps(va, vb));
    }
    float tmp[4]; // SAFE: fixed-size SSE2 lane store
    _mm_storeu_ps(tmp, sum);
    float total = tmp[0] + tmp[1] + tmp[2] + tmp[3];
    for (; i < n; i++) total += a[i] * b[i];
    return total;
#else
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += a[i] * b[i];
    return total;
#endif
}

static inline void axpy_f32_sse2(float* dst, const float* src, float a, int n) {
#if defined(__x86_64__) || defined(_M_X64)
    __m128 va = _mm_set1_ps(a);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 vd = _mm_loadu_ps(dst + i);
        __m128 vs = _mm_loadu_ps(src + i);
        vd = _mm_add_ps(vd, _mm_mul_ps(va, vs));
        _mm_storeu_ps(dst + i, vd);
    }
    for (; i < n; i++) dst[i] += a * src[i];
#else
    for (int i = 0; i < n; i++) dst[i] += a * src[i];
#endif
}

static inline float dot_f32_best(const float* a, const float* b, int n) {
    int use_avx2 = g_attn_use_avx2;
    if (g_attn_force == 0) use_avx2 = 0;
    else if (g_attn_force == 1) use_avx2 = 1;
    if (use_avx2) return llmk_dot_f32_avx2(a, b, n);
    return dot_f32_sse2(a, b, n);
}

static inline void axpy_f32_best(float* dst, const float* src, float a, int n) {
    int use_avx2 = g_attn_use_avx2;
    if (g_attn_force == 0) use_avx2 = 0;
    else if (g_attn_force == 1) use_avx2 = 1;
    if (use_avx2) { llmk_axpy_f32_avx2(dst, src, a, n); return; }
    axpy_f32_sse2(dst, src, a, n);
}

// ============================================================================
// HEAP ALLOCATOR
// ============================================================================

static char* heap_base = NULL;
static unsigned long heap_offset = 0;
static unsigned long heap_size = 0;

static LlmkZones g_zones;
static LlmkLog g_llmk_log;
static LlmkSentinel g_sentinel;
static int g_llmk_ready = 0;

// DjibMark global state
DjibMarkState g_djibmark_state = {0};

/* Global sampling parameters — declared extern in repl_ctx.c and other worktree units */
float g_temperature    = 0.85f;
float g_top_p          = 0.95f;
float g_min_p          = 0.05f;
float g_repeat_penalty = 1.15f;
int   g_repeat_last_n  = 64;
int   g_max_new_tokens = 160;
int   g_djibion_active = 0;

// Root volume handle (set after OpenVolume). Used for best-effort dumps to files.
EFI_FILE_HANDLE g_root = NULL;

// ── OOSI v2 globals ──────────────────────────────────────────────────────────
// Populated by /ssm_load <file.bin>. All pointers into WEIGHTS arena (cold zone).
static OosiWeights  g_oosi_weights;
static int          g_oosi_weights_valid = 0;

// Warm-zone scratch buffers for inference (allocated once from SCRATCH arena)
// Mamba-2.8B sizes: d_model=2560, d_inner=5120, vocab=50282
#define G_OOSI_D_MODEL  2560
#define G_OOSI_D_INNER  5120
#define G_OOSI_VOCAB    51200

static ssm_f32 *g_oosi_x_buf    = NULL;  // [2560]
static ssm_f32 *g_oosi_x_out    = NULL;  // [2560]
static ssm_f32 *g_oosi_scratch  = NULL;  // [8 * 5120]
static ssm_f32 *g_oosi_logits   = NULL;  // [51200]
static ssm_f32 *g_oosi_halt_buf = NULL;  // [2561]
static ssm_f32 *g_oosi_halt_h1  = NULL;  // [512]
static ssm_f32 *g_oosi_halt_h2  = NULL;  // [64]
// ─────────────────────────────────────────────────────────────────────────────

// ── OOSI v3 globals ──────────────────────────────────────────────────────────
// Full-standalone Mamba: all weights int8, no MAMB float32 needed.
// Activated when /ssm_load detects magic 0x4F4F5333 ("OOS3").
static OosiV3Weights  g_oosi_v3_weights;
static OosiV3GenCtx   g_oosi_v3_ctx;
static int            g_oosi_v3_valid = 0;

// v3 scratch buffers — allocated from zones arenas on first /ssm_load
static ssm_f32 *g_v3_scratch   = NULL;   // [D + 4*Di + Dt + 2*S] ≈ 91 KB
static ssm_f32 *g_v3_logits    = NULL;   // [vocab_size] ≈ 197 KB
static ssm_f32 *g_v3_h_state   = NULL;   // [n_layer * d_inner * d_state] ≈ 20 MB
static ssm_f32 *g_v3_conv_buf  = NULL;   // [n_layer * d_inner * d_conv]  ≈ 5 MB
static int     *g_v3_conv_pos  = NULL;   // [n_layer]                     ≈ 256 B
static ssm_f32 *g_v3_halt_h1  = NULL;   // [512]
static ssm_f32 *g_v3_halt_h2  = NULL;   // [64]
static ssm_f32 *g_v3_halt_buf  = NULL;   // [halt_d_input + 1]
// ─────────────────────────────────────────────────────────────────────────────

// ── SomaMind globals ─────────────────────────────────────────────────────────
static SomaRouterCtx  g_soma_router;
static SomaDNA        g_soma_dna;
static int            g_soma_initialized = 0;
// Dual Core: work buffer allocated from scratch arena on first /ssm_load
static SomaDualCtx    g_soma_dual;
static ssm_f32       *g_soma_dual_buf = NULL;  // [vocab_size] — lazy alloc
static int            g_soma_dual_enabled = 0; // /soma_dual [0|1] to toggle
// Synaptic Memory Bus: ring buffer of past interaction records
static SomaSmbCtx     g_soma_smb;
// Dreaming Engine: offline consolidation of SMB memories
static SomaDreamCtx   g_soma_dream;
// Meta-Evolution: fitness scoring + auto-mutation
static SomaMetaCtx    g_soma_meta;
// Phase V: Multi-Reality Sampling — 3-way per-token candidate selection
static int g_multireal_enabled = 0;
static struct {
    int solar_wins;
    int lunar_wins;
    int argmax_wins;
    int total_tokens;
} g_multireal_stats;
// Swarm Intelligence: N agents voting on each token
static SomaSwarmCtx    g_soma_swarm;
static SomaSwarmResult g_soma_swarm_last; // Last vote result (for fitness update)
static SomaReflexCtx   g_soma_reflex;
static SomaLogicCtx    g_soma_logic;
static SomaMemCtx      g_soma_memory;
// Phase I: persistent journal (autosave counter)
static unsigned int    g_soma_journal_total_turns = 0;  // cumulative across sessions
static int             g_soma_journal_turns_since_save = 0;
// Phase J: oo-model cortex (small OOSS routing brain)
static SomaCortexCtx   g_soma_cortex;
// Phase M: warden pressure bridge (sentinel → router feedback)
static SomaWardenCtx   g_soma_warden;
// Phase N: session fitness tracker (scoring + DNA evolution)
static SomaSessionCtx  g_soma_session;
// Phase W: speculative decoding
static SomaSpecCtx     g_soma_spec;
static ssm_f32        *g_soma_spec_buf = 0;
// Phase Y: distributed swarm net (multi-instance peer consensus)
static SomaSwarmNetCtx g_soma_swarm_net;
// Phase O: OO swarm node identity + sync protocol (multi-instance coordination)
static OoSwarmNode     g_swarm_node;
static OoSwarmSync     g_swarm_sync;
// Phase Z: Persistent Key-Value store (NeuralFS v2)
static Nfs2Store       g_nfs2_store;
// ─────────────────────────────────────────────────────────────────────────────

static const CHAR16 *llmk_soma_route_name_wide(SomaRoute route) {
    switch (route) {
        case SOMA_ROUTE_REFLEX: return L"REFLEX";
        case SOMA_ROUTE_INTERNAL: return L"INTERNAL";
        case SOMA_ROUTE_EXTERNAL: return L"EXTERNAL";
        case SOMA_ROUTE_DUAL: return L"DUAL";
        default: return L"UNKNOWN";
    }
}

static int llmk_attach_route_policy_parse_route(const char *s, SomaRoute *out_route) {
    if (!s || !out_route) return 0;
    if (llmk_ascii_streq(s, "external")) {
        *out_route = SOMA_ROUTE_EXTERNAL;
        return 1;
    }
    if (llmk_ascii_streq(s, "dual")) {
        *out_route = SOMA_ROUTE_DUAL;
        return 1;
    }
    return 0;
}

static LlmkAttachRoutePolicyConfig *llmk_attach_route_policy_config_slot(SomaRoute route) {
    if (route == SOMA_ROUTE_EXTERNAL) return &g_attach_policy_external_cfg;
    if (route == SOMA_ROUTE_DUAL) return &g_attach_policy_dual_cfg;
    return NULL;
}

static void llmk_attach_route_policy_get_default(SomaRoute route, LlmkAttachRoutePolicyConfig *out) {
    LlmkAttachRoutePolicyConfig cfg;
    SetMem(&cfg, sizeof(cfg), 0);
    if (route == SOMA_ROUTE_EXTERNAL) {
        cfg.temperature_milli = 780;
        cfg.top_p_milli = 890;
        cfg.repetition_penalty_milli = 1120;
        cfg.max_tokens = 144;
    } else if (route == SOMA_ROUTE_DUAL) {
        cfg.temperature_milli = 840;
        cfg.top_p_milli = 920;
        cfg.repetition_penalty_milli = 1080;
        cfg.max_tokens = 176;
    }
    if (out) *out = cfg;
}

static void llmk_attach_route_policy_reset(SomaRoute route) {
    LlmkAttachRoutePolicyConfig cfg;
    LlmkAttachRoutePolicyConfig *slot = llmk_attach_route_policy_config_slot(route);
    if (!slot) return;
    llmk_attach_route_policy_get_default(route, &cfg);
    *slot = cfg;
}

static int llmk_attach_route_policy_cfg_equals(
    const LlmkAttachRoutePolicyConfig *a,
    const LlmkAttachRoutePolicyConfig *b
) {
    if (!a || !b) return 0;
    return a->temperature_milli == b->temperature_milli &&
           a->top_p_milli == b->top_p_milli &&
           a->repetition_penalty_milli == b->repetition_penalty_milli &&
           a->max_tokens == b->max_tokens;
}

static EFI_STATUS llmk_attach_route_policy_query_cfg_best_effort(
    LlmkAttachRoutePolicyConfig *out_external,
    LlmkAttachRoutePolicyConfig *out_dual,
    int *out_found_external_fields,
    int *out_found_dual_fields
) {
    if (out_found_external_fields) *out_found_external_fields = 0;
    if (out_found_dual_fields) *out_found_dual_fields = 0;
    if (!g_root) return EFI_NOT_READY;

    if (out_external) llmk_attach_route_policy_get_default(SOMA_ROUTE_EXTERNAL, out_external);
    if (out_dual) llmk_attach_route_policy_get_default(SOMA_ROUTE_DUAL, out_dual);

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"repl.cfg", &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        return EFI_NOT_FOUND;
    }

    char *buf = NULL;
    EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
    if (EFI_ERROR(st2) || !buf) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        return EFI_OUT_OF_RESOURCES;
    }
    CopyMem(buf, raw, raw_len);
    buf[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0 || line[0] == '#' || line[0] == ';') continue;
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "attach_policy_external_temp")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.10f) v = 0.10f;
                if (v > 5.0f) v = 5.0f;
                if (out_external) out_external->temperature_milli = (int)(v * 1000.0f + 0.5f);
                if (out_found_external_fields) (*out_found_external_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_top_p")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                if (out_external) out_external->top_p_milli = (int)(v * 1000.0f + 0.5f);
                if (out_found_external_fields) (*out_found_external_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_rep")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 1.0f) v = 1.0f;
                if (v > 3.0f) v = 3.0f;
                if (out_external) out_external->repetition_penalty_milli = (int)(v * 1000.0f + 0.5f);
                if (out_found_external_fields) (*out_found_external_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_max_tokens")) {
            int v = 0;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                if (out_external) out_external->max_tokens = v;
                if (out_found_external_fields) (*out_found_external_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_temp")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.10f) v = 0.10f;
                if (v > 5.0f) v = 5.0f;
                if (out_dual) out_dual->temperature_milli = (int)(v * 1000.0f + 0.5f);
                if (out_found_dual_fields) (*out_found_dual_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_top_p")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                if (out_dual) out_dual->top_p_milli = (int)(v * 1000.0f + 0.5f);
                if (out_found_dual_fields) (*out_found_dual_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_rep")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 1.0f) v = 1.0f;
                if (v > 3.0f) v = 3.0f;
                if (out_dual) out_dual->repetition_penalty_milli = (int)(v * 1000.0f + 0.5f);
                if (out_found_dual_fields) (*out_found_dual_fields)++;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_max_tokens")) {
            int v = 0;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                if (out_dual) out_dual->max_tokens = v;
                if (out_found_dual_fields) (*out_found_dual_fields)++;
            }
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if ((out_found_external_fields && *out_found_external_fields > 0) ||
        (out_found_dual_fields && *out_found_dual_fields > 0)) {
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}

static EFI_STATUS llmk_attach_route_policy_query_sync_state_best_effort(
    LlmkAttachRoutePolicyConfig *out_external,
    LlmkAttachRoutePolicyConfig *out_dual,
    int *out_found_external_fields,
    int *out_found_dual_fields,
    int *out_found_any,
    int *out_in_sync
) {
    LlmkAttachRoutePolicyConfig persisted_external;
    LlmkAttachRoutePolicyConfig persisted_dual;
    int found_external_fields = 0;
    int found_dual_fields = 0;
    EFI_STATUS st;

    llmk_attach_route_policy_get_default(SOMA_ROUTE_EXTERNAL, &persisted_external);
    llmk_attach_route_policy_get_default(SOMA_ROUTE_DUAL, &persisted_dual);
    st = llmk_attach_route_policy_query_cfg_best_effort(
        &persisted_external,
        &persisted_dual,
        &found_external_fields,
        &found_dual_fields);

    if (out_external) *out_external = persisted_external;
    if (out_dual) *out_dual = persisted_dual;
    if (out_found_external_fields) *out_found_external_fields = found_external_fields;
    if (out_found_dual_fields) *out_found_dual_fields = found_dual_fields;

    if (EFI_ERROR(st) || (found_external_fields == 0 && found_dual_fields == 0)) {
        if (out_found_any) *out_found_any = 0;
        if (out_in_sync) *out_in_sync = 0;
        return st;
    }

    if (out_found_any) *out_found_any = 1;
    if (out_in_sync) {
        *out_in_sync = llmk_attach_route_policy_cfg_equals(&persisted_external, &g_attach_policy_external_cfg) &&
                       llmk_attach_route_policy_cfg_equals(&persisted_dual, &g_attach_policy_dual_cfg);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_attach_route_policy_load_best_effort(int *out_changed_external, int *out_changed_dual) {
    LlmkAttachRoutePolicyConfig persisted_external;
    LlmkAttachRoutePolicyConfig persisted_dual;
    LlmkAttachRoutePolicyConfig prev_external = g_attach_policy_external_cfg;
    LlmkAttachRoutePolicyConfig prev_dual = g_attach_policy_dual_cfg;
    int found_external_fields = 0;
    int found_dual_fields = 0;
    EFI_STATUS st = llmk_attach_route_policy_query_cfg_best_effort(
        &persisted_external,
        &persisted_dual,
        &found_external_fields,
        &found_dual_fields);
    if (EFI_ERROR(st)) return st;

    g_attach_policy_external_cfg = persisted_external;
    g_attach_policy_dual_cfg = persisted_dual;

    if (out_changed_external) {
        *out_changed_external = !llmk_attach_route_policy_cfg_equals(&prev_external, &g_attach_policy_external_cfg);
    }
    if (out_changed_dual) {
        *out_changed_dual = !llmk_attach_route_policy_cfg_equals(&prev_dual, &g_attach_policy_dual_cfg);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_attach_route_policy_apply_saved_if_needed_best_effort(int *out_was_needed, int *out_changed_external, int *out_changed_dual) {
    LlmkAttachRoutePolicyConfig persisted_external;
    LlmkAttachRoutePolicyConfig persisted_dual;
    int found_external_fields = 0;
    int found_dual_fields = 0;
    int found_any = 0;
    int in_sync = 0;
    EFI_STATUS st = llmk_attach_route_policy_query_sync_state_best_effort(
        &persisted_external,
        &persisted_dual,
        &found_external_fields,
        &found_dual_fields,
        &found_any,
        &in_sync);
    if (EFI_ERROR(st)) return st;
    if (!found_any) return EFI_NOT_FOUND;

    if (in_sync) {
        if (out_was_needed) *out_was_needed = 0;
        if (out_changed_external) *out_changed_external = 0;
        if (out_changed_dual) *out_changed_dual = 0;
        return EFI_SUCCESS;
    }

    if (out_was_needed) *out_was_needed = 1;
    return llmk_attach_route_policy_load_best_effort(out_changed_external, out_changed_dual);
}

static void llmk_attach_route_policy_print_signed_milli_delta(int milli) {
    int abs_milli = milli < 0 ? -milli : milli;
    Print(L"%c%d.%03d", milli < 0 ? L'-' : L'+', abs_milli / 1000, abs_milli % 1000);
}

static void llmk_attach_route_policy_print_signed_i32_delta(int v) {
    int abs_v = v < 0 ? -v : v;
    Print(L"%c%d", v < 0 ? L'-' : L'+', abs_v);
}

static void llmk_attach_route_policy_print_runtime_cfg_line(const CHAR16 *label, const LlmkAttachRoutePolicyConfig *cfg) {
    if (!cfg) return;
    Print(L"  runtime.%s=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
          label,
          cfg->temperature_milli / 1000,
          cfg->temperature_milli % 1000,
          cfg->top_p_milli / 1000,
          cfg->top_p_milli % 1000,
          cfg->repetition_penalty_milli / 1000,
          cfg->repetition_penalty_milli % 1000,
          cfg->max_tokens);
}

static void llmk_attach_route_policy_print_persisted_cfg_line(const CHAR16 *label, const LlmkAttachRoutePolicyConfig *cfg, int found_fields) {
    if (!cfg) return;
    Print(L"  persisted.%s=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d fields=%d/4\r\n",
          label,
          cfg->temperature_milli / 1000,
          cfg->temperature_milli % 1000,
          cfg->top_p_milli / 1000,
          cfg->top_p_milli % 1000,
          cfg->repetition_penalty_milli / 1000,
          cfg->repetition_penalty_milli % 1000,
          cfg->max_tokens,
          found_fields);
}

static void llmk_attach_route_policy_print_diff(void) {
    LlmkAttachRoutePolicyConfig persisted_external;
    LlmkAttachRoutePolicyConfig persisted_dual;
    int found_external_fields = 0;
    int found_dual_fields = 0;
    int found_any = 0;
    int in_sync = 0;
    EFI_STATUS st = llmk_attach_route_policy_query_sync_state_best_effort(
        &persisted_external,
        &persisted_dual,
        &found_external_fields,
        &found_dual_fields,
        &found_any,
        &in_sync);

    Print(L"\r\n[AttachPolicyDiff]\r\n");
    llmk_attach_route_policy_print_runtime_cfg_line(L"external", &g_attach_policy_external_cfg);
    llmk_attach_route_policy_print_runtime_cfg_line(L"dual", &g_attach_policy_dual_cfg);

    if (EFI_ERROR(st) || !found_any) {
        Print(L"  persisted=(not found in repl.cfg)\r\n\r\n");
        return;
    }

    llmk_attach_route_policy_print_persisted_cfg_line(L"external", &persisted_external, found_external_fields);
    llmk_attach_route_policy_print_persisted_cfg_line(L"dual", &persisted_dual, found_dual_fields);
    Print(L"  delta.external.temp=");
    llmk_attach_route_policy_print_signed_milli_delta(g_attach_policy_external_cfg.temperature_milli - persisted_external.temperature_milli);
    Print(L" top_p=");
    llmk_attach_route_policy_print_signed_milli_delta(g_attach_policy_external_cfg.top_p_milli - persisted_external.top_p_milli);
    Print(L" rep=");
    llmk_attach_route_policy_print_signed_milli_delta(g_attach_policy_external_cfg.repetition_penalty_milli - persisted_external.repetition_penalty_milli);
    Print(L" max_tokens=");
    llmk_attach_route_policy_print_signed_i32_delta(g_attach_policy_external_cfg.max_tokens - persisted_external.max_tokens);
    Print(L"\r\n");
    Print(L"  delta.dual.temp=");
    llmk_attach_route_policy_print_signed_milli_delta(g_attach_policy_dual_cfg.temperature_milli - persisted_dual.temperature_milli);
    Print(L" top_p=");
    llmk_attach_route_policy_print_signed_milli_delta(g_attach_policy_dual_cfg.top_p_milli - persisted_dual.top_p_milli);
    Print(L" rep=");
    llmk_attach_route_policy_print_signed_milli_delta(g_attach_policy_dual_cfg.repetition_penalty_milli - persisted_dual.repetition_penalty_milli);
    Print(L" max_tokens=");
    llmk_attach_route_policy_print_signed_i32_delta(g_attach_policy_dual_cfg.max_tokens - persisted_dual.max_tokens);
    Print(L"\r\n");
    Print(L"  sync=%s\r\n\r\n", in_sync ? L"in-sync" : L"runtime!=repl.cfg");
}

static void llmk_attach_route_policy_print_audit(void) {
    LlmkAttachRoutePolicyConfig persisted_external;
    LlmkAttachRoutePolicyConfig persisted_dual;
    int found_external_fields = 0;
    int found_dual_fields = 0;
    int found_any = 0;
    int in_sync = 0;
    EFI_STATUS st = llmk_attach_route_policy_query_sync_state_best_effort(
        &persisted_external,
        &persisted_dual,
        &found_external_fields,
        &found_dual_fields,
        &found_any,
        &in_sync);

    Print(L"\r\n[AttachPolicyAudit]\r\n");
    llmk_attach_route_policy_print_runtime_cfg_line(L"external", &g_attach_policy_external_cfg);
    llmk_attach_route_policy_print_runtime_cfg_line(L"dual", &g_attach_policy_dual_cfg);

    if (EFI_ERROR(st) || !found_any) {
        Print(L"  persisted=(not found in repl.cfg)\r\n");
        Print(L"  sync=unknown\r\n");
        Print(L"  suggested_action=/attach_policy reset\r\n");
    } else {
        llmk_attach_route_policy_print_persisted_cfg_line(L"external", &persisted_external, found_external_fields);
        llmk_attach_route_policy_print_persisted_cfg_line(L"dual", &persisted_dual, found_dual_fields);
        Print(L"  sync=%s\r\n", in_sync ? L"in-sync" : L"runtime!=repl.cfg");
        Print(L"  suggested_action=%s\r\n", in_sync ? L"already-synchronized" : L"/attach_policy_sync");
    }

    Print(L"  last_apply=");
    if (!g_attach_policy_apply_seen) {
        Print(L"never\r\n\r\n");
        return;
    }
    llmk_attach_route_policy_print_apply_mode(g_attach_policy_apply_mode);
    Print(L" effect=");
    llmk_attach_route_policy_print_apply_effect();
    Print(L" changed.external=%d changed.dual=%d\r\n\r\n",
          g_attach_policy_apply_changed_external,
          g_attach_policy_apply_changed_dual);
}

static void llmk_attach_route_policy_print_profile(const CHAR16 *label, SomaRoute route) {
    LlmkAttachRoutePolicyConfig *slot = llmk_attach_route_policy_config_slot(route);
    LlmkAttachRoutePolicyPreview preview;
    if (!slot) return;
    llmk_attach_route_policy_preview(route, &preview);
    Print(L"    %s cfg=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
          label,
          slot->temperature_milli / 1000,
          slot->temperature_milli % 1000,
          slot->top_p_milli / 1000,
          slot->top_p_milli % 1000,
          slot->repetition_penalty_milli / 1000,
          slot->repetition_penalty_milli % 1000,
          slot->max_tokens);
    Print(L"      preview active=%d applied=%d temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
          preview.active,
          preview.applied,
          preview.temperature_milli / 1000,
          preview.temperature_milli % 1000,
          preview.top_p_milli / 1000,
          preview.top_p_milli % 1000,
          preview.repetition_penalty_milli / 1000,
          preview.repetition_penalty_milli % 1000,
          preview.max_tokens);
}

static void llmk_attach_route_policy_print_status(void) {
    LlmkAttachRoutePolicyConfig persisted_external;
    LlmkAttachRoutePolicyConfig persisted_dual;
    int found_external_fields = 0;
    int found_dual_fields = 0;
    int any_found = 0;
    int in_sync = 0;
    EFI_STATUS persisted_st;

    llmk_attach_route_policy_get_default(SOMA_ROUTE_EXTERNAL, &persisted_external);
    llmk_attach_route_policy_get_default(SOMA_ROUTE_DUAL, &persisted_dual);
    persisted_st = llmk_attach_route_policy_query_cfg_best_effort(
        &persisted_external,
        &persisted_dual,
        &found_external_fields,
        &found_dual_fields);
    any_found = (found_external_fields > 0 || found_dual_fields > 0) ? 1 : 0;
    in_sync = any_found &&
              llmk_attach_route_policy_cfg_equals(&persisted_external, &g_attach_policy_external_cfg) &&
              llmk_attach_route_policy_cfg_equals(&persisted_dual, &g_attach_policy_dual_cfg);

    Print(L"\r\n[AttachPolicy]\r\n");
    Print(L"  scope=runtime-route-policy\r\n");
    Print(L"  attach_active=%d format=", g_mind_runtime_state.attach_active);
    llmk_print_ascii(g_mind_runtime_state.attach_format[0] ? g_mind_runtime_state.attach_format : "unknown");
    Print(L"\r\n");
    llmk_attach_route_policy_print_profile(L"external", SOMA_ROUTE_EXTERNAL);
    llmk_attach_route_policy_print_profile(L"dual", SOMA_ROUTE_DUAL);
    Print(L"  persisted.status=");
    if (EFI_ERROR(persisted_st) || !any_found) {
        Print(L"not-found\r\n");
    } else if (found_external_fields == 4 && found_dual_fields == 4) {
        Print(L"available\r\n");
    } else {
        Print(L"partial\r\n");
    }
    Print(L"  persisted.external_fields=%d/4 dual_fields=%d/4\r\n",
          found_external_fields, found_dual_fields);
    if (!EFI_ERROR(persisted_st) && any_found) {
        Print(L"    external persisted=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
              persisted_external.temperature_milli / 1000,
              persisted_external.temperature_milli % 1000,
              persisted_external.top_p_milli / 1000,
              persisted_external.top_p_milli % 1000,
              persisted_external.repetition_penalty_milli / 1000,
              persisted_external.repetition_penalty_milli % 1000,
              persisted_external.max_tokens);
        Print(L"    dual     persisted=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
              persisted_dual.temperature_milli / 1000,
              persisted_dual.temperature_milli % 1000,
              persisted_dual.top_p_milli / 1000,
              persisted_dual.top_p_milli % 1000,
              persisted_dual.repetition_penalty_milli / 1000,
              persisted_dual.repetition_penalty_milli % 1000,
              persisted_dual.max_tokens);
        Print(L"  persisted.sync=%s\r\n", in_sync ? L"in-sync" : L"runtime!=repl.cfg");
    } else {
        Print(L"  persisted.sync=unknown\r\n");
    }
    Print(L"  last_apply=");
    if (!g_attach_policy_apply_seen) {
        Print(L"never\r\n");
    } else {
        llmk_attach_route_policy_print_apply_mode(g_attach_policy_apply_mode);
        Print(L" effect=");
        llmk_attach_route_policy_print_apply_effect();
        Print(L" changed.external=%d changed.dual=%d\r\n",
              g_attach_policy_apply_changed_external,
              g_attach_policy_apply_changed_dual);
    }
    Print(L"\r\n");
}

static void llmk_attach_route_policy_format_milli_ascii(char *out, int out_cap, int milli) {
    if (!out || out_cap <= 0) return;
    out[0] = 0;
    if (milli < 0) milli = 0;
    if (milli > 5000) milli = 5000;

    int op = 0;
    llmk_ascii_append_u64(out, out_cap, &op, (UINT64)(milli / 1000));
    llmk_ascii_append_char(out, out_cap, &op, '.');
    llmk_ascii_append_char(out, out_cap, &op, (char)('0' + ((milli / 100) % 10)));
    llmk_ascii_append_char(out, out_cap, &op, (char)('0' + ((milli / 10) % 10)));
    llmk_ascii_append_char(out, out_cap, &op, (char)('0' + (milli % 10)));
}

static EFI_STATUS llmk_attach_route_policy_persist_best_effort(void) {
    char buf[32];
    EFI_STATUS st;

    llmk_attach_route_policy_format_milli_ascii(buf, (int)sizeof(buf), g_attach_policy_external_cfg.temperature_milli);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_external_temp", buf);
    if (EFI_ERROR(st)) return st;
    llmk_attach_route_policy_format_milli_ascii(buf, (int)sizeof(buf), g_attach_policy_external_cfg.top_p_milli);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_external_top_p", buf);
    if (EFI_ERROR(st)) return st;
    llmk_attach_route_policy_format_milli_ascii(buf, (int)sizeof(buf), g_attach_policy_external_cfg.repetition_penalty_milli);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_external_rep", buf);
    if (EFI_ERROR(st)) return st;
    llmk_ascii_from_i32(buf, (int)sizeof(buf), g_attach_policy_external_cfg.max_tokens);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_external_max_tokens", buf);
    if (EFI_ERROR(st)) return st;

    llmk_attach_route_policy_format_milli_ascii(buf, (int)sizeof(buf), g_attach_policy_dual_cfg.temperature_milli);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_dual_temp", buf);
    if (EFI_ERROR(st)) return st;
    llmk_attach_route_policy_format_milli_ascii(buf, (int)sizeof(buf), g_attach_policy_dual_cfg.top_p_milli);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_dual_top_p", buf);
    if (EFI_ERROR(st)) return st;
    llmk_attach_route_policy_format_milli_ascii(buf, (int)sizeof(buf), g_attach_policy_dual_cfg.repetition_penalty_milli);
    st = llmk_repl_cfg_set_kv_best_effort("attach_policy_dual_rep", buf);
    if (EFI_ERROR(st)) return st;
    llmk_ascii_from_i32(buf, (int)sizeof(buf), g_attach_policy_dual_cfg.max_tokens);
    return llmk_repl_cfg_set_kv_best_effort("attach_policy_dual_max_tokens", buf);
}

static void llmk_attach_route_policy_preview(SomaRoute route, LlmkAttachRoutePolicyPreview *out) {
    LlmkAttachRoutePolicyPreview preview;
    SetMem(&preview, sizeof(preview), 0);

    if (!g_oosi_v3_valid || !g_mind_runtime_state.attach_active) {
        if (out) *out = preview;
        return;
    }
    if (!(route == SOMA_ROUTE_EXTERNAL || route == SOMA_ROUTE_DUAL)) {
        if (out) *out = preview;
        return;
    }

    preview.active = 1;
    preview.temperature_milli = (int)(g_oosi_v3_ctx.temperature * 1000.0f + 0.5f);
    preview.top_p_milli = (int)(g_oosi_v3_ctx.top_p * 1000.0f + 0.5f);
    preview.repetition_penalty_milli = (int)(g_oosi_v3_ctx.repetition_penalty * 1000.0f + 0.5f);
    preview.max_tokens = g_oosi_v3_ctx.max_tokens;

    {
        LlmkAttachRoutePolicyConfig *cfg = llmk_attach_route_policy_config_slot(route);
        int target_temp = cfg ? cfg->temperature_milli : 0;
        int target_top_p = cfg ? cfg->top_p_milli : 0;
        int target_rep = cfg ? cfg->repetition_penalty_milli : 0;
        int target_max_tokens = cfg ? cfg->max_tokens : 0;
        int orig_temp = preview.temperature_milli;
        int orig_top_p = preview.top_p_milli;
        int orig_rep = preview.repetition_penalty_milli;
        int orig_max_tokens = preview.max_tokens;

        if (llmk_ascii_streq(g_mind_runtime_state.attach_format, "bin")) {
            target_temp -= 40;
            target_top_p -= 20;
            target_rep += 40;
            target_max_tokens -= 16;
        } else if (llmk_ascii_streq(g_mind_runtime_state.attach_format, "gguf")) {
            target_temp += 20;
            target_top_p += 10;
        }

        if (preview.temperature_milli > target_temp) preview.temperature_milli = target_temp;
        if (preview.top_p_milli > target_top_p) preview.top_p_milli = target_top_p;
        if (preview.repetition_penalty_milli < target_rep) preview.repetition_penalty_milli = target_rep;
        if (preview.max_tokens > target_max_tokens) preview.max_tokens = target_max_tokens;

        preview.applied = (preview.temperature_milli != orig_temp) ||
                          (preview.top_p_milli != orig_top_p) ||
                          (preview.repetition_penalty_milli != orig_rep) ||
                          (preview.max_tokens != orig_max_tokens);
    }

    if (out) *out = preview;
}

static void llmk_attach_route_policy_begin(SomaRoute route, LlmkAttachRoutePolicyState *state) {
    if (!state) return;
    SetMem(state, sizeof(*state), 0);
    if (!g_oosi_v3_valid || !g_mind_runtime_state.attach_active) return;
    if (!(route == SOMA_ROUTE_EXTERNAL || route == SOMA_ROUTE_DUAL)) return;

    state->temperature = g_oosi_v3_ctx.temperature;
    state->top_p = g_oosi_v3_ctx.top_p;
    state->repetition_penalty = g_oosi_v3_ctx.repetition_penalty;
    state->max_tokens = g_oosi_v3_ctx.max_tokens;

    {
        LlmkAttachRoutePolicyPreview preview;
        llmk_attach_route_policy_preview(route, &preview);
        if (!preview.active) return;
        g_oosi_v3_ctx.temperature = (float)preview.temperature_milli / 1000.0f;
        g_oosi_v3_ctx.top_p = (float)preview.top_p_milli / 1000.0f;
        g_oosi_v3_ctx.repetition_penalty = (float)preview.repetition_penalty_milli / 1000.0f;
        g_oosi_v3_ctx.max_tokens = preview.max_tokens;
        state->applied = preview.applied;
    }
}

static void llmk_attach_route_policy_end(const LlmkAttachRoutePolicyState *state) {
    if (!state || !state->applied || !g_oosi_v3_valid) return;
    g_oosi_v3_ctx.temperature = state->temperature;
    g_oosi_v3_ctx.top_p = state->top_p;
    g_oosi_v3_ctx.repetition_penalty = state->repetition_penalty;
    g_oosi_v3_ctx.max_tokens = state->max_tokens;
}

// GOP framebuffer (best-effort; may be unavailable on headless firmware paths)
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;
static UINT32 g_gop_w = 0;
static UINT32 g_gop_h = 0;
static UINT32 g_gop_ppsl = 0;
static EFI_GRAPHICS_PIXEL_FORMAT g_gop_pf = PixelFormatMax;
static EFI_PIXEL_BITMASK g_gop_mask = {0};
static volatile UINT32 *g_gop_fb32 = NULL;

// Mirror the REPL's KV position into a global so optional systems (like GOP TUI)
// can read it without threading local state through many functions.
static int g_llmk_kv_pos = 0;

// Minimal GOP-based TUI overlay (optional).
static int g_tui_enabled = 0;
static int g_tui_dirty = 0;
static int g_tui_last_id = 0;
static int g_tui_last_tick = 0;
static int g_tui_last_energy = 0;
static char g_tui_last_event[64] = "";

// Live generation counters (best-effort): updated during decode loop so the TUI can
// show progress even while the terminal output is streaming.
static int g_tui_gen_active = 0;
static int g_tui_gen_tokens = 0;

// GOP UI modes
// 0=status panel only (legacy)
// 1=log view (full-width)
// 2=split (log + files)
// 3=files focus (log + files, selection emphasized)
static int g_ui_mode = 0;

// Transcript ring buffer (ASCII-ish)
#define LLMK_TR_LINES 192
#define LLMK_TR_COLS  128
static char g_tr_lines[LLMK_TR_LINES][LLMK_TR_COLS];
static UINT32 g_tr_write = 0; // next write slot
static UINT32 g_tr_count = 0; // number of valid lines
static char g_tr_cur[LLMK_TR_COLS];
static int g_tr_cur_len = 0;
static int g_tr_scroll = 0; // how many lines back from newest

// GOP file browser (command-driven)
#define LLMK_FB_MAX_ENTRIES 96
typedef struct {
    CHAR16 name16[64];
    char name8[64];
    int is_dir;
    UINT64 size;
} LlmkFbEntry;

static CHAR16 g_fb_path16[128] = L"\\";
static char g_fb_path8[128] = "\\";
static LlmkFbEntry g_fb_entries[LLMK_FB_MAX_ENTRIES];
static int g_fb_count = 0;
static int g_fb_sel = 0;

#define LLMK_FB_PREVIEW_LINES 12
#define LLMK_FB_PREVIEW_COLS  96
static char g_fb_preview[LLMK_FB_PREVIEW_LINES][LLMK_FB_PREVIEW_COLS];
static int g_fb_preview_count = 0;

// Capture mode: used by /draw to collect model output (DSL) instead of printing it.
static int g_capture_mode = 0;
static char g_capture_buf[2048];
static int g_capture_len = 0;
static int g_capture_truncated = 0;

// /oo_auto persistent state (runs multiple think cycles back-to-back)
static int g_oo_auto_active = 0;
static int g_oo_auto_id = 0;
static int g_oo_auto_remaining = 0;
static int g_oo_auto_total = 0;
static char g_oo_auto_user[256];

// /oo_exec persistent state (agenda runner)
int g_oo_exec_active = 0;
static int g_oo_exec_id = 0;
static int g_oo_exec_remaining = 0;
static int g_oo_exec_total = 0;
static int g_oo_exec_plan_if_empty = 0;
static char g_oo_exec_hint[256];

// M19.1: Benchmark capture (writes JSONL rows to a file on the boot volume)
static int g_bench_active = 0;
static int g_bench_pending = 0;
static char g_bench_case_id[64];
static char g_bench_category[48]; // SAFE: short category label; always written with explicit bounds
static int g_bench_case_max_new_tokens = 0;
static unsigned long long g_bench_wall0_us = 0;
static int g_bench_have_wall = 0;
static UINT64 g_bench_decode_cycles_start = 0;
static UINT32 g_bench_decode_tokens_start = 0;
static EFI_FILE_HANDLE g_bench_file = NULL;
static CHAR16 g_bench_out_name[64] = L"LLMK_BEN.JNL";

// M16.1: Runtime metrics (tokens/sec, latency, memory pressure, sentinel)
typedef struct {
    UINT64 session_start_cycles;
    UINT64 total_prefill_cycles;
    UINT64 total_decode_cycles;
    UINT32 total_prefill_tokens;
    UINT32 total_decode_tokens;
    UINT32 total_prefill_calls;
    UINT32 total_decode_calls;
    UINT64 last_prefill_cycles;
    UINT64 last_decode_cycles;
    UINT32 last_prefill_tokens;
    UINT32 last_decode_tokens;
    UINT32 sentinel_violations_total;
    UINT32 kv_cache_resets;
    UINT32 generation_count;
} LlmkRuntimeMetrics;

static LlmkRuntimeMetrics g_metrics = {0};

typedef struct {
    int enabled;
    UINT64 decode_cpt_hi;
    UINT64 decode_cpt_lo;
    int step_top_k;
    int step_max_gen_tokens;
    int step_temp_milli;
    int min_top_k;
    int min_max_gen_tokens;
    int min_top_p_milli;
    int min_temp_milli;
    int last_action; // 0=hold, 1=tighten, 2=relax
    UINT64 last_decode_cpt;
} LlmkAutoTuneConfig;

static LlmkAutoTuneConfig g_autotune = {
    0,
    170000ULL,
    105000ULL,
    8,
    16,
    40,
    24,
    64,
    800,
    550,
    0,
    0ULL
};

typedef struct {
    int enabled;
    int hard_stop_overruns_decode;
    int safe_mode_turns;
    int safe_top_k_cap;
    int safe_max_tokens_cap;
    int safe_top_p_milli;
    int safe_temp_milli;
    int reset_kv_on_trip;
    int active_turns_remaining;
    int trip_count;
    int last_trip_overruns_decode;
} LlmkGuardrailsConfig;

static LlmkGuardrailsConfig g_guardrails = {
    0,
    6,
    2,
    40,
    96,
    880,
    700,
    0,
    0,
    0,
    0
};

static void llmk_metrics_reset(void) {
    g_metrics.session_start_cycles = __rdtsc();
    g_metrics.total_prefill_cycles = 0;
    g_metrics.total_decode_cycles = 0;
    g_metrics.total_prefill_tokens = 0;
    g_metrics.total_decode_tokens = 0;
    g_metrics.total_prefill_calls = 0;
    g_metrics.total_decode_calls = 0;
    g_metrics.last_prefill_cycles = 0;
    g_metrics.last_decode_cycles = 0;
    g_metrics.last_prefill_tokens = 0;
    g_metrics.last_decode_tokens = 0;
    g_metrics.sentinel_violations_total = 0;
    g_metrics.kv_cache_resets = 0;
    g_metrics.generation_count = 0;
    g_autotune.last_action = 0;
    g_autotune.last_decode_cpt = 0;
    g_guardrails.active_turns_remaining = 0;
    g_guardrails.trip_count = 0;
    g_guardrails.last_trip_overruns_decode = 0;
}

static void llmk_guardrails_print_status(float temperature, float top_p, int top_k, int max_gen_tokens) {
    int temp_milli = (int)(temperature * 1000.0f + 0.5f);
    int top_p_milli = (int)(top_p * 1000.0f + 0.5f);

    Print(L"[m18.1] guardrails=%d hard_stop_overruns_decode=%d safe_mode_turns=%d\r\n",
          g_guardrails.enabled,
          g_guardrails.hard_stop_overruns_decode,
          g_guardrails.safe_mode_turns);
    Print(L"[m18.1] safe caps: top_k<=%d max_tokens<=%d top_p<=%d.%03d temp<=%d.%03d reset_kv=%d\r\n",
          g_guardrails.safe_top_k_cap,
          g_guardrails.safe_max_tokens_cap,
          g_guardrails.safe_top_p_milli / 1000,
          g_guardrails.safe_top_p_milli % 1000,
          g_guardrails.safe_temp_milli / 1000,
          g_guardrails.safe_temp_milli % 1000,
          g_guardrails.reset_kv_on_trip);
    Print(L"[m18.1] runtime: active_safe_turns=%d trip_count=%d last_trip_overruns=%d\r\n",
          g_guardrails.active_turns_remaining,
          g_guardrails.trip_count,
          g_guardrails.last_trip_overruns_decode);
    Print(L"[m18.1] current: top_k=%d max_tokens=%d top_p=%d.%03d temp=%d.%03d\r\n",
          top_k,
          max_gen_tokens,
          top_p_milli / 1000,
          top_p_milli % 1000,
          temp_milli / 1000,
          temp_milli % 1000);
}

static void llmk_guardrails_apply_safe_caps(float *temperature, float *top_p, int *top_k, int *max_gen_tokens, int emit_log) {
    if (!g_guardrails.enabled) return;
    if (g_guardrails.active_turns_remaining <= 0) return;
    if (!temperature || !top_p || !top_k || !max_gen_tokens) return;

    int changed = 0;
    int temp_milli = (int)((*temperature) * 1000.0f + 0.5f);
    int top_p_milli = (int)((*top_p) * 1000.0f + 0.5f);

    if (*top_k > g_guardrails.safe_top_k_cap) {
        *top_k = g_guardrails.safe_top_k_cap;
        changed = 1;
    }
    if (*max_gen_tokens > g_guardrails.safe_max_tokens_cap) {
        *max_gen_tokens = g_guardrails.safe_max_tokens_cap;
        changed = 1;
    }
    if (top_p_milli > g_guardrails.safe_top_p_milli) {
        top_p_milli = g_guardrails.safe_top_p_milli;
        changed = 1;
    }
    if (temp_milli > g_guardrails.safe_temp_milli) {
        temp_milli = g_guardrails.safe_temp_milli;
        changed = 1;
    }

    *temperature = (float)temp_milli / 1000.0f;
    *top_p = (float)top_p_milli / 1000.0f;

    if (emit_log && changed) {
        Print(L"[m18.1] safe-mode caps applied (turns_left=%d): top_k=%d max_tokens=%d top_p=%d.%03d temp=%d.%03d\r\n",
              g_guardrails.active_turns_remaining,
              *top_k,
              *max_gen_tokens,
              top_p_milli / 1000,
              top_p_milli % 1000,
              temp_milli / 1000,
              temp_milli % 1000);
    }
}

static void llmk_guardrails_finish_turn(int had_guard_trip) {
    if (!g_guardrails.enabled) return;

    if (had_guard_trip) {
        g_guardrails.trip_count++;
        g_guardrails.active_turns_remaining = g_guardrails.safe_mode_turns;
    } else if (g_guardrails.active_turns_remaining > 0) {
        g_guardrails.active_turns_remaining--;
    }
}

static void llmk_autotune_print_status(float temperature, float top_p, int top_k, int max_gen_tokens) {
    int temp_milli = (int)(temperature * 1000.0f + 0.5f);
    int top_p_milli = (int)(top_p * 1000.0f + 0.5f);
    const CHAR16 *last = L"hold";
    if (g_autotune.last_action == 1) last = L"tighten";
    else if (g_autotune.last_action == 2) last = L"relax";

    Print(L"[m18] autotune=%d decode_cpt_hi=%lu decode_cpt_lo=%lu\r\n",
          g_autotune.enabled,
          g_autotune.decode_cpt_hi,
          g_autotune.decode_cpt_lo);
    Print(L"[m18] steps: top_k=%d max_tokens=%d temp_milli=%d\r\n",
          g_autotune.step_top_k,
          g_autotune.step_max_gen_tokens,
          g_autotune.step_temp_milli);
    Print(L"[m18] mins: top_k=%d max_tokens=%d top_p=%d.%03d temp=%d.%03d\r\n",
          g_autotune.min_top_k,
          g_autotune.min_max_gen_tokens,
          g_autotune.min_top_p_milli / 1000,
          g_autotune.min_top_p_milli % 1000,
          g_autotune.min_temp_milli / 1000,
          g_autotune.min_temp_milli % 1000);
    Print(L"[m18] current: top_k=%d max_tokens=%d top_p=%d.%03d temp=%d.%03d\r\n",
          top_k,
          max_gen_tokens,
          top_p_milli / 1000,
          top_p_milli % 1000,
          temp_milli / 1000,
          temp_milli % 1000);
    Print(L"[m18] last: action=%s decode_cpt=%lu\r\n",
          (CHAR16 *)last,
          g_autotune.last_decode_cpt);
}

static void llmk_autotune_apply_after_turn(
    UINT64 decode_cycles_delta,
    UINT32 decode_tokens_delta,
    float *temperature,
    float *top_p,
    int *top_k,
    int *max_gen_tokens,
    int base_temp_milli,
    int base_top_p_milli,
    int base_top_k,
    int base_max_gen_tokens,
    int emit_log
) {
    if (!g_autotune.enabled) return;
    if (!temperature || !top_p || !top_k || !max_gen_tokens) return;
    if (decode_tokens_delta == 0) return;

    UINT64 decode_cpt = decode_cycles_delta / (UINT64)decode_tokens_delta;
    g_autotune.last_decode_cpt = decode_cpt;

    int action = 0;
    if (decode_cpt > g_autotune.decode_cpt_hi) {
        action = 1;
    } else if (decode_cpt < g_autotune.decode_cpt_lo) {
        action = 2;
    }

    int temp_milli = (int)((*temperature) * 1000.0f + 0.5f);
    int top_p_milli = (int)((*top_p) * 1000.0f + 0.5f);
    int changed = 0;

    if (action == 1) {
        int new_top_k = *top_k - g_autotune.step_top_k;
        if (new_top_k < g_autotune.min_top_k) new_top_k = g_autotune.min_top_k;
        if (new_top_k != *top_k) { *top_k = new_top_k; changed = 1; }

        int new_max_tokens = *max_gen_tokens - g_autotune.step_max_gen_tokens;
        if (new_max_tokens < g_autotune.min_max_gen_tokens) new_max_tokens = g_autotune.min_max_gen_tokens;
        if (new_max_tokens != *max_gen_tokens) { *max_gen_tokens = new_max_tokens; changed = 1; }

        int new_temp = temp_milli - g_autotune.step_temp_milli;
        if (new_temp < g_autotune.min_temp_milli) new_temp = g_autotune.min_temp_milli;
        if (new_temp != temp_milli) { temp_milli = new_temp; changed = 1; }

        int new_top_p = top_p_milli - 15;
        if (new_top_p < g_autotune.min_top_p_milli) new_top_p = g_autotune.min_top_p_milli;
        if (new_top_p != top_p_milli) { top_p_milli = new_top_p; changed = 1; }
    } else if (action == 2) {
        int new_top_k = *top_k + g_autotune.step_top_k;
        if (new_top_k > base_top_k) new_top_k = base_top_k;
        if (new_top_k != *top_k) { *top_k = new_top_k; changed = 1; }

        int new_max_tokens = *max_gen_tokens + g_autotune.step_max_gen_tokens;
        if (new_max_tokens > base_max_gen_tokens) new_max_tokens = base_max_gen_tokens;
        if (new_max_tokens != *max_gen_tokens) { *max_gen_tokens = new_max_tokens; changed = 1; }

        int new_temp = temp_milli + g_autotune.step_temp_milli;
        if (new_temp > base_temp_milli) new_temp = base_temp_milli;
        if (new_temp != temp_milli) { temp_milli = new_temp; changed = 1; }

        int new_top_p = top_p_milli + 15;
        if (new_top_p > base_top_p_milli) new_top_p = base_top_p_milli;
        if (new_top_p != top_p_milli) { top_p_milli = new_top_p; changed = 1; }
    }

    if (temp_milli < 0) temp_milli = 0;
    if (top_p_milli < 0) top_p_milli = 0;
    if (top_p_milli > 1000) top_p_milli = 1000;

    *temperature = (float)temp_milli / 1000.0f;
    *top_p = (float)top_p_milli / 1000.0f;

    g_autotune.last_action = changed ? action : 0;

    if (emit_log && changed) {
        const CHAR16 *name = (action == 1) ? L"tighten" : L"relax";
        Print(L"\r\n[m18] autotune=%s decode_cpt=%lu -> temp=%d.%03d top_p=%d.%03d top_k=%d max_tokens=%d\r\n",
              (CHAR16 *)name,
              decode_cpt,
              temp_milli / 1000,
              temp_milli % 1000,
              top_p_milli / 1000,
              top_p_milli % 1000,
              *top_k,
              *max_gen_tokens);
    }
}

static void llmk_capture_reset(void) {
    g_capture_len = 0;
    g_capture_truncated = 0;
    g_capture_buf[0] = 0;
}

static void llmk_capture_append_ascii(const char *piece, int len) {
    if (!piece || len <= 0) return;
    if (g_capture_len >= (int)sizeof(g_capture_buf) - 1) {
        g_capture_truncated = 1;
        return;
    }
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)piece[i];
        // Keep a conservative ASCII subset. Map CR->LF and drop others.
        if (c == '\r') c = '\n';
        if (c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7E)) {
            if (c == '`') c = ' '; // avoid markdown fences
            g_capture_buf[g_capture_len++] = (char)c;
            if (g_capture_len >= (int)sizeof(g_capture_buf) - 1) {
                g_capture_truncated = 1;
                break;
            }
        }
    }
    g_capture_buf[g_capture_len] = 0;
}

static void llmk_capture_sanitize_inplace(void) {
    // Trim leading whitespace
    int start = 0;
    while (start < g_capture_len && (g_capture_buf[start] == ' ' || g_capture_buf[start] == '\n' || g_capture_buf[start] == '\t')) start++;
    if (start > 0) {
        for (int i = 0; i + start <= g_capture_len; i++) g_capture_buf[i] = g_capture_buf[i + start];
        g_capture_len -= start;
    }

    // Truncate at an END marker if present
    for (int i = 0; i + 2 < g_capture_len; i++) {
        if (g_capture_buf[i] == 'E' && g_capture_buf[i + 1] == 'N' && g_capture_buf[i + 2] == 'D') {
            g_capture_buf[i] = 0;
            g_capture_len = i;
            break;
        }
    }

    // Replace any non-useful characters
    for (int i = 0; i < g_capture_len; i++) {
        char c = g_capture_buf[i];
        if (!(c == '\n' || c == '\t' || c == ';' || c == '-' || c == '_' || c == ',' || c == '.' || c == ':' || c == '(' || c == ')' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ')) {
            g_capture_buf[i] = ' ';
        }
    }
    // Ensure null termination
    g_capture_buf[g_capture_len] = 0;
}

static int llmk_oo_build_think_prompt(int id, const char *user, char *out, int out_cap) {
    if (!out || out_cap <= 4) return 0;
    out[0] = 0;

    char goal[160];
    char dig[256];
    char tail[256];
    char next_action[96];
    goal[0] = 0;
    dig[0] = 0;
    tail[0] = 0;
    next_action[0] = 0;

    if (!llmk_oo_get_brief(id, goal, (int)sizeof(goal), dig, (int)sizeof(dig))) {
        return 0;
    }
    llmk_oo_get_notes_tail(id, tail, (int)sizeof(tail), 240);
    llmk_oo_agenda_peek(id, next_action, (int)sizeof(next_action));
    int todo = llmk_oo_agenda_count(id);

    int p = 0;
    const char *prefix = "OO_THINK. Respond concisely. Goal: ";
    for (const char *s = prefix; *s && p + 1 < out_cap; s++) out[p++] = *s;
    for (int k = 0; goal[k] && p + 1 < out_cap; k++) out[p++] = goal[k];

    if (dig[0]) {
        const char *d1 = "\nDigest: ";
        for (const char *s = d1; *s && p + 1 < out_cap; s++) out[p++] = *s;
        for (int k = 0; dig[k] && p + 1 < out_cap; k++) out[p++] = dig[k];
    }
    if (tail[0]) {
        const char *n1 = "\nNotes: ";
        for (const char *s = n1; *s && p + 1 < out_cap; s++) out[p++] = *s;
        for (int k = 0; tail[k] && p + 1 < out_cap; k++) out[p++] = tail[k];
    }

    if (next_action[0]) {
        const char *a1 = "\nNext action: ";
        for (const char *s = a1; *s && p + 1 < out_cap; s++) out[p++] = *s;
        for (int k = 0; next_action[k] && p + 1 < out_cap; k++) out[p++] = next_action[k];
        if (todo > 1) {
            const char *a2 = " (";
            for (const char *s = a2; *s && p + 1 < out_cap; s++) out[p++] = *s;
            // small itoa
            int v = todo;
            char rev[16]; // SAFE: enough for int decimal digits; bounded by sizeof(rev)
            int rn = 0;
            while (v > 0 && rn < (int)sizeof(rev)) { rev[rn++] = (char)('0' + (v % 10)); v /= 10; }
            while (rn > 0 && p + 1 < out_cap) out[p++] = rev[--rn];
            const char *a3 = " total)";
            for (const char *s = a3; *s && p + 1 < out_cap; s++) out[p++] = *s;
        }
    }

    const char *u1 = "\nUser: ";
    for (const char *s = u1; *s && p + 1 < out_cap; s++) out[p++] = *s;
    if (user && user[0]) {
        for (const char *s = user; *s && p + 1 < out_cap; s++) out[p++] = *s;
    } else {
        const char *def = "next concrete action";
        for (const char *s = def; *s && p + 1 < out_cap; s++) out[p++] = *s;
    }

    const char *suf = "\nAnswer:\n";
    for (const char *s = suf; *s && p + 1 < out_cap; s++) out[p++] = *s;
    out[p] = 0;
    return 1;
}

static int llmk_ascii_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

// Parse an entity id from prompt, starting at *io_i.
// Accepts either "1" or "<1>" (optional whitespace inside the brackets).
// Updates *io_i to the first non-space char after the id (and optional '>').
static int llmk_parse_entity_id_allow_brackets(const char *prompt, int *io_i) {
    if (!prompt || !io_i) return 0;
    int i = *io_i;
    while (prompt[i] == ' ' || prompt[i] == '\t') i++;

    int had_bracket = 0;
    if (prompt[i] == '<') {
        had_bracket = 1;
        i++;
        while (prompt[i] == ' ' || prompt[i] == '\t') i++;
    }

    int id = 0;
    while (prompt[i] >= '0' && prompt[i] <= '9') {
        id = id * 10 + (prompt[i] - '0');
        i++;
    }

    while (prompt[i] == ' ' || prompt[i] == '\t') i++;
    if (had_bracket && prompt[i] == '>') i++;
    while (prompt[i] == ' ' || prompt[i] == '\t') i++;

    *io_i = i;
    return id;
}

static UINT32 llmk_u32_ctz(UINT32 x) {
    if (x == 0) return 32;
    UINT32 n = 0;
    while ((x & 1U) == 0U) { n++; x >>= 1; }
    return n;
}

static UINT32 llmk_u32_popcount(UINT32 x) {
    UINT32 n = 0;
    while (x) { x &= (x - 1U); n++; }
    return n;
}

static EFI_STATUS llmk_gop_init_best_effort(void) {
    g_gop = NULL;
    g_gop_fb32 = NULL;
    g_gop_w = g_gop_h = g_gop_ppsl = 0;
    g_gop_pf = PixelFormatMax;
    g_gop_mask.RedMask = g_gop_mask.GreenMask = g_gop_mask.BlueMask = g_gop_mask.ReservedMask = 0;

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateProtocol, 3, &gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop);
    if (EFI_ERROR(st) || !gop || !gop->Mode || !gop->Mode->Info) {
        return EFI_NOT_FOUND;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    if (info->PixelFormat == PixelBltOnly) {
        return EFI_UNSUPPORTED;
    }
    if (gop->Mode->FrameBufferBase == 0 || gop->Mode->FrameBufferSize < 4) {
        return EFI_UNSUPPORTED;
    }

    g_gop = gop;
    g_gop_w = info->HorizontalResolution;
    g_gop_h = info->VerticalResolution;
    g_gop_ppsl = info->PixelsPerScanLine;
    g_gop_pf = info->PixelFormat;
    g_gop_mask = info->PixelInformation;
    g_gop_fb32 = (volatile UINT32 *)(UINTN)gop->Mode->FrameBufferBase;

    // Sanity: require 32bpp-like stride.
    UINT64 needed = (UINT64)g_gop_ppsl * (UINT64)g_gop_h * 4ULL;
    if (needed > (UINT64)gop->Mode->FrameBufferSize) {
        // Still allow writes, but clamp later.
    }
    return EFI_SUCCESS;
}

// Force screen update after rendering (some firmware needs this trigger)
static void llmk_gop_force_update(void) {
    if (!g_gop || !g_gop_fb32) return;
    // Touch a single pixel at 0,0 to trigger screen refresh
    volatile UINT32 *fb = (volatile UINT32 *)g_gop_fb32;
    UINT32 old = fb[0];
    fb[0] = old ^ 0x00000001;  // Flip LSB
    fb[0] = old;               // Restore original
}

static void llmk_gop_put_pixel(UINT32 x, UINT32 y, UINT8 r, UINT8 g, UINT8 b);

// Forward decls (used by optional GOP TUI before their definitions).
static void llmk_ascii_copy_cap(char *dst, int dst_cap, const char *src);
static int llmk_ascii_append_u32(char *dst, int cap, int pos, UINT32 v);
static void llmk_print_ascii(const char *s);
static void llmk_tui_redraw_best_effort(void);
static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name);
static int llmk_char16_streq(const CHAR16 *a, const CHAR16 *b);

static void djibion_apply_transform_path(char *io_path, int cap, const DjibionDecision *d) {
    if (!io_path || cap <= 1 || !d) return;
    if (d->verdict != DJIBION_VERDICT_TRANSFORM) return;
    if (!d->transformed_arg0[0]) return;
    llmk_ascii_copy_cap(io_path, cap, d->transformed_arg0);
}

static void djibion_log_if_observe(const DjibionEngine *e, const char *act_name, const DjibionDecision *d) {
    if (!e || !d || !act_name) return;
    if (e->mode != DJIBION_MODE_OBSERVE) return;

    Print(L"[djibion] ");
    llmk_print_ascii(act_name);
    Print(L" verdict=%d risk=%d tri=%d/%d/%d reason=",
          (int)d->verdict,
          (int)d->risk,
          (int)d->tri.sense.score,
          (int)d->tri.structure.score,
          (int)d->tri.reality.score);
    if (d->reason[0]) llmk_print_ascii(d->reason);
    else Print(L"(none)");
    if (d->verdict == DJIBION_VERDICT_TRANSFORM && d->transformed_arg0[0]) {
        Print(L" transform->");
        llmk_print_ascii(d->transformed_arg0);
    }
    Print(L"\r\n");
}

static UINT32 llmk_gop_pack_rgb(UINT8 r, UINT8 g, UINT8 b, int *out_ok) {
    if (out_ok) *out_ok = 0;
    if (g_gop_pf == PixelBlueGreenRedReserved8BitPerColor) {
        if (out_ok) *out_ok = 1;
        return ((UINT32)b) | ((UINT32)g << 8) | ((UINT32)r << 16) | (0xFFU << 24);
    }
    if (g_gop_pf == PixelRedGreenBlueReserved8BitPerColor) {
        if (out_ok) *out_ok = 1;
        return ((UINT32)r) | ((UINT32)g << 8) | ((UINT32)b << 16) | (0xFFU << 24);
    }
    if (g_gop_pf == PixelBitMask) {
        UINT32 rm = g_gop_mask.RedMask;
        UINT32 gm = g_gop_mask.GreenMask;
        UINT32 bm = g_gop_mask.BlueMask;
        UINT32 rs = llmk_u32_ctz(rm);
        UINT32 gs = llmk_u32_ctz(gm);
        UINT32 bs = llmk_u32_ctz(bm);
        UINT32 rbits = llmk_u32_popcount(rm);
        UINT32 gbits = llmk_u32_popcount(gm);
        UINT32 bbits = llmk_u32_popcount(bm);
        UINT32 rmax = (rbits >= 32) ? 0xFFFFFFFFU : ((1U << rbits) - 1U);
        UINT32 gmax = (gbits >= 32) ? 0xFFFFFFFFU : ((1U << gbits) - 1U);
        UINT32 bmax = (bbits >= 32) ? 0xFFFFFFFFU : ((1U << bbits) - 1U);
        UINT32 rv = (rmax == 0) ? 0 : ((UINT32)r * rmax + 127U) / 255U;
        UINT32 gv = (gmax == 0) ? 0 : ((UINT32)g * gmax + 127U) / 255U;
        UINT32 bv = (bmax == 0) ? 0 : ((UINT32)b * bmax + 127U) / 255U;
        if (out_ok) *out_ok = 1;
        return ((rv << rs) & rm) | ((gv << gs) & gm) | ((bv << bs) & bm);
    }
    return 0;
}

static void llmk_gop_fill_rect_solid(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    if (w == 0 || h == 0) return;
    if (x >= g_gop_w || y >= g_gop_h) return;
    UINT32 x2 = x + w;
    UINT32 y2 = y + h;
    if (x2 > g_gop_w) x2 = g_gop_w;
    if (y2 > g_gop_h) y2 = g_gop_h;
    int ok = 0;
    UINT32 px = llmk_gop_pack_rgb(r, g, b, &ok);
    if (!ok) return;
    for (UINT32 yy = y; yy < y2; yy++) {
        UINTN row = (UINTN)yy * (UINTN)g_gop_ppsl;
        for (UINT32 xx = x; xx < x2; xx++) {
            g_gop_fb32[row + (UINTN)xx] = px;
        }
    }
}

typedef struct {
    char c;
    UINT8 rows[7]; // 5-bit rows, MSB on the left (bits 4..0)
} LlmkGlyph5x7;

static const LlmkGlyph5x7 g_font_5x7[] = {
    { ' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
    { '-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00} },
    { '_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F} },
    { '.', {0x00,0x00,0x00,0x00,0x00,0x06,0x06} },
    { ':', {0x00,0x06,0x06,0x00,0x06,0x06,0x00} },
    { '/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00} },
    { '<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02} },
    { '>', {0x08,0x04,0x02,0x01,0x02,0x04,0x08} },
    { '[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E} },
    { ']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E} },
    { '(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02} },
    { ')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08} },
    { '*', {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00} },
    { '#', {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00} },
    { '+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00} },
    { '=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00} },
    { '?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04} },
    { '0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E} },
    { '1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E} },
    { '2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F} },
    { '3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E} },
    { '4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02} },
    { '5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E} },
    { '6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E} },
    { '7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08} },
    { '8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E} },
    { '9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C} },
    { 'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E} },
    { 'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E} },
    { 'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E} },
    { 'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F} },
    { 'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10} },
    { 'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F} },
    { 'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E} },
    { 'J', {0x07,0x02,0x02,0x02,0x12,0x12,0x0C} },
    { 'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11} },
    { 'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F} },
    { 'M', {0x11,0x1B,0x15,0x11,0x11,0x11,0x11} },
    { 'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11} },
    { 'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10} },
    { 'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D} },
    { 'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11} },
    { 'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E} },
    { 'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04} },
    { 'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04} },
    { 'W', {0x11,0x11,0x11,0x11,0x15,0x1B,0x11} },
    { 'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11} },
    { 'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04} },
    { 'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F} },
};

static const UINT8 *llmk_font5x7_get(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (UINTN i = 0; i < (sizeof(g_font_5x7) / sizeof(g_font_5x7[0])); i++) {
        if (g_font_5x7[i].c == c) return g_font_5x7[i].rows;
    }
    // fallback
    for (UINTN i = 0; i < (sizeof(g_font_5x7) / sizeof(g_font_5x7[0])); i++) {
        if (g_font_5x7[i].c == '?') return g_font_5x7[i].rows;
    }
    return NULL;
}

static void llmk_tui_set_event(const char *msg) {
    if (!msg) { g_tui_last_event[0] = 0; return; }
    llmk_ascii_copy_cap(g_tui_last_event, (int)sizeof(g_tui_last_event), msg);
    g_tui_dirty = 1;
}

static void llmk_tr_clear(void) {
    g_tr_write = 0;
    g_tr_count = 0;
    g_tr_cur_len = 0;
    g_tr_scroll = 0;
    g_tr_cur[0] = 0;
    for (UINT32 i = 0; i < LLMK_TR_LINES; i++) g_tr_lines[i][0] = 0;
    g_tui_dirty = 1;
}

static void llmk_tr_push_line(const char *line) {
    if (!line) line = "";

    UINT32 idx = g_tr_write % LLMK_TR_LINES;
    g_tr_write = (g_tr_write + 1) % LLMK_TR_LINES;
    if (g_tr_count < LLMK_TR_LINES) g_tr_count++;

    int p = 0;
    for (const char *s = line; *s && p + 1 < (int)LLMK_TR_COLS; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        g_tr_lines[idx][p++] = (char)c;
    }
    g_tr_lines[idx][p] = 0;
    g_tui_dirty = 1;
}

static void llmk_tr_flush_cur_line(void) {
    g_tr_cur[g_tr_cur_len] = 0;
    llmk_tr_push_line(g_tr_cur);
    g_tr_cur_len = 0;
    g_tr_cur[0] = 0;
}

static void llmk_tr_note(const char *msg) {
    llmk_tr_push_line(msg);
}

static void llmk_tr_push_prefixed(const char *prefix, const char *msg) {
    char line[LLMK_TR_COLS];
    int p = 0;
    line[0] = 0;
    if (!prefix) prefix = "";
    if (!msg) msg = "";
    for (const char *s = prefix; *s && p + 1 < (int)sizeof(line); s++) line[p++] = *s;
    for (const char *s = msg; *s && p + 1 < (int)sizeof(line); s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        line[p++] = (char)c;
    }
    line[p] = 0;
    llmk_tr_push_line(line);
}

static const char *llmk_tr_get_line_by_age(UINT32 age_from_newest) {
    // age_from_newest=0 -> newest line
    if (g_tr_count == 0) return "";
    if (age_from_newest >= g_tr_count) age_from_newest = g_tr_count - 1;

    UINT32 newest = (g_tr_write + LLMK_TR_LINES - 1) % LLMK_TR_LINES;
    UINT32 idx = (newest + LLMK_TR_LINES - (age_from_newest % LLMK_TR_LINES)) % LLMK_TR_LINES;
    return g_tr_lines[idx];
}

static void llmk_tr_append_ascii_bytes(const unsigned char *bytes, int len) {
    if (!bytes || len <= 0) return;

    for (int i = 0; i < len; i++) {
        unsigned char c = bytes[i];
        if (c == 0) continue;
        if (c == '\r' || c == '\n') {
            llmk_tr_flush_cur_line();
            continue;
        }
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        if (g_tr_cur_len + 1 >= (int)sizeof(g_tr_cur)) {
            llmk_tr_flush_cur_line();
        }
        g_tr_cur[g_tr_cur_len++] = (char)c;
    }
    g_tr_cur[g_tr_cur_len] = 0;
}

static void llmk_tui_on_prompt_best_effort(const char *prompt) {
    if (!g_tui_enabled || !g_gop_fb32) return;
    if (!prompt || prompt[0] == 0) {
        llmk_tui_set_event("(empty)");
        llmk_tui_redraw_best_effort();
        return;
    }

    if (prompt[0] == '/') {
        char cmd[64];
        int n = 0;
        while (prompt[n] && !llmk_ascii_is_space(prompt[n]) && prompt[n] != ';' && n + 1 < (int)sizeof(cmd)) {
            cmd[n] = prompt[n];
            n++;
        }
        cmd[n] = 0;
        llmk_tui_set_event(cmd[0] ? cmd : "/");
    } else {
        llmk_tui_set_event("prompt");
    }

    llmk_tui_redraw_best_effort();
}

static void llmk_ascii_append_cap(char *dst, int dst_cap, const char *src) {
    if (!dst || dst_cap <= 0) return;
    if (!src) return;
    int n = 0;
    while (n < dst_cap && dst[n]) n++;
    if (n >= dst_cap - 1) return;
    int i = 0;
    while (src[i] && n + 1 < dst_cap) {
        dst[n++] = src[i++];
    }
    dst[n] = 0;
}

static void llmk_tui_append_u32(char *dst, int cap, UINT32 v) {
    if (!dst || cap <= 0) return;
    int pos = 0;
    while (pos < cap && dst[pos]) pos++;
    pos = llmk_ascii_append_u32(dst, cap, pos, v);
    if (pos < cap) dst[pos] = 0;
    else dst[cap - 1] = 0;
}

static void llmk_gop_draw_char5x7(UINT32 x, UINT32 y, UINT32 scale,
                                 UINT8 fg_r, UINT8 fg_g, UINT8 fg_b,
                                 UINT8 bg_r, UINT8 bg_g, UINT8 bg_b,
                                 char c) {
    const UINT8 *rows = llmk_font5x7_get(c);
    if (!rows) return;

    // Background cell (5x7 + 1 column gap)
    llmk_gop_fill_rect_solid(x, y, (5U + 1U) * scale, 7U * scale, bg_r, bg_g, bg_b);
    for (UINT32 yy = 0; yy < 7; yy++) {
        UINT8 bits = rows[yy] & 0x1FU;
        for (UINT32 xx = 0; xx < 5; xx++) {
            UINT8 on = (UINT8)((bits >> (4 - xx)) & 1U);
            if (on) {
                llmk_gop_fill_rect_solid(x + xx * scale, y + yy * scale, scale, scale, fg_r, fg_g, fg_b);
            }
        }
    }
}

static void llmk_gop_draw_text5x7(UINT32 x, UINT32 y, UINT32 scale,
                                 UINT8 fg_r, UINT8 fg_g, UINT8 fg_b,
                                 UINT8 bg_r, UINT8 bg_g, UINT8 bg_b,
                                 const char *text) {
    if (!text) return;
    UINT32 cx = x;
    for (const char *p = text; *p; p++) {
        llmk_gop_draw_char5x7(cx, y, scale, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, *p);
        cx += (5U + 1U) * scale;
    }
}

static void llmk_ui_draw_text_clipped(UINT32 x, UINT32 y, UINT32 scale,
                                     UINT8 fg_r, UINT8 fg_g, UINT8 fg_b,
                                     UINT8 bg_r, UINT8 bg_g, UINT8 bg_b,
                                     const char *text, int max_chars) {
    if (!text || max_chars <= 0) return;
    char tmp[256];
    int p = 0;
    for (const char *s = text; *s && p + 1 < (int)sizeof(tmp) && p < max_chars; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) c = '?';
        tmp[p++] = (char)c;
    }
    tmp[p] = 0;
    llmk_gop_draw_text5x7(x, y, scale, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, tmp);
}

static void llmk_char16_to_ascii_cap(char *dst, int cap, const CHAR16 *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int p = 0;
    for (int i = 0; src[i] && p + 1 < cap; i++) {
        UINT16 ch = (UINT16)src[i];
        char c = (ch < 0x80) ? (char)ch : '?';
        if ((unsigned char)c < 0x20) c = ' ';
        dst[p++] = c;
    }
    dst[p] = 0;
}

static void llmk_fb_clear_preview(void) {
    g_fb_preview_count = 0;
    for (int i = 0; i < LLMK_FB_PREVIEW_LINES; i++) g_fb_preview[i][0] = 0;
}

static int llmk_read_file_prefix_best_effort(const CHAR16 *path, UINTN max_bytes, void **out_buf, UINTN *out_len) {
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    if (!path || !out_buf || !out_len) return 0;
    if (!g_root) return 0;
    if (max_bytes == 0) return 0;
    if (max_bytes > (256U * 1024U)) max_bytes = (256U * 1024U);

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, path);
    if (EFI_ERROR(st) || !f) return 0;

    void *buf = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, max_bytes + 1, &buf);
    if (EFI_ERROR(st) || !buf) {
        uefi_call_wrapper(f->Close, 1, f);
        return 0;
    }

    UINTN want = max_bytes;
    EFI_STATUS st2 = uefi_call_wrapper(f->Read, 3, f, &want, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st2)) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }
    ((UINT8 *)buf)[want] = 0;
    *out_buf = buf;
    *out_len = want;
    return 1;
}

static void llmk_fb_build_preview_from_bytes(const void *raw, UINTN raw_len) {
    llmk_fb_clear_preview();
    if (!raw || raw_len == 0) return;

    const UINT8 *b = (const UINT8 *)raw;
    UINTN n = raw_len;
    if (n > (UINTN)(LLMK_FB_PREVIEW_LINES * LLMK_FB_PREVIEW_COLS * 2)) {
        n = (UINTN)(LLMK_FB_PREVIEW_LINES * LLMK_FB_PREVIEW_COLS * 2);
    }

    int line = 0;
    int col = 0;

    // UTF-16 BOM detection (LE/BE). Down-convert to ASCII-ish.
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        int is_le = (b[0] == 0xFF);
        UINTN chars = (n - 2) / 2;
        for (UINTN i = 0; i < chars; i++) {
            UINT8 lo = b[2 + i * 2 + 0];
            UINT8 hi = b[2 + i * 2 + 1];
            UINT16 ch = is_le ? (UINT16)(lo | ((UINT16)hi << 8)) : (UINT16)(hi | ((UINT16)lo << 8));
            if (ch == 0) break;
            char c = (ch < 0x80) ? (char)ch : '?';
            if (c == '\r') continue;
            if (c == '\n') { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; continue; }
            if (c == '\t') c = ' ';
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = '?';
            if (col + 1 >= LLMK_FB_PREVIEW_COLS) { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; }
            g_fb_preview[line][col++] = c;
        }
    } else {
        for (UINTN i = 0; i < n; i++) {
            UINT8 ch = b[i];
            if (ch == 0) break;
            char c = (char)ch;
            if (c == '\r') continue;
            if (c == '\n') { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; continue; }
            if (c == '\t') c = ' ';
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = '?';
            if (col + 1 >= LLMK_FB_PREVIEW_COLS) { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; }
            g_fb_preview[line][col++] = c;
        }
    }

    if (line < LLMK_FB_PREVIEW_LINES) {
        g_fb_preview[line][col] = 0;
        g_fb_preview_count = line + 1;
    } else {
        g_fb_preview_count = LLMK_FB_PREVIEW_LINES;
    }
}

static int llmk_fb_refresh_best_effort(void) {
    if (!g_root) return 0;

    for (int i = 0; i < LLMK_FB_MAX_ENTRIES; i++) {
        g_fb_entries[i].name16[0] = 0;
        g_fb_entries[i].name8[0] = 0;
        g_fb_entries[i].is_dir = 0;
        g_fb_entries[i].size = 0;
    }
    g_fb_count = 0;
    llmk_fb_clear_preview();

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!g_fb_path16[0] || llmk_char16_streq(g_fb_path16, L".") || llmk_char16_streq(g_fb_path16, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, g_fb_path16, &dir, NULL, 0, L"fb_dir");
        if (EFI_ERROR(st) || !dir) return 0;
        close_dir = 1;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        return 0;
    }

    while (g_fb_count < LLMK_FB_MAX_ENTRIES) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (info->FileName && (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L".."))) {
            continue;
        }

        LlmkFbEntry *e = &g_fb_entries[g_fb_count++];
        e->is_dir = (info->Attribute & EFI_FILE_DIRECTORY) ? 1 : 0;
        e->size = info->FileSize;
        if (info->FileName) {
            StrnCpy(e->name16, info->FileName, (sizeof(e->name16) / sizeof(e->name16[0])) - 1);
            e->name16[(sizeof(e->name16) / sizeof(e->name16[0])) - 1] = 0;
            llmk_char16_to_ascii_cap(e->name8, (int)sizeof(e->name8), e->name16);
        } else {
            StrCpy(e->name16, L"(null)");
            llmk_ascii_copy_cap(e->name8, (int)sizeof(e->name8), "(null)");
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);

    if (g_fb_sel < 0) g_fb_sel = 0;
    if (g_fb_sel >= g_fb_count) g_fb_sel = (g_fb_count > 0) ? (g_fb_count - 1) : 0;
    return 1;
}

static void llmk_fb_preview_selected_best_effort(void) {
    llmk_fb_clear_preview();
    if (g_fb_count <= 0) return;
    if (g_fb_sel < 0 || g_fb_sel >= g_fb_count) return;
    if (g_fb_entries[g_fb_sel].is_dir) return;

    CHAR16 path[192];
    path[0] = 0;
    if (!g_fb_path16[0] || llmk_char16_streq(g_fb_path16, L"\\") || llmk_char16_streq(g_fb_path16, L".")) {
        // Root
        StrCpy(path, g_fb_entries[g_fb_sel].name16);
    } else {
        StrCpy(path, g_fb_path16);
        UINTN n = StrLen(path);
        if (n > 0 && path[n - 1] != L'\\') StrCat(path, L"\\");
        StrCat(path, g_fb_entries[g_fb_sel].name16);
    }

    void *buf = NULL;
    UINTN len = 0;
    if (!llmk_read_file_prefix_best_effort(path, 4096, &buf, &len)) return;
    llmk_fb_build_preview_from_bytes(buf, len);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_tui_redraw_best_effort(void) {
    if (!g_tui_enabled) return;
    if (!g_gop_fb32) return;

    const UINT32 scale = 2;
    const UINT32 char_w = (5U + 1U) * scale;
    const UINT32 line_h = 8U * scale;
    const UINT32 pad = 6;

    if (g_ui_mode == 0) {
        // Legacy status-only panel
        const UINT32 x = 8;
        const UINT32 y = 8;
        const UINT32 panel_w = 360;
        const UINT32 panel_h = (line_h * 6U) + pad * 2U;

        llmk_gop_fill_rect_solid(x, y, panel_w, panel_h, 0, 0, 32);
        llmk_gop_fill_rect_solid(x, y, panel_w, 1, 80, 80, 120);
        llmk_gop_fill_rect_solid(x, y + panel_h - 1, panel_w, 1, 80, 80, 120);

        char line1[96];
        char line2[96];
        char line3[96];
        char line4[96];
        char line5[96];
        char line6[96];
        line1[0] = line2[0] = line3[0] = line4[0] = line5[0] = line6[0] = 0;

        llmk_ascii_copy_cap(line1, (int)sizeof(line1), "LLMK UI [STATUS]");

        llmk_ascii_copy_cap(line2, (int)sizeof(line2), "KV_POS=");
        if (g_llmk_kv_pos > 0) llmk_tui_append_u32(line2, (int)sizeof(line2), (UINT32)g_llmk_kv_pos);
        else llmk_ascii_append_cap(line2, (int)sizeof(line2), "0");

        llmk_ascii_copy_cap(line3, (int)sizeof(line3), "OO_AUTO=");
        llmk_ascii_append_cap(line3, (int)sizeof(line3), g_oo_auto_active ? "1" : "0");
        llmk_ascii_append_cap(line3, (int)sizeof(line3), " OO_EXEC=");
        llmk_ascii_append_cap(line3, (int)sizeof(line3), g_oo_exec_active ? "1" : "0");

        llmk_ascii_copy_cap(line4, (int)sizeof(line4), "GEN=");
        llmk_ascii_append_cap(line4, (int)sizeof(line4), g_tui_gen_active ? "1" : "0");
        llmk_ascii_append_cap(line4, (int)sizeof(line4), " TOK=");
        llmk_tui_append_u32(line4, (int)sizeof(line4), (UINT32)g_tui_gen_tokens);

        llmk_ascii_copy_cap(line5, (int)sizeof(line5), "TICK=");
        llmk_tui_append_u32(line5, (int)sizeof(line5), (UINT32)g_tui_last_tick);
        llmk_ascii_append_cap(line5, (int)sizeof(line5), " ID=");
        llmk_tui_append_u32(line5, (int)sizeof(line5), (UINT32)g_tui_last_id);

        llmk_ascii_copy_cap(line6, (int)sizeof(line6), "EVT=");
        if (g_tui_last_event[0]) llmk_ascii_append_cap(line6, (int)sizeof(line6), g_tui_last_event);
        else llmk_ascii_append_cap(line6, (int)sizeof(line6), "(none)");

        UINT32 ty = y + pad;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 255, 255, 255, 0, 0, 32, line1);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 220, 255, 0, 0, 32, line2);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 255, 220, 0, 0, 32, line3);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 255, 220, 220, 0, 0, 32, line4);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 220, 220, 0, 0, 32, line5);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 220, 220, 0, 0, 32, line6);

        llmk_gop_force_update();
        g_tui_dirty = 0;
        return;
    }

    // Split/log/files UI
    const UINT32 x0 = 8;
    const UINT32 y0 = 8;
    UINT32 w0 = (g_gop_w > 16) ? (g_gop_w - 16) : g_gop_w;
    UINT32 h0 = (g_gop_h > 16) ? (g_gop_h - 16) : g_gop_h;
    if (w0 < 320) w0 = 320;
    if (h0 < 200) h0 = 200;

    // Background (keep it moderate; redraws are throttled)
    llmk_gop_fill_rect_solid(x0, y0, w0, h0, 0, 0, 0);

    // Header
    const UINT32 header_h = line_h * 2U + pad * 2U;
    llmk_gop_fill_rect_solid(x0, y0, w0, header_h, 0, 0, 32);
    llmk_gop_fill_rect_solid(x0, y0 + header_h, w0, 1, 80, 80, 120);

    char hdr1[128];
    char hdr2[128];
    hdr1[0] = hdr2[0] = 0;

    llmk_ascii_copy_cap(hdr1, (int)sizeof(hdr1), "LLMK UI ");
    if (g_ui_mode == 1) llmk_ascii_append_cap(hdr1, (int)sizeof(hdr1), "[LOG]");
    else if (g_ui_mode == 2) llmk_ascii_append_cap(hdr1, (int)sizeof(hdr1), "[SPLIT]");
    else llmk_ascii_append_cap(hdr1, (int)sizeof(hdr1), "[FILES]");

    llmk_ascii_copy_cap(hdr2, (int)sizeof(hdr2), "KV=");
    llmk_tui_append_u32(hdr2, (int)sizeof(hdr2), (UINT32)g_llmk_kv_pos);
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), " GEN=");
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), g_tui_gen_active ? "1" : "0");
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), " TOK=");
    llmk_tui_append_u32(hdr2, (int)sizeof(hdr2), (UINT32)g_tui_gen_tokens);
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), " EVT=");
    if (g_tui_last_event[0]) llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), g_tui_last_event);
    else llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), "-");

    UINT32 ty = y0 + pad;
    llmk_ui_draw_text_clipped(x0 + pad, ty, scale, 255, 255, 255, 0, 0, 32, hdr1, (int)((w0 - pad * 2U) / char_w));
    ty += line_h;
    llmk_ui_draw_text_clipped(x0 + pad, ty, scale, 220, 220, 220, 0, 0, 32, hdr2, (int)((w0 - pad * 2U) / char_w));

    // Body panes
    UINT32 body_y = y0 + header_h + 1;
    UINT32 body_h = (y0 + h0 > body_y) ? ((y0 + h0) - body_y) : 0;
    if (body_h < line_h * 2U) {
        llmk_gop_force_update();
        g_tui_dirty = 0;
        return;
    }

    UINT32 log_x = x0;
    UINT32 log_y = body_y;
    UINT32 log_w = w0;
    UINT32 log_h = body_h;

    UINT32 files_x = 0, files_y = 0, files_w = 0, files_h = 0;
    int show_files = (g_ui_mode >= 2);
    if (show_files) {
        UINT32 split = (w0 * 2U) / 3U;
        if (split < 240) split = 240;
        if (split + 240 > w0) split = (w0 > 240) ? (w0 - 240) : w0;
        log_w = split;
        files_x = x0 + log_w + 1;
        files_y = body_y;
        files_w = (x0 + w0 > files_x) ? ((x0 + w0) - files_x) : 0;
        files_h = body_h;
        llmk_gop_fill_rect_solid(x0 + log_w, body_y, 1, body_h, 80, 80, 120);
    }

    // Log pane background
    llmk_gop_fill_rect_solid(log_x, log_y, log_w, log_h, 0, 0, 24);

    // Render newest lines at bottom-ish (simple top-down with scroll)
    int max_chars = (int)((log_w - pad * 2U) / char_w);
    int max_lines = (int)((log_h - pad * 2U) / line_h);
    if (max_lines < 1) max_lines = 1;

    UINT32 start_age = 0;
    if (g_tr_scroll < 0) g_tr_scroll = 0;
    if ((UINT32)g_tr_scroll > g_tr_count) g_tr_scroll = (int)g_tr_count;
    start_age = (UINT32)g_tr_scroll;

    // Draw from newest backwards.
    UINT32 ly = log_y + pad;
    for (int i = 0; i < max_lines; i++) {
        const char *line = llmk_tr_get_line_by_age(start_age + (UINT32)(max_lines - 1 - i));
        llmk_ui_draw_text_clipped(log_x + pad, ly, scale, 220, 220, 220, 0, 0, 24, line, max_chars);
        ly += line_h;
    }

    if (show_files && files_w > 0) {
        llmk_gop_fill_rect_solid(files_x, files_y, files_w, files_h, 0, 16, 0);
        int f_chars = (int)((files_w - pad * 2U) / char_w);
        int f_lines = (int)((files_h - pad * 2U) / line_h);
        if (f_lines < 1) f_lines = 1;

        // Path header
        char pbuf[128];
        llmk_ascii_copy_cap(pbuf, (int)sizeof(pbuf), "PATH=");
        llmk_ascii_append_cap(pbuf, (int)sizeof(pbuf), g_fb_path8[0] ? g_fb_path8 : "\\");
        llmk_ui_draw_text_clipped(files_x + pad, files_y + pad, scale, 220, 255, 220, 0, 16, 0, pbuf, f_chars);

        // List + preview
        int list_lines = f_lines - 1;
        if (list_lines < 1) list_lines = 1;
        int preview_lines = LLMK_FB_PREVIEW_LINES;
        if (list_lines > preview_lines + 2) {
            list_lines = list_lines - preview_lines - 1;
        }
        if (list_lines < 1) list_lines = 1;

        UINT32 fy = files_y + pad + line_h;
        for (int i = 0; i < list_lines; i++) {
            int idx = i;
            if (idx >= g_fb_count) break;
            char name_line[96];
            name_line[0] = 0;
            if (idx == g_fb_sel) llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "> ");
            else llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "  ");
            if (g_fb_entries[idx].is_dir) llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "[D] ");
            else llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "    ");
            llmk_ascii_append_cap(name_line, (int)sizeof(name_line), g_fb_entries[idx].name8);
            llmk_ui_draw_text_clipped(files_x + pad, fy, scale,
                                      (idx == g_fb_sel) ? 255 : 200,
                                      (idx == g_fb_sel) ? 255 : 220,
                                      (idx == g_fb_sel) ? 255 : 200,
                                      0, 16, 0,
                                      name_line,
                                      f_chars);
            fy += line_h;
        }

        // Preview separator
        if (g_fb_preview_count > 0) {
            llmk_gop_fill_rect_solid(files_x, fy, files_w, 1, 80, 120, 80);
            fy += 2;
            for (int i = 0; i < g_fb_preview_count && i < LLMK_FB_PREVIEW_LINES; i++) {
                llmk_ui_draw_text_clipped(files_x + pad, fy, scale, 220, 220, 220, 0, 16, 0, g_fb_preview[i], f_chars);
                fy += line_h;
            }
        }
    }

    llmk_gop_force_update();
    g_tui_dirty = 0;
}

static void llmk_oo_on_step_gop(int id, int tick, int energy) {
    g_tui_last_id = id;
    g_tui_last_tick = tick;
    g_tui_last_energy = energy;
    if (!g_gop_fb32 || !g_gop_w || !g_gop_h) return;
    UINT32 x = (UINT32)((tick * 13 + id * 31) % (int)g_gop_w);
    UINT32 y = (UINT32)((tick * 7 + id * 17) % (int)g_gop_h);
    llmk_gop_put_pixel(x, y, 0, 255, 0);

    // Best-effort TUI refresh at a low cadence to avoid heavy overhead.
    if (g_tui_enabled && ((tick & 7) == 0 || g_tui_dirty)) {
        llmk_tui_redraw_best_effort();
    } else {
        llmk_gop_force_update();
    }
}

static void llmk_gop_put_pixel(UINT32 x, UINT32 y, UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    if (x >= g_gop_w || y >= g_gop_h) return;
    UINTN idx = (UINTN)y * (UINTN)g_gop_ppsl + (UINTN)x;

    int ok = 0;
    UINT32 px = llmk_gop_pack_rgb(r, g, b, &ok);
    if (!ok) return;
    g_gop_fb32[idx] = px;
}

static void llmk_gop_get_pixel(UINT32 x, UINT32 y, UINT8 *out_r, UINT8 *out_g, UINT8 *out_b) {
    if (!out_r || !out_g || !out_b) return;
    *out_r = *out_g = *out_b = 0;
    if (!g_gop_fb32) return;
    if (x >= g_gop_w || y >= g_gop_h) return;
    UINTN idx = (UINTN)y * (UINTN)g_gop_ppsl + (UINTN)x;
    UINT32 px = g_gop_fb32[idx];

    if (g_gop_pf == PixelBlueGreenRedReserved8BitPerColor) {
        *out_b = (UINT8)(px & 0xFFU);
        *out_g = (UINT8)((px >> 8) & 0xFFU);
        *out_r = (UINT8)((px >> 16) & 0xFFU);
    } else if (g_gop_pf == PixelRedGreenBlueReserved8BitPerColor) {
        *out_r = (UINT8)(px & 0xFFU);
        *out_g = (UINT8)((px >> 8) & 0xFFU);
        *out_b = (UINT8)((px >> 16) & 0xFFU);
    } else if (g_gop_pf == PixelBitMask) {
        UINT32 rm = g_gop_mask.RedMask;
        UINT32 gm = g_gop_mask.GreenMask;
        UINT32 bm = g_gop_mask.BlueMask;
        UINT32 rs = llmk_u32_ctz(rm);
        UINT32 gs = llmk_u32_ctz(gm);
        UINT32 bs = llmk_u32_ctz(bm);
        UINT32 rbits = llmk_u32_popcount(rm);
        UINT32 gbits = llmk_u32_popcount(gm);
        UINT32 bbits = llmk_u32_popcount(bm);
        UINT32 rmax = (rbits >= 32) ? 0xFFFFFFFFU : ((1U << rbits) - 1U);
        UINT32 gmax = (gbits >= 32) ? 0xFFFFFFFFU : ((1U << gbits) - 1U);
        UINT32 bmax = (bbits >= 32) ? 0xFFFFFFFFU : ((1U << bbits) - 1U);
        UINT32 rv = (rm == 0) ? 0 : ((px & rm) >> rs);
        UINT32 gv = (gm == 0) ? 0 : ((px & gm) >> gs);
        UINT32 bv = (bm == 0) ? 0 : ((px & bm) >> bs);
        *out_r = (rmax == 0) ? 0 : (UINT8)((rv * 255U) / rmax);
        *out_g = (gmax == 0) ? 0 : (UINT8)((gv * 255U) / gmax);
        *out_b = (bmax == 0) ? 0 : (UINT8)((bv * 255U) / bmax);
    }
}

static void llmk_gop_clear(UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    for (UINT32 y = 0; y < g_gop_h; y++) {
        for (UINT32 x = 0; x < g_gop_w; x++) {
            llmk_gop_put_pixel(x, y, r, g, b);
        }
    }
}

static void llmk_gop_fill_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    if (w == 0 || h == 0) return;
    UINT32 x2 = x + w;
    UINT32 y2 = y + h;
    if (x >= g_gop_w || y >= g_gop_h) return;
    if (x2 > g_gop_w) x2 = g_gop_w;
    if (y2 > g_gop_h) y2 = g_gop_h;
    for (UINT32 yy = y; yy < y2; yy++) {
        for (UINT32 xx = x; xx < x2; xx++) {
            llmk_gop_put_pixel(xx, yy, r, g, b);
        }
    }
}

static const char* llmk_parse_word(const char *s, char *out, int out_cap) {
    if (!s || !out || out_cap <= 0) return s;
    while (*s && llmk_ascii_is_space(*s)) s++;
    int n = 0;
    while (*s && !llmk_ascii_is_space(*s) && *s != ';') {
        if (n + 1 < out_cap) out[n++] = *s;
        s++;
    }
    out[n] = 0;
    return s;
}

static const char* llmk_parse_i32(const char *s, int *out) {
    if (!s || !out) return s;
    while (*s && llmk_ascii_is_space(*s)) s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    int v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10 + (int)(*s - '0');
        s++;
    }
    if (!any) {
        *out = 0;
        return NULL;
    }
    *out = v * sign;
    return s;
}

static const char* llmk_skip_to_stmt_end(const char *s) {
    if (!s) return s;
    while (*s && *s != ';') s++;
    if (*s == ';') s++;
    return s;
}

static int llmk_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

// Last DSL parse error (ASCII). Used to provide useful feedback for /render and /draw.
static char g_last_dsl_error[96];

static void llmk_set_dsl_error(const char *msg, const char *arg) {
    // Store a short ASCII error message (best-effort).
    int p = 0;
    for (const char *s = msg; s && *s && p + 1 < (int)sizeof(g_last_dsl_error); s++) g_last_dsl_error[p++] = *s;
    if (arg) {
        if (p + 2 < (int)sizeof(g_last_dsl_error)) { g_last_dsl_error[p++] = ':'; g_last_dsl_error[p++] = ' '; }
        for (const char *s = arg; *s && p + 1 < (int)sizeof(g_last_dsl_error); s++) {
            char c = *s;
            if (c < 0x20 || c > 0x7E) c = '?';
            g_last_dsl_error[p++] = c;
        }
    }
    g_last_dsl_error[p] = 0;
}

static const char* llmk_find_first_op(const char *s) {
    if (!s) return NULL;
    // Try to find a plausible start of DSL inside a larger prose blob.
    for (const char *p = s; *p; p++) {
        if ((p[0] == 'c' && p[1] == 'l' && p[2] == 'e' && p[3] == 'a' && p[4] == 'r') ||
            (p[0] == 'r' && p[1] == 'e' && p[2] == 'c' && p[3] == 't') ||
            (p[0] == 'p' && p[1] == 'i' && p[2] == 'x' && p[3] == 'e' && p[4] == 'l')) { // SAFE: constant-index prefix checks inside NUL-terminated C string
            return p;
        }
    }
    return NULL;
}

static void llmk_apply_simple_autocorrect(char *buf) {
    // Best-effort fix for common typo seen in logs: "react" -> "rect".
    if (!buf) return;
    for (char *p = buf; p[0] && p[1] && p[2] && p[3] && p[4]; p++) { // SAFE: constant-index lookahead inside NUL-terminated C string
        if (p[0] == 'r' && p[1] == 'e' && p[2] == 'a' && p[3] == 'c' && p[4] == 't') {
            p[2] = 'c'; // SAFE: constant index within validated lookahead window
            // p[3],p[4] already 'c','t' from "react"; make it "rect" by shifting left one.
            p[3] = 't'; // SAFE: constant index within validated lookahead window
            p[4] = ' '; // SAFE: constant index within validated lookahead window
        }
    }
}

static void llmk_draw_fallback_center_square(int white) {
    if (!g_gop_fb32) return;
    llmk_gop_clear(0, 0, 0);
    UINT32 size = g_gop_w < g_gop_h ? g_gop_w : g_gop_h;
    size = size / 4;
    if (size < 32) size = 32;
    UINT32 x = (g_gop_w > size) ? ((g_gop_w - size) / 2) : 0;
    UINT32 y = (g_gop_h > size) ? ((g_gop_h - size) / 2) : 0;
    if (white) llmk_gop_fill_rect(x, y, size, size, 255, 255, 255);
    else llmk_gop_fill_rect(x, y, size, size, 255, 0, 0);
}

static int llmk_render_scene_dsl_ex(const char *dsl, int strict) {
    g_last_dsl_error[0] = 0;
    if (!dsl) { llmk_set_dsl_error("null dsl", NULL); return 0; }
    if (!g_gop_fb32) { llmk_set_dsl_error("no gop", NULL); return 0; }

    // If this is a prose blob, try to find the first DSL op.
    const char *s = dsl;
    const char *first = llmk_find_first_op(dsl);
    if (first) s = first;

    int any = 0;
    while (*s) {
        while (*s && (llmk_ascii_is_space(*s) || *s == ';')) s++;
        if (!*s) break;

        char op[16]; // SAFE: fixed-size op token buffer; parser is passed sizeof(op)
        const char *ns = llmk_parse_word(s, op, (int)sizeof(op));
        if (!ns) { llmk_set_dsl_error("parse op", NULL); return 0; }
        s = ns;

        if (llmk_streq(op, "clear")) {
            int r, g, b;
            s = llmk_parse_i32(s, &r); if (!s) { llmk_set_dsl_error("parse clear", NULL); return 0; }
            s = llmk_parse_i32(s, &g); if (!s) { llmk_set_dsl_error("parse clear", NULL); return 0; }
            s = llmk_parse_i32(s, &b); if (!s) { llmk_set_dsl_error("parse clear", NULL); return 0; }
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            llmk_gop_clear((UINT8)r, (UINT8)g, (UINT8)b);
            any = 1;
            s = llmk_skip_to_stmt_end(s);
        } else if (llmk_streq(op, "rect")) {
            int x, y, w, h, r, g, b;
            s = llmk_parse_i32(s, &x); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &y); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &w); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &h); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &r); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &g); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &b); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            if (x < 0) x = 0; if (y < 0) y = 0;
            if (w < 0) w = 0; if (h < 0) h = 0;
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            llmk_gop_fill_rect((UINT32)x, (UINT32)y, (UINT32)w, (UINT32)h, (UINT8)r, (UINT8)g, (UINT8)b);
            any = 1;
            s = llmk_skip_to_stmt_end(s);
        } else if (llmk_streq(op, "pixel")) {
            int x, y, r, g, b;
            s = llmk_parse_i32(s, &x); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &y); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &r); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &g); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &b); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            if (x < 0) x = 0; if (y < 0) y = 0;
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            llmk_gop_put_pixel((UINT32)x, (UINT32)y, (UINT8)r, (UINT8)g, (UINT8)b);
            any = 1;
            s = llmk_skip_to_stmt_end(s);
        } else {
            if (strict) {
                llmk_set_dsl_error("unknown op", op);
                return 0;
            }
            // Non-strict: skip to ';' to avoid getting stuck.
            s = llmk_skip_to_stmt_end(s);
        }
    }
    if (!any && !g_last_dsl_error[0]) llmk_set_dsl_error("no ops", NULL);
    return any;
}

static int llmk_render_scene_dsl(const char *dsl) {
    return llmk_render_scene_dsl_ex(dsl, 0);
}

static EFI_STATUS llmk_open_binary_file(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    // Best-effort truncate by deleting existing file first.
    EFI_FILE_HANDLE existing = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &existing, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (!EFI_ERROR(st) && existing) {
        uefi_call_wrapper(existing->Delete, 1, existing);
        existing = NULL;
    }

    EFI_FILE_HANDLE f = NULL;
    st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                           EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st)) return st;
    uefi_call_wrapper(f->SetPosition, 2, f, 0);
    *out = f;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_file_write_bytes(EFI_FILE_HANDLE f, const void *buf, UINTN nb) {
    if (!f || (!buf && nb)) return EFI_INVALID_PARAMETER;
    if (nb == 0) return EFI_SUCCESS;
    return uefi_call_wrapper(f->Write, 3, f, &nb, (void *)buf);
}

static EFI_STATUS llmk_read_entire_file_best_effort(const CHAR16 *name, void **out_buf, UINTN *out_len) {
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    if (!g_root || !name || !out_buf || !out_len) return EFI_INVALID_PARAMETER;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, name, &f, NULL, 0, L"read_entire");
    if (EFI_ERROR(st) || !f) return st;

    // Get file size
    UINT64 file_size = 0;
    {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, NULL);
        if (s2 == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            s2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(s2) && info) {
                s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(s2)) file_size = info->FileSize;
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }

    if (file_size == 0) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_END_OF_FILE;
    }
    if (file_size > 1024 * 1024) {
        // Safety cap (1 MiB)
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_OUT_OF_RESOURCES;
    }

    void *buf = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)file_size, (void **)&buf);
    if (EFI_ERROR(st) || !buf) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN nb = (UINTN)file_size;
    st = uefi_call_wrapper(f->Read, 3, f, &nb, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || nb != (UINTN)file_size) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return EFI_LOAD_ERROR;
    }

    *out_buf = buf;
    *out_len = nb;
    return EFI_SUCCESS;
}

// ============================================================================
// OO POLICY GATE (optional)
//
// Preferred: compiled policy blob "OOPOLICY.BIN" (produced by OS-G host tool).
// Fallback: a D+ policy file ("policy.dplus" or legacy "oo-policy.dplus") on FAT root,
// which can allow/deny /oo* commands.
//
// Compiled blob format (fixed-size):
//   - magic: "OOPL" (4 bytes)
//   - version: 1 (1 byte)
//   - allow_by_default: 0/1 (1 byte)
//   - allow_count: u8 (1 byte)
//   - deny_count: u8 (1 byte)
//   - reserved: 8 bytes
//   - allow table: 32 entries * 64 bytes (NUL-terminated strings)
//   - deny  table: 32 entries * 64 bytes
//
// Supported formats (line-based; best-effort):
//   mode=deny_by_default|allow_by_default
//   allow=/oo_new
//   deny=/oo_exec
//   @@ALLOW /oo_new
//   @@DENY  /oo_exec
//
// Matching:
//   - Entries are case-insensitive.
//   - If an entry ends with '*', it is treated as a prefix match.
//
// Behavior:
//   - If the file is missing or contains no actionable keys, policy is inactive.
//   - If active and mode is omitted, defaults to deny_by_default.
// ============================================================================

static int g_oo_policy_state = 0; // 0=untried, 1=active, -1=inactive/missing
static int g_oo_policy_allow_by_default = 0;
static int g_oo_policy_allow_count = 0;
static int g_oo_policy_deny_count = 0;
static char g_oo_policy_allow[32][64]; // SAFE: bounded allow-list table (<=32 rules, each <=63 bytes + NUL)
static char g_oo_policy_deny[32][64];  // SAFE: bounded deny-list table (<=32 rules, each <=63 bytes + NUL)
static char g_oo_policy_loaded_name[32]; // SAFE: short policy filename (e.g., "policy.dplus") for diagnostics

static int llmk_oo_policy_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char llmk_oo_policy_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int llmk_oo_policy_startswith_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        char a = llmk_oo_policy_tolower(*s);
        char b = llmk_oo_policy_tolower(*prefix);
        if (a != b) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static void llmk_oo_policy_trim(char **s) {
    if (!s || !*s) return;
    char *p = *s;
    while (llmk_oo_policy_is_space(*p)) p++;
    *s = p;
    char *end = p;
    while (*end) end++;
    while (end > p && llmk_oo_policy_is_space(end[-1])) end--;
    *end = 0;
}

static void llmk_oo_policy_store_lower_cap(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int n = 0;
    while (src[n] && n + 1 < cap) {
        dst[n] = llmk_oo_policy_tolower(src[n]);
        n++;
    }
    dst[n] = 0;
}

static void llmk_oo_policy_add_rule(int is_allow, const char *val) {
    if (!val || !val[0]) return;
    if (is_allow) {
        if (g_oo_policy_allow_count >= (int)(sizeof(g_oo_policy_allow) / sizeof(g_oo_policy_allow[0]))) return;
        llmk_oo_policy_store_lower_cap(g_oo_policy_allow[g_oo_policy_allow_count], (int)sizeof(g_oo_policy_allow[0]), val);
        if (g_oo_policy_allow[g_oo_policy_allow_count][0]) g_oo_policy_allow_count++;
    } else {
        if (g_oo_policy_deny_count >= (int)(sizeof(g_oo_policy_deny) / sizeof(g_oo_policy_deny[0]))) return;
        llmk_oo_policy_store_lower_cap(g_oo_policy_deny[g_oo_policy_deny_count], (int)sizeof(g_oo_policy_deny[0]), val);
        if (g_oo_policy_deny[g_oo_policy_deny_count][0]) g_oo_policy_deny_count++;
    }
}

static UINT32 llmk_crc32_zlib(const void *data, UINTN len) {
    // CRC-32 (zlib/PKZIP) polynomial, reflected.
    const UINT8 *p = (const UINT8 *)data;
    UINT32 crc = 0xFFFFFFFFu;
    for (UINTN i = 0; i < len; i++) {
        crc ^= (UINT32)p[i];
        for (int b = 0; b < 8; b++) {
            UINT32 m = (UINT32)-(INT32)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & m);
        }
    }
    return ~crc;
}

static int llmk_parse_crc32_hex_from_text(const char *s, UINTN n, UINT32 *out_crc) {
    if (!s || !out_crc) return 0;
    // Find a 0x???????? pattern and parse it.
    for (UINTN i = 0; i + 10 <= n; i++) {
        if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            UINT32 v = 0;
            int ok = 1;
            for (UINTN k = 0; k < 8; k++) {
                char c = s[i + 2 + k];
                UINT32 d = 0;
                if (c >= '0' && c <= '9') d = (UINT32)(c - '0');
                else if (c >= 'a' && c <= 'f') d = 10u + (UINT32)(c - 'a');
                else if (c >= 'A' && c <= 'F') d = 10u + (UINT32)(c - 'A');
                else { ok = 0; break; }
                v = (v << 4) | d;
            }
            if (ok) {
                *out_crc = v;
                return 1;
            }
        }
    }
    return 0;
}

static int llmk_oo_policy_match(const char *rule, const char *cmd) {
    if (!rule || !cmd) return 0;
    int rl = 0;
    while (rule[rl]) rl++;
    if (rl <= 0) return 0;
    if (rule[rl - 1] == '*') {
        // Prefix match
        int pl = rl - 1;
        for (int i = 0; i < pl; i++) {
            if (cmd[i] == 0) return 0;
            if (llmk_oo_policy_tolower(cmd[i]) != llmk_oo_policy_tolower(rule[i])) return 0;
        }
        return 1;
    }
    // Exact match
    int i = 0;
    while (rule[i] && cmd[i]) {
        if (llmk_oo_policy_tolower(rule[i]) != llmk_oo_policy_tolower(cmd[i])) return 0;
        i++;
    }
    return (rule[i] == 0 && cmd[i] == 0);
}

static void llmk_oo_policy_try_load_once(void) {
    if (g_oo_policy_state != 0) return;
    g_oo_policy_state = -1; // assume inactive unless proven otherwise
    g_oo_policy_allow_by_default = 0;
    g_oo_policy_allow_count = 0;
    g_oo_policy_deny_count = 0;
    g_oo_policy_loaded_name[0] = 0;

    // Prefer compiled blob if present (Option A). Fail-closed if present but invalid.
    {
        void *bin = NULL;
        UINTN bin_len = 0;
        EFI_STATUS sb = llmk_read_entire_file_best_effort(L"OOPOLICY.BIN", &bin, &bin_len);
        if (!EFI_ERROR(sb) && bin && bin_len) {
            const UINTN header_len = 16;
            const UINTN max_rules = 32;
            const UINTN rule_cap = 64;
            const UINTN min_len = header_len + (max_rules * rule_cap * 2);
            const unsigned char *b = (const unsigned char *)bin;

            int ok = 1;
            if (bin_len < min_len) ok = 0;
            if (ok) {
                if (!(b[0] == 'O' && b[1] == 'O' && b[2] == 'P' && b[3] == 'L')) ok = 0;
            }
            if (ok) {
                if (b[4] != 1) ok = 0;
            }

            // Notary (optional): verify integrity against OOPOLICY.CRC if present.
            // If CRC file exists but is malformed or mismatched, fail-closed.
            {
                void *crc_txt = NULL;
                UINTN crc_len = 0;
                EFI_STATUS sc = llmk_read_entire_file_best_effort(L"OOPOLICY.CRC", &crc_txt, &crc_len);
                if (!EFI_ERROR(sc) && crc_txt && crc_len) {
                    UINT32 expected = 0;
                    if (!llmk_parse_crc32_hex_from_text((const char *)crc_txt, crc_len, &expected)) {
                        ok = 0;
                        Print(L"ERROR: OOPOLICY.CRC invalid; refusing OOPOLICY.BIN\r\n");
                    } else {
                        UINT32 actual = llmk_crc32_zlib(bin, bin_len);
                        if (actual != expected) {
                            ok = 0;
                            Print(L"ERROR: OOPOLICY.BIN CRC mismatch (expected=0x%08x actual=0x%08x)\r\n", expected, actual);
                        } else {
                            Print(L"OK: OOPOLICY.BIN integrity verified (crc32=0x%08x)\r\n", actual);
                        }
                    }
                }
                if (crc_txt) uefi_call_wrapper(BS->FreePool, 1, crc_txt);
            }

            llmk_oo_policy_store_lower_cap(g_oo_policy_loaded_name, (int)sizeof(g_oo_policy_loaded_name), "OOPOLICY.BIN");

            if (!ok) {
                // Active policy, deny-by-default, no allow rules => denies all /oo*.
                g_oo_policy_allow_by_default = 0;
                g_oo_policy_allow_count = 0;
                g_oo_policy_deny_count = 0;
                g_oo_policy_state = 1;
                uefi_call_wrapper(BS->FreePool, 1, bin);
                return;
            }

            g_oo_policy_allow_by_default = (b[5] ? 1 : 0); // SAFE: OOPOLICY.BIN header validated; bin_len checked against min_len
            int allow_n = (int)b[6];
            int deny_n = (int)b[7];
            if (allow_n < 0) allow_n = 0;
            if (deny_n < 0) deny_n = 0;
            if (allow_n > (int)max_rules) allow_n = (int)max_rules;
            if (deny_n > (int)max_rules) deny_n = (int)max_rules;

            // Reset counts then append rules using the normal bounded helper.
            g_oo_policy_allow_count = 0;
            g_oo_policy_deny_count = 0;

            const UINTN allow_off = header_len;
            const UINTN deny_off = header_len + (max_rules * rule_cap);

            for (int i = 0; i < allow_n; i++) {
                const unsigned char *src = b + allow_off + (UINTN)i * rule_cap;
                char tok[64]; // SAFE: fixed-size token; copied with bounds checks
                int tn = 0;
                while (tn + 1 < (int)sizeof(tok) && src[tn] != 0) {
                    tok[tn] = llmk_oo_policy_tolower((char)src[tn]);
                    tn++;
                }
                tok[tn] = 0;
                if (llmk_oo_policy_startswith_ci(tok, "/oo")) {
                    llmk_oo_policy_add_rule(1, tok);
                }
            }
            for (int i = 0; i < deny_n; i++) {
                const unsigned char *src = b + deny_off + (UINTN)i * rule_cap;
                char tok[64]; // SAFE: fixed-size token; copied with bounds checks
                int tn = 0;
                while (tn + 1 < (int)sizeof(tok) && src[tn] != 0) {
                    tok[tn] = llmk_oo_policy_tolower((char)src[tn]);
                    tn++;
                }
                tok[tn] = 0;
                if (llmk_oo_policy_startswith_ci(tok, "/oo")) {
                    llmk_oo_policy_add_rule(0, tok);
                }
            }

            g_oo_policy_state = 1;
            uefi_call_wrapper(BS->FreePool, 1, bin);
            return;
        }
        if (bin) uefi_call_wrapper(BS->FreePool, 1, bin);
    }

    void *raw = NULL;
    UINTN raw_len = 0;
    int is_primary_dplus = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"policy.dplus", &raw, &raw_len);
    if (!EFI_ERROR(st) && raw && raw_len) {
        llmk_oo_policy_store_lower_cap(g_oo_policy_loaded_name, (int)sizeof(g_oo_policy_loaded_name), "policy.dplus");
        is_primary_dplus = 1;
    } else {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        raw = NULL;
        raw_len = 0;
        st = llmk_read_entire_file_best_effort(L"oo-policy.dplus", &raw, &raw_len);
        if (EFI_ERROR(st) || !raw || raw_len == 0) {
            if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
            return;
        }
        llmk_oo_policy_store_lower_cap(g_oo_policy_loaded_name, (int)sizeof(g_oo_policy_loaded_name), "oo-policy.dplus");
    }

    // Copy to NUL-terminated buffer so we can split lines in-place.
    char *txt = NULL;
    EFI_STATUS s2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&txt);
    if (EFI_ERROR(s2) || !txt) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        return;
    }
    for (UINTN i = 0; i < raw_len; i++) txt[i] = ((const char *)raw)[i];
    txt[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    int mode_set = 0;
    int saw_any_atat = 0;
    int in_law = 0;
    int saw_law = 0;
    int saw_proof = 0;
    int saw_any_allow_deny = 0;

    // Strip UTF-8 BOM if present.
    if ((unsigned char)txt[0] == 0xEF && (unsigned char)txt[1] == 0xBB && (unsigned char)txt[2] == 0xBF) {
        for (UINTN i = 0; i + 3 <= raw_len; i++) txt[i] = txt[i + 3];
    }

    char *p = txt;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        llmk_oo_policy_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '/' && line[1] == '/') continue;

        if (line[0] == '@' && line[1] == '@') {
            saw_any_atat = 1;
            // Section markers
            if (llmk_oo_policy_startswith_ci(line, "@@law")) {
                in_law = 1;
                saw_law = 1;
                continue;
            }
            if (llmk_oo_policy_startswith_ci(line, "@@proof")) {
                in_law = 0;
                saw_proof = 1;
                continue;
            }
            // Any other section ends LAW block.
            in_law = 0;
        }

        // Inline comment (best-effort)
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_oo_policy_trim(&line);
        if (line[0] == 0) continue;

        // D+ style inside @@LAW: allow/deny <token>  (we only care about /oo* tokens)
        if (in_law) {
            if (llmk_oo_policy_startswith_ci(line, "allow")) {
                char *v = line + 5;
                llmk_oo_policy_trim(&v);
                // take first token
                char tok[80]; // SAFE: token buffer; parsed with bounds checks
                int tn = 0;
                while (v[tn] && !llmk_oo_policy_is_space(v[tn]) && tn + 1 < (int)sizeof(tok)) {
                    tok[tn] = v[tn];
                    tn++;
                }
                tok[tn] = 0;
                if (llmk_oo_policy_startswith_ci(tok, "/oo")) {
                    llmk_oo_policy_add_rule(1, tok);
                    saw_any_allow_deny = 1;
                }
                continue;
            }
            if (llmk_oo_policy_startswith_ci(line, "deny")) {
                char *v = line + 4;
                llmk_oo_policy_trim(&v);
                char tok[80]; // SAFE: token buffer; parsed with bounds checks
                int tn = 0;
                while (v[tn] && !llmk_oo_policy_is_space(v[tn]) && tn + 1 < (int)sizeof(tok)) {
                    tok[tn] = v[tn];
                    tn++;
                }
                tok[tn] = 0;
                if (llmk_oo_policy_startswith_ci(tok, "/oo")) {
                    llmk_oo_policy_add_rule(0, tok);
                    saw_any_allow_deny = 1;
                }
                continue;
            }
        }

        // @@ALLOW /cmd (legacy convenience; accepted even outside @@LAW)
        if (llmk_oo_policy_startswith_ci(line, "@@allow")) {
            char *v = line + 7;
            llmk_oo_policy_trim(&v);
            llmk_oo_policy_add_rule(1, v);
            saw_any_allow_deny = 1;
            continue;
        }
        if (llmk_oo_policy_startswith_ci(line, "@@deny")) {
            char *v = line + 6;
            llmk_oo_policy_trim(&v);
            llmk_oo_policy_add_rule(0, v);
            saw_any_allow_deny = 1;
            continue;
        }

        // key=value
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_oo_policy_trim(&key);
        llmk_oo_policy_trim(&val);
        if (key[0] == 0 || val[0] == 0) continue;

        if (llmk_oo_policy_startswith_ci(key, "mode")) {
            if (llmk_oo_policy_startswith_ci(val, "allow") || llmk_oo_policy_startswith_ci(val, "allow_by_default")) {
                g_oo_policy_allow_by_default = 1;
                mode_set = 1;
            } else if (llmk_oo_policy_startswith_ci(val, "deny") || llmk_oo_policy_startswith_ci(val, "deny_by_default")) {
                g_oo_policy_allow_by_default = 0;
                mode_set = 1;
            }
        } else if (llmk_oo_policy_startswith_ci(key, "allow")) {
            llmk_oo_policy_add_rule(1, val);
            saw_any_allow_deny = 1;
        } else if (llmk_oo_policy_startswith_ci(key, "deny")) {
            llmk_oo_policy_add_rule(0, val);
            saw_any_allow_deny = 1;
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, txt);

    // Decide mode: strict D+ when policy.dplus is used OR file contains @@ markers.
    int dplus_mode = (is_primary_dplus || saw_any_atat);

    if (dplus_mode) {
        // Hardened behavior: if policy file exists but is missing required structure/proof,
        // still activate in deny-by-default mode (fail-closed for /oo*).
        g_oo_policy_allow_by_default = 0;

        // For primary OS-G-style policy.dplus, require @@LAW and @@PROOF markers.
        // For oo-policy.dplus that "looks like D+" (has @@ markers), also require @@PROOF.
        if (!saw_law || !saw_proof) {
            // active, but with no allow rules => denies all /oo*
            g_oo_policy_allow_count = 0;
            g_oo_policy_deny_count = 0;
        }
        g_oo_policy_state = 1;
        return;
    }

    // Legacy mode (key=value / @@ALLOW/@@DENY): if file existed but contained no actionable keys, treat as inactive.
    if (!mode_set && !saw_any_allow_deny && g_oo_policy_allow_count == 0 && g_oo_policy_deny_count == 0) {
        g_oo_policy_state = -1;
        return;
    }
    if (!mode_set) g_oo_policy_allow_by_default = 0;
    g_oo_policy_state = 1;
}

static int llmk_oo_policy_is_allowed_cmd(const char *cmd_token_lower) {
    if (!cmd_token_lower || cmd_token_lower[0] == 0) return 1;
    llmk_oo_policy_try_load_once();
    if (g_oo_policy_state != 1) return 1; // inactive => allow

    // Deny rules win.
    for (int i = 0; i < g_oo_policy_deny_count; i++) {
        if (llmk_oo_policy_match(g_oo_policy_deny[i], cmd_token_lower)) return 0;
    }

    for (int i = 0; i < g_oo_policy_allow_count; i++) {
        if (llmk_oo_policy_match(g_oo_policy_allow[i], cmd_token_lower)) return 1;
    }

    return g_oo_policy_allow_by_default ? 1 : 0;
}

static void llmk_oo_policy_warn_deny_cmd(const char *cmd_token_lower) {
    if (!cmd_token_lower) cmd_token_lower = "";
    llmk_oo_policy_try_load_once();

    Print(L"\r\n[policy] DENY: ");
    llmk_print_ascii(cmd_token_lower);
    if (g_oo_policy_loaded_name[0]) {
        Print(L" (");
        llmk_print_ascii(g_oo_policy_loaded_name);
        Print(L")\r\n\r\n");
    } else {
        Print(L" (policy)\r\n\r\n");
    }
}

static int llmk_oo_policy_check_prompt_and_warn(const char *prompt) {
    if (!prompt || prompt[0] != '/') return 1;

    // Extract command token: '/cmd' until space or ';'
    char cmd[64];
    int n = 0;
    while (prompt[n] && !llmk_oo_policy_is_space(prompt[n]) && prompt[n] != ';' && n + 1 < (int)sizeof(cmd)) {
        cmd[n] = llmk_oo_policy_tolower(prompt[n]);
        n++;
    }
    cmd[n] = 0;

    if (!llmk_oo_policy_startswith_ci(cmd, "/oo")) return 1;

    if (!llmk_oo_policy_is_allowed_cmd(cmd)) {
        llmk_oo_policy_warn_deny_cmd(cmd);
        return 0;
    }
    return 1;
}

static void llmk_make_bak_name(const CHAR16 *src, CHAR16 *dst, int dst_cap) {
    if (!dst || dst_cap <= 0) return;
    dst[0] = 0;
    if (!src) return;

    int n = 0;
    while (src[n] && n + 1 < dst_cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;

    const CHAR16 *suffix = L".bak";
    int s = 0;
    while (suffix[s]) s++;
    if (n + s + 1 >= dst_cap) return;
    for (int i = 0; i < s; i++) dst[n + i] = suffix[i];
    dst[n + s] = 0;
}

static EFI_STATUS llmk_copy_file_best_effort(const CHAR16 *src, const CHAR16 *dst) {
    if (!src || !dst) return EFI_INVALID_PARAMETER;
    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(src, &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return st;
    }

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, dst);
    if (EFI_ERROR(st) || !f) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return st;
    }

    st = llmk_file_write_bytes(f, (const void *)buf, len);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_delete_file_best_effort(const CHAR16 *name) {
    if (!g_root || !name) return EFI_INVALID_PARAMETER;
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(st) || !f) return st;
    // Delete closes the handle.
    return uefi_call_wrapper(f->Delete, 1, f);
}

static EFI_STATUS llmk_open_binary_file_append(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st) || !f) return st;

    // Seek to end
    UINT64 file_size = 0;
    {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, NULL);
        if (s2 == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            s2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(s2) && info) {
                s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(s2)) file_size = info->FileSize;
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }
    uefi_call_wrapper(f->SetPosition, 2, f, file_size);
    *out = f;
    return EFI_SUCCESS;
}

// ============================================================================
// OPERATING ORGANISM (OO) - v0 STATE + JOURNAL (best-effort)
// ============================================================================

#define LLMK_OO_STATE_MAGIC 0x54534F4Fu  // 'OOST' little-endian
#define LLMK_OO_STATE_VER   1u

enum {
    LLMK_OO_MODE_NORMAL   = 0,
    LLMK_OO_MODE_DEGRADED = 1,
    LLMK_OO_MODE_SAFE     = 2,
};

static UINT32 g_oo_last_mode = LLMK_OO_MODE_SAFE;
static int g_oo_last_mode_valid = 0;

// flags layout (packed counters; keep state struct fixed-size for v1)
//   bits  0..7   : consecutive recoveries (0-255)
//   bits  8..15  : consecutive stable boots (0-255)
//   bits 16..23  : last auto-apply action meta (low6=action_id, high2=apply_mode)
//   bits 24..31  : last auto-apply boot_count low8 (for next-boot metrics)
#define LLMK_OO_FLAGS_RC_MASK   0x000000FFu
#define LLMK_OO_FLAGS_SC_MASK   0x0000FF00u
#define LLMK_OO_FLAGS_SC_SHIFT  8u

#define LLMK_OO_FLAGS_LAST_ACTION_META_MASK   0x00FF0000u
#define LLMK_OO_FLAGS_LAST_ACTION_META_SHIFT  16u

#define LLMK_OO_FLAGS_LAST_APPLY_BOOT_MASK    0xFF000000u
#define LLMK_OO_FLAGS_LAST_APPLY_BOOT_SHIFT   24u

enum {
    LLMK_OO_ACTION_NONE       = 0,
    LLMK_OO_ACTION_REDUCE_CTX = 1,
    LLMK_OO_ACTION_REDUCE_SEQ = 2,
    LLMK_OO_ACTION_INCREASE_CTX = 3,
};

static UINT32 llmk_oo_get_rc(UINT32 flags) { return (flags & LLMK_OO_FLAGS_RC_MASK); }
static UINT32 llmk_oo_get_sc(UINT32 flags) { return (flags & LLMK_OO_FLAGS_SC_MASK) >> LLMK_OO_FLAGS_SC_SHIFT; }
static UINT32 llmk_oo_get_last_action_meta(UINT32 flags) {
    return (flags & LLMK_OO_FLAGS_LAST_ACTION_META_MASK) >> LLMK_OO_FLAGS_LAST_ACTION_META_SHIFT;
}
static UINT32 llmk_oo_get_last_apply_boot_low8(UINT32 flags) {
    return (flags & LLMK_OO_FLAGS_LAST_APPLY_BOOT_MASK) >> LLMK_OO_FLAGS_LAST_APPLY_BOOT_SHIFT;
}
static UINT32 llmk_oo_set_rc(UINT32 flags, UINT32 rc) {
    flags &= ~LLMK_OO_FLAGS_RC_MASK;
    flags |= (rc & 0xFFu);
    return flags;
}
static UINT32 llmk_oo_set_sc(UINT32 flags, UINT32 sc) {
    flags &= ~LLMK_OO_FLAGS_SC_MASK;
    flags |= ((sc & 0xFFu) << LLMK_OO_FLAGS_SC_SHIFT);
    return flags;
}
static UINT32 llmk_oo_set_last_action_meta(UINT32 flags, UINT32 meta) {
    flags &= ~LLMK_OO_FLAGS_LAST_ACTION_META_MASK;
    flags |= ((meta & 0xFFu) << LLMK_OO_FLAGS_LAST_ACTION_META_SHIFT);
    return flags;
}
static UINT32 llmk_oo_set_last_apply_boot_low8(UINT32 flags, UINT32 b) {
    flags &= ~LLMK_OO_FLAGS_LAST_APPLY_BOOT_MASK;
    flags |= ((b & 0xFFu) << LLMK_OO_FLAGS_LAST_APPLY_BOOT_SHIFT);
    return flags;
}

static const char *llmk_oo_action_name(UINT32 action_id) {
    switch (action_id) {
        case LLMK_OO_ACTION_REDUCE_CTX: return "reduce_ctx";
        case LLMK_OO_ACTION_REDUCE_SEQ: return "reduce_seq";
        case LLMK_OO_ACTION_INCREASE_CTX: return "increase_ctx";
        default: return "none";
    }
}

static int llmk_oo_action_is_reduction(UINT32 action_id) {
    return (action_id == LLMK_OO_ACTION_REDUCE_CTX || action_id == LLMK_OO_ACTION_REDUCE_SEQ) ? 1 : 0;
}

static int llmk_oo_action_is_increase(UINT32 action_id) {
    return (action_id == LLMK_OO_ACTION_INCREASE_CTX) ? 1 : 0;
}

// Forward decl (defined later in file).
static char *my_strstr(const char* haystack, const char* needle);

typedef struct {
    UINT32 magic;
    UINT32 version;
    UINT32 checksum; // FNV-1a over the struct with checksum=0
    UINT32 size;
    UINT64 boot_count;
    UINT32 mode;
    UINT32 flags;
} LlmkOoState;

static UINT32 llmk_fnv1a32(const void *data, UINTN len) {
    const UINT8 *p = (const UINT8 *)data;
    UINT32 h = 2166136261u;
    for (UINTN i = 0; i < len; i++) {
        h ^= (UINT32)p[i];
        h *= 16777619u;
    }
    return h;
}

static UINT32 llmk_oo_state_checksum(const LlmkOoState *s) {
    if (!s) return 0;
    LlmkOoState tmp = *s;
    tmp.checksum = 0;
    return llmk_fnv1a32(&tmp, (UINTN)sizeof(tmp));
}

static int llmk_oo_load_state_from_file_best_effort(const CHAR16 *name, LlmkOoState *out) {
    if (!out) return 0;
    out->magic = LLMK_OO_STATE_MAGIC;
    out->version = LLMK_OO_STATE_VER;
    out->checksum = 0;
    out->size = (UINT32)sizeof(LlmkOoState);
    out->boot_count = 0;
    out->mode = LLMK_OO_MODE_SAFE;
    out->flags = 0;
    if (!g_root) return 0;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(name, &buf, &len);
    if (EFI_ERROR(st) || !buf || len < (UINTN)sizeof(LlmkOoState)) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }

    LlmkOoState s;
    // Copy as raw bytes (avoid alignment assumptions).
    UINT8 *dst = (UINT8 *)&s;
    UINT8 *src = (UINT8 *)buf;
    for (UINTN i = 0; i < (UINTN)sizeof(LlmkOoState); i++) dst[i] = src[i];
    uefi_call_wrapper(BS->FreePool, 1, buf);

    if (s.magic != LLMK_OO_STATE_MAGIC) return 0;
    if (s.version != LLMK_OO_STATE_VER) return 0;
    if (s.size != (UINT32)sizeof(LlmkOoState)) return 0;
    UINT32 want = llmk_oo_state_checksum(&s);
    if (want == 0 || want != s.checksum) return 0;

    *out = s;
    return 1;
}

static int llmk_oo_load_state_best_effort(LlmkOoState *out) {
    return llmk_oo_load_state_from_file_best_effort(L"OOSTATE.BIN", out);
}

static int llmk_oo_load_recovery_best_effort(LlmkOoState *out) {
    return llmk_oo_load_state_from_file_best_effort(L"OORECOV.BIN", out);
}

static const CHAR16 *llmk_oo_mode_name(UINT32 mode) {
    switch (mode) {
        case LLMK_OO_MODE_NORMAL: return L"NORMAL";
        case LLMK_OO_MODE_DEGRADED: return L"DEGRADED";
        case LLMK_OO_MODE_SAFE: return L"SAFE";
        default: return L"UNKNOWN";
    }
}

static void llmk_ascii_append_char(char *buf, int cap, int *io_p, char c) {
    if (!buf || cap <= 0 || !io_p) return;
    int p = *io_p;
    if (p < 0) p = 0;
    if (p + 1 >= cap) return;
    buf[p++] = c;
    buf[p] = 0;
    *io_p = p;
}

static void llmk_ascii_append_str(char *buf, int cap, int *io_p, const char *s) {
    if (!buf || cap <= 0 || !io_p || !s) return;
    for (int i = 0; s[i]; i++) {
        llmk_ascii_append_char(buf, cap, io_p, s[i]);
    }
}

static void llmk_ascii_append_u64(char *buf, int cap, int *io_p, UINT64 v) {
    if (!buf || cap <= 0 || !io_p) return;
    char tmp[32]; // SAFE: enough for UINT64 decimal digits; bounded by sizeof(tmp)
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    // reverse
    for (int i = n - 1; i >= 0; i--) {
        llmk_ascii_append_char(buf, cap, io_p, tmp[i]);
    }
}

static EFI_STATUS llmk_oo_write_state_best_effort(const LlmkOoState *s) {
    if (!s || !g_root) return EFI_NOT_READY;
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, L"OOSTATE.BIN");
    if (EFI_ERROR(st) || !f) return st;

    UINTN nb = (UINTN)sizeof(LlmkOoState);
    st = uefi_call_wrapper(f->Write, 3, f, &nb, (void *)s);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || nb != (UINTN)sizeof(LlmkOoState)) return EFI_LOAD_ERROR;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_oo_write_recovery_best_effort(const LlmkOoState *s) {
    if (!s || !g_root) return EFI_NOT_READY;
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, L"OORECOV.BIN");
    if (EFI_ERROR(st) || !f) return st;

    UINTN nb = (UINTN)sizeof(LlmkOoState);
    st = uefi_call_wrapper(f->Write, 3, f, &nb, (void *)s);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || nb != (UINTN)sizeof(LlmkOoState)) return EFI_LOAD_ERROR;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static void llmk_oo_jour_log_rotate_best_effort(void);

static void llmk_oo_outcome_log_rotate_best_effort(void) {
    if (!g_root) return;

    const UINTN max_bytes = 256 * 1024;
    const UINTN keep_bytes = 128 * 1024;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    if (len <= max_bytes) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    UINTN keep = keep_bytes;
    if (keep >= len) keep = len;
    UINTN start = len - keep;

    char *cbuf = (char *)buf;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"OOOUTCOME.LOG");
    if (!EFI_ERROR(st) && f) {
        UINTN nb = len - start;
        (void)llmk_file_write_bytes(f, (const void *)(cbuf + start), nb);
        (void)uefi_call_wrapper(f->Flush, 1, f);
        uefi_call_wrapper(f->Close, 1, f);
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_outcome_copy_token(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    int p = 0;
    dst[0] = 0;
    if (!src) return;
    for (int i = 0; src[i] && p + 1 < cap; i++) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == ':') {
            dst[p++] = c;
        } else {
            dst[p++] = '_';
        }
    }
    dst[p] = 0;
}

static int llmk_oo_outcome_extract_token(const char *line, const char *key, char *out, int out_cap) {
    if (!line || !key || !out || out_cap <= 0) return 0;
    out[0] = 0;
    char needle[24]; // SAFE: fixed-size "key=" needle; built with explicit sizeof() bounds
    int np = 0;
    for (int i = 0; key[i] && np + 2 < (int)sizeof(needle); i++) needle[np++] = key[i];
    needle[np++] = '=';
    needle[np] = 0;

    char *hit = my_strstr(line, needle);
    if (!hit) return 0;
    hit += np;

    int p = 0;
    while (*hit && *hit != ' ' && *hit != '\r' && *hit != '\n' && p + 1 < out_cap) {
        out[p++] = *hit++;
    }
    out[p] = 0;
    return (p > 0) ? 1 : 0;
}

static int llmk_oo_outcome_parse_expected_value(const char *token, const char *prefix, int *out_value) {
    if (!token || !prefix || !out_value) return 0;
    int plen = 0;
    while (prefix[plen]) plen++;
    if (plen <= 0) return 0;
    for (int i = 0; i < plen; i++) {
        if (token[i] != prefix[i]) return 0;
    }
    int p = plen;
    int v = 0;
    int saw = 0;
    while (token[p] >= '0' && token[p] <= '9') {
        saw = 1;
        v = (v * 10) + (token[p] - '0');
        p++;
    }
    if (!saw || token[p] != 0) return 0;
    *out_value = v;
    return 1;
}

static int llmk_oo_outcome_read_latest_pending_expected(char *out_expected, int out_cap) {
    if (!out_expected || out_cap <= 0) return 0;
    out_expected[0] = 0;
    if (!g_root) return 0;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }

    char *cbuf = (char *)buf;
    UINTN start = (len > 8192) ? (len - 8192) : 0;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    char *p = cbuf + start;
    char *end = cbuf + len;
    int found = 0;
    while (p < end) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') {
            *p = 0;
            p++;
        }
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        char improved[8]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(improved)
        char expected[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(expected)
        if (!llmk_oo_outcome_extract_token(line, "i", improved, (int)sizeof(improved))) continue;
        if (!(improved[0] == '-' && improved[1] == '1' && improved[2] == 0)) continue;
        if (!llmk_oo_outcome_extract_token(line, "exp", expected, (int)sizeof(expected))) continue;
        llmk_oo_outcome_copy_token(out_expected, out_cap, expected);
        found = 1;
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    return found;
}

static void llmk_oo_outcome_append_best_effort(UINT64 boot_count,
                                                UINT32 action_id,
                                                const char *expected_effect,
                                                const char *observed_effect,
                                                int improved) {
    if (!g_root) return;
    if (!g_cfg_oo_enable) return;
    if (g_djibion.mode != DJIBION_MODE_OFF && !g_djibion.laws.allow_oo_persist) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file_append(&f, L"OOOUTCOME.LOG");
    if (EFI_ERROR(st) || !f) return;

    char expected_tok[48]; // SAFE: bounded token buffer; populated via llmk_oo_outcome_copy_token()
    char observed_tok[48]; // SAFE: bounded token buffer; populated via llmk_oo_outcome_copy_token()
    llmk_oo_outcome_copy_token(expected_tok, (int)sizeof(expected_tok), expected_effect ? expected_effect : "none");
    llmk_oo_outcome_copy_token(observed_tok, (int)sizeof(observed_tok), observed_effect ? observed_effect : "none");

    // Stable outcome line.
    // Format: out b=<boot> a=<action> i=<0|1|-1> exp=<token> obs=<token>
    char line[192];
    int p = 0;
    line[0] = 0;

    llmk_ascii_append_str(line, (int)sizeof(line), &p, "out b=");
    llmk_ascii_append_u64(line, (int)sizeof(line), &p, boot_count);
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " a=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, llmk_oo_action_name(action_id));
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " i=");
    if (improved < 0) {
        llmk_ascii_append_str(line, (int)sizeof(line), &p, "-1");
    } else {
        llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)improved);
    }
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " exp=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, expected_tok[0] ? expected_tok : "none");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " obs=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, observed_tok[0] ? observed_tok : "none");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "\r\n");

    UINTN nb = (UINTN)p;
    if (nb > 0) {
        uefi_call_wrapper(f->Write, 3, f, &nb, (void *)line);
        uefi_call_wrapper(f->Flush, 1, f);
    }
    uefi_call_wrapper(f->Close, 1, f);

    llmk_oo_outcome_log_rotate_best_effort();
}

static void llmk_oo_outcome_feedback_recent_best_effort(int *out_reduction_good,
                                                        int *out_reduction_bad,
                                                        int *out_increase_good,
                                                        int *out_increase_bad,
                                                        int *out_reduce_ctx_score,
                                                        int *out_reduce_seq_score,
                                                        int *out_increase_ctx_score) {
    if (out_reduction_good) *out_reduction_good = 0;
    if (out_reduction_bad) *out_reduction_bad = 0;
    if (out_increase_good) *out_increase_good = 0;
    if (out_increase_bad) *out_increase_bad = 0;
    if (out_reduce_ctx_score) *out_reduce_ctx_score = 0;
    if (out_reduce_seq_score) *out_reduce_seq_score = 0;
    if (out_increase_ctx_score) *out_increase_ctx_score = 0;
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = (len > 8192) ? (len - 8192) : 0;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    int considered = 0;
    int action_codes[16]; // SAFE: fixed small history window; indexed by considered < 16
    int improved_vals[16]; // SAFE: fixed small history window; indexed by considered < 16
    char *p = cbuf + start;
    char *end = cbuf + len;
    while (p < end && considered < 16) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') {
            *p = 0;
            p++;
        }

        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }
        char action[32]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(action)
        char imp[8]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(imp)
        if (!llmk_oo_outcome_extract_token(line, "a", action, (int)sizeof(action))) continue;
        if (!llmk_oo_outcome_extract_token(line, "i", imp, (int)sizeof(imp))) continue;
        if (imp[0] == '-') continue;

        int action_code = 0;
        if (my_strstr(action, "reduce_ctx")) action_code = 1;
        else if (my_strstr(action, "reduce_seq")) action_code = 2;
        else if (my_strstr(action, "increase_ctx")) action_code = 3;
        if (action_code == 0) continue;

        action_codes[considered] = action_code;
        improved_vals[considered] = (imp[0] == '1') ? 1 : 0;
        considered++;
    }

    for (int i = considered - 1, age = 0; i >= 0 && age < 8; i--, age++) {
        int weight = 4 - (age / 2);
        if (weight < 1) weight = 1;
        int improved = improved_vals[i];
        int action_code = action_codes[i];
        int signed_weight = improved ? weight : -weight;

        if (action_code == 1 || action_code == 2) {
            if (improved) {
                if (out_reduction_good) (*out_reduction_good)++;
            } else {
                if (out_reduction_bad) (*out_reduction_bad)++;
            }
            if (action_code == 1 && out_reduce_ctx_score) *out_reduce_ctx_score += signed_weight;
            if (action_code == 2 && out_reduce_seq_score) *out_reduce_seq_score += signed_weight;
        } else if (action_code == 3) {
            if (improved) {
                if (out_increase_good) (*out_increase_good)++;
            } else {
                if (out_increase_bad) (*out_increase_bad)++;
            }
            if (out_increase_ctx_score) *out_increase_ctx_score += signed_weight;
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static int llmk_oo_ascii_equals(const char *a, const char *b) {
    int i;
    if (!a || !b) return 0;
    for (i = 0; a[i] || b[i]; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int llmk_oo_read_latest_confirmed_outcome_best_effort(char *out_action,
                                                             int out_action_cap,
                                                             char *out_improved,
                                                             int out_improved_cap,
                                                             char *out_expected,
                                                             int out_expected_cap,
                                                             char *out_observed,
                                                             int out_observed_cap) {
    if (out_action && out_action_cap > 0) out_action[0] = 0;
    if (out_improved && out_improved_cap > 0) out_improved[0] = 0;
    if (out_expected && out_expected_cap > 0) out_expected[0] = 0;
    if (out_observed && out_observed_cap > 0) out_observed[0] = 0;
    if (!g_root) return 0;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }

    char *cbuf = (char *)buf;
    UINTN start = (len > 8192) ? (len - 8192) : 0;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    int found_confirmed = 0;
    char *p = cbuf + start;
    char *end = cbuf + len;
    while (p < end) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') {
            *p = 0;
            p++;
        }
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        char imp[8]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(imp)
        char action[32]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(action)
        char expected[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(expected)
        char observed[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(observed)
        if (!llmk_oo_outcome_extract_token(line, "i", imp, (int)sizeof(imp))) continue;
        if (imp[0] == '-' && imp[1] == '1' && imp[2] == 0) continue;
        if (!llmk_oo_outcome_extract_token(line, "a", action, (int)sizeof(action))) continue;
        if (!llmk_oo_outcome_extract_token(line, "exp", expected, (int)sizeof(expected))) continue;
        if (!llmk_oo_outcome_extract_token(line, "obs", observed, (int)sizeof(observed))) continue;

        if (out_action && out_action_cap > 0) llmk_oo_outcome_copy_token(out_action, out_action_cap, action);
        if (out_improved && out_improved_cap > 0) llmk_oo_outcome_copy_token(out_improved, out_improved_cap, imp);
        if (out_expected && out_expected_cap > 0) llmk_oo_outcome_copy_token(out_expected, out_expected_cap, expected);
        if (out_observed && out_observed_cap > 0) llmk_oo_outcome_copy_token(out_observed, out_observed_cap, observed);
        found_confirmed = 1;
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    return found_confirmed;
}

static int llmk_oo_boot_relation_for_action_best_effort(const char *selected_action,
                                                        char *out_relation,
                                                        int out_relation_cap,
                                                        char *out_last_action,
                                                        int out_last_action_cap,
                                                        char *out_last_improved,
                                                        int out_last_improved_cap,
                                                        char *out_last_expected,
                                                        int out_last_expected_cap,
                                                        char *out_last_observed,
                                                        int out_last_observed_cap) {
    char last_action[32]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    char last_improved[8]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    char last_expected[64]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    char last_observed[64]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()

    if (out_relation && out_relation_cap > 0) out_relation[0] = 0;
    if (out_last_action && out_last_action_cap > 0) out_last_action[0] = 0;
    if (out_last_improved && out_last_improved_cap > 0) out_last_improved[0] = 0;
    if (out_last_expected && out_last_expected_cap > 0) out_last_expected[0] = 0;
    if (out_last_observed && out_last_observed_cap > 0) out_last_observed[0] = 0;
    if (!selected_action || !selected_action[0]) return 0;

    last_action[0] = 0;
    last_improved[0] = 0;
    last_expected[0] = 0;
    last_observed[0] = 0;

    if (!llmk_oo_read_latest_confirmed_outcome_best_effort(last_action, (int)sizeof(last_action),
                                                           last_improved, (int)sizeof(last_improved),
                                                           last_expected, (int)sizeof(last_expected),
                                                           last_observed, (int)sizeof(last_observed))) {
        return 0;
    }

    if (out_last_action && out_last_action_cap > 0) llmk_oo_outcome_copy_token(out_last_action, out_last_action_cap, last_action);
    if (out_last_improved && out_last_improved_cap > 0) llmk_oo_outcome_copy_token(out_last_improved, out_last_improved_cap, last_improved);
    if (out_last_expected && out_last_expected_cap > 0) llmk_oo_outcome_copy_token(out_last_expected, out_last_expected_cap, last_expected);
    if (out_last_observed && out_last_observed_cap > 0) llmk_oo_outcome_copy_token(out_last_observed, out_last_observed_cap, last_observed);

    if (llmk_oo_ascii_equals(selected_action, last_action)) {
        const char *relation = (last_improved[0] == '1' && last_improved[1] == 0)
            ? "selected_matches_confirmed_good"
            : "selected_matches_confirmed_bad";
        if (out_relation && out_relation_cap > 0) llmk_oo_outcome_copy_token(out_relation, out_relation_cap, relation);
        return (last_improved[0] == '1' && last_improved[1] == 0) ? 8 : -8;
    }

    if (out_relation && out_relation_cap > 0) {
        llmk_oo_outcome_copy_token(out_relation, out_relation_cap, "selected_differs_from_last_confirmed");
    }
    if (last_improved[0] == '0' && last_improved[1] == 0) return 3;
    return 0;
}

static int llmk_oo_recent_trend_for_action_best_effort(const char *selected_action,
                                                       char *out_trend,
                                                       int out_trend_cap) {
    if (out_trend && out_trend_cap > 0) out_trend[0] = 0;
    if (!selected_action || !selected_action[0] || !g_root) return 0;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }

    char actions[3][32]; // SAFE: fixed ring window (3); all writes use sizeof(actions[i])
    char improveds[3][8]; // SAFE: fixed ring window (3); all writes use sizeof(improveds[i])
    int found = 0;
    for (int i = 0; i < 3; i++) {
        actions[i][0] = 0;
        improveds[i][0] = 0;
    }

    char *cbuf = (char *)buf;
    UINTN start = (len > 8192) ? (len - 8192) : 0;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    char *p = cbuf + start;
    char *end = cbuf + len;
    while (p < end) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') {
            *p = 0;
            p++;
        }
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        char imp[8]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(imp)
        char action[32]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(action)
        if (!llmk_oo_outcome_extract_token(line, "i", imp, (int)sizeof(imp))) continue;
        if (imp[0] == '-' && imp[1] == '1' && imp[2] == 0) continue;
        if (!llmk_oo_outcome_extract_token(line, "a", action, (int)sizeof(action))) continue;

        if (found < 3) {
            llmk_oo_outcome_copy_token(actions[found], (int)sizeof(actions[found]), action);
            llmk_oo_outcome_copy_token(improveds[found], (int)sizeof(improveds[found]), imp);
            found++;
        } else {
            llmk_oo_outcome_copy_token(actions[0], (int)sizeof(actions[0]), actions[1]);
            llmk_oo_outcome_copy_token(improveds[0], (int)sizeof(improveds[0]), improveds[1]);
            llmk_oo_outcome_copy_token(actions[1], (int)sizeof(actions[1]), actions[2]);
            llmk_oo_outcome_copy_token(improveds[1], (int)sizeof(improveds[1]), improveds[2]);
            llmk_oo_outcome_copy_token(actions[2], (int)sizeof(actions[2]), action);
            llmk_oo_outcome_copy_token(improveds[2], (int)sizeof(improveds[2]), imp);
        }
    }
    uefi_call_wrapper(BS->FreePool, 1, buf);

    int trend_score = 0;
    int matched = 0;
    for (int i = found - 1, age = 0; i >= 0 && age < 3; i--, age++) {
        int weight = 3 - age;
        if (!llmk_oo_ascii_equals(actions[i], selected_action)) continue;
        matched++;
        trend_score += (improveds[i][0] == '1' && improveds[i][1] == 0) ? weight : -weight;
    }

    if (matched <= 0) {
        if (out_trend && out_trend_cap > 0) llmk_oo_outcome_copy_token(out_trend, out_trend_cap, "trend_recent_none");
        return 0;
    }
    if (trend_score > 0) {
        if (out_trend && out_trend_cap > 0) llmk_oo_outcome_copy_token(out_trend, out_trend_cap, "trend_recent_positive");
        return (trend_score >= 3) ? 4 : 2;
    }
    if (trend_score < 0) {
        if (out_trend && out_trend_cap > 0) llmk_oo_outcome_copy_token(out_trend, out_trend_cap, "trend_recent_negative");
        return (trend_score <= -3) ? -6 : -3;
    }

    if (out_trend && out_trend_cap > 0) llmk_oo_outcome_copy_token(out_trend, out_trend_cap, "trend_recent_mixed");
    return 0;
}

static int llmk_oo_saturation_bias_for_action_best_effort(const char *selected_action,
                                                          int ctx,
                                                          int seq,
                                                          char *out_state,
                                                          int out_state_cap) {
    if (out_state && out_state_cap > 0) out_state[0] = 0;
    if (!selected_action || !selected_action[0]) return 0;

    if (llmk_oo_ascii_equals(selected_action, "reduce_ctx")) {
        if (ctx <= 128) {
            if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "saturated_min");
            return -10;
        }
        if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "ready");
        return 0;
    }
    if (llmk_oo_ascii_equals(selected_action, "reduce_seq")) {
        if (seq <= 128) {
            if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "saturated_min");
            return -10;
        }
        if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "ready");
        return 0;
    }
    if (llmk_oo_ascii_equals(selected_action, "increase_ctx")) {
        if (ctx >= 2048) {
            if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "saturated_max");
            return -10;
        }
        if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "ready");
        return 0;
    }

    if (out_state && out_state_cap > 0) llmk_oo_outcome_copy_token(out_state, out_state_cap, "na");
    return 0;
}

static const char *llmk_oo_operator_summary_from_dynamics(int selected_kind,
                                                          int fallback_from_positive_saturated,
                                                          int boot_bias,
                                                          int trend_bias,
                                                          int saturation_bias,
                                                          int applied,
                                                          const char *reason_id) {
    if (applied) return "applied";
    if (fallback_from_positive_saturated) return "positive_but_saturated";
    if (reason_id && llmk_oo_ascii_equals(reason_id, "OO_BLOCK_PLAN_BUDGET")) return "deferred_by_plan";
    if (reason_id && llmk_oo_ascii_equals(reason_id, "OO_BLOCK_CONFIDENCE")) return "blocked_by_confidence";
    if (reason_id && llmk_oo_ascii_equals(reason_id, "OO_BLOCK_ALREADY_MIN")) return "saturated_min";
    if (reason_id && llmk_oo_ascii_equals(reason_id, "OO_BLOCK_ALREADY_MAX")) return "saturated_max";
    if (selected_kind == 0) return "no_selected_action";
    if (boot_bias > 0 && trend_bias > 0 && saturation_bias >= 0) return "positive_and_repeatable";
    if (boot_bias < 0 || trend_bias < 0) return "avoid_recently_bad";
    if (saturation_bias < 0) return "saturated";
    return "explore_alternative";
}

static void llmk_oo_print_recent_confirmed_outcomes_best_effort(int max_items) {
    if (max_items <= 0) return;
    if (max_items > 3) max_items = 3;
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain_boot] recent.count=0\r\n");
        return;
    }

    char actions[3][32]; // SAFE: fixed ring window (3); all writes use sizeof(actions[i])
    char improveds[3][8]; // SAFE: fixed ring window (3); all writes use sizeof(improveds[i])
    char expecteds[3][64]; // SAFE: fixed ring window (3); all writes use sizeof(expecteds[i])
    char observeds[3][64]; // SAFE: fixed ring window (3); all writes use sizeof(observeds[i])
    int found = 0;
    for (int i = 0; i < 3; i++) {
        actions[i][0] = 0;
        improveds[i][0] = 0;
        expecteds[i][0] = 0;
        observeds[i][0] = 0;
    }

    char *cbuf = (char *)buf;
    UINTN start = (len > 8192) ? (len - 8192) : 0;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    char *p = cbuf + start;
    char *end = cbuf + len;
    while (p < end) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') {
            *p = 0;
            p++;
        }
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        char imp[8]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(imp)
        char action[32]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(action)
        char expected[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(expected)
        char observed[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(observed)
        if (!llmk_oo_outcome_extract_token(line, "i", imp, (int)sizeof(imp))) continue;
        if (imp[0] == '-' && imp[1] == '1' && imp[2] == 0) continue;
        if (!llmk_oo_outcome_extract_token(line, "a", action, (int)sizeof(action))) continue;
        if (!llmk_oo_outcome_extract_token(line, "exp", expected, (int)sizeof(expected))) continue;
        if (!llmk_oo_outcome_extract_token(line, "obs", observed, (int)sizeof(observed))) continue;

        if (found < 3) {
            llmk_oo_outcome_copy_token(actions[found], (int)sizeof(actions[found]), action);
            llmk_oo_outcome_copy_token(improveds[found], (int)sizeof(improveds[found]), imp);
            llmk_oo_outcome_copy_token(expecteds[found], (int)sizeof(expecteds[found]), expected);
            llmk_oo_outcome_copy_token(observeds[found], (int)sizeof(observeds[found]), observed);
            found++;
        } else {
            llmk_oo_outcome_copy_token(actions[0], (int)sizeof(actions[0]), actions[1]);
            llmk_oo_outcome_copy_token(improveds[0], (int)sizeof(improveds[0]), improveds[1]);
            llmk_oo_outcome_copy_token(expecteds[0], (int)sizeof(expecteds[0]), expecteds[1]);
            llmk_oo_outcome_copy_token(observeds[0], (int)sizeof(observeds[0]), observeds[1]);
            llmk_oo_outcome_copy_token(actions[1], (int)sizeof(actions[1]), actions[2]);
            llmk_oo_outcome_copy_token(improveds[1], (int)sizeof(improveds[1]), improveds[2]);
            llmk_oo_outcome_copy_token(expecteds[1], (int)sizeof(expecteds[1]), expecteds[2]);
            llmk_oo_outcome_copy_token(observeds[1], (int)sizeof(observeds[1]), observeds[2]);
            llmk_oo_outcome_copy_token(actions[2], (int)sizeof(actions[2]), action);
            llmk_oo_outcome_copy_token(improveds[2], (int)sizeof(improveds[2]), imp);
            llmk_oo_outcome_copy_token(expecteds[2], (int)sizeof(expecteds[2]), expected);
            llmk_oo_outcome_copy_token(observeds[2], (int)sizeof(observeds[2]), observed);
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);

    if (found <= 0) {
        Print(L"[oo_explain_boot] recent.count=0\r\n");
        return;
    }

    int emit = (found < max_items) ? found : max_items;
    Print(L"[oo_explain_boot] recent.count=%d\r\n", emit);
    for (int i = 0; i < emit; i++) {
        int src = found - 1 - i;
        Print(L"[oo_explain_boot] recent[%d].action=%a improved=%a expected=%a observed=%a\r\n",
              i,
              actions[src][0] ? actions[src] : "na",
              improveds[src][0] ? improveds[src] : "na",
              expecteds[src][0] ? expecteds[src] : "na",
              observeds[src][0] ? observeds[src] : "na");
    }
}

static void llmk_oo_journal_append_best_effort(const LlmkOoState *s, const char *event) {
    if (!g_root || !s) return;
    if (!g_cfg_oo_enable) return;
    if (g_djibion.mode != DJIBION_MODE_OFF && !g_djibion.laws.allow_oo_persist) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file_append(&f, L"OOJOUR.LOG");
    if (EFI_ERROR(st) || !f) return;

    char line[192];
    int p = 0;
    line[0] = 0;
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "oo event=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, (event && event[0]) ? event : "boot");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "\r\n");

    UINTN nb = (UINTN)p;
    if (nb > 0) {
        uefi_call_wrapper(f->Write, 3, f, &nb, (void *)line);
        uefi_call_wrapper(f->Flush, 1, f);
    }
    uefi_call_wrapper(f->Close, 1, f);

    // Enforce a max size cap (best-effort; never blocks boot).
    // Keep only the newest part of the log (FIFO truncation).
    llmk_oo_jour_log_rotate_best_effort();
}

// Forward decl: used by early OO ticks (e.g. net tick) before full OO journaling helpers.
static void llmk_oo_journal_event_load_state_best_effort(const char *event);
static int llmk_repl_cfg_read_ctx_seq_best_effort(int *out_ctx, int *out_seq);

static int llmk_oo_consult_metrics_tick_best_effort(LlmkOoState *s, char *out_event, int out_cap) {
    if (out_event && out_cap > 0) out_event[0] = 0;
    if (!s || !g_cfg_oo_enable) return 0;

    UINT32 meta = llmk_oo_get_last_action_meta(s->flags);
    UINT32 apply_boot_low8 = llmk_oo_get_last_apply_boot_low8(s->flags);
    if (meta == 0 || apply_boot_low8 == 0) return 0;

    UINT32 action_id = (meta & 0x3Fu);
    UINT32 apply_mode = (meta >> 6u) & 0x3u;

    UINT32 curr_boot_low8 = (UINT32)(s->boot_count & 0xFFu);
    UINT32 want = (UINT32)((apply_boot_low8 + 1u) & 0xFFu);
    if (curr_boot_low8 != want) return 0;

    int improved = 0;
    char expected_effect[64]; // SAFE: bounded effect token; filled via bounded appends/extractors
    char observed_effect[64]; // SAFE: bounded effect token; filled via bounded appends/extractors
    expected_effect[0] = 0;
    observed_effect[0] = 0;

    if (!llmk_oo_outcome_read_latest_pending_expected(expected_effect, (int)sizeof(expected_effect))) {
        llmk_oo_outcome_copy_token(expected_effect, (int)sizeof(expected_effect),
                                   llmk_oo_action_is_increase(action_id) ? "mode_stable" : "mode_drop");
    }

    {
        int cfg_ctx = 0, cfg_seq = 0;
        int want_value = 0;
        int have_cfg = llmk_repl_cfg_read_ctx_seq_best_effort(&cfg_ctx, &cfg_seq);

        if (llmk_oo_outcome_parse_expected_value(expected_effect, "ctx_", &want_value) && have_cfg) {
            improved = (cfg_ctx == want_value) ? 1 : 0;
            if (improved) {
                llmk_oo_outcome_copy_token(observed_effect, (int)sizeof(observed_effect), expected_effect);
            } else {
                int p = 0;
                observed_effect[0] = 0;
                llmk_ascii_append_str(observed_effect, (int)sizeof(observed_effect), &p, "ctx_");
                llmk_ascii_append_u64(observed_effect, (int)sizeof(observed_effect), &p, (UINT64)((cfg_ctx > 0) ? cfg_ctx : 0));
                observed_effect[p] = 0;
            }
        } else if (llmk_oo_outcome_parse_expected_value(expected_effect, "seq_", &want_value) && have_cfg) {
            improved = (cfg_seq == want_value) ? 1 : 0;
            if (improved) {
                llmk_oo_outcome_copy_token(observed_effect, (int)sizeof(observed_effect), expected_effect);
            } else {
                int p = 0;
                observed_effect[0] = 0;
                llmk_ascii_append_str(observed_effect, (int)sizeof(observed_effect), &p, "seq_");
                llmk_ascii_append_u64(observed_effect, (int)sizeof(observed_effect), &p, (UINT64)((cfg_seq > 0) ? cfg_seq : 0));
                observed_effect[p] = 0;
            }
        } else if (expected_effect[0] == 'm' && expected_effect[1] == 'o' && expected_effect[2] == 'd' &&
                   expected_effect[3] == 'e' && expected_effect[4] == '_' && expected_effect[5] == 'd' && // SAFE: expected_effect is char[64]; fixed literal compare within bounds
                   expected_effect[6] == 'r' && expected_effect[7] == 'o' && expected_effect[8] == 'p' && // SAFE: expected_effect is char[64]; fixed literal compare within bounds
                   expected_effect[9] == 0) { // SAFE: expected_effect is char[64]; fixed literal compare within bounds
            improved = ((UINT32)s->mode < apply_mode) ? 1 : 0;
            llmk_oo_outcome_copy_token(observed_effect, (int)sizeof(observed_effect),
                                       improved ? "mode_improved" : "mode_not_improved");
        } else if (expected_effect[0] == 'm' && expected_effect[1] == 'o' && expected_effect[2] == 'd' &&
                   expected_effect[3] == 'e' && expected_effect[4] == '_' && expected_effect[5] == 's' && // SAFE: expected_effect is char[64]; fixed literal compare within bounds
                   expected_effect[6] == 't' && expected_effect[7] == 'a' && expected_effect[8] == 'b' && // SAFE: expected_effect is char[64]; fixed literal compare within bounds
                   expected_effect[9] == 'l' && expected_effect[10] == 'e' && expected_effect[11] == 0) { // SAFE: expected_effect is char[64]; fixed literal compare within bounds
            improved = ((UINT32)s->mode <= apply_mode) ? 1 : 0;
            llmk_oo_outcome_copy_token(observed_effect, (int)sizeof(observed_effect),
                                       improved ? "mode_stable_or_better" : "mode_regressed");
        } else {
            improved = ((UINT32)s->mode < apply_mode) ? 1 : 0;
            llmk_oo_outcome_copy_token(observed_effect, (int)sizeof(observed_effect),
                                       improved ? "mode_improved" : "mode_not_improved");
        }
    }

    Print(L"OK: OO consult metric: action=%a improved=%d expected=%a observed=%a\r\n",
          llmk_oo_action_name(action_id), improved, expected_effect, observed_effect);

    llmk_oo_outcome_append_best_effort((UINT64)s->boot_count,
                                       action_id,
                                       expected_effect,
                                       observed_effect,
                                       improved);

    if (out_event && out_cap > 0) {
        int p = 0;
        out_event[0] = 0;
        llmk_ascii_append_str(out_event, out_cap, &p, "consult_metric action=");
        llmk_ascii_append_str(out_event, out_cap, &p, llmk_oo_action_name(action_id));
        llmk_ascii_append_str(out_event, out_cap, &p, " improved=");
        llmk_ascii_append_u64(out_event, out_cap, &p, (UINT64)improved);
        llmk_ascii_append_str(out_event, out_cap, &p, " expected=");
        llmk_ascii_append_str(out_event, out_cap, &p, expected_effect);
        llmk_ascii_append_str(out_event, out_cap, &p, " observed=");
        llmk_ascii_append_str(out_event, out_cap, &p, observed_effect);
        out_event[p] = 0;
    }

    // Clear metadata so we only report once.
    s->flags = llmk_oo_set_last_action_meta(s->flags, 0);
    s->flags = llmk_oo_set_last_apply_boot_low8(s->flags, 0);
    return 1;
}

static void llmk_oo_boot_tick_best_effort(void) {
    if (!g_cfg_oo_enable) return;
    if (!g_root) return;

    LlmkOoState s;
    int ok_primary = llmk_oo_load_state_best_effort(&s);
    const char *event = "boot";

    if (!ok_primary) {
        // Try rollback state.
        LlmkOoState r;
        int ok_rec = llmk_oo_load_recovery_best_effort(&r);
        if (ok_rec) {
            s = r;
            event = "recover";
            // Enter safe mode on recovery.
            s.mode = LLMK_OO_MODE_SAFE;
            {
                UINT32 rc = llmk_oo_get_rc(s.flags);
                if (rc < 255u) rc++;
                s.flags = llmk_oo_set_rc(s.flags, rc);
                s.flags = llmk_oo_set_sc(s.flags, 0);
            }
            Print(L"[oo] RECOVERY: OOSTATE invalid; using OORECOV rollback\r\n");
        } else {
            // Fresh init in safe mode.
            // Defaults already set by loader.
            event = "init";
            s.mode = LLMK_OO_MODE_SAFE;
            {
                UINT32 rc = llmk_oo_get_rc(s.flags);
                if (rc < 255u) rc++;
                s.flags = llmk_oo_set_rc(s.flags, rc);
                s.flags = llmk_oo_set_sc(s.flags, 0);
            }
            Print(L"[oo] RECOVERY: state missing/invalid; initializing SAFE\r\n");
        }
    } else {
        // Stable boot (state valid).
        s.flags = llmk_oo_set_rc(s.flags, 0);
        {
            UINT32 sc = llmk_oo_get_sc(s.flags);
            if (sc < 255u) sc++;
            s.flags = llmk_oo_set_sc(s.flags, sc);
        }

        // Minimal deterministic mode transition policy.
        // SAFE -> DEGRADED after 2 stable boots; DEGRADED -> NORMAL after 2 more.
        {
            UINT32 sc = llmk_oo_get_sc(s.flags);
            if (s.mode == LLMK_OO_MODE_SAFE && sc >= 2u) {
                s.mode = LLMK_OO_MODE_DEGRADED;
                s.flags = llmk_oo_set_sc(s.flags, 0);
                event = "mode_degraded";
            } else if (s.mode == LLMK_OO_MODE_DEGRADED && sc >= 2u) {
                s.mode = LLMK_OO_MODE_NORMAL;
                s.flags = llmk_oo_set_sc(s.flags, 0);
                event = "mode_normal";
            }
        }
    }

    g_oo_last_mode = s.mode;
    g_oo_last_mode_valid = 1;

    s.boot_count++;

    // M5.4: If an auto-apply happened last boot, emit one-shot metrics now.
    char metric_event[96];
    int has_metric = llmk_oo_consult_metrics_tick_best_effort(&s, metric_event, (int)sizeof(metric_event));

    s.magic = LLMK_OO_STATE_MAGIC;
    s.version = LLMK_OO_STATE_VER;
    s.size = (UINT32)sizeof(LlmkOoState);
    s.checksum = llmk_oo_state_checksum(&s);

    EFI_STATUS wst = llmk_oo_write_state_best_effort(&s);
    if (!EFI_ERROR(wst)) {
        // Refresh rollback checkpoint.
        llmk_oo_write_recovery_best_effort(&s);
    }
    llmk_oo_journal_append_best_effort(&s, event);
    if (has_metric && metric_event[0]) {
        llmk_oo_journal_append_best_effort(&s, metric_event);
    }

    if (!EFI_ERROR(wst)) {
        Print(L"OK: OO boot_count=%lu mode=%s\r\n", (UINT64)s.boot_count, llmk_oo_mode_name(s.mode));
    } else {
        Print(L"[oo] WARN: state write failed: %r\r\n", wst);
    }
}

// ============================================================================
// OO M4 - Network Read-only Tick (placeholder)
// - Never required for boot
// - Best-effort: detect if SNP is present; do not perform IO yet
// - Emits deterministic serial markers and journal events
// ============================================================================

static void llmk_oo_net_tick_best_effort(void) {
    if (!g_cfg_oo_enable || !g_cfg_oo_net) return;
    if (!g_root || !BS) return;

    EFI_GUID SnpGuid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;

    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                                     ByProtocol,
                                     &SnpGuid,
                                     NULL,
                                     &count,
                                     &handles);

    const int available = (!EFI_ERROR(st) && handles && count > 0);
    if (!available) {
        Print(L"OK: OO net: unavailable\r\n");

        llmk_oo_journal_event_load_state_best_effort("net_unavailable");

        if (handles) uefi_call_wrapper(BS->FreePool, 1, handles);
        return;
    }

    // Present, but still placeholder (no DHCP/HTTP stack here yet).
    Print(L"OK: OO net: present\r\n");

    (void)count;
    llmk_oo_journal_event_load_state_best_effort("net_present");

    uefi_call_wrapper(BS->FreePool, 1, handles);
}

// OO M5: /oo_consult - LLM-based system health advisor (safety-first policy)
// Note: requires g_llmk_ready, weights loaded, etc. 
// Forward decl moved below after type definitions

static int llmk_char16_streq(const CHAR16 *a, const CHAR16 *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static void llmk_print_u64(UINT64 v) {
    // Print decimal without relying on format widths.
    CHAR16 buf[32]; // SAFE: enough for UINT64 decimal digits; bounded by sizeof(buf)
    int p = 0;
    if (v == 0) {
        buf[p++] = L'0';
    } else {
        CHAR16 rev[32]; // SAFE: enough for UINT64 decimal digits; bounded by sizeof(rev)
        int rn = 0;
        while (v > 0 && rn < (int)(sizeof(rev) / sizeof(rev[0]))) {
            rev[rn++] = (CHAR16)(L'0' + (v % 10));
            v /= 10;
        }
        while (rn > 0 && p + 1 < (int)(sizeof(buf) / sizeof(buf[0]))) buf[p++] = rev[--rn];
    }
    buf[p] = 0;
    Print(L"%s", buf);
}

static void llmk_fs_ls_best_effort(const CHAR16 *path, int max_entries) {
    if (!g_root) {
        Print(L"\r\nERROR: file system not ready\r\n\r\n");
        return;
    }
    if (max_entries <= 0) max_entries = 200;
    if (max_entries > 500) max_entries = 500;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!path || path[0] == 0 || llmk_char16_streq(path, L".") || llmk_char16_streq(path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, path, &dir, NULL, 0, L"ls_dir");
        if (EFI_ERROR(st) || !dir) {
            Print(L"\r\nERROR: cannot open %s: %r\r\n\r\n", (CHAR16 *)path, st);
            return;
        }
        close_dir = 1;
    }

    // Rewind
    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN printed = 0;
    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        Print(L"\r\nERROR: OOM\r\n\r\n");
        return;
    }

    while (printed < (UINTN)max_entries) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st)) {
            Print(L"\r\nERROR: ls read failed: %r\r\n\r\n", st);
            break;
        }
        if (sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        // Skip . and ..
        if (info->FileName && (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L".."))) {
            continue;
        }

        Print(L"  ");
        if (info->Attribute & EFI_FILE_DIRECTORY) {
            Print(L"<DIR> ");
        } else {
            Print(L"      ");
        }
        if (info->Attribute & EFI_FILE_DIRECTORY) {
            Print(L"      ");
        } else {
            llmk_print_u64(info->FileSize);
            // pad to ~10 chars (best-effort)
            Print(L" ");
        }
        Print(L" %s\r\n", info->FileName ? info->FileName : L"(null)");
        printed++;
    }

    if (printed == 0) {
        Print(L"  (empty)\r\n");
    }
    if (printed >= (UINTN)max_entries) {
        Print(L"  ... (truncated)\r\n");
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
}

static int llmk_is_model_file_name16(const CHAR16 *name) {
    if (!name || !name[0]) return 0;
    // tokenizer.bin is a required runtime asset, but it is not a model.
    if (llmk_char16_endswith_ci(name, L"tokenizer.bin")) return 0;
    // OO policy artifacts are not models (but are .BIN and may exist on no-model images).
    if (llmk_char16_endswith_ci(name, L"OOPOLICY.BIN")) return 0;
    // OO persistence / recovery files are not models, but can end up as .BIN.
    if (llmk_char16_endswith_ci(name, L"OOSTATE.BIN")) return 0;
    if (llmk_char16_endswith_ci(name, L"OORECOV.BIN")) return 0;
    // OOSI v2 SSM binaries: loaded via /ssm_load, not auto-selected as Llama2 models.
    if (llmk_char16_startswith_ci(name, L"oo_mamba")) return 0;
    if (llmk_char16_startswith_ci(name, L"oo_v")) return 0;
    if (llmk_char16_endswith_ci(name, L"_int8.bin")) return 0;
    if (llmk_char16_startswith_ci(name, L"oosi_")) return 0;
    if (llmk_char16_startswith_ci(name, L"oo_ssm")) return 0;
    if (llmk_char16_endswith_ci(name, L".bin")) return 1;
    if (llmk_char16_endswith_ci(name, L".gguf")) return 1;
    return 0;
}

static const CHAR16 *llmk_model_type_name16(const CHAR16 *name) {
    if (!name) return L"?";
    if (llmk_char16_endswith_ci(name, L".gguf")) return L"GGUF";
    if (llmk_char16_endswith_ci(name, L".bin")) return L"BIN";
    return L"?";
}

static int llmk_try_open_first_model_in_dir_best_effort(const CHAR16 *dir_path, EFI_FILE_HANDLE *out_f, CHAR16 *out_path, int out_cap) {
    if (out_f) *out_f = NULL;
    if (out_path && out_cap > 0) out_path[0] = 0;
    if (!g_root || !out_f || !out_path || out_cap <= 1) return 0;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, dir_path, &dir, NULL, 0, L"first_model_dir");
        if (EFI_ERROR(st) || !dir) {
            return 0;
        }
        close_dir = 1;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        return 0;
    }

    int found = 0;
    while (!found) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (!info->FileName) continue;
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        CHAR16 path[192];
        path[0] = 0;
        if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), info->FileName);
        } else {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), dir_path);
            // Ensure trailing backslash
            UINTN n = StrLen(path);
            if (n > 0 && path[n - 1] != L'\\' && n + 1 < (sizeof(path) / sizeof(path[0]))) {
                path[n] = L'\\';
                path[n + 1] = 0;
            }
            if (StrLen(path) + StrLen(info->FileName) + 1 < (sizeof(path) / sizeof(path[0]))) {
                StrCat(path, info->FileName);
            }
        }

        EFI_FILE_HANDLE f = NULL;
        CHAR16 picked[192];
        picked[0] = 0;
        EFI_STATUS fst = llmk_open_read_with_fat83_fallback(g_root, path, &f, picked,
                                                           (int)(sizeof(picked) / sizeof(picked[0])),
                                                           L"first_model");
        if (!EFI_ERROR(fst) && f) {
            *out_f = f;
            if (picked[0]) llmk_char16_copy_cap(out_path, out_cap, picked);
            else llmk_char16_copy_cap(out_path, out_cap, path);
            found = 1;
            break;
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
    return found;
}

// Forward declarations used by model picker.
void read_user_input(CHAR16* buffer, int max_len);
void char16_to_char(char* dest, CHAR16* src, int max_len);

typedef struct {
    CHAR16 path[192];
    UINT64 size;
} LlmkModelEntry;

static int llmk_collect_models_in_dir(const CHAR16 *dir_path, LlmkModelEntry *out, int cap) {
    if (!g_root || !out || cap <= 0) return 0;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;
    if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, dir_path, &dir, NULL, 0, L"collect_models_dir");
        if (EFI_ERROR(st) || !dir) return 0;
        close_dir = 1;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        return 0;
    }

    int count = 0;
    while (count < cap) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (!info->FileName) continue;
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        CHAR16 path[192];
        path[0] = 0;
        if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), info->FileName);
        } else {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), dir_path);
            UINTN n = StrLen(path);
            if (n > 0 && path[n - 1] != L'\\' && n + 1 < (sizeof(path) / sizeof(path[0]))) {
                path[n] = L'\\';
                path[n + 1] = 0;
            }
            if (StrLen(path) + StrLen(info->FileName) + 1 < (sizeof(path) / sizeof(path[0]))) {
                StrCat(path, info->FileName);
            }
        }

        llmk_char16_copy_cap(out[count].path, (int)(sizeof(out[count].path) / sizeof(out[count].path[0])), path);
        out[count].size = info->FileSize;
        count++;
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
    return count;
}

static int llmk_collect_models(LlmkModelEntry *out, int cap) {
    int n = 0;
    if (cap <= 0) return 0;
    n += llmk_collect_models_in_dir(NULL, out + n, cap - n);
    if (n < cap) {
        n += llmk_collect_models_in_dir(L"models", out + n, cap - n);
    }
    return n;
}

static int llmk_model_picker(EFI_FILE_HANDLE *out_f, CHAR16 *out_path, int out_cap) {
    if (out_f) *out_f = NULL;
    if (out_path && out_cap > 0) out_path[0] = 0;
    if (!g_root || !out_f || !out_path || out_cap <= 1) return 0;

    LlmkModelEntry entries[48]; // SAFE: fixed-cap model entry list; bounded by sizeof(entries)
    int n = llmk_collect_models(entries, (int)(sizeof(entries) / sizeof(entries[0])));
    if (n <= 0) return 0;

    if (n == 1) {
        EFI_FILE_HANDLE f = NULL;
        CHAR16 picked[192];
        picked[0] = 0;
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, entries[0].path, &f, picked,
                                                          (int)(sizeof(picked) / sizeof(picked[0])),
                                                          L"picker_one");
        if (EFI_ERROR(st) || !f) return 0;
        *out_f = f;
        if (picked[0]) llmk_char16_copy_cap(out_path, out_cap, picked);
        else llmk_char16_copy_cap(out_path, out_cap, entries[0].path);
        return 1;
    }

    Print(L"\r\nModel picker:\r\n");
    for (int i = 0; i < n; i++) {
        UINT64 mb = entries[i].size / (1024ULL * 1024ULL);
        Print(L"  %d) %s  (%lu MB)\r\n", i + 1, entries[i].path, mb);
    }
    Print(L"  0) cancel\r\n\r\n");

    CHAR16 input16[64];
    char input8[64];
    Print(L"Select model number: ");
    read_user_input(input16, (int)(sizeof(input16) / sizeof(input16[0])));
    char16_to_char(input8, input16, (int)sizeof(input8));

    int sel = 0;
    int i = 0;
    while (input8[i] == ' ' || input8[i] == '\t') i++;
    while (input8[i] >= '0' && input8[i] <= '9') {
        sel = sel * 10 + (input8[i] - '0');
        i++;
    }
    if (sel <= 0 || sel > n) {
        Print(L"\r\nModel picker canceled.\r\n\r\n");
        return 0;
    }

    int idx = sel - 1;
    EFI_FILE_HANDLE f = NULL;
    CHAR16 picked[192];
    picked[0] = 0;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, entries[idx].path, &f, picked,
                                                      (int)(sizeof(picked) / sizeof(picked[0])),
                                                      L"picker_sel");
    if (EFI_ERROR(st) || !f) {
        Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", entries[idx].path, st);
        return 0;
    }
    *out_f = f;
    if (picked[0]) llmk_char16_copy_cap(out_path, out_cap, picked);
    else llmk_char16_copy_cap(out_path, out_cap, entries[idx].path);
    return 1;
}

static int llmk_try_open_first_model_best_effort(EFI_FILE_HANDLE *out_f, CHAR16 *out_path, int out_cap) {
    if (out_f) *out_f = NULL;
    if (out_path && out_cap > 0) out_path[0] = 0;
    if (!g_root || !out_f || !out_path || out_cap <= 1) return 0;

    // Prefer root models (common for single-model images), then /models
    if (llmk_try_open_first_model_in_dir_best_effort(NULL, out_f, out_path, out_cap)) return 1;
    if (llmk_try_open_first_model_in_dir_best_effort(L"models", out_f, out_path, out_cap)) return 1;
    return 0;
}

static void llmk_print_no_model_help(void) {
    Print(L"\r\nNo model loaded.\r\n");
    Print(L"Commands:\r\n");
    Print(L"  /models               List .bin/.gguf in root + models\\\r\n");
    Print(L"  /model_info [path]    Inspect a .bin/.gguf header/metadata\r\n");
    Print(L"  /cat <path>           Print a text file (e.g. repl.cfg)\r\n");
    Print(L"  /oo_handoff_info [f]  Inspect sovereign handoff JSON (default sovereign_export.json)\r\n");
    Print(L"  /oo_handoff_apply [f] Apply safe host->sovereign handoff fields\r\n");
    Print(L"  /oo_handoff_receipt   Show persisted handoff continuity receipt\r\n");
    Print(L"  /oo_continuity_status Compare handoff receipt vs local sovereign state\r\n");
    Print(L"  /oo_consult_mock <s>  Run deterministic OO consult without a loaded model\r\n");
    Print(L"  /oo_explain [verbose|boot] Explain the latest OO consult decision\r\n");
    Print(L"  /oo_log               Tail OOCONSULT.LOG\r\n");
    Print(L"  /oo_outcome           Tail OOOUTCOME.LOG and show pending feedback\r\n");
    Print(L"  /oo_infermini [text]  Run tiny built-in inference selftest (no-model)\r\n");
    Print(L"  /core_load <file>     Register the OO-SomaMind internal core target\r\n");
    Print(L"  /mind_diag            Show detailed OO-SomaMind runtime diagnostics\r\n");
    Print(L"  /mind_halt_probe [x]  Run a tiny HaltingHead probe with loop_pos=x (default 1.0)\r\n");
    Print(L"  /mind_halt_decide [x] [t]  Decide HALT/CONTINUE using loop_pos=x and threshold t\r\n");
    Print(L"  /mind_halt_sweep [a] [b] [s] [t]  Sweep loop_pos from a to b with step s\r\n");
    Print(L"  /mind_halt_policy [t] [on|off]  Show or set runtime halt policy\r\n");
    Print(L"  /mind_halt_policy_save  Persist current halt policy to repl.cfg\r\n");
    Print(L"  /mind_halt_policy_load  Reload halt policy from repl.cfg\r\n");
    Print(L"  /mind_halt_policy_apply_saved  Apply the saved halt policy to runtime\r\n");
    Print(L"  /mind_halt_policy_apply_saved_if_needed  Apply saved policy only when runtime differs\r\n");
    Print(L"  /mind_halt_policy_sync  Sync runtime halt policy from repl.cfg when needed\r\n");
    Print(L"  /mind_halt_policy_sync_force  Force a runtime halt policy reload from repl.cfg\r\n");
    Print(L"  /mind_halt_policy_audit  Audit runtime, persisted policy, and last apply state\r\n");
    Print(L"  /mind_audit  Run a global OO-SomaMind runtime audit\r\n");
    Print(L"  /mind_doctor  Propose the next safe corrective sequence for the runtime\r\n");
    Print(L"  /mind_next  Print the single best next runtime action\r\n");
    Print(L"  /mind_snapshot [strict]  Print a compact stable key=value runtime snapshot\r\n");
    Print(L"  /mind_ready  Report whether the V1 runtime is ready\r\n");
    Print(L"  /mind_bootstrap_v1  Auto-apply the obvious safe V1 bootstrap steps\r\n");
    Print(L"  /mind_path_v1  Print the minimal recommended V1 startup path\r\n");
    Print(L"  /oo_sidecar_audit  Audit the registered OOSS sidecar state and readiness\r\n");
    Print(L"  /attach_audit  Audit the registered attached model validation state and role\r\n");
    Print(L"  /attach_policy [status|audit|diff|sync|sync_force|reset [route]|<route> <temp> <top_p> <rep> <max_tokens>]  Configure attach route policy\r\n");
    Print(L"  /attach_policy_audit  Audit runtime vs persisted attach route policy\r\n");
    Print(L"  /attach_policy_diff   Compare runtime attach route policy vs repl.cfg\r\n");
    Print(L"  /attach_policy_sync   Sync runtime attach route policy from repl.cfg when needed\r\n");
    Print(L"  /attach_policy_sync_force  Force a runtime attach route policy reload from repl.cfg\r\n");
    Print(L"  /mind_halt_policy_reset  Restore the V1 runtime halt policy defaults\r\n");
    Print(L"  /mind_halt_policy_diff  Compare runtime halt policy vs repl.cfg\r\n");
    Print(L"  /oo_sidecar <file>    Register an OO sidecar extension (future OOSS path)\r\n");
    Print(L"  /oo_sidecar_unload    Remove the registered OO sidecar\r\n");
    Print(L"  /attach_load <file>   Validate and attach an optional external model\r\n");
    Print(L"  /attach_unload        Detach the optional external model\r\n");
    Print(L"  /mind_status          Show core vs attach runtime topology\r\n");
    Print(L"\r\n  SSM Inference:\r\n");
    Print(L"  /ssm_load <file>      Load SSM model (.bin v3)\r\n");
    Print(L"  /ssm_infer <text>     Generate text with loaded SSM model\r\n");
    Print(L"  /ssm_reset            Reset SSM hidden state\r\n");
    Print(L"  /ssm_params           Show current SSM sampling parameters\r\n");
    Print(L"  /temp <0.1-5.0>       Set sampling temperature (default 0.7)\r\n");
    Print(L"  /top_p <0.0-1.0>      Set nucleus sampling threshold (default 0.9)\r\n");
    Print(L"  /rep_penalty <1-3>    Set repetition penalty (default 1.3)\r\n");
    Print(L"  /max_tokens <1-512>   Set max tokens per generation (default 128)\r\n");
    Print(L"  /seed <N>             Set RNG seed for reproducible output\r\n");
    Print(L"  /ssm_selftest         Verify tokenizer + model pipeline\r\n");
    Print(L"  /verbose [0|1|2]      Toggle debug verbosity level\r\n");
    Print(L"\r\n  SomaMind:\r\n");
    Print(L"  /soma_status          Show router stats + model topology\r\n");
    Print(L"  /soma_dna             Show Digital DNA parameters\r\n");
    Print(L"  /soma_mutate [mag]    Mutate DNA (meta-evolution step)\r\n");
    Print(L"  /soma_route <text>    Test routing decision without inference\r\n");
    Print(L"  /soma_dual [0|1]      Enable/disable Dual Core (Solar+Lunar) sampling\r\n");
    Print(L"  /soma_dual_stats      Show Dual Core sampling statistics\r\n");
    Print(L"  /soma_smb_stats       Show Synaptic Memory Bus counters\r\n");
    Print(L"  /soma_smb_dump        Dump memory bus slots (active interactions)\r\n");
    Print(L"  /soma_dream [apply]   Run dream cycle (add 'apply' to update DNA)\r\n");
    Print(L"  /soma_meta            Show meta-evolution fitness history\r\n");
    Print(L"  /soma_evolve          Force fitness score + DNA mutation step\r\n");
    Print(L"  /multireal [on|off|status]  3-way token selection (solar/lunar/argmax)\r\n");
    Print(L"  /specdecode [on|off|status|threshold <x>]  Speculative decoding (Phase W)\r\n");
    Print(L"  /swarm_net [on|off|status|peer <id>|addr <hex>]  Distributed peer consensus (Phase Y)\r\n");
    Print(L"  /soma_swarm [0|1]     Enable/disable swarm voting\r\n");
    Print(L"  /soma_swarm_stats     Show per-agent fitness and vote counts\r\n");
    Print(L"  /soma_swarm_mode [majority|weighted|confident]  Set consensus mode\r\n");
    Print(L"  /soma_reflex [0|1]    Enable/disable symbolic math pre-solver\r\n");
    Print(L"  /soma_reflex_test <expr>  Test reflex scanner on an expression\r\n");
    Print(L"  /soma_logic [0|1]     Enable/disable syllogism/logic pre-solver\r\n");
    Print(L"  /soma_logic_test <sentence>  Test logic scanner\r\n");
    Print(L"  /soma_memory [0|1]    Enable/disable session memory reflex\r\n");
    Print(L"  /soma_memory_stats    Show session memory ring buffer stats\r\n");
    Print(L"  /soma_memory_test <prompt>  Test memory scan on a prompt\r\n");
    Print(L"  /soma_journal_save    Force-save session journal to soma_journal.bin\r\n");
    Print(L"  /soma_journal_load    Reload journal from disk into memory buffer\r\n");
    Print(L"  /soma_journal_clear   Erase soma_journal.bin (fresh start)\r\n");
    Print(L"  /soma_journal_stats   Show journal file stats (turns, sessions, entries)\r\n");
    Print(L"  /cortex_load <file>   Load oo-model cortex (small OOSS 15M-130M)\r\n");
    Print(L"  /cortex_infer <text>  Run cortex only (domain+safety classification)\r\n");
    Print(L"  /cortex_stats         Show cortex load status and call stats\r\n");
    Print(L"  /cortex [0|1]         Enable/disable cortex pre-routing\r\n");
    Print(L"  /soma_export          Export session to soma_train.jsonl (training data)\r\n");
    Print(L"  /soma_export_stats    Show soma_train.jsonl record count\r\n");
    Print(L"  /warden_status        Show warden pressure level and router threshold\r\n");
    Print(L"  /warden_reset         Reset pressure to NONE\r\n");
    Print(L"  /session_score        Show session fitness score and mutation magnitude\r\n");
    Print(L"  /session_reset        Reset session fitness counters\r\n");
    Print(L"  /dna_evolve_session   Evolve DNA using scored mutation magnitude\r\n");
    Print(L"  /dna_save             Save DNA to soma_dna.bin (EFI partition)\r\n");
    Print(L"  /dna_load             Load DNA from soma_dna.bin\r\n");
    Print(L"  /dna_reset            Reset DNA to defaults + delete soma_dna.bin\r\n");
    Print(L"\r\n  System:\r\n");
    Print(L"  reboot | reset        Reboot\r\n");
    Print(L"  shutdown              Power off\r\n");
    Print(L"  exit                  Return to UEFI shell\r\n\r\n");
    Print(L"To boot with a model: copy a supported .gguf/.bin to the USB root (or models\\)\r\n");
    Print(L"and set repl.cfg: model=<filename> then reboot.\r\n");
    Print(L"For OO-SomaMind V1, the current core execution path remains /ssm_load <file.mamb>.\r\n");
    Print(L"The future OO extension path is /oo_sidecar <file.ooss>.\r\n\r\n");
}

// Forward declarations for the no-model REPL (definitions appear later in this file).
void read_user_input(CHAR16* buffer, int max_len);
void char16_to_char(char* dest, CHAR16* src, int max_len);
static int my_strncmp(const char* s1, const char* s2, int n);
static int my_strcmp(const char* s1, const char* s2);
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);
static void llmk_models_ls_best_effort(const CHAR16 *path, int max_entries);
static void llmk_fs_cat_best_effort(const CHAR16 *path, UINTN max_bytes);
static void llmk_print_diag(void);
static EFI_STATUS llmk_file_write_u16(EFI_FILE_HANDLE f, const CHAR16 *s);
static int llmk_autorun_start(const CHAR16 *name, int shutdown_when_done);
static int llmk_autorun_next_line(char *out, int out_cap);
static void llmk_autorun_stop(void);
static int llmk_autorun_finish_if_eof(void);
static void llmk_oo_print_ooconsult_tail_best_effort(int max_lines);
static void llmk_oo_print_oooutcome_tail_best_effort(int max_lines);
static void llmk_oo_print_oojour_tail_best_effort(int max_lines);
static void llmk_oo_journal_cmd_best_effort(const char *cmd);
static EFI_STATUS llmk_oo_save_to_file_best_effort(const CHAR16 *name, int *out_bytes);
static void llmk_oo_handoff_info_best_effort(const CHAR16 *path);
static void llmk_oo_handoff_apply_best_effort(const CHAR16 *path);
static void llmk_oo_handoff_receipt_best_effort(const CHAR16 *path);
static void llmk_oo_continuity_status_best_effort(const CHAR16 *path);
static void llmk_oo_print_persistence_status_best_effort(void);
static void llmk_oo_print_last_consult_status_best_effort(void);
static void llmk_oo_explain_last_consult_best_effort(int verbose);
static void llmk_oo_explain_boot_best_effort(void);
static void llmk_oo_reboot_probe_best_effort(void);
static int llmk_best_effort_set_bootnext_to_bootcurrent(UINT16 *boot_current_out, EFI_STATUS *status_out);
static int llmk_cfg_parse_u64(const char *s, UINT64 *out);
static void llmk_oo_infermini_no_model(const char *args);
static int llmk_repl_cfg_read_ctx_seq_best_effort(int *out_ctx, int *out_seq);
static void llmk_oo_consult_process_suggestion(UINT64 ram_mb, UINT32 mode, UINT64 boots,
                                               int ctx, int seq,
                                               const char *llm_suggestion);

/* Forward declarations for variables defined later in this TU */
extern int g_boot_verbose;
static unsigned long long tsc_per_sec;

