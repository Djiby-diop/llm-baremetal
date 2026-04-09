#ifndef LLM_SPLIT_EFI_MAIN
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    /* Enable OSXSAVE + YMM state (CR4 bit 18 + XCR0 bits 1:2) so that VZEROUPPER
     * instructions emitted by GCC -mavx2 do not trigger #UD in the UEFI environment.
     * OVMF does not enable CR4.OSXSAVE before handing control to the EFI app.
     * Check CPUID.1:ECX[26] (XSAVE support) before touching CR4 / XCR0. */
    {
        UINT32 ecx_cpuid = 0;
        __asm__ volatile (
            "mov $1, %%eax\n\t"
            "cpuid\n\t"
            "mov %%ecx, %0"
            : "=r"(ecx_cpuid) : : "eax", "ebx", "ecx", "edx"
        );
        if (ecx_cpuid & (1U << 26)) { /* XSAVE supported */
            UINT64 cr4_val;
            __asm__ volatile("mov %%cr4, %0" : "=r"(cr4_val));
            cr4_val |= (1ULL << 18); /* Set OSXSAVE */
            __asm__ volatile("mov %0, %%cr4" :: "r"(cr4_val));
            /* XGETBV / XSETBV: enable XMM (bit 1) and YMM (bit 2) in XCR0 */
            UINT32 xcr0_lo, xcr0_hi;
            __asm__ volatile("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0U));
            xcr0_lo |= 0x6U; /* SSE state (1) + YMM state (2) */
            __asm__ volatile("xsetbv" :: "a"(xcr0_lo), "d"(xcr0_hi), "c"(0U));
        }
    }

    InitializeLib(ImageHandle, SystemTable);

    // Djibion meta-engine defaults: off unless enabled by the user.
    djibion_init(&g_djibion);

    // Diopion complementary engine defaults: off unless enabled by the user.
    diopion_init(&g_diopion);

    // Diagnostion engine defaults: on (diagnostics are safe).
    diagnostion_init(&g_diagnostion);

    // Memorion engine defaults: on (read-only helpers + explicit manifest writes).
    memorion_init(&g_memorion);

    // Orchestrion engine defaults: off (workflow runner).
    orchestrion_init(&g_orchestrion);

    // Calibrion engine defaults: off (auto-tuning sampling).
    calibrion_init(&g_calibrion);

    // Compatibilion engine defaults: on (platform detection).
    compatibilion_init(&g_compatibilion);
    compatibilion_probe_cpu(&g_compatibilion);

    // OO Organism engines (stubs, off by default)
    evolvion_init(&g_evolvion);
    synaption_init(&g_synaption);
    conscience_init(&g_conscience);
    neuralfs_init(&g_neuralfs);
    ghost_init(&g_ghost);
    immunion_init(&g_immunion);
    dreamion_init(&g_dreamion);
    symbion_init(&g_symbion);
    collectivion_init(&g_collectivion);
    metabion_init(&g_metabion);
    metabion_set_mode(&g_metabion, (MetabionMode)METABION_DEFAULT_METABION_MODE);
    cellion_init(&g_cellion);
    morphion_init(&g_morphion);
    pheromion_init(&g_pheromion);
    compatibilion_set_platform(&g_compatibilion, COMPAT_PLAT_UEFI | COMPAT_PLAT_FAT32);

    // Initialize DjibMark tracing system
    djibmark_init();
    DJIBMARK_BOOT();

    // Disable the UEFI watchdog timer (large model loads can take minutes).
    // If not disabled, firmware may reset/reboot mid-load and it looks like a hang.
    uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);

    // 1. Clear Screen
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);

    // 2. Try to show image splash
    // Note: If successful, it covers the screen.
    ShowCyberpunkSplash(ImageHandle, SystemTable);

    // 3. Always show Text Banner (User requested boot interface to remain standard)
    // The splash function now handles its own display + pause + clear.
    Print(L"\r\n");
    Print(L"   ___   ___  \r\n");
    Print(L"  / _ \\ / _ \\ \r\n");
    Print(L" | | | | | | |\r\n");
    Print(L" | |_| | |_| |\r\n");
    Print(L"  \\___/ \\___/ \r\n\r\n");
    Print(L"  Operating Organism  --  v0.1  --  Bare-Metal Intelligence\r\n");
    Print(L"--------------------------------------------------------------------------\r\n");
    Print(L"Tips: /help | /logo | /oo_status | /oo_list | /oo_jour\r\n\r\n");
    
    if (!g_boot_verbose) {
        Print(L"Booting... (set boot_verbose=1 in repl.cfg for details; 2 for debug)\r\n\r\n");
    }

    llmk_boot_mark(L"banner");
    
    // ========================================================================
    // [1/7] File System
    // ========================================================================
    llmk_overlay_stage(1, 7);
    if (g_boot_verbose) {
        Print(L"[1/7] Opening file system...\r\n");
    }
    
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &LoadedImageProtocol, &LoadedImage);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: LoadedImage protocol failed\r\n");
        return status;
    }
    
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &FileSystemProtocol, &FileSystem);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: FileSystem protocol failed\r\n");
        return status;
    }
    
    EFI_FILE_HANDLE Root;
    status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: OpenVolume failed\r\n");
        return status;
    }

    // Persist root handle for best-effort dumps.
    g_root = Root;

    // Best-effort: read boot verbosity from repl.cfg now that the FS is ready.
    llmk_load_repl_cfg_boot_best_effort();

    // Best-effort: allow repl.cfg to configure OO engines modes (stubs).
    llmk_load_repl_cfg_oo_engines_best_effort();

    if (g_boot_logo) {
        llmk_print_logo();
    }

    if (g_boot_verbose) {
        Print(L"OK: File system ready\r\n\r\n");
    }

    llmk_boot_mark(L"fs_ready");

    // OO M1: best-effort persistent boot tick (writes OOSTATE.BIN + appends OOJOUR.LOG)
    // Opt-in via repl.cfg: oo_enable=1
    llmk_oo_boot_tick_best_effort();

    // OO M4: best-effort network read-only tick (placeholder)
    // Opt-in via repl.cfg: oo_net=1 (and oo_enable=1)
    llmk_oo_net_tick_best_effort();

    // Best-effort enable AVX/AVX2 state before feature detection.
#if !DJIBLAS_DISABLE_CPUID
    enable_avx_best_effort();
