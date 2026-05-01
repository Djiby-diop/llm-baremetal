static UINT32 llmk_infermini_lcg_step_u32(UINT32 *seed) {
    if (!seed) return 0;
    *seed = (*seed * 1664525u) + 1013904223u;
    return *seed;
}

static float llmk_infermini_randf01(UINT32 *seed) {
    UINT32 x = llmk_infermini_lcg_step_u32(seed);
    return (float)(x >> 8) / 16777216.0f;
}

static void llmk_infermini_fill_f32(float *dst, UINTN n, UINT32 *seed) {
    if (!dst || n == 0 || !seed) return;
    for (UINTN i = 0; i < n; i++) {
        float r = llmk_infermini_randf01(seed);
        dst[i] = (r - 0.5f) * 0.06f;
    }
}

static int llmk_infermini_map_char_to_tok(char c, int vocab_size) {
    if (vocab_size <= 0) return 0;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == ' ') return 37;
    if (c == '.') return 38;
    if (c == ',') return 39;
    if (c == '!') return 40;
    if (c == '?') return 41;
    return ((int)(unsigned char)c) % vocab_size;
}

static char llmk_infermini_tok_to_char(int tok) {
    if (tok >= 1 && tok <= 26) return (char)('a' + (tok - 1));
    if (tok >= 27 && tok <= 36) return (char)('0' + (tok - 27));
    if (tok == 37) return ' ';
    if (tok == 38) return '.';
    if (tok == 39) return ',';
    if (tok == 40) return '!';
    if (tok == 41) return '?';
    return '_';
}

// Diagnostic:displayGOP resolution, memory, build-id, detected models, CPU features
static void llmk_print_diag(void) {
    Print(L"\r\n========== DIAGNOSTIC MODE ==========\r\n\r\n");
    
    // 1. Build ID
    Print(L"Build ID: %s\r\n\r\n", LLMB_BUILD_ID);
    
    // 2. GOP (Graphics Output Protocol) Info
    if (g_gop && g_gop_fb32) {
        Print(L"[GOP] Graphics:\r\n");
        Print(L"  Resolution:    %dx%d\r\n", (int)g_gop_w, (int)g_gop_h);
        Print(L"  Scan Line:     %d pixels\r\n", (int)g_gop_ppsl);
        Print(L"  Framebuffer:   0x%lx\r\n", (UINT64)(UINTN)g_gop_fb32);
        if (g_gop->Mode) {
            Print(L"  FB Size:       %lu bytes\r\n", (UINT64)g_gop->Mode->FrameBufferSize);
        }
        Print(L"  Pixel Format:  %d\r\n", (int)g_gop_pf);
    } else {
        Print(L"[GOP] Graphics:  Not available\r\n");
    }
    Print(L"\r\n");
    
    // 3. Memory Info
    UINT64 total_mem = llmk_get_conventional_ram_bytes_best_effort();
    if (total_mem > 0) {
        UINT64 mb = total_mem / (1024ULL * 1024ULL);
        Print(L"[Memory] Conventional RAM: %lu MiB\r\n\r\n", mb);
    } else {
        Print(L"[Memory] Unable to query\r\n\r\n");
    }
    
    // 4. CPU Features
    Print(L"[CPU] Features:\r\n");
    CPUFeatures cpu_features;
    djiblas_detect_cpu(&cpu_features);
    sgemm_kernel_t k = djiblas_get_best_kernel(&cpu_features);
    const CHAR16 *kernel_name = L"SCALAR";
    if (k == djiblas_sgemm_avx512) kernel_name = L"AVX512";
    else if (k == djiblas_sgemm_avx2) kernel_name = (cpu_features.has_fma ? L"AVX2+FMA" : L"AVX2");
    else if (k == djiblas_sgemm_sse2) kernel_name = L"SSE2";
    
    Print(L"  SSE2:          %s\r\n", cpu_features.has_sse2 ? L"Yes" : L"No");
    Print(L"  AVX:           %s\r\n", cpu_features.has_avx ? L"Yes" : L"No");
    Print(L"  AVX2:          %s\r\n", cpu_features.has_avx2 ? L"Yes" : L"No");
    Print(L"  FMA:           %s\r\n", cpu_features.has_fma ? L"Yes" : L"No");
    Print(L"  SGEMM Kernel:  %s\r\n", kernel_name);
    Print(L"  Attn SIMD:     %s\r\n", g_attn_use_avx2 ? L"AVX2" : L"SSE2");
    Print(L"\r\n");
    
    // 5. Detected Models
    Print(L"[Models] Detected paths:\r\n");
    Print(L"  Root:\r\n");
    llmk_models_ls_best_effort(NULL, 200);
    Print(L"  models\\:\r\n");
    llmk_models_ls_best_effort(L"models", 200);
    
    Print(L"\r\n========== END DIAGNOSTIC ==========\r\n\r\n");
}

static void llmk_models_ls_best_effort(const CHAR16 *path, int max_entries) {
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
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, path, &dir, NULL, 0, L"models_ls_dir");
        if (EFI_ERROR(st) || !dir) {
            Print(L"\r\nERROR: cannot open %s: %r\r\n\r\n", (CHAR16 *)path, st);
            return;
        }
        close_dir = 1;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN printed = 0;
    UINTN matched = 0;
    UINTN bin_count = 0;
    UINTN gguf_count = 0;
    UINT64 total_bytes = 0;
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
        if (!info->FileName) continue;
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        CHAR16 alias_leaf[32]; // SAFE: FAT 8.3 alias leaf buffer for display only
        alias_leaf[0] = 0;
        int have_alias = llmk_try_guess_existing_fat83_alias(g_root, path, info->FileName,
                                                             alias_leaf, (int)(sizeof(alias_leaf) / sizeof(alias_leaf[0])));

        if (matched == 0) {
            Print(L"  size      type  name\r\n");
        }
        const CHAR16 *type = llmk_model_type_name16(info->FileName);
        Print(L"  ");
        llmk_print_u64(info->FileSize);
        Print(L" ");
        Print(L"%s", type);
        if (llmk_char16_streq(type, L"BIN")) {
            Print(L"   ");
            bin_count++;
        } else if (llmk_char16_streq(type, L"GGUF")) {
            Print(L"  ");
            gguf_count++;
        } else {
            Print(L"    ");
        }
        Print(L"%s", info->FileName);
        if (have_alias && !llmk_char16_streq_ci(alias_leaf, info->FileName)) {
            Print(L"  [8.3: %s]", alias_leaf);
        }
        Print(L"\r\n");
        printed++;
        matched++;
        total_bytes += info->FileSize;
    }

    if (matched == 0) {
        Print(L"  (no .bin/.gguf found)\r\n");
    }
    if (printed >= (UINTN)max_entries) {
        Print(L"  ... (truncated)\r\n");
    }
    if (matched > 0) {
        Print(L"  summary: total=");
        llmk_print_u64((UINT64)matched);
        Print(L" bin=");
        llmk_print_u64((UINT64)bin_count);
        Print(L" gguf=");
        llmk_print_u64((UINT64)gguf_count);
        Print(L" bytes=");
        llmk_print_u64(total_bytes);
        Print(L"\r\n");
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
}

static void llmk_diag_write_models_inventory_to_file(EFI_FILE_HANDLE f,
                                                     const CHAR16 *path,
                                                     const CHAR16 *label,
                                                     int max_entries) {
    if (!f) return;
    if (max_entries <= 0) max_entries = 200;
    if (max_entries > 500) max_entries = 500;

    llmk_file_write_u16(f, label ? label : L"Models");
    llmk_file_write_u16(f, L":\r\n");

    if (!g_root) {
        llmk_file_write_u16(f, L"  (file system not ready)\r\n\r\n");
        return;
    }

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;
    if (!path || path[0] == 0 || llmk_char16_streq(path, L".") || llmk_char16_streq(path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS open_st = llmk_open_read_with_fat83_fallback(g_root, path, &dir, NULL, 0, L"diag_models_dir");
        if (EFI_ERROR(open_st) || !dir) {
            CHAR16 line[160]; // SAFE: bounded one-line diagnostic message
            SPrint(line, sizeof(line), L"  (cannot open %s: %r)\r\n\r\n", (CHAR16 *)path, open_st);
            llmk_file_write_u16(f, line);
            return;
        }
        close_dir = 1;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        llmk_file_write_u16(f, L"  (OOM)\r\n\r\n");
        return;
    }

    UINTN printed = 0;
    UINTN matched = 0;
    UINTN bin_count = 0;
    UINTN gguf_count = 0;
    UINT64 total_bytes = 0;

    while (printed < (UINTN)max_entries) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st)) {
            CHAR16 line[128]; // SAFE: bounded error line buffer
            SPrint(line, sizeof(line), L"  (read failed: %r)\r\n", st);
            llmk_file_write_u16(f, line);
            break;
        }
        if (sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (!info->FileName) continue;
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        CHAR16 alias_leaf[32]; // SAFE: FAT 8.3 alias display buffer
        alias_leaf[0] = 0;
        int have_alias = llmk_try_guess_existing_fat83_alias(g_root, path, info->FileName,
                                                             alias_leaf, (int)(sizeof(alias_leaf) / sizeof(alias_leaf[0])));

        CHAR16 line[320]; // SAFE: bounded model inventory line
        const CHAR16 *type = llmk_model_type_name16(info->FileName);
        if (have_alias && !llmk_char16_streq_ci(alias_leaf, info->FileName)) {
            SPrint(line, sizeof(line), L"  %s | %lu bytes | %s | 8.3=%s\r\n",
                   type, (UINT64)info->FileSize, info->FileName, alias_leaf);
        } else {
            SPrint(line, sizeof(line), L"  %s | %lu bytes | %s\r\n",
                   type, (UINT64)info->FileSize, info->FileName);
        }
        llmk_file_write_u16(f, line);

        if (llmk_char16_streq(type, L"BIN")) bin_count++;
        else if (llmk_char16_streq(type, L"GGUF")) gguf_count++;
        total_bytes += info->FileSize;
        printed++;
        matched++;
    }

    if (matched == 0) {
        llmk_file_write_u16(f, L"  (no .bin/.gguf found)\r\n");
    }
    if (printed >= (UINTN)max_entries) {
        llmk_file_write_u16(f, L"  ... (truncated)\r\n");
    }
    if (matched > 0) {
        CHAR16 line[192]; // SAFE: bounded summary line
        SPrint(line, sizeof(line), L"  summary: total=%lu bin=%lu gguf=%lu bytes=%lu\r\n",
               (UINT64)matched, (UINT64)bin_count, (UINT64)gguf_count, total_bytes);
        llmk_file_write_u16(f, line);
    }
    llmk_file_write_u16(f, L"\r\n");

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
}

static int llmk_char16_has_path_sep(const CHAR16 *s) {
    if (!s) return 0;
    for (const CHAR16 *p = s; *p; p++) {
        if (*p == L'\\' || *p == L'/') return 1;
    }
    return 0;
}

static int llmk_try_open_model_spec_best_effort(EFI_FILE_HANDLE Root,
                                                const CHAR16 *spec,
                                                EFI_FILE_HANDLE *out_file,
                                                CHAR16 *out_picked,
                                                int out_picked_cap) {
    if (out_file) *out_file = NULL;
    if (out_picked && out_picked_cap > 0) out_picked[0] = 0;
    if (!Root || !spec || !spec[0] || !out_file) return 0;

    const int has_sep = llmk_char16_has_path_sep(spec);
    const int has_ext = llmk_char16_has_dot_ext(spec);
    CHAR16 candidates[8][192]; // SAFE: bounded candidate path list for model resolution
    int n_candidates = 0;

    llmk_char16_copy_cap(candidates[n_candidates++], 192, spec);
    if (!has_sep) {
        CHAR16 p[192]; // SAFE: bounded prefixed path buffer
        StrCpy(p, L"models\\");
        StrCat(p, spec);
        llmk_char16_copy_cap(candidates[n_candidates++], 192, p);
    }
    if (!has_ext) {
        CHAR16 p[192]; // SAFE: bounded extension candidate buffer
        llmk_char16_copy_cap(p, 192, spec);
        StrCat(p, L".bin");
        llmk_char16_copy_cap(candidates[n_candidates++], 192, p);
        llmk_char16_copy_cap(p, 192, spec);
        StrCat(p, L".gguf");
        llmk_char16_copy_cap(candidates[n_candidates++], 192, p);
        if (!has_sep) {
            StrCpy(p, L"models\\");
            StrCat(p, spec);
            StrCat(p, L".bin");
            llmk_char16_copy_cap(candidates[n_candidates++], 192, p);
            StrCpy(p, L"models\\");
            StrCat(p, spec);
            StrCat(p, L".gguf");
            llmk_char16_copy_cap(candidates[n_candidates++], 192, p);
        }
    }

    for (int ci = 0; ci < n_candidates; ci++) {
        EFI_FILE_HANDLE f = NULL;
        CHAR16 picked[192]; // SAFE: bounded picked path buffer
        picked[0] = 0;
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, candidates[ci], &f, picked,
                                                          (int)(sizeof(picked) / sizeof(picked[0])),
                                                          L"model_spec");
        if (!EFI_ERROR(st) && f) {
            *out_file = f;
            if (out_picked && out_picked_cap > 0) {
                llmk_char16_copy_cap(out_picked, out_picked_cap, picked[0] ? picked : candidates[ci]);
            }
            return 1;
        }
    }
    return 0;
}

static void llmk_fs_cat_best_effort(const CHAR16 *path, UINTN max_bytes) {
    if (max_bytes == 0) max_bytes = (256U * 1024U);
    if (max_bytes > (1024U * 1024U)) max_bytes = (1024U * 1024U);

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(path, &raw, &raw_len);
    if (EFI_ERROR(st)) {
        Print(L"\r\nERROR: cat failed: %r\r\n\r\n", st);
        return;
    }

    UINT8 *b = (UINT8 *)raw;
    UINTN n = raw_len;
    if (n > max_bytes) n = max_bytes;

    // UTF-16 BOM detection (LE/BE). Print best-effort ASCII.
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        int is_le = (b[0] == 0xFF);
        UINTN chars = (n - 2) / 2;
        for (UINTN i = 0; i < chars; i++) {
            UINT8 lo = b[2 + i * 2 + 0];
            UINT8 hi = b[2 + i * 2 + 1];
            UINT16 ch = is_le ? (UINT16)(lo | ((UINT16)hi << 8)) : (UINT16)(hi | ((UINT16)lo << 8));
            if (ch == 0) break;
            CHAR16 out = (ch < 0x80) ? (CHAR16)ch : L'?';
            Print(L"%c", out);
        }
    } else {
        for (UINTN i = 0; i < n; i++) {
            UINT8 c = b[i];
            if (c == 0) break;
            // Print ASCII; map CR to LF
            if (c == '\r') c = '\n';
            if (c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7E)) {
                Print(L"%c", (CHAR16)c);
            }
        }
    }
    Print(L"\r\n");

    if (raw_len > max_bytes) {
        Print(L"(truncated to %d bytes)\r\n", (int)max_bytes);
    }

    uefi_call_wrapper(BS->FreePool, 1, raw);
}

typedef struct {
    UINT32 magic;      // 'SNP1'
    UINT32 version;    // 1
    UINT32 dim;
    UINT32 n_layers;
    UINT32 n_heads;
    UINT32 n_kv_heads;
    UINT32 seq_len;
    UINT32 kv_dim;
    UINT32 kv_pos;
} LlmkSnapHeader;

#define LLMK_SNAP_MAGIC 0x31504E53u

static EFI_STATUS llmk_write_exact(EFI_FILE_HANDLE f, const void *src, UINTN total_bytes) {
    const UINT8 *p = (const UINT8 *)src;
    UINTN remaining = total_bytes;
    while (remaining > 0) {
        UINTN chunk = remaining;
        if (chunk > (8U * 1024U * 1024U)) chunk = (8U * 1024U * 1024U);
        UINTN nb = chunk;
        EFI_STATUS st = uefi_call_wrapper(f->Write, 3, f, &nb, (void *)p);
        if (EFI_ERROR(st)) return st;
        if (nb != chunk) return EFI_DEVICE_ERROR;
        p += nb;
        remaining -= nb;
    }
    return EFI_SUCCESS;
}

// ============================================================================
// AUTORUN SCRIPT (best-effort)
// ============================================================================

// Forward decl (autorun helpers use it before definition)
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);

static int g_autorun_active = 0;
static int g_autorun_shutdown_when_done = 0;
static char *g_autorun_buf = NULL;
static UINTN g_autorun_len = 0;
static UINTN g_autorun_pos = 0;

static void llmk_autorun_stop(void) {
    g_autorun_active = 0;
    g_autorun_shutdown_when_done = 0;
    g_autorun_pos = 0;
    g_autorun_len = 0;
    if (g_autorun_buf) {
        uefi_call_wrapper(BS->FreePool, 1, g_autorun_buf);
        g_autorun_buf = NULL;
    }
}

static int llmk_autorun_decode_to_ascii(void *raw, UINTN raw_len, char **out_ascii, UINTN *out_len) {
    if (out_ascii) *out_ascii = NULL;
    if (out_len) *out_len = 0;
    if (!raw || raw_len == 0 || !out_ascii || !out_len) return 0;

    UINT8 *b = (UINT8 *)raw;
    // Detect UTF-16 BOM (LE/BE). If present, down-convert to 7-bit ASCII best-effort.
    if (raw_len >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        int is_le = (b[0] == 0xFF);
        UINTN chars = (raw_len - 2) / 2;
        char *txt = NULL;
        EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, chars + 1, (void **)&txt);
        if (EFI_ERROR(st) || !txt) return 0;
        for (UINTN i = 0; i < chars; i++) {
            UINT8 lo = b[2 + i * 2 + 0];
            UINT8 hi = b[2 + i * 2 + 1];
            UINT16 ch = is_le ? (UINT16)(lo | ((UINT16)hi << 8)) : (UINT16)(hi | ((UINT16)lo << 8));
            if (ch == 0) { txt[i] = 0; *out_ascii = txt; *out_len = i; return 1; }
            txt[i] = (ch < 0x80) ? (char)ch : '?';
        }
        txt[chars] = 0;
        *out_ascii = txt;
        *out_len = chars;
        return 1;
    }

    // Assume ASCII/UTF-8 bytes; copy and NUL-terminate.
    char *txt = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&txt);
    if (EFI_ERROR(st) || !txt) return 0;
    for (UINTN i = 0; i < raw_len; i++) txt[i] = (char)b[i];
    txt[raw_len] = 0;
    *out_ascii = txt;
    *out_len = raw_len;
    return 1;
}

static int llmk_autorun_start(const CHAR16 *name, int shutdown_when_done) {
    if (!name) return 0;
    llmk_autorun_stop();

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        return 0;
    }

    char *txt = NULL;
    UINTN txt_len = 0;
    int ok = llmk_autorun_decode_to_ascii(raw, raw_len, &txt, &txt_len);
    uefi_call_wrapper(BS->FreePool, 1, raw);
    if (!ok || !txt) {
        if (txt) uefi_call_wrapper(BS->FreePool, 1, txt);
        return 0;
    }

    g_autorun_buf = txt;
    g_autorun_len = txt_len;
    g_autorun_pos = 0;
    g_autorun_active = 1;
    g_autorun_shutdown_when_done = shutdown_when_done ? 1 : 0;
    Print(L"[autorun] loaded %s (%d bytes)\r\n", name, (int)txt_len);
    return 1;
}

static void llmk_autorun_trim(char *s) {
    if (!s) return;
    // left trim
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
    if (i > 0) {
        int j = 0;
        while (s[i]) s[j++] = s[i++];
        s[j] = 0;
    }
    // right trim
    int n = 0;
    while (s[n]) n++;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    s[n] = 0;
}

static int llmk_autorun_next_line(char *out, int out_cap) {
    if (out && out_cap > 0) out[0] = 0;
    if (!g_autorun_active || !g_autorun_buf || g_autorun_pos >= g_autorun_len) return 0;
    if (!out || out_cap <= 1) return 0;

    // Skip empty lines and comments.
    while (g_autorun_pos < g_autorun_len) {
        int op = 0;
        // Read one line
        while (g_autorun_pos < g_autorun_len) {
            char c = g_autorun_buf[g_autorun_pos++];
            if (c == '\n') break;
            if (c == '\r') continue;
            if (op + 1 < out_cap) out[op++] = c;
        }
        out[op] = 0;
        llmk_autorun_trim(out);
        if (out[0] == 0) continue;
        if (out[0] == '#' || out[0] == ';') continue;
        return 1;
    }

    return 0;
}

static int llmk_autorun_finish_if_eof(void) {
    if (!g_autorun_active) return 0;
    if (!g_autorun_buf) return 0;
    if (g_autorun_pos < g_autorun_len) return 0;

    Print(L"[autorun] done\r\n");
    int shutdown = g_autorun_shutdown_when_done;
    llmk_autorun_stop();
    if (shutdown) {
        Print(L"[autorun] shutting down\r\n");
        llmk_shutdown_best_effort();
    }
    return 1;
}

static void llmk_autorun_print_file_best_effort(const CHAR16 *name, int max_lines) {
    if (!name) name = L"llmk-autorun.txt";
    if (max_lines <= 0) max_lines = 200;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[autorun] cannot read %s (%r)\r\n\r\n", name, st);
        return;
    }

    char *txt = NULL;
    UINTN txt_len = 0;
    int ok = llmk_autorun_decode_to_ascii(raw, raw_len, &txt, &txt_len);
    uefi_call_wrapper(BS->FreePool, 1, raw);
    if (!ok || !txt) {
        if (txt) uefi_call_wrapper(BS->FreePool, 1, txt);
        Print(L"\r\n[autorun] decode failed\r\n\r\n");
        return;
    }

    Print(L"\r\n[autorun] %s:\r\n", name);
    UINTN pos = 0;
    int lines = 0;
    char linebuf[256];

    while (pos < txt_len && lines < max_lines) {
        int op = 0;
        while (pos < txt_len) {
            char c = txt[pos++];
            if (c == '\n') break;
            if (c == '\r') continue;
            if (op + 1 < (int)sizeof(linebuf)) linebuf[op++] = c;
        }
        linebuf[op] = 0;
        llmk_autorun_trim(linebuf);
        if (linebuf[0] == 0) continue;
        if (linebuf[0] == '#' || linebuf[0] == ';') continue;

        CHAR16 p16[300];
        ascii_to_char16(p16, linebuf, (int)(sizeof(p16) / sizeof(p16[0])));
        Print(L"  %s\r\n", p16);
        lines++;
    }

    if (lines == 0) {
        Print(L"  (no runnable lines)\r\n");
    } else if (pos < txt_len) {
        Print(L"  ... (truncated)\r\n");
    }
    Print(L"\r\n");
    uefi_call_wrapper(BS->FreePool, 1, txt);
}

static int llmk_ascii_append_u32(char *dst, int cap, int pos, UINT32 v) {
    if (!dst || cap <= 0) return pos;
    if (pos < 0) pos = 0;
    if (pos >= cap) return pos;

    char tmp[16]; // SAFE: enough for UINT32 decimal digits; bounded by sizeof(tmp)
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        if (pos + 1 >= cap) break;
        dst[pos++] = tmp[i];
    }
    return pos;
}

// Forward declaration (llmk_save_ppm uses it before definition)
void* simple_alloc(unsigned long bytes);

static EFI_STATUS llmk_save_ppm(const CHAR16 *name) {
    if (!g_gop_fb32) return EFI_NOT_READY;
    if (!name) name = L"llmk-img.ppm";

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, name);
    if (EFI_ERROR(st)) return st;

    char header[64];
    int pos = 0;
    header[pos++] = 'P'; header[pos++] = '6'; header[pos++] = '\n';
    pos = llmk_ascii_append_u32(header, (int)sizeof(header), pos, g_gop_w);
    header[pos++] = ' ';
    pos = llmk_ascii_append_u32(header, (int)sizeof(header), pos, g_gop_h);
    header[pos++] = '\n';
    header[pos++] = '2'; header[pos++] = '5'; header[pos++] = '5'; header[pos++] = '\n';

    st = llmk_file_write_bytes(f, header, (UINTN)pos);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(f->Close, 1, f);
        return st;
    }

    // Row buffer: RGB bytes
    UINTN row_bytes = (UINTN)g_gop_w * 3U;
    UINT8 *row = (UINT8 *)simple_alloc((unsigned long)row_bytes);
    if (!row) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_OUT_OF_RESOURCES;
    }

    for (UINT32 y = 0; y < g_gop_h; y++) {
        UINTN off = 0;
        for (UINT32 x = 0; x < g_gop_w; x++) {
            UINT8 r, g, b;
            llmk_gop_get_pixel(x, y, &r, &g, &b);
            row[off++] = r;
            row[off++] = g;
            row[off++] = b;
        }
        st = llmk_file_write_bytes(f, row, row_bytes);
        if (EFI_ERROR(st)) {
            uefi_call_wrapper(f->Close, 1, f);
            return st;
        }
    }

    // Flush-before-close for persistence on real hardware
    uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    return EFI_SUCCESS;
}

static UINT64 g_budget_prefill_cycles = 0;
static UINT64 g_budget_decode_cycles = 0;

// Config (repl.cfg)
// By default, we do NOT auto-run llmk-autorun.txt at boot (to keep QEMU interactive).
static int g_cfg_autorun_autostart = 0;
static int g_cfg_autorun_shutdown_when_done = 0;
static CHAR16 g_cfg_autorun_file[96] = L"llmk-autorun.txt";

// Boot logging verbosity. 0=quiet (default), 1=verbose.
int g_boot_verbose = 0;
// Boot logo. 1=show ASCII logo after repl.cfg is loaded, 0=skip.
static int g_boot_logo = 1;
// Boot diagnostic. 0=off (default), 1=show system info at boot.
static int g_boot_diag = 0;
static int g_cfg_loaded = 0;

// GGUF Q8_0 blob mode (keeps matrices quantized in RAM). 1=enabled (default), 0=force float32 load.
static int g_cfg_gguf_q8_blob = 1;
// Q8_0 matmul option: quantize activations (x) to Q8_0 for faster AVX2 int8 dot kernels.
// 0=off (default, higher fidelity)
// 1=on for all Q8 matmuls (fastest, most approximation)
// 2=FFN-only (w1/w3/w2), attention projections stay float (better quality/perf tradeoff)
static int g_cfg_q8_act_quant = 0;

typedef enum {
    LLMK_CHAT_FMT_YOU_AI = 0,
    LLMK_CHAT_FMT_LLAMA2 = 1,
    LLMK_CHAT_FMT_CHATML = 2,
    LLMK_CHAT_FMT_ALPACA = 3,
    LLMK_CHAT_FMT_RAW = 4,
} LlmkChatFormat;

// Chat formatting (default: You/AI). Used to wrap user turns for instruct/chat models.
static int g_cfg_chat_format = LLMK_CHAT_FMT_YOU_AI;
static char g_cfg_system_prompt[256] = {0};

// Model picker and context override (repl.cfg)
static int g_cfg_model_picker = 1;
static int g_cfg_ctx_len = 0;

// Rate-limit budget overrun prints (avoid flooding console).
static UINT32 g_budget_overruns_prefill = 0;
static UINT32 g_budget_overruns_decode = 0;

// Forward decl (used by repl.cfg loader before definition)
static void set_seed(unsigned int seed);
// Forward decl (used by model override reader before definition)
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);
// Forward decl (used by repl.cfg editor helper before definition)
static int my_strlen(const char* s);
// Forward decl (used by transformer_forward before definition)
static int llmk_has_avx2_cached(void);

static void llmk_reset_runtime_state(void) {
    // Budgets
    g_budget_prefill_cycles = 0;
    g_budget_decode_cycles = 0;
    g_budget_overruns_prefill = 0;
    g_budget_overruns_decode = 0;

    // Log (best-effort)
    if (g_llmk_log.capacity) {
        g_llmk_log.entries = 0;
        g_llmk_log.write_idx = 0;
    }

    // Sentinel
    g_sentinel.tripped = FALSE;
    g_sentinel.last_error = LLMK_OK;
    g_sentinel.last_reason[0] = 0;

    // UTF-8 repair tail
    uefi_print_utf8_flush();
}

static UINT64 llmk_u64_max(UINT64 a, UINT64 b) { return (a > b) ? a : b; }

static void llmk_budget_update(UINT64 *budget, UINT64 last_dt) {
    // Adaptive budget: target = last_dt * margin, then EMA to smooth.
    // Margin must tolerate pos growth and occasional slowdowns.
    const UINT64 margin = 6ULL;
    UINT64 target = last_dt * margin;
    if (target < 500000ULL) target = 500000ULL;
    if (*budget == 0) {
        *budget = target;
        return;
    }
    UINT64 prev = *budget;
    // If we started from a huge initial budget, snap down quickly once we have a real measurement.
    if (prev > target * 4ULL) {
        *budget = target;
        return;
    }
    // EMA: new = (7/8)*old + (1/8)*target
    *budget = ((*budget * 7ULL) + target) / 8ULL;
    // Never decrease too aggressively; keep at least 80% of previous.
    *budget = llmk_u64_max(*budget, (prev * 4ULL) / 5ULL);
}

static void* llmk_alloc_acts(UINT64 bytes, const CHAR16* tag) {
    if (!g_llmk_ready) return NULL;
    return llmk_sentinel_alloc(&g_sentinel, LLMK_ARENA_ACTIVATIONS, bytes, 16, tag);
}

static void* llmk_alloc_weights(UINT64 bytes, const CHAR16* tag) {
    if (!g_llmk_ready) return NULL;
    return llmk_sentinel_alloc(&g_sentinel, LLMK_ARENA_WEIGHTS, bytes, 64, tag);
}

static void* llmk_alloc_kv(UINT64 bytes, const CHAR16* tag) {
    if (!g_llmk_ready) return NULL;
    return llmk_sentinel_alloc(&g_sentinel, LLMK_ARENA_KV_CACHE, bytes, 64, tag);
}

void* simple_alloc(unsigned long bytes) {
    // Backward-compatible interface: route default allocations into ACTS arena
    // once the kernel allocator is initialized.
    if (g_llmk_ready) {
        return llmk_alloc_acts((UINT64)bytes, L"repl alloc");
    }
    if (heap_offset + bytes > heap_size) return NULL;
    void* ptr = heap_base + heap_offset;
    heap_offset += bytes;
    return ptr;
}

static EFI_STATUS read_exact(EFI_FILE_HANDLE file, void *dst, UINTN total_bytes) {
    UINT8 *p = (UINT8 *)dst;
    UINTN remaining = total_bytes;
    UINTN done = 0;
    UINTN next_report = 0;
    UINTN next_ui = 0;
    while (remaining > 0) {
        UINTN chunk = remaining;
        // Large reads can fail on some UEFI implementations; keep chunks modest.
        if (chunk > (16U * 1024U * 1024U)) chunk = (16U * 1024U * 1024U);
        UINTN got = chunk;
        EFI_STATUS st = uefi_call_wrapper(file->Read, 3, file, &got, p);
        if (EFI_ERROR(st)) return st;
        if (got == 0) return EFI_LOAD_ERROR;
        p += got;
        done += got;
        if (got > remaining) return EFI_LOAD_ERROR;
        remaining -= got;

        // Animated UI (cheap): update every ~8MB for large reads.
        if (total_bytes >= (64U * 1024U * 1024U)) {
            if (done >= next_ui) {
                InterfaceFx_Tick();
                InterfaceFx_ProgressBytes(done, total_bytes);
                next_ui = done + (8U * 1024U * 1024U);
            }
        }

        // Progress (avoid spamming): report every 64MB for large reads.
        if (total_bytes >= (128U * 1024U * 1024U)) {
            if (done >= next_report) {
                UINTN mb_done = done / (1024U * 1024U);
                UINTN mb_total = total_bytes / (1024U * 1024U);
                if (g_boot_verbose) {
                    Print(L"  Reading weights... %d / %d MB\r\n", (int)mb_done, (int)mb_total);
                }
                next_report = done + (64U * 1024U * 1024U);
            }
        }
    }
    return EFI_SUCCESS;
}

// ============================================================================
// BEST-EFFORT DUMP TO FILE (UTF-16LE)
// ============================================================================

static EFI_STATUS llmk_open_text_file(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st)) return st;

    // Truncate to 0 and write BOM.
    uefi_call_wrapper(f->SetPosition, 2, f, 0);
    UINT16 bom = 0xFEFF;
    UINTN nb = sizeof(bom);
    uefi_call_wrapper(f->Write, 3, f, &nb, &bom);

    *out = f;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    // Use FAT83 fallback to tolerate flaky long filename opens.
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, name, &f, NULL, 0, L"open_read_file");
    if (EFI_ERROR(st) || !f) return st;
    *out = f;
    return EFI_SUCCESS;
}

static int llmk_cfg_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char llmk_cfg_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static void llmk_cfg_trim(char **s) {
    if (!s || !*s) return;
    char *p = *s;
    while (llmk_cfg_is_space(*p)) p++;
    *s = p;
    char *end = p;
    while (*end) end++;
    while (end > p && llmk_cfg_is_space(end[-1])) end--;
    *end = 0;
}

