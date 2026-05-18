/* oo-kernel/boot/efi_phases.c
 *
 * P1 God-file Split: implements efi_phase1..6 declared in efi_entry.h.
 *
 * ARCHITECTURE:
 *   This file uses a unity-build include to pull in all type definitions,
 *   helper functions, and global variables from llama2_efi_final.c.
 *   The original efi_main() is excluded via #define LLM_SPLIT_EFI_MAIN.
 *
 *   Phase functions extract the boot phases from what was inline in efi_main():
 *     god file [1/7] → efi_phase1_filesystem
 *     god file [2/7] → efi_phase2_model_scan
 *     god file [3/7] → efi_phase3_load_weights  (zones + sentinel)
 *     god file [4/7] → efi_phase4_tokenizer      (weight pointers)
 *     god file [5/7]+[6/7] → efi_phase5_safety   (state buffers + tokenizer)
 *     new             → efi_phase6_engines        (oo_bus_init + cmd register)
 *
 *   repl_body.c is #included at the bottom so llmk_repl_run() has access
 *   to all types (Config, TransformerWeights, etc.) defined in the god file.
 *
 * COMPILE from source repo root:
 *   gcc $(CFLAGS) -DLLM_SPLIT_EFI_MAIN -I. -I$(OO_WORKTREE) \
 *       -c $(OO_WORKTREE)/oo-kernel/boot/efi_phases.c -o efi_phases.o
 *
 * MIGRATION NOTE:
 *   Phase 2: extract types to llama2_model.h, remove unity include
 *   Phase 3: replace direct global access with ctx->xxx
 *   Phase 4: dissolve this file; each phase becomes its own .c
 */

/* ── Step 1: exclude original efi_main from the god file ─────────────── */
#define LLM_SPLIT_EFI_MAIN

/* ── Step 2: unity-include the god file (all types + helpers + globals) ─ */
/* Resolved via -I. (source repo root) when compiled from the source repo. */
#include "llama2_efi_final.c"

/* ── Step 3: worktree headers (OoBootCtx, phase declarations) ────────── */
/* oo_boot.h and efi_entry.h are in the worktree, found via -I$(OO_WORKTREE). */
#include "oo-kernel/boot/efi_entry.h"
#include "oo-bus/hermes/oo_bus_channels.h"
#include "oo-bus/hermes/oo_bus_router.h"
#include "oo-warden/sentinel/pressure.h"
#include "oo-engine/splitbrain/splitbrain.h"

/* P2: forward decls for module tick (avoids pulling in module_api.h + all
 *     module headers into this unity-build TU) */
extern void oo_module_table_tick_all(OoBootCtx *boot_ctx, uint32_t step);

/* ============================================================
 * Inter-phase static state
 *
 * Variables named exactly config/weights/state/tokenizer so
 * repl_body.c (included at the bottom) can reference them directly.
 * ============================================================ */
static Config             config;
static TransformerWeights weights;
static RunState           state;
static Tokenizer          tokenizer;

/* Phase 2 → phases 3-5 */
static EFI_FILE_HANDLE    s_model_file       = NULL;
static int                s_shared_classifier = 0;
static int                s_use_gguf_inference = 0;
static LlmkGgufPlan      *s_gguf_plan         = NULL;
static int                s_use_q8_blob        = 0;
static UINT64             s_q8_blob_bytes      = 0;
static UINT64             s_model_file_size    = 0;

/* Phase 3 → phases 4-5 */
static int    s_kv_dim        = 0;
static int    s_head_size     = 0;
static UINTN  s_weights_bytes = 0;

/* ============================================================
 * Phase 1: File System
 * Opens UEFI SFS volume, loads repl.cfg, probes CPU, inits GOP.
 * ============================================================ */
EFI_STATUS efi_phase1_filesystem(OoBootCtx *ctx, EFI_HANDLE ImageHandle) {
    /* Engine inits extracted from god file efi_main (before [1/7] marker).
     * In the old god file these ran before filesystem; keep them here. */
    djibion_init(&g_djibion);
    diopion_init(&g_diopion);
    diagnostion_init(&g_diagnostion);
    memorion_init(&g_memorion);
    orchestrion_init(&g_orchestrion);
    calibrion_init(&g_calibrion);
    compatibilion_init(&g_compatibilion);
    compatibilion_probe_cpu(&g_compatibilion);
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

    djibmark_init();
    DJIBMARK_BOOT();

    /* Print verbose/banner info */
    if (!g_boot_verbose) {
        Print(L"Booting... (set boot_verbose=1 in repl.cfg for details; 2 for debug)\r\n\r\n");
    }

    llmk_boot_mark(L"banner");

    /* ── [1/7] File System ─────────────────────────────────────────────── */
    if (g_boot_verbose) {
        Print(L"[1/7] Opening file system...\r\n");
    }

    EFI_LOADED_IMAGE *LoadedImage;
    EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3,
                            ImageHandle, &LoadedImageProtocol, &LoadedImage);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: LoadedImage protocol failed\r\n");
        return status;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                 LoadedImage->DeviceHandle, &FileSystemProtocol, &FileSystem);
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

    /* Persist root handle for best-effort dumps and subsequent phases. */
    g_root = Root;
    ctx->efi_root = Root;

    llmk_load_repl_cfg_boot_best_effort();
    llmk_load_repl_cfg_oo_engines_best_effort();

    if (g_boot_logo) {
        llmk_print_logo();
    }

    if (g_boot_verbose) {
        Print(L"OK: File system ready\r\n\r\n");
    }

    llmk_boot_mark(L"fs_ready");

    /* OO M1: persistent boot tick (writes OOSTATE.BIN + appends OOJOUR.LOG) */
    llmk_oo_boot_tick_best_effort();
    llmk_oo_net_tick_best_effort();

#if !DJIBLAS_DISABLE_CPUID
    enable_avx_best_effort();