#endif

    // CPU feature detection (djiblas)
    {
        CPUFeatures cpu_features;
        djiblas_detect_cpu(&cpu_features);
        sgemm_kernel_t k = djiblas_get_best_kernel(&cpu_features);
        const CHAR16 *name = L"SCALAR";
        if (k == djiblas_sgemm_avx512) name = L"AVX512";
        else if (k == djiblas_sgemm_avx2) name = (cpu_features.has_fma ? L"AVX2+FMA" : L"AVX2");
        else if (k == djiblas_sgemm_sse2) name = L"SSE2";

        // Attention SIMD dispatch: only use AVX2 if firmware/OS state supports it.
        g_attn_use_avx2 = (cpu_features.has_avx2 && cpu_features.has_avx);

        if (g_boot_verbose) {
            Print(L"[DJIBLAS] SGEMM kernel: %s (sse2=%d avx=%d avx2=%d fma=%d)\r\n\r\n",
                  name,
                  (int)cpu_features.has_sse2,
                  (int)cpu_features.has_avx,
                  (int)cpu_features.has_avx2,
                  (int)cpu_features.has_fma);
            Print(L"[ATTN] SIMD path: %s\r\n\r\n", g_attn_use_avx2 ? L"AVX2" : L"SSE2");
        }
    }

    llmk_boot_mark(L"cpu_detect");

    // Best-effort graphics init (GOP). Optional: REPL still works without it.
    {
        EFI_STATUS gst = llmk_gop_init_best_effort();
        if (!EFI_ERROR(gst)) {
            if (g_boot_verbose) {
                Print(L"[GOP] Framebuffer ready: %dx%d (ppsl=%d)\r\n\r\n", (int)g_gop_w, (int)g_gop_h, (int)g_gop_ppsl);
            }

            // Feed platform info into Compatibilion.
            compatibilion_set_gop(&g_compatibilion, (uint32_t)g_gop_w, (uint32_t)g_gop_h);
        } else {
            if (g_boot_verbose) {
                Print(L"[GOP] Not available (%r)\r\n\r\n", gst);
            }
        }
    }

    llmk_boot_mark(L"gop_init");

    // Show diagnostic info if requested via repl.cfg: boot_diag=1
    if (g_boot_diag) {
        llmk_print_diag();
    }

    // LLM-OO runtime: init early, then optionally hook to GOP for heartbeat.
    llmk_oo_init();
    llmk_oo_set_on_step(llmk_oo_on_step_gop);
    
    // ========================================================================
    // [2/7] Load Model Header
    // ========================================================================

    llmk_overlay_stage(2, 7);
    
    if (g_boot_verbose) {
        Print(L"[2/7] Loading model...\r\n");
    }

    unsigned long long startup_model_t0_us = 0;
    unsigned long long startup_model_select_done_us = 0;
    unsigned long long startup_model_prep_done_us = 0;
    (void)uefi_wall_us(&startup_model_t0_us);
    
    EFI_FILE_HANDLE ModelFile;
    CHAR16 *model_filename = NULL;
    {
        int cfg_model_override_requested = 0;
        int cfg_model_override_failed = 0;
        CHAR16 cfg_model_requested[128];
        cfg_model_requested[0] = 0;

        // Optional: allow repl.cfg to override which model file to open.
        // Example in repl.cfg:
        //   model=models\\my-instruct.bin
        //   model=models\\my-instruct.gguf
        //   model=models\\my-instruct      (no extension: tries .bin then .gguf)
        //   model=stories110M.bin
        CHAR16 cfg_model[128];
        cfg_model[0] = 0;
        if (llmk_read_cfg_model_best_effort(Root, cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])))) {
            cfg_model_override_requested = 1;
            llmk_char16_copy_cap(cfg_model_requested, (int)(sizeof(cfg_model_requested) / sizeof(cfg_model_requested[0])), cfg_model);
            EFI_FILE_HANDLE f = 0;
            EFI_STATUS st = EFI_NOT_FOUND;

            // If no extension is provided, try .bin first (for inference), then .gguf.
            if (!llmk_char16_has_dot_ext(cfg_model)) {
                CHAR16 picked[192];
                picked[0] = 0;
                if (llmk_try_open_with_ext(Root, cfg_model, L".bin", &f, picked, (int)(sizeof(picked) / sizeof(picked[0])))) {
                    llmk_char16_copy_cap(cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                    st = EFI_SUCCESS;
                } else if (llmk_try_open_with_ext(Root, cfg_model, L".gguf", &f, picked, (int)(sizeof(picked) / sizeof(picked[0])))) {
                    llmk_char16_copy_cap(cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                    st = EFI_SUCCESS;
                }
            } else {
                CHAR16 picked[192];
                picked[0] = 0;
                st = llmk_open_read_with_fat83_fallback(Root, cfg_model, &f, picked, (int)(sizeof(picked) / sizeof(picked[0])), L"model_cfg");
                if (!EFI_ERROR(st) && picked[0]) {
                    llmk_char16_copy_cap(cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                }
            }

            if (!EFI_ERROR(st) && f) {
                // Keep the file as-selected. GGUF inference support (or fallback) is decided later,
                // after we inspect the GGUF tensor types.
                // Always store the selected path in stable global storage.
                // (Early heap allocations can be overwritten later during weight mapping.)
                llmk_model_set_loaded_path(cfg_model);
                model_filename = g_loaded_model_path16;
                ModelFile = f;
                status = st;
            } else {
                Print(L"[cfg] WARNING: model override open failed: %s (%r)\r\n", cfg_model, st);
                Print(L"[cfg] hint: run /models to inspect available files, or set model=<name>.bin|.gguf\r\n");
                Print(L"[cfg] fallback: continuing with auto-detect candidates\r\n");
                cfg_model_override_failed = 1;
            }
        }

        if (model_filename != NULL) {
            // Using cfg override.
            goto model_selected;
        }

        // If the model picker is enabled and there are multiple models available,
        // prompt the user before auto-picking from the legacy candidate list.
        // This prevents e.g. stories110M.bin from bypassing the picker.
        if (g_cfg_model_picker != 0) {
            LlmkModelEntry entries2[2]; // SAFE: model picker only needs top-2 candidates
            int n_models = llmk_collect_models(entries2, (int)(sizeof(entries2) / sizeof(entries2[0])));
            if (n_models >= 2) {
                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[192];
                picked[0] = 0;

                int picked_ok = llmk_model_picker(&f, picked, (int)(sizeof(picked) / sizeof(picked[0])));
                if (picked_ok && f) {
                    ModelFile = f;
                    llmk_model_set_loaded_path(picked);
                    model_filename = g_loaded_model_path16;
                    status = EFI_SUCCESS;
                    goto model_selected;
                }

                // Picker was shown and the user canceled (or selection failed).
                // Keep the app alive so /models + /model_info are usable.
                InterfaceFx_End();
                llmk_repl_no_model_loop();
                return EFI_NOT_FOUND;
            }
        }

        // Try larger models first when present. Keep the list small and explicit
        // (UEFI shell users can rename the file to match one of these).
        CHAR16 *candidates[] = {
            L"stories300M.bin",
            L"stories260M.bin",
            L"stories200M.bin",
            L"stories110M.bin",
            L"stories15M.bin",
            L"model.bin",
        };
        const int n_candidates = (int)(sizeof(candidates) / sizeof(candidates[0]));
        EFI_STATUS last = EFI_NOT_FOUND;
        for (int i = 0; i < n_candidates; i++) {
            EFI_FILE_HANDLE f = 0;
            CHAR16 picked0[192];
            picked0[0] = 0;
            EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, candidates[i], &f, picked0,
                                                              (int)(sizeof(picked0) / sizeof(picked0[0])),
                                                              L"model_candidate");
            if (!EFI_ERROR(st) && f) {
                ModelFile = f;
                llmk_model_set_loaded_path(picked0[0] ? picked0 : candidates[i]);
                model_filename = g_loaded_model_path16;
                status = st;
                break;
            }
            // Also allow placing models under a /models directory.
            {
                CHAR16 path[96];
                StrCpy(path, L"models\\");
                StrCat(path, candidates[i]);
                CHAR16 picked1[192];
                picked1[0] = 0;
                st = llmk_open_read_with_fat83_fallback(Root, path, &f, picked1,
                                                       (int)(sizeof(picked1) / sizeof(picked1[0])),
                                                       L"model_candidate_models");
                if (!EFI_ERROR(st) && f) {
                    ModelFile = f;
                    llmk_model_set_loaded_path(picked1[0] ? picked1 : path);
                    model_filename = g_loaded_model_path16;
                    status = st;
                    break;
                }
            }
            last = st;
        }
        if (model_filename == NULL) {
            // Last-chance: model picker (menu), otherwise first match in root/models.
            EFI_FILE_HANDLE f = NULL;
            CHAR16 picked[192];
            picked[0] = 0;

            int picked_ok = 0;
            int picker_used = (g_cfg_model_picker != 0);
            if (picker_used) {
                picked_ok = llmk_model_picker(&f, picked, (int)(sizeof(picked) / sizeof(picked[0])));
            }
            if (!picked_ok && !picker_used) {
                picked[0] = 0;
                if (llmk_try_open_first_model_best_effort(&f, picked, (int)(sizeof(picked) / sizeof(picked[0])))) {
                    picked_ok = 1;
                }
            }

            if (picked_ok && f) {
                ModelFile = f;
                llmk_model_set_loaded_path(picked);
                model_filename = g_loaded_model_path16;
                status = EFI_SUCCESS;
            } else {
                Print(L"ERROR: Model file not found.\r\n");
                Print(L"Expected one of (root or models\\): stories300M.bin stories260M.bin stories200M.bin stories110M.bin stories15M.bin model.bin\r\n");
                Print(L"Last open status: %r\r\n", last);
                Print(L"Or set repl.cfg: model=<path> (supports .bin/.gguf)\r\n");
                Print(L"Tip: in no-model REPL use /models and /model_info <path>\r\n");
                // Do not exit: keep the app alive so /models + /model_info are usable.
                InterfaceFx_End();
                llmk_repl_no_model_loop();
                return last;
            }
        }
model_selected:
        ;

    (void)uefi_wall_us(&startup_model_select_done_us);

        if (g_cfg_oo_enable && cfg_model_override_requested && cfg_model_override_failed && model_filename != NULL) {
            Print(L"OK: OO model fallback: %s -> %s\r\n", cfg_model_requested, model_filename);
        }
    }
    
    // Record the selected model path for /model_info.
    llmk_model_set_loaded_path(model_filename);
    if (g_boot_verbose >= 2) llmk_debug_print_loaded_model_path(L"after_select");

    // Detect format early.
    g_loaded_model_format = llmk_detect_model_format(ModelFile);

    // GGUF inference support: F16/F32 and common quant types are supported by the loader.
    LlmkGgufPlan *gguf_plan = NULL;
    int use_gguf_inference = 0;
    int gguf_has_output_weight = 0;

    Config config;
    // Default-init in case of early exits.
    config.dim = 0;
    config.hidden_dim = 0;
    config.n_layers = 0;
    config.n_heads = 0;
    config.n_kv_heads = 0;
    config.vocab_size = 0;
    config.seq_len = 0;

    // In llama2.c format, a negative vocab_size indicates shared classifier weights.
    int shared_classifier = 0;

    if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF) {
        // Parse GGUF plan directly on startup path (avoid extra summary parse here).

        // Try to build a GGUF inference plan. If this fails (e.g., quantized GGUF), fall back to .bin.
        {
            int dim = 0, hidden = 0, layers = 0, heads = 0, kv = 0, vocab = 0, seq = 0;
            EFI_STATUS pst = llmk_gguf_build_plan(ModelFile, &gguf_plan, &dim, &hidden, &layers, &heads, &kv, &vocab, &seq, &gguf_has_output_weight);
            if (!EFI_ERROR(pst) && gguf_plan) {
                config.dim = dim;
                config.hidden_dim = hidden;
                config.n_layers = layers;
                config.n_heads = heads;
                config.n_kv_heads = kv;
                config.vocab_size = vocab;
                config.seq_len = seq;

                shared_classifier = gguf_has_output_weight ? 0 : 1;
                use_gguf_inference = 1;
                if (g_boot_verbose) {
                    Print(L"GGUF detected: ctx=%d dim=%d layers=%d heads=%d kv_heads=%d\r\n",
                          config.seq_len, config.dim, config.n_layers, config.n_heads, config.n_kv_heads);
                }
                Print(L"OK: GGUF inference enabled (F16/F32/Q4/Q5/Q8).\r\n\r\n");
            } else {
                Print(L"NOTE: GGUF inference unsupported (%r); searching for a .bin fallback...\r\n", pst);
            }
        }

        if (!use_gguf_inference) {
            uefi_call_wrapper(ModelFile->Close, 1, ModelFile);

            // Preferred fallback: sibling .bin next to the selected .gguf (same basename).
            if (model_filename && llmk_char16_endswith_ci(model_filename, L".gguf")) {
                CHAR16 alt[192];
                llmk_char16_copy_cap(alt, (int)(sizeof(alt) / sizeof(alt[0])), model_filename);
                // Find last '.' and overwrite.
                for (int k = (int)StrLen(alt) - 1; k >= 0; k--) {
                    if (alt[k] == L'.') {
                        alt[k] = 0;
                        break;
                    }
                    if (alt[k] == L'\\' || alt[k] == L'/') break;
                }
                if (StrLen(alt) + 4 < (sizeof(alt) / sizeof(alt[0]))) {
                    StrCat(alt, L".bin");
                    EFI_FILE_HANDLE fb = NULL;
                    CHAR16 picked[192];
                    picked[0] = 0;
                    EFI_STATUS fst = llmk_open_read_with_fat83_fallback(Root, alt, &fb, picked,
                                                                       (int)(sizeof(picked) / sizeof(picked[0])),
                                                                       L"gguf_sibling_bin");
                    if (!EFI_ERROR(fst) && fb) {
                        ModelFile = fb;
                        const CHAR16 *chosen = picked[0] ? picked : alt;
                        UINTN n = StrLen(chosen) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc((unsigned long)(n * sizeof(CHAR16)));
                        model_filename = stable ? stable : model_filename;
                        if (stable) StrCpy(stable, chosen);
                        llmk_model_set_loaded_path(model_filename);
                        g_loaded_model_format = LLMK_MODEL_FMT_BIN;
                        Print(L"OK: using sibling .bin fallback: %s\r\n\r\n", model_filename);
                        goto gguf_fallback_done;
                    }
                }
            }

            // Minimal fallback search (root and models\\) to avoid bricking boot.
            CHAR16 *fallbacks[] = {
                L"stories300M.bin",
                L"stories260M.bin",
                L"stories200M.bin",
                L"stories110M.bin",
                L"stories15M.bin",
                L"model.bin",
            };
            const int n_fallbacks = (int)(sizeof(fallbacks) / sizeof(fallbacks[0]));
            EFI_FILE_HANDLE fb = NULL;
            CHAR16 *fb_name = NULL;
            for (int fi = 0; fi < n_fallbacks; fi++) {
                EFI_FILE_HANDLE t = NULL;
                CHAR16 picked0[192];
                picked0[0] = 0;
                EFI_STATUS fst = llmk_open_read_with_fat83_fallback(Root, fallbacks[fi], &t, picked0,
                                                                   (int)(sizeof(picked0) / sizeof(picked0[0])),
                                                                   L"gguf_fallback_root");
                if (!EFI_ERROR(fst) && t) {
                    fb = t;
                    if (picked0[0]) {
                        UINTN n = StrLen(picked0) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc((unsigned long)(n * sizeof(CHAR16)));
                        fb_name = stable ? stable : fallbacks[fi];
                        if (stable) StrCpy(stable, picked0);
                    } else {
                        fb_name = fallbacks[fi];
                    }
                    break;
                }
                {
                    CHAR16 pth[96];
                    StrCpy(pth, L"models\\");
                    StrCat(pth, fallbacks[fi]);
                    CHAR16 picked1[192];
                    picked1[0] = 0;
                    fst = llmk_open_read_with_fat83_fallback(Root, pth, &t, picked1,
                                                            (int)(sizeof(picked1) / sizeof(picked1[0])),
                                                            L"gguf_fallback_models");
                    if (!EFI_ERROR(fst) && t) {
                        fb = t;
                        const CHAR16 *chosen = picked1[0] ? picked1 : pth;
                        UINTN n = StrLen(chosen) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc((unsigned long)(n * sizeof(CHAR16)));
                        fb_name = stable ? stable : fallbacks[fi];
                        if (stable) StrCpy(stable, chosen);
                        break;
                    }
                }
            }

            if (!fb || !fb_name) {
                Print(L"ERROR: no .bin fallback found. Use /model_info to inspect GGUF, or provide a .bin export for inference.\r\n");
                return EFI_UNSUPPORTED;
            }

            ModelFile = fb;
            model_filename = fb_name;
            llmk_model_set_loaded_path(model_filename);
            g_loaded_model_format = LLMK_MODEL_FMT_BIN;
            Print(L"OK: using .bin fallback: %s\r\n\r\n", model_filename);
        }
gguf_fallback_done:
        ;
    }

    (void)uefi_wall_us(&startup_model_prep_done_us);
    if (startup_model_t0_us && startup_model_prep_done_us && startup_model_prep_done_us >= startup_model_t0_us) {
        unsigned long long select_ms = (startup_model_select_done_us >= startup_model_t0_us)
                                       ? ((startup_model_select_done_us - startup_model_t0_us) / 1000ULL)
                                       : 0ULL;
        unsigned long long prep_ms = (startup_model_prep_done_us >= startup_model_select_done_us)
                                     ? ((startup_model_prep_done_us - startup_model_select_done_us) / 1000ULL)
                                     : 0ULL;
        const char *fmt_s = (g_loaded_model_format == LLMK_MODEL_FMT_GGUF) ? "gguf" :
                            (g_loaded_model_format == LLMK_MODEL_FMT_BIN) ? "bin" : "unknown";
        Print(L"[obs][startup] model_select_ms=%lu model_prepare_ms=%lu format=%a\r\n",
              (UINT64)select_ms, (UINT64)prep_ms, fmt_s);
    }

    UINTN bytes_to_read = 0;
    if (!use_gguf_inference) {
        bytes_to_read = 7 * sizeof(int);
        uefi_call_wrapper(ModelFile->Read, 3, ModelFile, &bytes_to_read, &config);

        shared_classifier = (config.vocab_size < 0);
        if (config.vocab_size < 0) config.vocab_size = -config.vocab_size;
    }

    // Some exported model files may *still* share classifier weights even if vocab_size is positive.
    // Detect this by comparing expected weights size vs actual file size.
    UINT64 model_file_size = 0;
    if (!use_gguf_inference) {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS st = uefi_call_wrapper(ModelFile->GetInfo, 4, ModelFile, &FileInfoGuid, &info_size, NULL);
        if (st == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(st) && info) {
                st = uefi_call_wrapper(ModelFile->GetInfo, 4, ModelFile, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(st)) {
                    model_file_size = info->FileSize;
                }
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }

    // For GGUF, optionally load weights into a compact Q8_0 blob when possible.
    // This keeps matrices quantized in RAM and dequantizes on-the-fly during matmuls.
    int use_q8_blob = 0;
    UINT64 q8_blob_bytes = 0;
    if (g_cfg_gguf_q8_blob && use_gguf_inference && gguf_plan) {
        if (llmk_gguf_plan_supports_q8_0_blob(gguf_plan, shared_classifier)) {
            EFI_STATUS bst = llmk_gguf_calc_llama2_q8_0_blob_bytes(
                gguf_plan,
                config.dim,
                config.hidden_dim,
                config.n_layers,
                config.n_heads,
                config.n_kv_heads,
                config.vocab_size,
                config.seq_len,
                shared_classifier,
                &q8_blob_bytes
            );
            if (!EFI_ERROR(bst) && q8_blob_bytes > 0) {
                use_q8_blob = 1;
                if (g_boot_verbose) {
                    Print(L"[gguf] Q8_0 blob enabled: %lu MB\r\n", (UINT64)(q8_blob_bytes / (1024ULL * 1024ULL)));
                }
            } else {
                Print(L"NOTE: GGUF Q8_0 blob sizing failed (%r); using float32 load.\r\n", bst);
            }
        }
    } else if (!g_cfg_gguf_q8_blob && use_gguf_inference && gguf_plan) {
        if (g_boot_verbose) {
            Print(L"[gguf] Q8_0 blob disabled by repl.cfg; using float32 load.\r\n");
        }
    }
    
    if (g_boot_verbose) {
        if (g_boot_verbose >= 2) llmk_debug_print_loaded_model_path(L"before_model_loaded_print");
        char model8[192];
        llmk_char16_to_ascii_cap(model8, (int)sizeof(model8), g_loaded_model_path16);
        Print(L"OK: Model loaded: ");
        llmk_print_ascii(model8[0] ? model8 : "(unknown)");
        Print(L" (dim=%d, layers=%d, heads=%d, kv=%d, vocab=%d, seq=%d)\r\n\r\n",
              config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
    }

    // Always print minimal boot markers for CI/smoke tests (serial-friendly).
    {
        char model8[192];
        llmk_char16_to_ascii_cap(model8, (int)sizeof(model8), g_loaded_model_path16);
        Print(L"OK: Djibion boot\r\n");
        Print(L"OK: Model loaded: ");
        llmk_print_ascii(model8[0] ? model8 : "(unknown)");
        Print(L"\r\n");
        Print(L"OK: Version: %s\r\n\r\n", LLMB_BUILD_ID);
    }

    llmk_boot_mark(L"model_header_loaded");

    // ========================================================================
    // [3/7] Kernel zones + heap (auto-sized)
    // ========================================================================

    llmk_overlay_stage(3, 7);

    {
        int min_ctx = 64;
        int before_model = config.seq_len;
        int effective = config.seq_len;

        // Apply user-requested context length (can only reduce vs model).
        if (g_cfg_ctx_len > 0) {
            int target = g_cfg_ctx_len;
            if (target < 0) target = -target;
            if (target < min_ctx) target = min_ctx;
            if (target < effective) {
                if (g_boot_verbose) {
                    Print(L"[cfg] ctx_len=%d -> effective seq_len=%d (model=%d)\r\n",
                          g_cfg_ctx_len, target, before_model);
                }
                effective = target;
            }
        }

        // OO M3 (homeostasis): clamp effective context length in SAFE/DEGRADED.
        // Keep this deterministic and serial-visible when it triggers.
        if (g_cfg_oo_enable && g_oo_last_mode_valid) {
            int cap = 0;
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE) cap = 256;
            else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED) cap = 512;
            if (cap > 0 && effective > cap) {
                int from = effective;
                effective = cap;
                Print(L"OK: OO ctx_len clamp: %d -> %d (mode=%s)\r\n",
                      from, effective, llmk_oo_mode_name(g_oo_last_mode));
            }
        }

        if (effective < min_ctx) effective = min_ctx;
        if (effective < config.seq_len) {
            config.seq_len = effective;
        }
    }

    int kv_dim = (config.dim * config.n_kv_heads) / config.n_heads;
    int head_size = config.dim / config.n_heads;

    // OO M3 (homeostasis): RAM budget preflight.
    // Goal: avoid hard failures on low-memory guests by (1) allowing a smaller Zone-B minimum in SAFE/DEGRADED,
    // and (2) optionally reducing seq_len further if the estimated Zone-B total would exceed available RAM.
    if (g_cfg_oo_enable && g_oo_last_mode_valid && (g_oo_last_mode == LLMK_OO_MODE_SAFE || g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
        UINT64 sys_ram = llmk_get_conventional_ram_bytes_best_effort();
        if (sys_ram > 0) {
            const UINT64 reserve = 128ULL * 1024ULL * 1024ULL;
            UINT64 usable = (sys_ram > reserve) ? (sys_ram - reserve) : (sys_ram * 3ULL) / 4ULL;

            UINT64 min_total_policy = 0;
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE) min_total_policy = 512ULL * 1024ULL * 1024ULL;
            else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED) min_total_policy = 640ULL * 1024ULL * 1024ULL;

            if (g_cfg_oo_min_total_mb >= 0) {
                min_total_policy = (UINT64)g_cfg_oo_min_total_mb * 1024ULL * 1024ULL;
            }

            int seq_from = config.seq_len;
            int seq = config.seq_len;
            for (int iter = 0; iter < 8; iter++) {
                if (seq < 64) seq = 64;

                // Compute weights size (floats), with seq_len substituted for the freq_cis arrays.
                UINTN n_floats_base_pf = 0;
                n_floats_base_pf += (UINTN)config.vocab_size * (UINTN)config.dim;                   // token_embedding_table
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_att_weight
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wq
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wk
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wv
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wo
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_ffn_weight
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w1
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.hidden_dim * (UINTN)config.dim; // w2
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w3
                n_floats_base_pf += (UINTN)config.dim;                                              // rms_final_weight
                n_floats_base_pf += (UINTN)seq * (UINTN)head_size / 2;                               // freq_cis_real
                n_floats_base_pf += (UINTN)seq * (UINTN)head_size / 2;                               // freq_cis_imag

                UINTN n_floats_with_cls_pf = n_floats_base_pf + (UINTN)config.vocab_size * (UINTN)config.dim;

                int shared_pf = shared_classifier;
                if (!use_q8_blob && model_file_size > 0) {
                    UINT64 available = model_file_size;
                    UINT64 header_bytes = (UINT64)(7 * sizeof(int));
                    if (available > header_bytes) available -= header_bytes;
                    UINT64 bytes_base = (UINT64)n_floats_base_pf * sizeof(float);
                    UINT64 bytes_with = (UINT64)n_floats_with_cls_pf * sizeof(float);

                    if (available < bytes_with && available >= bytes_base) shared_pf = 1;
                    else if (available >= bytes_with) shared_pf = 0;
                }

                UINT64 weights_u64 = use_q8_blob ? (UINT64)q8_blob_bytes
                                                : (UINT64)(shared_pf ? n_floats_base_pf : n_floats_with_cls_pf) * (UINT64)sizeof(float);

                UINT64 kv_bytes = llmk_calc_kv_bytes_for_seq(&config, seq, kv_dim);
                UINT64 state_u64 = llmk_calc_state_bytes_for_seq(&config, seq, kv_dim);

                UINT64 tokenizer_u64 = (UINT64)config.vocab_size * ((UINT64)sizeof(char*) + (UINT64)sizeof(float));
                tokenizer_u64 += 4ULL * 1024ULL * 1024ULL;

                UINT64 slack_u64 = 16ULL * 1024ULL * 1024ULL;
                UINT64 scratch_u64 = 32ULL * 1024ULL * 1024ULL;
                UINT64 zonec_u64 = 8ULL * 1024ULL * 1024ULL;

                UINT64 acts_u64 = (state_u64 >= kv_bytes ? (state_u64 - kv_bytes) : 0ULL) + tokenizer_u64 + slack_u64;
                UINT64 total = weights_u64 + kv_bytes + scratch_u64 + acts_u64 + zonec_u64;

                UINT64 min_total = min_total_policy;
                if (min_total > 0 && total < min_total) total = min_total;

                if (total <= usable) break;

                int next = seq / 2;
                if (next < 64) {
                    seq = 64;
                    break;
                }
                seq = next;
            }

            if (seq != seq_from) {
                Print(L"OK: OO ram preflight: seq_len %d -> %d (mode=%s)\r\n", seq_from, seq, llmk_oo_mode_name(g_oo_last_mode));
                config.seq_len = seq;
            }
        }
    }

    // Compute total weights size (floats)
    UINTN n_floats_base = 0;
    n_floats_base += (UINTN)config.vocab_size * (UINTN)config.dim;                   // token_embedding_table
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_att_weight
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wq
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wk
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wv
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wo
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_ffn_weight
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w1
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.hidden_dim * (UINTN)config.dim; // w2
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w3
    n_floats_base += (UINTN)config.dim;                                              // rms_final_weight
    n_floats_base += (UINTN)config.seq_len * (UINTN)head_size / 2;                   // freq_cis_real
    n_floats_base += (UINTN)config.seq_len * (UINTN)head_size / 2;                   // freq_cis_imag

    UINTN n_floats_with_cls = n_floats_base + (UINTN)config.vocab_size * (UINTN)config.dim;

    // If file size is known, use it to infer whether wcls is present.
    if (model_file_size > 0) {
        UINT64 available = model_file_size;
        UINT64 header_bytes = (UINT64)(7 * sizeof(int));
        if (available > header_bytes) available -= header_bytes;
        UINT64 bytes_base = (UINT64)n_floats_base * sizeof(float);
        UINT64 bytes_with = (UINT64)n_floats_with_cls * sizeof(float);

        if (available < bytes_with && available >= bytes_base) {
            shared_classifier = 1;
        } else if (available >= bytes_with) {
            shared_classifier = 0;
        }
    }

    UINTN n_floats = shared_classifier ? n_floats_base : n_floats_with_cls;
    UINTN weights_bytes = use_q8_blob ? (UINTN)q8_blob_bytes : (n_floats * sizeof(float));
    UINTN state_bytes = 0;
    state_bytes += (UINTN)config.dim * sizeof(float) * 3; // x, xb, xb2
    state_bytes += (UINTN)config.hidden_dim * sizeof(float) * 2; // hb, hb2
    state_bytes += (UINTN)config.dim * sizeof(float); // q
    state_bytes += (UINTN)kv_dim * sizeof(float) * 2; // k, v
    state_bytes += (UINTN)config.n_heads * (UINTN)config.seq_len * sizeof(float); // att
    state_bytes += (UINTN)config.vocab_size * sizeof(float); // logits
    state_bytes += (UINTN)config.n_layers * (UINTN)config.seq_len * (UINTN)kv_dim * sizeof(float) * 2; // key/value cache

    // Tokenizer: pointers + scores + strings (strings size varies; reserve a safe budget)
    UINTN tokenizer_bytes = (UINTN)config.vocab_size * (sizeof(char*) + sizeof(float));
    tokenizer_bytes += 4 * 1024 * 1024; // string storage budget

    UINTN slack_bytes = 16 * 1024 * 1024;
    heap_size = weights_bytes + state_bytes + tokenizer_bytes + slack_bytes;
    if (heap_size < 100ULL * 1024ULL * 1024ULL) heap_size = 100ULL * 1024ULL * 1024ULL;

    // Initialize LLM-Kernel Zone B arenas sized from the same accounting.
    // This makes the REPL and the kernel work together: all big allocations go through zones/sentinel.
    {
        UINT64 zonec_bytes = 8ULL * 1024ULL * 1024ULL;
        UINT64 scratch_bytes = 32ULL * 1024ULL * 1024ULL;

        // KV cache lives in its own arena.
        UINT64 kv_bytes = (UINT64)config.n_layers * (UINT64)config.seq_len * (UINT64)kv_dim * sizeof(float) * 2ULL;

        UINT64 weights_u64 = (UINT64)weights_bytes;
        UINT64 acts_u64 = (UINT64)(state_bytes - (UINTN)kv_bytes) + (UINT64)tokenizer_bytes + (UINT64)slack_bytes;

        // Total Zone B includes all arenas.
        UINT64 total = weights_u64 + kv_bytes + scratch_bytes + acts_u64 + zonec_bytes;

        // Legacy min: 768MB (or 1GB for larger totals). In OO SAFE/DEGRADED, allow a smaller floor.
        UINT64 default_min_total = (total > 768ULL * 1024ULL * 1024ULL) ? (1024ULL * 1024ULL * 1024ULL) : (768ULL * 1024ULL * 1024ULL);
        UINT64 min_total = default_min_total;
        if (g_cfg_oo_enable && g_oo_last_mode_valid) {
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE) {
                min_total = 512ULL * 1024ULL * 1024ULL;
            } else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED) {
                min_total = 640ULL * 1024ULL * 1024ULL;
            }
        }
        if (g_cfg_oo_enable && g_oo_last_mode_valid && (g_oo_last_mode == LLMK_OO_MODE_SAFE || g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
            if (g_cfg_oo_min_total_mb >= 0) {
                min_total = (UINT64)g_cfg_oo_min_total_mb * 1024ULL * 1024ULL;
            }
        }
        if (g_cfg_oo_enable && g_oo_last_mode_valid && (g_oo_last_mode == LLMK_OO_MODE_SAFE || g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
            if (min_total != default_min_total) {
                Print(L"OK: OO zones min_total=%luMB (mode=%s)\r\n", (UINT64)(min_total / (1024ULL * 1024ULL)), llmk_oo_mode_name(g_oo_last_mode));
            }
        }
        if (total < min_total) total = min_total;

        LlmkZonesConfig zcfg;
        zcfg.total_bytes = total;
        zcfg.weights_bytes = weights_u64;
        zcfg.kv_bytes = kv_bytes;
        zcfg.scratch_bytes = scratch_bytes;
        zcfg.activations_bytes = acts_u64;
        zcfg.zone_c_bytes = zonec_bytes;

        if (g_boot_verbose) {
            Print(L"[3/7] Init kernel zones (%d MB)...\r\n", (int)(total / (1024 * 1024)));
        }
        status = llmk_zones_init(BS, &zcfg, &g_zones);
        if (EFI_ERROR(status) && min_total > 0 && total > min_total) {
            // If the computed size can't be allocated (e.g. low guest RAM / fragmentation),
            // fall back to a smaller default so the REPL can still boot with smaller models.
            if (g_boot_verbose) {
                Print(L"[llmk] zones alloc failed, retrying with %d MB...\r\n", (int)(min_total / (1024 * 1024)));
            }
            zcfg.total_bytes = min_total;
            zcfg.weights_bytes = 0;
            zcfg.kv_bytes = 0;
            zcfg.scratch_bytes = 0;
            zcfg.activations_bytes = 0;
            zcfg.zone_c_bytes = 0;
            status = llmk_zones_init(BS, &zcfg, &g_zones);
        }
        if (EFI_ERROR(status)) {
            Print(L"ERROR: llmk_zones_init failed: %r\r\n", status);
            return status;
        }

        // Init Zone C log (best-effort)
        EFI_STATUS logst = llmk_log_init(&g_zones, &g_llmk_log);
        if (EFI_ERROR(logst)) {
            g_llmk_log.entries = 0;
            g_llmk_log.capacity = 0;
            g_llmk_log.write_idx = 0;
        }

        // Init sentinel
        LlmkSentinelConfig scfg;
        scfg.enabled = TRUE;
        // REPL: keep allocation failures fatal, but keep budget overruns non-fatal.
        // This lets us "activate budgets" without killing the whole session.
        scfg.strict_mode = FALSE;
        scfg.strict_alloc = TRUE;
        scfg.strict_budget = FALSE;
        scfg.max_cycles = 0;
        scfg.max_cycles_prefill = 0;
        scfg.max_cycles_decode = 0;
        scfg.log_violations = TRUE;

        status = llmk_sentinel_init(&g_sentinel, &g_zones, (g_llmk_log.capacity ? &g_llmk_log : 0), &scfg);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: llmk_sentinel_init failed: %r\r\n", status);
            return status;
        }

        g_llmk_ready = 1;

        // Feed memory info (best-effort) into Compatibilion.
        compatibilion_set_memory(&g_compatibilion, (uint64_t)g_zones.zone_b_size);

        if (g_boot_verbose) {
            llmk_zones_print(&g_zones);
            llmk_sentinel_print_status(&g_sentinel);
            Print(L"OK: Kernel allocator ready\r\n\r\n");
        }
    }
    
    // ========================================================================
    // [4/7] Weight Pointers
    // ========================================================================

    llmk_overlay_stage(4, 7);
    
    if (g_boot_verbose) {
        Print(L"[4/7] Mapping weights...\r\n");
    }
    bytes_to_read = weights_bytes;
    void* weights_mem_raw = (void*)llmk_alloc_weights((UINT64)bytes_to_read, L"weights");
    if (weights_mem_raw == NULL) {
        Print(L"ERROR: OOM while allocating weights (%d MB needed).\r\n", (int)(bytes_to_read / (1024 * 1024)));
        Print(L"Hint: use a smaller model, or GGUF Q8_0 blob (gguf_q8_blob=1), or reduce ctx_len in repl.cfg.\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    TransformerWeights weights;
    // Default init.
    weights.kind = 0;
    weights.token_embedding_table = NULL;
    weights.rms_att_weight = NULL;
    weights.wq = NULL;
    weights.wk = NULL;
    weights.wv = NULL;
    weights.wo = NULL;
    weights.rms_ffn_weight = NULL;
    weights.w1 = NULL;
    weights.w2 = NULL;
    weights.w3 = NULL;
    weights.rms_final_weight = NULL;
    weights.wcls = NULL;
    weights.token_embedding_table_q8 = NULL;
    weights.wq_q8 = NULL;
    weights.wk_q8 = NULL;
    weights.wv_q8 = NULL;
    weights.wo_q8 = NULL;
    weights.w1_q8 = NULL;
    weights.w2_q8 = NULL;
    weights.w3_q8 = NULL;
    weights.wcls_q8 = NULL;
    weights.tok_embd_row_bytes = 0;
    weights.wq_layer_bytes = 0;
    weights.wk_layer_bytes = 0;
    weights.wv_layer_bytes = 0;
    weights.wo_layer_bytes = 0;
    weights.w1_layer_bytes = 0;
    weights.w2_layer_bytes = 0;
    weights.w3_layer_bytes = 0;

    if (use_gguf_inference) {
        if (use_q8_blob) {
            status = llmk_gguf_load_into_llama2_q8_0_blob(
                ModelFile,
                gguf_plan,
                weights_mem_raw,
                q8_blob_bytes,
                config.dim,
                config.hidden_dim,
                config.n_layers,
                config.n_heads,
                config.n_kv_heads,
                config.vocab_size,
                config.seq_len,
                shared_classifier
            );
            if (gguf_plan) {
                llmk_gguf_free_plan(gguf_plan);
                gguf_plan = NULL;
            }
            if (EFI_ERROR(status)) {
                Print(L"ERROR: Failed to load GGUF Q8_0 blob weights (%r).\r\n", status);
                return EFI_LOAD_ERROR;
            }

            // Map Q8_0 blob layout into pointer fields.
            {
                const UINT64 A = 16;
                UINT8 *base = (UINT8 *)weights_mem_raw;
                UINT64 off = 0;

                UINT64 dim_u = (UINT64)config.dim;
                UINT64 hid_u = (UINT64)config.hidden_dim;
                UINT64 lay_u = (UINT64)config.n_layers;
                UINT64 vocab_u = (UINT64)config.vocab_size;
                UINT64 kv_dim_u = (UINT64)kv_dim;
                UINT64 head_size_u = (UINT64)head_size;

                UINT64 tok_row = llmk_q8_0_row_bytes(config.dim);
                UINT64 wq_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 wk_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 wo_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 w1_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 w2_row  = llmk_q8_0_row_bytes(config.hidden_dim);
                UINT64 w3_row  = llmk_q8_0_row_bytes(config.dim);
                if (!tok_row || !wq_row || !wk_row || !wo_row || !w1_row || !w2_row || !w3_row) {
                    Print(L"ERROR: Q8_0 blob requires dims multiple of 32 (dim=%d hidden=%d).\r\n", config.dim, config.hidden_dim);
                    return EFI_UNSUPPORTED;
                }

                weights.kind = 1;
                weights.tok_embd_row_bytes = tok_row;
                weights.wq_layer_bytes = (UINT64)config.dim * wq_row;
                weights.wk_layer_bytes = (UINT64)kv_dim * wk_row;
                weights.wv_layer_bytes = (UINT64)kv_dim * wk_row;
                weights.wo_layer_bytes = (UINT64)config.dim * wo_row;
                weights.w1_layer_bytes = (UINT64)config.hidden_dim * w1_row;
                weights.w2_layer_bytes = (UINT64)config.dim * w2_row;
                weights.w3_layer_bytes = (UINT64)config.hidden_dim * w3_row;

                // token_embedding_table (Q8_0) [vocab, dim]
                off = llmk_align_up_u64(off, A);
                weights.token_embedding_table_q8 = base + (UINTN)off;
                off += vocab_u * tok_row;

                // rms_att_weight (F32) [n_layers, dim]
                off = llmk_align_up_u64(off, A);
                weights.rms_att_weight = (float *)(base + (UINTN)off);
                off += lay_u * dim_u * 4ULL;

                // wq (Q8_0) per-layer [dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wq_q8 = base + (UINTN)off;
                off += lay_u * weights.wq_layer_bytes;

                // wk (Q8_0) per-layer [kv_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wk_q8 = base + (UINTN)off;
                off += lay_u * weights.wk_layer_bytes;

                // wv (Q8_0) per-layer [kv_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wv_q8 = base + (UINTN)off;
                off += lay_u * weights.wv_layer_bytes;

                // wo (Q8_0) per-layer [dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wo_q8 = base + (UINTN)off;
                off += lay_u * weights.wo_layer_bytes;

                // rms_ffn_weight (F32) [n_layers, dim]
                off = llmk_align_up_u64(off, A);
                weights.rms_ffn_weight = (float *)(base + (UINTN)off);
                off += lay_u * dim_u * 4ULL;

                // w1 (Q8_0) per-layer [hidden_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.w1_q8 = base + (UINTN)off;
                off += lay_u * weights.w1_layer_bytes;

                // w2 (Q8_0) per-layer [dim, hidden_dim]
                off = llmk_align_up_u64(off, A);
                weights.w2_q8 = base + (UINTN)off;
                off += lay_u * weights.w2_layer_bytes;

                // w3 (Q8_0) per-layer [hidden_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.w3_q8 = base + (UINTN)off;
                off += lay_u * weights.w3_layer_bytes;

                // rms_final_weight (F32) [dim]
                off = llmk_align_up_u64(off, A);
                weights.rms_final_weight = (float *)(base + (UINTN)off);
                off += dim_u * 4ULL;

                // freq_cis_real + freq_cis_imag (F32 zeros) [seq_len * head_size / 2] each
                off = llmk_align_up_u64(off, A);
                off += (UINT64)config.seq_len * head_size_u / 2ULL * 4ULL;
                off += (UINT64)config.seq_len * head_size_u / 2ULL * 4ULL;

                // wcls (Q8_0) [vocab, dim] if not shared
                if (shared_classifier) {
                    weights.wcls_q8 = weights.token_embedding_table_q8;
                } else {
                    off = llmk_align_up_u64(off, A);
                    weights.wcls_q8 = base + (UINTN)off;
                    off += vocab_u * tok_row;
                }

                (void)hid_u;
                (void)kv_dim_u;
            }
        } else {
            float *weights_mem = (float *)weights_mem_raw;
            status = llmk_gguf_load_into_llama2_layout(
                ModelFile,
                gguf_plan,
                weights_mem,
                config.dim,
                config.hidden_dim,
                config.n_layers,
                config.n_heads,
                config.n_kv_heads,
                config.vocab_size,
                config.seq_len,
                shared_classifier
            );
            if (gguf_plan) {
                llmk_gguf_free_plan(gguf_plan);
                gguf_plan = NULL;
            }
            if (EFI_ERROR(status)) {
                Print(L"ERROR: Failed to load GGUF weights (%r).\r\n", status);
                return EFI_LOAD_ERROR;
            }

            float* weights_ptr = weights_mem;

            weights.kind = 0;
            weights.token_embedding_table = weights_ptr;
            weights_ptr += config.vocab_size * config.dim;
    
            weights.rms_att_weight = weights_ptr;
            weights_ptr += config.n_layers * config.dim;
    
            weights.wq = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.dim;
    
            weights.wk = weights_ptr;
            weights_ptr += config.n_layers * config.dim * kv_dim;
    
            weights.wv = weights_ptr;
            weights_ptr += config.n_layers * config.dim * kv_dim;
    
            weights.wo = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.dim;
    
            weights.rms_ffn_weight = weights_ptr;
            weights_ptr += config.n_layers * config.dim;
    
            weights.w1 = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.hidden_dim;
    
            weights.w2 = weights_ptr;
            weights_ptr += config.n_layers * config.hidden_dim * config.dim;
    
            weights.w3 = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.hidden_dim;
    
            weights.rms_final_weight = weights_ptr;
            weights_ptr += config.dim;
    
            // Skip freq_cis_real and freq_cis_imag (RoPE precomputed freqs)
            weights_ptr += config.seq_len * head_size / 2;  // freq_cis_real
            weights_ptr += config.seq_len * head_size / 2;  // freq_cis_imag

            weights.wcls = shared_classifier ? weights.token_embedding_table : weights_ptr;
        }
    } else {
        float *weights_mem = (float *)weights_mem_raw;
        status = read_exact(ModelFile, weights_mem, bytes_to_read);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: Failed to read weights (need model file + enough RAM).\r\n");
            return EFI_LOAD_ERROR;
        }

        float* weights_ptr = weights_mem;

        weights.kind = 0;
        weights.token_embedding_table = weights_ptr;
        weights_ptr += config.vocab_size * config.dim;

        weights.rms_att_weight = weights_ptr;
        weights_ptr += config.n_layers * config.dim;

        weights.wq = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.dim;

        weights.wk = weights_ptr;
        weights_ptr += config.n_layers * config.dim * kv_dim;

        weights.wv = weights_ptr;
        weights_ptr += config.n_layers * config.dim * kv_dim;

        weights.wo = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.dim;

        weights.rms_ffn_weight = weights_ptr;
        weights_ptr += config.n_layers * config.dim;

        weights.w1 = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.hidden_dim;

        weights.w2 = weights_ptr;
        weights_ptr += config.n_layers * config.hidden_dim * config.dim;

        weights.w3 = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.hidden_dim;

        weights.rms_final_weight = weights_ptr;
        weights_ptr += config.dim;

        // Skip freq_cis_real and freq_cis_imag (RoPE precomputed freqs)
        weights_ptr += config.seq_len * head_size / 2;  // freq_cis_real
        weights_ptr += config.seq_len * head_size / 2;  // freq_cis_imag

        weights.wcls = shared_classifier ? weights.token_embedding_table : weights_ptr;

    }

    uefi_call_wrapper(ModelFile->Close, 1, ModelFile);
    
    if (g_boot_verbose) {
        Print(L"OK: Weights mapped\r\n\r\n");
    }

    llmk_boot_mark(L"weights_mapped");
    
    // ========================================================================
    // [5/7] State Buffers
    // ========================================================================

    llmk_overlay_stage(5, 7);
    
    if (g_boot_verbose) {
        Print(L"[5/7] Allocating state buffers...\r\n");
    }
    
    RunState state;

    int ctx_min = 64;
    int ctx_try = config.seq_len;
    int alloc_ok = 0;
    while (!alloc_ok) {
        state.x = (float*)simple_alloc(config.dim * sizeof(float));
        state.xb = (float*)simple_alloc(config.dim * sizeof(float));
        state.xb2 = (float*)simple_alloc(config.dim * sizeof(float));
        state.hb = (float*)simple_alloc(config.hidden_dim * sizeof(float));
        state.hb2 = (float*)simple_alloc(config.hidden_dim * sizeof(float));
        state.q = (float*)simple_alloc(config.dim * sizeof(float));
        state.k = (float*)simple_alloc(kv_dim * sizeof(float));
        state.v = (float*)simple_alloc(kv_dim * sizeof(float));
        state.att = (float*)simple_alloc(config.n_heads * config.seq_len * sizeof(float));
        state.logits = (float*)simple_alloc(config.vocab_size * sizeof(float));
        state.key_cache = (float*)llmk_alloc_kv((UINT64)config.n_layers * (UINT64)config.seq_len * (UINT64)kv_dim * sizeof(float), L"key cache");
        state.value_cache = (float*)llmk_alloc_kv((UINT64)config.n_layers * (UINT64)config.seq_len * (UINT64)kv_dim * sizeof(float), L"value cache");

        alloc_ok = (state.x && state.xb && state.xb2 && state.hb && state.hb2 && state.q && state.k && state.v &&
                    state.att && state.logits && state.key_cache && state.value_cache);
        if (alloc_ok) break;

        Print(L"\r\nERROR: OOM while allocating state/KV (seq_len=%d).\r\n", config.seq_len);
        llmk_print_ram_budget();

        if (g_llmk_ready) {
            llmk_arena_wipe_and_reset(&g_zones, LLMK_ARENA_ACTIVATIONS, 0);
            llmk_arena_wipe_and_reset(&g_zones, LLMK_ARENA_KV_CACHE, 0);
        }

        if (ctx_try <= ctx_min) {
            Print(L"Hint: use a smaller model or lower ctx_len in repl.cfg.\r\n");
            return EFI_OUT_OF_RESOURCES;
        }

        ctx_try = ctx_try / 2;
        if (ctx_try < ctx_min) ctx_try = ctx_min;
        config.seq_len = ctx_try;
        Print(L"Retrying with smaller ctx_len=%d...\r\n\r\n", config.seq_len);
    }
    
    if (g_boot_verbose) {
        Print(L"OK: State buffers allocated\r\n\r\n");
    }

    llmk_boot_mark(L"state_alloc");
    
    // ========================================================================
    // [6/7] Tokenizer
    // ========================================================================

    llmk_overlay_stage(6, 7);
    
    if (g_boot_verbose) {
        Print(L"[6/7] Loading tokenizer...\r\n");
    }
    
    EFI_FILE_HANDLE TokFile;
    TokFile = NULL;
    status = llmk_open_read_with_fat83_fallback(Root, L"tokenizer.bin", &TokFile, NULL, 0, L"tokenizer");
    if (EFI_ERROR(status) || !TokFile) {
        Print(L"ERROR: Tokenizer file not found (%r)\r\n", status);
        return status;
    }
    
    Tokenizer tokenizer;
    bytes_to_read = sizeof(int);
    uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &tokenizer.max_token_length);
    
    tokenizer.vocab_size = config.vocab_size;
    tokenizer.vocab = (char**)simple_alloc(config.vocab_size * sizeof(char*));
    tokenizer.vocab_scores = (float*)simple_alloc(config.vocab_size * sizeof(float));
    
    for (int i = 0; i < config.vocab_size; i++) {
        // Keep the GOP loading overlay alive while parsing tokenizer.
        // Rate-limited to stay cheap in UEFI.
        if (((UINT32)i & 0xFFu) == 0u) {
            InterfaceFx_ProgressBytes((UINTN)(i + 1), (UINTN)config.vocab_size);
        }
        bytes_to_read = sizeof(float);
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &tokenizer.vocab_scores[i]);
        
        int len;
        bytes_to_read = sizeof(int);
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &len);
        
        tokenizer.vocab[i] = (char*)simple_alloc(len + 1);
        bytes_to_read = len;
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, tokenizer.vocab[i]);
        tokenizer.vocab[i][len] = '\0';
    }
    
    uefi_call_wrapper(TokFile->Close, 1, TokFile);

    // Loading finished: stop the animated overlay now.
    InterfaceFx_End();

    llmk_boot_mark(L"tokenizer_loaded");
    
    if (g_boot_verbose) {
        Print(L"OK: Tokenizer loaded (%d tokens)\r\n\r\n", tokenizer.vocab_size);

        llmk_boot_print_timing_summary();

        // ========================================================================
        // [7/7] Interactive REPL Loop
        // ========================================================================
        Print(L"[7/7] Entering chat loop...\r\n\r\n");

        Print(L"----------------------------------------\r\n");
        Print(L"  CHAT MODE ACTIVE\r\n");
        Print(L"  Type 'quit' or 'exit' to stop\r\n");
        Print(L"  Multi-line: end line with '\\\\' to continue; ';;' alone submits\r\n");
        Print(L"  Commands: use /help or /commands\r\n");
        Print(L"----------------------------------------\r\n\r\n");
    } else {
        Print(L"OK: REPL ready (/help)\r\n\r\n");
    }

    llmk_boot_mark(L"repl_ready");
    
    // Initialize runtime metrics
    llmk_metrics_reset();
    
    // Sampling parameters
    // Defaults come from the Phase 5 metabolism profile (metabion_profile.h).
    float temperature = METABION_DEFAULT_TEMPERATURE;
    float min_p = METABION_DEFAULT_MIN_P;
    float top_p = METABION_DEFAULT_TOP_P;
    int top_k = METABION_DEFAULT_TOP_K;
    float repeat_penalty = METABION_DEFAULT_REPEAT_PENALTY;
    int no_repeat_ngram = METABION_DEFAULT_NO_REPEAT_NGRAM;
    int max_gen_tokens = METABION_DEFAULT_MAX_GEN_TOKENS;
    int stats_enabled = METABION_DEFAULT_STATS_ENABLED;
    // Turn-based chat defaults: stop when the model starts the next user prompt.
    // Double-newline stopping is useful for TinyStories prose, but is too aggressive for chat.
    int stop_on_you = METABION_DEFAULT_STOP_ON_YOU;
    int stop_on_double_nl = METABION_DEFAULT_STOP_ON_DOUBLE_NL;

    // Optional config: repl.cfg (key=value). Best-effort; ignored if missing.
    llmk_load_repl_cfg_best_effort(
        &temperature,
        &min_p,
        &top_p,
        &top_k,
        &repeat_penalty,
        &no_repeat_ngram,
        &max_gen_tokens,
        &stats_enabled,
        &stop_on_you,
        &stop_on_double_nl
    );

    // Phase Q: Apply DNA-driven sampler bias (after config/profile, before autotune).
    // Soft blend: 80% config + 20% DNA. Warden pressure reduces temperature further.
    if (g_soma_initialized && g_soma_dna.generation > 0) {
        temperature = soma_dna_blend_temperature(&g_soma_dna, soma_domain_used, temperature);
        top_p       = soma_dna_blend_top_p     (&g_soma_dna, soma_domain_used, top_p);
        temperature = soma_dna_pressure_temperature(temperature, g_soma_warden.pressure_level);
        if (g_boot_verbose >= 2)
            Print(L"[SomaDNA-Q] temp=%d/1000 top_p=%d/1000 bias=%d/100 gen=%d\r\n",
                  (int)(temperature * 1000.0f),
                  (int)(top_p       * 1000.0f),
                  (int)(g_soma_dna.cognition_bias * 100.0f),
                  (int)g_soma_dna.generation);
    }

    int m18_base_temp_milli = (int)(temperature * 1000.0f + 0.5f);
    int m18_base_top_p_milli = (int)(top_p * 1000.0f + 0.5f);
    int m18_base_top_k = top_k;
    int m18_base_max_gen_tokens = max_gen_tokens;

    if (m18_base_temp_milli < g_autotune.min_temp_milli) m18_base_temp_milli = g_autotune.min_temp_milli;
    if (m18_base_top_p_milli < g_autotune.min_top_p_milli) m18_base_top_p_milli = g_autotune.min_top_p_milli;
    if (m18_base_top_k < g_autotune.min_top_k) m18_base_top_k = g_autotune.min_top_k;
    if (m18_base_max_gen_tokens < g_autotune.min_max_gen_tokens) m18_base_max_gen_tokens = g_autotune.min_max_gen_tokens;

    if (g_autotune.enabled && g_boot_verbose) {
        Print(L"[cfg] m18 autotune enabled (decode_cpt_hi=%lu decode_cpt_lo=%lu)\r\n",
              g_autotune.decode_cpt_hi,
              g_autotune.decode_cpt_lo);
    }
    if (g_guardrails.enabled && g_boot_verbose) {
        Print(L"[cfg] m18.1 guardrails enabled (hard_stop_overruns=%d safe_turns=%d)\r\n",
              g_guardrails.hard_stop_overruns_decode,
              g_guardrails.safe_mode_turns);
    }

    // Optional Diopion config (repl.cfg): burst defaults + profile.
    llmk_load_repl_cfg_diopion_best_effort(&g_diopion);

    // Optional Djibion config (repl.cfg): enables enforcement at boot.
    llmk_load_repl_cfg_djibion_best_effort(&g_djibion);

    if (g_cfg_loaded && g_boot_verbose) {
        Print(L"[cfg] autorun_autostart=%d file=%s shutdown_when_done=%d\r\n",
              g_cfg_autorun_autostart,
              g_cfg_autorun_file,
              g_cfg_autorun_shutdown_when_done);
    }

    // OO config defaults (best-effort from repl.cfg)
    int oo_autoload = 0;
    int oo_autosave_every = 0;
    char oo_file_ascii[96];
    oo_file_ascii[0] = 0;
    llmk_load_repl_cfg_oo_best_effort(&oo_autoload, &oo_autosave_every, oo_file_ascii, (int)sizeof(oo_file_ascii));

    CHAR16 oo_state_file[96];
    if (oo_file_ascii[0]) {
        ascii_to_char16(oo_state_file, oo_file_ascii, (int)(sizeof(oo_state_file) / sizeof(oo_state_file[0])));
    } else {
        StrCpy(oo_state_file, L"oo-state.bin");
    }

    if (oo_autoload) {
        // Djibion gate (best-effort) for boot-time OO autoload.
        CHAR16 load_name[96];
        StrCpy(load_name, oo_state_file);
        if (g_djibion.mode != DJIBION_MODE_OFF) {
            char file8[128];
            llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), load_name);
            DjibionDecision d;
            djibion_decide(&g_djibion, DJIBION_ACT_OO_LOAD, file8, 0, &d);
            djibion_log_if_observe(&g_djibion, "oo_autoload", &d);
            if (djibion_should_block(&g_djibion, &d)) {
                CHAR16 msg[160];
                ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                Print(L"[oo] autoload blocked by Djibion: %s\r\n", msg);
                goto oo_autoload_done;
            }
            if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                Print(L"[oo] autoload path transformed by Djibion -> ");
                llmk_print_ascii(d.transformed_arg0);
                Print(L"\r\n");
                ascii_to_char16(load_name, d.transformed_arg0, (int)(sizeof(load_name) / sizeof(load_name[0])));
            }
        }

        void *buf = NULL;
        UINTN len = 0;
        EFI_STATUS st = llmk_read_entire_file_best_effort(load_name, &buf, &len);
        CHAR16 bak[120];
        llmk_make_bak_name(load_name, bak, (int)(sizeof(bak) / sizeof(bak[0])));

        if (EFI_ERROR(st)) {
            // Fallback to .bak
            EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
            if (EFI_ERROR(st2)) {
                Print(L"[oo] autoload skipped (%r)\r\n", st);
            } else {
                int imported = llmk_oo_import((const char *)buf, (int)len);
                uefi_call_wrapper(BS->FreePool, 1, buf);
                if (imported < 0) {
                    Print(L"[oo] autoload failed (parse)\r\n");
                } else {
                    Print(L"[oo] autoloaded %d entr%s from %s\r\n", imported, (imported == 1) ? L"y" : L"ies", bak);
                }
            }
        } else {
            int imported = llmk_oo_import((const char *)buf, (int)len);
            uefi_call_wrapper(BS->FreePool, 1, buf);
            if (imported < 0) {
                // Fallback to .bak if main parse failed.
                EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
                if (EFI_ERROR(st2)) {
                    Print(L"[oo] autoload failed (parse)\r\n");
                } else {
                    imported = llmk_oo_import((const char *)buf, (int)len);
                    uefi_call_wrapper(BS->FreePool, 1, buf);
                    if (imported < 0) {
                        Print(L"[oo] autoload failed (parse)\r\n");
                    } else {
                        Print(L"[oo] autoloaded %d entr%s from %s\r\n", imported, (imported == 1) ? L"y" : L"ies", bak);
                    }
                }
            } else {
                Print(L"[oo] autoloaded %d entr%s from %s\r\n", imported, (imported == 1) ? L"y" : L"ies", load_name);
            }
        }
    }

oo_autoload_done:

    // Optional autorun: only if enabled in repl.cfg (autorun_autostart=1).
    if (g_cfg_autorun_autostart) {
        // Djibion gate for boot-time autorun (autostart).
        CHAR16 ar_name[96];
        StrCpy(ar_name, g_cfg_autorun_file);
        if (g_djibion.mode != DJIBION_MODE_OFF) {
            char file8[128];
            llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), ar_name);
            DjibionDecision d;
            djibion_decide(&g_djibion, DJIBION_ACT_AUTORUN, file8, 0, &d);
            djibion_log_if_observe(&g_djibion, "autorun_autostart", &d);
            if (djibion_should_block(&g_djibion, &d)) {
                CHAR16 msg[160];
                ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                Print(L"[cfg] autorun autostart blocked by Djibion: %s\r\n", msg);
                goto autorun_autostart_done;
            }
            if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                Print(L"[cfg] autorun autostart path transformed by Djibion -> ");
                llmk_print_ascii(d.transformed_arg0);
                Print(L"\r\n");
                ascii_to_char16(ar_name, d.transformed_arg0, (int)(sizeof(ar_name) / sizeof(ar_name[0])));
            }
        }
        llmk_autorun_start(ar_name, g_cfg_autorun_shutdown_when_done);
    }

autorun_autostart_done:
    
    int conversation_count = 0;
    
    // KV cache position tracking (persistent across prompts for context retention)
    int kv_pos = 0;
    g_llmk_kv_pos = 0;

    // Optional snapshot auto-resume (repl.cfg): snap_autoload=1
    {
        int snap_autoload = 0;
        char snap_file_ascii[96];
        snap_file_ascii[0] = 0;
        llmk_load_repl_cfg_snap_best_effort(&snap_autoload, snap_file_ascii, (int)sizeof(snap_file_ascii));
        if (snap_autoload) {
            CHAR16 snap_file[96];
            if (snap_file_ascii[0]) {
                ascii_to_char16(snap_file, snap_file_ascii, (int)(sizeof(snap_file) / sizeof(snap_file[0])));
            } else {
                StrCpy(snap_file, L"llmk-snap.bin");
            }

            // Djibion gate for boot-time snapshot autoload.
            if (g_djibion.mode != DJIBION_MODE_OFF) {
                char file8[128];
                llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), snap_file);
                DjibionDecision d;
                djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, file8, 0, &d);
                djibion_log_if_observe(&g_djibion, "snap_autoload", &d);
                if (djibion_should_block(&g_djibion, &d)) {
                    CHAR16 msg[160];
                    ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                    Print(L"[cfg] snapshot autoload blocked by Djibion: %s\r\n", msg);
                    goto snap_autoload_done;
                }
                if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                    Print(L"[cfg] snapshot autoload path transformed by Djibion -> ");
                    llmk_print_ascii(d.transformed_arg0);
                    Print(L"\r\n");
                    ascii_to_char16(snap_file, d.transformed_arg0, (int)(sizeof(snap_file) / sizeof(snap_file[0])));
                }
            }

            EFI_STATUS st = llmk_snap_load_into_state_best_effort(&state, &config, &kv_pos, snap_file);
            if (EFI_ERROR(st)) {
                Print(L"[snap] autoload skipped (%r)\r\n", st);
                llmk_tr_note("SNAP: autoload failed");
            } else {
                Print(L"[snap] autoloaded %s (kv_pos=%d)\r\n", snap_file, kv_pos);
                llmk_tr_note("SNAP: autoloaded");
            }
        }
    }