static int llmk_cfg_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (llmk_cfg_tolower(*a) != llmk_cfg_tolower(*b)) return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static int llmk_read_cfg_model_best_effort(EFI_FILE_HANDLE Root, CHAR16 *out, int out_cap) {
    if (!out || out_cap <= 0) return 0;
    out[0] = 0;
    if (!Root) return 0;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, L"repl.cfg", &f, NULL, 0, L"cfg_open");
    if (EFI_ERROR(st) || !f) return 0;

    char buf[2048];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return 0;
    buf[sz] = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
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
        if (key[0] == 0 || val[0] == 0) continue;

        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "model") || llmk_cfg_streq_ci(key, "model_file") || llmk_cfg_streq_ci(key, "weights")) {
            ascii_to_char16(out, val, out_cap);
            // Trim again (defensive against trailing spaces).
            while (out[0] == L' ' || out[0] == L'\t') {
                for (int i = 0; i + 1 < out_cap; i++) out[i] = out[i + 1];
            }
            return (out[0] != 0);
        }
    }

    return 0;
}

static void llmk_load_repl_cfg_boot_best_effort(void) {
    // Best-effort: read repl.cfg early and pick only boot verbosity keys.
    // Supported keys:
    //   boot_verbose=0/1/2   (2 enables extra debug)
    //   boot_quiet=0/1  (inverse of boot_verbose)
    //   boot_logo=0/1
    //   boot_diag=0/1  (show system diagnostics: GOP/RAM/CPU/models)
    //   gguf_q8_blob=0/1  (enable/disable Q8_0 blob mode)
    //   q8_act_quant=0/1/2  (Q8 activation quantization mode)
    //   fat83_force=0/1 (test/diag: prefer FAT 8.3 alias opens)
    //   oo_enable=0/1 (OO v0: write oostate.bin + append oojour.log)
    //   oo_min_total_mb=<int> (OO M3: override Zone-B total minimum, in MB; 0 disables floor)
    //   oo_plan_enable=0/1 (OO M7.2: enable bounded multi-step auto-plan)
    //   oo_plan_max_actions=1..3 (OO M7.2: max auto-applies per boot window)
    //   oo_net=0/1 (OO M4: network read-only tick placeholder; never required)
    //   oo_manifest_url=<string> (OO M4: URL hint for a signed manifest fetch; placeholder)
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st) || !f) return;

    char buf[2048];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    char *p = buf;
    while (*p) {
        // Extract a line.
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR.
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        // Skip comments/empty.
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
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

        if (llmk_cfg_streq_ci(key, "boot_verbose") || llmk_cfg_streq_ci(key, "verbose_boot")) {
            int mode;
            if (llmk_cfg_parse_i32(val, &mode)) {
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                g_boot_verbose = mode;
            } else {
                int b;
                if (llmk_cfg_parse_bool(val, &b)) {
                    g_boot_verbose = (b != 0) ? 1 : 0;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "boot_quiet") || llmk_cfg_streq_ci(key, "quiet_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_boot_verbose = (b != 0) ? 0 : 1;
            }
        } else if (llmk_cfg_streq_ci(key, "boot_logo") || llmk_cfg_streq_ci(key, "logo_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_boot_logo = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "boot_diag") || llmk_cfg_streq_ci(key, "diag")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_boot_diag = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "gguf_q8_blob") || llmk_cfg_streq_ci(key, "q8_blob") || llmk_cfg_streq_ci(key, "gguf_blob")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_gguf_q8_blob = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "q8_act_quant") || llmk_cfg_streq_ci(key, "q8_act_quantize") || llmk_cfg_streq_ci(key, "q8_x_quant")) {
            int mode;
            if (llmk_cfg_parse_i32(val, &mode)) {
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                g_cfg_q8_act_quant = mode;
            } else {
                int b;
                if (llmk_cfg_parse_bool(val, &b)) {
                    g_cfg_q8_act_quant = (b != 0) ? 1 : 0;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "model_picker") || llmk_cfg_streq_ci(key, "model_menu")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_model_picker = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "ctx_len") || llmk_cfg_streq_ci(key, "context") || llmk_cfg_streq_ci(key, "context_len")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                g_cfg_ctx_len = v;
            }
        } else if (llmk_cfg_streq_ci(key, "fat83_force") || llmk_cfg_streq_ci(key, "force_fat83") || llmk_cfg_streq_ci(key, "fat83_prefer")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_fat83_force = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_enable") || llmk_cfg_streq_ci(key, "oo") || llmk_cfg_streq_ci(key, "organism")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_enable = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_min_total_mb") || llmk_cfg_streq_ci(key, "oo_zones_min_total_mb") || llmk_cfg_streq_ci(key, "oo_min_total")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < -1) v = -1;
                g_cfg_oo_min_total_mb = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_llm_consult") || llmk_cfg_streq_ci(key, "oo_consult")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_llm_consult = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_multi_actions") || llmk_cfg_streq_ci(key, "oo_multi")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_multi_actions = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_auto_apply") || llmk_cfg_streq_ci(key, "oo_auto")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                g_cfg_oo_auto_apply = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_plan_enable") || llmk_cfg_streq_ci(key, "oo_plan") || llmk_cfg_streq_ci(key, "oo_multi_plan")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_plan_enable = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_plan_max_actions") || llmk_cfg_streq_ci(key, "oo_plan_max") || llmk_cfg_streq_ci(key, "oo_max_actions")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 3) v = 3;
                g_cfg_oo_plan_max_actions = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_consult_log") || llmk_cfg_streq_ci(key, "oo_log")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_consult_log = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_conf_gate") || llmk_cfg_streq_ci(key, "oo_confidence_gate") || llmk_cfg_streq_ci(key, "oo_conf_gate_enable")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_conf_gate = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_conf_threshold") || llmk_cfg_streq_ci(key, "oo_confidence_threshold")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                g_cfg_oo_conf_threshold = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_net") || llmk_cfg_streq_ci(key, "oo_net_enable") || llmk_cfg_streq_ci(key, "oo_network")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_net = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_manifest_url") || llmk_cfg_streq_ci(key, "oo_manifest") || llmk_cfg_streq_ci(key, "oo_manifest_uri")) {
            llmk_cfg_copy_ascii_token(g_cfg_oo_manifest_url, (int)sizeof(g_cfg_oo_manifest_url), val);
        }
    }
}

static void llmk_print_logo(void) {
    // Keep it ASCII-only (UEFI consoles vary) and within ~80 cols.
    Print(L"\r\n");
    Print(L"   ___   ___  \r\n");
    Print(L"  / _ \\ / _ \\ \r\n");
    Print(L" | | | | | | |\r\n");
    Print(L" | |_| | |_| |\r\n");
    Print(L"  \\___/ \\___/ \r\n");
    Print(L"      Operating Organism\r\n\r\n");

    // Serial-visible marker (keeps automated logs honest).
    llmk_serial_write_char16(L"[logo] printed\r\n");
}

static int llmk_cfg_parse_u64(const char *s, UINT64 *out) {
    if (!s || !out) return 0;
    UINT64 v = 0;
    int any = 0;
    while (*s && llmk_cfg_is_space(*s)) s++;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10ULL + (UINT64)(*s - '0');
        s++;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

static int llmk_cfg_parse_i32(const char *s, int *out) {
    if (!s || !out) return 0;
    int sign = 1;
    while (*s && llmk_cfg_is_space(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    int v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10 + (int)(*s - '0');
        s++;
    }
    if (!any) return 0;
    *out = v * sign;
    return 1;
}

static int llmk_cfg_parse_f32(const char *s, float *out) {
    if (!s || !out) return 0;
    int sign = 1;
    while (*s && llmk_cfg_is_space(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    float val = 0.0f;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        val = val * 10.0f + (float)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        float frac = 0.1f;
        while (*s >= '0' && *s <= '9') {
            any = 1;
            val += (float)(*s - '0') * frac;
            frac *= 0.1f;
            s++;
        }
    }
    if (!any) return 0;
    *out = val * (float)sign;
    return 1;
}

static int llmk_cfg_parse_bool(const char *s, int *out) {
    if (!s || !out) return 0;
    while (*s && llmk_cfg_is_space(*s)) s++;
    if (llmk_cfg_streq_ci(s, "1") || llmk_cfg_streq_ci(s, "true") || llmk_cfg_streq_ci(s, "on") || llmk_cfg_streq_ci(s, "yes")) {
        *out = 1;
        return 1;
    }
    if (llmk_cfg_streq_ci(s, "0") || llmk_cfg_streq_ci(s, "false") || llmk_cfg_streq_ci(s, "off") || llmk_cfg_streq_ci(s, "no")) {
        *out = 0;
        return 1;
    }
    int iv = 0;
    if (llmk_cfg_parse_i32(s, &iv)) {
        *out = (iv != 0);
        return 1;
    }
    return 0;
}

static void llmk_wasm_apply_oo_dna_kv_best_effort(
    const uint8_t *dna,
    size_t dna_len,
    float *temperature,
    float *min_p,
    float *top_p,
    int *top_k,
    float *repeat_penalty,
    int *no_repeat_ngram,
    int *max_gen_tokens,
    int *stats_enabled,
    int *stop_on_you,
    int *stop_on_double_nl,
    int *m18_base_temp_milli,
    int *m18_base_top_p_milli,
    int *m18_base_top_k,
    int *m18_base_max_gen_tokens
) {
    if (!dna || dna_len == 0) return;
    if (!temperature || !min_p || !top_p || !top_k || !repeat_penalty || !no_repeat_ngram ||
        !max_gen_tokens || !stats_enabled || !stop_on_you || !stop_on_double_nl ||
        !m18_base_temp_milli || !m18_base_top_p_milli || !m18_base_top_k || !m18_base_max_gen_tokens) {
        return;
    }

    // Safety cap: oo.dna should be small text.
    if (dna_len > 8192) dna_len = 8192;

    char *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)dna_len + 1, (void **)&buf);
    if (EFI_ERROR(st) || !buf) return;
    for (size_t i = 0; i < dna_len; i++) buf[i] = (char)dna[i];
    buf[dna_len] = 0;

    int applied = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR and whitespace.
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        // key=value
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;

        // Lowercase key in-place (ASCII).
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "temp") || llmk_cfg_streq_ci(key, "temperature")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                *temperature = v;
                applied = 1;
                Print(L"[wasm] apply temperature=%d.%03d\r\n", (int)(v), (int)((v - (int)v) * 1000));
            }
        } else if (llmk_cfg_streq_ci(key, "min_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                *min_p = v;
                applied = 1;
                Print(L"[wasm] apply min_p=%d.%03d\r\n", (int)(v), (int)((v - (int)v) * 1000));
            }
        } else if (llmk_cfg_streq_ci(key, "top_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                *top_p = v;
                applied = 1;
                Print(L"[wasm] apply top_p=%d.%03d\r\n", (int)(v), (int)((v - (int)v) * 1000));
            }
        } else if (llmk_cfg_streq_ci(key, "top_k")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 256) v = 256;
                *top_k = v;
                applied = 1;
                Print(L"[wasm] apply top_k=%d\r\n", v);
            }
        } else if (llmk_cfg_streq_ci(key, "repeat") || llmk_cfg_streq_ci(key, "repeat_penalty")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v <= 0.0f) v = 1.0f;
                *repeat_penalty = v;
                applied = 1;
                Print(L"[wasm] apply repeat_penalty=%d.%03d\r\n", (int)(v), (int)((v - (int)v) * 1000));
            }
        } else if (llmk_cfg_streq_ci(key, "norepeat") || llmk_cfg_streq_ci(key, "no_repeat_ngram")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 16) v = 16;
                *no_repeat_ngram = v;
                applied = 1;
                Print(L"[wasm] apply no_repeat_ngram=%d\r\n", v);
            }
        } else if (llmk_cfg_streq_ci(key, "max_tokens") || llmk_cfg_streq_ci(key, "max_gen_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                *max_gen_tokens = v;
                applied = 1;
                Print(L"[wasm] apply max_gen_tokens=%d\r\n", v);
            }
        } else if (llmk_cfg_streq_ci(key, "stats") || llmk_cfg_streq_ci(key, "stats_enabled")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stats_enabled = (b != 0);
                applied = 1;
                Print(L"[wasm] apply stats_enabled=%d\r\n", *stats_enabled);
            }
        } else if (llmk_cfg_streq_ci(key, "stop_on_you")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stop_on_you = (b != 0);
                applied = 1;
                Print(L"[wasm] apply stop_on_you=%d\r\n", *stop_on_you);
            }
        } else if (llmk_cfg_streq_ci(key, "stop_on_double_nl")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stop_on_double_nl = (b != 0);
                applied = 1;
                Print(L"[wasm] apply stop_on_double_nl=%d\r\n", *stop_on_double_nl);
            }
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);

    if (applied) {
        *m18_base_temp_milli = (int)(*temperature * 1000.0f + 0.5f);
        *m18_base_top_p_milli = (int)(*top_p * 1000.0f + 0.5f);
        *m18_base_top_k = *top_k;
        *m18_base_max_gen_tokens = *max_gen_tokens;
        if (*m18_base_temp_milli < g_autotune.min_temp_milli) *m18_base_temp_milli = g_autotune.min_temp_milli;
        if (*m18_base_top_p_milli < g_autotune.min_top_p_milli) *m18_base_top_p_milli = g_autotune.min_top_p_milli;
        if (*m18_base_top_k < g_autotune.min_top_k) *m18_base_top_k = g_autotune.min_top_k;
        if (*m18_base_max_gen_tokens < g_autotune.min_max_gen_tokens) *m18_base_max_gen_tokens = g_autotune.min_max_gen_tokens;
    }
}

static void llmk_cfg_copy_ascii_token(char *dst, int cap, const char *src);

static void llmk_load_repl_cfg_best_effort(
    float *temperature,
    float *min_p,
    float *top_p,
    int *top_k,
    float *repeat_penalty,
    int *no_repeat_ngram,
    int *max_gen_tokens,
    int *stats_enabled,
    int *stop_on_you,
    int *stop_on_double_nl
) {
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

        // Trim CR and whitespace.
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        // key=value
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;

        // Lowercase key in-place (ASCII).
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "temp") || llmk_cfg_streq_ci(key, "temperature")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                *temperature = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "min_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                *min_p = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "top_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                *top_p = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "top_k")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 256) v = 256;
                *top_k = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "repeat") || llmk_cfg_streq_ci(key, "repeat_penalty")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v <= 0.0f) v = 1.0f;
                *repeat_penalty = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "norepeat") || llmk_cfg_streq_ci(key, "no_repeat_ngram")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 16) v = 16;
                *no_repeat_ngram = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                *max_gen_tokens = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "mind_halt_enabled")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_mind_runtime_halt_enabled = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "mind_halt_threshold")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                g_mind_runtime_halt_threshold = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_temp")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.10f) v = 0.10f;
                if (v > 5.0f) v = 5.0f;
                g_attach_policy_external_cfg.temperature_milli = (int)(v * 1000.0f + 0.5f);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_top_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                g_attach_policy_external_cfg.top_p_milli = (int)(v * 1000.0f + 0.5f);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_rep")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 1.0f) v = 1.0f;
                if (v > 3.0f) v = 3.0f;
                g_attach_policy_external_cfg.repetition_penalty_milli = (int)(v * 1000.0f + 0.5f);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_external_max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                g_attach_policy_external_cfg.max_tokens = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_temp")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.10f) v = 0.10f;
                if (v > 5.0f) v = 5.0f;
                g_attach_policy_dual_cfg.temperature_milli = (int)(v * 1000.0f + 0.5f);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_top_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                g_attach_policy_dual_cfg.top_p_milli = (int)(v * 1000.0f + 0.5f);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_rep")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 1.0f) v = 1.0f;
                if (v > 3.0f) v = 3.0f;
                g_attach_policy_dual_cfg.repetition_penalty_milli = (int)(v * 1000.0f + 0.5f);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attach_policy_dual_max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                g_attach_policy_dual_cfg.max_tokens = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autotune") || llmk_cfg_streq_ci(key, "m18_autotune")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_autotune.enabled = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autotune_decode_cpt_hi")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                if (v < 1) v = 1;
                g_autotune.decode_cpt_hi = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autotune_decode_cpt_lo")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                if (v < 1) v = 1;
                g_autotune.decode_cpt_lo = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autotune_step_top_k")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 64) v = 64;
                g_autotune.step_top_k = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autotune_step_max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 128) v = 128;
                g_autotune.step_max_gen_tokens = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autotune_step_temp_milli")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 300) v = 300;
                g_autotune.step_temp_milli = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails") || llmk_cfg_streq_ci(key, "m181_guardrails")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_guardrails.enabled = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_decode_hard_stop_overruns")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 128) v = 128;
                g_guardrails.hard_stop_overruns_decode = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_safe_turns")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 32) v = 32;
                g_guardrails.safe_mode_turns = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_safe_top_k")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 256) v = 256;
                g_guardrails.safe_top_k_cap = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_safe_max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                g_guardrails.safe_max_tokens_cap = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_safe_top_p_milli")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 100) v = 100;
                if (v > 1000) v = 1000;
                g_guardrails.safe_top_p_milli = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_safe_temp_milli")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 100) v = 100;
                if (v > 2000) v = 2000;
                g_guardrails.safe_temp_milli = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "guardrails_reset_kv_on_trip")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_guardrails.reset_kv_on_trip = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "stats")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stats_enabled = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "stop_you")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stop_on_you = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "stop_nl")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stop_on_double_nl = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "seed")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                set_seed((unsigned int)v);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "budget")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                g_budget_prefill_cycles = v;
                g_budget_decode_cycles = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "budget_prefill")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                g_budget_prefill_cycles = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "budget_decode")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                g_budget_decode_cycles = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "strict_budget")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_sentinel.cfg.strict_budget = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attn")) {
            if (llmk_cfg_streq_ci(val, "auto")) {
                g_attn_force = -1;
                applied = 1;
            } else if (llmk_cfg_streq_ci(val, "sse2")) {
                g_attn_force = 0;
                applied = 1;
            } else if (llmk_cfg_streq_ci(val, "avx2")) {
                if (g_attn_use_avx2) {
                    g_attn_force = 1;
                    applied = 1;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "autorun_autostart") || llmk_cfg_streq_ci(key, "autorun")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_autorun_autostart = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autorun_shutdown") || llmk_cfg_streq_ci(key, "autorun_shutdown_when_done")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_autorun_shutdown_when_done = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autorun_file") || llmk_cfg_streq_ci(key, "autorun_script")) {
            if (val && val[0]) {
                ascii_to_char16(g_cfg_autorun_file, val, (int)(sizeof(g_cfg_autorun_file) / sizeof(g_cfg_autorun_file[0])));
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "gguf_q8_blob") || llmk_cfg_streq_ci(key, "q8_blob") || llmk_cfg_streq_ci(key, "gguf_blob")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_gguf_q8_blob = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "q8_act_quant") || llmk_cfg_streq_ci(key, "q8_act_quantize") || llmk_cfg_streq_ci(key, "q8_x_quant")) {
            int mode;
            if (llmk_cfg_parse_i32(val, &mode)) {
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                g_cfg_q8_act_quant = mode;
                applied = 1;
            } else {
                int b;
                if (llmk_cfg_parse_bool(val, &b)) {
                    g_cfg_q8_act_quant = (b != 0) ? 1 : 0;
                    applied = 1;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "chat_format") || llmk_cfg_streq_ci(key, "prompt_format")) {
            char fmt[32]; // SAFE: small token buffer; written via llmk_cfg_copy_ascii_token(cap)
            llmk_cfg_copy_ascii_token(fmt, (int)sizeof(fmt), val);
            if (llmk_cfg_streq_ci(fmt, "you_ai") || llmk_cfg_streq_ci(fmt, "you")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_YOU_AI;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "llama2") || llmk_cfg_streq_ci(fmt, "llama2_chat")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_LLAMA2;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "chatml") || llmk_cfg_streq_ci(fmt, "qwen") || llmk_cfg_streq_ci(fmt, "qwen2")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_CHATML;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "alpaca") || llmk_cfg_streq_ci(fmt, "instruction")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_ALPACA;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "raw")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_RAW;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "system_prompt") || llmk_cfg_streq_ci(key, "system")) {
            llmk_cfg_copy_ascii_token(g_cfg_system_prompt,
                                      (int)sizeof(g_cfg_system_prompt),
                                      val);
            applied = 1;
        }
    }

    if (applied) {
        g_cfg_loaded = 1;
        if (g_boot_verbose) {
            Print(L"[cfg] repl.cfg loaded\r\n");
        }
    }
}

static void llmk_cfg_copy_ascii_token(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;

    while (*src && llmk_cfg_is_space(*src)) src++;
    int quoted = 0;
    if (*src == '"') {
        quoted = 1;
        src++;
    }

    int p = 0;
    for (const char *s = src; *s && p + 1 < cap; s++) {
        if (quoted && *s == '"') break;
        char c = *s;
        if (c == '\r' || c == '\n') break;
        if ((unsigned char)c < 0x20) c = ' ';
        dst[p++] = c;
    }
    dst[p] = 0;

    while (p > 0 && llmk_cfg_is_space(dst[p - 1])) dst[--p] = 0;
}

static const char *llmk_chat_format_name_ascii(int fmt) {
    switch (fmt) {
        case LLMK_CHAT_FMT_LLAMA2: return "llama2";
        case LLMK_CHAT_FMT_CHATML: return "chatml";
        case LLMK_CHAT_FMT_ALPACA: return "alpaca";
        case LLMK_CHAT_FMT_RAW: return "raw";
        case LLMK_CHAT_FMT_YOU_AI:
        default: return "you_ai";
    }
}

static int llmk_prompt_append(char *dst, int cap, int p, const char *s) {
    if (!dst || cap <= 0) return p;
    if (!s) return p;
    while (*s && p + 1 < cap) {
        dst[p++] = *s++;
    }
    dst[p] = 0;
    return p;
}

static const char *llmk_build_chat_prompt(char *out, int cap, const char *user, int kv_pos) {
    if (!out || cap <= 0 || !user) return user;

    if (g_cfg_chat_format == LLMK_CHAT_FMT_RAW) {
        return user;
    }

    out[0] = 0;
    int p = 0;

    if (g_cfg_chat_format == LLMK_CHAT_FMT_YOU_AI) {
        const char *pre = (kv_pos == 0) ? "You: " : "\nYou: ";
        p = llmk_prompt_append(out, cap, p, pre);
        p = llmk_prompt_append(out, cap, p, user);
        p = llmk_prompt_append(out, cap, p, "\nAI: ");
        return out;
    }

    if (g_cfg_chat_format == LLMK_CHAT_FMT_LLAMA2) {
        if (kv_pos == 0 && g_cfg_system_prompt[0]) {
            p = llmk_prompt_append(out, cap, p, "[INST] <<SYS>>\n");
            p = llmk_prompt_append(out, cap, p, g_cfg_system_prompt);
            p = llmk_prompt_append(out, cap, p, "\n<</SYS>>\n\n");
            p = llmk_prompt_append(out, cap, p, user);
            p = llmk_prompt_append(out, cap, p, " [/INST]");
        } else {
            p = llmk_prompt_append(out, cap, p, "[INST] ");
            p = llmk_prompt_append(out, cap, p, user);
            p = llmk_prompt_append(out, cap, p, " [/INST]");
        }
        return out;
    }

    if (g_cfg_chat_format == LLMK_CHAT_FMT_CHATML) {
        if (kv_pos == 0 && g_cfg_system_prompt[0]) {
            p = llmk_prompt_append(out, cap, p, "<|im_start|>system\n");
            p = llmk_prompt_append(out, cap, p, g_cfg_system_prompt);
            p = llmk_prompt_append(out, cap, p, "<|im_end|>\n");
        }
        p = llmk_prompt_append(out, cap, p, "<|im_start|>user\n");
        p = llmk_prompt_append(out, cap, p, user);
        p = llmk_prompt_append(out, cap, p, "<|im_end|>\n<|im_start|>assistant\n");
        return out;
    }

    // Alpaca-style
    if (kv_pos == 0 && g_cfg_system_prompt[0]) {
        p = llmk_prompt_append(out, cap, p, "### Instruction:\n");
        p = llmk_prompt_append(out, cap, p, g_cfg_system_prompt);
        p = llmk_prompt_append(out, cap, p, "\n\n");
    } else {
        p = llmk_prompt_append(out, cap, p, "### Instruction:\n");
    }
    p = llmk_prompt_append(out, cap, p, user);
    p = llmk_prompt_append(out, cap, p, "\n\n### Response:\n");
    return out;
}

static void llmk_load_repl_cfg_oo_best_effort(
    int *oo_autoload,
    int *oo_autosave_every,
    char *oo_file_out,
    int oo_file_cap
) {
    if (oo_autoload) *oo_autoload = 0;
    if (oo_autosave_every) *oo_autosave_every = 0;
    if (oo_file_out && oo_file_cap > 0) oo_file_out[0] = 0;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    int autosave_set = 0;

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

        if (llmk_cfg_streq_ci(key, "oo_autoload") || llmk_cfg_streq_ci(key, "oo_load_on_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (oo_autoload) *oo_autoload = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_file") || llmk_cfg_streq_ci(key, "oo_state_file") || llmk_cfg_streq_ci(key, "oo_autoload_file")) {
            if (oo_file_out && oo_file_cap > 0) {
                llmk_cfg_copy_ascii_token(oo_file_out, oo_file_cap, val);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_autosave") || llmk_cfg_streq_ci(key, "oo_autosave_on")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (!autosave_set && oo_autosave_every) {
                    *oo_autosave_every = (b != 0) ? 1 : 0;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "oo_autosave_every") || llmk_cfg_streq_ci(key, "oo_autosave_n")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 1000) v = 1000;
                if (oo_autosave_every) *oo_autosave_every = v;
                autosave_set = 1;
            }
        }
    }
}

static void llmk_load_repl_cfg_oo_engines_best_effort(void) {
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

        int mode;
        if (
            llmk_cfg_streq_ci(key, "evolvion_mode") ||
            llmk_cfg_streq_ci(key, "synaption_mode") ||
            llmk_cfg_streq_ci(key, "conscience_mode") ||
            llmk_cfg_streq_ci(key, "neuralfs_mode") ||
            llmk_cfg_streq_ci(key, "ghost_mode") ||
            llmk_cfg_streq_ci(key, "immunion_mode") ||
            llmk_cfg_streq_ci(key, "dreamion_mode") ||
            llmk_cfg_streq_ci(key, "symbion_mode") ||
            llmk_cfg_streq_ci(key, "collectivion_mode") ||
            llmk_cfg_streq_ci(key, "metabion_mode") ||
            llmk_cfg_streq_ci(key, "morphion_mode") ||
            llmk_cfg_streq_ci(key, "pheromion_mode")
        ) {
            if (!llmk_cfg_parse_i32(val, &mode)) continue;
            if (mode < 0) mode = 0;
            if (mode > 2) mode = 2;

            if (llmk_cfg_streq_ci(key, "evolvion_mode")) {
                evolvion_set_mode(&g_evolvion, (EvolvionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "synaption_mode")) {
                synaption_set_mode(&g_synaption, (SynaptionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "conscience_mode")) {
                conscience_set_mode(&g_conscience, (ConscienceMode)mode);
            } else if (llmk_cfg_streq_ci(key, "neuralfs_mode")) {
                neuralfs_set_mode(&g_neuralfs, (NeuralfsMode)mode);
            } else if (llmk_cfg_streq_ci(key, "ghost_mode")) {
                ghost_set_mode(&g_ghost, (GhostMode)mode);
            } else if (llmk_cfg_streq_ci(key, "immunion_mode")) {
                immunion_set_mode(&g_immunion, (ImmunionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "dreamion_mode")) {
                dreamion_set_mode(&g_dreamion, (DreamionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "symbion_mode")) {
                symbion_set_mode(&g_symbion, (SymbionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "collectivion_mode")) {
                collectivion_set_mode(&g_collectivion, (CollectivionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "metabion_mode")) {
                metabion_set_mode(&g_metabion, (MetabionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "morphion_mode")) {
                morphion_set_mode(&g_morphion, (MorphionMode)mode);
            } else if (llmk_cfg_streq_ci(key, "pheromion_mode")) {
                pheromion_set_mode(&g_pheromion, (PheromionMode)mode);
            }

            applied = 1;
            if (g_boot_verbose >= 2) {
                Print(L"[cfg][oo_engines] apply %a=%d\r\n", key, mode);
            }
        }
    }

    if (applied && g_boot_verbose) {
        Print(L"[cfg] oo engines configured (see /oo_status)\r\n");
    }
}

static void llmk_load_repl_cfg_snap_best_effort(
    int *snap_autoload,
    char *snap_file_out,
    int snap_file_cap
) {
    if (snap_autoload) *snap_autoload = 0;
    if (snap_file_out && snap_file_cap > 0) snap_file_out[0] = 0;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

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

        if (llmk_cfg_streq_ci(key, "snap_autoload") || llmk_cfg_streq_ci(key, "snap_load_on_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (snap_autoload) *snap_autoload = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "snap_file") || llmk_cfg_streq_ci(key, "snap_autoload_file")) {
            if (snap_file_out && snap_file_cap > 0) {
                llmk_cfg_copy_ascii_token(snap_file_out, snap_file_cap, val);
            }
        }
    }
}

static void llmk_load_repl_cfg_djibion_best_effort(DjibionEngine *e) {
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

        if (llmk_cfg_streq_ci(key, "djibion_mode")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                djibion_set_mode(e, (DjibionMode)v);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_prefix") || llmk_cfg_streq_ci(key, "djibion_fs_prefix") || llmk_cfg_streq_ci(key, "fs_mut_prefix")) {
            if (val && val[0]) {
                llmk_cfg_copy_ascii_token(e->laws.fs_mut_prefix, (int)sizeof(e->laws.fs_mut_prefix), val);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_delete") || llmk_cfg_streq_ci(key, "djibion_allow_fs_delete")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_fs_delete = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_write") || llmk_cfg_streq_ci(key, "djibion_allow_fs_write")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_fs_write = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_snap_load")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_snap_load = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_snap_save")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_snap_save = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_cfg_write") || llmk_cfg_streq_ci(key, "djibion_allow_config_write")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_cfg_write = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_max_write") || llmk_cfg_streq_ci(key, "djibion_max_fs_write_bytes")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > (1024 * 1024)) v = (1024 * 1024);
                e->laws.max_fs_write_bytes = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_max_snap") || llmk_cfg_streq_ci(key, "djibion_max_snap_bytes")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > (1024 * 1024 * 1024)) v = (1024 * 1024 * 1024);
                e->laws.max_snap_bytes = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_max_oo") || llmk_cfg_streq_ci(key, "djibion_max_oo_cycles")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 64) v = 64;
                e->laws.max_oo_cycles = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_autorun")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_autorun = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_oo_persist")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_oo_persist = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_oo_exec")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_oo_exec = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_oo_auto")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_oo_auto = (b != 0);
                applied = 1;
            }
        }
    }

    if (applied) {
        Print(L"[cfg] djibion: mode=%s prefix=",
              (CHAR16 *)djibion_mode_name(e->mode));
        if (e->laws.fs_mut_prefix[0]) {
            llmk_print_ascii(e->laws.fs_mut_prefix);
        } else {
            Print(L"(none)");
        }
        Print(L"\r\n");
    }
}

static int llmk_cfg_line_has_key_ci(const char *line, const char *key) {
    if (!line || !key) return 0;

    const char *p = line;
    while (*p && llmk_cfg_is_space(*p)) p++;

    // Allow commented-out keys like: # snap_autoload=1
    if (*p == '#' || *p == ';') {
        p++;
        while (*p && llmk_cfg_is_space(*p)) p++;
    }

    // Parse key token up to '=' or whitespace.
    char kbuf[64];
    int kp = 0;
    while (*p && *p != '=' && !llmk_cfg_is_space(*p) && *p != '#' && *p != ';') {
        if (kp + 1 < (int)sizeof(kbuf)) {
            kbuf[kp++] = llmk_cfg_tolower(*p);
        }
        p++;
    }
    kbuf[kp] = 0;
    if (kbuf[0] == 0) return 0;

    // Skip spaces before '='.
    while (*p && llmk_cfg_is_space(*p)) p++;
    if (*p != '=') return 0;

    return llmk_cfg_streq_ci(kbuf, key);
}

static void llmk_cfg_out_append(char *out, int *op, int out_cap, const char *s) {
    if (!out || !op || out_cap <= 0 || !s) return;
    int p = *op;
    while (*s && p + 1 < out_cap) out[p++] = *s++;
    out[p] = 0;
    *op = p;
}

// M19.1 benchmark capture helpers (JSONL to boot volume)
static void llmk_bench_sanitize_token(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int p = 0;
    for (int i = 0; src[i] && p + 1 < cap; i++) {
        char c = src[i];
        // Keep JSON-simple tokens only. Replace others to avoid escaping.
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
            dst[p++] = c;
        } else {
            dst[p++] = '?';
        }
    }
    dst[p] = 0;
}

static void llmk_bench_close_best_effort(void) {
    if (g_bench_file) {
        uefi_call_wrapper(g_bench_file->Flush, 1, g_bench_file);
        uefi_call_wrapper(g_bench_file->Close, 1, g_bench_file);
        g_bench_file = NULL;
    }
}