#endif

    /* CPU feature detection */
    {
        CPUFeatures cpu_features;
        djiblas_detect_cpu(&cpu_features);
        sgemm_kernel_t k = djiblas_get_best_kernel(&cpu_features);
        const CHAR16 *name = L"SCALAR";
        if (k == djiblas_sgemm_avx512) name = L"AVX512";
        else if (k == djiblas_sgemm_avx2)
            name = (cpu_features.has_fma ? L"AVX2+FMA" : L"AVX2");
        else if (k == djiblas_sgemm_sse2) name = L"SSE2";

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

    /* Best-effort graphics init (GOP) */
    {
        EFI_STATUS gst = llmk_gop_init_best_effort();
        if (!EFI_ERROR(gst)) {
            if (g_boot_verbose) {
                Print(L"[GOP] Framebuffer ready: %dx%d (ppsl=%d)\r\n\r\n",
                      (int)g_gop_w, (int)g_gop_h, (int)g_gop_ppsl);
            }
            compatibilion_set_gop(&g_compatibilion, (uint32_t)g_gop_w, (uint32_t)g_gop_h);
        } else {
            if (g_boot_verbose) {
                Print(L"[GOP] Not available (%r)\r\n\r\n", gst);
            }
        }
    }

    llmk_boot_mark(L"gop_init");

    if (g_boot_diag) {
        llmk_print_diag();
    }

    llmk_oo_init();
    llmk_oo_set_on_step(llmk_oo_on_step_gop);

    ctx->phase1_ok = 1;
    return EFI_SUCCESS;
}

/* ============================================================
 * Phase 2: Model Scan
 * Finds and opens the model file; reads header or GGUF plan.
 * ============================================================ */