snap_autoload_done:
    
    // MAIN LOOP
    while (1) { // SAFE: intentional long-running organism loop; terminates only via explicit operator command/shutdown.
        conversation_count++;

        // capture-mode state (per-turn)
        // capture_kind: 0=none, 1=/draw, 2=/oo_think, 3=/oo_auto, 4=/oo_exec
        int capture_kind = 0;
        int draw_mode = 0;
        int oo_think_id = 0;
        int oo_auto_planning = 0;
        int oo_auto_action_k = 0;
        int oo_exec_planning = 0;
        int oo_exec_action_k = 0;
        char oo_think_user[256];
        oo_think_user[0] = 0;
        int saved_stop_on_you = stop_on_you;
        int saved_stop_on_double_nl = stop_on_double_nl;
        int saved_max_gen_tokens = max_gen_tokens;
        int draw_saved_sampling = 0;
        float saved_temperature = temperature;
        float saved_min_p = min_p;
        float saved_top_p = top_p;
        int saved_top_k = top_k;
        float saved_repeat_penalty = repeat_penalty;
        char draw_user_prompt[256];
        draw_user_prompt[0] = 0;

        // Current turn prompt (either user input or synthesized for /oo_auto)
        char prompt[512];
        prompt[0] = 0;

        // Policy enforcement for internal OO runners:
        // /oo_auto and /oo_exec cycles run without going through the prompt dispatcher.
        // If these modes are active due to persisted/configured state, stop them here
        // when policy denies the corresponding command.
        if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
            if (!llmk_oo_policy_is_allowed_cmd("/oo_exec")) {
                llmk_oo_policy_warn_deny_cmd("/oo_exec");
                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;
            }
        }
        if (g_oo_auto_active && g_oo_auto_id > 0 && g_oo_auto_remaining > 0) {
            if (!llmk_oo_policy_is_allowed_cmd("/oo_auto")) {
                llmk_oo_policy_warn_deny_cmd("/oo_auto");
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;
            }
        }

        if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
            // Allow user to interrupt exec mode between cycles.
            {
                EFI_INPUT_KEY key;
                EFI_STATUS kst = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
                if (!EFI_ERROR(kst)) {
                    if (key.UnicodeChar == L'q' || key.UnicodeChar == L'Q' || key.ScanCode == SCAN_ESC) {
                        Print(L"\r\n[oo_exec] interrupted by user\r\n\r\n");
                        g_oo_exec_active = 0;
                        g_oo_exec_id = 0;
                        g_oo_exec_remaining = 0;
                        g_oo_exec_total = 0;
                        g_oo_exec_plan_if_empty = 0;
                        g_oo_exec_hint[0] = 0;
                    }
                }
            }
        }

        if (g_oo_auto_active && g_oo_auto_id > 0 && g_oo_auto_remaining > 0) {
            // Allow user to interrupt auto mode between cycles.
            // Note: we poll once per cycle; keypresses are consumed here.
            {
                EFI_INPUT_KEY key;
                EFI_STATUS kst = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
                if (!EFI_ERROR(kst)) {
                    if (key.UnicodeChar == L'q' || key.UnicodeChar == L'Q' || key.ScanCode == SCAN_ESC) {
                        Print(L"\r\n[oo_auto] interrupted by user\r\n\r\n");
                        g_oo_auto_active = 0;
                        g_oo_auto_id = 0;
                        g_oo_auto_remaining = 0;
                        g_oo_auto_total = 0;
                        g_oo_auto_user[0] = 0;
                    }
                }
            }
        }

        if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
            int cycle = (g_oo_exec_total - g_oo_exec_remaining) + 1;
            if (cycle < 1) cycle = 1;
            Print(L"\r\n[oo_exec] cycle %d/%d...\r\n", cycle, (g_oo_exec_total > 0) ? g_oo_exec_total : cycle);

            // Prefer consuming agenda items.
            char cycle_action[96];
            cycle_action[0] = 0;
            int picked_k = 0;

            // String passed into llmk_oo_build_think_prompt (can include hint + action).
            char cycle_user_build[256];
            cycle_user_build[0] = 0;

            if (llmk_oo_agenda_next_ex(g_oo_exec_id, &picked_k, cycle_action, (int)sizeof(cycle_action))) {
                oo_exec_planning = 0;
                oo_exec_action_k = picked_k;
                {
                    // Store just the action as the per-cycle "user" (notes + done marker).
                    int up = 0;
                    for (const char *s = cycle_action; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                    oo_think_user[up] = 0;
                }

                {
                    CHAR16 a16[120];
                    ascii_to_char16(a16, cycle_action, (int)(sizeof(a16) / sizeof(a16[0])));
                    Print(L"[oo_exec] action #%d: %s\r\n", picked_k, a16);
                }

                // Build prompt input: optional hint + action.
                if (g_oo_exec_hint[0]) {
                    int p = 0;
                    const char *h = g_oo_exec_hint;
                    for (int k = 0; h[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = h[k];
                    const char *mid = " | action: ";
                    for (int k = 0; mid[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = mid[k];
                    for (int k = 0; cycle_action[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = cycle_action[k];
                    cycle_user_build[p] = 0;
                } else {
                    int p = 0;
                    for (int k = 0; cycle_action[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = cycle_action[k];
                    cycle_user_build[p] = 0;
                }
            } else {
                if (!g_oo_exec_plan_if_empty) {
                    Print(L"[oo_exec] agenda empty -> stopping\r\n\r\n");
                    g_oo_exec_active = 0;
                    g_oo_exec_id = 0;
                    g_oo_exec_remaining = 0;
                    g_oo_exec_total = 0;
                    g_oo_exec_plan_if_empty = 0;
                    g_oo_exec_hint[0] = 0;
                } else {
                    // Planning cycle: propose one next action and push it.
                    oo_exec_planning = 1;
                    oo_exec_action_k = 0;
                    {
                        const char *plan = "Propose ONE next concrete action (single line, no bullets, no extra text).";
                        int up = 0;
                        for (const char *s = plan; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                        oo_think_user[up] = 0;
                    }
                    {
                        int p = 0;
                        for (int k = 0; oo_think_user[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = oo_think_user[k];
                        cycle_user_build[p] = 0;
                    }
                    Print(L"[oo_exec] agenda empty -> planning next action\r\n");
                }
            }

            if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
                char new_prompt[512];
                if (!llmk_oo_build_think_prompt(g_oo_exec_id, cycle_user_build, new_prompt, (int)sizeof(new_prompt))) {
                    Print(L"[oo_exec] ERROR: unknown entity id=%d (stopping)\r\n\r\n", g_oo_exec_id);
                    g_oo_exec_active = 0;
                    g_oo_exec_id = 0;
                    g_oo_exec_remaining = 0;
                    g_oo_exec_total = 0;
                    g_oo_exec_plan_if_empty = 0;
                    g_oo_exec_hint[0] = 0;
                } else {
                    // Keep model context clean per-cycle.
                    reset_kv_cache(&state, &config);
                    kv_pos = 0;
                    g_llmk_kv_pos = kv_pos;

                    for (int k = 0; k < (int)sizeof(prompt); k++) {
                        prompt[k] = new_prompt[k];
                        if (new_prompt[k] == 0) break;
                    }
                    oo_think_id = g_oo_exec_id;

                    g_capture_mode = 1;
                    capture_kind = 4; // /oo_exec cycle
                    llmk_capture_reset();
                    stop_on_you = 0;
                    stop_on_double_nl = 1;
                    if (max_gen_tokens > 64) max_gen_tokens = 64;
                }
            }
        }

        if (g_oo_auto_active && g_oo_auto_id > 0 && g_oo_auto_remaining > 0) {
            int cycle = (g_oo_auto_total - g_oo_auto_remaining) + 1;
            if (cycle < 1) cycle = 1;
            Print(L"\r\n[oo_auto] cycle %d/%d...\r\n", cycle, (g_oo_auto_total > 0) ? g_oo_auto_total : cycle);

            // Auto-consume agenda: if an action exists, select it and mark it DOING.
            // Falls back to the configured /oo_auto prompt when agenda is empty.
            const char *cycle_user = g_oo_auto_user;
            char cycle_action[96];
            cycle_action[0] = 0;
            int picked_k = 0;
            if (llmk_oo_agenda_next_ex(g_oo_auto_id, &picked_k, cycle_action, (int)sizeof(cycle_action))) {
                cycle_user = cycle_action;
                oo_auto_action_k = picked_k;
                {
                    CHAR16 a16[120];
                    ascii_to_char16(a16, cycle_action, (int)(sizeof(a16) / sizeof(a16[0])));
                    Print(L"[oo_auto] action #%d: %s\r\n", picked_k, a16);
                }
                oo_auto_planning = 0;
            } else {
                // No agenda to execute: ask the model for exactly one next action and push it.
                // This does NOT consume a cycle (remaining is unchanged).
                oo_auto_planning = 1;
                oo_auto_action_k = 0;
                cycle_user = "Propose ONE next concrete action (single line, no bullets, no extra text).";
                Print(L"[oo_auto] agenda empty -> planning next action\r\n");
            }

            char new_prompt[512];
            if (!llmk_oo_build_think_prompt(g_oo_auto_id, cycle_user, new_prompt, (int)sizeof(new_prompt))) {
                Print(L"[oo_auto] ERROR: unknown entity id=%d (stopping)\r\n\r\n", g_oo_auto_id);
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
            } else {
                // /oo_auto builds a fresh prompt every cycle; keep model context clean per-cycle.
                reset_kv_cache(&state, &config);
                kv_pos = 0;
                g_llmk_kv_pos = kv_pos;

                for (int k = 0; k < (int)sizeof(prompt); k++) {
                    prompt[k] = new_prompt[k];
                    if (new_prompt[k] == 0) break;
                }
                // Per-cycle state for capture handler
                oo_think_id = g_oo_auto_id;
                {
                    int up = 0;
                    if (cycle_user && cycle_user[0]) {
                        for (const char *s = cycle_user; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                    }
                    oo_think_user[up] = 0;
                }
                oo_think_user[(int)sizeof(oo_think_user) - 1] = 0;

                g_capture_mode = 1;
                capture_kind = 3; // /oo_auto cycle
                llmk_capture_reset();
                stop_on_you = 0;
                stop_on_double_nl = 1;
                if (max_gen_tokens > 64) max_gen_tokens = 64;
            }
        }

        if (prompt[0] == 0) {
            // Autorun: consume next scripted line instead of blocking for keyboard input.
            if (g_autorun_active) {
                if (llmk_autorun_next_line(prompt, (int)sizeof(prompt))) {
                    CHAR16 p16[540];
                    ascii_to_char16(p16, prompt, (int)(sizeof(p16) / sizeof(p16[0])));
                    Print(L"You (autorun): %s\r\n", p16);
                    llmk_tr_push_prefixed("AUTO: ", prompt);
                } else {
                    Print(L"[autorun] done\r\n");
                    int shutdown = g_autorun_shutdown_when_done;
                    llmk_autorun_stop();
                    if (shutdown) {
                        Print(L"[autorun] shutting down\r\n");
                        llmk_shutdown_best_effort();
                    }
                }
            }

            // Orchestrion: inject next pipeline step (non-blocking) when running.
            if (prompt[0] == 0 && g_orchestrion.mode != ORCHESTRION_MODE_OFF) {
                const char *step = orchestrion_pipeline_next_step(&g_orchestrion);
                if (step && step[0]) {
                    // Flow Enforcer (Zone-OO): when Orchestrion is in ENFORCE mode,
                    // only allow /oo* commands (and those are still subject to OO policy).
                    // This prevents Orchestrion pipelines from executing arbitrary system commands.
                    if (g_orchestrion.mode == ORCHESTRION_MODE_ENFORCE) {
                        const char *s = step;
                        while (*s == ' ' || *s == '\t') s++;
                        if (*s == '/') {
                            if (llmk_oo_policy_startswith_ci(s, "/oo")) {
                                // If denied, do not inject into prompt; stop pipeline and mark error.
                                if (!llmk_oo_policy_check_prompt_and_warn(s)) {
                                    g_orchestrion.pipeline.state = ORCHESTRION_STATE_ERROR;
                                    g_orchestrion.errors++;
                                    llmk_tr_note("ORCH_ENFORCE: deny /oo");
                                    continue;
                                }
                            } else {
                                Print(L"\r\n[orch_enforce] DENY non-oo command: ");
                                llmk_print_ascii(s);
                                Print(L"\r\n\r\n");
                                g_orchestrion.pipeline.state = ORCHESTRION_STATE_ERROR;
                                g_orchestrion.errors++;
                                llmk_tr_note("ORCH_ENFORCE: deny non-oo");
                                continue;
                            }
                        }
                    }

                    int i = 0;
                    for (; step[i] && i + 1 < (int)sizeof(prompt); i++) prompt[i] = step[i];
                    prompt[i] = 0;

                    CHAR16 p16[540];
                    ascii_to_char16(p16, prompt, (int)(sizeof(p16) / sizeof(p16[0])));
                    Print(L"You (orch): %s\r\n", p16);
                    llmk_tr_push_prefixed("ORCH: ", prompt);
                }
            }

            // Read user input
            if (prompt[0] == 0) {
                CHAR16 user_input[512];
                Print(L"You: ");
                read_user_input(user_input, 512);

                // Convert to char
                char16_to_char(prompt, user_input, 512);
                if (prompt[0]) llmk_tr_push_prefixed("YOU: ", prompt);
            }
        }

        // If GOP TUI is enabled, refresh it for every prompt/command.
        llmk_tui_on_prompt_best_effort(prompt);

        // Special command: /draw uses the model to generate render DSL, captures it, then runs /render.
        // It intentionally consumes context like any other prompt; use /clear if you want a clean slate.
        if (my_strncmp(prompt, "/draw", 5) == 0) {
            if (!g_gop_fb32) {
                Print(L"\r\nERROR: GOP not available (cannot draw)\r\n\r\n");
                continue;
            }

            const char *q = prompt + 5;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == 0) {
                Print(L"\r\nUsage: /draw <prompt>\r\n");
                Print(L"  Example: /draw a futuristic NOOSPHERE logo\r\n\r\n");
                continue;
            }

            // Immediate feedback: /draw can be slow under TCG, so show progress + clear screen.
            Print(L"\r\n[draw] generating DSL (may take a while under emulation) ...\r\n");
            llmk_gop_clear(0, 0, 0);
            llmk_gop_force_update();

            // Save the user's raw request for fallback rendering.
            {
                int up = 0;
                for (const char *s = q; *s && up + 1 < (int)sizeof(draw_user_prompt); s++) {
                    draw_user_prompt[up++] = *s;
                }
                draw_user_prompt[up] = 0;
            }

            // Build a compact instruction prompt.
            // Keep it short (prompt buffer is 512 bytes).
            // Use concrete examples to override narrative bias from stories model.
            char new_prompt[512];
            int p = 0;
            const char *prefix = "INSTRUCTION: Output render DSL code only. Format: clear R G B; rect X Y W H R G B; pixel X Y R G B; Example: clear 0 0 0; rect 100 100 50 50 255 255 255; END. Now:";
            for (const char *s = prefix; *s && p + 1 < (int)sizeof(new_prompt); s++) new_prompt[p++] = *s;
            if (p + 1 < (int)sizeof(new_prompt)) new_prompt[p++] = ' ';
            for (const char *s = q; *s && p + 1 < (int)sizeof(new_prompt); s++) new_prompt[p++] = *s;
            const char *suffix = " OUTPUT:";
            for (const char *s = suffix; *s && p + 1 < (int)sizeof(new_prompt); s++) new_prompt[p++] = *s;
            if (p + 2 < (int)sizeof(new_prompt)) { new_prompt[p++] = '\n'; new_prompt[p++] = 0; }
            else new_prompt[(int)sizeof(new_prompt) - 1] = 0;

            // Swap in the synthesized prompt for this turn.
            for (int i = 0; i < (int)sizeof(prompt); i++) {
                prompt[i] = new_prompt[i];
                if (new_prompt[i] == 0) break;
            }

            // Configure capture mode.
            draw_mode = 1;
            g_capture_mode = 1;
            capture_kind = 1;
            llmk_capture_reset();

            // Force deterministic sampling for /draw (TinyStories models tend to drift into prose).
            draw_saved_sampling = 1;
            saved_temperature = temperature;
            saved_min_p = min_p;
            saved_top_p = top_p;
            saved_top_k = top_k;
            saved_repeat_penalty = repeat_penalty;
            temperature = 0.0f;
            min_p = 0.0f;
            top_p = 0.0f;
            top_k = 1;
            repeat_penalty = 1.0f;

            // Prefer to stop on double newline in case END never appears.
            stop_on_you = 0;
            stop_on_double_nl = 1;
            if (max_gen_tokens > 48) max_gen_tokens = 48;
        }

        // M19.1 benchmark: /bench_case rewrites into a normal prompt and falls through into generation.
        // Usage:
        //   /bench_case <case_id> <category> <max_new_tokens> <prompt...>
        if (!draw_mode && my_strncmp(prompt, "/bench_case", 11) == 0) {
            const char *s = prompt + 11;
            while (*s == ' ' || *s == '\t') s++;

            if (*s == 0) {
                Print(L"\r\nUsage: /bench_case <id> <cat> <max_new_tokens> <prompt...>\r\n\r\n");
                prompt[0] = 0;
                continue;
            }
            if (!g_bench_active || !g_bench_file) {
                Print(L"\r\nERROR: bench not active. Run /bench_begin first.\r\n\r\n");
                prompt[0] = 0;
                continue;
            }

            char case_id[64];
            char category[48]; // SAFE: small fixed category label parsed with bounds check
            int max_new = 0;

            int p = 0;
            while (*s && *s != ' ' && *s != '\t' && p + 1 < (int)sizeof(case_id)) case_id[p++] = *s++;
            case_id[p] = 0;
            while (*s == ' ' || *s == '\t') s++;

            p = 0;
            while (*s && *s != ' ' && *s != '\t' && p + 1 < (int)sizeof(category)) category[p++] = *s++;
            category[p] = 0;
            while (*s == ' ' || *s == '\t') s++;

            while (*s >= '0' && *s <= '9') {
                max_new = max_new * 10 + (*s - '0');
                s++;
            }
            while (*s == ' ' || *s == '\t') s++;

            if (case_id[0] == 0 || category[0] == 0 || max_new <= 0 || *s == 0) {
                Print(L"\r\nUsage: /bench_case <id> <cat> <max_new_tokens> <prompt...>\r\n\r\n");
                prompt[0] = 0;
                continue;
            }

            // Keep each case independent.
            reset_kv_cache(&state, &config);
            kv_pos = 0;
            g_llmk_kv_pos = kv_pos;

            if (max_new > MAX_TOKENS) max_new = MAX_TOKENS;
            max_gen_tokens = max_new;
            llmk_bench_prepare_case(case_id, category, max_new);

            // Overwrite prompt with the case prompt text (so it falls through into generation).
            for (int i = 0; i + 1 < (int)sizeof(prompt); i++) {
                prompt[i] = s[i];
                if (s[i] == 0) break;
            }
            prompt[(int)sizeof(prompt) - 1] = 0;
        }
        
        // Check for quit
        if (check_quit_command(prompt)) {
            Print(L"\r\n");
            Print(L"----------------------------------------\r\n");
            Print(L"  Goodbye! Had %d conversations.\r\n", conversation_count - 1);
            Print(L"----------------------------------------\r\n\r\n");
            break;
        }

        // UX: accept common commands even if user forgets the leading '/'.
        // Example: "oo_note 1 hello" becomes "/oo_note 1 hello".
        // Also: typing "reset" (no slash) should not accidentally trigger generation.
        if (!draw_mode && prompt[0] != '/') {
            const char *s = prompt;
            while (*s == ' ' || *s == '\t') s++;

            int do_rewrite = 0;
            const char *rewrite_from = s;

            // Accept OO commands with args.
            if (my_strncmp(s, "oo_", 3) == 0) {
                do_rewrite = 1;
            } else {
                // Accept a small whitelist of exact commands (no args), ignoring leading/trailing whitespace.
                const char *cmd = NULL;
                int cmd_len = 0;
                if (my_strncmp(s, "reset", 5) == 0) {
                    cmd = "reset";
                    cmd_len = 5;
                } else if (my_strncmp(s, "help", 4) == 0) {
                    cmd = "help";
                    cmd_len = 4;
                } else if (my_strncmp(s, "version", 7) == 0) {
                    cmd = "version";
                    cmd_len = 7;
                } else if (my_strncmp(s, "ctx", 3) == 0) {
                    cmd = "ctx";
                    cmd_len = 3;
                } else if (my_strncmp(s, "log", 3) == 0) {
                    cmd = "log";
                    cmd_len = 3;
                } else if (my_strncmp(s, "zones", 5) == 0) {
                    cmd = "zones";
                    cmd_len = 5;
                } else if (my_strncmp(s, "cpu", 3) == 0) {
                    cmd = "cpu";
                    cmd_len = 3;
                }

                if (cmd) {
                    const char *t = s + cmd_len;
                    while (*t == ' ' || *t == '\t') t++;
                    if (*t == 0) {
                        do_rewrite = 1;
                        rewrite_from = cmd; // normalize: drop extra whitespace
                    }
                }
            }

            if (do_rewrite) {
                char tmp2[512];
                int p2 = 0;
                tmp2[p2++] = '/';
                for (int i = 0; rewrite_from[i] && p2 + 1 < (int)sizeof(tmp2); i++) tmp2[p2++] = rewrite_from[i];
                tmp2[p2] = 0;

                for (int i = 0; i < (int)sizeof(prompt); i++) {
                    prompt[i] = tmp2[i];
                    if (tmp2[i] == 0) break;
                }
            }
        }
        
        // Check for commands (except /draw which is handled above and falls through into generation)
        if (!draw_mode && prompt[0] == '/') {
            if (my_strncmp(prompt, "/oo", 3) == 0) {
                if (!llmk_oo_policy_check_prompt_and_warn(prompt)) {
                    continue;
                }
            }
            if (my_strncmp(prompt, "/temp ", 6) == 0) {
                float val = 0.0f;
                int i = 6;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                temperature = val;
                Print(L"  Temperature set to: ");
                Print(L"%d.", (int)temperature);
                Print(L"%d\r\n", (int)((temperature - (int)temperature) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/min_p ", 7) == 0) {
                float val = 0.0f;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                min_p = val;
                Print(L"  Min-p set to: ");
                Print(L"%d.", (int)min_p);
                Print(L"%d\r\n", (int)((min_p - (int)min_p) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/top_p ", 7) == 0) {
                float val = 0.0f;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                top_p = val;
                Print(L"  Top-p set to: ");
                Print(L"%d.", (int)top_p);
                Print(L"%d\r\n", (int)((top_p - (int)top_p) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/top_k ", 7) == 0) {
                int val = 0;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                if (val < 0) val = 0;
                if (val > 256) val = 256;
                top_k = val;
                Print(L"  Top-k set to: %d\r\n", top_k);
                continue;
            } else if (my_strncmp(prompt, "/max_tokens ", 12) == 0) {
                int val = 0;
                int i = 12;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                if (val < 1) val = 1;
                if (val > MAX_TOKENS) val = MAX_TOKENS;
                max_gen_tokens = val;
                Print(L"  Max tokens set to: %d\r\n", max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/seed ", 6) == 0) {
                unsigned int val = 0;
                int i = 6;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10u + (unsigned int)(prompt[i] - '0');
                    i++;
                }
                set_seed(val);
                Print(L"  Seed set to: %d\r\n", (int)g_sample_seed);
                continue;
            } else if (my_strncmp(prompt, "/stats ", 7) == 0) {
                int val = 0;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                stats_enabled = (val != 0);
                Print(L"  Stats: %s\r\n", stats_enabled ? L"on" : L"off");
                continue;
            } else if (my_strncmp(prompt, "/stop_you ", 10) == 0) {
                int val = 0;
                int i = 10;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                stop_on_you = (val != 0);
                Print(L"  Stop on \\nYou:: %s\r\n", stop_on_you ? L"on" : L"off");
                continue;
            } else if (my_strncmp(prompt, "/stop_nl ", 9) == 0) {
                int val = 0;
                int i = 9;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                stop_on_double_nl = (val != 0);
                Print(L"  Stop on double newline: %s\r\n", stop_on_double_nl ? L"on" : L"off");
                continue;
            } else if (my_strncmp(prompt, "/norepeat ", 10) == 0) {
                int val = 0;
                int i = 10;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                if (val < 0) val = 0;
                if (val > 16) val = 16;
                no_repeat_ngram = val;
                Print(L"  No-repeat ngram set to: %d\r\n", no_repeat_ngram);
                continue;
            } else if (my_strncmp(prompt, "/repeat ", 8) == 0) {
                float val = 0.0f;
                int i = 8;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                repeat_penalty = val;
                Print(L"  Repetition penalty set to: ");
                Print(L"%d.", (int)repeat_penalty);
                Print(L"%d\r\n", (int)((repeat_penalty - (int)repeat_penalty) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/sampling", 9) == 0) {
                Print(L"\r\nSampling:\r\n");
                Print(L"  temp=");
                Print(L"%d.", (int)temperature);
                Print(L"%d\r\n", (int)((temperature - (int)temperature) * 100.0f));
                Print(L"  min_p=");
                Print(L"%d.", (int)min_p);
                Print(L"%d\r\n", (int)((min_p - (int)min_p) * 100.0f));
                Print(L"  top_p=");
                Print(L"%d.", (int)top_p);
                Print(L"%d\r\n", (int)((top_p - (int)top_p) * 100.0f));
                Print(L"  top_k=%d\r\n", top_k);
                Print(L"  norepeat=%d\r\n", no_repeat_ngram);
                Print(L"  repeat=");
                Print(L"%d.", (int)repeat_penalty);
                Print(L"%d\r\n", (int)((repeat_penalty - (int)repeat_penalty) * 100.0f));
                Print(L"  max_tokens=%d\r\n\r\n", max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/preset_save", 12) == 0) {
                int i = 12;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    Print(L"\r\nUsage:\r\n");
                    Print(L"  /preset_save stable|creative|greedy\r\n");
                    Print(L"  (persists to repl.cfg; Djibion allow_cfg_write must allow it)\r\n\r\n");
                    continue;
                }

                char name[32]; // SAFE: small preset name token parsed with bounds check
                int n = 0;
                while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(name)) {
                    name[n++] = prompt[i++];
                }
                name[n] = 0;

                int applied = 1;
                // Also hold the canonical string values for cfg persistence.
                const char *cfg_temp = NULL;
                const char *cfg_min_p = NULL;
                const char *cfg_top_p = NULL;
                const char *cfg_top_k = NULL;
                const char *cfg_repeat = NULL;
                const char *cfg_norepeat = NULL;

                if (llmk_cfg_streq_ci(name, "stable")) {
                    temperature = 0.70f;
                    min_p = 0.05f;
                    top_p = 0.90f;
                    top_k = 40;
                    repeat_penalty = 1.10f;
                    no_repeat_ngram = 4;
                    cfg_temp = "0.70";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.90";
                    cfg_top_k = "40";
                    cfg_repeat = "1.10";
                    cfg_norepeat = "4";
                } else if (llmk_cfg_streq_ci(name, "creative")) {
                    temperature = 1.00f;
                    min_p = 0.05f;
                    top_p = 0.95f;
                    top_k = 80;
                    repeat_penalty = 1.05f;
                    no_repeat_ngram = 3;
                    cfg_temp = "1.00";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.95";
                    cfg_top_k = "80";
                    cfg_repeat = "1.05";
                    cfg_norepeat = "3";
                } else if (llmk_cfg_streq_ci(name, "greedy") || llmk_cfg_streq_ci(name, "det") || llmk_cfg_streq_ci(name, "deterministic")) {
                    temperature = 0.00f;
                    min_p = 0.00f;
                    top_p = 0.00f;
                    top_k = 0;
                    repeat_penalty = 1.00f;
                    no_repeat_ngram = 0;
                    cfg_temp = "0.00";
                    cfg_min_p = "0.00";
                    cfg_top_p = "0.00";
                    cfg_top_k = "0";
                    cfg_repeat = "1.00";
                    cfg_norepeat = "0";
                } else {
                    applied = 0;
                }

                if (!applied || !cfg_temp || !cfg_min_p || !cfg_top_p || !cfg_top_k || !cfg_repeat || !cfg_norepeat) {
                    Print(L"  Unknown preset: ");
                    llmk_print_ascii(name);
                    Print(L"\r\n  Try: /preset_save stable | /preset_save creative | /preset_save greedy\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "sampling_preset", (UINT32)my_strlen(name), &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/preset_save): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                int ok = 1;
                EFI_STATUS st;
                st = llmk_repl_cfg_set_kv_best_effort("temp", cfg_temp);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("min_p", cfg_min_p);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("top_p", cfg_top_p);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("top_k", cfg_top_k);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("repeat_penalty", cfg_repeat);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("no_repeat_ngram", cfg_norepeat);
                if (EFI_ERROR(st)) ok = 0;

                Print(L"  Preset applied + saved: ");
                llmk_print_ascii(name);
                Print(L"\r\n");
                if (!ok) {
                    Print(L"  WARNING: repl.cfg update had errors (settings applied in RAM)\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/preset", 7) == 0) {
                int i = 7;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    Print(L"\r\nPresets:\r\n");
                    Print(L"  /preset stable           - temp=0.70 top_p=0.90 top_k=40 min_p=0.05 repeat=1.10 norepeat=4\r\n");
                    Print(L"  /preset creative         - temp=1.00 top_p=0.95 top_k=80 min_p=0.05 repeat=1.05 norepeat=3\r\n");
                    Print(L"  /preset greedy           - temp=0.00 top_p=0.00 top_k=0  min_p=0.00 repeat=1.00 norepeat=0\r\n");
                    Print(L"  /preset stable --save    - same but persists to repl.cfg\r\n");
                    Print(L"  /preset_save stable      - same as --save\r\n\r\n");
                    continue;
                }

                char name[32]; // SAFE: small preset name token parsed with bounds check
                int n = 0;
                while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(name)) {
                    name[n++] = prompt[i++];
                }
                name[n] = 0;

                // Optional: persist to repl.cfg.
                int save_cfg = 0;
                while (prompt[i] == ' ') i++;
                if (prompt[i]) {
                    // Accept: --save or -s
                    if (my_strncmp(prompt + i, "--save", 6) == 0) {
                        save_cfg = 1;
                    } else if (my_strncmp(prompt + i, "-s", 2) == 0) {
                        save_cfg = 1;
                    }
                }

                int applied = 1;
                const char *cfg_temp = NULL;
                const char *cfg_min_p = NULL;
                const char *cfg_top_p = NULL;
                const char *cfg_top_k = NULL;
                const char *cfg_repeat = NULL;
                const char *cfg_norepeat = NULL;
                if (llmk_cfg_streq_ci(name, "stable")) {
                    temperature = 0.70f;
                    min_p = 0.05f;
                    top_p = 0.90f;
                    top_k = 40;
                    repeat_penalty = 1.10f;
                    no_repeat_ngram = 4;
                    cfg_temp = "0.70";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.90";
                    cfg_top_k = "40";
                    cfg_repeat = "1.10";
                    cfg_norepeat = "4";
                } else if (llmk_cfg_streq_ci(name, "creative")) {
                    temperature = 1.00f;
                    min_p = 0.05f;
                    top_p = 0.95f;
                    top_k = 80;
                    repeat_penalty = 1.05f;
                    no_repeat_ngram = 3;
                    cfg_temp = "1.00";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.95";
                    cfg_top_k = "80";
                    cfg_repeat = "1.05";
                    cfg_norepeat = "3";
                } else if (llmk_cfg_streq_ci(name, "greedy") || llmk_cfg_streq_ci(name, "det") || llmk_cfg_streq_ci(name, "deterministic")) {
                    temperature = 0.00f;
                    min_p = 0.00f;
                    top_p = 0.00f;
                    top_k = 0;
                    repeat_penalty = 1.00f;
                    no_repeat_ngram = 0;
                    cfg_temp = "0.00";
                    cfg_min_p = "0.00";
                    cfg_top_p = "0.00";
                    cfg_top_k = "0";
                    cfg_repeat = "1.00";
                    cfg_norepeat = "0";
                } else {
                    applied = 0;
                }

                if (!applied) {
                    Print(L"  Unknown preset: ");
                    llmk_print_ascii(name);
                    Print(L"\r\n  Try: /preset stable | /preset creative | /preset greedy\r\n");
                    continue;
                }

                Print(L"  Preset applied: ");
                llmk_print_ascii(name);

                if (save_cfg) {
                    // Djibion gate (best-effort)
                    if (g_djibion.mode != DJIBION_MODE_OFF) {
                        DjibionDecision d;
                        djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "sampling_preset", (UINT32)my_strlen(name), &d);
                        djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                        if (djibion_should_block(&g_djibion, &d)) {
                            CHAR16 msg[160];
                            ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                            Print(L"\r\nDJIBION: blocked (--save): %s\r\n", msg);
                            Print(L"\r\n");
                            continue;
                        }
                    }

                    int ok = 1;
                    EFI_STATUS st;
                    if (cfg_temp) {
                        st = llmk_repl_cfg_set_kv_best_effort("temp", cfg_temp);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_min_p) {
                        st = llmk_repl_cfg_set_kv_best_effort("min_p", cfg_min_p);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_top_p) {
                        st = llmk_repl_cfg_set_kv_best_effort("top_p", cfg_top_p);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_top_k) {
                        st = llmk_repl_cfg_set_kv_best_effort("top_k", cfg_top_k);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_repeat) {
                        st = llmk_repl_cfg_set_kv_best_effort("repeat_penalty", cfg_repeat);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_norepeat) {
                        st = llmk_repl_cfg_set_kv_best_effort("no_repeat_ngram", cfg_norepeat);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;

                    if (ok) {
                        Print(L" (saved)");
                    } else {
                        Print(L" (save failed)");
                    }
                }

                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/autostart_engines_on", 20) == 0) {
                // Usage: /autostart_engines_on [observe|enforce] [--run]
                const char *p = prompt + 20;
                while (*p == ' ' || *p == '\t') p++;

                int mode = 1; // observe
                int run_now = 0;

                // Parse tokens in any order: observe|enforce|1|2 and --run
                while (*p) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == 0) break;

                    char tok[24]; // SAFE: short option token parsed with bounds check
                    int tp = 0;
                    while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) tok[tp++] = *p++;
                    tok[tp] = 0;

                    if (llmk_cfg_streq_ci(tok, "enforce") || llmk_cfg_streq_ci(tok, "2")) {
                        mode = 2;
                    } else if (llmk_cfg_streq_ci(tok, "observe") || llmk_cfg_streq_ci(tok, "1")) {
                        mode = 1;
                    } else if (llmk_cfg_streq_ci(tok, "--run")) {
                        run_now = 1;
                    } else if (llmk_cfg_streq_ci(tok, "--help") || llmk_cfg_streq_ci(tok, "-h")) {
                        Print(L"\r\nUsage:\r\n");
                        Print(L"  /autostart_engines_on observe [--run]\r\n");
                        Print(L"  /autostart_engines_on enforce [--run]\r\n\r\n");
                        continue;
                    }
                }

                const char *mode_name = (mode == 2) ? "enforce" : "observe";

                // Generate llmk-autorun.txt in the boot volume root.
                // Keep it ASCII + CRLF, allow comments.
                char script[1024];
                int sp = 0;
                script[0] = 0;
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "# llmk-autorun.txt (generated by /autostart_engines_on)\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "# Mode: ");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), mode_name);
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "\r\n\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/version\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/compat_on\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/compat_probe\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/compat_status\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/djibion_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/djibion_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/djibion_enforce 1\r\n");
                }

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/mem_on\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diag_on\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_enforce 1\r\n");
                }
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_status\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_enforce 1\r\n");
                }
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_status\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_apply\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_enforce 1\r\n");
                }
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_status\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/preset stable\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/sampling\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/ctx\r\n");

                // Djibion gate for file write (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_WRITE, "llmk-autorun.txt", (UINT32)my_strlen(script), &d);
                    djibion_log_if_observe(&g_djibion, "fs_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (autorun script write): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                {
                    CHAR16 path[64];
                    StrCpy(path, L"llmk-autorun.txt");
                    EFI_FILE_HANDLE f = NULL;
                    EFI_STATUS st = llmk_open_binary_file(&f, path);
                    if (EFI_ERROR(st) || !f) {
                        Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                        continue;
                    }
                    UINTN nbytes = (UINTN)my_strlen(script);
                    EFI_STATUS wst = llmk_file_write_bytes(f, script, nbytes);
                    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                    uefi_call_wrapper(f->Close, 1, f);
                    if (EFI_ERROR(wst)) {
                        Print(L"\r\nERROR: write failed: %r\r\n\r\n", wst);
                        continue;
                    }
                    if (EFI_ERROR(flush_st)) {
                        Print(L"\r\nWARNING: flush failed: %r (file may not persist)\r\n\r\n", flush_st);
                    }
                }

                // Djibion gate for cfg write (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "autorun_autostart", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (repl.cfg update): %s\r\n\r\n", msg);
                        Print(L"OK: wrote llmk-autorun.txt; enable autorun manually in repl.cfg\r\n\r\n");
                        continue;
                    }
                }

                // Persist autorun settings.
                {
                    EFI_STATUS st;
                    st = llmk_repl_cfg_set_kv_best_effort("autorun_autostart", "1");
                    if (EFI_ERROR(st)) {
                        Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                        continue;
                    }
                    llmk_repl_cfg_set_kv_best_effort("autorun_shutdown_when_done", "0");
                    llmk_repl_cfg_set_kv_best_effort("autorun_file", "llmk-autorun.txt");

                    // Ensure Djibion allows autorun when enabled via repl.cfg.
                    llmk_repl_cfg_set_kv_best_effort("djibion_allow_autorun", "1");
                    llmk_repl_cfg_set_kv_best_effort("djibion_mode", (mode == 2) ? "2" : "1");
                }

                Print(L"\r\nOK: engines autostart enabled (mode=");
                llmk_print_ascii(mode_name);
                Print(L"). Reboot to apply.\r\n");

                if (run_now) {
                    Print(L"[autostart] launching autorun now...\r\n\r\n");
                    int ok = llmk_autorun_start(L"llmk-autorun.txt", 0);
                    if (!ok) {
                        Print(L"\r\nERROR: autorun start failed\r\n\r\n");
                    }
                } else {
                    Print(L"\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/autostart_engines_off", 21) == 0) {
                // Disable autorun_autostart.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "autorun_autostart", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/autostart_engines_off): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("autorun_autostart", "0");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                    continue;
                }
                Print(L"\r\nOK: autorun_autostart=0 (reboot to apply)\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/model", 6) == 0 && (prompt[6] == 0 || prompt[6] == ' ')) {
                Print(L"\r\nModel:\r\n");
                Print(L"  %s\r\n", model_filename ? model_filename : L"(none)");
                Print(L"Config:\r\n");
                Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n\r\n",
                      config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                continue;
            } else if (my_strncmp(prompt, "/model_info", 11) == 0) {
                // Usage:
                //   /model_info           -> info for current loaded model path
                //   /model_info <path>    -> info for a specific file
                CHAR16 path16[192];
                path16[0] = 0;

                // Parse optional path argument from ASCII prompt.
                int i = 11;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[160];
                    int n = 0;
                    while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(p8)) {
                        p8[n++] = prompt[i++];
                    }
                    p8[n] = 0;
                    ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
                } else {
                    // Default to last loaded model path (if known)
                    if (g_loaded_model_path16[0]) {
                        llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), g_loaded_model_path16);
                    } else if (model_filename) {
                        llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), model_filename);
                    } else {
                        StrCpy(path16, L"model.bin");
                    }
                }

                if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF &&
                    g_loaded_model_gguf_valid &&
                    llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                    llmk_print_gguf_summary_block(path16, &g_loaded_model_gguf);
                    Print(L"\r\nNOTE: GGUF inference is not wired yet; use .bin for generation today.\r\n\r\n");
                    continue;
                }

                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[192];
                picked[0] = 0;
                if (!llmk_try_open_model_spec_best_effort(Root, path16, &f, picked,
                                                          (int)(sizeof(picked) / sizeof(picked[0])))) {
                    Print(L"\r\nERROR: open failed: %s\r\n", path16);
                    Print(L"Hint: try /models, then /model_info <name>, <name>.bin, or models\\<name>.gguf\r\n\r\n");
                    continue;
                }

                // If we opened via an 8.3 alias, reflect it in the printed file path.
                if (picked[0]) {
                    llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), picked);
                }

                LlmkModelFormat fmt = llmk_detect_model_format(f);
                if (fmt == LLMK_MODEL_FMT_GGUF) {
                    GgufSummary s;
                    EFI_STATUS gst = gguf_read_summary(f, &s);
                    uefi_call_wrapper(f->Close, 1, f);
                    if (EFI_ERROR(gst)) {
                        Print(L"\r\nGGUF: failed to parse (%r)\r\n\r\n", gst);
                        continue;
                    }

                    llmk_print_gguf_summary_block(path16, &s);
                    if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF && llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                        g_loaded_model_gguf = s;
                        g_loaded_model_gguf_valid = 1;
                    }
                    Print(L"\r\nNOTE: GGUF inference is not wired yet; use .bin for generation today.\r\n\r\n");
                    continue;
                }

                // Default: treat as llama2.c .bin header (7 ints)
                Config c;
                EFI_STATUS pst = uefi_call_wrapper(f->SetPosition, 2, f, 0);
                if (EFI_ERROR(pst)) {
                    uefi_call_wrapper(f->Close, 1, f);
                    Print(L"\r\nERROR: seek failed (%r)\r\n\r\n", pst);
                    continue;
                }
                UINTN bytes = 7 * sizeof(int);
                EFI_STATUS rst = uefi_call_wrapper(f->Read, 3, f, &bytes, &c);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(rst) || bytes != 7 * sizeof(int)) {
                    Print(L"\r\nBIN: failed to read header (%r)\r\n\r\n", rst);
                    continue;
                }

                int shared = (c.vocab_size < 0);
                if (c.vocab_size < 0) c.vocab_size = -c.vocab_size;
                Print(L"\r\nBIN model info:\r\n");
                Print(L"  file=%s\r\n", path16);
                Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d shared_cls=%d\r\n\r\n",
                      c.dim, c.n_layers, c.n_heads, c.n_kv_heads, c.vocab_size, c.seq_len, shared);
                continue;
            } else if (my_strncmp(prompt, "/models", 7) == 0) {
                // Usage:
                //   /models            -> list root + models\\
                //   /models <dir>      -> list .bin/.gguf in a directory (e.g. models, \\models)
                CHAR16 path[128];
                path[0] = 0;

                int i = 7;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[96];
                    int n = 0;
                    while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(p8)) {
                        p8[n++] = prompt[i++];
                    }
                    p8[n] = 0;
                    ascii_to_char16(path, p8, (int)(sizeof(path) / sizeof(path[0])));
                }

                Print(L"\r\nModels (.bin/.gguf):\r\n");
                if (path[0]) {
                    Print(L"Dir: %s\r\n", path);
                    llmk_models_ls_best_effort(path, 200);
                    Print(L"\r\n");
                } else {
                    Print(L"Root:\r\n");
                    llmk_models_ls_best_effort(NULL, 200);
                    Print(L"\r\nmodels\\:\r\n");
                    llmk_models_ls_best_effort(L"models", 200);
                    Print(L"\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_handoff_info", 16) == 0) {
                CHAR16 path16[256];
                path16[0] = 0;
                int i = 16;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[192];
                    int n = 0;
                    while (prompt[i] && n + 1 < (int)sizeof(p8)) p8[n++] = prompt[i++];
                    p8[n] = 0;
                    ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
                } else {
                    StrCpy(path16, L"sovereign_export.json");
                }
                llmk_oo_handoff_info_best_effort(path16);
                continue;
            } else if (my_strncmp(prompt, "/oo_handoff_apply", 17) == 0) {
                CHAR16 path16[256];
                path16[0] = 0;
                int i = 17;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[192];
                    int n = 0;
                    while (prompt[i] && n + 1 < (int)sizeof(p8)) p8[n++] = prompt[i++];
                    p8[n] = 0;
                    ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
                } else {
                    StrCpy(path16, L"sovereign_export.json");
                }
                llmk_oo_handoff_apply_best_effort(path16);
                continue;
            } else if (my_strncmp(prompt, "/oo_handoff_receipt", 19) == 0) {
                CHAR16 path16[256];
                path16[0] = 0;
                int i = 19;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[192];
                    int n = 0;
                    while (prompt[i] && n + 1 < (int)sizeof(p8)) p8[n++] = prompt[i++];
                    p8[n] = 0;
                    ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
                } else {
                    StrCpy(path16, L"OOHANDOFF.TXT");
                }
                llmk_oo_handoff_receipt_best_effort(path16);
                continue;
            } else if (my_strncmp(prompt, "/oo_continuity_status", 21) == 0) {
                CHAR16 path16[256];
                path16[0] = 0;
                int i = 21;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[192];
                    int n = 0;
                    while (prompt[i] && n + 1 < (int)sizeof(p8)) p8[n++] = prompt[i++];
                    p8[n] = 0;
                    ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
                } else {
                    StrCpy(path16, L"OOHANDOFF.TXT");
                }
                llmk_oo_continuity_status_best_effort(path16);
                continue;
            } else if (my_strncmp(prompt, "/oo_reboot_probe", 16) == 0) {
                llmk_oo_reboot_probe_best_effort();
                continue;
            } else if (my_strncmp(prompt, "/cpu", 4) == 0) {
                CPUFeatures f;
                djiblas_detect_cpu(&f);
                sgemm_kernel_t k = djiblas_get_best_kernel(&f);
                const CHAR16 *name = L"SCALAR";
                if (k == djiblas_sgemm_avx512) name = L"AVX512";
                else if (k == djiblas_sgemm_avx2) name = (f.has_fma ? L"AVX2+FMA" : L"AVX2");
                else if (k == djiblas_sgemm_sse2) name = L"SSE2";
                Print(L"\r\nCPU features:\r\n");
                Print(L"  sse2=%d avx=%d avx2=%d fma=%d\r\n", (int)f.has_sse2, (int)f.has_avx, (int)f.has_avx2, (int)f.has_fma);
                Print(L"  djiblas_sgemm=%s\r\n", name);
                const CHAR16 *attn = g_attn_use_avx2 ? L"AVX2" : L"SSE2";
                if (g_attn_force == 0) attn = L"SSE2 (forced)";
                else if (g_attn_force == 1) attn = L"AVX2 (forced)";
                Print(L"  attn_simd=%s\r\n\r\n", attn);
                continue;
            } else if (my_strncmp(prompt, "/zones", 6) == 0) {
                Print(L"\r\nZones:\r\n");
                if (g_llmk_ready) {
                    llmk_zones_print(&g_zones);
                    llmk_sentinel_print_status(&g_sentinel);
                    Print(L"\r\n");
                } else {
                    Print(L"  (llmk not ready)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/ram", 4) == 0) {
                llmk_print_ram_budget();
                continue;
            } else if (my_strncmp(prompt, "/budget", 7) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }

                // Usage:
                //   /budget                 -> show
                //   /budget <p>             -> set both prefill+decode
                //   /budget <p> <d>         -> set separately
                int i = 7;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    Print(L"\r\nBudgets (cycles):\r\n");
                    Print(L"  prefill_max=%lu\r\n", g_budget_prefill_cycles);
                    Print(L"  decode_max=%lu\r\n\r\n", g_budget_decode_cycles);
                    continue;
                }

                UINT64 pre = 0;
                UINT64 dec = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    pre = pre * 10ULL + (UINT64)(prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    dec = pre;
                } else {
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        dec = dec * 10ULL + (UINT64)(prompt[i] - '0');
                        i++;
                    }
                }

                g_budget_prefill_cycles = pre;
                g_budget_decode_cycles = dec;
                Print(L"\r\nBudgets set (cycles):\r\n");
                Print(L"  prefill_max=%lu\r\n", g_budget_prefill_cycles);
                Print(L"  decode_max=%lu\r\n\r\n", g_budget_decode_cycles);
                continue;
            } else if (my_strncmp(prompt, "/attn", 5) == 0) {
                // Usage:
                //   /attn          -> show
                //   /attn auto     -> runtime default
                //   /attn sse2     -> force SSE2 path
                //   /attn avx2     -> force AVX2 path (only if auto AVX2 is enabled)
                int i = 5;
                while (prompt[i] == ' ') i++;

                if (prompt[i] == 0) {
                    Print(L"\r\nAttention SIMD:\r\n");
                    Print(L"  auto=%s\r\n", g_attn_use_avx2 ? L"AVX2" : L"SSE2");
                    Print(L"  mode=%s\r\n\r\n",
                          (g_attn_force == -1) ? L"auto" : (g_attn_force == 0 ? L"sse2 (forced)" : L"avx2 (forced)"));
                    continue;
                }

                if (prompt[i] == 'a') {
                    g_attn_force = -1;
                    Print(L"\r\nOK: attn mode=auto\r\n\r\n");
                    continue;
                }
                if (prompt[i] == 's') {
                    g_attn_force = 0;
                    Print(L"\r\nOK: attn mode=sse2 (forced)\r\n\r\n");
                    continue;
                }
                if (prompt[i] == 'v') {
                    if (!g_attn_use_avx2) {
                        Print(L"\r\nERROR: AVX2 attention not available (auto is SSE2)\r\n\r\n");
                        continue;
                    }
                    g_attn_force = 1;
                    Print(L"\r\nOK: attn mode=avx2 (forced)\r\n\r\n");
                    continue;
                }

                Print(L"\r\nUsage: /attn [auto|sse2|avx2]\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/test_failsafe", 14) == 0) {
                // One-shot: temporarily enable strict budget and set tiny budgets so the next prompt trips.
                // Usage:
                //   /test_failsafe                -> decode trip with default cycles
                //   /test_failsafe prefill [c]    -> trip during prefill
                //   /test_failsafe decode [c]     -> trip during decode
                //   /test_failsafe both [c]       -> either phase can trip
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }

                UINT64 cycles = 10000ULL;
                int mode = 2; // 1=prefill, 2=decode, 3=both

                int i = 14;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 'p') mode = 1;
                else if (prompt[i] == 'd') mode = 2;
                else if (prompt[i] == 'b') mode = 3;

                // Skip word
                while (prompt[i] && prompt[i] != ' ') i++;
                while (prompt[i] == ' ') i++;

                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    cycles = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        cycles = cycles * 10ULL + (UINT64)(prompt[i] - '0');
                        i++;
                    }
                }
                if (cycles < 100ULL) cycles = 100ULL;

                if (!g_test_failsafe_active) {
                    g_test_failsafe_prev_strict_budget = g_sentinel.cfg.strict_budget;
                    g_test_failsafe_prev_prefill = g_budget_prefill_cycles;
                    g_test_failsafe_prev_decode = g_budget_decode_cycles;
                }
                g_test_failsafe_active = 1;
                g_sentinel.cfg.strict_budget = TRUE;

                const UINT64 huge = 100000000000ULL;
                if (mode == 1) {
                    g_budget_prefill_cycles = cycles;
                    g_budget_decode_cycles = huge;
                } else if (mode == 2) {
                    g_budget_prefill_cycles = huge;
                    g_budget_decode_cycles = cycles;
                } else {
                    g_budget_prefill_cycles = cycles;
                    g_budget_decode_cycles = cycles;
                }

                Print(L"\r\n[test] fail-safe armed (strict_budget=1)\r\n");
                Print(L"  prefill_max=%lu decode_max=%lu\r\n", g_budget_prefill_cycles, g_budget_decode_cycles);
                Print(L"  Next prompt should trip and auto-dump ctx/zones/sentinel/log.\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/cfg", 4) == 0) {
                llmk_print_cfg(&config, model_filename, &weights,
                               kv_pos,
                               temperature, min_p, top_p, top_k,
                               no_repeat_ngram, repeat_penalty, max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/ctx", 4) == 0) {
                llmk_print_ctx(&config, model_filename, kv_pos, temperature, min_p, top_p, top_k, no_repeat_ngram, repeat_penalty, max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/log", 4) == 0) {
                UINT32 n = 16;
                if (prompt[4] == ' ') {
                    int i = 5;
                    UINT32 val = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val = val * 10u + (UINT32)(prompt[i] - '0');
                        i++;
                    }
                    if (val > 0) n = val;
                    if (n > 128) n = 128;
                }
                llmk_print_log(n);
                continue;
            } else if (my_strncmp(prompt, "/save_log", 9) == 0) {
                if (!g_llmk_ready || !g_llmk_log.capacity) {
                    Print(L"\r\n  (log not available)\r\n\r\n");
                    continue;
                }

                UINT32 n = 64;
                if (prompt[9] == ' ') {
                    int i = 10;
                    UINT32 val = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val = val * 10u + (UINT32)(prompt[i] - '0');
                        i++;
                    }
                    if (val > 0) n = val;
                    if (n > 128) n = 128;
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, L"llmk-log.txt");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to open llmk-log.txt: %r\r\n\r\n", st);
                    continue;
                }
                llmk_dump_log_to_file(f, &g_llmk_log, n);
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote llmk-log.txt (flushed)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/save_dump", 10) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, L"llmk-dump.txt");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to open llmk-dump.txt: %r\r\n\r\n", st);
                    continue;
                }

                // Minimal ctx dump (match /ctx, in UTF-16)
                {
                    CHAR16 line[256];
                    llmk_file_write_u16(f, L"Context:\r\n");
                          SPrint(line, sizeof(line), L"  model=%s\r\n", model_filename ? model_filename : L"(unknown)");
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
                           config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                    llmk_file_write_u16(f, line);
                          SPrint(line, sizeof(line), L"  kv_pos=%d\r\n", kv_pos);
                          llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"Sampling:\r\n");
                    SPrint(line, sizeof(line), L"  temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
                           (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
                           (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
                           (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
                           top_k);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  norepeat=%d repeat_penalty=%d.%02d max_tokens=%d\r\n",
                           no_repeat_ngram,
                           (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
                           max_gen_tokens);
                    llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"Budgets:\r\n");
                    SPrint(line, sizeof(line), L"  prefill_max=%lu decode_max=%lu overruns(p=%d d=%d)\r\n\r\n",
                           g_budget_prefill_cycles, g_budget_decode_cycles,
                           (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
                    llmk_file_write_u16(f, line);
                }

                llmk_dump_zones_to_file(f, &g_zones);
                llmk_dump_sentinel_to_file(f, &g_sentinel);
                if (g_llmk_log.capacity) {
                    llmk_dump_log_to_file(f, &g_llmk_log, 128);
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote llmk-dump.txt (flushed)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/diag_on", 8) == 0) {
                diagnostion_set_mode(&g_diagnostion, DIAGNOSTION_MODE_ON);
                Print(L"\r\nOK: diagnostion=on\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diag_off", 9) == 0) {
                diagnostion_set_mode(&g_diagnostion, DIAGNOSTION_MODE_OFF);
                Print(L"\r\nOK: diagnostion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diag_status", 11) == 0) {
                Print(L"\r\n[Diagnostion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(diagnostion_mode_name_ascii(g_diagnostion.mode));
                Print(L"\r\n");
                Print(L"  reports_written=%d\r\n\r\n", (int)g_diagnostion.reports_written);
                continue;
            } else if (my_strncmp(prompt, "/diag_report", 11) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (g_diagnostion.mode == DIAGNOSTION_MODE_OFF) {
                    Print(L"\r\nERROR: Diagnostion is off (use /diag_on)\r\n\r\n");
                    continue;
                }

                // Optional: /diag_report <file>
                char out_name8[96];
                out_name8[0] = 0;
                {
                    const char *p = prompt + 11;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p) {
                        int n = 0;
                        while (*p && *p != ' ' && *p != '\t' && n + 1 < (int)sizeof(out_name8)) {
                            out_name8[n++] = *p++;
                        }
                        out_name8[n] = 0;
                    }
                }

                CHAR16 out_name16[96];
                if (out_name8[0]) {
                    ascii_to_char16(out_name16, out_name8, (int)(sizeof(out_name16) / sizeof(out_name16[0])));
                } else {
                    StrCpy(out_name16, L"llmk-diag.txt");
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, out_name16);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to open diag file: %r\r\n\r\n", st);
                    continue;
                }

                // Human-friendly report header (UTF-16)
                {
                    CHAR16 line[256];
                    UINT64 total_mem = llmk_get_conventional_ram_bytes_best_effort();
                    CPUFeatures cpu_features;
                    djiblas_detect_cpu(&cpu_features);
                    sgemm_kernel_t k = djiblas_get_best_kernel(&cpu_features);
                    const CHAR16 *kernel_name = L"SCALAR";
                    if (k == djiblas_sgemm_avx512) kernel_name = L"AVX512";
                    else if (k == djiblas_sgemm_avx2) kernel_name = (cpu_features.has_fma ? L"AVX2+FMA" : L"AVX2");
                    else if (k == djiblas_sgemm_sse2) kernel_name = L"SSE2";

                    llmk_file_write_u16(f, L"LLMK DIAGNOSTIC REPORT\r\n\r\n");
                    llmk_file_write_u16(f, L"System:\r\n");
                    SPrint(line, sizeof(line), L"  build=%s\r\n", LLMB_BUILD_ID);
                    llmk_file_write_u16(f, line);
                    if (g_gop && g_gop_fb32) {
                        SPrint(line, sizeof(line), L"  gop=%dx%d ppsl=%d fb=0x%lx\r\n",
                               (int)g_gop_w, (int)g_gop_h, (int)g_gop_ppsl, (UINT64)(UINTN)g_gop_fb32);
                    } else {
                        SPrint(line, sizeof(line), L"  gop=(not available)\r\n");
                    }
                    llmk_file_write_u16(f, line);
                    if (total_mem > 0) {
                        SPrint(line, sizeof(line), L"  ram_mib=%lu\r\n", total_mem / (1024ULL * 1024ULL));
                    } else {
                        SPrint(line, sizeof(line), L"  ram_mib=(unknown)\r\n");
                    }
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  cpu: sse2=%d avx=%d avx2=%d fma=%d kernel=%s attn=%s\r\n\r\n",
                           (int)cpu_features.has_sse2,
                           (int)cpu_features.has_avx,
                           (int)cpu_features.has_avx2,
                           (int)cpu_features.has_fma,
                           kernel_name,
                           g_attn_use_avx2 ? L"AVX2" : L"SSE2");
                    llmk_file_write_u16(f, line);

                    llmk_diag_write_models_inventory_to_file(f, NULL, L"Models root", 64);
                    llmk_diag_write_models_inventory_to_file(f, L"models", L"Models models\\", 64);

                    SPrint(line, sizeof(line), L"  model=%s\r\n", model_filename ? model_filename : L"(unknown)");
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
                           config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  kv_pos=%d\r\n", kv_pos);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  budgets: prefill_max=%lu decode_max=%lu overruns(p=%d d=%d)\r\n",
                           g_budget_prefill_cycles, g_budget_decode_cycles,
                           (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
                    llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"\r\nEngines:\r\n");
                    SPrint(line, sizeof(line), L"  djibion_mode=%s decisions=%d rejected=%d transformed=%d\r\n",
                           (CHAR16 *)djibion_mode_name(g_djibion.mode),
                           (int)g_djibion.decisions_total,
                           (int)g_djibion.decisions_rejected,
                           (int)g_djibion.decisions_transformed);
                    llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"  diopion_mode=");
                    llmk_file_write_u16(f, L"\"");
                    {
                        // diopion_mode_name_ascii returns ASCII; print it char-by-char into UTF-16 file.
                        CHAR16 m[32]; // SAFE: small fixed string buffer for mode name
                        ascii_to_char16(m, diopion_mode_name_ascii(g_diopion.mode), (int)(sizeof(m) / sizeof(m[0])));
                        llmk_file_write_u16(f, m);
                    }
                    llmk_file_write_u16(f, L"\" profile=\"");
                    {
                        CHAR16 p[32]; // SAFE: small fixed string buffer for profile name
                        ascii_to_char16(p, diopion_profile_name_ascii(g_diopion.profile), (int)(sizeof(p) / sizeof(p[0])));
                        llmk_file_write_u16(f, p);
                    }
                    llmk_file_write_u16(f, L"\"\r\n\r\n");

                    llmk_file_write_u16(f, L"Sampling:\r\n");
                    SPrint(line, sizeof(line), L"  temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
                           (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
                           (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
                           (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
                           top_k);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  norepeat=%d repeat_penalty=%d.%02d max_tokens=%d\r\n\r\n",
                           no_repeat_ngram,
                           (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
                           max_gen_tokens);
                    llmk_file_write_u16(f, line);
                }

                // Deep dumps (same building blocks as /save_dump)
                llmk_dump_zones_to_file(f, &g_zones);
                llmk_dump_sentinel_to_file(f, &g_sentinel);
                if (g_llmk_log.capacity) {
                    llmk_dump_log_to_file(f, &g_llmk_log, 128);
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    g_diagnostion.reports_written++;
                    Print(L"\r\nOK: wrote %s (flushed)\r\n\r\n", out_name16);
                }
                continue;
            } else if (my_strncmp(prompt, "/mem_on", 7) == 0) {
                memorion_set_mode(&g_memorion, MEMORION_MODE_ON);
                Print(L"\r\nOK: memorion=on\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/mem_off", 8) == 0) {
                memorion_set_mode(&g_memorion, MEMORION_MODE_OFF);
                Print(L"\r\nOK: memorion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/mem_status", 10) == 0) {
                Print(L"\r\n[Memorion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(memorion_mode_name_ascii(g_memorion.mode));
                Print(L"\r\n");
                Print(L"  manifests_written=%d\r\n", (int)g_memorion.manifests_written);
                Print(L"  checks_done=%d\r\n\r\n", (int)g_memorion.checks_done);
                continue;
            } else if (my_strncmp(prompt, "/mem_snap_info", 14) == 0 || my_strncmp(prompt, "/mem_snap_check", 15) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (g_memorion.mode == MEMORION_MODE_OFF) {
                    Print(L"\r\nERROR: Memorion is off (use /mem_on)\r\n\r\n");
                    continue;
                }

                int is_check = (my_strncmp(prompt, "/mem_snap_check", 15) == 0);
                int cmd_len = is_check ? 15 : 14;
                const char *p = prompt + cmd_len;
                while (*p == ' ' || *p == '\t') p++;

                char snap8[96];
                snap8[0] = 0;
                if (*p) {
                    int n = 0;
                    while (*p && *p != ' ' && *p != '\t' && n + 1 < (int)sizeof(snap8)) {
                        snap8[n++] = *p++;
                    }
                    snap8[n] = 0;
                }
                if (snap8[0] == 0) {
                    llmk_ascii_copy_cap(snap8, (int)sizeof(snap8), "llmk-snap.bin");
                }

                if (llmk_ascii_has_dotdot(snap8)) {
                    Print(L"\r\nERROR: path contains '..'\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort): treat as a snapshot load-like read.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, snap8, 0, &d);
                    djibion_log_if_observe(&g_djibion, is_check ? "mem_snap_check" : "mem_snap_info", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (%s): %s\r\n\r\n", is_check ? L"/mem_snap_check" : L"/mem_snap_info", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] snap path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        llmk_ascii_copy_cap(snap8, (int)sizeof(snap8), d.transformed_arg0);
                    }
                }

                CHAR16 snap16[96];
                ascii_to_char16(snap16, snap8, (int)(sizeof(snap16) / sizeof(snap16[0])));

                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[96];
                picked[0] = 0;
                EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, snap16, &f, picked,
                                                                  (int)(sizeof(picked) / sizeof(picked[0])),
                                                                  is_check ? L"mem_snap_check" : L"mem_snap_info");
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }

                if (picked[0]) {
                    llmk_char16_copy_cap(snap16, (int)(sizeof(snap16) / sizeof(snap16[0])), picked);
                }

                LlmkSnapHeader hdr;
                st = read_exact(f, &hdr, sizeof(hdr));
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: read failed: %r\r\n\r\n", st);
                    continue;
                }
                if (hdr.magic != LLMK_SNAP_MAGIC || hdr.version != 1) {
                    Print(L"\r\nERROR: invalid snapshot header (magic/version)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[Snapshot]\r\n");
                Print(L"  file=%s\r\n", snap16);
                Print(L"  dim=%d layers=%d heads=%d kv=%d seq=%d\r\n", (int)hdr.dim, (int)hdr.n_layers, (int)hdr.n_heads, (int)hdr.n_kv_heads, (int)hdr.seq_len);
                Print(L"  kv_dim=%d kv_pos=%d\r\n", (int)hdr.kv_dim, (int)hdr.kv_pos);
                {
                    UINTN slice_bytes = (UINTN)hdr.kv_pos * (UINTN)hdr.kv_dim * sizeof(float);
                    UINTN total = sizeof(LlmkSnapHeader) + (UINTN)hdr.n_layers * 2u * slice_bytes;
                    Print(L"  approx_bytes=%lu\r\n", (UINT64)total);
                }

                if (is_check) {
                    int ok = 1;
                    if (hdr.dim != (UINT32)config.dim) ok = 0;
                    if (hdr.n_layers != (UINT32)config.n_layers) ok = 0;
                    if (hdr.n_heads != (UINT32)config.n_heads) ok = 0;
                    if (hdr.n_kv_heads != (UINT32)config.n_kv_heads) ok = 0;
                    if (hdr.seq_len != (UINT32)config.seq_len) ok = 0;
                    if (hdr.kv_pos == 0 || hdr.kv_pos > (UINT32)config.seq_len) ok = 0;
                    Print(L"  compatible=%s\r\n\r\n", ok ? L"yes" : L"NO");
                    g_memorion.checks_done++;
                } else {
                    Print(L"\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/mem_manifest", 13) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (g_memorion.mode == MEMORION_MODE_OFF) {
                    Print(L"\r\nERROR: Memorion is off (use /mem_on)\r\n\r\n");
                    continue;
                }

                // Usage:
                //   /mem_manifest                 -> write current context manifest to llmk-manifest.txt
                //   /mem_manifest <snap>          -> include snapshot header, write llmk-manifest.txt
                //   /mem_manifest <snap> <out>    -> include snapshot header, write <out>
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;

                char snap8[96];
                char out8[96];
                snap8[0] = 0;
                out8[0] = 0;

                if (*p) {
                    int n = 0;
                    while (*p && *p != ' ' && *p != '\t' && n + 1 < (int)sizeof(snap8)) snap8[n++] = *p++;
                    snap8[n] = 0;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p) {
                        int m = 0;
                        while (*p && *p != ' ' && *p != '\t' && m + 1 < (int)sizeof(out8)) out8[m++] = *p++;
                        out8[m] = 0;
                    }
                }

                if (snap8[0] && llmk_ascii_has_dotdot(snap8)) {
                    Print(L"\r\nERROR: snap path contains '..'\r\n\r\n");
                    continue;
                }
                if (out8[0] && llmk_ascii_has_dotdot(out8)) {
                    Print(L"\r\nERROR: out path contains '..'\r\n\r\n");
                    continue;
                }

                CHAR16 out16[96];
                if (out8[0]) {
                    ascii_to_char16(out16, out8, (int)(sizeof(out16) / sizeof(out16[0])));
                } else {
                    StrCpy(out16, L"llmk-manifest.txt");
                }

                // Djibion gate (best-effort): writing a file.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char out_file8[128];
                    llmk_char16_to_ascii_cap(out_file8, (int)sizeof(out_file8), out16);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_WRITE, out_file8, 4096u, &d);
                    djibion_log_if_observe(&g_djibion, "mem_manifest", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/mem_manifest): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] manifest path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(out16, d.transformed_arg0, (int)(sizeof(out16) / sizeof(out16[0])));
                    }
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, out16);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }

                LlmkSnapHeader hdr;
                int have_hdr = 0;
                int compat = 0;
                if (snap8[0]) {
                    // Djibion gate (best-effort): read snapshot.
                    if (g_djibion.mode != DJIBION_MODE_OFF) {
                        DjibionDecision d;
                        djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, snap8, 0, &d);
                        djibion_log_if_observe(&g_djibion, "mem_manifest_snap", &d);
                        if (djibion_should_block(&g_djibion, &d)) {
                            CHAR16 msg[160];
                            ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                            Print(L"\r\nDJIBION: blocked (snap read): %s\r\n\r\n", msg);
                            uefi_call_wrapper(f->Close, 1, f);
                            continue;
                        }
                        if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                            llmk_ascii_copy_cap(snap8, (int)sizeof(snap8), d.transformed_arg0);
                        }
                    }

                    CHAR16 snap16[96];
                    ascii_to_char16(snap16, snap8, (int)(sizeof(snap16) / sizeof(snap16[0])));
                    EFI_FILE_HANDLE rf = NULL;
                    st = llmk_open_read_file(&rf, snap16);
                    if (!EFI_ERROR(st) && rf) {
                        EFI_STATUS st2 = read_exact(rf, &hdr, sizeof(hdr));
                        uefi_call_wrapper(rf->Close, 1, rf);
                        if (!EFI_ERROR(st2) && hdr.magic == LLMK_SNAP_MAGIC && hdr.version == 1) {
                            have_hdr = 1;
                            compat = 1;
                            if (hdr.dim != (UINT32)config.dim) compat = 0;
                            if (hdr.n_layers != (UINT32)config.n_layers) compat = 0;
                            if (hdr.n_heads != (UINT32)config.n_heads) compat = 0;
                            if (hdr.n_kv_heads != (UINT32)config.n_kv_heads) compat = 0;
                            if (hdr.seq_len != (UINT32)config.seq_len) compat = 0;
                            if (hdr.kv_pos == 0 || hdr.kv_pos > (UINT32)config.seq_len) compat = 0;
                        }
                    }
                }

                {
                    CHAR16 line[256];
                    UINT32 h = llmk_memorion_ctx_hash32(&config, model_filename);
                    llmk_file_write_u16(f, L"LLMK MEMORION MANIFEST\r\n\r\n");
                    SPrint(line, sizeof(line), L"  model=%s\r\n", model_filename ? model_filename : L"(unknown)");
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
                           config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  kv_pos=%d\r\n", kv_pos);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  ctx_hash32=0x%08x\r\n\r\n", h);
                    llmk_file_write_u16(f, line);

                    if (snap8[0]) {
                        llmk_file_write_u16(f, L"Snapshot:\r\n");
                        {
                            CHAR16 snap16[96];
                            ascii_to_char16(snap16, snap8, (int)(sizeof(snap16) / sizeof(snap16[0])));
                            SPrint(line, sizeof(line), L"  file=%s\r\n", snap16);
                            llmk_file_write_u16(f, line);
                        }
                        if (have_hdr) {
                            SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d seq=%d\r\n",
                                   (int)hdr.dim, (int)hdr.n_layers, (int)hdr.n_heads, (int)hdr.n_kv_heads, (int)hdr.seq_len);
                            llmk_file_write_u16(f, line);
                            SPrint(line, sizeof(line), L"  kv_dim=%d kv_pos=%d\r\n",
                                   (int)hdr.kv_dim, (int)hdr.kv_pos);
                            llmk_file_write_u16(f, line);
                            SPrint(line, sizeof(line), L"  compatible=%s\r\n\r\n", compat ? L"yes" : L"NO");
                            llmk_file_write_u16(f, line);
                        } else {
                            llmk_file_write_u16(f, L"  (could not read valid header)\r\n\r\n");
                        }
                    }
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    g_memorion.manifests_written++;
                    Print(L"\r\nOK: wrote %s (flushed)\r\n\r\n", out16);
                }
                continue;

            // ==============================================================
            // ORCHESTRION commands
            // ==============================================================
            } else if (my_strncmp(prompt, "/orch_on", 8) == 0) {
                orchestrion_set_mode(&g_orchestrion, ORCHESTRION_MODE_OBSERVE);
                Print(L"\r\nOK: orchestrion=observe\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_off", 9) == 0) {
                orchestrion_set_mode(&g_orchestrion, ORCHESTRION_MODE_OFF);
                Print(L"\r\nOK: orchestrion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_enforce", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ') p++;
                int v = 2;
                if (*p >= '0' && *p <= '2') v = *p - '0';
                orchestrion_set_mode(&g_orchestrion, (OrchestrionMode)v);
                Print(L"\r\nOK: orchestrion_mode=%d\r\n\r\n", v);
                continue;
            } else if (my_strncmp(prompt, "/orch_status", 12) == 0) {
                Print(L"\r\n[Orchestrion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(orchestrion_mode_name_ascii(g_orchestrion.mode));
                Print(L"\r\n");
                Print(L"  state=");
                llmk_print_ascii(orchestrion_state_name_ascii(g_orchestrion.pipeline.state));
                Print(L"\r\n");
                Print(L"  steps=%d current=%d loops=%d/%d\r\n",
                      (int)g_orchestrion.pipeline.step_count,
                      (int)g_orchestrion.pipeline.current_step,
                      (int)g_orchestrion.pipeline.loops_done,
                      (int)g_orchestrion.pipeline.loops_max);
                Print(L"  workflows_run=%d steps_executed=%d errors=%d\r\n\r\n",
                      (int)g_orchestrion.workflows_run,
                      (int)g_orchestrion.steps_executed,
                      (int)g_orchestrion.errors);
                continue;
            } else if (my_strncmp(prompt, "/orch_clear", 11) == 0) {
                orchestrion_pipeline_clear(&g_orchestrion);
                Print(L"\r\nOK: pipeline cleared\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_add", 9) == 0) {
                const char *p = prompt + 9;
                while (*p == ' ') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /orch_add <step> [; <step2> ...]\r\n\r\n");
                    continue;
                }
                int added = 0;
                while (*p) {
                    char step[ORCHESTRION_STEP_LEN];
                    int n = 0;
                    while (*p && *p != ';' && n + 1 < ORCHESTRION_STEP_LEN) {
                        step[n++] = *p++;
                    }
                    step[n] = 0;
                    // Trim trailing spaces
                    while (n > 0 && (step[n-1] == ' ' || step[n-1] == '\t')) step[--n] = 0;
                    // Trim leading spaces
                    char *s = step;
                    while (*s == ' ' || *s == '\t') s++;
                    if (*s) {
                        if (orchestrion_pipeline_add_step(&g_orchestrion, s)) added++;
                    }
                    if (*p == ';') p++;
                    while (*p == ' ' || *p == '\t') p++;
                }
                Print(L"\r\nOK: added %d step(s), total=%d\r\n\r\n", added, (int)g_orchestrion.pipeline.step_count);
                continue;
            } else if (my_strncmp(prompt, "/orch_start", 11) == 0) {
                const char *p = prompt + 11;
                while (*p == ' ') p++;
                uint32_t loops = 1;
                if (*p >= '0' && *p <= '9') {
                    loops = 0;
                    while (*p >= '0' && *p <= '9') loops = loops * 10 + (*p++ - '0');
                }
                if (orchestrion_pipeline_start(&g_orchestrion, loops)) {
                    Print(L"\r\nOK: pipeline started (loops=%d)\r\n\r\n", (int)loops);
                } else {
                    Print(L"\r\nERROR: cannot start (no steps?)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/orch_pause", 11) == 0) {
                orchestrion_pipeline_pause(&g_orchestrion);
                Print(L"\r\nOK: pipeline paused\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_resume", 12) == 0) {
                orchestrion_pipeline_resume(&g_orchestrion);
                Print(L"\r\nOK: pipeline resumed\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_stop", 10) == 0) {
                orchestrion_pipeline_stop(&g_orchestrion);
                Print(L"\r\nOK: pipeline stopped\r\n\r\n");
                continue;

            // ==============================================================
            // CALIBRION commands
            // ==============================================================
            } else if (my_strncmp(prompt, "/calib_on", 9) == 0) {
                calibrion_set_mode(&g_calibrion, CALIBRION_MODE_OBSERVE);
                Print(L"\r\nOK: calibrion=observe\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_off", 10) == 0) {
                calibrion_set_mode(&g_calibrion, CALIBRION_MODE_OFF);
                Print(L"\r\nOK: calibrion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_enforce", 14) == 0) {
                const char *p = prompt + 14;
                while (*p == ' ') p++;
                int v = 2;
                if (*p >= '0' && *p <= '2') v = *p - '0';
                calibrion_set_mode(&g_calibrion, (CalibrionMode)v);
                Print(L"\r\nOK: calibrion_mode=%d\r\n\r\n", v);
                continue;
            } else if (my_strncmp(prompt, "/calib_strategy", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ') p++;
                CalibrionStrategy s = CALIBRION_STRATEGY_NONE;
                if (my_strncmp(p, "entropy", 7) == 0) s = CALIBRION_STRATEGY_ENTROPY;
                else if (my_strncmp(p, "length", 6) == 0) s = CALIBRION_STRATEGY_LENGTH;
                else if (my_strncmp(p, "quality", 7) == 0) s = CALIBRION_STRATEGY_QUALITY;
                else if (my_strncmp(p, "hybrid", 6) == 0) s = CALIBRION_STRATEGY_HYBRID;
                calibrion_set_strategy(&g_calibrion, s);
                Print(L"\r\nOK: calibrion_strategy=");
                llmk_print_ascii(calibrion_strategy_name_ascii(s));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_status", 13) == 0) {
                Print(L"\r\n[Calibrion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(calibrion_mode_name_ascii(g_calibrion.mode));
                Print(L"\r\n");
                Print(L"  strategy=");
                llmk_print_ascii(calibrion_strategy_name_ascii(g_calibrion.strategy));
                Print(L"\r\n");
                Print(L"  samples=%d total_tokens=%d repeats=%d\r\n",
                      (int)g_calibrion.stats.samples,
                      (int)g_calibrion.stats.total_tokens,
                      (int)g_calibrion.stats.total_repeats);
                Print(L"  short=%d long=%d avg_entropy_milli=%d\r\n",
                      (int)g_calibrion.stats.short_responses,
                      (int)g_calibrion.stats.long_responses,
                      (int)g_calibrion.stats.avg_entropy_milli);
                Print(L"  rec: temp=%d.%02d top_k=%d top_p=%d.%02d\r\n",
                      (int)(g_calibrion.rec_temp_milli / 1000),
                      (int)((g_calibrion.rec_temp_milli % 1000) / 10),
                      (int)g_calibrion.rec_top_k,
                      (int)(g_calibrion.rec_top_p_milli / 1000),
                      (int)((g_calibrion.rec_top_p_milli % 1000) / 10));
                Print(L"  calibrations_done=%d\r\n\r\n", (int)g_calibrion.calibrations_done);
                continue;
            } else if (my_strncmp(prompt, "/calib_reset", 12) == 0) {
                calibrion_reset_stats(&g_calibrion);
                Print(L"\r\nOK: calibrion stats reset\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_apply", 12) == 0) {
                uint32_t t, k, p;
                calibrion_get_recommendation(&g_calibrion, &t, &k, &p);
                temperature = (float)t / 1000.0f;
                top_k = (int)k;
                top_p = (float)p / 1000.0f;
                Print(L"\r\nOK: applied temp=%d.%02d top_k=%d top_p=%d.%02d\r\n\r\n",
                      (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
                      top_k,
                      (int)top_p, (int)((top_p - (int)top_p) * 100.0f));
                continue;

            // ==============================================================
            // COMPATIBILION commands
            // ==============================================================
            } else if (my_strncmp(prompt, "/compat_on", 10) == 0) {
                compatibilion_set_mode(&g_compatibilion, COMPATIBILION_MODE_ON);
                Print(L"\r\nOK: compatibilion=on\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/compat_off", 11) == 0) {
                compatibilion_set_mode(&g_compatibilion, COMPATIBILION_MODE_OFF);
                Print(L"\r\nOK: compatibilion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/compat_status", 14) == 0) {
                Print(L"\r\n[Compatibilion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(compatibilion_mode_name_ascii(g_compatibilion.mode));
                Print(L"\r\n");
                Print(L"  cpu_vendor=");
                llmk_print_ascii(g_compatibilion.caps.cpu_vendor);
                Print(L"\r\n");
                Print(L"  cpu_brand=");
                llmk_print_ascii(g_compatibilion.caps.cpu_brand);
                Print(L"\r\n");
                Print(L"  cpu_flags=0x%x (SSE2=%d AVX=%d AVX2=%d FMA=%d)\r\n",
                      (unsigned)g_compatibilion.caps.cpu_flags,
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_SSE2),
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_AVX),
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_AVX2),
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_FMA));
                Print(L"  platform_flags=0x%x (UEFI=%d GOP=%d FAT32=%d)\r\n",
                      (unsigned)g_compatibilion.caps.platform_flags,
                      compatibilion_has_platform(&g_compatibilion, COMPAT_PLAT_UEFI),
                      compatibilion_has_platform(&g_compatibilion, COMPAT_PLAT_GOP),
                      compatibilion_has_platform(&g_compatibilion, COMPAT_PLAT_FAT32));
                Print(L"  mem_tier=");
                llmk_print_ascii(compatibilion_mem_tier_name_ascii(g_compatibilion.caps.mem_tier));
                Print(L" (%lu bytes)\r\n", g_compatibilion.caps.mem_bytes);
                if (g_compatibilion.caps.gop_width > 0) {
                    Print(L"  gop=%dx%d\r\n", (int)g_compatibilion.caps.gop_width, (int)g_compatibilion.caps.gop_height);
                }
                Print(L"  recommend: attn=%s model_mb=%d\r\n",
                      compatibilion_recommend_attn(&g_compatibilion) ? L"AVX2" : L"SSE2",
                      (int)compatibilion_recommend_model_mb(&g_compatibilion));
                Print(L"  probes_done=%d\r\n\r\n", (int)g_compatibilion.probes_done);
                continue;
            } else if (my_strncmp(prompt, "/compat_probe", 13) == 0) {
                compatibilion_probe_cpu(&g_compatibilion);
                Print(L"\r\nOK: CPU probed (flags=0x%x)\r\n\r\n", (unsigned)g_compatibilion.caps.cpu_flags);
                continue;

            } else if (my_strncmp(prompt, "/oo_status", 10) == 0) {
                Print(L"\r\n[OO Engines]\r\n");
                Print(L"  evolvion   %s need=%d jit=%d\r\n", evolvion_mode_name_ascii(g_evolvion.mode), (int)g_evolvion.needs_recorded, (int)g_evolvion.jit_successes);
                Print(L"  synaption  %s blocks=%d\r\n", synaption_mode_name_ascii(g_synaption.mode), (int)g_synaption.blocks_tracked);
                Print(L"  conscience %s samples=%d\r\n", conscience_mode_name_ascii(g_conscience.mode), (int)g_conscience.samples_taken);
                Print(L"  neuralfs   %s idx=%d qry=%d\r\n", neuralfs_mode_name_ascii(g_neuralfs.mode), (int)g_neuralfs.blobs_indexed, (int)g_neuralfs.queries_done);
                Print(L"  ghost      %s sent=%d recv=%d\r\n", ghost_mode_name_ascii(g_ghost.mode), (int)g_ghost.tokens_sent, (int)g_ghost.tokens_recv);
                Print(L"  immunion   %s rec=%d react=%d\r\n", immunion_mode_name_ascii(g_immunion.mode), (int)g_immunion.patterns_recorded, (int)g_immunion.reactions_triggered);
                Print(L"  dreamion   %s cycles=%lu synth=%d dna=%d\r\n", dreamion_mode_name_ascii(g_dreamion.mode), (unsigned long)g_dreamion.stats.total_dream_cycles, (int)g_dreamion.stats.synth_pairs_generated, (int)g_dreamion.stats.dna_mutations_suggested);
                Print(L"  symbion    %s samples=%d\r\n", symbion_mode_name_ascii(g_symbion.mode), (int)g_symbion.samples_taken);
                Print(L"  collectivion %s bcast=%d poll=%d\r\n", collectivion_mode_name_ascii(g_collectivion.mode), (int)g_collectivion.broadcasts_sent, (int)g_collectivion.broadcasts_recv);
                Print(L"  metabion   %s tok_s=%lu samples=%d\r\n", metabion_mode_name_ascii(g_metabion.mode), (unsigned long)g_metabion.last.tokens_per_sec, (int)g_metabion.samples_count);
                Print(L"  morphion   %s\r\n", morphion_mode_name_ascii(g_morphion.mode));
                Print(L"  pheromion  %s top_path=%u\r\n\r\n", pheromion_mode_name_ascii(g_pheromion.mode), (unsigned)pheromion_top_path(&g_pheromion));
                llmk_oo_print_persistence_status_best_effort();
                continue;

            } else if (my_strncmp(prompt, "/gop", 4) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\n  GOP: not available\r\n\r\n");
                } else {
                    const CHAR16 *pf = L"unknown";
                    if (g_gop_pf == PixelBlueGreenRedReserved8BitPerColor) pf = L"BGRX8888";
                    else if (g_gop_pf == PixelRedGreenBlueReserved8BitPerColor) pf = L"RGBX8888";
                    else if (g_gop_pf == PixelBitMask) pf = L"BITMASK";
                    Print(L"\r\n  GOP: %dx%d ppsl=%d fmt=%s fb=0x%lx\r\n\r\n",
                          (int)g_gop_w, (int)g_gop_h, (int)g_gop_ppsl, pf, (UINT64)(UINTN)g_gop_fb32);
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_on", 7) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                } else {
                    g_tui_enabled = 1;
                    llmk_tui_set_event("/tui_on");
                    llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: TUI enabled\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_off", 8) == 0) {
                g_tui_enabled = 0;
                llmk_tui_set_event("/tui_off");
                Print(L"\r\nOK: TUI disabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_toggle", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                } else {
                    g_tui_enabled = !g_tui_enabled;
                    llmk_tui_set_event("/tui_toggle");
                    if (g_tui_enabled) llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: TUI %s\r\n\r\n", g_tui_enabled ? L"enabled" : L"disabled");
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_redraw", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                } else {
                    llmk_tui_set_event("/tui_redraw");
                    g_tui_enabled = 1;
                    llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: TUI redrawn\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_mode", 9) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                const char *p = prompt + 9;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /tui_mode <status|log|split|files>\r\n");
                    Print(L"  Current: %d\r\n\r\n", g_ui_mode);
                    continue;
                }
                if (my_strncmp(p, "status", 6) == 0) g_ui_mode = 0;
                else if (my_strncmp(p, "log", 3) == 0) g_ui_mode = 1;
                else if (my_strncmp(p, "split", 5) == 0) g_ui_mode = 2;
                else if (my_strncmp(p, "files", 5) == 0) g_ui_mode = 3;
                else {
                    Print(L"\r\nERROR: unknown mode\r\n\r\n");
                    continue;
                }
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_mode");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: UI mode=%d\r\n\r\n", g_ui_mode);
                continue;
            } else if (my_strncmp(prompt, "/tui_log_on", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                g_ui_mode = 1;
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_on");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log UI enabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_log_off", 12) == 0) {
                g_ui_mode = 0;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_off");
                if (g_tui_enabled) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log UI disabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_log_clear", 13) == 0) {
                llmk_tr_clear();
                llmk_tui_set_event("/tui_log_clear");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: transcript cleared\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_log_up", 11) == 0) {
                const char *p = prompt + 11;
                while (*p == ' ' || *p == '\t') p++;
                int n = 10;
                if (*p) {
                    int v = 0;
                    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                    if (v > 0) n = v;
                }
                g_tr_scroll += n;
                if ((UINT32)g_tr_scroll > g_tr_count) g_tr_scroll = (int)g_tr_count;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_up");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log scroll=%d\r\n\r\n", g_tr_scroll);
                continue;
            } else if (my_strncmp(prompt, "/tui_log_down", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;
                int n = 10;
                if (*p) {
                    int v = 0;
                    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                    if (v > 0) n = v;
                }
                g_tr_scroll -= n;
                if (g_tr_scroll < 0) g_tr_scroll = 0;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_down");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log scroll=%d\r\n\r\n", g_tr_scroll);
                continue;
            } else if (my_strncmp(prompt, "/tui_log_dump", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 out_name[96];
                if (*p == 0) {
                    StrCpy(out_name, L"llmk-transcript.txt");
                } else {
                    ascii_to_char16(out_name, p, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                // Flush any partial line so the dump matches what the user saw.
                if (g_tr_cur_len > 0) llmk_tr_flush_cur_line();

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, out_name);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: cannot open %s (%r)\r\n\r\n", out_name, st);
                    continue;
                }
                for (UINT32 age = g_tr_count; age > 0; age--) {
                    const char *line8 = llmk_tr_get_line_by_age(age - 1);
                    CHAR16 line16[LLMK_TR_COLS + 4];
                    ascii_to_char16(line16, line8, (int)(sizeof(line16) / sizeof(line16[0])));
                    llmk_file_write_u16(f, line16);
                    llmk_file_write_u16(f, L"\r\n");
                }
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed (%r)\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote %s\r\n\r\n", out_name);
                }
                continue;
            } else if (my_strncmp(prompt, "/fb_on", 6) == 0 || my_strcmp(prompt, "/fb") == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                g_ui_mode = 3;
                g_tui_enabled = 1;
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_on");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: file browser enabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fb_off", 7) == 0) {
                g_ui_mode = 0;
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_off");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: file browser disabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fb_refresh", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_refresh");
                if (g_tui_enabled) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fb_cd", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /fb_cd <dir>\r\n\r\n");
                    continue;
                }
                llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), p);
                ascii_to_char16(g_fb_path16, g_fb_path8, (int)(sizeof(g_fb_path16) / sizeof(g_fb_path16[0])));
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_ui_mode = 3;
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_cd");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: cd %s\r\n\r\n", g_fb_path16);
                continue;
            } else if (my_strncmp(prompt, "/fb_up", 6) == 0) {
                // Parent directory (ASCII path best-effort)
                int n = 0;
                while (g_fb_path8[n]) n++;
                while (n > 0 && (g_fb_path8[n - 1] == '\\' || g_fb_path8[n - 1] == '/')) n--;
                while (n > 0 && g_fb_path8[n - 1] != '\\') n--;
                if (n <= 0) {
                    llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), "\\");
                } else {
                    g_fb_path8[n] = 0;
                    if (g_fb_path8[0] == 0) llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), "\\");
                }
                ascii_to_char16(g_fb_path16, g_fb_path8, (int)(sizeof(g_fb_path16) / sizeof(g_fb_path16[0])));
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_ui_mode = 3;
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_up");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: cd %s\r\n\r\n", g_fb_path16);
                continue;
            } else if (my_strncmp(prompt, "/fb_sel", 7) == 0) {
                const char *p = prompt + 7;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 0) v = 0;
                if (v >= g_fb_count) v = (g_fb_count > 0) ? (g_fb_count - 1) : 0;
                g_fb_sel = v;
                llmk_fb_preview_selected_best_effort();
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_sel");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: sel=%d\r\n\r\n", g_fb_sel);
                continue;
            } else if (my_strncmp(prompt, "/fb_open", 8) == 0) {
                if (g_fb_count <= 0 || g_fb_sel < 0 || g_fb_sel >= g_fb_count) {
                    Print(L"\r\nERROR: no selection\r\n\r\n");
                    continue;
                }
                if (g_fb_entries[g_fb_sel].is_dir) {
                    // cd into dir
                    char newp[128];
                    newp[0] = 0;
                    llmk_ascii_copy_cap(newp, (int)sizeof(newp), g_fb_path8[0] ? g_fb_path8 : "\\");
                    int np = 0;
                    while (newp[np]) np++;
                    if (np > 0 && newp[np - 1] != '\\') {
                        if (np + 1 < (int)sizeof(newp)) newp[np++] = '\\';
                        newp[np] = 0;
                    }
                    llmk_ascii_append_cap(newp, (int)sizeof(newp), g_fb_entries[g_fb_sel].name8);
                    llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), newp);
                    ascii_to_char16(g_fb_path16, g_fb_path8, (int)(sizeof(g_fb_path16) / sizeof(g_fb_path16[0])));
                    llmk_fb_refresh_best_effort();
                    llmk_fb_preview_selected_best_effort();
                    g_tui_dirty = 1;
                    llmk_tui_set_event("/fb_open(dir)");
                    if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: cd %s\r\n\r\n", g_fb_path16);
                } else {
                    llmk_fb_preview_selected_best_effort();
                    g_tui_dirty = 1;
                    llmk_tui_set_event("/fb_open(file)");
                    if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: preview loaded\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/render", 7) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available on this firmware path\r\n\r\n");
                    continue;
                }
                const char *dsl = prompt + 7;
                while (*dsl == ' ' || *dsl == '\t') dsl++;
                if (*dsl == 0) {
                    Print(L"\r\nUsage: /render <dsl>\r\n");
                    Print(L"  DSL ops (separate by ';'):\r\n");
                    Print(L"    clear R G B; rect X Y W H R G B; pixel X Y R G B\r\n\r\n");
                    continue;
                }
                int ok = llmk_render_scene_dsl_ex(dsl, 1);
                if (ok) {
                    llmk_gop_force_update();
                    Print(L"\r\nOK: rendered (check screen above)\r\n\r\n");
                } else {
                    CHAR16 msg[140];
                    ascii_to_char16(msg, g_last_dsl_error, (int)(sizeof(msg) / sizeof(msg[0])));
                    Print(L"\r\nERROR: render failed (%s)\r\n", msg);
                    Print(L"Hint: use 'rect' not 'react'\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/save_img", 9) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available (nothing to save)\r\n\r\n");
                    continue;
                }
                const char *name = prompt + 9;
                while (*name == ' ' || *name == '\t') name++;

                CHAR16 out_name[64];
                if (*name == 0) {
                    StrCpy(out_name, L"llmk-img.ppm");
                } else {
                    ascii_to_char16(out_name, name, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                EFI_STATUS st = llmk_save_ppm(out_name);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: save failed (%r)\r\n\r\n", st);
                } else {
                    Print(L"\r\nOK: wrote %s (PPM, flushed)\r\n\r\n", out_name);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_ls", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 path[160];
                if (*p == 0) {
                    path[0] = 0;
                } else {
                    ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));
                }
                Print(L"\r\n");
                llmk_fs_ls_best_effort(path[0] ? path : NULL, 200);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fs_cat", 7) == 0) {
                const char *p = prompt + 7;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /fs_cat <file>\r\n\r\n");
                    continue;
                }
                CHAR16 path[160];
                ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));
                Print(L"\r\n");
                llmk_fs_cat_best_effort(path, 256U * 1024U);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/wasm_info", 10) == 0) {
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /wasm_info <file.wasm>\r\n\r\n");
                    continue;
                }
                CHAR16 path[160];
                ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));

                void *wasm_buf = NULL;
                UINTN wasm_len = 0;
                EFI_STATUS st = llmk_read_entire_file_best_effort(path, &wasm_buf, &wasm_len);
                if (EFI_ERROR(st) || !wasm_buf || wasm_len == 0) {
                    if (wasm_buf) uefi_call_wrapper(BS->FreePool, 1, wasm_buf);
                    Print(L"\r\nERROR: read failed: %r\r\n\r\n", st);
                    continue;
                }

                const uint8_t *dna = NULL;
                size_t dna_len = 0;
                int rc = cellion_wasm_find_custom_section(&g_cellion, (const uint8_t *)wasm_buf, (size_t)wasm_len, "oo.dna", &dna, &dna_len);

                Print(L"\r\n[wasm] file=%s bytes=%lu\r\n", path, (unsigned long)wasm_len);
                if (rc == CELLION_OK && dna && dna_len) {
                    Print(L"[wasm] custom section oo.dna: %lu bytes\r\n", (unsigned long)dna_len);
                    UINTN preview = (UINTN)dna_len;
                    if (preview > 256) preview = 256;
                    Print(L"[wasm] preview (first %lu bytes):\r\n", (unsigned long)preview);
                    for (UINTN i = 0; i < preview; i++) {
                        char c = (char)dna[i];
                        if (c == '\r') continue;
                        if (c == 0) break;
                        if (c == '\n') {
                            Print(L"\r\n");
                        } else if (c >= 32 && c <= 126) {
                            CHAR16 w[2]; // SAFE: single UTF-16 char + NUL
                            w[0] = (CHAR16)c;
                            w[1] = 0; // SAFE: fixed-size local buffer
                            Print(L"%s", w);
                        }
                    }
                    Print(L"\r\n");
                } else if (rc == CELLION_ERR_NOT_FOUND) {
                    Print(L"[wasm] custom section oo.dna: not found\r\n");
                } else {
                    Print(L"[wasm] parse error (rc=%d)\r\n", rc);
                }

                uefi_call_wrapper(BS->FreePool, 1, wasm_buf);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/wasm_apply", 10) == 0) {
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /wasm_apply <file.wasm>\r\n");
                    Print(L"Notes: applies custom section name 'oo.dna' with ASCII key=value lines\r\n\r\n");
                    continue;
                }
                CHAR16 path[160];
                ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));

                void *wasm_buf = NULL;
                UINTN wasm_len = 0;
                EFI_STATUS st = llmk_read_entire_file_best_effort(path, &wasm_buf, &wasm_len);
                if (EFI_ERROR(st) || !wasm_buf || wasm_len == 0) {
                    if (wasm_buf) uefi_call_wrapper(BS->FreePool, 1, wasm_buf);
                    Print(L"\r\nERROR: read failed: %r\r\n\r\n", st);
                    continue;
                }

                const uint8_t *dna = NULL;
                size_t dna_len = 0;
                int rc = cellion_wasm_find_custom_section(&g_cellion, (const uint8_t *)wasm_buf, (size_t)wasm_len, "oo.dna", &dna, &dna_len);
                Print(L"\r\n[wasm] apply from %s\r\n", path);
                if (rc == CELLION_OK && dna && dna_len) {
                    llmk_wasm_apply_oo_dna_kv_best_effort(
                        dna,
                        dna_len,
                        &temperature,
                        &min_p,
                        &top_p,
                        &top_k,
                        &repeat_penalty,
                        &no_repeat_ngram,
                        &max_gen_tokens,
                        &stats_enabled,
                        &stop_on_you,
                        &stop_on_double_nl,
                        &m18_base_temp_milli,
                        &m18_base_top_p_milli,
                        &m18_base_top_k,
                        &m18_base_max_gen_tokens
                    );
                    Print(L"[wasm] done\r\n\r\n");
                } else if (rc == CELLION_ERR_NOT_FOUND) {
                    Print(L"\r\nERROR: oo.dna not found in module\r\n\r\n");
                } else {
                    Print(L"\r\nERROR: wasm parse failed (rc=%d)\r\n\r\n", rc);
                }
                uefi_call_wrapper(BS->FreePool, 1, wasm_buf);
                continue;
            } else if (my_strncmp(prompt, "/fs_write", 9) == 0) {
                const char *p = prompt + 9;
                while (*p == ' ' || *p == '\t') p++;
                // Parse path token
                char tok[160];
                int tp = 0;
                while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) tok[tp++] = *p++;
                tok[tp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                const char *text = p;
                if (tok[0] == 0) {
                    Print(L"\r\nUsage: /fs_write <file> <text...>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_WRITE, tok, (UINT32)my_strlen(text), &d);
                    djibion_log_if_observe(&g_djibion, "fs_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_write): %s\r\n\r\n", msg);
                        continue;
                    }

                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/fs_write) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        djibion_apply_transform_path(tok, (int)sizeof(tok), &d);
                    }
                }

                CHAR16 path[160];
                ascii_to_char16(path, tok, (int)(sizeof(path) / sizeof(path[0])));
                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_binary_file(&f, path);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }
                UINTN n = (UINTN)my_strlen(text);
                st = llmk_file_write_bytes(f, text, n);
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: write failed: %r\r\n\r\n", st);
                } else if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed: %r\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote ");
                    Print(L"%s", path);
                    Print(L" (%d bytes)\r\n\r\n", (int)n);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_append", 10) == 0) {
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                // Parse path token
                char tok[160];
                int tp = 0;
                while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) tok[tp++] = *p++;
                tok[tp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                const char *text = p;
                if (tok[0] == 0) {
                    Print(L"\r\nUsage: /fs_append <file> <text...>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_APPEND, tok, (UINT32)my_strlen(text), &d);
                    djibion_log_if_observe(&g_djibion, "fs_append", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_append): %s\r\n\r\n", msg);
                        continue;
                    }

                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/fs_append) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        djibion_apply_transform_path(tok, (int)sizeof(tok), &d);
                    }
                }

                CHAR16 path[160];
                ascii_to_char16(path, tok, (int)(sizeof(path) / sizeof(path[0])));
                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_binary_file_append(&f, path);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }
                UINTN n = (UINTN)my_strlen(text);
                st = llmk_file_write_bytes(f, text, n);
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: append failed: %r\r\n\r\n", st);
                } else if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed: %r\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: appended ");
                    Print(L"%s", path);
                    Print(L" (%d bytes)\r\n\r\n", (int)n);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_rm", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /fs_rm <file>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_RM, p, 0, &d);
                    djibion_log_if_observe(&g_djibion, "fs_rm", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_rm): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                CHAR16 path[160];
                ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));
                EFI_STATUS st = llmk_delete_file_best_effort(path);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: delete failed: %r\r\n\r\n", st);
                } else {
                    Print(L"\r\nOK: deleted %s\r\n\r\n", path);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_cp", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                char src8[128];
                int sp = 0;
                while (*p && *p != ' ' && *p != '\t' && sp + 1 < (int)sizeof(src8)) src8[sp++] = *p++;
                src8[sp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                char dst8[128];
                int dp = 0;
                while (*p && *p != ' ' && *p != '\t' && dp + 1 < (int)sizeof(dst8)) dst8[dp++] = *p++;
                dst8[dp] = 0;
                if (src8[0] == 0 || dst8[0] == 0) {
                    Print(L"\r\nUsage: /fs_cp <src> <dst>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort): validate src (no '..') and govern dst.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    if (llmk_ascii_has_dotdot(src8)) {
                        Print(L"\r\nDJIBION: blocked (/fs_cp): src path contains '..'\r\n\r\n");
                        continue;
                    }
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_CP, dst8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "fs_cp", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_cp): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] fs_cp dst transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        llmk_ascii_copy_cap(dst8, (int)sizeof(dst8), d.transformed_arg0);
                    }
                }

                CHAR16 src[160];
                CHAR16 dst[160];
                ascii_to_char16(src, src8, (int)(sizeof(src) / sizeof(src[0])));
                ascii_to_char16(dst, dst8, (int)(sizeof(dst) / sizeof(dst[0])));
                EFI_STATUS st = llmk_copy_file_best_effort(src, dst);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: copy failed: %r\r\n\r\n", st);
                } else {
                    Print(L"\r\nOK: copied %s -> %s\r\n\r\n", src, dst);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_mv", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                char src8[128];
                int sp = 0;
                while (*p && *p != ' ' && *p != '\t' && sp + 1 < (int)sizeof(src8)) src8[sp++] = *p++;
                src8[sp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                char dst8[128];
                int dp = 0;
                while (*p && *p != ' ' && *p != '\t' && dp + 1 < (int)sizeof(dst8)) dst8[dp++] = *p++;
                dst8[dp] = 0;
                if (src8[0] == 0 || dst8[0] == 0) {
                    Print(L"\r\nUsage: /fs_mv <src> <dst>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort): validate src (no '..') and govern dst (move implies delete).
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    if (llmk_ascii_has_dotdot(src8)) {
                        Print(L"\r\nDJIBION: blocked (/fs_mv): src path contains '..'\r\n\r\n");
                        continue;
                    }
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_MV, dst8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "fs_mv", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_mv): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] fs_mv dst transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        llmk_ascii_copy_cap(dst8, (int)sizeof(dst8), d.transformed_arg0);
                    }
                }

                CHAR16 src[160];
                CHAR16 dst[160];
                ascii_to_char16(src, src8, (int)(sizeof(src) / sizeof(src[0])));
                ascii_to_char16(dst, dst8, (int)(sizeof(dst) / sizeof(dst[0])));
                EFI_STATUS st = llmk_copy_file_best_effort(src, dst);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: move copy failed: %r\r\n\r\n", st);
                    continue;
                }
                EFI_STATUS st2 = llmk_delete_file_best_effort(src);
                if (EFI_ERROR(st2)) {
                    Print(L"\r\nWARNING: move delete failed: %r\r\n\r\n", st2);
                } else {
                    Print(L"\r\nOK: moved %s -> %s\r\n\r\n", src, dst);
                }
                continue;
            } else if (my_strncmp(prompt, "/snap_save", 10) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 out_name[96];
                if (*p == 0) {
                    StrCpy(out_name, L"llmk-snap.bin");
                } else {
                    ascii_to_char16(out_name, p, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                if (kv_pos <= 0) {
                    Print(L"\r\nERROR: nothing to snapshot (kv_pos=0)\r\n\r\n");
                    continue;
                }
                if (kv_pos > config.seq_len) {
                    Print(L"\r\nERROR: kv_pos out of range\r\n\r\n");
                    continue;
                }

                int kv_dim = (config.dim * config.n_kv_heads) / config.n_heads;
                UINTN slice_floats = (UINTN)kv_pos * (UINTN)kv_dim;
                UINTN slice_bytes = slice_floats * sizeof(float);

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), out_name);
                    UINTN total_bytes = sizeof(LlmkSnapHeader) + (UINTN)config.n_layers * (UINTN)2 * slice_bytes;
                    UINT32 total32 = (total_bytes > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (UINT32)total_bytes;
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_SNAP_SAVE, file8, total32, &d);
                    djibion_log_if_observe(&g_djibion, "snap_save", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_save): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] snap_save path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(out_name, d.transformed_arg0, (int)(sizeof(out_name) / sizeof(out_name[0])));
                    }
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_binary_file(&f, out_name);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }

                LlmkSnapHeader hdr;
                hdr.magic = LLMK_SNAP_MAGIC;
                hdr.version = 1;
                hdr.dim = (UINT32)config.dim;
                hdr.n_layers = (UINT32)config.n_layers;
                hdr.n_heads = (UINT32)config.n_heads;
                hdr.n_kv_heads = (UINT32)config.n_kv_heads;
                hdr.seq_len = (UINT32)config.seq_len;
                hdr.kv_dim = (UINT32)kv_dim;
                hdr.kv_pos = (UINT32)kv_pos;

                st = llmk_write_exact(f, &hdr, sizeof(hdr));
                if (!EFI_ERROR(st)) {
                    for (int l = 0; l < config.n_layers && !EFI_ERROR(st); l++) {
                        float *base = state.key_cache + (UINTN)l * (UINTN)config.seq_len * (UINTN)kv_dim;
                        st = llmk_write_exact(f, base, slice_bytes);
                    }
                    for (int l = 0; l < config.n_layers && !EFI_ERROR(st); l++) {
                        float *base = state.value_cache + (UINTN)l * (UINTN)config.seq_len * (UINTN)kv_dim;
                        st = llmk_write_exact(f, base, slice_bytes);
                    }
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: snapshot write failed: %r\r\n\r\n", st);
                } else if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed: %r\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote snapshot %s (kv_pos=%d)\r\n\r\n", out_name, kv_pos);
                }
                continue;
            } else if (my_strncmp(prompt, "/snap_load", 10) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 in_name[96];
                if (*p == 0) {
                    StrCpy(in_name, L"llmk-snap.bin");
                } else {
                    ascii_to_char16(in_name, p, (int)(sizeof(in_name) / sizeof(in_name[0])));
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), in_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "snap_load", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_load): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] snap_load path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(in_name, d.transformed_arg0, (int)(sizeof(in_name) / sizeof(in_name[0])));
                    }
                }

                EFI_STATUS st = llmk_snap_load_into_state_best_effort(&state, &config, &kv_pos, in_name);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: snapshot load failed: %r\r\n\r\n", st);
                    continue;
                }
                Print(L"\r\nOK: loaded snapshot %s (kv_pos=%d)\r\n\r\n", in_name, kv_pos);
                continue;
            } else if (my_strncmp(prompt, "/snap_autoload_on", 16) == 0) {
                const char *p = prompt + 16;
                while (*p == ' ' || *p == '\t') p++;

                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "snap_autoload", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_autoload_on): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("snap_autoload", "1");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                    continue;
                }
                if (*p) {
                    // Optional file override

                    if (g_djibion.mode != DJIBION_MODE_OFF) {
                        DjibionDecision d;
                        djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "snap_file", (UINT32)my_strlen(p), &d);
                        djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                        if (djibion_should_block(&g_djibion, &d)) {
                            CHAR16 msg[160];
                            ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                            Print(L"\r\nDJIBION: blocked (snap_file update): %s\r\n\r\n", msg);
                            Print(L"\r\nOK: snap_autoload=1 (reboot to apply)\r\n\r\n");
                            llmk_tr_note("SNAP: snap_autoload_on");
                            continue;
                        }
                    }

                    EFI_STATUS st2 = llmk_repl_cfg_set_kv_best_effort("snap_file", p);
                    if (EFI_ERROR(st2)) {
                        Print(L"\r\nWARNING: snap_file update failed: %r\r\n\r\n", st2);
                    }
                }
                Print(L"\r\nOK: snap_autoload=1 (reboot to apply)\r\n\r\n");
                llmk_tr_note("SNAP: snap_autoload_on");
                continue;
            } else if (my_strncmp(prompt, "/snap_autoload_off", 17) == 0) {

                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "snap_autoload", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_autoload_off): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("snap_autoload", "0");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                    continue;
                }
                Print(L"\r\nOK: snap_autoload=0 (reboot to apply)\r\n\r\n");
                llmk_tr_note("SNAP: snap_autoload_off");
                continue;
            } else if (my_strncmp(prompt, "/oo_new", 7) == 0) {
                const char *goal = prompt + 7;
                while (*goal == ' ' || *goal == '\t') goal++;
                if (*goal == 0) {
                    Print(L"\r\nUsage: /oo_new <goal>\r\n\r\n");
                    continue;
                }
                int id = llmk_oo_new(goal);
                if (id < 0) {
                    Print(L"\r\nERROR: cannot create entity (full?)\r\n\r\n");
                } else {
                    Print(L"\r\nOK: created entity id=%d\r\n\r\n", id);
                    llmk_oo_journal_cmd_best_effort("oo_new");
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_list", 8) == 0) {
                llmk_oo_list_print();
                llmk_oo_journal_cmd_best_effort("oo_list");
                continue;
            } else if (my_strncmp(prompt, "/oo_status", 10) == 0) {
                // Print effective OO config (best-effort)
                int consult_enabled = g_cfg_oo_llm_consult;
                if (consult_enabled < 0) consult_enabled = g_cfg_oo_enable ? 1 : 0;
                int multi_enabled = g_cfg_oo_multi_actions;
                if (multi_enabled < 0) multi_enabled = (consult_enabled > 0) ? 1 : 0;

                Print(L"\r\nOO status:\r\n");
                Print(L"  oo_enable=%d autoload=%d autosave_every=%d\r\n", g_cfg_oo_enable, oo_autoload, oo_autosave_every);
                Print(L"  state_file=%s\r\n", oo_state_file);
                Print(L"  llm_consult=%d multi_actions=%d\r\n", consult_enabled, multi_enabled);
                Print(L"  conf_gate=%d conf_threshold=%d\r\n", g_cfg_oo_conf_gate, g_cfg_oo_conf_threshold);
                llmk_oo_print_persistence_status_best_effort();
                Print(L"\r\nHint: /oo_list, /oo_show <id>, /oo_agenda <id>, /oo_outcome, /oo_save\r\n\r\n");
                llmk_oo_journal_cmd_best_effort("oo_status");
                continue;
            } else if (my_strncmp(prompt, "/oo_kill", 8) == 0) {
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_kill <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_kill(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: killed entity id=%d\r\n\r\n", id);
                llmk_oo_journal_cmd_best_effort("oo_kill");
                continue;
            } else if (my_strncmp(prompt, "/oo_step", 8) == 0) {
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_step <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_step(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: stepped entity id=%d\r\n\r\n", id);
                llmk_oo_journal_cmd_best_effort("oo_step");
                continue;
            } else if (my_strncmp(prompt, "/oo_run", 7) == 0) {
                int steps = 1;
                if (prompt[7] == ' ') {
                    int i = 8;
                    int val = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val = val * 10 + (prompt[i] - '0');
                        i++;
                    }
                    if (val > 0) steps = val;
                }
                int ran = llmk_oo_run(steps);

                Print(L"\r\nOK: ran %d step(s)\r\n\r\n", ran);
                llmk_oo_journal_cmd_best_effort("oo_run");
                continue;
            } else if (my_strncmp(prompt, "/oo_note", 8) == 0) {
                // Usage: /oo_note <id> <text...>
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                const char *text = prompt + i;
                if (id <= 0 || !text || !text[0]) {
                    Print(L"\r\nUsage: /oo_note <id> <text>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_note(id, text)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: noted entity id=%d\r\n\r\n", id);
                llmk_oo_journal_cmd_best_effort("oo_note");
                continue;
            } else if (my_strncmp(prompt, "/oo_show", 8) == 0) {
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_show <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_show_print(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                llmk_oo_journal_cmd_best_effort("oo_show");
                continue;
            } else if (my_strncmp(prompt, "/oo_digest", 10) == 0) {
                int i = 10;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_digest <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_digest(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: digested entity id=%d\r\n\r\n", id);
                llmk_oo_journal_cmd_best_effort("oo_digest");
                continue;
            } else if (my_strncmp(prompt, "/oo_plan", 8) == 0) {
                // Usage: /oo_plan <id> [prio] <action...>  (optionally: a1; a2; a3)
                // prio forms: +3, -1, p=2
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;

                int prio = 0;
                // Optional priority token
                if ((prompt[i] == '+' || prompt[i] == '-') && (prompt[i + 1] >= '0' && prompt[i + 1] <= '9')) {
                    int sign = (prompt[i] == '-') ? -1 : 1;
                    i++;
                    int v = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        v = v * 10 + (prompt[i] - '0');
                        i++;
                    }
                    prio = v * sign;
                    while (prompt[i] == ' ') i++;
                } else if (prompt[i] == 'p' && prompt[i + 1] == '=' && (prompt[i + 2] >= '0' && prompt[i + 2] <= '9')) {
                    i += 2;
                    int v = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        v = v * 10 + (prompt[i] - '0');
                        i++;
                    }
                    prio = v;
                    while (prompt[i] == ' ') i++;
                }

                const char *text = prompt + i;
                if (id <= 0 || !text || !text[0]) {
                    Print(L"\r\nUsage: /oo_plan <id> <action>\r\n");
                    Print(L"  Example: /oo_plan 1 do X; do Y\r\n");
                    Print(L"  Priority: /oo_plan 1 +2 urgent thing\r\n");
                    Print(L"  Tip: you can also write: /oo_plan <1> ...\r\n\r\n");
                    continue;
                }

                // Split on ';' (simple)
                int added = 0;
                char tmp[128];
                int tp = 0;
                for (const char *s = text; ; s++) {
                    char c = *s;
                    if (c == 0 || c == ';') {
                        tmp[tp] = 0;
                        // trim
                        const char *t = tmp;
                        while (*t == ' ' || *t == '\t') t++;
                        int end = 0;
                        while (t[end]) end++;
                        while (end > 0 && (t[end - 1] == ' ' || t[end - 1] == '\t')) end--;
                        char one[128];
                        int op = 0;
                        for (int k = 0; k < end && op + 1 < (int)sizeof(one); k++) one[op++] = t[k];
                        one[op] = 0;

                        if (one[0]) {
                            if (llmk_oo_agenda_add_ex(id, one, prio)) added++;
                        }
                        tp = 0;
                        if (c == 0) break;
                    } else {
                        if (tp + 1 < (int)sizeof(tmp)) tmp[tp++] = c;
                    }
                }

                if (added <= 0) {
                    Print(L"\r\nERROR: failed to add action(s) (unknown id or agenda full)\r\n\r\n");
                } else {
                    Print(L"\r\nOK: added %d action(s) to id=%d\r\n\r\n", added, id);
                    llmk_oo_digest(id);
                    llmk_oo_journal_cmd_best_effort("oo_plan");
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_agenda", 10) == 0) {
                int i = 10;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_agenda <id>\r\n");
                    Print(L"  Example: /oo_agenda 1\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_get_brief(id, NULL, 0, NULL, 0)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOO agenda for id=%d:\r\n", id);
                llmk_oo_agenda_print(id);
                Print(L"\r\n");
                llmk_oo_journal_cmd_best_effort("oo_agenda");
                continue;
            } else if (my_strncmp(prompt, "/oo_next", 8) == 0) {
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_next <id>\r\n");
                    Print(L"  Example: /oo_next 1\r\n\r\n");
                    continue;
                }
                char act[96];
                act[0] = 0;
                int k = 0;
                if (!llmk_oo_agenda_next_ex(id, &k, act, (int)sizeof(act))) {
                    Print(L"\r\nOK: agenda empty (or unknown id=%d)\r\n\r\n", id);
                    continue;
                }
                CHAR16 a16[110];
                ascii_to_char16(a16, act, (int)(sizeof(a16) / sizeof(a16[0])));
                Print(L"\r\nOK: next action for id=%d (#%d, marked doing):\r\n  %s\r\n\r\n", id, k, a16);
                llmk_oo_digest(id);
                llmk_oo_journal_cmd_best_effort("oo_next");
                continue;
            } else if (my_strncmp(prompt, "/oo_done", 8) == 0) {
                // Usage: /oo_done <id> <k>
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;
                int k = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    k = k * 10 + (prompt[i] - '0');
                    i++;
                }
                if (id <= 0 || k <= 0) {
                    Print(L"\r\nUsage: /oo_done <id> <k>\r\n");
                    Print(L"  Example: /oo_done 1 2\r\n\r\n");
                    continue;
                }
                char txt[96];
                txt[0] = 0;
                if (!llmk_oo_action_get(id, k, txt, (int)sizeof(txt), NULL, NULL)) {
                    Print(L"\r\nERROR: unknown action #%d for id=%d\r\n\r\n", k, id);
                    continue;
                }
                if (!llmk_oo_action_set_state(id, k, 2)) {
                    Print(L"\r\nERROR: failed to mark done (#%d)\r\n\r\n", k);
                    continue;
                }
                {
                    char dn[196];
                    int dp = 0;
                    const char *h = "done: ";
                    for (int j = 0; h[j] && dp + 1 < (int)sizeof(dn); j++) dn[dp++] = h[j];
                    for (int j = 0; txt[j] && dp + 1 < (int)sizeof(dn); j++) dn[dp++] = txt[j];
                    dn[dp] = 0;
                    llmk_oo_note(id, dn);
                }
                Print(L"\r\nOK: marked done id=%d #%d\r\n\r\n", id, k);
                llmk_oo_digest(id);
                llmk_oo_journal_cmd_best_effort("oo_done");
                continue;
            } else if (my_strncmp(prompt, "/oo_prio", 8) == 0) {
                // Usage: /oo_prio <id> <k> <prio>
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;
                int k = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    k = k * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                int sign = 1;
                if (prompt[i] == '-') { sign = -1; i++; }
                else if (prompt[i] == '+') { i++; }
                int pr = 0;
                int any = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    pr = pr * 10 + (prompt[i] - '0');
                    i++;
                    any = 1;
                }
                pr *= sign;
                if (id <= 0 || k <= 0 || !any) {
                    Print(L"\r\nUsage: /oo_prio <id> <k> <prio>\r\n");
                    Print(L"  Example: /oo_prio 1 2 +3\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_action_set_prio(id, k, pr)) {
                    Print(L"\r\nERROR: failed to set prio id=%d #%d\r\n\r\n", id, k);
                    continue;
                }
                Print(L"\r\nOK: set prio id=%d #%d -> %d\r\n\r\n", id, k, pr);
                llmk_oo_digest(id);
                llmk_oo_journal_cmd_best_effort("oo_prio");
                continue;
            } else if (my_strncmp(prompt, "/oo_edit", 7) == 0) {
                // Usage: /oo_edit <id> <k> <new text...>
                int i = 7;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;
                int k = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    k = k * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                const char *text = prompt + i;
                if (id <= 0 || k <= 0 || !text || !text[0]) {
                    Print(L"\r\nUsage: /oo_edit <id> <k> <text>\r\n");
                    Print(L"  Example: /oo_edit 1 2 rewrite this action\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_action_edit(id, k, text)) {
                    Print(L"\r\nERROR: failed to edit id=%d #%d\r\n\r\n", id, k);
                    continue;
                }
                Print(L"\r\nOK: edited id=%d #%d\r\n\r\n", id, k);
                llmk_oo_digest(id);
                llmk_oo_journal_cmd_best_effort("oo_edit");
                continue;
            } else if (my_strncmp(prompt, "/oo_save", 8) == 0) {
                const char *name = prompt + 8;
                while (*name == ' ' || *name == '\t') name++;
                CHAR16 out_name[96];
                if (*name == 0) {
                    StrCpy(out_name, oo_state_file);
                } else {
                    ascii_to_char16(out_name, name, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), out_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_SAVE, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "oo_save", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_save): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/oo_save) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(out_name, d.transformed_arg0, (int)(sizeof(out_name) / sizeof(out_name[0])));
                    }
                }

                // Best-effort backup (copy previous target -> .bak) before overwriting.
                {
                    CHAR16 bak[120];
                    llmk_make_bak_name(out_name, bak, (int)(sizeof(bak) / sizeof(bak[0])));
                    llmk_copy_file_best_effort(out_name, bak);
                }

                int n = 0;
                EFI_STATUS st = llmk_oo_save_to_file_best_effort(out_name, &n);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to write %s: %r\r\n\r\n", out_name, st);
                } else {
                    Print(L"\r\nOK: wrote %s (%d bytes)\r\n\r\n", out_name, n);
                    llmk_oo_journal_cmd_best_effort("oo_save");
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_load", 8) == 0) {
                const char *name = prompt + 8;
                while (*name == ' ' || *name == '\t') name++;
                CHAR16 in_name[96];
                if (*name == 0) {
                    StrCpy(in_name, oo_state_file);
                } else {
                    ascii_to_char16(in_name, name, (int)(sizeof(in_name) / sizeof(in_name[0])));
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), in_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_LOAD, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "oo_load", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_load): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/oo_load) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(in_name, d.transformed_arg0, (int)(sizeof(in_name) / sizeof(in_name[0])));
                    }
                }

                // Stop auto/exec mode (loading changes entity IDs/state)
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;

                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;

                void *buf = NULL;
                UINTN len = 0;
                EFI_STATUS st = llmk_read_entire_file_best_effort(in_name, &buf, &len);
                CHAR16 bak[120];
                llmk_make_bak_name(in_name, bak, (int)(sizeof(bak) / sizeof(bak[0])));

                if (EFI_ERROR(st)) {
                    // Fallback: try .bak
                    EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
                    if (EFI_ERROR(st2)) {
                        Print(L"\r\nERROR: failed to read %s: %r\r\n\r\n", in_name, st);
                        continue;
                    }
                    int imported = llmk_oo_import((const char *)buf, (int)len);
                    uefi_call_wrapper(BS->FreePool, 1, buf);
                    if (imported < 0) {
                        Print(L"\r\nERROR: parse failed\r\n\r\n");
                    } else {
                        Print(L"\r\nOK: loaded %d entity(s) from %s\r\n\r\n", imported, bak);
                        llmk_oo_journal_cmd_best_effort("oo_load");
                    }
                    continue;
                }

                int imported = llmk_oo_import((const char *)buf, (int)len);
                uefi_call_wrapper(BS->FreePool, 1, buf);

                if (imported < 0) {
                    // Fallback: try .bak
                    EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
                    if (EFI_ERROR(st2)) {
                        Print(L"\r\nERROR: parse failed\r\n\r\n");
                    } else {
                        imported = llmk_oo_import((const char *)buf, (int)len);
                        uefi_call_wrapper(BS->FreePool, 1, buf);
                        if (imported < 0) {
                            Print(L"\r\nERROR: parse failed\r\n\r\n");
                        } else {
                            Print(L"\r\nOK: loaded %d entity(s) from %s\r\n\r\n", imported, bak);
                            llmk_oo_journal_cmd_best_effort("oo_load");
                        }
                    }
                } else {
                    Print(L"\r\nOK: loaded %d entity(s) from %s\r\n\r\n", imported, in_name);
                    llmk_oo_journal_cmd_best_effort("oo_load");
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_think", 9) == 0) {
                // Usage: /oo_think <id> <prompt...>
                int i = 9;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                const char *q = prompt + i;
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_think <id> [prompt]\r\n");
                    Print(L"  Example: /oo_think 1\r\n");
                    Print(L"           /oo_think 1 how should I proceed?\r\n\r\n");
                    continue;
                }

                // Save the user's raw prompt (for logging into entity notes).
                // If empty, use the default question.
                const char *user_q = (q && q[0]) ? q : "next concrete action";
                {
                    int up = 0;
                    for (const char *s = user_q; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                    oo_think_user[up] = 0;
                }

                // Build a compact prompt; includes agenda context.
                char new_prompt[512];
                if (!llmk_oo_build_think_prompt(id, oo_think_user, new_prompt, (int)sizeof(new_prompt))) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }

                // Swap in synthesized prompt for this turn.
                for (int k = 0; k < (int)sizeof(prompt); k++) {
                    prompt[k] = new_prompt[k];
                    if (new_prompt[k] == 0) break;
                }

                Print(L"\r\n[oo] thinking...\r\n");
                llmk_oo_journal_cmd_best_effort("oo_think");

                // Configure capture mode for model output.
                g_capture_mode = 1;
                capture_kind = 2;
                oo_think_id = id;
                llmk_capture_reset();

                // Keep it short and avoid stopping on the "You:" needle.
                stop_on_you = 0;
                stop_on_double_nl = 1;
                if (max_gen_tokens > 96) max_gen_tokens = 96;
                continue;
            } else if (my_strncmp(prompt, "/oo_auto", 8) == 0) {
                // Usage: /oo_auto <id> [n] [prompt...]
                // Runs n cycles of: think (LLM capture) -> store notes -> step -> digest
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;

                int n = 3;
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    n = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        n = n * 10 + (prompt[i] - '0');
                        i++;
                    }
                    while (prompt[i] == ' ') i++;
                }

                const char *q = prompt + i;
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_auto <id> [n] [prompt]\r\n\r\n");
                    continue;
                }

                // Ensure entity exists
                if (!llmk_oo_get_brief(id, NULL, 0, NULL, 0)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }

                if (n < 1) n = 1;
                if (n > 16) n = 16;

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_AUTO, "oo_auto", (UINT32)n, &d);
                    djibion_log_if_observe(&g_djibion, "oo_auto", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_auto): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                // Store the user prompt (optional)
                g_oo_auto_user[0] = 0;
                if (q && q[0]) {
                    int up = 0;
                    for (const char *s = q; *s && up + 1 < (int)sizeof(g_oo_auto_user); s++) g_oo_auto_user[up++] = *s;
                    g_oo_auto_user[up] = 0;
                } else {
                    const char *def = "next concrete action";
                    int up = 0;
                    for (const char *s = def; *s && up + 1 < (int)sizeof(g_oo_auto_user); s++) g_oo_auto_user[up++] = *s;
                    g_oo_auto_user[up] = 0;
                }

                g_oo_auto_active = 1;
                g_oo_auto_id = id;
                g_oo_auto_remaining = n;
                g_oo_auto_total = n;

                // /oo_auto takes over; ensure /oo_exec is off.
                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;

                Print(L"\r\n[oo_auto] started: id=%d cycles=%d\r\n", id, n);
                {
                    CHAR16 p16[260];
                    ascii_to_char16(p16, g_oo_auto_user, (int)(sizeof(p16) / sizeof(p16[0])));
                    Print(L"[oo_auto] prompt: %s\r\n\r\n", p16);
                }

                llmk_oo_journal_cmd_best_effort("oo_auto");

                // The actual cycles will run automatically at the top of the loop.
                continue;
            } else if (my_strncmp(prompt, "/oo_exec", 8) == 0) {
                // Usage: /oo_exec <id> [n] [--plan] [hint...]
                // Runs n cycles consuming agenda actions (marks done). Stops when agenda empty unless --plan.
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;

                int n = 3;
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    n = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        n = n * 10 + (prompt[i] - '0');
                        i++;
                    }
                    while (prompt[i] == ' ') i++;
                }

                int plan_if_empty = 0;
                // Optional flag "--plan" (must appear before hint text)
                if (prompt[i] == '-' && prompt[i + 1] == '-' && prompt[i + 2] == 'p' && prompt[i + 3] == 'l' && prompt[i + 4] == 'a' && prompt[i + 5] == 'n') {
                    plan_if_empty = 1;
                    i += 6;
                    while (prompt[i] == ' ') i++;
                }

                const char *hint = prompt + i;

                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_exec <id> [n] [--plan] [hint]\r\n");
                    Print(L"  Example: /oo_exec 1 5\r\n");
                    Print(L"           /oo_exec <1> 8 --plan\r\n");
                    Print(L"           /oo_exec 1 4 be strict and concise\r\n\r\n");
                    continue;
                }

                // Ensure entity exists
                if (!llmk_oo_get_brief(id, NULL, 0, NULL, 0)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }

                if (n < 1) n = 1;
                if (n > 16) n = 16;

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_EXEC, "oo_exec", (UINT32)n, &d);
                    djibion_log_if_observe(&g_djibion, "oo_exec", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_exec): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                // Store hint (optional)
                g_oo_exec_hint[0] = 0;
                if (hint && hint[0]) {
                    int hp = 0;
                    for (const char *s = hint; *s && hp + 1 < (int)sizeof(g_oo_exec_hint); s++) g_oo_exec_hint[hp++] = *s;
                    g_oo_exec_hint[hp] = 0;
                } else {
                    const char *def = "Execute the action concisely; give concrete steps.";
                    int hp = 0;
                    for (const char *s = def; *s && hp + 1 < (int)sizeof(g_oo_exec_hint); s++) g_oo_exec_hint[hp++] = *s;
                    g_oo_exec_hint[hp] = 0;
                }

                g_oo_exec_active = 1;
                g_oo_exec_id = id;
                g_oo_exec_remaining = n;
                g_oo_exec_total = n;
                g_oo_exec_plan_if_empty = plan_if_empty;

                // /oo_exec takes over; ensure /oo_auto is off.
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;

                Print(L"\r\n[oo_exec] started: id=%d cycles=%d plan_if_empty=%d\r\n", id, n, plan_if_empty);
                {
                    Print(L"[oo_exec] hint: ");
                    llmk_print_ascii(g_oo_exec_hint);
                    Print(L"\r\n\r\n");
                }
                llmk_oo_journal_cmd_best_effort("oo_exec");
                continue;
            } else if (my_strncmp(prompt, "/oo_exec_stop", 13) == 0) {
                if (g_oo_exec_active) {
                    Print(L"\r\n[oo_exec] stopping (id=%d remaining=%d)\r\n\r\n", g_oo_exec_id, g_oo_exec_remaining);
                } else {
                    Print(L"\r\n[oo_exec] not active\r\n\r\n");
                }
                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;
                llmk_oo_journal_cmd_best_effort("oo_exec_stop");
                continue;
            } else if (my_strncmp(prompt, "/oo_auto_stop", 13) == 0) {
                if (g_oo_auto_active) {
                    Print(L"\r\n[oo_auto] stopping (id=%d remaining=%d)\r\n\r\n", g_oo_auto_id, g_oo_auto_remaining);
                } else {
                    Print(L"\r\n[oo_auto] not active\r\n\r\n");
                }
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;
                llmk_oo_journal_cmd_best_effort("oo_auto_stop");
                continue;
            } else if (my_strncmp(prompt, "/oo_consult_mock", 15) == 0) {
                // OO M5 (test/CI): deterministic consult without LLM generation.
                // Usage:
                //   /oo_consult_mock <suggestion>
                int consult_enabled = g_cfg_oo_llm_consult;
                if (consult_enabled < 0) {
                    consult_enabled = g_cfg_oo_enable; // default: follow oo_enable
                }
                if (!consult_enabled) {
                    Print(L"\r\nERROR: OO LLM consult is disabled (oo_llm_consult=0)\r\n\r\n");
                    continue;
                }
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                if (!p || !p[0]) {
                    Print(L"\r\nUsage: /oo_consult_mock <suggestion>\r\n\r\n");
                    continue;
                }

                // 1) Collect system state (best-effort)
                UINT64 ram_mb = llmk_get_conventional_ram_bytes_best_effort() / (1024ULL * 1024ULL);
                UINT32 mode = g_oo_last_mode_valid ? g_oo_last_mode : LLMK_OO_MODE_SAFE;
                UINT64 boots = 0;
                {
                    LlmkOoState s;
                    if (llmk_oo_load_state_best_effort(&s)) {
                        boots = s.boot_count;
                        mode = s.mode;
                    }
                }

                // 2) Sanitize suggestion to ASCII and run the same policy pipeline.
                char sugg[128];
                int sp = 0;
                while (*p && sp + 1 < (int)sizeof(sugg)) {
                    char c = *p++;
                    if (c < 0x20 || c > 0x7E) c = '_';
                    sugg[sp++] = c;
                }
                sugg[sp] = 0;

                Print(L"\r\n[oo_consult_mock] using mock suggestion\r\n\r\n");
                llmk_oo_journal_cmd_best_effort("oo_consult_mock");
                llmk_oo_consult_process_suggestion(ram_mb, mode, boots, config.seq_len, config.seq_len, sugg);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/oo_consult", 11) == 0) {
                // OO M5: LLM consult (suggest system adaptation action).
                // Check prerequisites.
                int consult_enabled = g_cfg_oo_llm_consult;
                if (consult_enabled < 0) {
                    consult_enabled = g_cfg_oo_enable; // default: follow oo_enable
                }
                if (!consult_enabled) {
                    Print(L"\r\nERROR: OO LLM consult is disabled (oo_llm_consult=0)\r\n\r\n");
                    continue;
                }
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }
                if (!g_llmk_ready) {
                    Print(L"\r\nERROR: llmk not ready (no model loaded)\r\n\r\n");
                    continue;
                }
                if (g_loaded_model_format == LLMK_MODEL_FMT_UNKNOWN) {
                    Print(L"\r\nERROR: no model loaded\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_consult] Consulting LLM for system status adaptation...\r\n\r\n");
                llmk_oo_journal_cmd_best_effort("oo_consult");
                llmk_oo_consult_execute(&config, &weights, &state, &tokenizer,
                                       temperature, min_p, top_p, top_k);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/oo_log", 7) == 0) {
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_log] latest summary:\r\n");
                llmk_oo_print_last_consult_status_best_effort();
                Print(L"\r\n[oo_log] OOCONSULT.LOG tail:\r\n");
                llmk_oo_print_ooconsult_tail_best_effort(10);
                Print(L"\r\n");
                llmk_oo_journal_cmd_best_effort("oo_log");
                continue;
            } else if (my_strncmp(prompt, "/oo_explain", 11) == 0) {
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                int verbose = 0;
                int boot_compare = 0;
                const char *p = prompt + 11;
                while (*p == ' ' || *p == '\t') p++;
                if (my_strncmp(p, "verbose", 7) == 0 && (p[7] == 0 || p[7] == ' ' || p[7] == '\t')) verbose = 1;
                else if (my_strncmp(p, "boot", 4) == 0 && (p[4] == 0 || p[4] == ' ' || p[4] == '\t')) boot_compare = 1;
                if (boot_compare) {
                    Print(L"\r\n[oo_explain] boot comparison:\r\n");
                    llmk_oo_explain_boot_best_effort();
                } else {
                    Print(L"\r\n[oo_explain] latest consult:\r\n");
                    llmk_oo_explain_last_consult_best_effort(verbose);
                }
                Print(L"\r\n");
                llmk_oo_journal_cmd_best_effort("oo_explain");
                continue;
            } else if (my_strncmp(prompt, "/oo_jour", 8) == 0 || my_strncmp(prompt, "/oo_journal", 11) == 0) {
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_jour] OOJOUR.LOG tail:\r\n");
                llmk_oo_print_oojour_tail_best_effort(10);
                Print(L"\r\n");
                llmk_oo_journal_cmd_best_effort("oo_jour");
                continue;
            } else if (my_strncmp(prompt, "/oo_outcome", 11) == 0) {
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_outcome] OOOUTCOME.LOG tail:\r\n");
                llmk_oo_print_oooutcome_tail_best_effort(10);
                Print(L"\r\n");
                llmk_oo_journal_cmd_best_effort("oo_outcome");
                continue;
            } else if (my_strncmp(prompt, "/autorun_stop", 13) == 0) {
                if (g_autorun_active) {
                    Print(L"\r\n[autorun] stopping\r\n\r\n");
                    llmk_autorun_stop();
                } else {
                    Print(L"\r\n[autorun] not active\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/autorun", 8) == 0) {
                // Usage:
                //   /autorun [--print] [--shutdown|--no-shutdown] [file]
                // Defaults come from repl.cfg (autorun_file, autorun_shutdown_when_done).
                int do_print = 0;
                int shutdown = g_cfg_autorun_shutdown_when_done;
                CHAR16 in_name[96];
                StrCpy(in_name, g_cfg_autorun_file);

                const char *p = prompt + 8;
                while (*p == ' ' || *p == '\t') p++;

                while (*p) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == 0) break;

                    char tok[96];
                    int tp = 0;
                    while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) {
                        tok[tp++] = *p++;
                    }
                    tok[tp] = 0;
                    if (tok[0] == 0) break;

                    if (llmk_cfg_streq_ci(tok, "--print") || llmk_cfg_streq_ci(tok, "--dry") || llmk_cfg_streq_ci(tok, "--dry-run")) {
                        do_print = 1;
                        continue;
                    }
                    if (llmk_cfg_streq_ci(tok, "--shutdown")) {
                        shutdown = 1;
                        continue;
                    }
                    if (llmk_cfg_streq_ci(tok, "--no-shutdown")) {
                        shutdown = 0;
                        continue;
                    }

                    // First non-flag token is treated as file name.
                    if (tok[0] != '-') {
                        ascii_to_char16(in_name, tok, (int)(sizeof(in_name) / sizeof(in_name[0])));
                        continue;
                    }

                    Print(L"\r\nUsage: /autorun [--print] [--shutdown|--no-shutdown] [file]\r\n\r\n");
                    do_print = -1;
                    break;
                }

                if (do_print == -1) continue;
                if (do_print) {
                    llmk_autorun_print_file_best_effort(in_name, 200);
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), in_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_AUTORUN, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "autorun", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/autorun): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/autorun) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(in_name, d.transformed_arg0, (int)(sizeof(in_name) / sizeof(in_name[0])));
                    }
                }

                if (!llmk_autorun_start(in_name, shutdown)) {
                    Print(L"\r\nERROR: failed to start autorun from %s\r\n\r\n", in_name);
                } else {
                    Print(L"\r\nOK: autorun started from %s (shutdown_when_done=%d)\r\n\r\n", in_name, shutdown);
                }
                continue;
            } else if (my_strncmp(prompt, "/reset", 6) == 0) {
                Print(L"\r\nResetting runtime state...\r\n");
                if (g_llmk_ready) {
                    llmk_reset_runtime_state();
                    Print(L"OK\r\n\r\n");
                } else {
                    Print(L"  (llmk not ready)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/clear", 6) == 0) {
                Print(L"\r\nClearing KV cache...\r\n");
                reset_kv_cache(&state, &config);
                kv_pos = 0;
                g_llmk_kv_pos = kv_pos;
                Print(L"OK: KV cache cleared, context reset\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/version", 8) == 0) {
                Print(L"\r\nllm-baremetal REPL v3\r\n");
                Print(L"  build=%s\r\n", LLMB_BUILD_ID);
                const CHAR16 *shown_model = NULL;
                if (g_loaded_model_path16[0]) shown_model = g_loaded_model_path16;
                else shown_model = model_filename;
                Print(L"  model=%s seq_len=%d kv_pos=%d\r\n", shown_model ? shown_model : L"(unknown)", config.seq_len, kv_pos);
                Print(L"  features=zones+sentinel+log djibmark utf8 multiline persist\r\n");
                Print(L"  hint: /cpu for SIMD, /ctx for config\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diag", 5) == 0) {
                llmk_print_diag();
                continue;
            } else if (my_strncmp(prompt, "/verbose", 8) == 0) {
                int vi = 8;
                while (prompt[vi] == ' ') vi++;
                if (prompt[vi] >= '0' && prompt[vi] <= '2') {
                    g_boot_verbose = prompt[vi] - '0';
                } else {
                    g_boot_verbose = (g_boot_verbose + 1) % 3;
                }
                Print(L"\r\nverbose=%d (%s)\r\n\r\n", g_boot_verbose,
                      g_boot_verbose == 0 ? L"quiet" :
                      g_boot_verbose == 1 ? L"normal" : L"debug");
                continue;
            } else if (my_strncmp(prompt, "/djibmarks", 10) == 0) {
                DJIBMARK_REPL();
                Print(L"\r\nDjibMark Trace (last %d marks):\r\n", (int)djibmark_count());
                Print(L"  Magic: 0x%08X (DJIB2026)\r\n", DJIBMARK_MAGIC);
                Print(L"  Total recorded: %u\r\n", g_djibmark_state.total_marks);
                Print(L"  Enabled: %s\r\n\r\n", g_djibmark_state.enabled ? L"yes" : L"no");
                
                UINT32 count = djibmark_count();
                if (count > 32) count = 32;  // Limit to 32 most recent
                
                Print(L"  Seq      TSC          Phase    Location\r\n");
                Print(L"  -------- ------------ -------- ------------------\r\n");
                for (UINT32 i = 0; i < count; i++) {
                    DjibMark* m = djibmark_get(i);
                    if (!m || m->magic != DJIBMARK_MAGIC) continue;
                    
                    // Convert CHAR8* to print char by char
                    Print(L"  %08u %012lu %-8s ", m->sequence, m->timestamp_tsc, djibmark_phase_name(m->phase));
                    if (m->location) {
                        for (const CHAR8* p = m->location; *p; p++) {
                            Print(L"%c", (CHAR16)*p);
                        }
                    }
                    Print(L":%u\r\n", m->line);
                }
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/djibperf", 9) == 0) {
                DJIBMARK_REPL();
                Print(L"\r\nDjibMark Performance Analysis:\r\n\r\n");
                
                UINT32 count = djibmark_count();
                if (count < 2) {
                    Print(L"  Need at least 2 marks for analysis\r\n\r\n");
                    continue;
                }
                
                // Analyze phase transitions
                UINT64 prefill_cycles = 0, decode_cycles = 0;
                UINT32 prefill_count = 0, decode_count = 0;
                
                for (UINT32 i = 1; i < count && i < 128; i++) {
                    DjibMark* curr = djibmark_get(i-1);
                    DjibMark* prev = djibmark_get(i);
                    if (!curr || !prev) continue;
                    if (curr->magic != DJIBMARK_MAGIC || prev->magic != DJIBMARK_MAGIC) continue;
                    
                    UINT64 delta = (curr->timestamp_tsc > prev->timestamp_tsc) 
                                   ? (curr->timestamp_tsc - prev->timestamp_tsc) : 0;
                    
                    if (curr->phase == DJIBMARK_PHASE_PREFILL) {
                        prefill_cycles += delta;
                        prefill_count++;
                    } else if (curr->phase == DJIBMARK_PHASE_DECODE) {
                        decode_cycles += delta;
                        decode_count++;
                    }
                }
                
                Print(L"  Prefill phase:\r\n");
                Print(L"    Count: %u marks\r\n", prefill_count);
                Print(L"    Total cycles: %lu\r\n", prefill_cycles);
                if (prefill_count > 0) {
                    Print(L"    Avg cycles/mark: %lu\r\n", prefill_cycles / prefill_count);
                }
                
                Print(L"\r\n  Decode phase:\r\n");
                Print(L"    Count: %u marks\r\n", decode_count);
                Print(L"    Total cycles: %lu\r\n", decode_cycles);
                if (decode_count > 0) {
                    Print(L"    Avg cycles/mark: %lu\r\n", decode_cycles / decode_count);
                }
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/djibion_on", 10) == 0) {
                djibion_set_mode(&g_djibion, DJIBION_MODE_OBSERVE);
                Print(L"\r\nOK: Djibion mode=%s\r\n\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                continue;
            } else if (my_strncmp(prompt, "/djibion_off", 11) == 0) {
                djibion_set_mode(&g_djibion, DJIBION_MODE_OFF);
                Print(L"\r\nOK: Djibion mode=%s\r\n\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                continue;
            } else if (my_strncmp(prompt, "/djibion_enforce", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                djibion_set_mode(&g_djibion, (DjibionMode)v);
                Print(L"\r\nOK: Djibion mode=%s\r\n\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                continue;
            } else if (my_strncmp(prompt, "/djibion_status", 14) == 0) {
                Print(L"\r\n[Djibion]\r\n");
                Print(L"  mode=%s\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                Print(L"  laws: max_fs_write_bytes=%d allow_fs_write=%d allow_fs_delete=%d\r\n",
                      (int)g_djibion.laws.max_fs_write_bytes,
                      (int)g_djibion.laws.allow_fs_write,
                      (int)g_djibion.laws.allow_fs_delete);
                Print(L"  laws: max_snap_bytes=%d allow_snap_load=%d allow_snap_save=%d\r\n",
                    (int)g_djibion.laws.max_snap_bytes,
                    (int)g_djibion.laws.allow_snap_load,
                    (int)g_djibion.laws.allow_snap_save);
                Print(L"  laws: allow_cfg_write=%d\r\n", (int)g_djibion.laws.allow_cfg_write);
                Print(L"  laws: max_oo_cycles=%d allow_oo_exec=%d allow_oo_auto=%d allow_autorun=%d\r\n",
                      (int)g_djibion.laws.max_oo_cycles,
                      (int)g_djibion.laws.allow_oo_exec,
                      (int)g_djibion.laws.allow_oo_auto,
                      (int)g_djibion.laws.allow_autorun);
                    Print(L"  laws: allow_oo_persist=%d\r\n", (int)g_djibion.laws.allow_oo_persist);
                {
                    CHAR16 pfx[80];
                    ascii_to_char16(pfx, g_djibion.laws.fs_mut_prefix, (int)(sizeof(pfx) / sizeof(pfx[0])));
                    Print(L"  laws: fs_mut_prefix=%s\r\n", pfx[0] ? pfx : L"(none)");
                }
                Print(L"  decisions: total=%d rejected=%d transformed=%d\r\n\r\n",
                      (int)g_djibion.decisions_total,
                      (int)g_djibion.decisions_rejected,
                      (int)g_djibion.decisions_transformed);
                continue;
            } else if (my_strncmp(prompt, "/djibion_prefix", 14) == 0) {
                const char *p = prompt + 14;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /djibion_prefix <prefix>\r\n");
                    Print(L"  Example: /djibion_prefix \\test_dir\\\r\n\r\n");
                    continue;
                }
                llmk_ascii_copy_cap(g_djibion.laws.fs_mut_prefix, (int)sizeof(g_djibion.laws.fs_mut_prefix), p);
                Print(L"\r\nOK: fs_mut_prefix=");
                llmk_print_ascii(g_djibion.laws.fs_mut_prefix);
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_delete", 20) == 0) {
                const char *p = prompt + 20;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_fs_delete = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_fs_delete=%d\r\n\r\n", (int)g_djibion.laws.allow_fs_delete);
                continue;
            } else if (my_strncmp(prompt, "/djibion_max_write", 16) == 0) {
                const char *p = prompt + 16;
                while (*p == ' ' || *p == '\t') p++;
                UINT32 v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (UINT32)(*p - '0'); p++; }
                if (v < 256) v = 256;
                g_djibion.laws.max_fs_write_bytes = v;
                Print(L"\r\nOK: max_fs_write_bytes=%d\r\n\r\n", (int)g_djibion.laws.max_fs_write_bytes);
                continue;
            } else if (my_strncmp(prompt, "/djibion_max_oo", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;
                UINT32 v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (UINT32)(*p - '0'); p++; }
                if (v < 1) v = 1;
                if (v > 64) v = 64;
                g_djibion.laws.max_oo_cycles = v;
                Print(L"\r\nOK: max_oo_cycles=%d\r\n\r\n", (int)g_djibion.laws.max_oo_cycles);
                continue;
            } else if (my_strncmp(prompt, "/djibion_max_snap", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                UINT32 v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (UINT32)(*p - '0'); p++; }
                if (v < (1024 * 1024)) v = (1024 * 1024);
                g_djibion.laws.max_snap_bytes = v;
                Print(L"\r\nOK: max_snap_bytes=%d\r\n\r\n", (int)g_djibion.laws.max_snap_bytes);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_snap_load", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_snap_load = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_snap_load=%d\r\n\r\n", (int)g_djibion.laws.allow_snap_load);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_snap_save", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_snap_save = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_snap_save=%d\r\n\r\n", (int)g_djibion.laws.allow_snap_save);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_cfg_write", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_cfg_write = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_cfg_write=%d\r\n\r\n", (int)g_djibion.laws.allow_cfg_write);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_autorun", 21) == 0) {
                const char *p = prompt + 21;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_autorun = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_autorun=%d\r\n\r\n", (int)g_djibion.laws.allow_autorun);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_oo_persist", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_oo_persist = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_oo_persist=%d\r\n\r\n", (int)g_djibion.laws.allow_oo_persist);
                continue;
            } else if (my_strncmp(prompt, "/diopion_on", 10) == 0) {
                diopion_set_mode(&g_diopion, DIOPION_MODE_OBSERVE);
                Print(L"\r\nOK: Diopion mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_off", 11) == 0) {
                // Stop any active burst and restore knobs immediately.
                if (g_diopion_burst_active) {
                    g_diopion_burst_remaining = 0;
                    llmk_diopion_burst_finish_one(&max_gen_tokens, &top_k, &temperature);
                }
                diopion_set_mode(&g_diopion, DIOPION_MODE_OFF);
                Print(L"\r\nOK: Diopion mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_enforce", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                diopion_set_mode(&g_diopion, (DiopionMode)v);
                Print(L"\r\nOK: Diopion mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_profile", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /diopion_profile <none|animal|vegetal|geom|bio>\r\n\r\n");
                    continue;
                }
                if (llmk_cfg_streq_ci(p, "animal")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_ANIMAL);
                else if (llmk_cfg_streq_ci(p, "vegetal")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_VEGETAL);
                else if (llmk_cfg_streq_ci(p, "geom") || llmk_cfg_streq_ci(p, "geometric")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_GEOM);
                else if (llmk_cfg_streq_ci(p, "bio") || llmk_cfg_streq_ci(p, "biological")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_BIO);
                else diopion_set_profile(&g_diopion, DIOPION_PROFILE_NONE);
                Print(L"\r\nOK: Diopion profile=");
                llmk_print_ascii(diopion_profile_name_ascii(g_diopion.profile));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_status", 14) == 0) {
                Print(L"\r\n[Diopion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L" profile=");
                llmk_print_ascii(diopion_profile_name_ascii(g_diopion.profile));
                Print(L"\r\n");
                Print(L"  burst_defaults: turns=%d max_tokens=%d top_k=%d temp=%d.%03d\r\n",
                      (int)g_diopion.params.burst_turns_default,
                      (int)g_diopion.params.burst_max_gen_tokens,
                      (int)g_diopion.params.burst_top_k,
                      (int)(g_diopion.params.burst_temp_milli / 1000u),
                      (int)(g_diopion.params.burst_temp_milli % 1000u));
                Print(L"  bursts_started=%d\r\n", (int)g_diopion.bursts_started);
                Print(L"  burst_active=%d remaining=%d\r\n\r\n", g_diopion_burst_active, g_diopion_burst_remaining);
                continue;
            } else if (my_strncmp(prompt, "/diopion_burst", 13) == 0) {
                if (g_diopion.mode == DIOPION_MODE_OFF) {
                    Print(L"\r\nERROR: Diopion is off (use /diopion_on)\r\n\r\n");
                    continue;
                }

                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;

                // Args: [turns] [temp_milli] [top_k] [max_tokens]
                UINT32 turns = g_diopion.params.burst_turns_default;
                UINT32 temp_milli = g_diopion.params.burst_temp_milli;
                UINT32 topk = g_diopion.params.burst_top_k;
                UINT32 max_tokens = g_diopion.params.burst_max_gen_tokens;

                int argc = 0;
                while (*p && argc < 4) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == 0) break;
                    UINT32 v = 0;
                    int any = 0;
                    while (*p >= '0' && *p <= '9') { v = v * 10u + (UINT32)(*p - '0'); p++; any = 1; }
                    if (!any) break;
                    if (argc == 0) turns = v;
                    else if (argc == 1) temp_milli = v;
                    else if (argc == 2) topk = v;
                    else if (argc == 3) max_tokens = v;
                    argc++;
                }

                if (turns < 1) turns = 1;
                if (turns > 16) turns = 16;
                if (temp_milli < 50) temp_milli = 50;
                if (temp_milli > 2000) temp_milli = 2000;
                if (topk < 1) topk = 1;
                if (topk > 200) topk = 200;
                if (max_tokens < 16) max_tokens = 16;
                if (max_tokens > 1024) max_tokens = 1024;

                llmk_diopion_burst_apply(turns, max_tokens, topk, temp_milli, &max_gen_tokens, &top_k, &temperature);
                g_diopion.bursts_started++;

                Print(L"\r\nOK: burst turns=%d temp=%d.%03d top_k=%d max_tokens=%d\r\n\r\n",
                      (int)turns,
                      (int)(temp_milli / 1000u),
                      (int)(temp_milli % 1000u),
                      (int)topk,
                      (int)max_tokens);
                continue;
            } else if (my_strncmp(prompt, "/commands", 9) == 0) {
                char pref[64];
                pref[0] = 0;
                llmk_parse_optional_prefix(prompt, 9, pref, (int)sizeof(pref));

                Print(L"\r\nCommands:\r\n");
                if (pref[0]) {
                    Print(L"  (filter: ");
                    llmk_print_ascii(pref);
                    Print(L")\r\n");
                }
                llmk_print_commands_filtered(pref[0] ? pref : NULL);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/cls", 4) == 0) {
                uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
                continue;
            } else if (my_strncmp(prompt, "/logo", 5) == 0) {
                llmk_print_logo();
                continue;
            } else if (my_strncmp(prompt, "/blas_bench", 11) == 0) {
                Print(L"\r\nRunning DjibLAS Benchmark (256x256)...\r\n");
                int M=256, N=256, K=256;
                // Use simple_alloc (monotonic) - suitable for a test command if memory allows
                float *A = (float*)simple_alloc(M*K*sizeof(float));
                float *B = (float*)simple_alloc(K*N*sizeof(float));
                float *C_sc = (float*)simple_alloc(M*N*sizeof(float));
                float *C_avx = (float*)simple_alloc(M*N*sizeof(float));

                if (!A || !B || !C_sc || !C_avx) {
                    Print(L"Benchmark aborted: Alloc failed\r\n");
                    continue;
                }

                // Init with deterministic values
                for(int i=0; i<M*K; i++) A[i] = (float)((i % 17) - 8) * 0.1f;
                for(int i=0; i<K*N; i++) B[i] = (float)((i % 19) - 9) * 0.1f;

                // 1. Scalar Baseline
                unsigned long long t0 = rdtsc();
                djiblas_sgemm_scalar(M, N, K, A, K, B, N, C_sc, N);
                unsigned long long t_scalar = rdtsc() - t0;
                Print(L"Scalar: %lu cycles\r\n", t_scalar);

                // 2. AVX2
                CPUFeatures f;
                djiblas_detect_cpu(&f);
                if (f.has_avx2 && f.has_fma) {
                    t0 = rdtsc();
                    djiblas_sgemm_avx2(M, N, K, A, K, B, N, C_avx, N);
                    unsigned long long t_avx = rdtsc() - t0;
                    
                    int speedup = (int)(t_scalar / t_avx);
                    int dec = (int)(((t_scalar * 10) / t_avx) % 10);
                    Print(L"AVX2:   %lu cycles (Speedup: %d.%dx)\r\n", t_avx, speedup, dec);

                    // Verify
                    float max_err = 0.0f;
                    for(int i=0; i<M*N; i++) {
                        float d = C_sc[i] - C_avx[i];
                        if (d < 0) d = -d;
                        if (d > max_err) max_err = d;
                    }
                    Print(L"Max Error: %d.%06d\r\n", (int)max_err, (int)((max_err - (int)max_err)*1000000));
                } else {
                    Print(L"AVX2:   Skipped (Not Supported)\r\n");
                }
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/q8_bench", 9) == 0) {
                // Usage:
                //   /q8_bench           -> default n=d=256, reps=10
                //   /q8_bench <n> <d>   -> custom sizes (n must be multiple of 32)
                //   /q8_bench <n> <d> <reps>
                int n = 256;
                int d = 256;
                int reps = 10;

                int i = 9;
                while (prompt[i] == ' ') i++;
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    n = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { n = n * 10 + (prompt[i] - '0'); i++; }
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    d = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { d = d * 10 + (prompt[i] - '0'); i++; }
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    reps = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { reps = reps * 10 + (prompt[i] - '0'); i++; }
                }
                if (reps < 1) reps = 1;
                if (reps > 100) reps = 100;

                if ((n % 32) != 0 || n <= 0 || d <= 0) {
                    Print(L"\r\nUsage: /q8_bench [n multiple-of-32] [d] [reps]\r\n\r\n");
                    continue;
                }

                Print(L"\r\nRunning Q8_0 matmul benchmark (n=%d d=%d reps=%d)...\r\n", n, d, reps);

                UINT64 row_bytes = llmk_q8_0_row_bytes(n);
                if (row_bytes == 0) {
                    Print(L"ERROR: invalid Q8 row_bytes\r\n\r\n");
                    continue;
                }

                float *x = (float*)simple_alloc((UINTN)n * sizeof(float));
                UINT8 *wq8 = (UINT8*)simple_alloc((UINTN)d * (UINTN)row_bytes);
                float *y_sc = (float*)simple_alloc((UINTN)d * sizeof(float));
                float *y_avx = (float*)simple_alloc((UINTN)d * sizeof(float));
                if (!x || !wq8 || !y_sc || !y_avx) {
                    Print(L"Benchmark aborted: Alloc failed\r\n\r\n");
                    continue;
                }

                // Init deterministic input vector
                for (int j = 0; j < n; j++) {
                    x[j] = (float)(((j * 13) % 97) - 48) * 0.01f;
                }

                // Init deterministic Q8_0 weights: fp16 scale = 1.0 (0x3C00) and int8 values.
                // Layout per block: [u16 d][32 i8 qs] = 34 bytes.
                for (int r = 0; r < d; r++) {
                    UINT8 *row = wq8 + (UINTN)r * (UINTN)row_bytes;
                    UINT8 *p = row;
                    int nb = n / 32;
                    for (int b = 0; b < nb; b++) {
                        // d = 1.0f in fp16
                        p[0] = 0x00;
                        p[1] = 0x3C; // SAFE: row points to at least 34 bytes (block layout)
                        INT8 *qs = (INT8 *)(p + 2);
                        for (int k = 0; k < 32; k++) {
                            int v = (r * 31 + b * 17 + k * 7) & 255;
                            v -= 128;
                            if (v < -127) v = -127;
                            if (v > 127) v = 127;
                            qs[k] = (INT8)v;
                        }
                        p += 34;
                    }
                }

                // Scalar baseline
                unsigned long long best_sc = ~0ULL;
                for (int it = 0; it < reps; it++) {
                    unsigned long long t0 = rdtsc();
                    matmul_q8_0_scalar(y_sc, x, wq8, n, d);
                    unsigned long long dt = rdtsc() - t0;
                    if (dt < best_sc) best_sc = dt;
                }
                Print(L"Q8 scalar: %lu cycles (best of %d)\r\n", best_sc, reps);

                // AVX2 (if supported)
                CPUFeatures f;
                djiblas_detect_cpu(&f);
                if (f.has_avx2) {
                    unsigned long long best_avx = ~0ULL;
                    for (int it = 0; it < reps; it++) {
                        unsigned long long t0 = rdtsc();
                        if (g_cfg_q8_act_quant != 0) {
                            matmul_q8_0_avx2_i8(y_avx, x, wq8, n, d);
                        } else {
                            matmul_q8_0_avx2(y_avx, x, wq8, n, d);
                        }
                        unsigned long long dt = rdtsc() - t0;
                        if (dt < best_avx) best_avx = dt;
                    }

                    int speedup = (best_avx > 0) ? (int)(best_sc / best_avx) : 0;
                    int dec = (best_avx > 0) ? (int)(((best_sc * 10ULL) / best_avx) % 10ULL) : 0;
                    Print(L"Q8 AVX2%s:   %lu cycles (Speedup: %d.%dx)\r\n", (g_cfg_q8_act_quant != 0) ? L"(i8)" : L"", best_avx, speedup, dec);

                    // Verify (loose tolerance, since AVX path accumulates in float too).
                    float max_err = 0.0f;
                    for (int t = 0; t < d; t++) {
                        float diff = y_sc[t] - y_avx[t];
                        if (diff < 0) diff = -diff;
                        if (diff > max_err) max_err = diff;
                    }
                    Print(L"Max Error: %d.%06d\r\n", (int)max_err, (int)((max_err - (int)max_err) * 1000000));
                } else {
                    Print(L"Q8 AVX2:   Skipped (Not Supported)\r\n");
                }

                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/q8_matvec", 10) == 0) {
                // Bench a *real* model matrix-vector multiply when running in Q8_0 blob mode.
                // Usage:
                //   /q8_matvec                 -> wq layer0, reps=20
                //   /q8_matvec <name>          -> e.g. wq|wk|wv|wo|w1|w2|w3|cls
                //   /q8_matvec <name> <layer>  -> selects layer for per-layer matrices
                //   /q8_matvec <name> <layer> <reps>
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (weights.kind != 1) {
                    Print(L"\r\nERROR: /q8_matvec requires GGUF Q8_0 blob mode (weights_kind=q8_0_blob).\r\n");
                    Print(L"Tip: set gguf_q8_blob=1 in repl.cfg and load a Q8_0 GGUF.\r\n\r\n");
                    continue;
                }

                char name[8]; // SAFE: short matrix name token parsed with bounds check
                name[0] = 'w'; name[1] = 'q'; name[2] = 0;
                int layer = 0;
                int reps = 20;

                int i = 10;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    int n = 0;
                    while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(name)) {
                        name[n++] = prompt[i++];
                    }
                    name[n] = 0;
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    layer = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { layer = layer * 10 + (prompt[i] - '0'); i++; }
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    reps = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { reps = reps * 10 + (prompt[i] - '0'); i++; }
                }
                if (reps < 1) reps = 1;
                if (reps > 100) reps = 100;
                if (layer < 0) layer = 0;
                if (layer >= config.n_layers) layer = config.n_layers - 1;

                // Select matrix pointer + shape.
                const UINT8 *W = NULL;
                int n_in = 0;
                int d_out = 0;
                const char *kind = "";

                const int dim = config.dim;
                const int hidden_dim = config.hidden_dim;
                const int kv_dim = (dim * config.n_kv_heads) / config.n_heads;

                if (my_strncmp(name, "wq", 2) == 0) {
                    W = weights.wq_q8 + (UINTN)layer * (UINTN)weights.wq_layer_bytes;
                    n_in = dim; d_out = dim; kind = "wq";
                } else if (my_strncmp(name, "wk", 2) == 0) {
                    W = weights.wk_q8 + (UINTN)layer * (UINTN)weights.wk_layer_bytes;
                    n_in = dim; d_out = kv_dim; kind = "wk";
                } else if (my_strncmp(name, "wv", 2) == 0) {
                    W = weights.wv_q8 + (UINTN)layer * (UINTN)weights.wv_layer_bytes;
                    n_in = dim; d_out = kv_dim; kind = "wv";
                } else if (my_strncmp(name, "wo", 2) == 0) {
                    W = weights.wo_q8 + (UINTN)layer * (UINTN)weights.wo_layer_bytes;
                    n_in = dim; d_out = dim; kind = "wo";
                } else if (my_strncmp(name, "w1", 2) == 0) {
                    W = weights.w1_q8 + (UINTN)layer * (UINTN)weights.w1_layer_bytes;
                    n_in = dim; d_out = hidden_dim; kind = "w1";
                } else if (my_strncmp(name, "w2", 2) == 0) {
                    W = weights.w2_q8 + (UINTN)layer * (UINTN)weights.w2_layer_bytes;
                    n_in = hidden_dim; d_out = dim; kind = "w2";
                } else if (my_strncmp(name, "w3", 2) == 0) {
                    W = weights.w3_q8 + (UINTN)layer * (UINTN)weights.w3_layer_bytes;
                    n_in = dim; d_out = hidden_dim; kind = "w3";
                } else if (my_strncmp(name, "cls", 3) == 0) {
                    W = weights.wcls_q8;
                    n_in = dim; d_out = config.vocab_size; kind = "cls";
                } else {
                    Print(L"\r\nUsage: /q8_matvec [wq|wk|wv|wo|w1|w2|w3|cls] [layer] [reps]\r\n\r\n");
                    continue;
                }

                if (!W || n_in <= 0 || d_out <= 0) {
                    Print(L"\r\nERROR: matrix not available for %a\r\n\r\n", kind);
                    continue;
                }
                if ((n_in % 32) != 0) {
                    Print(L"\r\nERROR: Q8_0 matvec requires n multiple of 32 (n=%d)\r\n\r\n", n_in);
                    continue;
                }

                // Allocate input/output
                float *x = (float*)simple_alloc((UINTN)n_in * sizeof(float));
                float *y_sc = (float*)simple_alloc((UINTN)d_out * sizeof(float));
                float *y_avx = (float*)simple_alloc((UINTN)d_out * sizeof(float));
                if (!x || !y_sc || !y_avx) {
                    Print(L"\r\nERROR: alloc failed\r\n\r\n");
                    continue;
                }

                for (int t = 0; t < n_in; t++) {
                    x[t] = (float)(((t * 29) % 101) - 50) * 0.01f;
                }

                Print(L"\r\nQ8 matvec (%a", kind);
                if (kind[0] == 'w') Print(L" layer=%d", layer);
                Print(L") n=%d d=%d reps=%d\r\n", n_in, d_out, reps);

                unsigned long long best_sc = ~0ULL;
                for (int it = 0; it < reps; it++) {
                    unsigned long long t0 = rdtsc();
                    matmul_q8_0_scalar(y_sc, x, W, n_in, d_out);
                    unsigned long long dt = rdtsc() - t0;
                    if (dt < best_sc) best_sc = dt;
                }
                Print(L"Scalar: %lu cycles (%.2f cyc/out)\r\n", best_sc, (double)best_sc / (double)d_out);

                CPUFeatures f;
                djiblas_detect_cpu(&f);
                if (f.has_avx2) {
                    int allow_i8 = 0;
                    if (g_cfg_q8_act_quant == 1) {
                        allow_i8 = 1;
                    } else if (g_cfg_q8_act_quant == 2) {
                        // Hybrid mode: enable i8 dot only for FFN matrices.
                        if (kind[0] == 'w' && kind[2] == 0 && (kind[1] == '1' || kind[1] == '2' || kind[1] == '3')) {
                            allow_i8 = 1;
                        }
                    }
                    unsigned long long best_avx = ~0ULL;
                    for (int it = 0; it < reps; it++) {
                        unsigned long long t0 = rdtsc();
                        if (allow_i8) {
                            matmul_q8_0_avx2_i8(y_avx, x, W, n_in, d_out);
                        } else {
                            matmul_q8_0_avx2(y_avx, x, W, n_in, d_out);
                        }
                        unsigned long long dt = rdtsc() - t0;
                        if (dt < best_avx) best_avx = dt;
                    }
                    int speedup = (best_avx > 0) ? (int)(best_sc / best_avx) : 0;
                    int dec = (best_avx > 0) ? (int)(((best_sc * 10ULL) / best_avx) % 10ULL) : 0;
                    Print(L"AVX2%s:   %lu cycles (%.2f cyc/out, %d.%dx)\r\n", allow_i8 ? L"(i8)" : L"", best_avx, (double)best_avx / (double)d_out, speedup, dec);

                    float max_err = 0.0f;
                    for (int t = 0; t < d_out; t++) {
                        float diff = y_sc[t] - y_avx[t];
                        if (diff < 0) diff = -diff;
                        if (diff > max_err) max_err = diff;
                    }
                    Print(L"Max Error: %d.%06d\r\n", (int)max_err, (int)((max_err - (int)max_err) * 1000000));
                } else {
                    Print(L"AVX2:   Skipped (Not Supported)\r\n");
                }

                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/help", 5) == 0) {
                char pref[64];
                pref[0] = 0;
                llmk_parse_optional_prefix(prompt, 5, pref, (int)sizeof(pref));

                llmk_print_help_filtered(
                    pref[0] ? pref : NULL,
                    temperature, min_p, top_p,
                    top_k, no_repeat_ngram, max_gen_tokens,
                    stats_enabled, stop_on_you, stop_on_double_nl,
                    repeat_penalty
                );
                continue;
            } else if (my_strncmp(prompt, "/metrics", 8) == 0) {
                // Export runtime metrics to LLMK_METRICS.LOG (JSON format)
                EFI_FILE_HANDLE metrics_file = NULL;
                EFI_STATUS metrics_st = llmk_open_binary_file(&metrics_file, L"LLMK_METRICS.LOG");

                if (!EFI_ERROR(metrics_st) && metrics_file) {
                    // Build JSON string manually (no sprintf in UEFI)
                    char json_buf[2048];
                    int jpos = 0;

                    // Helper macro to append string
                    #define JAPPEND(s) do { const char *_s = (s); while (*_s && jpos < (int)sizeof(json_buf)-1) json_buf[jpos++] = *_s++; } while(0)
                    #define JAPPEND_U64(label, val) do { \
                        JAPPEND("  \""); JAPPEND(label); JAPPEND("\": "); \
                        char _tmp[32]; llmk_u64_to_str(val, _tmp, sizeof(_tmp)); JAPPEND(_tmp); JAPPEND(",\n"); \
                    } while(0)

                    JAPPEND("{\n");
                    JAPPEND_U64("session_start_cycles", g_metrics.session_start_cycles);
                    JAPPEND_U64("total_prefill_cycles", g_metrics.total_prefill_cycles);
                    JAPPEND_U64("total_decode_cycles", g_metrics.total_decode_cycles);
                    JAPPEND_U64("total_prefill_tokens", g_metrics.total_prefill_tokens);
                    JAPPEND_U64("total_decode_tokens", g_metrics.total_decode_tokens);
                    JAPPEND_U64("total_prefill_calls", g_metrics.total_prefill_calls);
                    JAPPEND_U64("total_decode_calls", g_metrics.total_decode_calls);
                    JAPPEND_U64("last_prefill_cycles", g_metrics.last_prefill_cycles);
                    JAPPEND_U64("last_decode_cycles", g_metrics.last_decode_cycles);
                    JAPPEND_U64("last_prefill_tokens", g_metrics.last_prefill_tokens);
                    JAPPEND_U64("last_decode_tokens", g_metrics.last_decode_tokens);
                    JAPPEND_U64("sentinel_violations_total", g_metrics.sentinel_violations_total);
                    JAPPEND_U64("kv_cache_resets", g_metrics.kv_cache_resets);
                    JAPPEND_U64("generation_count", g_metrics.generation_count);

                    // Remove trailing comma + newline before closing brace
                    if (jpos >= 2 && json_buf[jpos-2] == ',' && json_buf[jpos-1] == '\n') {
                        jpos -= 2;
                    }
                    JAPPEND("\n}\n");
                    json_buf[jpos] = 0;

                    #undef JAPPEND
                    #undef JAPPEND_U64

                    metrics_st = llmk_file_write_bytes(metrics_file, json_buf, (UINTN)jpos);
                    uefi_call_wrapper(metrics_file->Close, 1, metrics_file);

                    if (!EFI_ERROR(metrics_st)) {
                        Print(L"OK: Metrics exported to LLMK_METRICS.LOG (%d bytes)\r\n", jpos);
                    } else {
                        Print(L"WARN: Metrics file write failed (status=%lx)\r\n", metrics_st);
                    }
                } else {
                    Print(L"WARN: Cannot open LLMK_METRICS.LOG for writing (status=%lx)\r\n", metrics_st);
                }
                continue;
            } else if (my_strncmp(prompt, "/bench_begin", 12) == 0) {
                // Begin benchmark JSONL capture (default: LLMK_BENCH.JSONL)
                const char *s = prompt + 12;
                while (*s == ' ' || *s == '\t') s++;

                char name_ascii[64];
                int n = 0;
                while (*s && *s != ' ' && *s != '\t' && n + 1 < (int)sizeof(name_ascii)) name_ascii[n++] = *s++;
                name_ascii[n] = 0;

                CHAR16 out16[64];
                out16[0] = 0;
                if (name_ascii[0]) {
                    ascii_to_char16(out16, name_ascii, (int)(sizeof(out16) / sizeof(out16[0])));
                }
                (void)llmk_bench_begin_best_effort(name_ascii[0] ? out16 : NULL);
                continue;
            } else if (my_strncmp(prompt, "/bench_end", 10) == 0) {
                llmk_bench_end_best_effort();
                continue;
            } else if (my_strncmp(prompt, "/autotune_status", 16) == 0) {
                llmk_autotune_print_status(temperature, top_p, top_k, max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/guard_status", 13) == 0) {
                llmk_guardrails_print_status(temperature, top_p, top_k, max_gen_tokens);
                continue;
            }
        }
        
        // Encode prompt
        // For normal chat turns, wrap input so the model sees explicit roles.
        // Keep /commands and capture-mode prompts untouched.
        const char *encode_text = prompt;
        char model_prompt[1024];
        model_prompt[0] = 0;
        if (!g_capture_mode && !draw_mode && prompt[0] != '/' && prompt[0] != 0) {
            encode_text = llmk_build_chat_prompt(model_prompt, (int)sizeof(model_prompt), prompt, kv_pos);
        }

        int prompt_tokens[384];
        int n_prompt_tokens = 0;
        encode((char *)encode_text, prompt_tokens, &n_prompt_tokens, (int)(sizeof(prompt_tokens) / sizeof(prompt_tokens[0])), &tokenizer);

        // Avoid injecting BOS into the middle of an ongoing conversation.
        if (kv_pos > 0 && n_prompt_tokens > 0 && prompt_tokens[0] == TOKEN_BOS) {
            for (int i = 1; i < n_prompt_tokens; i++) prompt_tokens[i - 1] = prompt_tokens[i];
            n_prompt_tokens--;
        }
        
        // Check if KV cache will overflow
        if (kv_pos + n_prompt_tokens + max_gen_tokens > config.seq_len) {
            Print(L"\r\nWARNING: context too long (%d + %d tokens), clearing KV cache\r\n", 
                  kv_pos, n_prompt_tokens + max_gen_tokens);
            reset_kv_cache(&state, &config);
            kv_pos = 0;
            g_llmk_kv_pos = kv_pos;
        }
        
        if (!g_capture_mode) {
            Print(L"AI: ");
        }

        if (g_llmk_ready) {
            // Reset per-generation overrun counters and print current budget state.
            g_budget_overruns_prefill = 0;
            g_budget_overruns_decode = 0;
            if (!g_capture_mode) {
                Print(L"\r\n[llmk][budget] prefill_max=%lu decode_max=%lu\r\n",
                      g_budget_prefill_cycles, g_budget_decode_cycles);
            }
        }

        if (!g_capture_mode && !draw_mode) {
            llmk_guardrails_apply_safe_caps(&temperature, &top_p, &top_k, &max_gen_tokens, 1);
        }

        // M19.1: capture wall-clock start for benchmark cases.
        llmk_bench_on_turn_start();

        ConscienceSample cs = {0};
        conscience_sample(&g_conscience, &cs);

        UINT64 m18_decode_cycles_start = g_metrics.total_decode_cycles;
        UINT32 m18_decode_tokens_start = g_metrics.total_decode_tokens;
        int m181_guard_trip = 0;
        
        // Process prompt tokens through model first (prefill)
        for (int i = 0; i < n_prompt_tokens; i++) {
            int pos = kv_pos + i;  // Use persistent KV position
            if (g_llmk_ready) {
                // Per-token prefill budgeting (pos-dependent): set budget before each forward.
                if (g_budget_prefill_cycles == 0) {
                    // Start huge to ensure we get a first measurement without tripping.
                    // llmk_budget_update() will snap down quickly after the first dt sample.
                    g_budget_prefill_cycles = 100000000000ULL;
                }
                g_sentinel.cfg.max_cycles_prefill = g_budget_prefill_cycles;
                llmk_sentinel_phase_start(&g_sentinel, LLMK_PHASE_PREFILL);
                transformer_forward(&state, &weights, &config, prompt_tokens[i], pos);
                BOOLEAN ok = llmk_sentinel_phase_end(&g_sentinel);
                if (g_sentinel.tripped) {
                    immunion_record(&g_immunion, IMMUNION_THREAT_OOBCheck, (uint32_t)g_sentinel.last_error, 80);
                    Print(L"\r\n[llmk] prefill stopped (fail-safe) at i=%d\r\n", i);
                    llmk_print_ctx(&config, model_filename, kv_pos, temperature, min_p, top_p, top_k, no_repeat_ngram, repeat_penalty, max_gen_tokens);
                    llmk_zones_print(&g_zones);
                    llmk_sentinel_print_status(&g_sentinel);
                    llmk_print_log(32);

                    // Best-effort: persist dump to file for offline diagnosis.
                    {
                        EFI_FILE_HANDLE f = NULL;
                        EFI_STATUS st = llmk_open_text_file(&f, L"llmk-failsafe.txt");
                        if (!EFI_ERROR(st)) {
                            llmk_file_write_u16(f, L"FAIL-SAFE: prefill\r\n\r\n");
                            llmk_dump_zones_to_file(f, &g_zones);
                            llmk_dump_sentinel_to_file(f, &g_sentinel);
                            if (g_llmk_log.capacity) llmk_dump_log_to_file(f, &g_llmk_log, 128);
                            uefi_call_wrapper(f->Flush, 1, f);
                            uefi_call_wrapper(f->Close, 1, f);
                            Print(L"[llmk] wrote llmk-failsafe.txt\r\n");
                        }
                    }
                    if (g_test_failsafe_active) {
                        g_sentinel.cfg.strict_budget = g_test_failsafe_prev_strict_budget;
                        g_budget_prefill_cycles = g_test_failsafe_prev_prefill;
                        g_budget_decode_cycles = g_test_failsafe_prev_decode;
                        g_test_failsafe_active = 0;
                        Print(L"[test] fail-safe test complete (restored)\r\n");
                    }
                    break;
                }
                if (!ok) {
                    // Non-fatal budget overrun: adapt budget upward and continue.
                    g_budget_overruns_prefill++;
                    if (g_budget_overruns_prefill <= 3) {
                        Print(L"\r\n[llmk][budget] prefill overrun i=%d cycles=%lu max=%lu (auto-raise)\r\n",
                              i, g_sentinel.last_dt_cycles, g_sentinel.last_budget_cycles);
                    }
                }
                llmk_budget_update(&g_budget_prefill_cycles, g_sentinel.last_dt_cycles);
            } else {
                transformer_forward(&state, &weights, &config, prompt_tokens[i], i);
            }
        }
        
        // Start generation from the last prompt token.
        // After prefill, state.logits already corresponds to the last prompt token at position (n_prompt_tokens-1).
        int next;
        int token = prompt_tokens[n_prompt_tokens - 1];
        int pos = kv_pos + n_prompt_tokens - 1;  // Use persistent KV position
        
        int generated_count = 0;
        int repeat_count = 0;
        int last_token = -1;
        int immediate_repeat_count = 0;
        int loop_escape_used = 0; // count (budgeted) rather than boolean
        int repeat_escape_used = 0; // count (budgeted)

        // Record why generation stopped early (useful for GGUF vs BIN debugging).
        const CHAR16 *stop_reason = NULL;
        int stop_token = -1;
        int stop_step = -1;
        int stop_pos = -1;
        
        // Track context for repetition penalty and loop detection.
        int context_tokens[384 + MAX_TOKENS];
        int n_context_tokens = 0;
        for (int i = 0; i < n_prompt_tokens && n_context_tokens < (int)(sizeof(context_tokens) / sizeof(context_tokens[0])); i++) {
            context_tokens[n_context_tokens++] = prompt_tokens[i];
        }

        // Simple stop detection on the last bytes printed.
        char out_tail[64];
        int out_tail_len = 0;
        for (int i = 0; i < 64; i++) out_tail[i] = 0;

        unsigned long long gen_t0 = 0;
        unsigned long long gen_wall0_us = 0;
        int gen_have_wall = 0;
        if (stats_enabled) {
            calibrate_tsc_once();
            gen_t0 = rdtsc();
            gen_have_wall = uefi_wall_us(&gen_wall0_us);
        }

        // TUI: show live generation progress (skip /draw to avoid scribbling over images).
        if (!draw_mode) {
            g_tui_gen_active = 1;
            g_tui_gen_tokens = 0;
            if (g_tui_enabled && g_gop_fb32) {
                g_tui_dirty = 1;
                llmk_tui_redraw_best_effort();
            }
        }

        { CHAR16 _gdmg[80]; SPrint(_gdmg, sizeof(_gdmg), L"[dbg] gen-loop-start max_gen=%d capture=%d\r\n", max_gen_tokens, (int)g_capture_mode); llmk_serial_write_char16(_gdmg); Print(_gdmg); }
        for (int step = 0; step < max_gen_tokens; step++) {
            { CHAR16 _gds[80]; SPrint(_gds, sizeof(_gds), L"[dbg] step-enter step=%d max=%d\r\n", step, max_gen_tokens); llmk_serial_write_char16(_gds); }
            // We sample from the logits produced by the previous forward pass.
            // For step==0, logits come from the final prompt token (prefill).

            // Apply no-repeat ngram blocking (works on pre-softmax logits).
            if (no_repeat_ngram > 1) {
                apply_no_repeat_ngram(state.logits, config.vocab_size, context_tokens, n_context_tokens, no_repeat_ngram);
            }

            // Sample next token (temperature/top_p/top_k + repetition penalty)
            int n_recent = n_context_tokens;
            if (n_recent > 64) n_recent = 64;
            int* recent = (n_recent > 0) ? &context_tokens[n_context_tokens - n_recent] : (int*)0;

            // Loop escapes:
            // - if we detect a short repeating suffix, ban the sampled token and resample (budgeted).
            // - if we are stuck repeating the same token too many times, ban it once and resample.
            for (int attempt = 0; attempt < 3; attempt++) {
                next = sample_advanced(state.logits, config.vocab_size, temperature, min_p, top_p, top_k, recent, n_recent, repeat_penalty);
                if (next == TOKEN_EOS || next == TOKEN_BOS) break;

                // Prevent premature termination on small models that briefly get stuck repeating one token.
                // If we've already repeated the last token 5 times and would do it again, ban it once and resample.
                if (repeat_escape_used < 8 && next == last_token && repeat_count >= 5) {
                    repeat_escape_used++;
                    state.logits[next] = -1.0e9f;
                    continue;
                }

                if (loop_escape_used < 8 && n_context_tokens + 1 < (int)(sizeof(context_tokens) / sizeof(context_tokens[0]))) {
                    context_tokens[n_context_tokens] = next;
                    int would_repeat = has_suffix_repeat(context_tokens, n_context_tokens + 1, 8) ||
                                      has_suffix_repeat(context_tokens, n_context_tokens + 1, 12) ||
                                      has_suffix_repeat(context_tokens, n_context_tokens + 1, 16);
                    if (would_repeat) {
                        loop_escape_used++;
                        state.logits[next] = -1.0e9f;
                        continue;
                    }
                }
                break;
            }
            
            // Check for EOS (some exports may still emit BOS; treat both as stop)
            if (next == TOKEN_EOS || next == TOKEN_BOS) {
                if (!stop_reason) {
                    stop_reason = L"eos/bos";
                    stop_token = next;
                    stop_step = step;
                    stop_pos = pos;
                }
                break;
            }

            // Track immediate repeats (useful as a cheap repetition signal).
            if (next == token) immediate_repeat_count++;
            
            // Check if stuck on same token (per conversation)
            if (next == last_token) {
                repeat_count++;
            } else {
                repeat_count = 0;
                last_token = next;
            }
            
            // Print token (or capture token output for /draw)
            if (next >= 0 && next < config.vocab_size && tokenizer.vocab[next]) {
                char* piece = tokenizer.vocab[next];
                // Decode byte-tokens (<0xNN>) and SentencePiece spaces (▁)
                char decoded[8];
                int dlen = llmk_decode_piece(piece, decoded, (int)sizeof(decoded));
                // Fallback to raw piece if decode returned nothing (e.g. empty token)
                const char* out_bytes = (dlen > 0) ? decoded : piece;
                int out_len = (dlen > 0) ? dlen : (int)(piece[0] ? (int)__builtin_strlen(piece) : 0);
                if (out_len <= 0) { int sl=0; while(piece[sl]) sl++; out_len=sl; }
                if (out_len > 0) {
                    if (g_capture_mode) {
                        llmk_capture_append_ascii(out_bytes, out_len);
                    } else {
                        uefi_print_utf8_bytes(out_bytes, out_len);
                    }
                    generated_count++;
                    
                    // Update UI (simple overlay if enabled)
                    if ((step % 2) == 0) {
                         InterfaceFx_Tick(); 
                    }

                    if (!draw_mode) {
                        g_tui_gen_tokens = generated_count;
                        int mask = (g_ui_mode == 0) ? 15 : 63;
                        if (g_tui_enabled && g_gop_fb32 && ((generated_count & mask) == 0)) {
                            // Throttle redraws to keep overhead low.
                            g_tui_dirty = 1;
                            llmk_tui_redraw_best_effort();
                        }
                    }

                    // Update ASCII tail buffer for stop detection.
                    for (int k = 0; k < out_len; k++) {
                        char ch = out_bytes[k];
                        if (out_tail_len < (int)sizeof(out_tail) - 1) {
                            out_tail[out_tail_len++] = ch;
                            out_tail[out_tail_len] = 0;
                        } else {
                            // shift left by 1
                            for (int s = 0; s < (int)sizeof(out_tail) - 2; s++) out_tail[s] = out_tail[s + 1];
                            out_tail[(int)sizeof(out_tail) - 2] = ch;
                            out_tail[(int)sizeof(out_tail) - 1] = 0;
                        }
                    }

                    // Stop conditions
                    if (stop_on_double_nl) {
                        // Look for "\n\n" in tail.
                        for (int i = 0; i + 1 < out_tail_len; i++) {
                            if (out_tail[i] == '\n' && out_tail[i + 1] == '\n') {
                                if (!stop_reason) {
                                    stop_reason = L"stop_double_nl";
                                    stop_token = next;
                                    stop_step = step;
                                    stop_pos = pos;
                                }
                                step = max_gen_tokens; // force exit
                                break;
                            }
                        }
                    }
                    if (stop_on_you) {
                        // Look for "\nYou:" in tail.
                        for (int i = 0; i + 4 < out_tail_len; i++) {
                            if (out_tail[i] == '\n' && out_tail[i + 1] == 'Y' && out_tail[i + 2] == 'o' && out_tail[i + 3] == 'u' && out_tail[i + 4] == ':') {
                                if (!stop_reason) {
                                    stop_reason = L"stop_you";
                                    stop_token = next;
                                    stop_step = step;
                                    stop_pos = pos;
                                }
                                step = max_gen_tokens; // force exit
                                break;
                            }
                        }
                    }
                }
            }

            // Append to context and apply a simple loop-stop heuristic.
            if (n_context_tokens < (int)(sizeof(context_tokens) / sizeof(context_tokens[0]))) {
                context_tokens[n_context_tokens++] = next;
            }
            {
                float mind_logit = 0.0f;
                float mind_prob = 0.0f;
                float loop_pos = (float)(generated_count > 0 ? generated_count : (step + 1));
                if (llmk_mind_runtime_should_halt(loop_pos, &mind_logit, &mind_prob)) {
                    if (!stop_reason) {
                        stop_reason = L"mind_halt";
                        stop_token = next;
                        stop_step = step;
                        stop_pos = pos;
                    }
                    Print(L"\r\n[MindHaltRuntime] stop step=%d halt_prob=%d.%03d threshold=%d.%03d\r\n",
                          step,
                          (int)mind_prob,
                          (int)((mind_prob >= 0.0f ? mind_prob - (int)mind_prob : ((int)mind_prob - mind_prob)) * 1000.0f),
                          (int)g_mind_runtime_halt_threshold,
                          (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
                    break;
                }
            }
            // Loop heuristic (suffix repeat): do not hard-stop.
            // Under small GGUF models this can trigger very early and mask real generation.
            // The decode budget is already bounded by max_gen_tokens, and we also have
            // loop-escape resampling + repetition penalty.
            // (no-op)
            
            // Advance position and compute next logits
            token = next;
            pos++;
            if (pos >= config.seq_len) {
                if (!stop_reason) {
                    stop_reason = L"seq_len";
                    stop_token = next;
                    stop_step = step;
                    stop_pos = pos;
                }
                break;
            }

            if (g_llmk_ready) {
                if (g_budget_decode_cycles == 0) {
                    g_budget_decode_cycles = 100000000000ULL;
                }
                g_sentinel.cfg.max_cycles_decode = g_budget_decode_cycles;
                llmk_sentinel_phase_start(&g_sentinel, LLMK_PHASE_DECODE);
                transformer_forward(&state, &weights, &config, token, pos);
                BOOLEAN ok = llmk_sentinel_phase_end(&g_sentinel);
                if (g_sentinel.tripped) {
                    immunion_record(&g_immunion, IMMUNION_THREAT_OOBCheck, (uint32_t)g_sentinel.last_error, 80);
                    Print(L"\r\n[llmk] decode stopped (fail-safe) at step=%d pos=%d\r\n", step, pos);
                    if (!stop_reason) {
                        stop_reason = L"sentinel_decode";
                        stop_token = token;
                        stop_step = step;
                        stop_pos = pos;
                    }
                    llmk_print_ctx(&config, model_filename, kv_pos, temperature, min_p, top_p, top_k, no_repeat_ngram, repeat_penalty, max_gen_tokens);
                    llmk_zones_print(&g_zones);
                    llmk_sentinel_print_status(&g_sentinel);
                    llmk_print_log(32);

                    // Best-effort: persist dump to file for offline diagnosis.
                    {
                        EFI_FILE_HANDLE f = NULL;
                        EFI_STATUS st = llmk_open_text_file(&f, L"llmk-failsafe.txt");
                        if (!EFI_ERROR(st)) {
                            llmk_file_write_u16(f, L"FAIL-SAFE: decode\r\n\r\n");
                            llmk_dump_zones_to_file(f, &g_zones);
                            llmk_dump_sentinel_to_file(f, &g_sentinel);
                            if (g_llmk_log.capacity) llmk_dump_log_to_file(f, &g_llmk_log, 128);
                            uefi_call_wrapper(f->Flush, 1, f);
                            uefi_call_wrapper(f->Close, 1, f);
                            Print(L"[llmk] wrote llmk-failsafe.txt\r\n");
                        }
                    }
                    if (g_test_failsafe_active) {
                        g_sentinel.cfg.strict_budget = g_test_failsafe_prev_strict_budget;
                        g_budget_prefill_cycles = g_test_failsafe_prev_prefill;
                        g_budget_decode_cycles = g_test_failsafe_prev_decode;
                        g_test_failsafe_active = 0;
                        Print(L"[test] fail-safe test complete (restored)\r\n");
                    }
                    break;
                }
                if (!ok) {
                    g_budget_overruns_decode++;
                    if (g_budget_overruns_decode <= 3) {
                        Print(L"\r\n[llmk][budget] decode overrun step=%d pos=%d cycles=%lu max=%lu (auto-raise)\r\n",
                              step, pos, g_sentinel.last_dt_cycles, g_sentinel.last_budget_cycles);
                    }
                    if (g_guardrails.enabled &&
                        g_budget_overruns_decode >= (UINT32)g_guardrails.hard_stop_overruns_decode) {
                        m181_guard_trip = 1;
                        g_guardrails.last_trip_overruns_decode = (int)g_budget_overruns_decode;
                        if (!stop_reason) {
                            stop_reason = L"budget_guard";
                            stop_token = token;
                            stop_step = step;
                            stop_pos = pos;
                        }
                        Print(L"\r\n[m18.1] hard-stop decode (overruns=%d threshold=%d)\r\n",
                              (int)g_budget_overruns_decode,
                              g_guardrails.hard_stop_overruns_decode);
                        break;
                    }
                }
                llmk_budget_update(&g_budget_decode_cycles, g_sentinel.last_dt_cycles);
            } else {
                transformer_forward(&state, &weights, &config, token, pos);
            }
        }

        // Emit early-stop reason to serial for automated diagnosis.
        // Only when we stopped before using the full token budget.
        if (!g_capture_mode && stop_reason && generated_count < max_gen_tokens) {
            CHAR16 smsg[160];
            SPrint(smsg, sizeof(smsg), L"[stop] reason=%s tok=%d step=%d pos=%d\r\n",
                   (CHAR16 *)stop_reason, stop_token, stop_step, stop_pos);
            llmk_serial_write_char16(smsg);
        }

        // Flush any pending bytes held for mojibake repair across token boundaries.
        if (!g_capture_mode) {
            uefi_print_utf8_flush();
        }

        if (!draw_mode) {
            g_tui_gen_active = 0;
            if (g_tui_enabled && g_gop_fb32) {
                g_tui_dirty = 1;
                llmk_tui_redraw_best_effort();
            }
        }

        if (g_test_failsafe_active) {
            g_sentinel.cfg.strict_budget = g_test_failsafe_prev_strict_budget;
            g_budget_prefill_cycles = g_test_failsafe_prev_prefill;
            g_budget_decode_cycles = g_test_failsafe_prev_decode;
            g_test_failsafe_active = 0;
            Print(L"\r\n[test] fail-safe test cancelled (no trip; restored)\r\n");
        }

        if (g_llmk_ready && !g_capture_mode) {
            Print(L"\r\n[llmk][budget] final prefill_max=%lu decode_max=%lu overruns(p=%d d=%d)\r\n",
                  g_budget_prefill_cycles,
                  g_budget_decode_cycles,
                  (int)g_budget_overruns_prefill,
                  (int)g_budget_overruns_decode);
        }

        // Emit a serial-visible marker so automated QEMU tests can prove generation happened,
        // even when token text is not routed to the serial console.
        if (!g_capture_mode) {
            CHAR16 msg[96];
            SPrint(msg, sizeof(msg), L"[gen] tokens=%d\r\n", generated_count);
            llmk_serial_write_char16(msg);

            {
                const CHAR16 *obs_reason = stop_reason ? stop_reason : L"max_tokens";
                CHAR16 omsg[224];
                SPrint(omsg, sizeof(omsg),
                       L"[obs] gen_end tokens=%d reason=%s step=%d pos=%d repeat_escape=%d loop_escape=%d overrun_d=%d\r\n",
                       generated_count,
                       (CHAR16 *)obs_reason,
                       stop_step,
                       stop_pos,
                       repeat_escape_used,
                       loop_escape_used,
                       (int)g_budget_overruns_decode);
                llmk_serial_write_char16(omsg);
            }
        }

        if (stats_enabled && !g_capture_mode) {
            unsigned long long gen_t1 = rdtsc();
            unsigned long long dt = (gen_t1 > gen_t0) ? (gen_t1 - gen_t0) : 0;

            // Prefer wall-clock timing when available (more stable under emulation).
            if (gen_have_wall) {
                unsigned long long gen_wall1_us = 0;
                if (uefi_wall_us(&gen_wall1_us)) {
                    unsigned long long wall_dt_us = (gen_wall1_us >= gen_wall0_us) ? (gen_wall1_us - gen_wall0_us)
                                                                                   : (gen_wall1_us + 86400ULL * 1000000ULL - gen_wall0_us);
                    unsigned long long ms = wall_dt_us / 1000ULL;
                    if (wall_dt_us == 0) {
                        Print(L"\r\n[stats] tokens=%d time_ms=%d tok_s=inf\r\n", generated_count, (int)ms);
                    } else {
                        unsigned long long tps_milli = ((unsigned long long)generated_count * 1000000ULL * 1000ULL) / wall_dt_us;
                        unsigned long long tps_int = tps_milli / 1000ULL;
                        unsigned long long tps_frac = tps_milli % 1000ULL;
                        Print(L"\r\n[stats] tokens=%d time_ms=%d tok_s=%d.%03d\r\n",
                              generated_count, (int)ms, (int)tps_int, (int)tps_frac);
                    }
                    goto stats_done;
                }
            }

            // Fallback to TSC-based estimate.
            if (tsc_per_sec == 0 || dt == 0) {
                Print(L"\r\n[stats] tokens=%d cycles=%d\r\n", generated_count, (int)dt);
            } else {
                unsigned long long ms = (dt * 1000ULL) / tsc_per_sec;
                // milli tok/s for visibility even when < 1 tok/s
                unsigned long long tps_milli = ((unsigned long long)generated_count * tsc_per_sec * 1000ULL) / dt;
                unsigned long long tps_int = tps_milli / 1000ULL;
                unsigned long long tps_frac = tps_milli % 1000ULL;
                Print(L"\r\n[stats] tokens=%d time_ms=%d tok_s=%d.%03d\r\n",
                      generated_count, (int)ms, (int)tps_int, (int)tps_frac);
            }
stats_done:
            ;
        }
        
        // M16.1: Track completed generation
        g_metrics.generation_count++;

        // M19.1: write per-case benchmark JSONL row when active.
        llmk_bench_on_turn_end(generated_count);

        // Diopion burst: decrement remaining and restore knobs when done.
        llmk_diopion_burst_finish_one(&max_gen_tokens, &top_k, &temperature);

        // Calibrion: feed basic stats after each non-capture generation.
        // Keep it simple and cheap: tokens_generated + immediate repeats, entropy is a neutral placeholder.
        if (!g_capture_mode && !draw_mode) {
            calibrion_feed(&g_calibrion,
                           (uint32_t)generated_count,
                           (uint32_t)immediate_repeat_count,
                           1000 /* entropy_milli (neutral) */);

            if (g_calibrion.mode == CALIBRION_MODE_ENFORCE) {
                uint32_t t, k, p;
                calibrion_get_recommendation(&g_calibrion, &t, &k, &p);
                temperature = (float)t / 1000.0f;
                top_k = (int)k;
                top_p = (float)p / 1000.0f;
            }
        }

        {
            UINT64 m18_decode_cycles_delta = (g_metrics.total_decode_cycles >= m18_decode_cycles_start)
                                          ? (g_metrics.total_decode_cycles - m18_decode_cycles_start)
                                          : 0ULL;
            UINT32 m18_decode_tokens_delta = (g_metrics.total_decode_tokens >= m18_decode_tokens_start)
                                          ? (g_metrics.total_decode_tokens - m18_decode_tokens_start)
                                          : 0U;

            if (!g_capture_mode && !draw_mode && m18_decode_cycles_delta > 0 && tsc_per_sec > 0) {
                MetabionSample ms = {0};
                ms.tokens_per_sec = ((uint64_t)m18_decode_tokens_delta * tsc_per_sec) / m18_decode_cycles_delta;
                ms.cache_hit_milli = 0;
                metabion_feed(&g_metabion, &ms);
            }

            llmk_autotune_apply_after_turn(
                m18_decode_cycles_delta,
                m18_decode_tokens_delta,
                &temperature,
                &top_p,
                &top_k,
                &max_gen_tokens,
                m18_base_temp_milli,
                m18_base_top_p_milli,
                m18_base_top_k,
                m18_base_max_gen_tokens,
                (!g_capture_mode && !draw_mode)
            );
        }

        llmk_guardrails_finish_turn(m181_guard_trip);
        if (m181_guard_trip && g_guardrails.reset_kv_on_trip) {
            reset_kv_cache(&state, &config);
            kv_pos = 0;
            g_llmk_kv_pos = 0;
            if (!g_capture_mode) {
                Print(L"[m18.1] safe fallback: KV cache reset\r\n");
            }
        }

        // If capture mode was active, handle it now.
        if (g_capture_mode) {
            llmk_capture_sanitize_inplace();

            if (capture_kind == 1) {
                llmk_apply_simple_autocorrect(g_capture_buf);
                Print(L"\r\n[draw] captured %d chars%s\r\n", g_capture_len, g_capture_truncated ? L" (truncated)" : L"");
                if (g_capture_len == 0) {
                    Print(L"[draw] ERROR: empty output\r\n\r\n");
                } else {
                    int ok = llmk_render_scene_dsl_ex(g_capture_buf, 0);
                    if (ok) {
                        llmk_gop_force_update();
                        Print(L"[draw] OK: rendered (check screen above, use /save_img to export)\r\n\r\n");
                    } else {
                        // The stories model often outputs prose. Render a fallback so the user sees something.
                        llmk_draw_fallback_center_square(1);
                        llmk_gop_force_update();

                        CHAR16 msg[140];
                        ascii_to_char16(msg, g_last_dsl_error, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"[draw] WARNING: model output was not valid DSL (%s)\r\n", msg);
                        Print(L"[draw] Rendered fallback: black background + centered white square\r\n\r\n");
                    }
                }
            } else if (capture_kind == 2) {
                if (oo_think_id > 0) {
                    char n1[320];
                    int p1 = 0;
                    const char *h1 = "think: ";
                    for (int k = 0; h1[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = h1[k];
                    for (int k = 0; oo_think_user[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = oo_think_user[k];
                    n1[p1] = 0;
                    llmk_oo_note(oo_think_id, n1);

                    char n2[640];
                    int p2 = 0;
                    const char *h2 = "answer: ";
                    for (int k = 0; h2[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = h2[k];
                    for (int k = 0; g_capture_buf[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = g_capture_buf[k];
                    n2[p2] = 0;
                    llmk_oo_note(oo_think_id, n2);
                    llmk_oo_digest(oo_think_id);

                    Print(L"\r\n[oo] stored thought for entity id=%d (%d chars%s)\r\n\r\n",
                          oo_think_id, g_capture_len, g_capture_truncated ? L"; truncated" : L"");
                } else {
                    Print(L"\r\n[oo] ERROR: internal think state\r\n\r\n");
                }
            } else if (capture_kind == 3) {
                if (oo_think_id > 0) {
                    // Store the cycle's prompt + answer.
                    char n1[320];
                    int p1 = 0;
                    const char *h1 = "auto: ";
                    for (int k = 0; h1[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = h1[k];
                    for (int k = 0; oo_think_user[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = oo_think_user[k];
                    n1[p1] = 0;
                    llmk_oo_note(oo_think_id, n1);

                    char n2[640];
                    int p2 = 0;
                    const char *h2 = "answer: ";
                    for (int k = 0; h2[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = h2[k];
                    for (int k = 0; g_capture_buf[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = g_capture_buf[k];
                    n2[p2] = 0;
                    llmk_oo_note(oo_think_id, n2);

                    if (oo_auto_planning) {
                        // Planning cycle: extract first line as an action and push to agenda.
                        char act[96];
                        int ap = 0;
                        int si = 0;
                        while (g_capture_buf[si] == ' ' || g_capture_buf[si] == '\t' || g_capture_buf[si] == '\n') si++;
                        for (; g_capture_buf[si] && g_capture_buf[si] != '\n' && ap + 1 < (int)sizeof(act); si++) {
                            act[ap++] = g_capture_buf[si];
                        }
                        while (ap > 0 && (act[ap - 1] == ' ' || act[ap - 1] == '\t')) ap--;
                        act[ap] = 0;

                        if (act[0] && llmk_oo_agenda_add(oo_think_id, act)) {
                            CHAR16 a16[120];
                            ascii_to_char16(a16, act, (int)(sizeof(a16) / sizeof(a16[0])));
                            Print(L"\r\n[oo_auto] planned: %s\r\n\r\n", a16);
                            llmk_oo_digest(oo_think_id);
                            // Do NOT decrement remaining; next cycle will execute.
                        } else {
                            Print(L"\r\n[oo_auto] planning failed; stopping\r\n\r\n");
                            g_oo_auto_active = 0;
                            g_oo_auto_id = 0;
                            g_oo_auto_remaining = 0;
                            g_oo_auto_total = 0;
                            g_oo_auto_user[0] = 0;
                        }
                    } else {
                        // Execute cycle: advance entity and refresh digest.
                        llmk_oo_step(oo_think_id);
                        llmk_oo_digest(oo_think_id);

                        // If this cycle executed an agenda action, mark it DONE and log it.
                        if (oo_auto_action_k > 0) {
                            char done_note[196];
                            int dp = 0;
                            const char *h = "done: ";
                            for (int k = 0; h[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = h[k];
                            for (int k = 0; oo_think_user[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = oo_think_user[k];
                            done_note[dp] = 0;
                            llmk_oo_note(oo_think_id, done_note);
                            llmk_oo_action_set_state(oo_think_id, oo_auto_action_k, 2);
                        }

                        if (g_oo_auto_active && g_oo_auto_id == oo_think_id && g_oo_auto_remaining > 0) {
                            g_oo_auto_remaining--;
                            Print(L"\r\n[oo_auto] stored + stepped id=%d (%d chars%s); remaining=%d\r\n\r\n",
                                  oo_think_id, g_capture_len, g_capture_truncated ? L"; truncated" : L"", g_oo_auto_remaining);
                            if (g_oo_auto_remaining <= 0) {
                                Print(L"[oo_auto] done\r\n\r\n");
                                g_oo_auto_active = 0;
                                g_oo_auto_id = 0;
                                g_oo_auto_remaining = 0;
                                g_oo_auto_total = 0;
                                g_oo_auto_user[0] = 0;
                            }

                            // Optional autosave (repl.cfg: oo_autosave_every=N). Best-effort.
                            if (oo_autosave_every > 0 && oo_state_file[0]) {
                                int completed = 0;
                                if (g_oo_auto_total > 0) {
                                    completed = g_oo_auto_total - g_oo_auto_remaining;
                                }
                                if (completed > 0 && (completed % oo_autosave_every) == 0) {
                                    int nb = 0;
                                    EFI_STATUS st = llmk_oo_save_to_file_best_effort(oo_state_file, &nb);
                                    if (!EFI_ERROR(st)) {
                                        Print(L"[oo_autosave] saved %s (%d bytes)\r\n", oo_state_file, nb);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    Print(L"\r\n[oo_auto] ERROR: internal state\r\n\r\n");
                    g_oo_auto_active = 0;
                    g_oo_auto_id = 0;
                    g_oo_auto_remaining = 0;
                    g_oo_auto_total = 0;
                    g_oo_auto_user[0] = 0;
                }
            } else if (capture_kind == 4) {
                if (oo_think_id > 0) {
                    // Store the cycle's prompt + answer.
                    char n1[320];
                    int p1 = 0;
                    const char *h1 = "exec: ";
                    for (int k = 0; h1[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = h1[k];
                    for (int k = 0; oo_think_user[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = oo_think_user[k];
                    n1[p1] = 0;
                    llmk_oo_note(oo_think_id, n1);

                    char n2[640];
                    int p2 = 0;
                    const char *h2 = "answer: ";
                    for (int k = 0; h2[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = h2[k];
                    for (int k = 0; g_capture_buf[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = g_capture_buf[k];
                    n2[p2] = 0;
                    llmk_oo_note(oo_think_id, n2);

                    if (oo_exec_planning) {
                        // Planning cycle: extract first line as an action and push to agenda.
                        char act[96];
                        int ap = 0;
                        int si = 0;
                        while (g_capture_buf[si] == ' ' || g_capture_buf[si] == '\t' || g_capture_buf[si] == '\n') si++;
                        for (; g_capture_buf[si] && g_capture_buf[si] != '\n' && ap + 1 < (int)sizeof(act); si++) {
                            act[ap++] = g_capture_buf[si];
                        }
                        while (ap > 0 && (act[ap - 1] == ' ' || act[ap - 1] == '\t')) ap--;
                        act[ap] = 0;

                        if (act[0] && llmk_oo_agenda_add(oo_think_id, act)) {
                            CHAR16 a16[120];
                            ascii_to_char16(a16, act, (int)(sizeof(a16) / sizeof(a16[0])));
                            Print(L"\r\n[oo_exec] planned: %s\r\n\r\n", a16);
                            llmk_oo_digest(oo_think_id);
                            // Do NOT decrement remaining; next cycle will execute.
                        } else {
                            Print(L"\r\n[oo_exec] planning failed; stopping\r\n\r\n");
                            g_oo_exec_active = 0;
                            g_oo_exec_id = 0;
                            g_oo_exec_remaining = 0;
                            g_oo_exec_total = 0;
                            g_oo_exec_plan_if_empty = 0;
                            g_oo_exec_hint[0] = 0;
                        }
                    } else {
                        llmk_oo_step(oo_think_id);
                        llmk_oo_digest(oo_think_id);

                        if (oo_exec_action_k > 0) {
                            char done_note[196];
                            int dp = 0;
                            const char *h = "done: ";
                            for (int k = 0; h[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = h[k];
                            for (int k = 0; oo_think_user[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = oo_think_user[k];
                            done_note[dp] = 0;
                            llmk_oo_note(oo_think_id, done_note);
                            llmk_oo_action_set_state(oo_think_id, oo_exec_action_k, 2);
                        }

                        if (g_oo_exec_active && g_oo_exec_id == oo_think_id && g_oo_exec_remaining > 0) {
                            g_oo_exec_remaining--;
                            Print(L"\r\n[oo_exec] stored + stepped id=%d (%d chars%s); remaining=%d\r\n\r\n",
                                  oo_think_id, g_capture_len, g_capture_truncated ? L"; truncated" : L"", g_oo_exec_remaining);
                            if (g_oo_exec_remaining <= 0) {
                                Print(L"[oo_exec] done\r\n\r\n");
                                g_oo_exec_active = 0;
                                g_oo_exec_id = 0;
                                g_oo_exec_remaining = 0;
                                g_oo_exec_total = 0;
                                g_oo_exec_plan_if_empty = 0;
                                g_oo_exec_hint[0] = 0;
                            }

                            // Optional autosave (repl.cfg: oo_autosave_every=N). Best-effort.
                            if (oo_autosave_every > 0 && oo_state_file[0]) {
                                int completed = 0;
                                if (g_oo_exec_total > 0) {
                                    completed = g_oo_exec_total - g_oo_exec_remaining;
                                }
                                if (completed > 0 && (completed % oo_autosave_every) == 0) {
                                    int nb = 0;
                                    EFI_STATUS st = llmk_oo_save_to_file_best_effort(oo_state_file, &nb);
                                    if (!EFI_ERROR(st)) {
                                        Print(L"[oo_autosave] saved %s (%d bytes)\r\n", oo_state_file, nb);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    Print(L"\r\n[oo_exec] ERROR: internal state\r\n\r\n");
                    g_oo_exec_active = 0;
                    g_oo_exec_id = 0;
                    g_oo_exec_remaining = 0;
                    g_oo_exec_total = 0;
                    g_oo_exec_plan_if_empty = 0;
                    g_oo_exec_hint[0] = 0;
                }
            }

            // Disable capture mode and restore sampling flags.
            g_capture_mode = 0;
            llmk_capture_reset();
            stop_on_you = saved_stop_on_you;
            stop_on_double_nl = saved_stop_on_double_nl;
            max_gen_tokens = saved_max_gen_tokens;

            if (draw_saved_sampling) {
                temperature = saved_temperature;
                min_p = saved_min_p;
                top_p = saved_top_p;
                top_k = saved_top_k;
                repeat_penalty = saved_repeat_penalty;
            }
        }
        
        // Update persistent KV cache position for next generation
        kv_pos += n_prompt_tokens + generated_count;
        g_llmk_kv_pos = kv_pos;
        
        if (!g_capture_mode) {
            Print(L"\r\n\r\n");
        }
    }
    
    Print(L"Press any key to exit...\r\n");
    EFI_INPUT_KEY Key;
    UINTN index;
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
    uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
    
    return EFI_SUCCESS;
}

#endif /* LLM_SPLIT_EFI_MAIN */