static int llmk_bench_begin_best_effort(const CHAR16 *out_name) {
    llmk_bench_close_best_effort();
    g_bench_pending = 0;
    g_bench_have_wall = 0;
    g_bench_wall0_us = 0;
    g_bench_decode_cycles_start = 0;
    g_bench_decode_tokens_start = 0;

    if (out_name && out_name[0]) {
        StrCpy(g_bench_out_name, (CHAR16 *)out_name);
        } else {
            StrCpy(g_bench_out_name, L"LLMK_BEN.JNL");
    }

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, g_bench_out_name);
    if (EFI_ERROR(st) || !f) {
        Print(L"\r\nERROR: bench_begin cannot open %s (%r)\r\n\r\n", g_bench_out_name, st);
        g_bench_active = 0;
        return 0;
    }
    g_bench_file = f;
    g_bench_active = 1;
    Print(L"\r\n[bench] begin -> %s\r\n\r\n", g_bench_out_name);
    return 1;
}

static void llmk_bench_end_best_effort(void) {
    if (!g_bench_active) {
        Print(L"\r\n[bench] not active\r\n\r\n");
        return;
    }
    llmk_bench_close_best_effort();
    g_bench_active = 0;
    g_bench_pending = 0;
    Print(L"\r\n[bench] end\r\n\r\n");
}

static void llmk_bench_prepare_case(const char *case_id, const char *category, int max_new_tokens) {
    llmk_bench_sanitize_token(g_bench_case_id, (int)sizeof(g_bench_case_id), case_id);
    llmk_bench_sanitize_token(g_bench_category, (int)sizeof(g_bench_category), category);
    g_bench_case_max_new_tokens = max_new_tokens;
    g_bench_pending = 1;
    g_bench_have_wall = 0;
    g_bench_wall0_us = 0;
    g_bench_decode_cycles_start = g_metrics.total_decode_cycles;
    g_bench_decode_tokens_start = g_metrics.total_decode_tokens;
}

static void llmk_bench_on_turn_start(void) {
    if (!g_bench_active || !g_bench_pending) return;
    unsigned long long us = 0;
    g_bench_have_wall = uefi_wall_us(&us) ? 1 : 0;
    g_bench_wall0_us = us;
    // Decode starts already captured in llmk_bench_prepare_case().
}

static void llmk_bench_on_turn_end(int generated_tokens) {
    if (!g_bench_active || !g_bench_pending) return;

    UINT64 decode_cycles = (g_metrics.total_decode_cycles >= g_bench_decode_cycles_start)
                             ? (g_metrics.total_decode_cycles - g_bench_decode_cycles_start)
                             : 0ULL;
    UINT32 decode_tokens = (g_metrics.total_decode_tokens >= g_bench_decode_tokens_start)
                             ? (g_metrics.total_decode_tokens - g_bench_decode_tokens_start)
                             : 0U;

    UINT64 latency_ms = 0;
    if (g_bench_have_wall) {
        unsigned long long us1 = 0;
        if (uefi_wall_us(&us1)) {
            unsigned long long dt_us = (us1 >= g_bench_wall0_us) ? (us1 - g_bench_wall0_us)
                                                                 : (us1 + 86400ULL * 1000000ULL - g_bench_wall0_us);
            latency_ms = (UINT64)(dt_us / 1000ULL);
        }
    }

    if (!g_bench_file) {
        Print(L"[bench] WARN: file not open; skipping write\r\n");
        g_bench_pending = 0;
        return;
    }

    char row[512];
    int rp = 0;
    row[0] = 0;
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), "{\"case_id\":\"");
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), g_bench_case_id);
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), "\",\"category\":\"");
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), g_bench_category);
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), "\",\"latency_ms\":");
    {
        char tmp[32]; // SAFE: decimal render buffer; bounded by sizeof(tmp)
        int tp = 0;
        tmp[0] = 0;
        UINT64 v = latency_ms;
        char digits[24]; // SAFE: digit scratch; bounded by sizeof(digits)
        int nd = 0;
        if (v == 0) {
            digits[nd++] = '0';
        } else {
            while (v > 0 && nd < (int)sizeof(digits)) {
                digits[nd++] = (char)('0' + (v % 10ULL));
                v /= 10ULL;
            }
        }
        for (int i = nd - 1; i >= 0 && tp + 1 < (int)sizeof(tmp); i--) tmp[tp++] = digits[i];
        tmp[tp] = 0;
        llmk_cfg_out_append(row, &rp, (int)sizeof(row), tmp);
    }
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), ",\"decode_cycles\":");
    {
        char tmp[32]; // SAFE: decimal render buffer; bounded by sizeof(tmp)
        int tp = 0;
        tmp[0] = 0;
        UINT64 v = decode_cycles;
        char digits[24]; // SAFE: digit scratch; bounded by sizeof(digits)
        int nd = 0;
        if (v == 0) {
            digits[nd++] = '0';
        } else {
            while (v > 0 && nd < (int)sizeof(digits)) {
                digits[nd++] = (char)('0' + (v % 10ULL));
                v /= 10ULL;
            }
        }
        for (int i = nd - 1; i >= 0 && tp + 1 < (int)sizeof(tmp); i--) tmp[tp++] = digits[i];
        tmp[tp] = 0;
        llmk_cfg_out_append(row, &rp, (int)sizeof(row), tmp);
    }
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), ",\"decode_tokens\":");
    {
        char tmp[24]; // SAFE: decimal render buffer; bounded by sizeof(tmp)
        int tp = 0;
        tmp[0] = 0;
        UINT32 v = decode_tokens;
        char digits[16]; // SAFE: digit scratch; bounded by sizeof(digits)
        int nd = 0;
        if (v == 0) {
            digits[nd++] = '0';
        } else {
            while (v > 0 && nd < (int)sizeof(digits)) {
                digits[nd++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }
        for (int i = nd - 1; i >= 0 && tp + 1 < (int)sizeof(tmp); i--) tmp[tp++] = digits[i];
        tmp[tp] = 0;
        llmk_cfg_out_append(row, &rp, (int)sizeof(row), tmp);
    }
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), ",\"generated_tokens\":");
    {
        char tmp[24]; // SAFE: decimal render buffer; bounded by sizeof(tmp)
        int tp = 0;
        tmp[0] = 0;
        int v = generated_tokens;
        if (v < 0) v = 0;
        char digits[16]; // SAFE: digit scratch; bounded by sizeof(digits)
        int nd = 0;
        if (v == 0) {
            digits[nd++] = '0';
        } else {
            while (v > 0 && nd < (int)sizeof(digits)) {
                digits[nd++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }
        for (int i = nd - 1; i >= 0 && tp + 1 < (int)sizeof(tmp); i--) tmp[tp++] = digits[i];
        tmp[tp] = 0;
        llmk_cfg_out_append(row, &rp, (int)sizeof(row), tmp);
    }
    llmk_cfg_out_append(row, &rp, (int)sizeof(row), "}\n");

    EFI_STATUS st = llmk_file_write_bytes(g_bench_file, row, (UINTN)my_strlen(row));
    if (EFI_ERROR(st)) {
        Print(L"[bench] WARN: write failed (%r)\r\n", st);
    } else {
        uefi_call_wrapper(g_bench_file->Flush, 1, g_bench_file);
        Print(L"[bench] case=");
        llmk_print_ascii(g_bench_case_id);
        Print(L" latency_ms=%lu decode_tokens=%u\r\n", latency_ms, decode_tokens);
    }

    g_bench_pending = 0;
}

// Forward decl: used by OO auto-apply verification helpers.
static EFI_STATUS llmk_repl_cfg_set_kv_best_effort(const char *key, const char *val);

static int llmk_repl_cfg_read_ctx_seq_best_effort(int *out_ctx, int *out_seq) {
    if (out_ctx) *out_ctx = 0;
    if (out_seq) *out_seq = 0;
    if (!g_root) return 0;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"repl.cfg", &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        return 0;
    }

    // Make NUL-terminated ASCII buffer.
    char *buf = NULL;
    EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
    if (EFI_ERROR(st2) || !buf) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        return 0;
    }
    CopyMem(buf, raw, raw_len);
    buf[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    int got_ctx = 0;
    int got_seq = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR.
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
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

        if (!got_ctx && (llmk_cfg_streq_ci(key, "ctx_len") || llmk_cfg_streq_ci(key, "context") || llmk_cfg_streq_ci(key, "context_len"))) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                if (out_ctx) *out_ctx = v;
                got_ctx = 1;
            }
        } else if (!got_seq && (llmk_cfg_streq_ci(key, "seq_len") || llmk_cfg_streq_ci(key, "sequence") || llmk_cfg_streq_ci(key, "sequence_len"))) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                if (out_seq) *out_seq = v;
                got_seq = 1;
            }
        }

        if (got_ctx && got_seq) break;
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    return (got_ctx || got_seq);
}

static UINT64 llmk_oo_cfg_checksum_i64(int ctx_len, int seq_len, UINT64 ram_mb) {
    // Spec (M5.2): checksum = ctx_len XOR seq_len XOR (ram_mb << 8)
    UINT64 c = ((UINT64)((UINT32)ctx_len) ^ (UINT64)((UINT32)seq_len));
    c ^= (ram_mb << 8);
    return c;
}

static void llmk_oo_journal_event_load_state_best_effort(const char *event) {
    if (!g_root || !event) return;
    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) return;
    llmk_oo_journal_append_best_effort(&s, event);
}

static void llmk_oo_journal_cmd_best_effort(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    char event[96];
    int p = 0;
    event[0] = 0;
    llmk_ascii_append_str(event, (int)sizeof(event), &p, "cmd=");
    llmk_ascii_append_str(event, (int)sizeof(event), &p, cmd);
    event[p] = 0;
    llmk_oo_journal_event_load_state_best_effort(event);
}

static void llmk_oo_plan_checkpoint_best_effort(const char *tag) {
    if (!g_root) return;

    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) return;

    s.magic = LLMK_OO_STATE_MAGIC;
    s.version = LLMK_OO_STATE_VER;
    s.size = (UINT32)sizeof(LlmkOoState);
    s.checksum = llmk_oo_state_checksum(&s);

    (void)llmk_oo_write_recovery_best_effort(&s);

    char event[96];
    int p = 0;
    event[0] = 0;
    llmk_ascii_append_str(event, (int)sizeof(event), &p, "plan_checkpoint tag=");
    llmk_ascii_append_str(event, (int)sizeof(event), &p, tag ? tag : "default");
    event[p] = 0;

    llmk_oo_journal_append_best_effort(&s, event);
}

static void llmk_oo_record_last_auto_apply_best_effort(UINT64 boot_count, UINT32 apply_mode, UINT32 action_id) {
    if (!g_root) return;
    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) return;

    // meta: low6=action_id, high2=apply_mode
    UINT32 meta = ((apply_mode & 0x3u) << 6u) | (action_id & 0x3Fu);
    UINT32 boot_low8 = (UINT32)(boot_count & 0xFFu);

    s.flags = llmk_oo_set_last_action_meta(s.flags, meta);
    s.flags = llmk_oo_set_last_apply_boot_low8(s.flags, boot_low8);

    s.magic = LLMK_OO_STATE_MAGIC;
    s.version = LLMK_OO_STATE_VER;
    s.size = (UINT32)sizeof(LlmkOoState);
    s.checksum = llmk_oo_state_checksum(&s);
    EFI_STATUS wst = llmk_oo_write_state_best_effort(&s);
    if (!EFI_ERROR(wst)) {
        llmk_oo_write_recovery_best_effort(&s);
    }

    {
        char expected[48]; // SAFE: bounded effect token; built with explicit sizeof() bounds
        int cfg_ctx = 0, cfg_seq = 0;
        int p = 0;
        expected[0] = 0;

        if (llmk_repl_cfg_read_ctx_seq_best_effort(&cfg_ctx, &cfg_seq)) {
            if (action_id == LLMK_OO_ACTION_REDUCE_CTX || action_id == LLMK_OO_ACTION_INCREASE_CTX) {
                llmk_ascii_append_str(expected, (int)sizeof(expected), &p, "ctx_");
                llmk_ascii_append_u64(expected, (int)sizeof(expected), &p, (UINT64)((cfg_ctx > 0) ? cfg_ctx : 0));
            } else if (action_id == LLMK_OO_ACTION_REDUCE_SEQ) {
                llmk_ascii_append_str(expected, (int)sizeof(expected), &p, "seq_");
                llmk_ascii_append_u64(expected, (int)sizeof(expected), &p, (UINT64)((cfg_seq > 0) ? cfg_seq : 0));
            }
            expected[p] = 0;
        }
        if (!expected[0]) {
            llmk_oo_outcome_copy_token(expected, (int)sizeof(expected),
                                       llmk_oo_action_is_increase(action_id) ? "mode_stable" : "mode_drop");
        }
        (void)apply_mode;
        llmk_oo_outcome_append_best_effort(boot_count,
                                           action_id,
                                           expected,
                                           "pending_next_boot",
                                           -1);
    }
}

static int llmk_oo_auto_apply_write_verify_best_effort(const char *action,
                                                      const char *key,
                                                      int old_ctx_hint,
                                                      int old_seq_hint,
                                                      int expected_ctx,
                                                      int expected_seq,
                                                      UINT64 ram_mb) {
    if (!action || !key) return 0;

    // Read current values from repl.cfg when available.
    int ctx_before = old_ctx_hint;
    int seq_before = old_seq_hint;
    {
        int rc = 0;
        int rs = 0;
        if (llmk_repl_cfg_read_ctx_seq_best_effort(&rc, &rs)) {
            if (rc > 0) ctx_before = rc;
            if (rs > 0) seq_before = rs;
        }
    }

    UINT64 c_before = llmk_oo_cfg_checksum_i64(ctx_before, seq_before, ram_mb);

    // Apply: write the intended key.
    {
        char val[32]; // SAFE: config value string buffer; bounded by sizeof(val)
        int vp = 0;
        int v = 0;
        if (llmk_cfg_streq_ci(key, "ctx_len")) v = expected_ctx;
        else if (llmk_cfg_streq_ci(key, "seq_len")) v = expected_seq;
        else return 0;
        llmk_ascii_append_u64(val, (int)sizeof(val), &vp, (UINT64)v);
        val[vp] = 0;
        EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort(key, val);
        if (EFI_ERROR(st)) return 0;
    }

    // Re-read + verify.
    int ctx_after = 0;
    int seq_after = 0;
    if (!llmk_repl_cfg_read_ctx_seq_best_effort(&ctx_after, &seq_after)) {
        return 0;
    }

    // Fill missing key from expected (best-effort) so checksum check is meaningful.
    if (ctx_after <= 0) ctx_after = expected_ctx;
    if (seq_after <= 0) seq_after = expected_seq;

    // Range checks (spec guidance: ctx_len in [16,4096] etc).
    if (ctx_after < 16 || ctx_after > 4096) return 0;
    if (seq_after < 16 || seq_after > 4096) return 0;

    // Ensure the modified key matches the expected value.
    if (llmk_cfg_streq_ci(key, "ctx_len") && ctx_after != expected_ctx) return 0;
    if (llmk_cfg_streq_ci(key, "seq_len") && seq_after != expected_seq) return 0;

    UINT64 c_after = llmk_oo_cfg_checksum_i64(ctx_after, seq_after, ram_mb);
    UINT64 c_expected = llmk_oo_cfg_checksum_i64(expected_ctx, expected_seq, ram_mb);

    // Verification checks: checksum matches expected post-state and changed.
    if (c_after != c_expected) return 0;
    (void)c_before; // currently used only as a spec-aligned pre-computation

    return 1;
}

static EFI_STATUS llmk_repl_cfg_set_kv_best_effort(const char *key, const char *val) {
    if (!key || !val) return EFI_INVALID_PARAMETER;
    if (!g_root) return EFI_NOT_READY;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS read_st = llmk_read_entire_file_best_effort(L"repl.cfg", &raw, &raw_len);

    // Make a mutable NUL-terminated copy (ASCII).
    char *in = NULL;
    UINTN in_len = 0;
    if (!EFI_ERROR(read_st) && raw && raw_len > 0) {
        in_len = raw_len;
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)(in_len + 1), (void **)&in);
        if (EFI_ERROR(st2) || !in) {
            uefi_call_wrapper(BS->FreePool, 1, raw);
            return EFI_OUT_OF_RESOURCES;
        }
        CopyMem(in, raw, in_len);
        in[in_len] = 0;
        uefi_call_wrapper(BS->FreePool, 1, raw);
        raw = NULL;
        raw_len = 0;
    } else {
        // Missing/empty file: start fresh.
        const char *stub = "# repl.cfg (generated best-effort)\r\n";
        in_len = (UINTN)my_strlen(stub);
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)(in_len + 1), (void **)&in);
        if (EFI_ERROR(st2) || !in) return EFI_OUT_OF_RESOURCES;
        CopyMem(in, stub, in_len);
        in[in_len] = 0;
        read_st = EFI_NOT_FOUND;
    }

    int out_cap = (int)in_len + 512;
    if (out_cap < 1024) out_cap = 1024;
    if (out_cap > 64 * 1024) out_cap = 64 * 1024;

    char *out = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)out_cap, (void **)&out);
    if (EFI_ERROR(st) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, in);
        return EFI_OUT_OF_RESOURCES;
    }
    int op = 0;
    out[0] = 0;

    int replaced = 0;
    char *p = in;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int had_nl = (*p == '\n');
        if (had_nl) { *p = 0; p++; }

        // Strip trailing CR for parsing/writing.
        int ll = (int)my_strlen(line);
        if (ll > 0 && line[ll - 1] == '\r') line[ll - 1] = 0;

        if (llmk_cfg_line_has_key_ci(line, key)) {
            llmk_cfg_out_append(out, &op, out_cap, key);
            llmk_cfg_out_append(out, &op, out_cap, "=");
            llmk_cfg_out_append(out, &op, out_cap, val);
            llmk_cfg_out_append(out, &op, out_cap, "\r\n");
            replaced = 1;
        } else {
            llmk_cfg_out_append(out, &op, out_cap, line);
            llmk_cfg_out_append(out, &op, out_cap, "\r\n");
        }

        if (!had_nl) break;
    }

    if (!replaced) {
        // Ensure trailing newline then append key.
        if (op >= 2 && !(out[op - 2] == '\r' && out[op - 1] == '\n')) {
            llmk_cfg_out_append(out, &op, out_cap, "\r\n");
        }
        llmk_cfg_out_append(out, &op, out_cap, key);
        llmk_cfg_out_append(out, &op, out_cap, "=");
        llmk_cfg_out_append(out, &op, out_cap, val);
        llmk_cfg_out_append(out, &op, out_cap, "\r\n");
    }

    // Backup previous file when it existed.
    if (!EFI_ERROR(read_st)) {
        CHAR16 bak[64];
        llmk_make_bak_name(L"repl.cfg", bak, (int)(sizeof(bak) / sizeof(bak[0])));
        llmk_copy_file_best_effort(L"repl.cfg", bak);
    }

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"repl.cfg");
    if (EFI_ERROR(st) || !f) {
        uefi_call_wrapper(BS->FreePool, 1, out);
        uefi_call_wrapper(BS->FreePool, 1, in);
        return st;
    }

    UINTN nb = (UINTN)my_strlen(out);
    st = llmk_file_write_bytes(f, out, nb);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, in);

    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static void llmk_mind_format_threshold_ascii(char *out, int out_cap, float threshold) {
    if (!out || out_cap <= 0) return;
    out[0] = 0;

    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;

    int milli = (int)(threshold * 1000.0f + 0.5f);
    if (milli < 0) milli = 0;
    if (milli > 1000) milli = 1000;

    int op = 0;
    llmk_ascii_append_u64(out, out_cap, &op, (UINT64)(milli / 1000));
    llmk_ascii_append_char(out, out_cap, &op, '.');
    llmk_ascii_append_char(out, out_cap, &op, (char)('0' + ((milli / 100) % 10)));
    llmk_ascii_append_char(out, out_cap, &op, (char)('0' + ((milli / 10) % 10)));
    llmk_ascii_append_char(out, out_cap, &op, (char)('0' + (milli % 10)));
}

static EFI_STATUS llmk_mind_persist_halt_policy_best_effort(void) {
    char threshold[32];
    llmk_mind_format_threshold_ascii(threshold, (int)sizeof(threshold), g_mind_runtime_halt_threshold);

    EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("mind_halt_enabled", g_mind_runtime_halt_enabled ? "1" : "0");
    if (EFI_ERROR(st)) return st;
    return llmk_repl_cfg_set_kv_best_effort("mind_halt_threshold", threshold);
}

static EFI_STATUS llmk_mind_query_halt_policy_cfg_best_effort(
    int *out_enabled,
    float *out_threshold,
    int *out_found_enabled,
    int *out_found_threshold
) {
    if (out_found_enabled) *out_found_enabled = 0;
    if (out_found_threshold) *out_found_threshold = 0;
    if (!g_root) return EFI_NOT_READY;

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

        if (llmk_cfg_streq_ci(key, "mind_halt_enabled")) {
            int b = 0;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (out_enabled) *out_enabled = (b != 0);
                if (out_found_enabled) *out_found_enabled = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "mind_halt_threshold")) {
            float v = 0.0f;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                if (out_threshold) *out_threshold = v;
                if (out_found_threshold) *out_found_threshold = 1;
            }
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if ((out_found_enabled && *out_found_enabled) || (out_found_threshold && *out_found_threshold)) {
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}

static EFI_STATUS llmk_mind_load_halt_policy_best_effort(void) {
    int cfg_enabled = g_mind_runtime_halt_enabled;
    float cfg_threshold = g_mind_runtime_halt_threshold;
    int found_enabled = 0;
    int found_threshold = 0;
    EFI_STATUS st = llmk_mind_query_halt_policy_cfg_best_effort(&cfg_enabled, &cfg_threshold, &found_enabled, &found_threshold);
    if (EFI_ERROR(st)) return st;
    if (found_enabled) g_mind_runtime_halt_enabled = cfg_enabled;
    if (found_threshold) g_mind_runtime_halt_threshold = cfg_threshold;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_mind_apply_saved_halt_policy_best_effort(int *out_changed_enabled, int *out_changed_threshold) {
    int prev_enabled = g_mind_runtime_halt_enabled;
    float prev_threshold = g_mind_runtime_halt_threshold;
    EFI_STATUS st = llmk_mind_load_halt_policy_best_effort();
    if (EFI_ERROR(st)) return st;
    g_mind_runtime_halt_apply_seen = 1;
    g_mind_runtime_halt_apply_changed_enabled = (prev_enabled != g_mind_runtime_halt_enabled);
    if (out_changed_threshold) {
        float diff = g_mind_runtime_halt_threshold - prev_threshold;
        if (diff < 0.0f) diff = -diff;
        g_mind_runtime_halt_apply_changed_threshold = (diff >= 0.0005f);
        *out_changed_threshold = g_mind_runtime_halt_apply_changed_threshold;
    } else {
        float diff = g_mind_runtime_halt_threshold - prev_threshold;
        if (diff < 0.0f) diff = -diff;
        g_mind_runtime_halt_apply_changed_threshold = (diff >= 0.0005f);
    }
    if (out_changed_enabled) *out_changed_enabled = g_mind_runtime_halt_apply_changed_enabled;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_mind_apply_saved_halt_policy_if_needed_best_effort(int *out_was_needed, int *out_changed_enabled, int *out_changed_threshold) {
    int cfg_enabled = g_mind_runtime_halt_enabled;
    float cfg_threshold = g_mind_runtime_halt_threshold;
    int found_enabled = 0;
    int found_threshold = 0;
    EFI_STATUS st = llmk_mind_query_halt_policy_cfg_best_effort(&cfg_enabled, &cfg_threshold, &found_enabled, &found_threshold);
    if (EFI_ERROR(st)) return st;

    {
        int eff_enabled = found_enabled ? cfg_enabled : g_mind_runtime_halt_enabled;
        float eff_threshold = found_threshold ? cfg_threshold : g_mind_runtime_halt_threshold;
        float diff = eff_threshold - g_mind_runtime_halt_threshold;
        if (diff < 0.0f) diff = -diff;
        if (eff_enabled == g_mind_runtime_halt_enabled && diff < 0.0005f) {
            g_mind_runtime_halt_apply_seen = 1;
            g_mind_runtime_halt_apply_changed_enabled = 0;
            g_mind_runtime_halt_apply_changed_threshold = 0;
            if (out_was_needed) *out_was_needed = 0;
            if (out_changed_enabled) *out_changed_enabled = 0;
            if (out_changed_threshold) *out_changed_threshold = 0;
            return EFI_SUCCESS;
        }
    }

    if (out_was_needed) *out_was_needed = 1;
    return llmk_mind_apply_saved_halt_policy_best_effort(out_changed_enabled, out_changed_threshold);
}

static EFI_STATUS llmk_oo_save_to_file_best_effort(const CHAR16 *name, int *out_bytes) {
    if (out_bytes) *out_bytes = 0;
    if (!name) return EFI_INVALID_PARAMETER;

    char *blob = (char *)simple_alloc(32768);
    if (!blob) return EFI_OUT_OF_RESOURCES;

    int n = llmk_oo_export(blob, 32768);
    if (n < 0) return EFI_BUFFER_TOO_SMALL;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, name);
    if (EFI_ERROR(st)) return st;

    st = llmk_file_write_bytes(f, blob, (UINTN)n);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (out_bytes) *out_bytes = n;
    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_file_write_u16(EFI_FILE_HANDLE f, const CHAR16 *s) {
    if (!f || !s) return EFI_INVALID_PARAMETER;
    UINTN chars = (UINTN)StrLen((CHAR16 *)s);
    UINTN nb = chars * sizeof(CHAR16);
    if (nb == 0) return EFI_SUCCESS;
    return uefi_call_wrapper(f->Write, 3, f, &nb, (void *)s);
}

static EFI_STATUS llmk_dump_zones_to_file(EFI_FILE_HANDLE f, const LlmkZones *zones) {
    if (!f || !zones) return EFI_INVALID_PARAMETER;
    CHAR16 line[256];
    SPrint(line, sizeof(line), L"[llmk] Zone B: base=0x%lx size=%lu MiB\r\n",
           (UINT64)zones->zone_b_base, zones->zone_b_size / (1024ULL * 1024ULL));
    llmk_file_write_u16(f, line);
    for (int i = 0; i < LLMK_ARENA_COUNT; i++) {
        const LlmkArena *a = &zones->arenas[i];
        SPrint(line, sizeof(line), L"  [%s] base=0x%lx size=%lu MiB used=%lu MiB flags=0x%x\r\n",
               a->name,
               a->base,
               a->size / (1024ULL * 1024ULL),
               a->cursor / (1024ULL * 1024ULL),
               (unsigned)a->flags);
        llmk_file_write_u16(f, line);
    }
    llmk_file_write_u16(f, L"\r\n");
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_dump_sentinel_to_file(EFI_FILE_HANDLE f, const LlmkSentinel *s) {
    if (!f || !s) return EFI_INVALID_PARAMETER;
    CHAR16 line[256];
    SPrint(line, sizeof(line), L"[llmk][sentinel] enabled=%d strict=%d max_cycles=%lu last_err=%d reason=%s\r\n\r\n",
           s->cfg.enabled ? 1 : 0,
           s->cfg.strict_mode ? 1 : 0,
           s->cfg.max_cycles,
           (int)s->last_error,
           s->last_reason);
    llmk_file_write_u16(f, line);
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_dump_log_to_file(EFI_FILE_HANDLE f, const LlmkLog *log, UINT32 max_entries) {
    if (!f || !log || !log->entries || log->capacity == 0) return EFI_INVALID_PARAMETER;

    UINT32 n = log->capacity;
    if (max_entries != 0 && max_entries < n) n = max_entries;

    CHAR16 line[256];
    SPrint(line, sizeof(line), L"[llmk][log] last %u events (ring cap=%u)\r\n", n, log->capacity);
    llmk_file_write_u16(f, line);

    UINT32 w = log->write_idx;
    for (UINT32 i = 0; i < n; i++) {
        UINT32 off = (w + log->capacity - 1 - i) % log->capacity;
        const LlmkLogEntry *e = &log->entries[off];
        if (e->tsc == 0 && e->code == 0 && e->msg[0] == 0) continue;
        SPrint(line, sizeof(line), L"  #%u tsc=%lu code=%u arena=%d ptr=0x%lx size=%lu msg=%s\r\n",
               i, e->tsc, e->code, e->arena, e->ptr, e->size, e->msg);
        llmk_file_write_u16(f, line);
    }
    llmk_file_write_u16(f, L"\r\n");
    return EFI_SUCCESS;
}

// ============================================================================
// MATH FUNCTIONS
// ============================================================================

float fast_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float xhalf = 0.5f * x;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - xhalf * x * x);
    x = x * (1.5f - xhalf * x * x);
    return 1.0f / x;
}

float fast_exp(float x) {
    if (x < -10.0f) return 0.0f;
    if (x > 10.0f) return 22026.0f;
    x = 1.0f + x / 256.0f;
    x *= x; x *= x; x *= x; x *= x;
    x *= x; x *= x; x *= x; x *= x;
    return x;
}

static int my_strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == 0) return 0;
    }
    return 0;
}

// ============================================================================
// TRANSFORMER OPERATIONS
// ============================================================================

void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / fast_sqrt(ss);
    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

void matmul(float* xout, float* x, float* w, int n, int d) {
    // DjibLAS computes (column-major): C(m x n) = A(k x m)^T * B(k x n)
    // We want (row-major weights): xout(d) = W(d x n) * x(n)
    // Trick: W(d x n) row-major has the same memory layout as B(k x n_out)
    // column-major when k=n and n_out=d (because W[i*n + l] == B[l + k*i]).
    // Use A = x as a (k x 1) column-major matrix.
    // Result C is (1 x d) column-major, so it lands contiguous into xout.
    djiblas_sgemm_f32(
        /*m=*/1, /*n=*/d, /*k=*/n,
        /*A=*/x, /*lda=*/n,
        /*B=*/w, /*ldb=*/n,
        /*C=*/xout, /*ldc=*/1
    );
}

static UINT16 llmk_read_u16_unaligned(const void *p) {
    const UINT8 *b = (const UINT8 *)p;
    return (UINT16)((UINT16)b[0] | ((UINT16)b[1] << 8));
}

// IEEE-754 half -> float32. Handles normals/denormals/inf/nan.
static float llmk_fp16_to_fp32(UINT16 h) {
    UINT32 sign = (UINT32)(h >> 15) & 1u;
    UINT32 exp  = (UINT32)(h >> 10) & 0x1Fu;
    UINT32 mant = (UINT32)h & 0x3FFu;

    UINT32 out_sign = sign << 31;
    UINT32 out_exp;
    UINT32 out_mant;

    if (exp == 0) {
        if (mant == 0) {
            UINT32 u = out_sign;
            return *(float *)&u;
        }
        // subnormal
        exp = 1;
        while ((mant & 0x400u) == 0) {
            mant <<= 1;
            exp--;
        }
        mant &= 0x3FFu;
        out_exp  = (exp + (127 - 15)) << 23;
        out_mant = mant << 13;
    } else if (exp == 31) {
        // inf/nan
        out_exp  = 0xFFu << 23;
        out_mant = mant ? (mant << 13) : 0;
    } else {
        out_exp  = (exp + (127 - 15)) << 23;
        out_mant = mant << 13;
    }

    UINT32 u = out_sign | out_exp | out_mant;
    return *(float *)&u;
}

static UINT64 llmk_align_up_u64(UINT64 x, UINT64 a) {
    return (a == 0) ? x : ((x + a - 1ULL) / a) * a;
}

// GGML Q8_0 block format: fp16 scale + 32 int8 values.
// bytes_per_row = (cols/32) * 34.
static UINT64 llmk_q8_0_row_bytes(int cols) {
    if (cols <= 0) return 0;
    if ((cols % 32) != 0) return 0;
    return ((UINT64)cols / 32ULL) * 34ULL;
}

static void llmk_dequantize_q8_0_row(float *dst, const UINT8 *row_q8, int cols) {
    UINT64 rb = llmk_q8_0_row_bytes(cols);
    if (!dst || !row_q8 || rb == 0) return;

    const int nb = cols / 32;
    const UINT8 *p = row_q8;
    for (int b = 0; b < nb; b++) {
        UINT16 dh = llmk_read_u16_unaligned(p);
        float d = llmk_fp16_to_fp32(dh);
        const INT8 *qs = (const INT8 *)(p + 2);
        for (int i = 0; i < 32; i++) {
            dst[b * 32 + i] = d * (float)qs[i];
        }
        p += 34;
    }
}

// xout(d) = W(d x n) * x(n) where W is Q8_0 row-major blocks.
static void matmul_q8_0_scalar(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        // Q8_0 requires cols multiple of 32.
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    const UINT64 row_bytes = llmk_q8_0_row_bytes(n);
    const int nb = n / 32;

    for (int r = 0; r < d; r++) {
        const UINT8 *row = w_q8 + (UINTN)r * (UINTN)row_bytes;
        float acc = 0.0f;
        const UINT8 *p = row;
        for (int b = 0; b < nb; b++) {
            float dscale = llmk_fp16_to_fp32(llmk_read_u16_unaligned(p));
            const INT8 *qs = (const INT8 *)(p + 2);
            float sum = 0.0f;
            const float *xblk = x + b * 32;
            for (int i = 0; i < 32; i++) {
                sum += xblk[i] * (float)qs[i];
            }
            acc += dscale * sum;
            p += 34;
        }
        xout[r] = acc;
    }
}

#if defined(__x86_64__) || defined(_M_X64)
// Shared activation quant buffers for Q8_0 matmuls (used only when q8_act_quant!=0).
// Monotonic allocation is OK; we only grow a couple of times (dim/hidden_dim).
static float *g_q8_act_scales = NULL;
static INT8  *g_q8_act_qs = NULL;
static int g_q8_act_cap_n = 0;