EFI_STATUS efi_phase2_model_scan(OoBootCtx *ctx) {
    EFI_FILE_HANDLE Root = ctx->efi_root;
    EFI_STATUS status = EFI_NOT_FOUND;

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

        CHAR16 cfg_model[128];
        cfg_model[0] = 0;
        if (llmk_read_cfg_model_best_effort(Root, cfg_model,
                (int)(sizeof(cfg_model) / sizeof(cfg_model[0])))) {
            cfg_model_override_requested = 1;
            llmk_char16_copy_cap(cfg_model_requested,
                (int)(sizeof(cfg_model_requested) / sizeof(cfg_model_requested[0])),
                cfg_model);
            EFI_FILE_HANDLE f = 0;
            EFI_STATUS st = EFI_NOT_FOUND;

            if (!llmk_char16_has_dot_ext(cfg_model)) {
                CHAR16 picked[192];
                picked[0] = 0;
                if (llmk_try_open_with_ext(Root, cfg_model, L".bin", &f, picked,
                        (int)(sizeof(picked) / sizeof(picked[0])))) {
                    llmk_char16_copy_cap(cfg_model,
                        (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                    st = EFI_SUCCESS;
                } else if (llmk_try_open_with_ext(Root, cfg_model, L".gguf", &f, picked,
                        (int)(sizeof(picked) / sizeof(picked[0])))) {
                    llmk_char16_copy_cap(cfg_model,
                        (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                    st = EFI_SUCCESS;
                }
            } else {
                CHAR16 picked[192];
                picked[0] = 0;
                st = llmk_open_read_with_fat83_fallback(Root, cfg_model, &f, picked,
                        (int)(sizeof(picked) / sizeof(picked[0])), L"model_cfg");
                if (!EFI_ERROR(st) && picked[0]) {
                    llmk_char16_copy_cap(cfg_model,
                        (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                }
            }

            if (!EFI_ERROR(st) && f) {
                llmk_model_set_loaded_path(cfg_model);
                model_filename = g_loaded_model_path16;
                ModelFile = f;
                status = st;
            } else {
                Print(L"[cfg] WARNING: model override open failed: %s (%r)\r\n",
                      cfg_model, st);
                Print(L"[cfg] hint: run /models to inspect available files, "
                      L"or set model=<name>.bin|.gguf\r\n");
                Print(L"[cfg] fallback: continuing with auto-detect candidates\r\n");
                cfg_model_override_failed = 1;
            }
        }

        if (model_filename != NULL) {
            goto model_selected;
        }

        if (g_cfg_model_picker != 0) {
            LlmkModelEntry entries2[2]; // SAFE: only top-2 candidates needed for picker
            int n_models = llmk_collect_models(entries2,
                (int)(sizeof(entries2) / sizeof(entries2[0])));
            Print(L"[ph2-dbg] n_models=%d\r\n", (UINT64)n_models);
            if (n_models > 0) Print(L"[ph2-dbg] model[0]=%s\r\n", entries2[0].path);
            if (n_models >= 2) {
                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[192];
                picked[0] = 0;

                int picked_ok = llmk_model_picker(&f, picked,
                    (int)(sizeof(picked) / sizeof(picked[0])));
                if (picked_ok && f) {
                    ModelFile = f;
                    llmk_model_set_loaded_path(picked);
                    model_filename = g_loaded_model_path16;
                    status = EFI_SUCCESS;
                    goto model_selected;
                }

                InterfaceFx_End();
                llmk_repl_no_model_loop();
                return EFI_NOT_FOUND;
            }
        }

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
            EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, candidates[i],
                    &f, picked0, (int)(sizeof(picked0) / sizeof(picked0[0])),
                    L"model_candidate");
            if (!EFI_ERROR(st) && f) {
                ModelFile = f;
                llmk_model_set_loaded_path(picked0[0] ? picked0 : candidates[i]);
                model_filename = g_loaded_model_path16;
                status = st;
                break;
            }
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
            EFI_FILE_HANDLE f = NULL;
            CHAR16 picked[192];
            picked[0] = 0;

            int picked_ok = 0;
            int picker_used = (g_cfg_model_picker != 0);
            if (picker_used) {
                picked_ok = llmk_model_picker(&f, picked,
                    (int)(sizeof(picked) / sizeof(picked[0])));
            }
            if (!picked_ok && !picker_used) {
                picked[0] = 0;
                if (llmk_try_open_first_model_best_effort(&f, picked,
                        (int)(sizeof(picked) / sizeof(picked[0])))) {
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
                Print(L"Expected one of (root or models\\): "
                      L"stories300M.bin stories260M.bin stories200M.bin "
                      L"stories110M.bin stories15M.bin model.bin\r\n");
                Print(L"Last open status: %r\r\n", last);
                Print(L"Or set repl.cfg: model=<path> (supports .bin/.gguf)\r\n");
                InterfaceFx_End();
                llmk_repl_no_model_loop();
                return last;
            }
        }
model_selected:
        ;

        (void)uefi_wall_us(&startup_model_select_done_us);

        if (g_cfg_oo_enable && cfg_model_override_requested &&
                cfg_model_override_failed && model_filename != NULL) {
            Print(L"OK: OO model fallback: %s -> %s\r\n",
                  cfg_model_requested, model_filename);
        }
    }

    llmk_model_set_loaded_path(model_filename);
    if (g_boot_verbose >= 2) llmk_debug_print_loaded_model_path(L"after_select");

    g_loaded_model_format = llmk_detect_model_format(ModelFile);

    s_gguf_plan = NULL;
    s_use_gguf_inference = 0;
    int gguf_has_output_weight = 0;

    /* Zero-init config */
    config.dim = 0;
    config.hidden_dim = 0;
    config.n_layers = 0;
    config.n_heads = 0;
    config.n_kv_heads = 0;
    config.vocab_size = 0;
    config.seq_len = 0;

    s_shared_classifier = 0;

    if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF) {
        {
            int dim = 0, hidden = 0, layers = 0, heads = 0, kv = 0, vocab = 0, seq = 0;
            EFI_STATUS pst = llmk_gguf_build_plan(ModelFile, &s_gguf_plan,
                    &dim, &hidden, &layers, &heads, &kv, &vocab, &seq,
                    &gguf_has_output_weight);
            if (!EFI_ERROR(pst) && s_gguf_plan) {
                config.dim = dim;
                config.hidden_dim = hidden;
                config.n_layers = layers;
                config.n_heads = heads;
                config.n_kv_heads = kv;
                config.vocab_size = vocab;
                config.seq_len = seq;

                s_shared_classifier = gguf_has_output_weight ? 0 : 1;
                s_use_gguf_inference = 1;
                if (g_boot_verbose) {
                    Print(L"GGUF detected: ctx=%d dim=%d layers=%d heads=%d kv_heads=%d\r\n",
                          config.seq_len, config.dim, config.n_layers,
                          config.n_heads, config.n_kv_heads);
                }
                Print(L"OK: GGUF inference enabled (F16/F32/Q4/Q5/Q8).\r\n\r\n");
            } else {
                Print(L"NOTE: GGUF inference unsupported (%r); "
                      L"searching for a .bin fallback...\r\n", pst);
            }
        }

        if (!s_use_gguf_inference) {
            uefi_call_wrapper(ModelFile->Close, 1, ModelFile);

            /* Preferred fallback: sibling .bin next to the selected .gguf */
            if (model_filename && llmk_char16_endswith_ci(model_filename, L".gguf")) {
                CHAR16 alt[192];
                llmk_char16_copy_cap(alt,
                    (int)(sizeof(alt) / sizeof(alt[0])), model_filename);
                for (int k = (int)StrLen(alt) - 1; k >= 0; k--) {
                    if (alt[k] == L'.') { alt[k] = 0; break; }
                    if (alt[k] == L'\\' || alt[k] == L'/') break;
                }
                if (StrLen(alt) + 4 < (sizeof(alt) / sizeof(alt[0]))) {
                    StrCat(alt, L".bin");
                    EFI_FILE_HANDLE fb = NULL;
                    CHAR16 picked[192];
                    picked[0] = 0;
                    EFI_STATUS fst = llmk_open_read_with_fat83_fallback(
                            Root, alt, &fb, picked,
                            (int)(sizeof(picked) / sizeof(picked[0])),
                            L"gguf_sibling_bin");
                    if (!EFI_ERROR(fst) && fb) {
                        ModelFile = fb;
                        const CHAR16 *chosen = picked[0] ? picked : alt;
                        UINTN n = StrLen(chosen) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc(
                            (unsigned long)(n * sizeof(CHAR16)));
                        model_filename = stable ? stable : model_filename;
                        if (stable) StrCpy(stable, chosen);
                        llmk_model_set_loaded_path(model_filename);
                        g_loaded_model_format = LLMK_MODEL_FMT_BIN;
                        Print(L"OK: using sibling .bin fallback: %s\r\n\r\n",
                              model_filename);
                        goto gguf_fallback_done;
                    }
                }
            }

            CHAR16 *fallbacks[] = {
                L"stories300M.bin", L"stories260M.bin", L"stories200M.bin",
                L"stories110M.bin", L"stories15M.bin", L"model.bin",
            };
            const int n_fallbacks = (int)(sizeof(fallbacks) / sizeof(fallbacks[0]));
            EFI_FILE_HANDLE fb = NULL;
            CHAR16 *fb_name = NULL;
            for (int fi = 0; fi < n_fallbacks; fi++) {
                EFI_FILE_HANDLE t = NULL;
                CHAR16 picked0[192];
                picked0[0] = 0;
                EFI_STATUS fst = llmk_open_read_with_fat83_fallback(
                        Root, fallbacks[fi], &t, picked0,
                        (int)(sizeof(picked0) / sizeof(picked0[0])),
                        L"gguf_fallback_root");
                if (!EFI_ERROR(fst) && t) {
                    fb = t;
                    if (picked0[0]) {
                        UINTN n = StrLen(picked0) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc(
                            (unsigned long)(n * sizeof(CHAR16)));
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
                    fst = llmk_open_read_with_fat83_fallback(
                            Root, pth, &t, picked1,
                            (int)(sizeof(picked1) / sizeof(picked1[0])),
                            L"gguf_fallback_models");
                    if (!EFI_ERROR(fst) && t) {
                        fb = t;
                        const CHAR16 *chosen = picked1[0] ? picked1 : pth;
                        UINTN n = StrLen(chosen) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc(
                            (unsigned long)(n * sizeof(CHAR16)));
                        fb_name = stable ? stable : fallbacks[fi];
                        if (stable) StrCpy(stable, chosen);
                        break;
                    }
                }
            }

            if (!fb || !fb_name) {
                Print(L"ERROR: no .bin fallback found. "
                      L"Use /model_info to inspect GGUF, "
                      L"or provide a .bin export for inference.\r\n");
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
    if (startup_model_t0_us && startup_model_prep_done_us >= startup_model_t0_us) {
        unsigned long long select_ms = (startup_model_select_done_us >= startup_model_t0_us)
                ? ((startup_model_select_done_us - startup_model_t0_us) / 1000ULL) : 0ULL;
        unsigned long long prep_ms = (startup_model_prep_done_us >= startup_model_select_done_us)
                ? ((startup_model_prep_done_us - startup_model_select_done_us) / 1000ULL) : 0ULL;
        const char *fmt_s = (g_loaded_model_format == LLMK_MODEL_FMT_GGUF) ? "gguf" :
                            (g_loaded_model_format == LLMK_MODEL_FMT_BIN)  ? "bin" : "unknown";
        Print(L"[obs][startup] model_select_ms=%lu model_prepare_ms=%lu format=%a\r\n",
              (UINT64)select_ms, (UINT64)prep_ms, fmt_s);
    }

    UINTN bytes_to_read = 0;
    if (!s_use_gguf_inference && ModelFile) {
        /* Guard: detect OOSI v3 binary before reading it as Llama2 config */
        UINT64 _pos0 = 0;
        uefi_call_wrapper(ModelFile->SetPosition, 2, ModelFile, _pos0);
        UINT8 _magic4[4] = {0,0,0,0}; UINTN _msz = 4;
        uefi_call_wrapper(ModelFile->Read, 3, ModelFile, &_msz, _magic4);
        UINT32 _pmagic = (UINT32)_magic4[0] | ((UINT32)_magic4[1]<<8)
                       | ((UINT32)_magic4[2]<<16) | ((UINT32)_magic4[3]<<24);
        Print(L"[ph2-guard] magic=%08x v3=%08x\r\n", (UINT64)_pmagic, (UINT64)OOSI_V3_MAGIC);
        if (_pmagic == OOSI_V3_MAGIC) {
            Print(L"[ph2-guard] OOSI v3 — routing to no-model REPL for /ssm_load\r\n");
            uefi_call_wrapper(ModelFile->Close, 1, ModelFile);
            InterfaceFx_End();
            llmk_repl_no_model_loop();
            return EFI_SUCCESS;
        }
        /* Not v3: rewind to position 0 before reading Llama2 config */
        UINT64 _rewind = 0;
        uefi_call_wrapper(ModelFile->SetPosition, 2, ModelFile, _rewind);
    }
    if (!s_use_gguf_inference) {
        bytes_to_read = 7 * sizeof(int);
        uefi_call_wrapper(ModelFile->Read, 3, ModelFile, &bytes_to_read, &config);

        s_shared_classifier = (config.vocab_size < 0);
        if (config.vocab_size < 0) config.vocab_size = -config.vocab_size;
    }

    /* Detect model_file_size for shared_classifier auto-detect */
    s_model_file_size = 0;
    if (!s_use_gguf_inference) {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS st = uefi_call_wrapper(ModelFile->GetInfo, 4,
                            ModelFile, &FileInfoGuid, &info_size, NULL);
        if (st == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            st = uefi_call_wrapper(BS->AllocatePool, 3,
                     EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(st) && info) {
                st = uefi_call_wrapper(ModelFile->GetInfo, 4,
                         ModelFile, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(st)) {
                    s_model_file_size = info->FileSize;
                }
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }

    /* Q8 blob check */
    s_use_q8_blob = 0;
    s_q8_blob_bytes = 0;
    if (g_cfg_gguf_q8_blob && s_use_gguf_inference && s_gguf_plan) {
        if (llmk_gguf_plan_supports_q8_0_blob(s_gguf_plan, s_shared_classifier)) {
            EFI_STATUS bst = llmk_gguf_calc_llama2_q8_0_blob_bytes(
                s_gguf_plan,
                config.dim, config.hidden_dim, config.n_layers, config.n_heads,
                config.n_kv_heads, config.vocab_size, config.seq_len,
                s_shared_classifier, &s_q8_blob_bytes);
            if (!EFI_ERROR(bst) && s_q8_blob_bytes > 0) {
                s_use_q8_blob = 1;
                if (g_boot_verbose) {
                    Print(L"[gguf] Q8_0 blob enabled: %lu MB\r\n",
                          (UINT64)(s_q8_blob_bytes / (1024ULL * 1024ULL)));
                }
            } else {
                Print(L"NOTE: GGUF Q8_0 blob sizing failed (%r); using float32 load.\r\n",
                      bst);
            }
        }
    } else if (!g_cfg_gguf_q8_blob && s_use_gguf_inference && s_gguf_plan) {
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
              config.dim, config.n_layers, config.n_heads,
              config.n_kv_heads, config.vocab_size, config.seq_len);
    }

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

    s_model_file = ModelFile;
    ctx->phase2_ok = 1;
    return EFI_SUCCESS;
}

/* ============================================================
 * Phase 3: Load Weights  (zones + heap + sentinel)
 * ============================================================ */
EFI_STATUS efi_phase3_load_weights(OoBootCtx *ctx) {
    llmk_overlay_stage(3, 7);

    /* Context length clamping */
    {
        int min_ctx = 64;
        int effective = config.seq_len;

        if (g_cfg_ctx_len > 0) {
            int target = g_cfg_ctx_len;
            if (target < 0) target = -target;
            if (target < min_ctx) target = min_ctx;
            if (target < effective) {
                if (g_boot_verbose) {
                    Print(L"[cfg] ctx_len=%d -> effective seq_len=%d (model=%d)\r\n",
                          g_cfg_ctx_len, target, config.seq_len);
                }
                effective = target;
            }
        }

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
        if (effective < config.seq_len) config.seq_len = effective;
    }

    s_kv_dim  = (config.dim * config.n_kv_heads) / config.n_heads;
    s_head_size = config.dim / config.n_heads;

    /* OO M3: RAM budget preflight */
    if (g_cfg_oo_enable && g_oo_last_mode_valid &&
            (g_oo_last_mode == LLMK_OO_MODE_SAFE ||
             g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
        UINT64 sys_ram = llmk_get_conventional_ram_bytes_best_effort();
        if (sys_ram > 0) {
            const UINT64 reserve = 128ULL * 1024ULL * 1024ULL;
            UINT64 usable = (sys_ram > reserve) ? (sys_ram - reserve)
                                                 : (sys_ram * 3ULL) / 4ULL;

            UINT64 min_total_policy = 0;
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE)
                min_total_policy = 512ULL * 1024ULL * 1024ULL;
            else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED)
                min_total_policy = 640ULL * 1024ULL * 1024ULL;

            if (g_cfg_oo_min_total_mb >= 0) {
                min_total_policy = (UINT64)g_cfg_oo_min_total_mb * 1024ULL * 1024ULL;
            }

            int seq_from = config.seq_len;
            int seq = config.seq_len;
            for (int iter = 0; iter < 8; iter++) {
                if (seq < 64) seq = 64;

                UINTN n_floats_base_pf = 0;
                n_floats_base_pf += (UINTN)config.vocab_size * (UINTN)config.dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim * (UINTN)config.dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim * (UINTN)s_kv_dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim * (UINTN)s_kv_dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim * (UINTN)config.dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim * (UINTN)config.hidden_dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.hidden_dim * (UINTN)config.dim;
                n_floats_base_pf += (UINTN)config.n_layers  * (UINTN)config.dim * (UINTN)config.hidden_dim;
                n_floats_base_pf += (UINTN)config.dim;
                n_floats_base_pf += (UINTN)seq * (UINTN)s_head_size / 2;
                n_floats_base_pf += (UINTN)seq * (UINTN)s_head_size / 2;

                UINTN n_floats_with_cls_pf =
                    n_floats_base_pf + (UINTN)config.vocab_size * (UINTN)config.dim;

                int shared_pf = s_shared_classifier;
                if (!s_use_q8_blob && s_model_file_size > 0) {
                    UINT64 available = s_model_file_size;
                    UINT64 header_bytes = (UINT64)(7 * sizeof(int));
                    if (available > header_bytes) available -= header_bytes;
                    UINT64 bytes_base = (UINT64)n_floats_base_pf * sizeof(float);
                    UINT64 bytes_with = (UINT64)n_floats_with_cls_pf * sizeof(float);
                    if (available < bytes_with && available >= bytes_base) shared_pf = 1;
                    else if (available >= bytes_with) shared_pf = 0;
                }

                UINT64 weights_u64 = s_use_q8_blob
                    ? (UINT64)s_q8_blob_bytes
                    : (UINT64)(shared_pf ? n_floats_base_pf : n_floats_with_cls_pf)
                      * (UINT64)sizeof(float);

                UINT64 kv_bytes = llmk_calc_kv_bytes_for_seq(&config, seq, s_kv_dim);
                UINT64 state_u64 = llmk_calc_state_bytes_for_seq(&config, seq, s_kv_dim);

                UINT64 tokenizer_u64 = (UINT64)config.vocab_size *
                    ((UINT64)sizeof(char*) + (UINT64)sizeof(float));
                tokenizer_u64 += 4ULL * 1024ULL * 1024ULL;

                UINT64 slack_u64    = 16ULL * 1024ULL * 1024ULL;
                UINT64 scratch_u64  = 32ULL * 1024ULL * 1024ULL;
                UINT64 zonec_u64    =  8ULL * 1024ULL * 1024ULL;

                UINT64 acts_u64 =
                    (state_u64 >= kv_bytes ? (state_u64 - kv_bytes) : 0ULL)
                    + tokenizer_u64 + slack_u64;
                UINT64 total = weights_u64 + kv_bytes + scratch_u64 + acts_u64 + zonec_u64;

                UINT64 min_total = min_total_policy;
                if (min_total > 0 && total < min_total) total = min_total;

                if (total <= usable) break;

                int next = seq / 2;
                if (next < 64) { seq = 64; break; }
                seq = next;
            }

            if (seq != seq_from) {
                Print(L"OK: OO ram preflight: seq_len %d -> %d (mode=%s)\r\n",
                      seq_from, seq, llmk_oo_mode_name(g_oo_last_mode));
                config.seq_len = seq;
            }
        }
    }

    /* Compute total weights floats */
    UINTN n_floats_base = 0;
    n_floats_base += (UINTN)config.vocab_size * (UINTN)config.dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)s_kv_dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)s_kv_dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.hidden_dim * (UINTN)config.dim;
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim;
    n_floats_base += (UINTN)config.dim;
    n_floats_base += (UINTN)config.seq_len * (UINTN)s_head_size / 2;
    n_floats_base += (UINTN)config.seq_len * (UINTN)s_head_size / 2;

    UINTN n_floats_with_cls = n_floats_base +
        (UINTN)config.vocab_size * (UINTN)config.dim;

    if (s_model_file_size > 0) {
        UINT64 available = s_model_file_size;
        UINT64 header_bytes = (UINT64)(7 * sizeof(int));
        if (available > header_bytes) available -= header_bytes;
        UINT64 bytes_base = (UINT64)n_floats_base * sizeof(float);
        UINT64 bytes_with = (UINT64)n_floats_with_cls * sizeof(float);
        if (available < bytes_with && available >= bytes_base)      s_shared_classifier = 1;
        else if (available >= bytes_with)                            s_shared_classifier = 0;
    }

    UINTN n_floats = s_shared_classifier ? n_floats_base : n_floats_with_cls;
    s_weights_bytes = s_use_q8_blob ? (UINTN)s_q8_blob_bytes : (n_floats * sizeof(float));

    UINTN state_bytes = 0;
    state_bytes += (UINTN)config.dim * sizeof(float) * 3;
    state_bytes += (UINTN)config.hidden_dim * sizeof(float) * 2;
    state_bytes += (UINTN)config.dim * sizeof(float);
    state_bytes += (UINTN)s_kv_dim * sizeof(float) * 2;
    state_bytes += (UINTN)config.n_heads * (UINTN)config.seq_len * sizeof(float);
    state_bytes += (UINTN)config.vocab_size * sizeof(float);
    state_bytes += (UINTN)config.n_layers * (UINTN)config.seq_len *
                   (UINTN)s_kv_dim * sizeof(float) * 2;

    UINTN tokenizer_bytes = (UINTN)config.vocab_size * (sizeof(char*) + sizeof(float));
    tokenizer_bytes += 4 * 1024 * 1024;

    UINTN slack_bytes = 16 * 1024 * 1024;
    heap_size = s_weights_bytes + state_bytes + tokenizer_bytes + slack_bytes;
    if (heap_size < 100ULL * 1024ULL * 1024ULL) heap_size = 100ULL * 1024ULL * 1024ULL;

    /* Initialize LLM-Kernel Zone B arenas */
    {
        UINT64 zonec_bytes   =  8ULL * 1024ULL * 1024ULL;
        UINT64 scratch_bytes = 32ULL * 1024ULL * 1024ULL;
        UINT64 kv_bytes = (UINT64)config.n_layers * (UINT64)config.seq_len *
                          (UINT64)s_kv_dim * sizeof(float) * 2ULL;
        UINT64 weights_u64 = (UINT64)s_weights_bytes;
        UINT64 acts_u64 = (UINT64)(state_bytes - (UINTN)kv_bytes) +
                          (UINT64)tokenizer_bytes + (UINT64)slack_bytes;
        UINT64 total = weights_u64 + kv_bytes + scratch_bytes + acts_u64 + zonec_bytes;

        UINT64 default_min_total = (total > 768ULL * 1024ULL * 1024ULL)
            ? (1024ULL * 1024ULL * 1024ULL) : (768ULL * 1024ULL * 1024ULL);
        UINT64 min_total = default_min_total;
        if (g_cfg_oo_enable && g_oo_last_mode_valid) {
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE)
                min_total = 512ULL * 1024ULL * 1024ULL;
            else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED)
                min_total = 640ULL * 1024ULL * 1024ULL;
        }
        if (g_cfg_oo_enable && g_oo_last_mode_valid &&
                (g_oo_last_mode == LLMK_OO_MODE_SAFE ||
                 g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
            if (g_cfg_oo_min_total_mb >= 0) {
                min_total = (UINT64)g_cfg_oo_min_total_mb * 1024ULL * 1024ULL;
            }
        }
        if (total < min_total) total = min_total;

        LlmkZonesConfig zcfg;
        zcfg.total_bytes       = total;
        zcfg.weights_bytes     = weights_u64;
        zcfg.kv_bytes          = kv_bytes;
        zcfg.scratch_bytes     = scratch_bytes;
        zcfg.activations_bytes = acts_u64;
        zcfg.zone_c_bytes      = zonec_bytes;

        if (g_boot_verbose) {
            Print(L"[3/7] Init kernel zones (%d MB)...\r\n",
                  (int)(total / (1024 * 1024)));
        }

        EFI_STATUS status = llmk_zones_init(BS, &zcfg, &g_zones);
        if (EFI_ERROR(status) && min_total > 0 && total > min_total) {
            if (g_boot_verbose) {
                Print(L"[llmk] zones alloc failed, retrying with %d MB...\r\n",
                      (int)(min_total / (1024 * 1024)));
            }
            zcfg.total_bytes       = min_total;
            zcfg.weights_bytes     = 0;
            zcfg.kv_bytes          = 0;
            zcfg.scratch_bytes     = 0;
            zcfg.activations_bytes = 0;
            zcfg.zone_c_bytes      = 0;
            status = llmk_zones_init(BS, &zcfg, &g_zones);
        }
        if (EFI_ERROR(status)) {
            Print(L"ERROR: llmk_zones_init failed: %r\r\n", status);
            return status;
        }

        /* Init Zone C log (best-effort) */
        EFI_STATUS logst = llmk_log_init(&g_zones, &g_llmk_log);
        if (EFI_ERROR(logst)) {
            g_llmk_log.entries   = 0;
            g_llmk_log.capacity  = 0;
            g_llmk_log.write_idx = 0;
        }

        /* Init sentinel */
        LlmkSentinelConfig scfg;
        scfg.enabled              = TRUE;
        scfg.strict_mode          = FALSE;
        scfg.strict_alloc         = TRUE;
        scfg.strict_budget        = FALSE;
        scfg.max_cycles           = 0;
        scfg.max_cycles_prefill   = 0;
        scfg.max_cycles_decode    = 0;
        scfg.log_violations       = TRUE;

        status = llmk_sentinel_init(&g_sentinel, &g_zones,
                     (g_llmk_log.capacity ? &g_llmk_log : 0), &scfg);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: llmk_sentinel_init failed: %r\r\n", status);
            return status;
        }

        g_llmk_ready = 1;
        compatibilion_set_memory(&g_compatibilion, (uint64_t)g_zones.zone_b_size);

        if (g_boot_verbose) {
            llmk_zones_print(&g_zones);
            llmk_sentinel_print_status(&g_sentinel);
            Print(L"OK: Kernel allocator ready\r\n\r\n");
        }
    }

    /* Expose zones/sentinel/log via ctx for callers */
    ctx->zones    = g_zones;
    ctx->sentinel = g_sentinel;
    ctx->log      = g_llmk_log;

    ctx->phase3_ok = 1;
    return EFI_SUCCESS;
}

/* ============================================================
 * Phase 4: Tokenizer  (weight pointer mapping in the task mapping)
 * Despite the function name in efi_entry.h, this phase loads
 * weights per the task's Phase 4 responsibility.
 * ============================================================ */
EFI_STATUS efi_phase4_tokenizer(OoBootCtx *ctx) {
    llmk_overlay_stage(4, 7);

    if (g_boot_verbose) {
        Print(L"[4/7] Mapping weights...\r\n");
    }

    UINTN bytes_to_read = s_weights_bytes;
    void *weights_mem_raw = (void *)llmk_alloc_weights((UINT64)bytes_to_read, L"weights");
    if (weights_mem_raw == NULL) {
        Print(L"ERROR: OOM while allocating weights (%d MB needed).\r\n",
              (int)(bytes_to_read / (1024 * 1024)));
        Print(L"Hint: use a smaller model, or GGUF Q8_0 blob (gguf_q8_blob=1), "
              L"or reduce ctx_len in repl.cfg.\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    /* Zero-init weights struct */
    weights.kind = 0;
    weights.token_embedding_table = NULL;
    weights.rms_att_weight = NULL;
    weights.wq = NULL; weights.wk = NULL; weights.wv = NULL; weights.wo = NULL;
    weights.rms_ffn_weight = NULL;
    weights.w1 = NULL; weights.w2 = NULL; weights.w3 = NULL;
    weights.rms_final_weight = NULL;
    weights.wcls = NULL;
    weights.token_embedding_table_q8 = NULL;
    weights.wq_q8 = NULL; weights.wk_q8 = NULL; weights.wv_q8 = NULL;
    weights.wo_q8 = NULL; weights.w1_q8 = NULL; weights.w2_q8 = NULL;
    weights.w3_q8 = NULL; weights.wcls_q8 = NULL;
    weights.tok_embd_row_bytes = 0;
    weights.wq_layer_bytes = 0; weights.wk_layer_bytes = 0;
    weights.wv_layer_bytes = 0; weights.wo_layer_bytes = 0;
    weights.w1_layer_bytes = 0; weights.w2_layer_bytes = 0;
    weights.w3_layer_bytes = 0;

    EFI_STATUS status = EFI_SUCCESS;

    if (s_use_gguf_inference) {
        if (s_use_q8_blob) {
            status = llmk_gguf_load_into_llama2_q8_0_blob(
                s_model_file, s_gguf_plan, weights_mem_raw, s_q8_blob_bytes,
                config.dim, config.hidden_dim, config.n_layers, config.n_heads,
                config.n_kv_heads, config.vocab_size, config.seq_len,
                s_shared_classifier);
            if (s_gguf_plan) { llmk_gguf_free_plan(s_gguf_plan); s_gguf_plan = NULL; }
            if (EFI_ERROR(status)) {
                Print(L"ERROR: Failed to load GGUF Q8_0 blob weights (%r).\r\n", status);
                return EFI_LOAD_ERROR;
            }

            {
                const UINT64 A = 16;
                UINT8 *base = (UINT8 *)weights_mem_raw;
                UINT64 off = 0;
                UINT64 dim_u   = (UINT64)config.dim;
                UINT64 hid_u   = (UINT64)config.hidden_dim;
                UINT64 lay_u   = (UINT64)config.n_layers;
                UINT64 vocab_u = (UINT64)config.vocab_size;
                UINT64 kv_dim_u = (UINT64)s_kv_dim;
                UINT64 head_size_u = (UINT64)s_head_size;

                UINT64 tok_row = llmk_q8_0_row_bytes(config.dim);
                UINT64 wq_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 wk_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 wo_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 w1_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 w2_row  = llmk_q8_0_row_bytes(config.hidden_dim);
                UINT64 w3_row  = llmk_q8_0_row_bytes(config.dim);
                if (!tok_row || !wq_row || !wk_row || !wo_row ||
                    !w1_row  || !w2_row  || !w3_row) {
                    Print(L"ERROR: Q8_0 blob requires dims multiple of 32 "
                          L"(dim=%d hidden=%d).\r\n", config.dim, config.hidden_dim);
                    return EFI_UNSUPPORTED;
                }

                weights.kind = 1;
                weights.tok_embd_row_bytes = tok_row;
                weights.wq_layer_bytes = (UINT64)config.dim      * wq_row;
                weights.wk_layer_bytes = (UINT64)s_kv_dim        * wk_row;
                weights.wv_layer_bytes = (UINT64)s_kv_dim        * wk_row;
                weights.wo_layer_bytes = (UINT64)config.dim      * wo_row;
                weights.w1_layer_bytes = (UINT64)config.hidden_dim * w1_row;
                weights.w2_layer_bytes = (UINT64)config.dim      * w2_row;
                weights.w3_layer_bytes = (UINT64)config.hidden_dim * w3_row;

                off = llmk_align_up_u64(off, A);
                weights.token_embedding_table_q8 = base + (UINTN)off;
                off += vocab_u * tok_row;

                off = llmk_align_up_u64(off, A);
                weights.rms_att_weight = (float *)(base + (UINTN)off);
                off += lay_u * dim_u * 4ULL;

                off = llmk_align_up_u64(off, A);
                weights.wq_q8 = base + (UINTN)off;
                off += lay_u * weights.wq_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.wk_q8 = base + (UINTN)off;
                off += lay_u * weights.wk_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.wv_q8 = base + (UINTN)off;
                off += lay_u * weights.wv_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.wo_q8 = base + (UINTN)off;
                off += lay_u * weights.wo_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.rms_ffn_weight = (float *)(base + (UINTN)off);
                off += lay_u * dim_u * 4ULL;

                off = llmk_align_up_u64(off, A);
                weights.w1_q8 = base + (UINTN)off;
                off += lay_u * weights.w1_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.w2_q8 = base + (UINTN)off;
                off += lay_u * weights.w2_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.w3_q8 = base + (UINTN)off;
                off += lay_u * weights.w3_layer_bytes;

                off = llmk_align_up_u64(off, A);
                weights.rms_final_weight = (float *)(base + (UINTN)off);
                off += dim_u * 4ULL;

                off = llmk_align_up_u64(off, A);
                off += (UINT64)config.seq_len * head_size_u / 2ULL * 4ULL;
                off += (UINT64)config.seq_len * head_size_u / 2ULL * 4ULL;

                if (s_shared_classifier) {
                    weights.wcls_q8 = weights.token_embedding_table_q8;
                } else {
                    off = llmk_align_up_u64(off, A);
                    weights.wcls_q8 = base + (UINTN)off;
                    off += vocab_u * tok_row;
                }

                (void)hid_u; (void)kv_dim_u;
            }
        } else {
            float *weights_mem = (float *)weights_mem_raw;
            status = llmk_gguf_load_into_llama2_layout(
                s_model_file, s_gguf_plan, weights_mem,
                config.dim, config.hidden_dim, config.n_layers, config.n_heads,
                config.n_kv_heads, config.vocab_size, config.seq_len,
                s_shared_classifier);
            if (s_gguf_plan) { llmk_gguf_free_plan(s_gguf_plan); s_gguf_plan = NULL; }
            if (EFI_ERROR(status)) {
                Print(L"ERROR: Failed to load GGUF weights (%r).\r\n", status);
                return EFI_LOAD_ERROR;
            }

            float *weights_ptr = weights_mem;
            weights.kind = 0;
            weights.token_embedding_table = weights_ptr;
            weights_ptr += config.vocab_size * config.dim;
            weights.rms_att_weight = weights_ptr;
            weights_ptr += config.n_layers * config.dim;
            weights.wq = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.dim;
            weights.wk = weights_ptr;
            weights_ptr += config.n_layers * config.dim * s_kv_dim;
            weights.wv = weights_ptr;
            weights_ptr += config.n_layers * config.dim * s_kv_dim;
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
            weights_ptr += config.seq_len * s_head_size / 2;
            weights_ptr += config.seq_len * s_head_size / 2;
            weights.wcls = s_shared_classifier ? weights.token_embedding_table : weights_ptr;
        }
    } else {
        float *weights_mem = (float *)weights_mem_raw;
        status = read_exact(s_model_file, weights_mem, bytes_to_read);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: Failed to read weights "
                  L"(need model file + enough RAM).\r\n");
            return EFI_LOAD_ERROR;
        }

        float *weights_ptr = weights_mem;
        weights.kind = 0;
        weights.token_embedding_table = weights_ptr;
        weights_ptr += config.vocab_size * config.dim;
        weights.rms_att_weight = weights_ptr;
        weights_ptr += config.n_layers * config.dim;
        weights.wq = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.dim;
        weights.wk = weights_ptr;
        weights_ptr += config.n_layers * config.dim * s_kv_dim;
        weights.wv = weights_ptr;
        weights_ptr += config.n_layers * config.dim * s_kv_dim;
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
        weights_ptr += config.seq_len * s_head_size / 2;
        weights_ptr += config.seq_len * s_head_size / 2;
        weights.wcls = s_shared_classifier ? weights.token_embedding_table : weights_ptr;
    }

    uefi_call_wrapper(s_model_file->Close, 1, s_model_file);
    s_model_file = NULL;

    if (g_boot_verbose) {
        Print(L"OK: Weights mapped\r\n\r\n");
    }

    llmk_boot_mark(L"weights_mapped");
    ctx->phase4_ok = 1;
    return EFI_SUCCESS;
}

/* ============================================================
 * Phase 5: Safety  (state buffers + tokenizer in the task mapping)
 * ============================================================ */
EFI_STATUS efi_phase5_safety(OoBootCtx *ctx) {
    EFI_FILE_HANDLE Root = ctx->efi_root;

    llmk_overlay_stage(5, 7);

    if (g_boot_verbose) {
        Print(L"[5/7] Allocating state buffers...\r\n");
    }

    /* ── State buffers ─────────────────────────────────────────────────── */
    int ctx_min = 64;
    int ctx_try = config.seq_len;
    int alloc_ok = 0;
    while (!alloc_ok) {
        state.x       = (float *)simple_alloc(config.dim * sizeof(float));
        state.xb      = (float *)simple_alloc(config.dim * sizeof(float));
        state.xb2     = (float *)simple_alloc(config.dim * sizeof(float));
        state.hb      = (float *)simple_alloc(config.hidden_dim * sizeof(float));
        state.hb2     = (float *)simple_alloc(config.hidden_dim * sizeof(float));
        state.q       = (float *)simple_alloc(config.dim * sizeof(float));
        state.k       = (float *)simple_alloc(s_kv_dim * sizeof(float));
        state.v       = (float *)simple_alloc(s_kv_dim * sizeof(float));
        state.att     = (float *)simple_alloc(config.n_heads * config.seq_len * sizeof(float));
        state.logits  = (float *)simple_alloc(config.vocab_size * sizeof(float));
        state.key_cache = (float *)llmk_alloc_kv(
            (UINT64)config.n_layers * (UINT64)config.seq_len *
            (UINT64)s_kv_dim * sizeof(float), L"key cache");
        state.value_cache = (float *)llmk_alloc_kv(
            (UINT64)config.n_layers * (UINT64)config.seq_len *
            (UINT64)s_kv_dim * sizeof(float), L"value cache");

        alloc_ok = (state.x && state.xb && state.xb2 && state.hb && state.hb2 &&
                    state.q && state.k && state.v && state.att && state.logits &&
                    state.key_cache && state.value_cache);
        if (alloc_ok) break;

        Print(L"\r\nERROR: OOM while allocating state/KV (seq_len=%d).\r\n",
              config.seq_len);
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

    /* ── [6/7] Tokenizer ──────────────────────────────────────────────── */
    llmk_overlay_stage(6, 7);

    if (g_boot_verbose) {
        Print(L"[6/7] Loading tokenizer...\r\n");
    }

    EFI_FILE_HANDLE TokFile;
    TokFile = NULL;
    EFI_STATUS status = llmk_open_read_with_fat83_fallback(
            Root, L"tokenizer.bin", &TokFile, NULL, 0, L"tokenizer");
    if (EFI_ERROR(status) || !TokFile) {
        Print(L"ERROR: Tokenizer file not found (%r)\r\n", status);
        return status;
    }

    UINTN bytes_to_read = sizeof(int);
    uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read,
                      &tokenizer.max_token_length);

    tokenizer.vocab_size   = config.vocab_size;
    tokenizer.vocab        = (char **)simple_alloc(config.vocab_size * sizeof(char *));
    tokenizer.vocab_scores = (float *)simple_alloc(config.vocab_size * sizeof(float));

    for (int i = 0; i < config.vocab_size; i++) {
        if (((UINT32)i & 0xFFu) == 0u) {
            InterfaceFx_ProgressBytes((UINTN)(i + 1), (UINTN)config.vocab_size);
        }
        bytes_to_read = sizeof(float);
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read,
                          &tokenizer.vocab_scores[i]);

        int len;
        bytes_to_read = sizeof(int);
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &len);

        tokenizer.vocab[i] = (char *)simple_alloc(len + 1);
        bytes_to_read = len;
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read,
                          tokenizer.vocab[i]);
        tokenizer.vocab[i][len] = '\0';
    }

    uefi_call_wrapper(TokFile->Close, 1, TokFile);

    InterfaceFx_End();

    llmk_boot_mark(L"tokenizer_loaded");

    if (g_boot_verbose) {
        Print(L"OK: Tokenizer loaded (%d tokens)\r\n\r\n", tokenizer.vocab_size);
        llmk_boot_print_timing_summary();
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

    ctx->phase5_ok = 1;
    return EFI_SUCCESS;
}

/* ============================================================
 * REPL body (unity-include)
 *
 * repl_body.c references config/weights/state/tokenizer directly.
 * Including it here (in the same TU) gives it access to the statics
 * declared above AND to all god-file types (Config, TransformerWeights...).
 * ============================================================ */

/* Aliases so repl_body.c code can use 'Root' and 'model_filename'
 * as if they were locals (they were locals in the original efi_main). */
#ifndef Root
#define Root g_root
#endif
#ifndef model_filename
#define model_filename g_loaded_model_path16
#endif

#include "oo-kernel/repl/repl_body.c"

/* End-of-compilation marker. If this causes "expected ';' before token"
 * or similar, there's an unclosed block from the repl_body.c include. */
extern int _efi_phases_end_marker;