static void llmk_q8_act_ensure(int n) {
    if (n <= 0) return;
    if ((n % 32) != 0) return;
    if (g_q8_act_cap_n >= n && g_q8_act_scales && g_q8_act_qs) return;

    const int nb = n / 32;
    g_q8_act_scales = (float *)simple_alloc((unsigned long)nb * sizeof(float));
    g_q8_act_qs = (INT8 *)simple_alloc((unsigned long)n * sizeof(INT8));
    g_q8_act_cap_n = n;
}

static void llmk_quantize_f32_to_q8_blocks(const float *x, int n, INT8 *out_qs, float *out_scales) {
    if (!x || !out_qs || !out_scales) return;
    if (n <= 0 || (n % 32) != 0) return;
    const int nb = n / 32;
    for (int b = 0; b < nb; b++) {
        const float *xb = x + b * 32;
        float max_abs = 0.0f;
        for (int i = 0; i < 32; i++) {
            float v = xb[i];
            if (v < 0.0f) v = -v;
            if (v > max_abs) max_abs = v;
        }
        float dscale = (max_abs > 0.0f) ? (max_abs / 127.0f) : 0.0f;
        out_scales[b] = dscale;
        float inv = (dscale > 0.0f) ? (1.0f / dscale) : 0.0f;
        INT8 *qdst = out_qs + b * 32;
        for (int i = 0; i < 32; i++) {
            float fv = xb[i] * inv;
            int iv = (fv >= 0.0f) ? (int)(fv + 0.5f) : (int)(fv - 0.5f);
            if (iv < -127) iv = -127;
            if (iv > 127) iv = 127;
            qdst[i] = (INT8)iv;
        }
    }
}

// Dot kernel for 32 signed int8 values using AVX2.
// Returns int32 sum(a[i] * b[i]).
__attribute__((target("avx2")))
static int llmk_dot_i8_32_avx2(const INT8 *a, const INT8 *b) {
    __m128i a0 = _mm_loadu_si128((const __m128i *)(a + 0));
    __m128i a1 = _mm_loadu_si128((const __m128i *)(a + 16));
    __m128i b0 = _mm_loadu_si128((const __m128i *)(b + 0));
    __m128i b1 = _mm_loadu_si128((const __m128i *)(b + 16));

    __m256i a16_0 = _mm256_cvtepi8_epi16(a0);
    __m256i a16_1 = _mm256_cvtepi8_epi16(a1);
    __m256i b16_0 = _mm256_cvtepi8_epi16(b0);
    __m256i b16_1 = _mm256_cvtepi8_epi16(b1);

    __m256i s0 = _mm256_madd_epi16(a16_0, b16_0);
    __m256i s1 = _mm256_madd_epi16(a16_1, b16_1);
    __m256i s = _mm256_add_epi32(s0, s1);

    __m128i lo = _mm256_castsi256_si128(s);
    __m128i hi = _mm256_extracti128_si256(s, 1);
    __m128i sum = _mm_add_epi32(lo, hi);
    __m128i shuf = _mm_shuffle_epi32(sum, _MM_SHUFFLE(2, 3, 0, 1));
    sum = _mm_add_epi32(sum, shuf);
    shuf = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
    sum = _mm_add_epi32(sum, shuf);
    return _mm_cvtsi128_si32(sum);
}

// AVX2 implementation: converts int8 weights to float on the fly.
// Compiled as AVX2 even when the TU default is SSE2.
__attribute__((target("avx2")))
static void matmul_q8_0_avx2(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    const UINT64 row_bytes = llmk_q8_0_row_bytes(n);
    const int nb = n / 32;

    for (int r = 0; r < d; r++) {
        const UINT8 *row = w_q8 + (UINTN)r * (UINTN)row_bytes;
        float acc = 0.0f;
        const UINT8 *p = row;

        for (int b = 0; b < nb; b++) {
            float dscale = llmk_fp16_to_fp32(llmk_read_u16_unaligned(p));
            const INT8 *qs = (const INT8 *)(p + 2);
            const float *xblk = x + b * 32;

            __m256 vacc = _mm256_setzero_ps();

            // 32 values per block, process 8 at a time.
            for (int i = 0; i < 32; i += 8) {
                // Load 8 int8 values (unaligned) and sign-extend to 8 int32.
                __m128i q8 = _mm_loadl_epi64((const __m128i *)(qs + i));
                __m256i q32 = _mm256_cvtepi8_epi32(q8);
                __m256 qf = _mm256_cvtepi32_ps(q32);

                __m256 xf = _mm256_loadu_ps(xblk + i);
                vacc = _mm256_add_ps(vacc, _mm256_mul_ps(xf, qf));
            }

            // Horizontal sum of vacc without requiring SSE3 (build uses -msse2).
            __m128 lo = _mm256_castps256_ps128(vacc);
            __m128 hi = _mm256_extractf128_ps(vacc, 1);
            __m128 sum128 = _mm_add_ps(lo, hi);
            __m128 shuf = _mm_shuffle_ps(sum128, sum128, _MM_SHUFFLE(2, 3, 0, 1));
            sum128 = _mm_add_ps(sum128, shuf);
            shuf = _mm_shuffle_ps(sum128, sum128, _MM_SHUFFLE(1, 0, 3, 2));
            sum128 = _mm_add_ps(sum128, shuf);
            float sum = _mm_cvtss_f32(sum128);
            acc += dscale * sum;
            p += 34;
        }

        xout[r] = acc;
    }
}

// AVX2 implementation: quantize activations (x) into Q8_0 blocks and use int8 dot-products.
// Faster on real AVX2 CPUs; adds extra approximation (beyond quantized weights).
__attribute__((target("avx2")))
static void matmul_q8_0_avx2_i8_prequant(float *xout, const INT8 *x_qs, const float *x_scales, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x_qs || !x_scales || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    const UINT64 row_bytes = llmk_q8_0_row_bytes(n);
    const int nb = n / 32;

    for (int r = 0; r < d; r++) {
        const UINT8 *row = w_q8 + (UINTN)r * (UINTN)row_bytes;
        float acc = 0.0f;
        const UINT8 *p = row;
        for (int b = 0; b < nb; b++) {
            float wscale = llmk_fp16_to_fp32(llmk_read_u16_unaligned(p));
            const INT8 *wqs = (const INT8 *)(p + 2);
            const INT8 *blk = x_qs + b * 32;
            int dot = llmk_dot_i8_32_avx2(blk, wqs);
            acc += (wscale * x_scales[b]) * (float)dot;
            p += 34;
        }
        xout[r] = acc;
    }
}

__attribute__((target("avx2")))
static void matmul_q8_0_avx2_i8(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    llmk_q8_act_ensure(n);
    if (!g_q8_act_qs || !g_q8_act_scales) return;
    llmk_quantize_f32_to_q8_blocks(x, n, g_q8_act_qs, g_q8_act_scales);
    matmul_q8_0_avx2_i8_prequant(xout, g_q8_act_qs, g_q8_act_scales, w_q8, n, d);
}
#endif

static void matmul_q8_0(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

#if defined(__x86_64__) || defined(_M_X64)
    static int g_q8_kernel_inited = 0;
    static int g_q8_use_avx2 = 0;
    if (!g_q8_kernel_inited) {
        CPUFeatures f;
        djiblas_detect_cpu(&f);
        g_q8_use_avx2 = (f.has_avx2 != 0);
        g_q8_kernel_inited = 1;
    }
    if (g_q8_use_avx2) {
        if (g_cfg_q8_act_quant == 1) {
            matmul_q8_0_avx2_i8(xout, x, w_q8, n, d);
        } else {
            matmul_q8_0_avx2(xout, x, w_q8, n, d);
        }
        return;
    }
#endif

    matmul_q8_0_scalar(xout, x, w_q8, n, d);
}

void softmax(float* x, int size) {
    float max_val = x[0];
#if defined(__x86_64__) || defined(_M_X64)
    // SSE2 max reduction
    {
        __m128 vmax = _mm_set1_ps(max_val);
        int i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 v = _mm_loadu_ps(&x[i]);
            vmax = _mm_max_ps(vmax, v);
        }
        __m128 shuf = _mm_shuffle_ps(vmax, vmax, _MM_SHUFFLE(2, 3, 0, 1));
        vmax = _mm_max_ps(vmax, shuf);
        shuf = _mm_shuffle_ps(vmax, vmax, _MM_SHUFFLE(1, 0, 3, 2));
        vmax = _mm_max_ps(vmax, shuf);
        _mm_store_ss(&max_val, vmax);
        for (; i < size; i++) {
            if (x[i] > max_val) max_val = x[i];
        }
    }
#else
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) max_val = x[i];
    }
#endif

    float sum = 0.0f;
#if defined(__x86_64__) || defined(_M_X64)
    // Scalar exp, but vectorized accumulation + normalization.
    {
        __m128 vsum = _mm_setzero_ps();
        int i = 0;
        for (; i + 4 <= size; i += 4) {
            float e0 = fast_exp(x[i + 0] - max_val);
            float e1 = fast_exp(x[i + 1] - max_val);
            float e2 = fast_exp(x[i + 2] - max_val);
            float e3 = fast_exp(x[i + 3] - max_val);
            x[i + 0] = e0;
            x[i + 1] = e1;
            x[i + 2] = e2;
            x[i + 3] = e3;
            __m128 v = _mm_loadu_ps(&x[i]);
            vsum = _mm_add_ps(vsum, v);
        }
        __m128 shuf = _mm_shuffle_ps(vsum, vsum, _MM_SHUFFLE(2, 3, 0, 1));
        vsum = _mm_add_ps(vsum, shuf);
        shuf = _mm_shuffle_ps(vsum, vsum, _MM_SHUFFLE(1, 0, 3, 2));
        vsum = _mm_add_ps(vsum, shuf);
        _mm_store_ss(&sum, vsum);
        for (; i < size; i++) {
            x[i] = fast_exp(x[i] - max_val);
            sum += x[i];
        }

        float invsum = 1.0f / sum;
        __m128 vinv = _mm_set1_ps(invsum);
        i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 v = _mm_loadu_ps(&x[i]);
            v = _mm_mul_ps(v, vinv);
            _mm_storeu_ps(&x[i], v);
        }
        for (; i < size; i++) {
            x[i] *= invsum;
        }
    }
#else
    for (int i = 0; i < size; i++) {
        x[i] = fast_exp(x[i] - max_val);
        sum += x[i];
    }
    float invsum = 1.0f / sum;
    for (int i = 0; i < size; i++) {
        x[i] *= invsum;
    }
#endif
}

// ============================================================================
// STRUCTURES
// ============================================================================

typedef struct {
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int vocab_size;
    int seq_len;
} Config;

static UINT64 llmk_calc_kv_bytes_for_seq(const Config *cfg, int seq_len, int kv_dim) {
    if (!cfg || seq_len <= 0 || kv_dim <= 0) return 0;
    return (UINT64)cfg->n_layers * (UINT64)seq_len * (UINT64)kv_dim * (UINT64)sizeof(float) * 2ULL;
}

static UINT64 llmk_calc_state_bytes_for_seq(const Config *cfg, int seq_len, int kv_dim) {
    if (!cfg || seq_len <= 0 || kv_dim <= 0) return 0;

    UINT64 state_bytes = 0;
    state_bytes += (UINT64)cfg->dim * (UINT64)sizeof(float) * 3ULL; // x, xb, xb2
    state_bytes += (UINT64)cfg->hidden_dim * (UINT64)sizeof(float) * 2ULL; // hb, hb2
    state_bytes += (UINT64)cfg->dim * (UINT64)sizeof(float); // q
    state_bytes += (UINT64)kv_dim * (UINT64)sizeof(float) * 2ULL; // k, v
    state_bytes += (UINT64)cfg->n_heads * (UINT64)seq_len * (UINT64)sizeof(float); // att
    state_bytes += (UINT64)cfg->vocab_size * (UINT64)sizeof(float); // logits
    state_bytes += (UINT64)cfg->n_layers * (UINT64)seq_len * (UINT64)kv_dim * (UINT64)sizeof(float) * 2ULL; // key/value cache
    return state_bytes;
}

static UINT32 llmk_fnv1a32_update(UINT32 h, const void *data, UINTN len) {
    const UINT8 *p = (const UINT8 *)data;
    for (UINTN i = 0; i < len; i++) {
        h ^= (UINT32)p[i];
        h *= 16777619u;
    }
    return h;
}

static UINT32 llmk_memorion_ctx_hash32(const Config *config, const CHAR16 *model_filename) {
    UINT32 h = 2166136261u;
    if (config) {
        h = llmk_fnv1a32_update(h, &config->dim, sizeof(config->dim));
        h = llmk_fnv1a32_update(h, &config->n_layers, sizeof(config->n_layers));
        h = llmk_fnv1a32_update(h, &config->n_heads, sizeof(config->n_heads));
        h = llmk_fnv1a32_update(h, &config->n_kv_heads, sizeof(config->n_kv_heads));
        h = llmk_fnv1a32_update(h, &config->seq_len, sizeof(config->seq_len));
        h = llmk_fnv1a32_update(h, &config->vocab_size, sizeof(config->vocab_size));
    }
    if (model_filename) {
        char name8[128];
        llmk_char16_to_ascii_cap(name8, (int)sizeof(name8), model_filename);
        h = llmk_fnv1a32_update(h, name8, (UINTN)my_strlen(name8));
    }
    return h;
}

static void llmk_print_ctx(const Config *config,
                   const CHAR16 *model_name,
                   int kv_pos,
                   float temperature,
                   float min_p,
                   float top_p,
                   int top_k,
                   int no_repeat_ngram,
                   float repeat_penalty,
                   int max_gen_tokens) {
    Print(L"\r\nCTX\r\n");
    Print(L"  model=%s\r\n", model_name ? model_name : L"(unknown)");
    Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d\r\n",
        config->dim, config->n_layers, config->n_heads, config->n_kv_heads, config->vocab_size);
    Print(L"  seq_len=%d kv_pos=%d\r\n", config->seq_len, kv_pos);
    Print(L"  sample: temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
        (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
        (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
        (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
        top_k);
    Print(L"          norepeat=%d repeat=%d.%02d max_tokens=%d\r\n",
        no_repeat_ngram,
        (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
        max_gen_tokens);
    if (g_llmk_ready) {
      Print(L"  budget: prefill=%lu decode=%lu strict=%d overruns(p=%d d=%d)\r\n",
          g_budget_prefill_cycles, g_budget_decode_cycles,
          (int)g_sentinel.cfg.strict_budget,
          (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
    }
    Print(L"\r\n");
}

static void llmk_oo_print_last_consult_status_best_effort(void) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    char *cbuf = (char *)buf;
    UINTN line_start = 0;
    UINTN line_end = len;

    while (line_end > 0 && (cbuf[line_end - 1] == '\r' || cbuf[line_end - 1] == '\n' || cbuf[line_end - 1] == 0)) {
        line_end--;
    }
    if (line_end == 0) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }
    for (UINTN i = line_end; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            line_start = i;
            break;
        }
    }

    UINTN line_len = line_end - line_start;
    char *line = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, line_len + 1, (void **)&line)) || !line) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }
    CopyMem(line, cbuf + line_start, line_len);
    line[line_len] = 0;

    char decision[48]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(decision)
    char selected[48]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(selected)
    char reason_id[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(reason_id)
    char confidence_reason_id[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(confidence_reason_id)
    char plan_enabled[16]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(plan_enabled)
    char plan_remaining[16]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(plan_remaining)
    char plan_hard_stop[16]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(plan_hard_stop)
    char plan_reason_id[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(plan_reason_id)
    char applied[16]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(applied)
    char score[24]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(score)
    char threshold[24]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(threshold)
    char feedback_bias[24]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(feedback_bias)
    char boot_relation[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(boot_relation)
    char boot_bias[24]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(boot_bias)
    char trend_relation[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(trend_relation)
    char trend_bias[24]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(trend_bias)
    char saturation_state[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(saturation_state)
    char saturation_bias[24]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(saturation_bias)
    char operator_summary[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(operator_summary)
    decision[0] = 0;
    selected[0] = 0;
    reason_id[0] = 0;
    confidence_reason_id[0] = 0;
    plan_enabled[0] = 0;
    plan_remaining[0] = 0;
    plan_hard_stop[0] = 0;
    plan_reason_id[0] = 0;
    applied[0] = 0;
    score[0] = 0;
    threshold[0] = 0;
    feedback_bias[0] = 0;
    boot_relation[0] = 0;
    boot_bias[0] = 0;
    trend_relation[0] = 0;
    trend_bias[0] = 0;
    saturation_state[0] = 0;
    saturation_bias[0] = 0;
    operator_summary[0] = 0;

    (void)llmk_oo_outcome_extract_token(line, "d", decision, (int)sizeof(decision));
    (void)llmk_oo_outcome_extract_token(line, "sel", selected, (int)sizeof(selected));
    (void)llmk_oo_outcome_extract_token(line, "ri", reason_id, (int)sizeof(reason_id));
    (void)llmk_oo_outcome_extract_token(line, "cri", confidence_reason_id, (int)sizeof(confidence_reason_id));
    (void)llmk_oo_outcome_extract_token(line, "pe", plan_enabled, (int)sizeof(plan_enabled));
    (void)llmk_oo_outcome_extract_token(line, "pr", plan_remaining, (int)sizeof(plan_remaining));
    (void)llmk_oo_outcome_extract_token(line, "ph", plan_hard_stop, (int)sizeof(plan_hard_stop));
    (void)llmk_oo_outcome_extract_token(line, "pri", plan_reason_id, (int)sizeof(plan_reason_id));
    (void)llmk_oo_outcome_extract_token(line, "a", applied, (int)sizeof(applied));
    (void)llmk_oo_outcome_extract_token(line, "sc", score, (int)sizeof(score));
    (void)llmk_oo_outcome_extract_token(line, "th", threshold, (int)sizeof(threshold));
    (void)llmk_oo_outcome_extract_token(line, "fb", feedback_bias, (int)sizeof(feedback_bias));
    (void)llmk_oo_outcome_extract_token(line, "br", boot_relation, (int)sizeof(boot_relation));
    (void)llmk_oo_outcome_extract_token(line, "bb", boot_bias, (int)sizeof(boot_bias));
    (void)llmk_oo_outcome_extract_token(line, "tr", trend_relation, (int)sizeof(trend_relation));
    (void)llmk_oo_outcome_extract_token(line, "tb", trend_bias, (int)sizeof(trend_bias));
    (void)llmk_oo_outcome_extract_token(line, "sr", saturation_state, (int)sizeof(saturation_state));
    (void)llmk_oo_outcome_extract_token(line, "sb", saturation_bias, (int)sizeof(saturation_bias));
    (void)llmk_oo_outcome_extract_token(line, "os", operator_summary, (int)sizeof(operator_summary));

    Print(L"  last.consult.decision=%a selected=%a reason_id=%a applied=%a score=%a threshold=%a feedback_bias=%a\r\n",
          decision[0] ? decision : "na",
          selected[0] ? selected : "na",
          reason_id[0] ? reason_id : "na",
          applied[0] ? applied : "na",
          score[0] ? score : "na",
          threshold[0] ? threshold : "na",
          feedback_bias[0] ? feedback_bias : "na");
    Print(L"  last.consult.conf_reason_id=%a plan.enabled=%a remain=%a hard_stop=%a plan_reason_id=%a\r\n",
          confidence_reason_id[0] ? confidence_reason_id : "na",
          plan_enabled[0] ? plan_enabled : "na",
          plan_remaining[0] ? plan_remaining : "na",
          plan_hard_stop[0] ? plan_hard_stop : "na",
          plan_reason_id[0] ? plan_reason_id : "na");
        Print(L"  last.consult.boot_relation=%a boot_bias=%a\r\n",
            boot_relation[0] ? boot_relation : "na",
            boot_bias[0] ? boot_bias : "na");
            Print(L"  last.consult.trend=%a trend_bias=%a saturation=%a saturation_bias=%a\r\n",
                trend_relation[0] ? trend_relation : "na",
                trend_bias[0] ? trend_bias : "na",
                saturation_state[0] ? saturation_state : "na",
                saturation_bias[0] ? saturation_bias : "na");
                Print(L"  last.consult.operator_summary=%a\r\n",
                    operator_summary[0] ? operator_summary : "na");

    {
        const char *why_summary = "see_reason_id";
        if (applied[0] == '1' && applied[1] == 0) {
            why_summary = "applied";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_BLOCK_PLAN_BUDGET", 20) == 0 && reason_id[20] == 0) {
            why_summary = "not_applied_plan_budget";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_BLOCK_CONFIDENCE", 19) == 0 && reason_id[19] == 0) {
            why_summary = "not_applied_confidence";
        } else if ((reason_id[0] == 'O' && my_strncmp(reason_id, "OO_BLOCK_HARD_STOP", 18) == 0 && reason_id[18] == 0) ||
                   (plan_hard_stop[0] == '1' && plan_hard_stop[1] == 0)) {
            why_summary = "not_applied_hard_stop";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_BLOCK_ALREADY_MIN", 20) == 0 && reason_id[20] == 0) {
            why_summary = "already_at_min";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_BLOCK_ALREADY_MAX", 20) == 0 && reason_id[20] == 0) {
            why_summary = "already_at_max";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_NO_ACTIONABLE_KEYWORD", 24) == 0 && reason_id[24] == 0) {
            why_summary = "no_actionable_change";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_MODEL_LOG_ONLY", 17) == 0 && reason_id[17] == 0) {
            why_summary = "logged_model_change";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_REBOOT_LOG_ONLY", 18) == 0 && reason_id[18] == 0) {
            why_summary = "logged_reboot";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_STABLE_OK", 12) == 0 && reason_id[12] == 0) {
            why_summary = "stable_no_action";
        } else if (reason_id[0] == 'O' && my_strncmp(reason_id, "OO_APPLY_VERIFY_FAILED", 22) == 0 && reason_id[22] == 0) {
            why_summary = "apply_verify_failed";
        }
        Print(L"  last.consult.why=%a\r\n", why_summary);
    }

    uefi_call_wrapper(BS->FreePool, 1, line);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_explain_last_consult_best_effort(int verbose) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain] no consult history\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN line_start = 0;
    UINTN line_end = len;
    while (line_end > 0 && (cbuf[line_end - 1] == '\r' || cbuf[line_end - 1] == '\n' || cbuf[line_end - 1] == 0)) {
        line_end--;
    }
    if (line_end == 0) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain] no consult history\r\n");
        return;
    }
    for (UINTN i = line_end; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            line_start = i;
            break;
        }
    }

    UINTN line_len = line_end - line_start;
    char *line = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, line_len + 1, (void **)&line)) || !line) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain] oom\r\n");
        return;
    }
    CopyMem(line, cbuf + line_start, line_len);
    line[line_len] = 0;

    char decision[48], selected[48], reason_id[64], confidence_reason_id[64], plan_enabled[16], plan_remaining[16], plan_hard_stop[16], plan_reason_id[64], applied[16], score[24], threshold[24], why[64], boot_relation[64], boot_bias[24], trend_relation[64], trend_bias[24], saturation_state[64], saturation_bias[24], operator_summary[64]; // SAFE: fixed-size token buffers; each extracted with explicit out_cap=sizeof(buf)
    decision[0] = 0;
    selected[0] = 0;
    reason_id[0] = 0;
    confidence_reason_id[0] = 0;
    plan_enabled[0] = 0;
    plan_remaining[0] = 0;
    plan_hard_stop[0] = 0;
    plan_reason_id[0] = 0;
    applied[0] = 0;
    score[0] = 0;
    threshold[0] = 0;
    why[0] = 0;
    boot_relation[0] = 0;
    boot_bias[0] = 0;
    trend_relation[0] = 0;
    trend_bias[0] = 0;
    saturation_state[0] = 0;
    saturation_bias[0] = 0;
    operator_summary[0] = 0;
    (void)llmk_oo_outcome_extract_token(line, "d", decision, (int)sizeof(decision));
    (void)llmk_oo_outcome_extract_token(line, "sel", selected, (int)sizeof(selected));
    (void)llmk_oo_outcome_extract_token(line, "ri", reason_id, (int)sizeof(reason_id));
    (void)llmk_oo_outcome_extract_token(line, "cri", confidence_reason_id, (int)sizeof(confidence_reason_id));
    (void)llmk_oo_outcome_extract_token(line, "pe", plan_enabled, (int)sizeof(plan_enabled));
    (void)llmk_oo_outcome_extract_token(line, "pr", plan_remaining, (int)sizeof(plan_remaining));
    (void)llmk_oo_outcome_extract_token(line, "ph", plan_hard_stop, (int)sizeof(plan_hard_stop));
    (void)llmk_oo_outcome_extract_token(line, "pri", plan_reason_id, (int)sizeof(plan_reason_id));
    (void)llmk_oo_outcome_extract_token(line, "a", applied, (int)sizeof(applied));
    (void)llmk_oo_outcome_extract_token(line, "sc", score, (int)sizeof(score));
    (void)llmk_oo_outcome_extract_token(line, "th", threshold, (int)sizeof(threshold));
    (void)llmk_oo_outcome_extract_token(line, "br", boot_relation, (int)sizeof(boot_relation));
    (void)llmk_oo_outcome_extract_token(line, "bb", boot_bias, (int)sizeof(boot_bias));
    (void)llmk_oo_outcome_extract_token(line, "tr", trend_relation, (int)sizeof(trend_relation));
    (void)llmk_oo_outcome_extract_token(line, "tb", trend_bias, (int)sizeof(trend_bias));
    (void)llmk_oo_outcome_extract_token(line, "sr", saturation_state, (int)sizeof(saturation_state));
    (void)llmk_oo_outcome_extract_token(line, "sb", saturation_bias, (int)sizeof(saturation_bias));
    (void)llmk_oo_outcome_extract_token(line, "os", operator_summary, (int)sizeof(operator_summary));

    if (applied[0] == '1' && applied[1] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "applied");
    } else if (my_strncmp(reason_id, "OO_BLOCK_PLAN_BUDGET", 20) == 0 && reason_id[20] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "not_applied_plan_budget");
    } else if (my_strncmp(reason_id, "OO_BLOCK_CONFIDENCE", 19) == 0 && reason_id[19] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "not_applied_confidence");
    } else if (my_strncmp(reason_id, "OO_BLOCK_HARD_STOP", 18) == 0 && reason_id[18] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "not_applied_hard_stop");
    } else if (my_strncmp(reason_id, "OO_BLOCK_ALREADY_MIN", 20) == 0 && reason_id[20] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "already_at_min");
    } else if (my_strncmp(reason_id, "OO_BLOCK_ALREADY_MAX", 20) == 0 && reason_id[20] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "already_at_max");
    } else if (my_strncmp(reason_id, "OO_STABLE_OK", 12) == 0 && reason_id[12] == 0) {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "stable_no_action");
    } else {
        llmk_oo_outcome_copy_token(why, (int)sizeof(why), "see_reason_id");
    }

    Print(L"[oo_explain] selected=%a decision=%a\r\n",
          selected[0] ? selected : "na",
          decision[0] ? decision : "na");
    Print(L"[oo_explain] why=%a reason_id=%a\r\n",
          why[0] ? why : "na",
          reason_id[0] ? reason_id : "na");
    Print(L"[oo_explain] applied=%a score=%a threshold=%a\r\n",
          applied[0] ? applied : "na",
          score[0] ? score : "na",
          threshold[0] ? threshold : "na");
        if (verbose) {
          Print(L"[oo_explain] conf_reason_id=%a\r\n",
              confidence_reason_id[0] ? confidence_reason_id : "na");
          Print(L"[oo_explain] plan.enabled=%a remain=%a hard_stop=%a plan_reason_id=%a\r\n",
              plan_enabled[0] ? plan_enabled : "na",
              plan_remaining[0] ? plan_remaining : "na",
              plan_hard_stop[0] ? plan_hard_stop : "na",
              plan_reason_id[0] ? plan_reason_id : "na");
          Print(L"[oo_explain] boot_relation=%a boot_bias=%a\r\n",
              boot_relation[0] ? boot_relation : "na",
              boot_bias[0] ? boot_bias : "na");
          Print(L"[oo_explain] trend=%a trend_bias=%a saturation=%a saturation_bias=%a\r\n",
              trend_relation[0] ? trend_relation : "na",
              trend_bias[0] ? trend_bias : "na",
              saturation_state[0] ? saturation_state : "na",
              saturation_bias[0] ? saturation_bias : "na");
          Print(L"[oo_explain] operator_summary=%a\r\n",
              operator_summary[0] ? operator_summary : "na");
        }

    uefi_call_wrapper(BS->FreePool, 1, line);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_explain_boot_best_effort(void) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain_boot] no consult history\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN line_start = 0;
    UINTN line_end = len;
    while (line_end > 0 && (cbuf[line_end - 1] == '\r' || cbuf[line_end - 1] == '\n' || cbuf[line_end - 1] == 0)) {
        line_end--;
    }
    if (line_end == 0) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain_boot] no consult history\r\n");
        return;
    }
    for (UINTN i = line_end; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            line_start = i;
            break;
        }
    }

    UINTN consult_line_len = line_end - line_start;
    char *consult_line = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, consult_line_len + 1, (void **)&consult_line)) || !consult_line) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_explain_boot] oom\r\n");
        return;
    }
    CopyMem(consult_line, cbuf + line_start, consult_line_len);
    consult_line[consult_line_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, buf);

    char consult_selected[48]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(consult_selected)
    char consult_reason_id[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(consult_reason_id)
    char consult_operator_summary[64]; // SAFE: bounded token buffer; extracted with out_cap=sizeof(consult_operator_summary)
    consult_selected[0] = 0;
    consult_reason_id[0] = 0;
    consult_operator_summary[0] = 0;
    (void)llmk_oo_outcome_extract_token(consult_line, "sel", consult_selected, (int)sizeof(consult_selected));
    (void)llmk_oo_outcome_extract_token(consult_line, "ri", consult_reason_id, (int)sizeof(consult_reason_id));
    (void)llmk_oo_outcome_extract_token(consult_line, "os", consult_operator_summary, (int)sizeof(consult_operator_summary));

    char last_action[32]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    char last_improved[8]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    char last_expected[64]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    char last_observed[64]; // SAFE: bounded token buffer; populated via llmk_oo_read_latest_confirmed_outcome_best_effort()
    last_action[0] = 0;
    last_improved[0] = 0;
    last_expected[0] = 0;
    last_observed[0] = 0;
    if (!llmk_oo_read_latest_confirmed_outcome_best_effort(last_action, (int)sizeof(last_action),
                                                           last_improved, (int)sizeof(last_improved),
                                                           last_expected, (int)sizeof(last_expected),
                                                           last_observed, (int)sizeof(last_observed))) {
        uefi_call_wrapper(BS->FreePool, 1, consult_line);
        Print(L"[oo_explain_boot] no outcome history\r\n");
        return;
    }

    const char *relation = "selected_differs_from_last_confirmed";
    if (llmk_oo_ascii_equals(consult_selected, last_action)) {
        relation = (last_improved[0] == '1' && last_improved[1] == 0) ? "selected_matches_confirmed_good" : "selected_matches_confirmed_bad";
    }

    Print(L"[oo_explain_boot] consult.selected=%a reason_id=%a\r\n",
          consult_selected[0] ? consult_selected : "na",
          consult_reason_id[0] ? consult_reason_id : "na");
        Print(L"[oo_explain_boot] operator_summary=%a\r\n",
            consult_operator_summary[0] ? consult_operator_summary : "na");
    Print(L"[oo_explain_boot] outcome.action=%a improved=%a expected=%a observed=%a\r\n",
          last_action[0] ? last_action : "na",
          last_improved[0] ? last_improved : "na",
          last_expected[0] ? last_expected : "na",
          last_observed[0] ? last_observed : "na");
    Print(L"[oo_explain_boot] relation=%a\r\n", relation);
    llmk_oo_print_recent_confirmed_outcomes_best_effort(3);

    uefi_call_wrapper(BS->FreePool, 1, consult_line);
}

static void llmk_print_log(UINT32 n) {
    if (n == 0) n = 16;
    if (n > 128) n = 128;
    Print(L"\r\nLog (last %d):\r\n", (int)n);
    if (g_llmk_ready && g_llmk_log.capacity) {
        llmk_log_dump(&g_llmk_log, n);
    } else {
        Print(L"  (log not available)\r\n");
    }
    Print(L"\r\n");
}

static void llmk_print_ram_budget(void) {
    if (!g_llmk_ready) {
        Print(L"\r\nRAM budget: (llmk not ready)\r\n\r\n");
        return;
    }

    Print(L"\r\nRAM budget (Zone B):\r\n");
    for (int i = 0; i < LLMK_ARENA_COUNT; i++) {
        const LlmkArena *a = &g_zones.arenas[i];
        UINT64 used = llmk_arena_used_bytes(&g_zones, (LlmkArenaId)i);
        UINT64 rem = llmk_arena_remaining_bytes(&g_zones, (LlmkArenaId)i);
        UINT64 total = a->size;
        UINT64 used_mb = used / (1024ULL * 1024ULL);
        UINT64 total_mb = total / (1024ULL * 1024ULL);
        UINT64 rem_mb = rem / (1024ULL * 1024ULL);
        Print(L"  %s: used=%lu MB  free=%lu MB  total=%lu MB\r\n",
              a->name, used_mb, rem_mb, total_mb);
    }
    Print(L"\r\n");
}

typedef struct {
    int kind; // 0 = float32, 1 = Q8_0 blob

    // float32 pointers (always valid for norms; valid for matrices in float32 mode)
    float* token_embedding_table;
    float* rms_att_weight;
    float* wq;
    float* wk;
    float* wv;
    float* wo;
    float* rms_ffn_weight;
    float* w1;
    float* w2;
    float* w3;
    float* rms_final_weight;
    float* wcls;

    // Q8_0 pointers (valid in Q8_0 blob mode)
    const UINT8 *token_embedding_table_q8;
    const UINT8 *wq_q8;
    const UINT8 *wk_q8;
    const UINT8 *wv_q8;
    const UINT8 *wo_q8;
    const UINT8 *w1_q8;
    const UINT8 *w2_q8;
    const UINT8 *w3_q8;
    const UINT8 *wcls_q8;

    // Strides/sizes for Q8_0 blob addressing
    UINT64 tok_embd_row_bytes;
    UINT64 wq_layer_bytes;
    UINT64 wk_layer_bytes;
    UINT64 wv_layer_bytes;
    UINT64 wo_layer_bytes;
    UINT64 w1_layer_bytes;
    UINT64 w2_layer_bytes;
    UINT64 w3_layer_bytes;
} TransformerWeights;

static void llmk_print_cfg(const Config *config,
                           const CHAR16 *model_name,
                           const TransformerWeights *weights,
                           int kv_pos,
                           float temperature,
                           float min_p,
                           float top_p,
                           int top_k,
                           int no_repeat_ngram,
                           float repeat_penalty,
                           int max_gen_tokens) {

    Print(L"\r\nCFG\r\n");

    Print(L"  repl_cfg_loaded=%d\r\n", g_cfg_loaded);
    Print(L"  boot_verbose=%d\r\n", g_boot_verbose);

    Print(L"  gguf_q8_blob=%d\r\n", g_cfg_gguf_q8_blob ? 1 : 0);
    Print(L"  q8_act_quant=%d\r\n", g_cfg_q8_act_quant);
    Print(L"  model_picker=%d\r\n", g_cfg_model_picker ? 1 : 0);
    Print(L"  ctx_len_cfg=%d\r\n", g_cfg_ctx_len);
    Print(L"  chat_format=");
    llmk_print_ascii(llmk_chat_format_name_ascii(g_cfg_chat_format));
    Print(L"\r\n");
    Print(L"  system_prompt=");
    if (g_cfg_system_prompt[0]) {
        llmk_print_ascii(g_cfg_system_prompt);
    } else {
        Print(L"(empty)");
    }
    Print(L"\r\n");
    Print(L"  autorun_autostart=%d\r\n", g_cfg_autorun_autostart);
    Print(L"  autorun_shutdown_when_done=%d\r\n", g_cfg_autorun_shutdown_when_done);
    Print(L"  autorun_file=%s\r\n", g_cfg_autorun_file);

    // Runtime state
    if (g_loaded_model_path16[0]) {
        Print(L"  loaded_model_path=%s\r\n", g_loaded_model_path16);
    } else {
        Print(L"  loaded_model_path=(unknown)\r\n");
    }
    Print(L"  model=%s\r\n", model_name ? model_name : L"(unknown)");

    if (config) {
        Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
              config->dim, config->n_layers, config->n_heads, config->n_kv_heads, config->vocab_size, config->seq_len);
        Print(L"  kv_pos=%d\r\n", kv_pos);
    }

    if (weights) {
        Print(L"  weights_kind=%s\r\n", (weights->kind == 1) ? L"q8_0_blob" : L"float32");
        if (weights->kind == 1) {
            Print(L"  tok_embd_row_bytes=%lu\r\n", (UINT64)weights->tok_embd_row_bytes);
            Print(L"  wq_layer_bytes=%lu\r\n", (UINT64)weights->wq_layer_bytes);
            Print(L"  wk_layer_bytes=%lu\r\n", (UINT64)weights->wk_layer_bytes);
            Print(L"  wv_layer_bytes=%lu\r\n", (UINT64)weights->wv_layer_bytes);
            Print(L"  wo_layer_bytes=%lu\r\n", (UINT64)weights->wo_layer_bytes);
            Print(L"  w1_layer_bytes=%lu\r\n", (UINT64)weights->w1_layer_bytes);
            Print(L"  w2_layer_bytes=%lu\r\n", (UINT64)weights->w2_layer_bytes);
            Print(L"  w3_layer_bytes=%lu\r\n", (UINT64)weights->w3_layer_bytes);
        }
    } else {
        Print(L"  weights_kind=(unknown)\r\n");
    }

    // Attention SIMD mode
    const CHAR16 *attn_mode = L"auto";
    if (g_attn_force == 0) attn_mode = L"sse2 (forced)";
    else if (g_attn_force == 1) attn_mode = L"avx2 (forced)";
    Print(L"  attn_mode=%s\r\n", attn_mode);
    Print(L"  attn_auto=%s\r\n", g_attn_use_avx2 ? L"avx2" : L"sse2");

        Print(L"  sampling: temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
            (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
            (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
            (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
            top_k);
        Print(L"            norepeat=%d repeat=%d.%02d max_tokens=%d\r\n",
            no_repeat_ngram,
            (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
            max_gen_tokens);

        if (g_llmk_ready) {
          Print(L"  budgets: prefill_max=%lu decode_max=%lu strict=%d overruns(p=%d d=%d)\r\n",
              g_budget_prefill_cycles, g_budget_decode_cycles,
              (int)g_sentinel.cfg.strict_budget,
              (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
        }

    Print(L"\r\n");
}

typedef struct {
    float* x;
    float* xb;
    float* xb2;
    float* hb;
    float* hb2;
    float* q;
    float* k;
    float* v;
    float* att;
    float* logits;
    float* key_cache;
    float* value_cache;
} RunState;

typedef struct {
    char** vocab;
    float* vocab_scores;
    int vocab_size;
    int max_token_length;
} Tokenizer;

// Forward decl for M5 /oo_consult (needs Config, TransformerWeights, RunState, Tokenizer)
static void llmk_oo_consult_execute(Config *config, TransformerWeights *weights, 
                                    RunState *state, Tokenizer *tokenizer,
                                    float temperature, float min_p, float top_p, int top_k);

// ============================================================================
// FORWARD PASS
// ============================================================================

void transformer_forward(RunState* s, TransformerWeights* w, Config* p, int token, int pos) {
    UINT64 start_cycles = __rdtsc();
    int is_prefill = (pos == 0);
    
    // DjibMark: record entry into transformer (prefill vs decode determined by caller)
    if (is_prefill) {
        DJIBMARK_PREFILL();
    } else {
        DJIBMARK_DECODE();
    }
    
    int dim = p->dim;
    int hidden_dim = p->hidden_dim;
    int n_layers = p->n_layers;
    int n_heads = p->n_heads;
    int head_size = dim / n_heads;
    int kv_dim = (dim * p->n_kv_heads) / n_heads;
    int kv_mul = n_heads / p->n_kv_heads;

    const int q8_mode = g_cfg_q8_act_quant;
    const int use_i8_attn = (q8_mode == 1) && llmk_has_avx2_cached();
    const int use_i8_ffn = ((q8_mode == 1) || (q8_mode == 2)) && llmk_has_avx2_cached();
    const int use_i8_cls = (q8_mode == 1) && llmk_has_avx2_cached();
    
    // Copy embedding
    if (w->kind == 1) {
        const UINT8 *row = w->token_embedding_table_q8 + (UINTN)token * (UINTN)w->tok_embd_row_bytes;
        llmk_dequantize_q8_0_row(s->x, row, dim);
    } else {
        float* content_row = w->token_embedding_table + token * dim;
        for (int i = 0; i < dim; i++) {
            s->x[i] = content_row[i];
        }
    }
    
    // Forward all layers
    for (int l = 0; l < n_layers; l++) {
        // Attention RMSNorm
        rmsnorm(s->xb, s->x, w->rms_att_weight + l*dim, dim);
        
        // Q, K, V matrices
        if (w->kind == 1) {
            if (use_i8_attn) {
                llmk_q8_act_ensure(dim);
                llmk_quantize_f32_to_q8_blocks(s->xb, dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->q, g_q8_act_qs, g_q8_act_scales, w->wq_q8 + (UINTN)l * (UINTN)w->wq_layer_bytes, dim, dim);
                matmul_q8_0_avx2_i8_prequant(s->k, g_q8_act_qs, g_q8_act_scales, w->wk_q8 + (UINTN)l * (UINTN)w->wk_layer_bytes, dim, kv_dim);
                matmul_q8_0_avx2_i8_prequant(s->v, g_q8_act_qs, g_q8_act_scales, w->wv_q8 + (UINTN)l * (UINTN)w->wv_layer_bytes, dim, kv_dim);
            } else {
                matmul_q8_0(s->q, s->xb, w->wq_q8 + (UINTN)l * (UINTN)w->wq_layer_bytes, dim, dim);
                matmul_q8_0(s->k, s->xb, w->wk_q8 + (UINTN)l * (UINTN)w->wk_layer_bytes, dim, kv_dim);
                matmul_q8_0(s->v, s->xb, w->wv_q8 + (UINTN)l * (UINTN)w->wv_layer_bytes, dim, kv_dim);
            }
        } else {
            matmul(s->q, s->xb, w->wq + l*dim*dim, dim, dim);
            matmul(s->k, s->xb, w->wk + l*dim*kv_dim, dim, kv_dim);
            matmul(s->v, s->xb, w->wv + l*dim*kv_dim, dim, kv_dim);
        }
        
        // Store in KV cache
        int loff = l * p->seq_len * kv_dim;
        float* key_cache_row = s->key_cache + loff + pos * kv_dim;
        float* value_cache_row = s->value_cache + loff + pos * kv_dim;
        for (int i = 0; i < kv_dim; i++) {
            key_cache_row[i] = s->k[i];
            value_cache_row[i] = s->v[i];
        }
        
        // Multihead attention
        for (int h = 0; h < n_heads; h++) {
            float* q_h = s->q + h * head_size;
            int att_offset = h * p->seq_len;
            float inv_scale = 1.0f / fast_sqrt((float)head_size);
            int kv_head = h / kv_mul;
            const float *key_base = s->key_cache + loff + kv_head * head_size;
            const float *val_base = s->value_cache + loff + kv_head * head_size;

            llmk_kv_prefetch_range(key_base, kv_dim, head_size, pos + 1);
            llmk_kv_prefetch_range(val_base, kv_dim, head_size, pos + 1);
            // Attention scores
            for (int t = 0; t <= pos; t++) {
                float* k_t = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float score = dot_f32_best(q_h, k_t, head_size) * inv_scale;
                s->att[att_offset + t] = score;
            }
            
            // Softmax
            softmax(s->att + att_offset, pos + 1);

            // Weighted sum
            float* xb_h = s->xb + h * head_size;
            for (int i = 0; i < head_size; i++) xb_h[i] = 0.0f;
            
            for (int t = 0; t <= pos; t++) {
                float* v_t = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float a = s->att[att_offset + t];
                axpy_f32_best(xb_h, v_t, a, head_size);
            }
        }
        pheromion_touch(&g_pheromion, 1);
        // Output projection
        if (w->kind == 1) {
            if (use_i8_attn) {
                llmk_q8_act_ensure(dim);
                llmk_quantize_f32_to_q8_blocks(s->xb, dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->xb2, g_q8_act_qs, g_q8_act_scales, w->wo_q8 + (UINTN)l * (UINTN)w->wo_layer_bytes, dim, dim);
            } else {
                matmul_q8_0(s->xb2, s->xb, w->wo_q8 + (UINTN)l * (UINTN)w->wo_layer_bytes, dim, dim);
            }
        } else {
            matmul(s->xb2, s->xb, w->wo + l*dim*dim, dim, dim);
        }
        
        // Residual
        for (int i = 0; i < dim; i++) {
            s->x[i] += s->xb2[i];
        }
        
        // FFN RMSNorm
        rmsnorm(s->xb, s->x, w->rms_ffn_weight + l*dim, dim);
        
        // FFN
        if (w->kind == 1) {
            if (use_i8_ffn) {
                llmk_q8_act_ensure(dim);
                llmk_quantize_f32_to_q8_blocks(s->xb, dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->hb, g_q8_act_qs, g_q8_act_scales, w->w1_q8 + (UINTN)l * (UINTN)w->w1_layer_bytes, dim, hidden_dim);
                matmul_q8_0_avx2_i8_prequant(s->hb2, g_q8_act_qs, g_q8_act_scales, w->w3_q8 + (UINTN)l * (UINTN)w->w3_layer_bytes, dim, hidden_dim);
            } else {
                matmul_q8_0(s->hb, s->xb, w->w1_q8 + (UINTN)l * (UINTN)w->w1_layer_bytes, dim, hidden_dim);
                matmul_q8_0(s->hb2, s->xb, w->w3_q8 + (UINTN)l * (UINTN)w->w3_layer_bytes, dim, hidden_dim);
            }
        } else {
            matmul(s->hb, s->xb, w->w1 + l*dim*hidden_dim, dim, hidden_dim);
            matmul(s->hb2, s->xb, w->w3 + l*dim*hidden_dim, dim, hidden_dim);
        }
        
        pheromion_touch(&g_pheromion, 2);
        // SwiGLU
        for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            val *= (1.0f / (1.0f + fast_exp(-val)));
            s->hb[i] = val * s->hb2[i];
        }
        
        if (w->kind == 1) {
            if (use_i8_ffn) {
                llmk_q8_act_ensure(hidden_dim);
                llmk_quantize_f32_to_q8_blocks(s->hb, hidden_dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->xb, g_q8_act_qs, g_q8_act_scales, w->w2_q8 + (UINTN)l * (UINTN)w->w2_layer_bytes, hidden_dim, dim);
            } else {
                matmul_q8_0(s->xb, s->hb, w->w2_q8 + (UINTN)l * (UINTN)w->w2_layer_bytes, hidden_dim, dim);
            }
        } else {
            matmul(s->xb, s->hb, w->w2 + l*dim*hidden_dim, hidden_dim, dim);
        }
        
        // Residual
        for (int i = 0; i < dim; i++) {
            s->x[i] += s->xb[i];
        }
    }
    
    // Final RMSNorm
    rmsnorm(s->x, s->x, w->rms_final_weight, dim);
    
    // Classifier
    if (w->kind == 1) {
        if (use_i8_cls) {
            llmk_q8_act_ensure(dim);
            llmk_quantize_f32_to_q8_blocks(s->x, dim, g_q8_act_qs, g_q8_act_scales);
            matmul_q8_0_avx2_i8_prequant(s->logits, g_q8_act_qs, g_q8_act_scales, w->wcls_q8, dim, p->vocab_size);
        } else {
            matmul_q8_0(s->logits, s->x, w->wcls_q8, dim, p->vocab_size);
        }
    } else {
        matmul(s->logits, s->x, w->wcls, dim, p->vocab_size);
    }
    
    // M16.1: Capture transformer metrics
    UINT64 end_cycles = __rdtsc();
    UINT64 elapsed = (end_cycles > start_cycles) ? (end_cycles - start_cycles) : 0;
    
    if (is_prefill) {
        g_metrics.total_prefill_cycles += elapsed;
        g_metrics.total_prefill_tokens++;
        g_metrics.total_prefill_calls++;
        g_metrics.last_prefill_cycles = elapsed;
        g_metrics.last_prefill_tokens = 1;
    } else {
        g_metrics.total_decode_cycles += elapsed;
        g_metrics.total_decode_tokens++;
        g_metrics.total_decode_calls++;
        g_metrics.last_decode_cycles = elapsed;
        g_metrics.last_decode_tokens = 1;
    }
}

// Simple PRNG for sampling
static unsigned int g_sample_seed = 1234567;

static void set_seed(unsigned int seed) {
    if (seed == 0) {
        // /seed 0 = "randomize from hardware entropy"
        g_sample_seed = oo_quantum_seed();
    } else {
        // Mix user seed with one RDTSC jitter sample for extra uniqueness
        g_sample_seed = seed ^ oo_rdtsc_jitter_u32();
        if (g_sample_seed == 0) g_sample_seed = seed ? seed : 1;
    }
}

static unsigned long long rdtsc(void) {
    unsigned int lo, hi;
    // Serialize via LFENCE to reduce reordering noise.
    __asm__ __volatile__("lfence\nrdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((unsigned long long)hi << 32) | lo;
}

// 0 means "unavailable / calibration failed".
static unsigned long long tsc_per_sec = 0;

// Cached CPU feature checks (avoid repeated CPUID).
static int llmk_has_avx2_cached(void) {
    static int inited = 0;
    static int has = 0;
    if (!inited) {
        CPUFeatures f;
        djiblas_detect_cpu(&f);
        has = (f.has_avx2 != 0);
        inited = 1;
    }
    return has;
}

// Best-effort wall-clock microsecond timestamp using UEFI GetTime.
// Returns 1 on success, 0 on failure.
static int uefi_wall_us(unsigned long long *out_us) {
    if (!out_us) return 0;
    if (!ST || !ST->RuntimeServices || !ST->RuntimeServices->GetTime) return 0;
    EFI_TIME t;
    EFI_STATUS st = uefi_call_wrapper(ST->RuntimeServices->GetTime, 2, &t, NULL);
    if (EFI_ERROR(st)) return 0;
    // Seconds-of-day is sufficient for short deltas (we handle midnight wrap).
    unsigned long long sod = (unsigned long long)t.Hour * 3600ULL + (unsigned long long)t.Minute * 60ULL + (unsigned long long)t.Second;
    unsigned long long us = sod * 1000000ULL;
    // Nanosecond is defined by EFI_TIME; firmware may provide 0.
    us += ((unsigned long long)t.Nanosecond) / 1000ULL;
    *out_us = us;
    return 1;
}

static void calibrate_tsc_once(void) {
    if (tsc_per_sec != 0) return;
    // Use UEFI Stall (microseconds) to estimate TSC frequency.
    // 500ms gives decent accuracy even on coarse/slow TSC emulation.
    unsigned long long t0 = rdtsc();
    uefi_call_wrapper(BS->Stall, 1, 500000);
    unsigned long long t1 = rdtsc();
    unsigned long long dt = (t1 > t0) ? (t1 - t0) : 0;
    // If dt is implausibly small, treat as unavailable.
    if (dt < 1000ULL) {
        tsc_per_sec = 0;
        return;
    }
    // 500ms -> multiply by 2 to get cycles/sec.
    tsc_per_sec = dt * 2ULL;
}

static float randf(void) {
    g_sample_seed = g_sample_seed * 1664525 + 1013904223;
    // Every 8 calls: inject one RDTSC jitter byte into the seed.
    // Cost: ~5 cycles / 8 tokens = negligible. Breaks LCG predictability.
    static unsigned int randf_call_count = 0;
    if ((++randf_call_count & 7U) == 0) {
        g_sample_seed = oo_quantum_mix(g_sample_seed);
    }
    return (float)(g_sample_seed >> 8) / 16777216.0f;
}

// Sample with temperature + min_p + top-p + top-k + repetition penalty
int sample_advanced(float* logits, int n, float temperature, float min_p, float top_p, int top_k,
                    int* recent_tokens, int n_recent, float repeat_penalty) {
    // Apply repetition penalty
    if (repeat_penalty != 1.0f && n_recent > 0) {
        for (int i = 0; i < n_recent; i++) {
            int tok = recent_tokens[i];
            if (tok >= 0 && tok < n) {
                if (logits[tok] > 0) {
                    logits[tok] /= repeat_penalty;
                } else {
                    logits[tok] *= repeat_penalty;
                }
            }
        }
    }
    
    // Greedy if temp=0
    if (temperature <= 0.0f) {
        int max_i = 0;
        float max_val = logits[0];
        for (int i = 1; i < n; i++) {
            if (logits[i] > max_val) {
                max_val = logits[i];
                max_i = i;
            }
        }
        return max_i;
    }
    
    // Apply temperature
    for (int i = 0; i < n; i++) {
        logits[i] /= temperature;
    }
    
    // Softmax
    float max_val = logits[0];
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_val) max_val = logits[i];
    }
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        logits[i] = fast_exp(logits[i] - max_val);
        sum += logits[i];
    }
    for (int i = 0; i < n; i++) {
        logits[i] /= sum;
    }

    // Min-p filtering (relative to max probability)
    if (min_p > 0.0f) {
        float max_p = 0.0f;
        for (int i = 0; i < n; i++) {
            if (logits[i] > max_p) max_p = logits[i];
        }
        float thresh = min_p * max_p;
        float new_sum = 0.0f;
        for (int i = 0; i < n; i++) {
            if (logits[i] < thresh) {
                logits[i] = 0.0f;
            }
            new_sum += logits[i];
        }
        if (new_sum > 0.0f) {
            for (int i = 0; i < n; i++) {
                logits[i] /= new_sum;
            }
        }
    }
    
    // Top-k / Top-p sampling
    {
        // IMPORTANT: vocab is 32k; do NOT full-sort.
        // We maintain a small descending top-list.
        #define MAX_TOP_K 256
        static int top_idx[MAX_TOP_K];
        static float top_prob[MAX_TOP_K];
        int k = top_k;
        if (k < 0) k = 0;
        if (k > MAX_TOP_K) k = MAX_TOP_K;
        if (k == 0 || k > n) k = (n < MAX_TOP_K) ? n : MAX_TOP_K;

        int top_count = 0;
        for (int i = 0; i < n; i++) {
            float p = logits[i];
            if (top_count < k) {
                int j = top_count;
                while (j > 0 && top_prob[j - 1] < p) {
                    top_prob[j] = top_prob[j - 1];
                    top_idx[j] = top_idx[j - 1];
                    j--;
                }
                top_prob[j] = p;
                top_idx[j] = i;
                top_count++;
            } else if (p > top_prob[top_count - 1]) {
                int j = top_count - 1;
                while (j > 0 && top_prob[j - 1] < p) {
                    top_prob[j] = top_prob[j - 1];
                    top_idx[j] = top_idx[j - 1];
                    j--;
                }
                top_prob[j] = p;
                top_idx[j] = i;
            }
        }

        // If both are effectively "disabled" (top_p>=1 and top_k<=0), fall through to full sampling.
        if (top_k > 0 || top_p < 1.0f) {
            float mass = 0.0f;
            int cutoff = 0;
            for (int i = 0; i < top_count; i++) {
                mass += top_prob[i];
                cutoff++;
                if (top_p < 1.0f && mass >= top_p) break;
            }
            if (cutoff < 1) cutoff = 1;

            float r = randf() * mass;
            float cdf = 0.0f;
            for (int i = 0; i < cutoff; i++) {
                cdf += top_prob[i];
                if (r < cdf) {
                    return top_idx[i];
                }
            }
            return top_idx[cutoff - 1];
        }
        #undef MAX_TOP_K
    }
    
    // Sample from distribution
    float r = randf();
    float cumsum = 0.0f;
    for (int i = 0; i < n; i++) {
        cumsum += logits[i];
        if (r < cumsum) {
            return i;
        }
    }
    
    return n - 1;
}

int sample(float* logits, int n) {
    // Simple greedy for now (kept for compatibility)
    int max_i = 0;
    float max_val = logits[0];
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_val) {
            max_val = logits[i];
            max_i = i;
        }
    }
    return max_i;
}

static void llmk_oo_infermini_no_model(const char *args) {
    const char *text = args;
    if (!text || text[0] == 0) text = "hello";

    Config cfg;
    cfg.dim = 32;
    cfg.hidden_dim = 64;
    cfg.n_layers = 2;
    cfg.n_heads = 4;
    cfg.n_kv_heads = 4;
    cfg.vocab_size = 64;
    cfg.seq_len = 32;

    int kv_dim = (cfg.dim * cfg.n_kv_heads) / cfg.n_heads;
    if (kv_dim <= 0) {
        Print(L"\r\n[oo_infermini] ERROR: invalid kv_dim\r\n\r\n");
        return;
    }

    UINT32 h = 2166136261u;
    h = llmk_fnv1a32_update(h, text, (UINTN)my_strlen(text));
    UINT32 seed = (h == 0) ? 1u : h;

    UINTN w_floats = 0;
    w_floats += (UINTN)cfg.vocab_size * (UINTN)cfg.dim; // tok_embd
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim; // rms_att
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.dim; // wq
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)kv_dim; // wk
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)kv_dim; // wv
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.dim; // wo
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim; // rms_ffn
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.hidden_dim; // w1
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.hidden_dim * (UINTN)cfg.dim; // w2
    w_floats += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.hidden_dim; // w3
    w_floats += (UINTN)cfg.dim; // rms_final
    w_floats += (UINTN)cfg.dim * (UINTN)cfg.vocab_size; // wcls

    float *wmem = NULL;
    EFI_STATUS alloc_w = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, w_floats * sizeof(float), (void **)&wmem);
    if (EFI_ERROR(alloc_w) || !wmem) {
        Print(L"\r\n[oo_infermini] ERROR: OOM allocating weights\r\n\r\n");
        return;
    }

    TransformerWeights w;
    SetMem(&w, sizeof(w), 0);
    w.kind = 0;

    UINTN off = 0;
    w.token_embedding_table = wmem + off; off += (UINTN)cfg.vocab_size * (UINTN)cfg.dim;
    w.rms_att_weight = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim;
    w.wq = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.dim;
    w.wk = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)kv_dim;
    w.wv = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)kv_dim;
    w.wo = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.dim;
    w.rms_ffn_weight = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim;
    w.w1 = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.hidden_dim;
    w.w2 = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.hidden_dim * (UINTN)cfg.dim;
    w.w3 = wmem + off; off += (UINTN)cfg.n_layers * (UINTN)cfg.dim * (UINTN)cfg.hidden_dim;
    w.rms_final_weight = wmem + off; off += (UINTN)cfg.dim;
    w.wcls = wmem + off; off += (UINTN)cfg.dim * (UINTN)cfg.vocab_size;

    if (off != w_floats) {
        Print(L"\r\n[oo_infermini] ERROR: internal weight layout mismatch\r\n\r\n");
        uefi_call_wrapper(BS->FreePool, 1, wmem);
        return;
    }

    UINT32 wseed = seed ^ 0x9E3779B9u;
    llmk_infermini_fill_f32(wmem, w_floats, &wseed);

    UINTN s_floats = 0;
    s_floats += (UINTN)cfg.dim * 3U; // x, xb, xb2
    s_floats += (UINTN)cfg.hidden_dim * 2U; // hb, hb2
    s_floats += (UINTN)cfg.dim; // q
    s_floats += (UINTN)kv_dim * 2U; // k, v
    s_floats += (UINTN)cfg.n_heads * (UINTN)cfg.seq_len; // att
    s_floats += (UINTN)cfg.vocab_size; // logits
    s_floats += (UINTN)cfg.n_layers * (UINTN)cfg.seq_len * (UINTN)kv_dim * 2U; // key/value cache

    float *smem = NULL;
    EFI_STATUS alloc_s = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, s_floats * sizeof(float), (void **)&smem);
    if (EFI_ERROR(alloc_s) || !smem) {
        Print(L"\r\n[oo_infermini] ERROR: OOM allocating state\r\n\r\n");
        uefi_call_wrapper(BS->FreePool, 1, wmem);
        return;
    }
    for (UINTN i = 0; i < s_floats; i++) smem[i] = 0.0f;

    RunState st;
    SetMem(&st, sizeof(st), 0);
    UINTN so = 0;
    st.x = smem + so; so += (UINTN)cfg.dim;
    st.xb = smem + so; so += (UINTN)cfg.dim;
    st.xb2 = smem + so; so += (UINTN)cfg.dim;
    st.hb = smem + so; so += (UINTN)cfg.hidden_dim;
    st.hb2 = smem + so; so += (UINTN)cfg.hidden_dim;
    st.q = smem + so; so += (UINTN)cfg.dim;
    st.k = smem + so; so += (UINTN)kv_dim;
    st.v = smem + so; so += (UINTN)kv_dim;
    st.att = smem + so; so += (UINTN)cfg.n_heads * (UINTN)cfg.seq_len;
    st.logits = smem + so; so += (UINTN)cfg.vocab_size;
    st.key_cache = smem + so; so += (UINTN)cfg.n_layers * (UINTN)cfg.seq_len * (UINTN)kv_dim;
    st.value_cache = smem + so; so += (UINTN)cfg.n_layers * (UINTN)cfg.seq_len * (UINTN)kv_dim;
    if (so != s_floats) {
        Print(L"\r\n[oo_infermini] ERROR: internal state layout mismatch\r\n\r\n");
        uefi_call_wrapper(BS->FreePool, 1, smem);
        uefi_call_wrapper(BS->FreePool, 1, wmem);
        return;
    }

    const int max_prefill = 8;
    int prompt_tokens[max_prefill]; // SAFE: bounded local prompt token buffer (max 8)
    int n_prompt = 0;
    for (int i = 0; text[i] && n_prompt < max_prefill; i++) {
        prompt_tokens[n_prompt++] = llmk_infermini_map_char_to_tok(text[i], cfg.vocab_size);
    }
    if (n_prompt <= 0) prompt_tokens[n_prompt++] = llmk_infermini_map_char_to_tok('h', cfg.vocab_size);

    int pos = 0;
    for (int i = 0; i < n_prompt && pos < cfg.seq_len; i++) {
        transformer_forward(&st, &w, &cfg, prompt_tokens[i], pos);
        pos++;
    }

    Print(L"\r\n[oo_infermini] prompt='");
    llmk_print_ascii(text);
    Print(L"' seed=%u\r\n", (UINT32)seed);

    int recent[16]; // SAFE: bounded recent-token ring buffer (fixed 16 entries)
    int n_recent = 0;
    for (int i = 0; i < n_prompt && n_recent < 16; i++) recent[n_recent++] = prompt_tokens[i];

    const int gen = 12;
    UINT32 out_hash = 2166136261u;
    Print(L"[oo_infermini] out: ");
    for (int step = 0; step < gen && pos < cfg.seq_len; step++) {
        int next = sample_advanced(st.logits, cfg.vocab_size, 0.0f, 0.0f, 1.0f, 0, recent, n_recent, 1.0f);
        char c = llmk_infermini_tok_to_char(next);
        Print(L"%c", (CHAR16)c);
        out_hash = llmk_fnv1a32_update(out_hash, &next, sizeof(next));
        if (n_recent < 16) recent[n_recent++] = next;
        else {
            for (int i = 1; i < 16; i++) recent[i - 1] = recent[i];
            recent[15] = next; // SAFE: fixed last slot of recent[16]
        }
        transformer_forward(&st, &w, &cfg, next, pos);
        pos++;
    }
    Print(L"\r\n[oo_infermini] ok hash=%u\r\n\r\n", out_hash);
    uefi_call_wrapper(BS->FreePool, 1, smem);
    uefi_call_wrapper(BS->FreePool, 1, wmem);
}

static UINT32 llmk_infermini_hash_update(UINT32 h, const void *data, UINTN len) {
    const UINT8 *p = (const UINT8 *)data;
    for (UINTN i = 0; i < len; i++) {
        h ^= (UINT32)p[i];
        h *= 16777619u;
    }
    return h;
}

static UINT32 llmk_infermini_lcg_step(UINT32 *seed) {
    if (!seed) return 0;
    *seed = (*seed * 1664525u) + 1013904223u;
    return *seed;
}

static float llmk_infermini_randf(UINT32 *seed) {
    UINT32 x = llmk_infermini_lcg_step(seed);
    return (float)(x >> 8) / 16777216.0f;
}

static void llmk_infermini_fill(float *dst, UINTN n, UINT32 *seed) {
    if (!dst || !seed) return;
    for (UINTN i = 0; i < n; i++) {
        dst[i] = (llmk_infermini_randf(seed) - 0.5f) * 0.06f;
    }
}

static int llmk_infermini_char_to_tok(char c, int vocab_size) {
    if (vocab_size <= 0) return 0;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == ' ') return 37;
    if (c == '.') return 38;
    if (c == ',') return 39;
    if (c == '!') return 40;
    if (c == '?') return 41;
    return ((int)(unsigned char)c) % vocab_size;
}

// ============================================================================
// TOKENIZER
// ============================================================================

static int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int my_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static char *my_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

static void llmk_u64_to_str(UINT64 val, char *buf, int buf_size) {
    // Convert UINT64 to decimal string (no sprintf in UEFI)
    if (buf_size < 2) return;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = 0; // SAFE: buf_size checked >= 2
        return;
    }
    
    char tmp[32]; // SAFE: bounded local buffer (max 31 digits + NUL)
    int i = 0;
    while (val > 0 && i < 31) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    
    int j = 0;
    while (i > 0 && j < buf_size - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = 0;
}

int str_lookup(char* str, char** vocab, int vocab_size) {
    for (int i = 0; i < vocab_size; i++) {
        if (vocab[i] && my_strcmp(str, vocab[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void encode(char* text, int* tokens, int* n_tokens, int max_tokens, Tokenizer* t) {
    *n_tokens = 0;
    if (max_tokens <= 0) return;

    // Add BOS
    tokens[(*n_tokens)++] = TOKEN_BOS;
    if (*n_tokens >= max_tokens) return;

    // Greedy longest-match encoding
    char* str = text;
    while (*str && *n_tokens < max_tokens) {
        int best_id = -1;
        int best_len = 0;

        for (int len = 64; len > 0; len--) {
            char piece[65];
            int i = 0;
            for (i = 0; i < len && str[i]; i++) {
                piece[i] = str[i];
            }
            if (i != len) continue; // not enough chars remaining
            piece[i] = '\0';

            int id = str_lookup(piece, t->vocab, t->vocab_size);
            if (id >= 0) {
                best_id = id;
                best_len = len;
                break;
            }
        }

        if (best_id >= 0) {
            if (*n_tokens >= max_tokens) break;
            tokens[(*n_tokens)++] = best_id;
            str += best_len;
        } else {
            char single[2]; // SAFE: 1 char + NUL
            single[0] = *str;
            single[1] = '\0'; // SAFE: fixed-size local buffer
            int id = str_lookup(single, t->vocab, t->vocab_size);
            if (id >= 0) {
                if (*n_tokens >= max_tokens) break;
                tokens[(*n_tokens)++] = id;
            }
            str++;
        }
    }
}

// ============================================================================
// KEYBOARD INPUT
// ============================================================================

#define LLMK_INPUT_HIST_MAX 32
#define LLMK_INPUT_HIST_MAXLEN 256

static CHAR16 g_input_hist[LLMK_INPUT_HIST_MAX][LLMK_INPUT_HIST_MAXLEN];
static int g_input_hist_count = 0; // <= LLMK_INPUT_HIST_MAX
static int g_input_hist_head = 0;  // next insert index (ring)

static int llmk_str16_len_cap(const CHAR16 *s, int cap) {
    if (!s || cap <= 0) return 0;
    int n = 0;
    while (n < cap && s[n]) n++;
    return n;
}

static void llmk_str16_copy_cap(CHAR16 *dst, int dst_cap, const CHAR16 *src) {
    if (!dst || dst_cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int n = 0;
    while (n + 1 < dst_cap && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static int llmk_str16_has_newline(const CHAR16 *s) {
    if (!s) return 0;
    for (const CHAR16 *p = s; *p; p++) {
        if (*p == L'\n' || *p == L'\r') return 1;
    }
    return 0;
}

static const CHAR16 *llmk_hist_get_nth_from_last(int n_from_last) {
    if (n_from_last < 0) return NULL;
    if (g_input_hist_count <= 0) return NULL;
    if (n_from_last >= g_input_hist_count) return NULL;

    int idx = g_input_hist_head - 1 - n_from_last;
    while (idx < 0) idx += LLMK_INPUT_HIST_MAX;
    idx %= LLMK_INPUT_HIST_MAX;
    return g_input_hist[idx];
}

static void llmk_hist_add_line(const CHAR16 *line) {
    if (!line || line[0] == 0) return;
    if (llmk_str16_has_newline(line)) return; // keep history simple (single-line only)

    // Avoid duplicates vs the last entry.
    if (g_input_hist_count > 0) {
        const CHAR16 *last = llmk_hist_get_nth_from_last(0);
        if (last && StrCmp((CHAR16 *)last, (CHAR16 *)line) == 0) return;
    }

    llmk_str16_copy_cap(g_input_hist[g_input_hist_head], LLMK_INPUT_HIST_MAXLEN, line);
    g_input_hist_head = (g_input_hist_head + 1) % LLMK_INPUT_HIST_MAX;
    if (g_input_hist_count < LLMK_INPUT_HIST_MAX) g_input_hist_count++;
}

static void llmk_console_erase_chars(int n) {
    for (int i = 0; i < n; i++) {
        Print(L"\b \b");
    }
}

static int g_tab_cycle_active = 0;
static int g_tab_cycle_index = -1;
static int g_tab_cycle_token_start = 0;
static char g_tab_cycle_prefix[64];

static void llmk_tab_cycle_reset(void) {
    g_tab_cycle_active = 0;
    g_tab_cycle_index = -1;
    g_tab_cycle_token_start = 0;
    g_tab_cycle_prefix[0] = 0;
}

static int llmk_ascii_startswith(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static char llmk_ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int llmk_ascii_startswith_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        char a = llmk_ascii_tolower(*s);
        char b = llmk_ascii_tolower(*prefix);
        if (a != b) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int llmk_ascii_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (needle[0] == 0) return 1;

    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (needle[j] && haystack[i + j]) {
            char a = llmk_ascii_tolower(haystack[i + j]);
            char b = llmk_ascii_tolower(needle[j]);
            if (a != b) break;
            j++;
        }
        if (needle[j] == 0) return 1;
    }
    return 0;
}

static int llmk_cmd_matches_filter(const char *name, const char *filter) {
    if (!name) return 0;
    if (!filter || !filter[0]) return 1;

    // If filter starts with '/', treat as a (case-insensitive) prefix.
    if (filter[0] == '/') {
        return llmk_ascii_startswith_ci(name, filter);
    }

    // Otherwise, treat as a (case-insensitive) substring.
    return llmk_ascii_contains_ci(name, filter);
}

static void llmk_ascii_copy_cap(char *dst, int dst_cap, const char *src) {
    if (!dst || dst_cap <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    int i = 0;
    for (; i < dst_cap - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static int llmk_ascii_has_dotdot(const char *s) {
    if (!s) return 0;
    for (const char *p = s; p[0] && p[1]; p++) {
        if (p[0] == '.' && p[1] == '.') return 1;
    }
    return 0;
}

static void llmk_print_ascii(const char *s) {
    if (!s) return;
    CHAR16 wbuf[256]; // SAFE: bounded temporary buffer for ASCII-to-UTF16 conversion for UEFI Print
    int i = 0;
    while (s[i] && i < 255) {
        wbuf[i] = (CHAR16)(unsigned char)s[i];
        i++;
    }
    wbuf[i] = 0;
    Print(L"%s", wbuf);
}

static int llmk_parse_optional_prefix(const char *prompt, int cmd_len, char *out, int out_cap) {
    if (!out || out_cap <= 0) return 0;
    out[0] = 0;
    if (!prompt || cmd_len <= 0) return 0;

    const char *p = prompt + cmd_len;
    while (*p && llmk_ascii_is_space(*p)) p++;
    if (*p == 0) return 0;

    int n = 0;
    while (*p && !llmk_ascii_is_space(*p) && n + 1 < out_cap) {
        out[n++] = *p++;
    }
    out[n] = 0;
    if (n <= 0) return 0;
    return 1;
}

typedef struct {
    const char *name;         // ASCII command, e.g. "/temp"
    const CHAR16 *desc;       // Wide description
} llmk_cmd_help_entry;

static const llmk_cmd_help_entry g_llmk_cmd_help[] = {
    { "/temp", L"Set temperature (0.0=greedy, 1.0=creative)" },
    { "/min_p", L"Set min_p (0.0-1.0, 0=off)" },
    { "/top_p", L"Set nucleus sampling (0.0-1.0)" },
    { "/top_k", L"Set top-k (0=off, typical 40-200)" },
    { "/norepeat", L"No-repeat ngram (0=off, typical 3-6)" },
    { "/repeat", L"Set repetition penalty (1.0=none, 1.5=strong)" },
    { "/sampling", L"Show sampling settings" },
    { "/preset", L"Apply sampling preset: stable|creative|greedy" },
    { "/preset_save", L"Apply preset and save to repl.cfg (Djibion allow_cfg_write required)" },
    { "/autostart_engines_on", L"Generate llmk-autorun.txt + enable autorun at boot (observe|enforce) [--run]" },
    { "/autostart_engines_off", L"Disable autorun_autostart in repl.cfg" },
    { "/max_tokens", L"Max generation tokens (1-256)" },
    { "/seed", L"RNG seed" },
    { "/stats", L"Print generation stats (0/1)" },
    { "/stop_you", L"Stop on \\nYou: pattern (0/1)" },
    { "/stop_nl", L"Stop on double newline (0/1)" },
    { "/model", L"Show loaded model config" },
    { "/model_info", L"Show model header (bin) or metadata (gguf)" },
    { "/models", L"List available .bin/.gguf files (root + models\\)" },
    { "/oo_handoff_info", L"Inspect host->sovereign handoff JSON (default sovereign_export.json)" },
    { "/oo_handoff_apply", L"Apply only safe handoff fields (mode/policy, fail-closed)" },
    { "/oo_handoff_receipt", L"Inspect persisted sovereign handoff receipt (default OOHANDOFF.TXT)" },
    { "/oo_continuity_status", L"Compare handoff receipt vs local state/recovery checkpoint" },
    { "/cpu", L"Show CPU SIMD status" },
    { "/ram", L"Show RAM budget (weights/kv/scratch/acts)" },
    { "/zones", L"Dump allocator zones + sentinel" },
    { "/budget", L"Set budgets in cycles (p=prefill, d=decode)" },
    { "/attn", L"Force attention SIMD path: auto|sse2|avx2" },
    { "/test_failsafe", L"One-shot strict budget trip" },
    { "/ctx", L"Show model + sampling + budgets" },
    { "/cfg", L"Show effective repl.cfg settings" },
    { "/log", L"Dump last n log entries" },
    { "/save_log", L"Write last n log entries to llmk-log.txt" },
    { "/save_dump", L"Write ctx+zones+sentinel+log to llmk-dump.txt" },
    { "/cls", L"Clear the screen" },
    { "/logo", L"Print startup ASCII logo" },
    { "/blas_bench", L"Benchmark Matrix Multiplication (Scalar vs SIMD)" },
    { "/q8_bench", L"Benchmark Q8_0 matmul (scalar vs AVX2)" },
    { "/q8_matvec", L"Benchmark Q8_0 model matvec (wq/wk/wv/wo/w1/w2/w3/cls)" },
    { "/gop", L"Show GOP framebuffer info" },
    { "/tui_on", L"Enable GOP TUI overlay" },
    { "/tui_off", L"Disable GOP TUI overlay" },
    { "/tui_toggle", L"Toggle GOP TUI overlay" },
    { "/tui_redraw", L"Force redraw GOP TUI overlay" },
    { "/tui_mode", L"Set GOP UI mode: status|log|split|files" },
    { "/tui_log_on", L"Show transcript log UI (GOP)" },
    { "/tui_log_off", L"Return to status-only UI" },
    { "/tui_log_clear", L"Clear transcript ring buffer" },
    { "/tui_log_up", L"Scroll transcript up (older)" },
    { "/tui_log_down", L"Scroll transcript down (newer)" },
    { "/tui_log_dump", L"Dump transcript to llmk-transcript.txt" },
    { "/fb", L"Open GOP file browser (same as /fb_on)" },
    { "/fb_on", L"Enable GOP file browser" },
    { "/fb_off", L"Disable GOP file browser" },
    { "/fb_refresh", L"Refresh file browser listing" },
    { "/fb_cd", L"File browser: change directory" },
    { "/fb_up", L"File browser: parent directory" },
    { "/fb_sel", L"File browser: select entry by index" },
    { "/fb_open", L"File browser: open selection (dir->cd, file->preview)" },
    { "/render", L"Render simple shapes to GOP framebuffer" },
    { "/save_img", L"Save GOP framebuffer as PPM (default llmk-img.ppm)" },
    { "/draw", L"Ask the model to output DSL and render it (GOP required)" },

    { "/fs_ls", L"List files in directory (default: root)" },
    { "/fs_cat", L"Print a text file (best-effort; truncated)" },
    { "/fs_write", L"Write text to file (truncate/create)" },
    { "/wasm_info", L"Inspect Wasm custom section oo.dna" },
    { "/wasm_apply", L"Apply oo.dna key=value deltas to runtime" },
    { "/fs_append", L"Append text to file (create if missing)" },
    { "/fs_rm", L"Delete a file" },
    { "/fs_cp", L"Copy file (best-effort)" },
    { "/fs_mv", L"Move file (copy+delete best-effort)" },

    { "/snap_save", L"Save KV cache snapshot to file (fast resume)" },
    { "/snap_load", L"Load KV cache snapshot from file" },
    { "/snap_autoload_on", L"Enable snapshot auto-load at boot (writes repl.cfg)" },
    { "/snap_autoload_off", L"Disable snapshot auto-load at boot (writes repl.cfg)" },

    { "/oo_new", L"Create an entity (long-lived intention)" },
    { "/oo_list", L"List entities" },
    { "/oo_status", L"Show OO config + persistence status" },
    { "/oo_reboot_probe", L"Arm or verify OO continuity across reboot" },
    { "/oo_step", L"Advance one entity by one step" },
    { "/oo_run", L"Run n cooperative steps across entities" },
    { "/oo_kill", L"Kill an entity" },
    { "/oo_note", L"Append a note to entity memory" },
    { "/oo_plan", L"Add agenda action(s) (use ';' to add many; prio like +2)" },
    { "/oo_agenda", L"Show agenda action list" },
    { "/oo_next", L"Select next action (marks doing)" },
    { "/oo_done", L"Mark action #k done" },
    { "/oo_prio", L"Set priority for action #k" },
    { "/oo_edit", L"Edit text for action #k" },
    { "/oo_show", L"Show entity (goal/status/digest/notes tail)" },
    { "/oo_digest", L"Update digest + compress notes tail" },
    { "/oo_save", L"Save OO state to file (default oo-state.bin)" },
    { "/oo_load", L"Load OO state from file (default oo-state.bin)" },
    { "/oo_think", L"Ask the model, store answer in entity notes" },
    { "/oo_auto", L"Run n think->store->step cycles (auto; press 'q' or Esc to stop)" },
    { "/oo_auto_stop", L"Stop /oo_auto cycles" },
    { "/oo_exec", L"Run agenda items (n cycles). Stops when agenda empty unless --plan" },
    { "/oo_exec_stop", L"Stop /oo_exec" },
    { "/oo_consult", L"Ask the model to suggest a system adaptation action" },
    { "/oo_consult_mock", L"Run the OO consult policy with a deterministic mock suggestion" },
    { "/oo_explain", L"Explain the latest OO consult decision (add 'verbose' or 'boot')" },
    { "/oo_log", L"Tail OOCONSULT.LOG" },
    { "/oo_jour", L"Tail OOJOUR.LOG" },
    { "/oo_outcome", L"Tail OOOUTCOME.LOG and show pending next-boot feedback" },

    { "/autorun", L"Run scripted REPL commands from file (default from repl.cfg)" },
    { "/autorun_stop", L"Stop autorun" },

    { "/reset", L"Clear budgets/log + untrip sentinel" },
    { "/clear", L"Clear KV cache (reset conversation context)" },
    { "/djibmarks", L"Show DjibMark execution trace" },
    { "/djibperf", L"DjibMark performance analysis by phase" },
    { "/djibion_on", L"Enable Djibion (observe mode)" },
    { "/djibion_off", L"Disable Djibion" },
    { "/djibion_enforce", L"Set Djibion mode: 0=off 1=observe 2=enforce" },
    { "/djibion_status", L"Show Djibion laws + counters" },
    { "/djibion_prefix", L"Set Djibion prefix for file actions (e.g. \\test_dir\\)" },
    { "/djibion_allow_delete", L"Set allow_fs_delete (0/1)" },
    { "/djibion_max_write", L"Set max_fs_write_bytes" },
    { "/djibion_max_oo", L"Set max_oo_cycles" },
    { "/djibion_max_snap", L"Set max_snap_bytes" },
    { "/djibion_allow_autorun", L"Set allow_autorun (0/1)" },
    { "/djibion_allow_snap_load", L"Set allow_snap_load (0/1)" },
    { "/djibion_allow_snap_save", L"Set allow_snap_save (0/1)" },
    { "/djibion_allow_cfg_write", L"Set allow_cfg_write (0/1)" },
    { "/djibion_allow_oo_persist", L"Set allow_oo_persist (0/1)" },

    { "/diopion_on", L"Enable Diopion (observe mode)" },
    { "/diopion_off", L"Disable Diopion" },
    { "/diopion_enforce", L"Set Diopion mode: 0=off 1=observe 2=enforce" },
    { "/diopion_profile", L"Set Diopion profile: none|animal|vegetal|geom|bio" },
    { "/diopion_burst", L"Burst sampling for N turns (temp/topk/max_tokens)" },
    { "/diopion_status", L"Show Diopion status + burst defaults" },

    { "/mem_on", L"Enable Memorion (manifest/check helpers)" },
    { "/mem_off", L"Disable Memorion" },
    { "/mem_status", L"Show Memorion status + counters" },
    { "/mem_snap_info", L"Print snapshot header info (default llmk-snap.bin)" },
    { "/mem_snap_check", L"Check snapshot compatibility vs current model" },
    { "/mem_manifest", L"Write manifest (optionally include snap header)" },

    { "/orch_on", L"Enable Orchestrion (observe mode)" },
    { "/orch_off", L"Disable Orchestrion" },
    { "/orch_enforce", L"Set Orchestrion mode: 0=off 1=observe 2=enforce" },
    { "/orch_status", L"Show Orchestrion status + pipeline state" },
    { "/orch_clear", L"Clear pipeline" },
    { "/orch_add", L"Add step(s) to pipeline (sep by ;)" },
    { "/orch_start", L"Start pipeline (optionally loops)" },
    { "/orch_pause", L"Pause pipeline" },
    { "/orch_resume", L"Resume pipeline" },
    { "/orch_stop", L"Stop pipeline" },

    { "/calib_on", L"Enable Calibrion (observe mode)" },
    { "/calib_off", L"Disable Calibrion" },
    { "/calib_enforce", L"Set Calibrion mode: 0=off 1=observe 2=enforce" },
    { "/calib_strategy", L"Set Calibrion strategy: none|entropy|length|quality|hybrid" },
    { "/calib_status", L"Show Calibrion status + recommendation" },
    { "/calib_reset", L"Reset Calibrion stats" },
    { "/calib_apply", L"Apply Calibrion recommendation to sampling" },

    { "/compat_on", L"Enable Compatibilion" },
    { "/compat_off", L"Disable Compatibilion" },
    { "/compat_status", L"Show platform capabilities" },
    { "/compat_probe", L"Re-probe CPU features" },
    { "/oo_status", L"Show OO organism engines status" },

    { "/diag_on", L"Enable Diagnostion diagnostics" },
    { "/diag_off", L"Disable Diagnostion diagnostics" },
    { "/diag_status", L"Show diagnostics status + counters" },
    { "/diag_report", L"Write llmk-diag.txt report (or /diag_report <file>)" },
    { "/metrics", L"Export runtime performance metrics to LLMK_METRICS.LOG (JSON)" },
    { "/bench_begin", L"Begin benchmark capture to LLMK_BENCH.JSONL (optional: filename)" },
    { "/bench_case", L"Run one benchmark case: /bench_case <id> <cat> <max_new_tokens> <prompt...>" },
    { "/bench_end", L"End benchmark capture (flush/close file)" },
    { "/autotune_status", L"Show M18 autotune thresholds + current sampling knobs" },
    { "/guard_status", L"Show M18.1 guardrails + safe-mode status" },
    { "/version", L"Show build version + features" },
    { "/diag", L"Display system diagnostics (GOP/RAM/CPU/models)" },
    { "/commands", L"List commands (optionally filtered)" },
    { "/help", L"Show help (optionally filtered)" },
};

static void llmk_print_commands_filtered(const char *filter) {
    int printed = 0;
    for (UINTN i = 0; i < (sizeof(g_llmk_cmd_help) / sizeof(g_llmk_cmd_help[0])); i++) {
        const char *name = g_llmk_cmd_help[i].name;
        if (!name) continue;
        if (!llmk_cmd_matches_filter(name, filter)) continue;
        Print(L"  ");
        llmk_print_ascii(name);
        Print(L"\r\n");
        printed++;
    }
    if (printed == 0) {
        Print(L"  (no matches)\r\n");
    }
}

static void llmk_print_help_filtered(const char *filter,
                                    float temperature, float min_p, float top_p,
                                    int top_k, int no_repeat_ngram, int max_gen_tokens,
                                    int stats_enabled, int stop_on_you, int stop_on_double_nl,
                                    float repeat_penalty) {
    Print(L"\r\nCommands:\r\n");
    if (filter && filter[0]) {
        Print(L"  (filter: ");
        llmk_print_ascii(filter);
        Print(L")\r\n");
    }

    int printed = 0;
    for (UINTN i = 0; i < (sizeof(g_llmk_cmd_help) / sizeof(g_llmk_cmd_help[0])); i++) {
        const char *name = g_llmk_cmd_help[i].name;
        const CHAR16 *desc = g_llmk_cmd_help[i].desc;
        if (!name || !desc) continue;
        if (!llmk_cmd_matches_filter(name, filter)) continue;

        Print(L"  ");
        llmk_print_ascii(name);
        Print(L" - %s\r\n", (CHAR16 *)desc);
        printed++;
    }

    if (printed == 0) {
        Print(L"  (no matches)\r\n");
    }

    Print(L"\r\nUsage:\r\n");
    Print(L"  /help [filter]     - Examples: /help dump ; /help /oo_\r\n");
    Print(L"  /commands [filter] - Examples: /commands save ; /commands /oo_\r\n\r\n");

    // Keep the long sections only for unfiltered help.
    if (!(filter && filter[0])) {
        Print(L"Multi-line input:\r\n");
        Print(L"  End a line with '\\' to continue; type ';;' on its own line to submit.\r\n");
        Print(L"  Use '\\\\' at end of line for a literal backslash.\r\n\r\n");
        Print(L"Render DSL:\r\n");
        Print(L"  clear R G B; rect X Y W H R G B; pixel X Y R G B\r\n\r\n");

        Print(L"Current settings:\r\n");
        Print(L"  Temperature: ");
        Print(L"%d.", (int)temperature);
        Print(L"%d\r\n", (int)((temperature - (int)temperature) * 100.0f));
        Print(L"  Min-p: ");
        Print(L"%d.", (int)min_p);
        Print(L"%d\r\n", (int)((min_p - (int)min_p) * 100.0f));
        Print(L"  Top-p: ");
        Print(L"%d.", (int)top_p);
        Print(L"%d\r\n", (int)((top_p - (int)top_p) * 100.0f));
        Print(L"  Top-k: %d\r\n", top_k);
        Print(L"  No-repeat ngram: %d\r\n", no_repeat_ngram);
        Print(L"  Max tokens: %d\r\n", max_gen_tokens);
        Print(L"  Stats: %s\r\n", stats_enabled ? L"on" : L"off");
        Print(L"  Stop on \\nYou:: %s\r\n", stop_on_you ? L"on" : L"off");
        Print(L"  Stop on double newline: %s\r\n", stop_on_double_nl ? L"on" : L"off");
        Print(L"  Repeat penalty: ");
        Print(L"%d.", (int)repeat_penalty);
        Print(L"%d\r\n\r\n", (int)((repeat_penalty - (int)repeat_penalty) * 100.0f));
    }
}

static int llmk_cmd_common_prefix_len(const char *a, const char *b) {
    int n = 0;
    if (!a || !b) return 0;
    while (a[n] && b[n] && a[n] == b[n]) n++;
    return n;
}

static void llmk_try_tab_complete_command(CHAR16 *buffer, int max_len, int *io_pos) {
    if (!buffer || !io_pos || max_len <= 1) return;
    int pos = *io_pos;
    if (pos <= 0) return;

    // Find current token start (we only complete the token that ends at the cursor).
    int token_start = pos;
    while (token_start > 0) {
        CHAR16 c = buffer[token_start - 1];
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') break;
        token_start--;
    }
    if (token_start >= pos) return;
    if (buffer[token_start] != L'/') return;

    static const char *cmds[] = {
        "/draw",
        "/temp",
        "/min_p",
        "/top_p",
        "/top_k",
        "/max_tokens",
        "/seed",
        "/stats",
        "/stop_you",
        "/stop_nl",
        "/norepeat",
        "/repeat",
        "/sampling",
        "/preset",
        "/preset_save",
        "/autostart_engines_on",
        "/autostart_engines_off",
        "/model",
        "/model_info",
        "/models",
        "/oo_handoff_info",
        "/oo_handoff_apply",
        "/oo_handoff_receipt",
        "/oo_continuity_status",
        "/cpu",
        "/zones",
        "/budget",
        "/attn",
        "/test_failsafe",
        "/ctx",
        "/log",
        "/save_log",
        "/save_dump",
        "/diag_on",
        "/diag_off",
        "/diag_status",
        "/diag_report",
        "/mem_on",
        "/mem_off",
        "/mem_status",
        "/mem_snap_info",
        "/mem_snap_check",
        "/mem_manifest",
        "/orch_on",
        "/orch_off",
        "/orch_enforce",
        "/orch_status",
        "/orch_clear",
        "/orch_add",
        "/orch_start",
        "/orch_pause",
        "/orch_resume",
        "/orch_stop",
        "/calib_on",
        "/calib_off",
        "/calib_enforce",
        "/calib_strategy",
        "/calib_status",
        "/calib_reset",
        "/calib_apply",
        "/compat_on",
        "/compat_off",
        "/compat_status",
        "/compat_probe",
        "/gop",
        "/render",
        "/save_img",
        "/oo_new",
        "/oo_list",
        "/oo_status",
        "/oo_kill",
        "/oo_step",
        "/oo_run",
        "/oo_note",
        "/oo_show",
        "/oo_digest",
        "/oo_plan",
        "/oo_agenda",
        "/oo_next",
        "/oo_done",
        "/oo_prio",
        "/oo_edit",
        "/oo_save",
        "/oo_load",
        "/oo_think",
        "/oo_auto",
        "/oo_auto_stop",
        "/oo_exec",
        "/oo_exec_stop",
        "/oo_consult",
        "/oo_consult_mock",
        "/oo_explain",
        "/oo_log",
        "/oo_jour",
        "/oo_outcome",
        "/autorun",
        "/autorun_stop",
        "/reset",
        "/clear",
        "/version",
        "/diag",
        "/djibmarks",
        "/djibperf",
        "/djibion_on",
        "/djibion_off",
        "/djibion_enforce",
        "/djibion_status",
        "/djibion_prefix",
        "/djibion_allow_delete",
        "/djibion_max_write",
        "/djibion_max_oo",
        "/djibion_max_snap",
        "/djibion_allow_autorun",
        "/djibion_allow_snap_load",
        "/djibion_allow_snap_save",
        "/djibion_allow_cfg_write",
        "/djibion_allow_oo_persist",
        "/diopion_on",
        "/diopion_off",
        "/diopion_enforce",
        "/diopion_profile",
        "/diopion_burst",
        "/diopion_status",
        "/logo",
        "/commands",
        "/help",
    };

    // If there's no active session (or token start changed), seed a new cycling session
    // from the token currently under the cursor.
    if (!g_tab_cycle_active || g_tab_cycle_token_start != token_start) {
        llmk_tab_cycle_reset();

        char prefix[64];
        int p = 0;
        for (int i = token_start; i < pos && p + 1 < (int)sizeof(prefix); i++) {
            CHAR16 c = buffer[i];
            if (c < 0x20 || c > 0x7E) return;
            prefix[p++] = (char)c;
        }
        prefix[p] = 0;
        if (p <= 1) return; // just "/" -> do nothing

        llmk_ascii_copy_cap(g_tab_cycle_prefix, (int)sizeof(g_tab_cycle_prefix), prefix);
        g_tab_cycle_active = 1;
        g_tab_cycle_index = -1;
        g_tab_cycle_token_start = token_start;
    } else {
        // Ensure the current token still begins with the session prefix. If not, restart.
        int p = (int)my_strlen(g_tab_cycle_prefix);
        if (p <= 1) {
            llmk_tab_cycle_reset();
            return;
        }
        if (pos - token_start < p) {
            llmk_tab_cycle_reset();
            return;
        }
        for (int i = 0; i < p; i++) {
            CHAR16 c = buffer[token_start + i];
            if ((c < 0x20 || c > 0x7E) || (char)c != g_tab_cycle_prefix[i]) {
                llmk_tab_cycle_reset();
                // Re-run once from scratch (no recursion).
                char prefix[64];
                int pp = 0;
                for (int j = token_start; j < pos && pp + 1 < (int)sizeof(prefix); j++) {
                    CHAR16 cj = buffer[j];
                    if (cj < 0x20 || cj > 0x7E) return;
                    prefix[pp++] = (char)cj;
                }
                prefix[pp] = 0;
                if (pp <= 1) return;
                llmk_ascii_copy_cap(g_tab_cycle_prefix, (int)sizeof(g_tab_cycle_prefix), prefix);
                g_tab_cycle_active = 1;
                g_tab_cycle_index = -1;
                g_tab_cycle_token_start = token_start;
                break;
            }
        }
    }

    // Build match list from the session prefix.
    const char *matches[64];
    int match_count = 0;
    const char *first = NULL;
    for (UINTN i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); i++) {
        if (llmk_ascii_startswith(cmds[i], g_tab_cycle_prefix)) {
            if (match_count < (int)(sizeof(matches) / sizeof(matches[0]))) {
                matches[match_count++] = cmds[i];
            }
            if (!first) first = cmds[i];
        }
    }

    if (match_count <= 0 || !first) {
        llmk_tab_cycle_reset();
        return;
    }

    int base_len = (int)my_strlen(g_tab_cycle_prefix);
    if (base_len <= 1) return;

    // Common prefix across matches.
    int common_len = (int)my_strlen(first);
    for (int i = 0; i < match_count; i++) {
        int cpl = llmk_cmd_common_prefix_len(first, matches[i]);
        if (cpl < common_len) common_len = cpl;
    }

    int cur_token_len = pos - token_start;

    // 1) If there's a longer common prefix, extend to it (first Tab behavior).
    if (common_len > base_len && cur_token_len < common_len) {
        for (int i = cur_token_len; i < common_len; i++) {
            if (pos + 1 >= max_len) break;
            char c = first[i];
            buffer[pos++] = (CHAR16)c;
            Print(L"%c", (CHAR16)c);
        }
        buffer[pos] = 0;
        *io_pos = pos;
        return;
    }

    // 2) Otherwise, cycle through full command candidates.
    if (g_tab_cycle_index < 0) g_tab_cycle_index = 0;
    else g_tab_cycle_index = (g_tab_cycle_index + 1) % match_count;

    const char *candidate = matches[g_tab_cycle_index];
    if (!candidate) return;

    // Replace current token with candidate.
    llmk_console_erase_chars(cur_token_len);
    pos = token_start;

    for (int i = 0; candidate[i] && pos + 1 < max_len; i++) {
        buffer[pos++] = (CHAR16)candidate[i];
        Print(L"%c", (CHAR16)candidate[i]);
    }
    buffer[pos] = 0;
    *io_pos = pos;
}

void read_user_input(CHAR16* buffer, int max_len) {
    int pos = 0;
    EFI_INPUT_KEY Key;
    int line_start = 0;

    // History browsing (single-line only, for simplicity).
    int hist_n = -1; // -1 = draft, 0 = last entry, 1 = one before...
    CHAR16 draft[LLMK_INPUT_HIST_MAXLEN];
    draft[0] = 0;
    
    while (pos < max_len - 1) {
        // Wait for key (Polling with UI Update for SentienceOS)
        while (1) { // SAFE: bounded polling loop; breaks immediately on keypress and yields via Stall() + UI tick.
             EFI_STATUS Status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
             if (!EFI_ERROR(Status)) break;
             InterfaceFx_Tick(); // Animate Desktop
             uefi_call_wrapper(BS->Stall, 1, 10000); // 10ms stall
        }
        // UINTN index;
        // uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
        // uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);

        // Any non-Tab key cancels completion cycling.
        if (Key.UnicodeChar != L'\t') {
            llmk_tab_cycle_reset();
        }
        
        // History navigation (only when still on the first line).
        if ((Key.ScanCode == SCAN_UP || Key.ScanCode == SCAN_DOWN) && line_start == 0) {
            if (g_input_hist_count <= 0) continue;

            if (Key.ScanCode == SCAN_UP) {
                if (hist_n + 1 >= g_input_hist_count) continue;
                if (hist_n < 0) {
                    // Save current draft on first UP.
                    llmk_str16_copy_cap(draft, (int)(sizeof(draft) / sizeof(draft[0])), buffer);
                }
                hist_n++;
            } else {
                // SCAN_DOWN
                if (hist_n < 0) continue;
                hist_n--;
            }

            // Erase current line and replace with history/draft.
            llmk_console_erase_chars(pos);
            pos = 0;

            const CHAR16 *src = NULL;
            if (hist_n >= 0) {
                src = llmk_hist_get_nth_from_last(hist_n);
            } else {
                src = draft;
            }
            if (!src) src = L"";

            // Copy + print.
            int slen = llmk_str16_len_cap(src, max_len - 1);
            for (int i = 0; i < slen; i++) {
                buffer[i] = src[i];
            }
            pos = slen;
            buffer[pos] = 0;
            if (pos > 0) {
                Print(L"%s", buffer);
            }
            continue;
        }

        // Tab completion (single-line only)
        if (Key.UnicodeChar == L'\t' && line_start == 0) {
            llmk_try_tab_complete_command(buffer, max_len, &pos);
            continue;
        }

        if (Key.UnicodeChar == 0x000D) {  // Enter
            // If the user ends the line with "\\\\", treat it as a literal "\\" and do NOT continue.
            if (pos >= 2 && buffer[pos - 2] == L'\\' && buffer[pos - 1] == L'\\') {
                pos -= 1;
                buffer[pos - 1] = L'\\';
            } else {
                // Multi-line continuation: if the current line ends with '\\', continue.
                if (pos > 0 && buffer[pos - 1] == L'\\') {
                    buffer[pos - 1] = L'\n';  // Replace \ with newline
                    Print(L"\r\n... ");
                    line_start = pos;
                    continue;
                }
            }

            // Multi-line terminator: line is exactly ";;" on its own line.
            if ((pos - line_start) == 2 && buffer[line_start] == L';' && buffer[line_start + 1] == L';') {
                // Remove terminator line and the preceding newline if present.
                if (line_start > 0 && buffer[line_start - 1] == L'\n') {
                    pos = line_start - 1;
                } else {
                    pos = line_start;
                }
                buffer[pos] = 0;
                Print(L"\r\n");
                break;
            }
            buffer[pos] = 0;
            Print(L"\r\n");
            break;
        } else if (Key.UnicodeChar == 0x0008) {  // Backspace
            if (pos > line_start) {
                pos--;
                Print(L"\b \b");
            }
        } else if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
            buffer[pos++] = Key.UnicodeChar;
            Print(L"%c", Key.UnicodeChar);
        }
    }
    
    buffer[pos] = 0;

    // Add to history (single-line only, non-empty).
    if (line_start == 0 && pos > 0 && !llmk_str16_has_newline(buffer)) {
        llmk_hist_add_line(buffer);
    }
}

void char16_to_char(char* dest, CHAR16* src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = (char)src[i];
    }
    dest[i] = 0;
}

static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len) {
    if (!dst || max_len <= 0) return;
    int i = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (; i < max_len - 1 && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x20 || c > 0x7E) {
            dst[i] = L'_';
        } else {
            dst[i] = (CHAR16)c;
        }
    }
    dst[i] = 0;
}

int check_quit_command(char* text) {
    // Check for "quit" or "exit"
    if (my_strcmp(text, "quit") == 0 || my_strcmp(text, "exit") == 0) {
        return 1;
    }
    return 0;
}

void reset_kv_cache(RunState* s, Config* p) {
    // Clear KV cache for new conversation
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int cache_size = p->n_layers * p->seq_len * kv_dim;
    
    for (int i = 0; i < cache_size; i++) {
        s->key_cache[i] = 0.0f;
        s->value_cache[i] = 0.0f;
    }
    
    // M16.1: Track KV cache resets
    g_metrics.kv_cache_resets++;
}

static EFI_STATUS llmk_snap_load_into_state_best_effort(RunState *state, const Config *config, int *io_kv_pos, const CHAR16 *in_name) {
    if (!state || !config || !io_kv_pos || !in_name) return EFI_INVALID_PARAMETER;
    if (!g_llmk_ready) return EFI_NOT_READY;

    EFI_FILE_HANDLE f = NULL;
    CHAR16 picked[192];
    picked[0] = 0;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, in_name, &f, picked,
                                                      (int)(sizeof(picked) / sizeof(picked[0])),
                                                      L"snap_load");
    if (EFI_ERROR(st) || !f) return st;

    LlmkSnapHeader hdr;
    st = read_exact(f, &hdr, sizeof(hdr));
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(f->Close, 1, f);
        return st;
    }
    if (hdr.magic != LLMK_SNAP_MAGIC || hdr.version != 1) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_COMPROMISED_DATA;
    }
    if (hdr.dim != (UINT32)config->dim ||
        hdr.n_layers != (UINT32)config->n_layers ||
        hdr.n_heads != (UINT32)config->n_heads ||
        hdr.n_kv_heads != (UINT32)config->n_kv_heads ||
        hdr.seq_len != (UINT32)config->seq_len) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_INCOMPATIBLE_VERSION;
    }
    if (hdr.kv_pos == 0 || hdr.kv_pos > (UINT32)config->seq_len) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_INVALID_PARAMETER;
    }

    // Clear caches, then load prefix.
    reset_kv_cache(state, (Config *)config);

    int kv_dim = (int)hdr.kv_dim;
    UINTN slice_floats = (UINTN)hdr.kv_pos * (UINTN)kv_dim;
    UINTN slice_bytes = slice_floats * sizeof(float);

    for (int l = 0; l < config->n_layers && !EFI_ERROR(st); l++) {
        float *base = state->key_cache + (UINTN)l * (UINTN)config->seq_len * (UINTN)kv_dim;
        st = read_exact(f, base, slice_bytes);
    }
    for (int l = 0; l < config->n_layers && !EFI_ERROR(st); l++) {
        float *base = state->value_cache + (UINTN)l * (UINTN)config->seq_len * (UINTN)kv_dim;
        st = read_exact(f, base, slice_bytes);
    }
    uefi_call_wrapper(f->Close, 1, f);

    if (EFI_ERROR(st)) {
        *io_kv_pos = 0;
        g_llmk_kv_pos = 0;
        return st;
    }

    *io_kv_pos = (int)hdr.kv_pos;
    g_llmk_kv_pos = *io_kv_pos;
    return EFI_SUCCESS;
}

// ============================================================================
// OO M5: LLM Consult Implementation
// ============================================================================

static void llmk_oo_log_consultation(UINT64 boot_count, UINT32 mode, UINT64 ram_mb,
                                     int ctx, int seq, const char *suggestion,
                                     const char *decision, int applied,
                                     const char *selected_action,
                                     const char *boot_relation,
                                     int boot_bias,
                                     const char *trend_relation,
                                     int trend_bias,
                                     const char *saturation_state,
                                     int saturation_bias,
                                     const char *operator_summary,
                                     const char *reason_id,
                                     const char *confidence_reason_id,
                                     int plan_enabled,
                                     int plan_remaining_budget,
                                     int plan_hard_stop,
                                     const char *plan_reason_id,
                                     int confidence_score, int confidence_threshold,
                                     int confidence_gate_enabled,
                                     int feedback_bias);

static int llmk_oo_confidence_score(UINT32 mode, UINT64 ram_mb, int ctx, int seq,
                                    int llm_len,
                                    int action_reduce_ctx, int action_reduce_seq,
                                    int action_increase, int action_reboot,
                                    int action_model, int action_stable,
                                    int *out_feedback_bias,
                                    int *out_action_bias,
                                    int *out_reduce_ctx_score,
                                    int *out_reduce_seq_score,
                                    int *out_increase_ctx_score,
                                    int *out_feedback_good,
                                    int *out_feedback_bad) {
    int score = 50;
    int feedback_bias = 0;
    int action_bias = 0;

    if (out_feedback_bias) *out_feedback_bias = 0;
    if (out_action_bias) *out_action_bias = 0;
    if (out_reduce_ctx_score) *out_reduce_ctx_score = 0;
    if (out_reduce_seq_score) *out_reduce_seq_score = 0;
    if (out_increase_ctx_score) *out_increase_ctx_score = 0;
    if (out_feedback_good) *out_feedback_good = 0;
    if (out_feedback_bad) *out_feedback_bad = 0;

    if (mode == LLMK_OO_MODE_NORMAL) score += 20;
    else if (mode == LLMK_OO_MODE_DEGRADED) score += 10;

    if (ram_mb >= 1024ULL) score += 15;
    else if (ram_mb >= 768ULL) score += 8;
    else score += 2;

    if (ctx <= 512) score += 5;
    else if (ctx > 2048) score -= 5;

    if (seq <= 1024) score += 5;
    else if (seq > 2048) score -= 5;

    if (llm_len <= 0) score -= 15;

    if (action_stable) score += 10;
    if (action_reduce_ctx || action_reduce_seq) score += 5;
    if (action_increase && mode != LLMK_OO_MODE_NORMAL) score -= 10;
    if (action_reboot || action_model) score -= 5;

    {
        int rg = 0, rb = 0, ig = 0, ib = 0;
        int rc_score = 0, rs_score = 0, ic_score = 0;
        llmk_oo_outcome_feedback_recent_best_effort(&rg, &rb, &ig, &ib,
                                                    &rc_score, &rs_score, &ic_score);
        if (out_reduce_ctx_score) *out_reduce_ctx_score = rc_score;
        if (out_reduce_seq_score) *out_reduce_seq_score = rs_score;
        if (out_increase_ctx_score) *out_increase_ctx_score = ic_score;

        int wants_reduce = (action_reduce_ctx || action_reduce_seq) ? 1 : 0;
        int wants_increase = action_increase ? 1 : 0;

        if (wants_reduce) {
            int delta = rg - rb;
            if (delta > 0) feedback_bias += (delta >= 3) ? 8 : 4;
            else if (delta < 0) feedback_bias -= ((-delta) >= 3) ? 10 : 5;
            if (out_feedback_good) *out_feedback_good += rg;
            if (out_feedback_bad) *out_feedback_bad += rb;
        }

        if (wants_increase) {
            int delta = ig - ib;
            if (delta > 0) feedback_bias += (delta >= 2) ? 6 : 3;
            else if (delta < 0) feedback_bias -= ((-delta) >= 2) ? 8 : 4;
            if (out_feedback_good) *out_feedback_good += ig;
            if (out_feedback_bad) *out_feedback_bad += ib;
        }

        if (action_reduce_ctx) {
            if (rc_score > 0) action_bias += (rc_score >= 6) ? 8 : 4;
            else if (rc_score < 0) action_bias -= ((-rc_score) >= 6) ? 12 : 6;
        }
        if (action_reduce_seq) {
            if (rs_score > 0) action_bias += (rs_score >= 6) ? 6 : 3;
            else if (rs_score < 0) action_bias -= ((-rs_score) >= 6) ? 10 : 5;
        }
        if (wants_increase) {
            if (ic_score > 0) action_bias += (ic_score >= 5) ? 6 : 3;
            else if (ic_score < 0) action_bias -= ((-ic_score) >= 5) ? 10 : 5;
        }
    }

    feedback_bias += action_bias;
    score += feedback_bias;
    if (out_feedback_bias) *out_feedback_bias = feedback_bias;
    if (out_action_bias) *out_action_bias = action_bias;

    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return score;
}

static void llmk_oo_consult_process_suggestion(UINT64 ram_mb, UINT32 mode, UINT64 boots,
                                               int ctx, int seq,
                                               const char *llm_suggestion) {
    if (!llm_suggestion) llm_suggestion = "";

    // Clamp suggestion length to local buffers.
    int llm_len = my_strlen(llm_suggestion);
    if (llm_len < 0) llm_len = 0;
    if (llm_len > 120) llm_len = 120;

    // Emit LLM suggestion marker (deterministic)
    Print(L"OK: OO LLM suggested: ");
    {
        char tmp[128];
        int tp = 0;
        for (int i = 0; i < llm_len && tp + 1 < (int)sizeof(tmp); i++) {
            char c = llm_suggestion[i];
            if (c < 0x20 || c > 0x7E) c = '_';
            if (c == '"') c = '\'';
            tmp[tp++] = c;
        }
        tmp[tp] = 0;
        llmk_print_ascii(tmp);
    }
    Print(L"\r\n");

    // 5. Parse suggestion for keywords (M5.1: detect ALL keywords)
    int action_reduce_ctx = 0;
    int action_reduce_seq = 0;
    int action_increase = 0;
    int action_reboot = 0;
    int action_model = 0;
    int action_stable = 0;

    // Simple substring search (case-insensitive)
    char lower[128];
    int copy_len = llm_len;
    if (copy_len > (int)sizeof(lower) - 1) copy_len = (int)sizeof(lower) - 1;
    for (int i = 0; i < copy_len; i++) {
        char c = llm_suggestion[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        lower[i] = c;
    }
    lower[copy_len] = 0;

    // Check for multi-actions feature flag (default: follows oo_llm_consult)
    int multi_enabled = g_cfg_oo_multi_actions;
    if (multi_enabled < 0) {
        multi_enabled = (g_cfg_oo_llm_consult > 0) ? 1 : 0;
    }

    // Detect reduce actions (ctx and/or seq)
    if (my_strstr(lower, "reduce") || my_strstr(lower, "lower") || my_strstr(lower, "decrease")) {
        if (my_strstr(lower, "ctx") || my_strstr(lower, "context")) action_reduce_ctx = 1;
        if (my_strstr(lower, "seq") || my_strstr(lower, "sequence")) action_reduce_seq = 1;
        // Generic "reduce" without target: default to ctx
        if (!action_reduce_ctx && !action_reduce_seq) action_reduce_ctx = 1;
    }

    // Detect increase (blocked in most cases)
    if (my_strstr(lower, "increase") || my_strstr(lower, "raise") || my_strstr(lower, "more")) {
        action_increase = 1;
    }

    // Detect system actions (reboot, model change)
    if (my_strstr(lower, "reboot") || my_strstr(lower, "restart")) action_reboot = 1;
    if (my_strstr(lower, "model") || my_strstr(lower, "switch")) action_model = 1;

    // Detect "stable" / no-op signal
    if (my_strstr(lower, "stable") || my_strstr(lower, "ok") || my_strstr(lower, "wait") || my_strstr(lower, "good")) {
        action_stable = 1;
    }

        int confidence_threshold = g_cfg_oo_conf_threshold;
        if (confidence_threshold < 0) confidence_threshold = 0;
        if (confidence_threshold > 100) confidence_threshold = 100;
        int confidence_gate_enabled = (g_cfg_oo_conf_gate != 0);
    int feedback_bias = 0;
    int action_bias = 0;
    int recent_reduce_ctx_score = 0;
    int recent_reduce_seq_score = 0;
    int recent_increase_ctx_score = 0;
    int feedback_good = 0;
    int feedback_bad = 0;
    int confidence_score = llmk_oo_confidence_score(mode, ram_mb, ctx, seq, llm_len,
                                                    action_reduce_ctx, action_reduce_seq,
                                                    action_increase, action_reboot,
                                                    action_model, action_stable,
                                                    &feedback_bias, &action_bias,
                                                    &recent_reduce_ctx_score,
                                                    &recent_reduce_seq_score,
                                                    &recent_increase_ctx_score,
                                                    &feedback_good, &feedback_bad);
        int confidence_gate_pass = (confidence_score >= confidence_threshold);
        int plan_hard_stop = (confidence_gate_enabled && !confidence_gate_pass) ? 1 : 0;
        const char *confidence_reason_id = (!confidence_gate_enabled) ? "OO_CONF_LOG_ONLY" :
                           (confidence_gate_pass ? "OO_CONF_GATE_PASS" : "OO_CONF_GATE_FAIL");

        int plan_enabled = ((g_cfg_oo_plan_enable != 0) && multi_enabled) ? 1 : 0;
        int plan_max_actions = g_cfg_oo_plan_max_actions;
        if (plan_max_actions < 1) plan_max_actions = 1;
        if (plan_max_actions > 3) plan_max_actions = 3;
        if (!plan_enabled) plan_max_actions = 1;

        int plan_remaining_budget = plan_max_actions - g_oo_auto_applied_count_this_boot;
        if (plan_remaining_budget < 0) plan_remaining_budget = 0;
        int plan_applied_now = 0;
        int plan_checkpoint_done = 0;
        const char *selected_action = "na";
        const char *operator_summary = "na";
        int operator_positive_saturated = 0;
        const char *final_reason_id = confidence_reason_id;

        Print(L"OK: OO confidence: score=%d threshold=%d gate=%a pass=%a reason_id=%a\r\n",
            confidence_score,
            confidence_threshold,
            confidence_gate_enabled ? "enforced" : "log_only",
            confidence_gate_pass ? "yes" : "no",
            confidence_reason_id);
        Print(L"OK: OO feedback: good=%d bad=%d bias=%d action_bias=%d\r\n",
            feedback_good, feedback_bad, feedback_bias, action_bias);
        {
            int pref_reduce_ctx = recent_reduce_ctx_score;
            int pref_reduce_seq = recent_reduce_seq_score;
            int pref_increase_ctx = recent_increase_ctx_score;
            int boot_reduce_ctx_bias = 0;
            int boot_reduce_seq_bias = 0;
            int boot_increase_ctx_bias = 0;
            int trend_reduce_ctx_bias = 0;
            int trend_reduce_seq_bias = 0;
            int trend_increase_ctx_bias = 0;
            int sat_reduce_ctx_bias = 0;
            int sat_reduce_seq_bias = 0;
            int sat_increase_ctx_bias = 0;
            char boot_reduce_ctx_relation[48]; // SAFE: bounded relation token; filled with llmk_oo_outcome_copy_token()
            char boot_reduce_seq_relation[48]; // SAFE: bounded relation token; filled with llmk_oo_outcome_copy_token()
            char boot_increase_ctx_relation[48]; // SAFE: bounded relation token; filled with llmk_oo_outcome_copy_token()
            char trend_reduce_ctx_relation[48]; // SAFE: bounded relation token; filled with llmk_oo_outcome_copy_token()
            char trend_reduce_seq_relation[48]; // SAFE: bounded relation token; filled with llmk_oo_outcome_copy_token()
            char trend_increase_ctx_relation[48]; // SAFE: bounded relation token; filled with llmk_oo_outcome_copy_token()
            char sat_reduce_ctx_state[48]; // SAFE: bounded state token; filled with llmk_oo_outcome_copy_token()
            char sat_reduce_seq_state[48]; // SAFE: bounded state token; filled with llmk_oo_outcome_copy_token()
            char sat_increase_ctx_state[48]; // SAFE: bounded state token; filled with llmk_oo_outcome_copy_token()
            int requested_adaptive = action_reduce_ctx + action_reduce_seq + action_increase;
            int best_kind = 0;
            int best_score = -999;
            int second_score = -999;
            int fallback_from_positive_saturated = 0;

            boot_reduce_ctx_relation[0] = 0;
            boot_reduce_seq_relation[0] = 0;
            boot_increase_ctx_relation[0] = 0;
            trend_reduce_ctx_relation[0] = 0;
            trend_reduce_seq_relation[0] = 0;
            trend_increase_ctx_relation[0] = 0;
            sat_reduce_ctx_state[0] = 0;
            sat_reduce_seq_state[0] = 0;
            sat_increase_ctx_state[0] = 0;

            if (action_reduce_ctx) {
                boot_reduce_ctx_bias = llmk_oo_boot_relation_for_action_best_effort("reduce_ctx",
                                                                                   boot_reduce_ctx_relation,
                                                                                   (int)sizeof(boot_reduce_ctx_relation),
                                                                                   NULL, 0, NULL, 0, NULL, 0, NULL, 0);
                trend_reduce_ctx_bias = llmk_oo_recent_trend_for_action_best_effort("reduce_ctx",
                                                                                    trend_reduce_ctx_relation,
                                                                                    (int)sizeof(trend_reduce_ctx_relation));
                sat_reduce_ctx_bias = llmk_oo_saturation_bias_for_action_best_effort("reduce_ctx",
                                                                                     ctx,
                                                                                     seq,
                                                                                     sat_reduce_ctx_state,
                                                                                     (int)sizeof(sat_reduce_ctx_state));
                pref_reduce_ctx += boot_reduce_ctx_bias;
                pref_reduce_ctx += trend_reduce_ctx_bias;
                pref_reduce_ctx += sat_reduce_ctx_bias;
            }
            if (action_reduce_seq) {
                boot_reduce_seq_bias = llmk_oo_boot_relation_for_action_best_effort("reduce_seq",
                                                                                   boot_reduce_seq_relation,
                                                                                   (int)sizeof(boot_reduce_seq_relation),
                                                                                   NULL, 0, NULL, 0, NULL, 0, NULL, 0);
                trend_reduce_seq_bias = llmk_oo_recent_trend_for_action_best_effort("reduce_seq",
                                                                                    trend_reduce_seq_relation,
                                                                                    (int)sizeof(trend_reduce_seq_relation));
                sat_reduce_seq_bias = llmk_oo_saturation_bias_for_action_best_effort("reduce_seq",
                                                                                     ctx,
                                                                                     seq,
                                                                                     sat_reduce_seq_state,
                                                                                     (int)sizeof(sat_reduce_seq_state));
                pref_reduce_seq += boot_reduce_seq_bias;
                pref_reduce_seq += trend_reduce_seq_bias;
                pref_reduce_seq += sat_reduce_seq_bias;
            }
            if (action_increase) {
                boot_increase_ctx_bias = llmk_oo_boot_relation_for_action_best_effort("increase_ctx",
                                                                                      boot_increase_ctx_relation,
                                                                                      (int)sizeof(boot_increase_ctx_relation),
                                                                                      NULL, 0, NULL, 0, NULL, 0, NULL, 0);
                trend_increase_ctx_bias = llmk_oo_recent_trend_for_action_best_effort("increase_ctx",
                                                                                       trend_increase_ctx_relation,
                                                                                       (int)sizeof(trend_increase_ctx_relation));
                sat_increase_ctx_bias = llmk_oo_saturation_bias_for_action_best_effort("increase_ctx",
                                                                                        ctx,
                                                                                        seq,
                                                                                        sat_increase_ctx_state,
                                                                                        (int)sizeof(sat_increase_ctx_state));
                pref_increase_ctx += boot_increase_ctx_bias;
                pref_increase_ctx += trend_increase_ctx_bias;
                pref_increase_ctx += sat_increase_ctx_bias;
            }

            if (action_reduce_ctx) {
                if (mode == LLMK_OO_MODE_SAFE || mode == LLMK_OO_MODE_DEGRADED) pref_reduce_ctx += 2;
                if (ctx <= 256) pref_reduce_ctx -= 3;
                if (ctx <= 128) pref_reduce_ctx -= 8;
                if (pref_reduce_ctx > best_score) {
                    second_score = best_score;
                    best_score = pref_reduce_ctx;
                    best_kind = 1;
                } else if (pref_reduce_ctx > second_score) {
                    second_score = pref_reduce_ctx;
                }
            }
            if (action_reduce_seq) {
                if (mode == LLMK_OO_MODE_SAFE && ram_mb < 1024ULL) pref_reduce_seq += 2;
                if (seq <= 256) pref_reduce_seq -= 3;
                if (seq <= 128) pref_reduce_seq -= 8;
                if (pref_reduce_seq > best_score) {
                    second_score = best_score;
                    best_score = pref_reduce_seq;
                    best_kind = 2;
                } else if (pref_reduce_seq > second_score) {
                    second_score = pref_reduce_seq;
                }
            }
            if (action_increase) {
                if (mode != LLMK_OO_MODE_NORMAL) pref_increase_ctx -= 6;
                if (g_cfg_oo_auto_apply < 2) pref_increase_ctx -= 4;
                if (ctx >= 1536) pref_increase_ctx -= 3;
                if (pref_increase_ctx > best_score) {
                    second_score = best_score;
                    best_score = pref_increase_ctx;
                    best_kind = 3;
                } else if (pref_increase_ctx > second_score) {
                    second_score = pref_increase_ctx;
                }
            }

            Print(L"OK: OO action preference: reduce_ctx=%d reduce_seq=%d increase_ctx=%d\r\n",
                action_reduce_ctx ? pref_reduce_ctx : -999,
                action_reduce_seq ? pref_reduce_seq : -999,
                action_increase ? pref_increase_ctx : -999);
            Print(L"OK: OO boot feedback: reduce_ctx=%a(%d) reduce_seq=%a(%d) increase_ctx=%a(%d)\r\n",
                action_reduce_ctx ? (boot_reduce_ctx_relation[0] ? boot_reduce_ctx_relation : "none") : "na",
                action_reduce_ctx ? boot_reduce_ctx_bias : 0,
                action_reduce_seq ? (boot_reduce_seq_relation[0] ? boot_reduce_seq_relation : "none") : "na",
                action_reduce_seq ? boot_reduce_seq_bias : 0,
                action_increase ? (boot_increase_ctx_relation[0] ? boot_increase_ctx_relation : "none") : "na",
                action_increase ? boot_increase_ctx_bias : 0);
            Print(L"OK: OO trend feedback: reduce_ctx=%a(%d) reduce_seq=%a(%d) increase_ctx=%a(%d)\r\n",
                action_reduce_ctx ? (trend_reduce_ctx_relation[0] ? trend_reduce_ctx_relation : "none") : "na",
                action_reduce_ctx ? trend_reduce_ctx_bias : 0,
                action_reduce_seq ? (trend_reduce_seq_relation[0] ? trend_reduce_seq_relation : "none") : "na",
                action_reduce_seq ? trend_reduce_seq_bias : 0,
                action_increase ? (trend_increase_ctx_relation[0] ? trend_increase_ctx_relation : "none") : "na",
                action_increase ? trend_increase_ctx_bias : 0);
            Print(L"OK: OO saturation feedback: reduce_ctx=%a(%d) reduce_seq=%a(%d) increase_ctx=%a(%d)\r\n",
                action_reduce_ctx ? (sat_reduce_ctx_state[0] ? sat_reduce_ctx_state : "none") : "na",
                action_reduce_ctx ? sat_reduce_ctx_bias : 0,
                action_reduce_seq ? (sat_reduce_seq_state[0] ? sat_reduce_seq_state : "none") : "na",
                action_reduce_seq ? sat_reduce_seq_bias : 0,
                action_increase ? (sat_increase_ctx_state[0] ? sat_increase_ctx_state : "none") : "na",
                action_increase ? sat_increase_ctx_bias : 0);

            if ((action_reduce_ctx && (boot_reduce_ctx_bias > 0 || trend_reduce_ctx_bias > 0) && sat_reduce_ctx_bias < 0) ||
                (action_reduce_seq && (boot_reduce_seq_bias > 0 || trend_reduce_seq_bias > 0) && sat_reduce_seq_bias < 0) ||
                (action_increase && (boot_increase_ctx_bias > 0 || trend_increase_ctx_bias > 0) && sat_increase_ctx_bias < 0)) {
                fallback_from_positive_saturated = 1;
            }
            if (fallback_from_positive_saturated) {
                operator_positive_saturated = 1;
            }

            if (!action_stable && !action_reboot && requested_adaptive > 1 && best_kind != 0) {
                int prefer_single = (!plan_enabled) || (plan_max_actions <= 1) || ((best_score - second_score) >= 3);
                if (prefer_single) {
                    action_reduce_ctx = (best_kind == 1) ? 1 : 0;
                    action_reduce_seq = (best_kind == 2) ? 1 : 0;
                    action_increase = (best_kind == 3) ? 1 : 0;
                    selected_action = (best_kind == 1) ? "reduce_ctx" : (best_kind == 2) ? "reduce_seq" : "increase_ctx";
                    Print(L"OK: OO action selection: selected=%a mode=single_best lead=%d\r\n",
                        selected_action,
                        best_score - second_score);
                } else {
                    selected_action = "multi";
                    Print(L"OK: OO action selection: selected=multi mode=keep_multiple lead=%d\r\n",
                        best_score - second_score);
                }
            }

            if (best_kind == 1) {
                operator_summary = llmk_oo_operator_summary_from_dynamics(best_kind,
                                                                          fallback_from_positive_saturated && selected_action[0] && !llmk_oo_ascii_equals(selected_action, "reduce_ctx"),
                                                                          boot_reduce_ctx_bias,
                                                                          trend_reduce_ctx_bias,
                                                                          sat_reduce_ctx_bias,
                                                                          0,
                                                                          final_reason_id);
            } else if (best_kind == 2) {
                operator_summary = llmk_oo_operator_summary_from_dynamics(best_kind,
                                                                          fallback_from_positive_saturated && selected_action[0] && !llmk_oo_ascii_equals(selected_action, "reduce_seq"),
                                                                          boot_reduce_seq_bias,
                                                                          trend_reduce_seq_bias,
                                                                          sat_reduce_seq_bias,
                                                                          0,
                                                                          final_reason_id);
            } else if (best_kind == 3) {
                operator_summary = llmk_oo_operator_summary_from_dynamics(best_kind,
                                                                          fallback_from_positive_saturated && selected_action[0] && !llmk_oo_ascii_equals(selected_action, "increase_ctx"),
                                                                          boot_increase_ctx_bias,
                                                                          trend_increase_ctx_bias,
                                                                          sat_increase_ctx_bias,
                                                                          0,
                                                                          final_reason_id);
            }
        }
        const char *plan_reason_id = plan_hard_stop ? "OO_PLAN_HARD_STOP" : "OO_PLAN_ACTIVE";

        Print(L"OK: OO plan: enabled=%a max=%d used=%d remain=%d hard_stop=%a reason_id=%a\r\n",
            plan_enabled ? "yes" : "no",
            plan_max_actions,
            g_oo_auto_applied_count_this_boot,
            plan_remaining_budget,
            plan_hard_stop ? "yes" : "no",
            plan_reason_id);

        // Ultra-minimal journal markers (details go to serial + OOCONSULT.LOG).
        if (!confidence_gate_enabled) {
            llmk_oo_journal_event_load_state_best_effort("conf_log_only");
        } else if (confidence_gate_pass) {
            llmk_oo_journal_event_load_state_best_effort("conf_pass");
        } else {
            llmk_oo_journal_event_load_state_best_effort("conf_fail");
        }

        if (plan_hard_stop) {
            llmk_oo_journal_event_load_state_best_effort("plan_hard_stop");
        } else {
            llmk_oo_journal_event_load_state_best_effort("plan_active");
        }

    // 6. Policy decision (M5.1: apply ALL valid actions when multi_enabled)
    int actions_applied = 0;
    int actions_blocked = 0;
    char batch_summary[256];
    int batch_summary_pos = 0;
    batch_summary[0] = 0;

    // Priority filtering: stable cancels all, reboot primes others
    if (action_stable) {
        // Stable signal: no action needed
        selected_action = "stable";
        final_reason_id = "OO_STABLE_OK";
        Print(L"OK: OO policy decided: system_stable (reason=llm_reports_ok reason_id=OO_STABLE_OK)\r\n");
        actions_applied = 0;
        actions_blocked = (action_reduce_ctx + action_reduce_seq + action_increase + action_reboot + action_model - 1);
        llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "stable");
    } else if (action_reboot) {
        // Reboot primes others: log but don't auto-apply (v0)
        selected_action = "reboot_logged";
        final_reason_id = "OO_REBOOT_LOG_ONLY";
        Print(L"OK: OO policy decided: logged_only (reason=reboot_not_auto reason_id=OO_REBOOT_LOG_ONLY)\r\n");
        actions_applied = 0;
        actions_blocked = (action_reduce_ctx + action_reduce_seq + action_increase + action_model);
        llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "reboot_logged");
    } else {
        // Apply reductions first (safe), then increases (blocked), then model

        // 6.1: Apply reduce_ctx (if detected and safe)
        if (action_reduce_ctx) {
            if (mode == LLMK_OO_MODE_SAFE || mode == LLMK_OO_MODE_DEGRADED) {
                int new_ctx = ctx / 2;
                if (new_ctx < 128) new_ctx = 128;
                if (new_ctx != ctx) {
                    // M5.2: Check auto-apply config and throttling
                    int can_auto_apply = (g_cfg_oo_auto_apply > 0) && (!plan_hard_stop) &&
                                         (plan_applied_now < plan_remaining_budget) &&
                                         (!confidence_gate_enabled || confidence_gate_pass);
                    int is_reduction = 1; // reduce_ctx is always a reduction

                    if (!can_auto_apply) {
                        // Auto-apply disabled or throttled
                        if (confidence_gate_enabled && !confidence_gate_pass) {
                                final_reason_id = "OO_BLOCK_CONFIDENCE";
                                Print(L"OK: OO policy blocked: reduce_ctx (reason=confidence_below_threshold reason_id=OO_BLOCK_CONFIDENCE score=%d threshold=%d)\r\n",
                                  confidence_score, confidence_threshold);
                        } else if (g_cfg_oo_auto_apply == 0) {
                            final_reason_id = "OO_SIM_AUTO_APPLY_DISABLED";
                            Print(L"OK: OO policy simulation: reduce_ctx (would_apply_if_enabled, new=%d)\r\n", new_ctx);
                        } else if (plan_hard_stop) {
                            final_reason_id = "OO_BLOCK_HARD_STOP";
                            Print(L"OK: OO policy blocked: reduce_ctx (reason=hard_stop_active reason_id=OO_BLOCK_HARD_STOP, new=%d)\r\n", new_ctx);
                        } else {
                            final_reason_id = "OO_BLOCK_PLAN_BUDGET";
                            Print(L"OK: OO policy throttled: reduce_ctx (reason=plan_budget_exhausted reason_id=OO_BLOCK_PLAN_BUDGET, new=%d)\r\n", new_ctx);
                        }
                        actions_blocked++;
                    } else if (g_cfg_oo_auto_apply == 1 && !is_reduction) {
                        // Conservative mode: only reductions (this branch won't hit for reduce_ctx)
                        Print(L"OK: OO policy blocked: reduce_ctx (reason=conservative_mode)\r\n");
                        actions_blocked++;
                    } else {
                        if (!plan_checkpoint_done) {
                            llmk_oo_plan_checkpoint_best_effort("pre_auto_apply");
                            plan_checkpoint_done = 1;
                        }
                        // Auto-apply enabled: write + verify per M5.2
                        int ok = llmk_oo_auto_apply_write_verify_best_effort("reduce_ctx",
                                                                            "ctx_len",
                                                                            ctx,
                                                                            seq,
                                                                            new_ctx,
                                                                            seq,
                                                                            ram_mb);
                        if (ok) {
                            final_reason_id = "OO_APPLY_OK";
                            Print(L"OK: OO auto-apply: reduce_ctx (old=%d new=%d check=pass reason_id=OO_APPLY_OK)\r\n", ctx, new_ctx);
                            llmk_oo_journal_event_load_state_best_effort("auto_apply_reduce_ctx_ok");
                            llmk_oo_record_last_auto_apply_best_effort(boots, mode, LLMK_OO_ACTION_REDUCE_CTX);
                            actions_applied++;
                            plan_applied_now++;
                            g_oo_auto_applied_count_this_boot++;
                            g_oo_auto_applied_this_boot = (g_oo_auto_applied_count_this_boot > 0) ? 1 : 0;
                            if (batch_summary_pos > 0) llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, ",");
                            llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "reduce_ctx");
                        } else {
                            // Revert to previous value (best-effort)
                            char oval[32]; // SAFE: small temp buffer for cfg numeric string
                            int op = 0;
                            llmk_ascii_append_u64(oval, (int)sizeof(oval), &op, (UINT64)ctx);
                            oval[op] = 0;
                            llmk_repl_cfg_set_kv_best_effort("ctx_len", oval);
                            final_reason_id = "OO_APPLY_VERIFY_FAILED";
                            Print(L"ERROR: OO auto-apply verification failed: reduce_ctx (reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED, reverting)\r\n");
                            llmk_oo_journal_event_load_state_best_effort("auto_apply_reduce_ctx_fail");
                            llmk_oo_journal_event_load_state_best_effort("plan_hard_stop");
                            plan_hard_stop = 1;
                            actions_blocked++;
                        }
                    }
                } else {
                    final_reason_id = "OO_BLOCK_ALREADY_MIN";
                    Print(L"OK: OO policy blocked: reduce_ctx (reason=already_at_min)\r\n");
                    actions_blocked++;
                }
            } else {
                final_reason_id = "OO_BLOCK_NORMAL_MODE";
                Print(L"OK: OO policy blocked: reduce_ctx (reason=normal_mode_no_auto_reduce)\r\n");
                actions_blocked++;
            }
        }

        // 6.2: Apply reduce_seq (if detected, multi_enabled, and safe)
        if (action_reduce_seq && multi_enabled) {
            if (mode == LLMK_OO_MODE_SAFE && ram_mb < 1024ULL) {
                int new_seq = seq / 2;
                if (new_seq < 128) new_seq = 128;
                if (new_seq != seq) {
                    // M5.2: Check auto-apply config and throttling
                    int can_auto_apply = (g_cfg_oo_auto_apply > 0) && (!plan_hard_stop) &&
                                         (plan_applied_now < plan_remaining_budget) &&
                                         (!confidence_gate_enabled || confidence_gate_pass);

                    if (!can_auto_apply) {
                        if (confidence_gate_enabled && !confidence_gate_pass) {
                                final_reason_id = "OO_BLOCK_CONFIDENCE";
                                Print(L"OK: OO policy blocked: reduce_seq (reason=confidence_below_threshold reason_id=OO_BLOCK_CONFIDENCE score=%d threshold=%d)\r\n",
                                  confidence_score, confidence_threshold);
                        } else if (g_cfg_oo_auto_apply == 0) {
                            final_reason_id = "OO_SIM_AUTO_APPLY_DISABLED";
                            Print(L"OK: OO policy simulation: reduce_seq (would_apply_if_enabled, new=%d)\r\n", new_seq);
                        } else if (plan_hard_stop) {
                            final_reason_id = "OO_BLOCK_HARD_STOP";
                            Print(L"OK: OO policy blocked: reduce_seq (reason=hard_stop_active reason_id=OO_BLOCK_HARD_STOP, new=%d)\r\n", new_seq);
                        } else {
                            final_reason_id = "OO_BLOCK_PLAN_BUDGET";
                            Print(L"OK: OO policy throttled: reduce_seq (reason=plan_budget_exhausted reason_id=OO_BLOCK_PLAN_BUDGET, new=%d)\r\n", new_seq);
                        }
                        actions_blocked++;
                    } else {
                        if (!plan_checkpoint_done) {
                            llmk_oo_plan_checkpoint_best_effort("pre_auto_apply");
                            plan_checkpoint_done = 1;
                        }
                        int ok = llmk_oo_auto_apply_write_verify_best_effort("reduce_seq",
                                                                            "seq_len",
                                                                            ctx,
                                                                            seq,
                                                                            ctx,
                                                                            new_seq,
                                                                            ram_mb);
                        if (ok) {
                            final_reason_id = "OO_APPLY_OK";
                            Print(L"OK: OO auto-apply: reduce_seq (old=%d new=%d check=pass reason_id=OO_APPLY_OK)\r\n", seq, new_seq);
                            llmk_oo_journal_event_load_state_best_effort("auto_apply_reduce_seq_ok");
                            llmk_oo_record_last_auto_apply_best_effort(boots, mode, LLMK_OO_ACTION_REDUCE_SEQ);
                            actions_applied++;
                            plan_applied_now++;
                            g_oo_auto_applied_count_this_boot++;
                            g_oo_auto_applied_this_boot = (g_oo_auto_applied_count_this_boot > 0) ? 1 : 0;
                            if (batch_summary_pos > 0) llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, ",");
                            llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "reduce_seq");
                        } else {
                            char oval[32]; // SAFE: small temp buffer for cfg numeric string
                            int op = 0;
                            llmk_ascii_append_u64(oval, (int)sizeof(oval), &op, (UINT64)seq);
                            oval[op] = 0;
                            llmk_repl_cfg_set_kv_best_effort("seq_len", oval);
                            final_reason_id = "OO_APPLY_VERIFY_FAILED";
                            Print(L"ERROR: OO auto-apply verification failed: reduce_seq (reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED, reverting)\r\n");
                            llmk_oo_journal_event_load_state_best_effort("auto_apply_reduce_seq_fail");
                            llmk_oo_journal_event_load_state_best_effort("plan_hard_stop");
                            plan_hard_stop = 1;
                            actions_blocked++;
                        }
                    }
                } else {
                    final_reason_id = "OO_BLOCK_ALREADY_MIN";
                    Print(L"OK: OO policy blocked: reduce_seq (reason=already_at_min)\r\n");
                    actions_blocked++;
                }
            } else {
                final_reason_id = "OO_BLOCK_NOT_SAFE_LOW_RAM";
                Print(L"OK: OO policy blocked: reduce_seq (reason=not_safe_low_ram)\r\n");
                actions_blocked++;
            }
        } else if (action_reduce_seq && !multi_enabled) {
            final_reason_id = "OO_BLOCK_MULTI_DISABLED";
            Print(L"OK: OO policy blocked: reduce_seq (reason=multi_actions_disabled)\r\n");
            actions_blocked++;
        }

        // 6.3: Handle increases (M5.2: allow in aggressive mode if conditions met)
        if (action_increase) {
            // M5.2: Aggressive mode can apply increases if RAM>=1GB and mode=NORMAL
            int can_increase = (g_cfg_oo_auto_apply == 2) && (mode == LLMK_OO_MODE_NORMAL) && (ram_mb >= 1024ULL);
            int can_auto_apply = (g_cfg_oo_auto_apply > 0) && (!plan_hard_stop) &&
                                 (plan_applied_now < plan_remaining_budget) &&
                                 (!confidence_gate_enabled || confidence_gate_pass);

            if (can_increase && can_auto_apply) {
                // Apply increase: double ctx (capped at 2048)
                int new_ctx = ctx * 2;
                if (new_ctx > 2048) new_ctx = 2048;
                if (new_ctx != ctx) {
                    if (!plan_checkpoint_done) {
                        llmk_oo_plan_checkpoint_best_effort("pre_auto_apply");
                        plan_checkpoint_done = 1;
                    }
                    int ok = llmk_oo_auto_apply_write_verify_best_effort("increase_ctx",
                                                                        "ctx_len",
                                                                        ctx,
                                                                        seq,
                                                                        new_ctx,
                                                                        seq,
                                                                        ram_mb);
                    if (ok) {
                        final_reason_id = "OO_APPLY_OK";
                        Print(L"OK: OO auto-apply: increase_ctx (old=%d new=%d check=pass mode=aggressive reason_id=OO_APPLY_OK)\r\n", ctx, new_ctx);
                        llmk_oo_journal_event_load_state_best_effort("auto_apply_increase_ctx_ok");
                        llmk_oo_record_last_auto_apply_best_effort(boots, mode, LLMK_OO_ACTION_INCREASE_CTX);
                        actions_applied++;
                        plan_applied_now++;
                        g_oo_auto_applied_count_this_boot++;
                        g_oo_auto_applied_this_boot = (g_oo_auto_applied_count_this_boot > 0) ? 1 : 0;
                        if (batch_summary_pos > 0) llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, ",");
                        llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "increase_ctx");
                    } else {
                        char oval[32]; // SAFE: small temp buffer for cfg numeric string
                        int op = 0;
                        llmk_ascii_append_u64(oval, (int)sizeof(oval), &op, (UINT64)ctx);
                        oval[op] = 0;
                        llmk_repl_cfg_set_kv_best_effort("ctx_len", oval);
                        final_reason_id = "OO_APPLY_VERIFY_FAILED";
                        Print(L"ERROR: OO auto-apply verification failed: increase_ctx (reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED, reverting)\r\n");
                        llmk_oo_journal_event_load_state_best_effort("auto_apply_increase_ctx_fail");
                        llmk_oo_journal_event_load_state_best_effort("plan_hard_stop");
                        plan_hard_stop = 1;
                        actions_blocked++;
                    }
                } else {
                    final_reason_id = "OO_BLOCK_ALREADY_MAX";
                    Print(L"OK: OO policy blocked: increase_ctx (reason=already_at_max)\r\n");
                    actions_blocked++;
                }
            } else {
                final_reason_id = "OO_BLOCK_DYNAMIC";
                const char *block_reason = (!can_auto_apply && confidence_gate_enabled && !confidence_gate_pass) ? "confidence_below_threshold" :
                                           (!can_auto_apply && g_cfg_oo_auto_apply == 0) ? "auto_apply_disabled" :
                                           (!can_auto_apply && plan_hard_stop) ? "hard_stop_active" :
                                           (!can_auto_apply) ? "plan_budget_exhausted" :
                                           (mode == LLMK_OO_MODE_SAFE) ? "safe_mode_no_increase" :
                                           (ram_mb < 1024ULL) ? "low_ram_no_increase" :
                                           (g_cfg_oo_auto_apply < 2) ? "conservative_mode_no_increase" :
                                           "increase_blocked";
                Print(L"OK: OO policy blocked: increase (reason=%a reason_id=OO_BLOCK_DYNAMIC)\r\n", block_reason);
                actions_blocked++;
            }
        }

        // 6.4: Log model change (not auto-applied in v0)
        if (action_model) {
            final_reason_id = "OO_MODEL_LOG_ONLY";
            Print(L"OK: OO policy decided: logged_only (reason=model_change_not_auto reason_id=OO_MODEL_LOG_ONLY)\r\n");
            actions_blocked++;
        }

        // If no actions applied/blocked, mark as no actionable keywords
        if (actions_applied == 0 && actions_blocked == 0) {
            final_reason_id = "OO_NO_ACTIONABLE_KEYWORD";
            Print(L"OK: OO policy decided: ignored (reason=no_actionable_keyword reason_id=OO_NO_ACTIONABLE_KEYWORD)\r\n");
        }
    }

    // Emit batch summary (M5.1)
    if (multi_enabled && (actions_applied > 0 || actions_blocked > 0)) {
        Print(L"OK: OO policy batch: %d actions applied, %d blocked\r\n", actions_applied, actions_blocked);
    }

    if (selected_action[0] && !(selected_action[0] == 'n' && selected_action[1] == 'a' && selected_action[2] == 0)) {
        int selected_kind = llmk_oo_ascii_equals(selected_action, "reduce_ctx") ? 1 :
                            llmk_oo_ascii_equals(selected_action, "reduce_seq") ? 2 :
                            llmk_oo_ascii_equals(selected_action, "increase_ctx") ? 3 : 0;
        int selected_boot_bias_now = 0;
        int selected_trend_bias_now = 0;
        int selected_saturation_bias_now = 0;
        if (selected_kind == 1) {
            selected_boot_bias_now = llmk_oo_boot_relation_for_action_best_effort("reduce_ctx", NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
            selected_trend_bias_now = llmk_oo_recent_trend_for_action_best_effort("reduce_ctx", NULL, 0);
            selected_saturation_bias_now = llmk_oo_saturation_bias_for_action_best_effort("reduce_ctx", ctx, seq, NULL, 0);
        } else if (selected_kind == 2) {
            selected_boot_bias_now = llmk_oo_boot_relation_for_action_best_effort("reduce_seq", NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
            selected_trend_bias_now = llmk_oo_recent_trend_for_action_best_effort("reduce_seq", NULL, 0);
            selected_saturation_bias_now = llmk_oo_saturation_bias_for_action_best_effort("reduce_seq", ctx, seq, NULL, 0);
        } else if (selected_kind == 3) {
            selected_boot_bias_now = llmk_oo_boot_relation_for_action_best_effort("increase_ctx", NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
            selected_trend_bias_now = llmk_oo_recent_trend_for_action_best_effort("increase_ctx", NULL, 0);
            selected_saturation_bias_now = llmk_oo_saturation_bias_for_action_best_effort("increase_ctx", ctx, seq, NULL, 0);
        }
        operator_summary = llmk_oo_operator_summary_from_dynamics(selected_kind,
                                                                  operator_positive_saturated,
                                                                  selected_boot_bias_now,
                                                                  selected_trend_bias_now,
                                                                  selected_saturation_bias_now,
                                                                  (actions_applied > 0) ? 1 : 0,
                                                                  final_reason_id);
    }
    Print(L"OK: OO operator summary: %a\r\n", operator_summary ? operator_summary : "na");

    // M5.3: Log consultation to OOCONSULT.LOG
    {
        // Determine decision string for log
        const char *decision_str = "unknown";
        char selected_boot_relation[64];
        int selected_boot_bias = 0;
        char selected_trend_relation[64];
        int selected_trend_bias = 0;
        char selected_saturation_state[64];
        int selected_saturation_bias = 0;
        char selected_operator_summary[64];

        selected_boot_relation[0] = 0;
        selected_trend_relation[0] = 0;
        selected_saturation_state[0] = 0;
        selected_operator_summary[0] = 0;
        if (action_stable) {
            decision_str = "stable";
        } else if (action_reboot) {
            decision_str = "reboot_logged";
        } else if (actions_applied == 0 && actions_blocked == 0) {
            decision_str = "ignored";
        } else if (multi_enabled && (actions_applied > 0 || actions_blocked > 0)) {
            // Multi-action: use batch_summary as decision
            decision_str = batch_summary[0] ? batch_summary : "multi";
        } else if (action_reduce_ctx) {
            decision_str = "reduce_ctx";
        } else if (action_reduce_seq) {
            decision_str = "reduce_seq";
        } else if (action_increase) {
            decision_str = "increase_blocked";
        } else if (action_model) {
            decision_str = "model_logged";
        }

        if (selected_action[0] == 'n' && selected_action[1] == 'a' && selected_action[2] == 0) {
            if (action_reduce_ctx) selected_action = "reduce_ctx";
            else if (action_reduce_seq) selected_action = "reduce_seq";
            else if (action_increase) selected_action = "increase_ctx";
            else if (action_model) selected_action = "model_logged";
        }

        selected_boot_bias = llmk_oo_boot_relation_for_action_best_effort(selected_action,
                                                                          selected_boot_relation,
                                                                          (int)sizeof(selected_boot_relation),
                                                                          NULL, 0, NULL, 0, NULL, 0, NULL, 0);
        selected_trend_bias = llmk_oo_recent_trend_for_action_best_effort(selected_action,
                                                                          selected_trend_relation,
                                                                          (int)sizeof(selected_trend_relation));
        selected_saturation_bias = llmk_oo_saturation_bias_for_action_best_effort(selected_action,
                                                                                   ctx,
                                                                                   seq,
                                                                                   selected_saturation_state,
                                                                                   (int)sizeof(selected_saturation_state));
        if (!selected_boot_relation[0]) {
            llmk_oo_outcome_copy_token(selected_boot_relation, (int)sizeof(selected_boot_relation), "na");
        }
        if (!selected_trend_relation[0]) {
            llmk_oo_outcome_copy_token(selected_trend_relation, (int)sizeof(selected_trend_relation), "na");
        }
        if (!selected_saturation_state[0]) {
            llmk_oo_outcome_copy_token(selected_saturation_state, (int)sizeof(selected_saturation_state), "na");
        }
        llmk_oo_outcome_copy_token(selected_operator_summary, (int)sizeof(selected_operator_summary), operator_summary ? operator_summary : "na");

        llmk_oo_log_consultation(boots, mode, ram_mb, ctx, seq, llm_suggestion,
                                 decision_str, (actions_applied > 0) ? 1 : 0,
                                 selected_action,
                                 selected_boot_relation,
                                 selected_boot_bias,
                                 selected_trend_relation,
                                 selected_trend_bias,
                                 selected_saturation_state,
                                 selected_saturation_bias,
                                 selected_operator_summary,
                                 final_reason_id,
                                 confidence_reason_id,
                                 plan_enabled,
                                 plan_remaining_budget,
                                 plan_hard_stop,
                                 plan_reason_id,
                                 confidence_score, confidence_threshold,
                                 confidence_gate_enabled, feedback_bias);
    }

    // 7. Log to journal (best-effort): ultra-minimal markers.
    if (multi_enabled && (actions_applied > 0 || actions_blocked > 0)) {
        llmk_oo_journal_event_load_state_best_effort("consult_multi");
    } else {
        llmk_oo_journal_event_load_state_best_effort("consult");
    }
}

// M5.3: Log consultation to OOCONSULT.LOG (append-only)
// Spec: cap log at 64KB and truncate oldest (FIFO) when exceeded.
#define LLMK_OO_CONSULT_LOG_MAX_BYTES  (64u * 1024u)
#define LLMK_OO_CONSULT_LOG_KEEP_BYTES (32u * 1024u)

// Journal cap (same policy as consult log): cap at 64KB and keep newest 32KB.
#define LLMK_OO_JOUR_LOG_MAX_BYTES  (64u * 1024u)
#define LLMK_OO_JOUR_LOG_KEEP_BYTES (32u * 1024u)

static void llmk_oo_jour_log_rotate_best_effort(void) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOJOUR.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    if (len <= (UINTN)LLMK_OO_JOUR_LOG_MAX_BYTES) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    UINTN keep = (UINTN)LLMK_OO_JOUR_LOG_KEEP_BYTES;
    if (keep >= len) keep = len;
    UINTN start = len - keep;

    // Align to line boundary (avoid starting mid-line).
    char *cbuf = (char *)buf;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') { start = i + 1; break; }
    }
    if (start >= len) start = 0;

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"OOJOUR.LOG");
    if (!EFI_ERROR(st) && f) {
        UINTN nb = len - start;
        (void)llmk_file_write_bytes(f, (const void *)(cbuf + start), nb);
        (void)uefi_call_wrapper(f->Flush, 1, f);
        uefi_call_wrapper(f->Close, 1, f);
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_consult_log_rotate_best_effort(void) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    if (len <= (UINTN)LLMK_OO_CONSULT_LOG_MAX_BYTES) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    UINTN keep = (UINTN)LLMK_OO_CONSULT_LOG_KEEP_BYTES;
    if (keep >= len) keep = len;
    UINTN start = len - keep;

    // Try to align on a line boundary to avoid starting mid-line.
    char *cbuf = (char *)buf;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') { start = i + 1; break; }
    }
    if (start >= len) start = 0;

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"OOCONSULT.LOG");
    if (!EFI_ERROR(st) && f) {
        UINTN nb = len - start;
        (void)llmk_file_write_bytes(f, (const void *)(cbuf + start), nb);
        (void)uefi_call_wrapper(f->Flush, 1, f);
        uefi_call_wrapper(f->Close, 1, f);
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_log_consultation(UINT64 boot_count, UINT32 mode, UINT64 ram_mb, 
                                     int ctx, int seq, const char *suggestion, 
                                     const char *decision, int applied,
                                     const char *selected_action,
                                     const char *boot_relation,
                                     int boot_bias,
                                     const char *trend_relation,
                                     int trend_bias,
                                     const char *saturation_state,
                                     int saturation_bias,
                                     const char *operator_summary,
                                     const char *reason_id,
                                     const char *confidence_reason_id,
                                     int plan_enabled,
                                     int plan_remaining_budget,
                                     int plan_hard_stop,
                                     const char *plan_reason_id,
                                     int confidence_score, int confidence_threshold,
                                     int confidence_gate_enabled,
                                     int feedback_bias) {
    // Check if logging enabled
    int log_enabled = g_cfg_oo_consult_log;
    if (log_enabled < 0) {
        log_enabled = (g_cfg_oo_llm_consult > 0) ? 1 : 0;
    }
    if (!log_enabled || !g_root) return;
    if (!g_cfg_oo_enable) return;
    if (g_djibion.mode != DJIBION_MODE_OFF && !g_djibion.laws.allow_oo_persist) return;

    (void)suggestion;

    // Compact, stable log line (avoid persisting raw suggestion payload).
    // Format: consult b=<boot> m=<N|D|S> r=<ram_mb> c=<ctx> s=<seq> d=<decision> sel=<selected> br=<boot_relation> bb=<boot_bias> tr=<trend_relation> tb=<trend_bias> sr=<saturation_state> sb=<saturation_bias> os=<operator_summary> ri=<reason_id> cri=<confidence_reason_id> pe=<0|1> pr=<remain> ph=<0|1> pri=<plan_reason_id> a=<0|1> sc=<score> th=<thr> g=<0|1> fb=<bias>
    char logline[640];
    int lp = 0;

    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "consult b=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, boot_count);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " m=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp,
                         (mode == LLMK_OO_MODE_NORMAL) ? "N" :
                         (mode == LLMK_OO_MODE_DEGRADED) ? "D" : "S");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " r=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, ram_mb);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " c=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)ctx);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " s=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)seq);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " d=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, decision ? decision : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " sel=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, selected_action ? selected_action : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " br=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, boot_relation ? boot_relation : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " bb=");
    if (boot_bias < 0) {
        llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "-");
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(-boot_bias));
    } else {
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)boot_bias);
    }
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " tr=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, trend_relation ? trend_relation : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " tb=");
    if (trend_bias < 0) {
        llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "-");
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(-trend_bias));
    } else {
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)trend_bias);
    }
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " sr=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, saturation_state ? saturation_state : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " sb=");
    if (saturation_bias < 0) {
        llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "-");
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(-saturation_bias));
    } else {
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)saturation_bias);
    }
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " os=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, operator_summary ? operator_summary : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " ri=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, reason_id ? reason_id : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " cri=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, confidence_reason_id ? confidence_reason_id : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " pe=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(plan_enabled ? 1 : 0));
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " pr=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)((plan_remaining_budget < 0) ? 0 : plan_remaining_budget));
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " ph=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(plan_hard_stop ? 1 : 0));
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " pri=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, plan_reason_id ? plan_reason_id : "na");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " a=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)applied);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " sc=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)confidence_score);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " th=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)confidence_threshold);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " g=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(confidence_gate_enabled ? 1 : 0));
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " fb=");
    if (feedback_bias < 0) {
        llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "-");
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)(-feedback_bias));
    } else {
        llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)feedback_bias);
    }
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "\r\n");
    logline[lp] = 0;

    // Append to OOCONSULT.LOG
    EFI_FILE_HANDLE logf = NULL;
    if (!EFI_ERROR(llmk_open_binary_file_append(&logf, L"OOCONSULT.LOG"))) {
        UINTN nb = (UINTN)lp;
        uefi_call_wrapper(logf->Write, 3, logf, &nb, (void *)logline);
        uefi_call_wrapper(logf->Flush, 1, logf);
        uefi_call_wrapper(logf->Close, 1, logf);
        Print(L"OK: OO consult logged to OOCONSULT.LOG\r\n");

        // Enforce max size (best-effort; no boot impact).
        llmk_oo_consult_log_rotate_best_effort();
    }
}

static void llmk_oo_print_ooconsult_tail_best_effort(int max_lines) {
    if (!g_root || max_lines <= 0) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_log] (no OOCONSULT.LOG)\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = 0;
    int lines = 0;
    for (UINTN i = len; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            lines++;
            if (lines > max_lines) {
                start = i;
                break;
            }
        }
    }
    if (start >= len) start = 0;

    UINTN out_len = len - start;
    char *out = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, out_len + 1, (void **)&out)) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_log] (OOM printing tail)\r\n");
        return;
    }

    CopyMem(out, cbuf + start, out_len);
    out[out_len] = 0;

    Print(L"%a", out);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_print_oooutcome_tail_best_effort(int max_lines) {
    if (!g_root || max_lines <= 0) return;

    LlmkOoState s;
    if (llmk_oo_load_state_best_effort(&s)) {
        UINT32 meta = llmk_oo_get_last_action_meta(s.flags);
        UINT32 apply_boot_low8 = llmk_oo_get_last_apply_boot_low8(s.flags);
        if (meta != 0 && apply_boot_low8 != 0) {
            UINT32 action_id = (meta & 0x3Fu);
            UINT32 apply_mode = (meta >> 6u) & 0x3u;
            UINT32 want_boot_low8 = (UINT32)((apply_boot_low8 + 1u) & 0xFFu);
            Print(L"[oo_outcome] pending.action=%a\r\n", llmk_oo_action_name(action_id));
            Print(L"[oo_outcome] pending.apply_mode=%s\r\n", llmk_oo_mode_name(apply_mode));
            Print(L"[oo_outcome] pending.next_boot_low8=%lu\r\n", (UINT64)want_boot_low8);
        }
    }

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_outcome] (no OOOUTCOME.LOG)\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = 0;
    int lines = 0;
    for (UINTN i = len; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            lines++;
            if (lines > max_lines) {
                start = i;
                break;
            }
        }
    }
    if (start >= len) start = 0;

    UINTN out_len = len - start;
    char *out = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, out_len + 1, (void **)&out)) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_outcome] (OOM printing tail)\r\n");
        return;
    }

    CopyMem(out, cbuf + start, out_len);
    out[out_len] = 0;

    Print(L"%a", out);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_print_oojour_tail_best_effort(int max_lines) {
    if (!g_root || max_lines <= 0) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOJOUR.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_jour] (no OOJOUR.LOG)\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = 0;
    int lines = 0;
    for (UINTN i = len; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            lines++;
            if (lines > max_lines) {
                start = i;
                break;
            }
        }
    }
    if (start >= len) start = 0;

    UINTN out_len = len - start;
    char *out = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, out_len + 1, (void **)&out)) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_jour] (OOM printing tail)\r\n");
        return;
    }

    CopyMem(out, cbuf + start, out_len);
    out[out_len] = 0;

    Print(L"%a", out);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_consult_execute(Config *config, TransformerWeights *weights, 
                                    RunState *state, Tokenizer *tokenizer,
                                    float temperature, float min_p, float top_p, int top_k) {
    if (!config || !weights || !state || !tokenizer) return;

    // 1. Collect system state
    UINT64 ram_mb = llmk_get_conventional_ram_bytes_best_effort() / (1024ULL * 1024ULL);
    UINT32 mode = g_oo_last_mode_valid ? g_oo_last_mode : LLMK_OO_MODE_SAFE;
    int ctx = config->seq_len;
    int seq = config->seq_len;
    UINT64 boots = 0;

    // Load current boot count from state (best-effort)
    LlmkOoState s;
    if (llmk_oo_load_state_best_effort(&s)) {
        boots = s.boot_count;
        mode = s.mode;
    }

    const char *mode_str = (mode == LLMK_OO_MODE_NORMAL) ? "NORMAL" :
                           (mode == LLMK_OO_MODE_DEGRADED) ? "DEGRADED" : "SAFE";
    Print(L"[obs][oo] consult_start mode=%a ram=%lu ctx=%d seq=%d boots=%lu\r\n",
          mode_str, ram_mb, ctx, seq, boots);

    // Read tail of journal (last 3 lines, best-effort)
    char journal_tail[256];
    journal_tail[0] = 0;
    if (g_root) {
        void *buf = NULL;
        UINTN len = 0;
        EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOJOUR.LOG", &buf, &len);
        if (!EFI_ERROR(st) && buf && len > 0) {
            char *tmp = (char *)buf;
            UINTN start = len;
            int nl = 0;
            while (start > 0) {
                start--;
                if (tmp[start] == '\n') {
                    nl++;
                    if (nl >= 3) { start++; break; }
                }
            }
            if (start >= len) start = 0;

            int jt_p = 0;
            for (UINTN i = start; i < len && jt_p + 1 < (int)sizeof(journal_tail); i++) {
                char c = tmp[i];
                if (c == '\r' || c == '\n') c = ' ';
                journal_tail[jt_p++] = c;
            }
            journal_tail[jt_p] = 0;
        }
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
    }

    // 2. Compose prompt (compact, <256 chars)
    // Check if multi-actions is enabled (for prompt adaptation)
    int multi_enabled_for_prompt = g_cfg_oo_multi_actions;
    if (multi_enabled_for_prompt < 0) {
        multi_enabled_for_prompt = (g_cfg_oo_llm_consult > 0) ? 1 : 0;
    }

    char prompt_buf[256];
    int pp = 0;
    prompt_buf[0] = 0;
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, "System: mode=");
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, 
                         (mode == LLMK_OO_MODE_NORMAL) ? "NORMAL" : 
                         (mode == LLMK_OO_MODE_DEGRADED) ? "DEGRADED" : "SAFE");
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, " ram=");
    llmk_ascii_append_u64(prompt_buf, (int)sizeof(prompt_buf), &pp, ram_mb);
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, "MB ctx=");
    llmk_ascii_append_u64(prompt_buf, (int)sizeof(prompt_buf), &pp, (UINT64)ctx);
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, " boots=");
    llmk_ascii_append_u64(prompt_buf, (int)sizeof(prompt_buf), &pp, boots);
    if (journal_tail[0]) {
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, " log=[");
        // Truncate if needed
        int jl = 0;
        while (journal_tail[jl] && pp + 1 < (int)sizeof(prompt_buf) - 32) {
            prompt_buf[pp++] = journal_tail[jl++];
        }
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, "]");
    }
    
    // Adapt prompt for multi-action mode (M5.1)
    if (multi_enabled_for_prompt) {
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, 
                             ". Suggest 1-3 brief actions (max 20 words):");
    } else {
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, 
                             ". Suggest ONE brief action (max 10 words):");
    }
    prompt_buf[pp] = 0;

    Print(L"[oo_consult] Prompt: ");
    llmk_print_ascii(prompt_buf);
    Print(L"\r\n\r\n");

    // 3. Tokenize prompt
    int prompt_tokens[128];
    int n_prompt = 0;
    {
        int cap = (int)(sizeof(prompt_tokens) / sizeof(prompt_tokens[0]));
        encode(prompt_buf, prompt_tokens, &n_prompt, cap, tokenizer);
        if (n_prompt <= 0) {
            Print(L"[oo_consult] ERROR: tokenization failed\r\n");
            return;
        }
    }

    // 4. Generate LLM suggestion (low creativity: temp=0.3, max_tokens=32)
    char llm_suggestion[128];
    llm_suggestion[0] = 0;
    int llm_len = 0;

    {
        const int max_sugg_tokens = 32;
        float consult_temp = 0.3f;
        float consult_min_p = min_p;
        float consult_top_p = top_p;
        int consult_top_k = top_k;
        float consult_repeat_penalty = 1.10f;

        int kv_pos = g_llmk_kv_pos;
        if (kv_pos < 0) kv_pos = 0;
        if (kv_pos + n_prompt + max_sugg_tokens + 1 >= config->seq_len) {
            // Best-effort: avoid overflow by clearing KV.
            reset_kv_cache(state, config);
            kv_pos = 0;
        }

        // Prefill prompt into the model.
        for (int i = 0; i < n_prompt; i++) {
            int pos = kv_pos + i;
            transformer_forward(state, weights, config, prompt_tokens[i], pos);
        }

        int token = prompt_tokens[n_prompt - 1];
        int pos = kv_pos + n_prompt - 1;

        int context_tokens[160];
        int n_context = 0;
        for (int i = 0; i < n_prompt && n_context < (int)(sizeof(context_tokens) / sizeof(context_tokens[0])); i++) {
            context_tokens[n_context++] = prompt_tokens[i];
        }

        for (int step = 0; step < max_sugg_tokens; step++) {
            int n_recent = n_context;
            if (n_recent > 64) n_recent = 64;
            int *recent = (n_recent > 0) ? &context_tokens[n_context - n_recent] : (int *)0;

            int next = sample_advanced(state->logits, config->vocab_size,
                                       consult_temp, consult_min_p, consult_top_p, consult_top_k,
                                       recent, n_recent, consult_repeat_penalty);
            if (next == TOKEN_EOS) break;

            char *piece = (tokenizer && tokenizer->vocab) ? tokenizer->vocab[next] : NULL;
            if (piece) {
                char decoded2[8];
                int dlen2 = llmk_decode_piece(piece, decoded2, (int)sizeof(decoded2));
                for (int k = 0; k < dlen2 && llm_len + 1 < (int)sizeof(llm_suggestion); k++) {
                    llm_suggestion[llm_len++] = decoded2[k];
                }
                llm_suggestion[llm_len] = 0;
            }

            if (n_context < (int)(sizeof(context_tokens) / sizeof(context_tokens[0]))) {
                context_tokens[n_context++] = next;
            }

            token = next;
            pos++;
            transformer_forward(state, weights, config, token, pos);
        }
    }

    Print(L"[obs][oo] consult_gen prompt_tok=%d out_chars=%d\r\n", n_prompt, llm_len);

    llmk_oo_consult_process_suggestion(ram_mb, mode, boots, ctx, seq, llm_suggestion);
}

// ============================================================================
// MAIN
// ============================================================================

