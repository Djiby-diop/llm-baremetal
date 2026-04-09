static void llmk_repl_no_model_loop(void) {
    // Minimal repl.cfg parsing for autorun in no-model mode.
    int autorun_autostart = 0;
    int autorun_shutdown_when_done = 0;
    CHAR16 autorun_file[96];
    StrCpy(autorun_file, L"llmk-autorun.txt");
    if (g_root) {
        void *raw = NULL;
        UINTN raw_len = 0;
        EFI_STATUS st = llmk_read_entire_file_best_effort(L"repl.cfg", &raw, &raw_len);
        if (!EFI_ERROR(st) && raw && raw_len > 0) {
            // Make a NUL-terminated ASCII buffer.
            char *buf = NULL;
            EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
            if (!EFI_ERROR(st2) && buf) {
                CopyMem(buf, raw, raw_len);
                buf[raw_len] = 0;

                // Parse a very small subset of keys.
                char *p = buf;
                while (*p) {
                    char *line = p;
                    while (*p && *p != '\n') p++;
                    if (*p == '\n') { *p = 0; p++; }
                    for (char *c = line; *c; c++) { if (*c == '\r') { *c = 0; break; } }
                    // Trim leading spaces
                    while (*line == ' ' || *line == '\t') line++;
                    if (line[0] == 0 || line[0] == '#' || line[0] == ';') continue;
                    // Strip inline comment
                    for (char *c = line; *c; c++) { if (*c == '#') { *c = 0; break; } }
                    // key=value
                    char *eq = line;
                    while (*eq && *eq != '=') eq++;
                    if (*eq != '=') continue;
                    *eq = 0;
                    char *key = line;
                    char *val = eq + 1;
                    // trim key/val
                    while (*key == ' ' || *key == '\t') key++;
                    while (*val == ' ' || *val == '\t') val++;
                    char *kend = key;
                    while (*kend) kend++;
                    while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
                    *kend = 0;
                    char *vend = val;
                    while (*vend) vend++;
                    while (vend > val && (vend[-1] == ' ' || vend[-1] == '\t')) vend--;
                    *vend = 0;
                    // lowercase key (ASCII)
                    for (char *k = key; *k; k++) {
                        if (*k >= 'A' && *k <= 'Z') *k = (char)(*k - 'A' + 'a');
                    }

                    if (my_strncmp(key, "autorun_autostart", 17) == 0 && key[17] == 0) {
                        autorun_autostart = (val[0] == '1' || val[0] == 't' || val[0] == 'T' || val[0] == 'y' || val[0] == 'Y');
                    } else if (my_strncmp(key, "autorun_shutdown_when_done", 26) == 0 && key[26] == 0) {
                        autorun_shutdown_when_done = (val[0] == '1' || val[0] == 't' || val[0] == 'T' || val[0] == 'y' || val[0] == 'Y');
                    } else if (my_strncmp(key, "autorun_file", 12) == 0 && key[12] == 0) {
                        if (val[0]) {
                            ascii_to_char16(autorun_file, val, (int)(sizeof(autorun_file) / sizeof(autorun_file[0])));
                        }
                    } else if (my_strncmp(key, "max_new_tokens", 14) == 0 && key[14] == 0) {
                        int v = 0;
                        for (const char *vp = val; *vp >= '0' && *vp <= '9'; vp++)
                            v = v * 10 + (*vp - '0');
                        if (v > 0 && v <= 4096) g_max_new_tokens = v;
                    } else if (my_strncmp(key, "mind_halt_enabled", 17) == 0 && key[17] == 0) {
                        int b = 0;
                        if (llmk_cfg_parse_bool(val, &b)) {
                            g_mind_runtime_halt_enabled = (b != 0);
                        }
                    } else if (my_strncmp(key, "mind_halt_threshold", 19) == 0 && key[19] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 0.0f) v = 0.0f;
                            if (v > 1.0f) v = 1.0f;
                            g_mind_runtime_halt_threshold = v;
                        }
                    } else if (my_strncmp(key, "attach_policy_external_temp", 27) == 0 && key[27] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 0.10f) v = 0.10f;
                            if (v > 5.0f) v = 5.0f;
                            g_attach_policy_external_cfg.temperature_milli = (int)(v * 1000.0f + 0.5f);
                        }
                    } else if (my_strncmp(key, "attach_policy_external_top_p", 28) == 0 && key[28] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 0.0f) v = 0.0f;
                            if (v > 1.0f) v = 1.0f;
                            g_attach_policy_external_cfg.top_p_milli = (int)(v * 1000.0f + 0.5f);
                        }
                    } else if (my_strncmp(key, "attach_policy_external_rep", 26) == 0 && key[26] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 1.0f) v = 1.0f;
                            if (v > 3.0f) v = 3.0f;
                            g_attach_policy_external_cfg.repetition_penalty_milli = (int)(v * 1000.0f + 0.5f);
                        }
                    } else if (my_strncmp(key, "attach_policy_external_max_tokens", 33) == 0 && key[33] == 0) {
                        int v = 0;
                        if (llmk_cfg_parse_i32(val, &v)) {
                            if (v < 1) v = 1;
                            if (v > 512) v = 512;
                            g_attach_policy_external_cfg.max_tokens = v;
                        }
                    } else if (my_strncmp(key, "attach_policy_dual_temp", 23) == 0 && key[23] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 0.10f) v = 0.10f;
                            if (v > 5.0f) v = 5.0f;
                            g_attach_policy_dual_cfg.temperature_milli = (int)(v * 1000.0f + 0.5f);
                        }
                    } else if (my_strncmp(key, "attach_policy_dual_top_p", 24) == 0 && key[24] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 0.0f) v = 0.0f;
                            if (v > 1.0f) v = 1.0f;
                            g_attach_policy_dual_cfg.top_p_milli = (int)(v * 1000.0f + 0.5f);
                        }
                    } else if (my_strncmp(key, "attach_policy_dual_rep", 22) == 0 && key[22] == 0) {
                        float v = 0.0f;
                        if (llmk_cfg_parse_f32(val, &v)) {
                            if (v < 1.0f) v = 1.0f;
                            if (v > 3.0f) v = 3.0f;
                            g_attach_policy_dual_cfg.repetition_penalty_milli = (int)(v * 1000.0f + 0.5f);
                        }
                    } else if (my_strncmp(key, "attach_policy_dual_max_tokens", 29) == 0 && key[29] == 0) {
                        int v = 0;
                        if (llmk_cfg_parse_i32(val, &v)) {
                            if (v < 1) v = 1;
                            if (v > 512) v = 512;
                            g_attach_policy_dual_cfg.max_tokens = v;
                        }
                    }
                }

                uefi_call_wrapper(BS->FreePool, 1, buf);
            }
        }
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
    }

    Print(L"OK: REPL ready (no model). Type /help\r\n");

    // Initialize SomaMind (Router + DNA + SMB + Dream + Meta + Swarm stub)
    soma_router_init(&g_soma_router);
    soma_dna_init_default(&g_soma_dna);
    soma_smb_init(&g_soma_smb);
    soma_dream_init(&g_soma_dream);
    soma_meta_init(&g_soma_meta);
    g_multireal_enabled = 0;
    g_multireal_stats.solar_wins = g_multireal_stats.lunar_wins =
        g_multireal_stats.argmax_wins = g_multireal_stats.total_tokens = 0;
    // Swarm needs vocab_size — initialized fully on /ssm_load
    g_soma_swarm.enabled = 0;
    g_soma_swarm.ready   = 0;
    soma_reflex_init(&g_soma_reflex);
    soma_logic_init(&g_soma_logic);
    soma_memory_init(&g_soma_memory);
    // Phase I: load persistent journal → pre-populate ring buffer
    if (g_root) {
        unsigned int prev_turns = 0;
        int jloaded = soma_journal_load(&g_soma_memory, g_root, &prev_turns);
        if (jloaded > 0) {
            g_soma_journal_total_turns = prev_turns;
            Print(L"[SomaJournal] Loaded %d entries from disk (total_turns=%d, sessions=%d)\r\n",
                  jloaded, (int)prev_turns, g_soma_memory.boot_count);
        } else if (jloaded == -1) {
            Print(L"[SomaJournal] No journal file found (first boot)\r\n");
        } else {
            Print(L"[SomaJournal] WARNING: journal corrupt or unreadable\r\n");
        }
    }
    // Phase O: load persistent DNA → resume evolution across reboots
    if (g_root) {
        int dret = soma_dna_load(&g_soma_dna, g_root);
        if (dret == SOMA_DNA_PERSIST_OK) {
            Print(L"[SomaDNA] Loaded: gen=%d bias=%.2f conf=%.2f hash=0x%08X\r\n",
                  (int)g_soma_dna.generation,
                  (double)g_soma_dna.cognition_bias,
                  (double)g_soma_dna.avg_confidence,
                  soma_dna_hash(&g_soma_dna));
        } else if (dret == SOMA_DNA_PERSIST_NOT_FOUND) {
            Print(L"[SomaDNA] No soma_dna.bin found (first boot, defaults applied)\r\n");
        } else {
            Print(L"[SomaDNA] WARNING: soma_dna.bin corrupt — using defaults\r\n");
        }
    }
    // Phase J: init cortex (loaded later via /cortex_load)
    soma_cortex_init(&g_soma_cortex);
    // Phase M: init warden pressure bridge
    soma_warden_init(&g_soma_warden);
    // Phase N: init session fitness tracker
    soma_session_init(&g_soma_session);
    // Phase W: init speculative decoding (buf allocated on /ssm_load)
    soma_spec_init(&g_soma_spec, 0, 0);
    g_soma_spec_buf = 0;
    // Phase Y: init distributed swarm net (peer_id=0, no fixed addr — single instance)
    soma_swarm_net_init(&g_soma_swarm_net, 0, 0ULL, 0);
    // Phase P: enable immunion in record mode (logs threat patterns; no auto-react)
    immunion_set_mode(&g_immunion, IMMUNION_MODE_RECORD);
    // Phase R: enable symbion in watch mode (feeds performance samples)
    symbion_set_mode(&g_symbion, SYMBION_MODE_WATCH);
    // Phase S: enable pheromion in trace mode (tracks hot domain/route paths)
    pheromion_set_mode(&g_pheromion, PHEROMION_MODE_TRACE);
    g_soma_initialized = 1;
    Print(L"SomaMind: A-Z initialized (gen=%d hash=0x%08X)\r\n\r\n",
          g_soma_dna.generation, soma_dna_hash(&g_soma_dna));

    // Best-effort autorun (no-model).
    if (autorun_autostart) {
        (void)llmk_autorun_start(autorun_file, autorun_shutdown_when_done);
    }

    while (1) { // SAFE: intentional REPL event loop; exits via explicit shutdown/reset paths.
        CHAR16 user_input[512];
        char prompt[512];
        prompt[0] = 0;

        /* ── Dreamion idle hook ─────────────────────────────────────────
         * Tick the dream engine on every REPL iteration.
         * If there is no input (prompt == 0 at end of loop), dreamion_tick()
         * will count this as an idle cycle.  When user types something,
         * dreamion_tick_active() wakes the dream engine.
         * A lightweight dreamion_step() runs one task per idle iteration.
         * ---------------------------------------------------------------- */
        dreamion_tick(&g_dreamion, 1);
        if (g_dreamion.mode != DREAMION_MODE_OFF && !g_dreamion.awake) {
            dreamion_step(&g_dreamion);
            /* Apply pending DNA mutation to SomaDNA if available */
            if (dreamion_has_dna_mutation(&g_dreamion)) {
                float bias_d = 0.0f, temp_d = 0.0f;
                dreamion_pop_dna_mutation(&g_dreamion, &bias_d, &temp_d);
                /* Apply: g_soma_dna.cognition_bias += bias_d (best-effort) */
                (void)bias_d; (void)temp_d; /* applied by inference layer */
            }
        }

        // Autorun: consume next scripted line if active.
        if (llmk_autorun_next_line(prompt, (int)sizeof(prompt))) {
            CHAR16 p16[540];
            ascii_to_char16(p16, prompt, (int)(sizeof(p16) / sizeof(p16[0])));
            Print(L"llmk (autorun)> %s\r\n", p16);
        } else if (llmk_autorun_finish_if_eof()) {
            // EOF handled (stop/shutdown); loop continues if not shutdown.
            continue;
        } else {
            Print(L"llmk> ");
            read_user_input(user_input, 512);
            char16_to_char(prompt, user_input, 512);
            /* User activity — wake dreamion */
            dreamion_tick_active(&g_dreamion, DREAMION_WAKE_THRESH);
        }
        if (prompt[0] == 0) continue;

        if (my_strncmp(prompt, "/help", 5) == 0 || my_strncmp(prompt, "/commands", 9) == 0) {
            llmk_print_no_model_help();
            continue;
        }
        if (my_strncmp(prompt, "/verbose", 8) == 0) {
            int i = 8;
            while (prompt[i] == ' ') i++;
            if (prompt[i] >= '0' && prompt[i] <= '2') {
                g_boot_verbose = prompt[i] - '0';
            } else {
                g_boot_verbose = (g_boot_verbose + 1) % 3;
            }
            Print(L"\r\nverbose=%d (%s)\r\n\r\n", g_boot_verbose,
                  g_boot_verbose == 0 ? L"quiet" :
                  g_boot_verbose == 1 ? L"normal" : L"debug");
            continue;
        }
        if (my_strncmp(prompt, "/diag", 5) == 0) {
            llmk_print_diag();
            continue;
        }
        if (my_strncmp(prompt, "/temp ", 6) == 0 || my_strncmp(prompt, "/temperature ", 12) == 0) {
            int i = (prompt[1] == 'e' && prompt[5] == ' ') ? 6 : 12;
            float val = 0.0f;
            while (prompt[i] >= '0' && prompt[i] <= '9') { val = val * 10.0f + (prompt[i] - '0'); i++; }
            if (prompt[i] == '.') { i++; float frac = 0.1f; while (prompt[i] >= '0' && prompt[i] <= '9') { val += (prompt[i] - '0') * frac; frac /= 10.0f; i++; } }
            if (val < 0.01f) val = 0.01f;
            if (val > 5.0f) val = 5.0f;
            if (g_oosi_v3_valid) g_oosi_v3_ctx.temperature = val;
            Print(L"\r\ntemperature=%d.%02d\r\n\r\n", (int)val, (int)((val - (int)val) * 100.0f));
            continue;
        }
        if (my_strncmp(prompt, "/top_p ", 7) == 0) {
            int i = 7;
            float val = 0.0f;
            while (prompt[i] >= '0' && prompt[i] <= '9') { val = val * 10.0f + (prompt[i] - '0'); i++; }
            if (prompt[i] == '.') { i++; float frac = 0.1f; while (prompt[i] >= '0' && prompt[i] <= '9') { val += (prompt[i] - '0') * frac; frac /= 10.0f; i++; } }
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
            if (g_oosi_v3_valid) g_oosi_v3_ctx.top_p = val;
            Print(L"\r\ntop_p=%d.%02d\r\n\r\n", (int)val, (int)((val - (int)val) * 100.0f));
            continue;
        }
        if (my_strncmp(prompt, "/rep_penalty ", 13) == 0) {
            int i = 13;
            float val = 0.0f;
            while (prompt[i] >= '0' && prompt[i] <= '9') { val = val * 10.0f + (prompt[i] - '0'); i++; }
            if (prompt[i] == '.') { i++; float frac = 0.1f; while (prompt[i] >= '0' && prompt[i] <= '9') { val += (prompt[i] - '0') * frac; frac /= 10.0f; i++; } }
            if (val < 1.0f) val = 1.0f;
            if (val > 3.0f) val = 3.0f;
            if (g_oosi_v3_valid) g_oosi_v3_ctx.repetition_penalty = val;
            Print(L"\r\nrep_penalty=%d.%02d\r\n\r\n", (int)val, (int)((val - (int)val) * 100.0f));
            continue;
        }
        if (my_strncmp(prompt, "/max_tokens ", 12) == 0) {
            int i = 12, val = 0;
            while (prompt[i] >= '0' && prompt[i] <= '9') { val = val * 10 + (prompt[i] - '0'); i++; }
            if (val < 1) val = 1;
            if (val > 512) val = 512;
            if (g_oosi_v3_valid) g_oosi_v3_ctx.max_tokens = val;
            Print(L"\r\nmax_tokens=%d\r\n\r\n", val);
            continue;
        }
        if (my_strncmp(prompt, "/ssm_params", 11) == 0) {
            if (g_oosi_v3_valid) {
                Print(L"\r\nSSM sampling params:\r\n");
                Print(L"  temperature    = %d.%02d\r\n", (int)g_oosi_v3_ctx.temperature, (int)((g_oosi_v3_ctx.temperature - (int)g_oosi_v3_ctx.temperature) * 100.0f));
                Print(L"  top_p          = %d.%02d\r\n", (int)g_oosi_v3_ctx.top_p, (int)((g_oosi_v3_ctx.top_p - (int)g_oosi_v3_ctx.top_p) * 100.0f));
                Print(L"  rep_penalty    = %d.%02d\r\n", (int)g_oosi_v3_ctx.repetition_penalty, (int)((g_oosi_v3_ctx.repetition_penalty - (int)g_oosi_v3_ctx.repetition_penalty) * 100.0f));
                Print(L"  max_tokens     = %d\r\n", g_oosi_v3_ctx.max_tokens);
                Print(L"  halt_threshold = %d.%02d\r\n", (int)g_oosi_v3_ctx.halt_threshold, (int)((g_oosi_v3_ctx.halt_threshold - (int)g_oosi_v3_ctx.halt_threshold) * 100.0f));
                Print(L"  rng_state      = 0x%08X\r\n\r\n", g_oosi_v3_ctx.rng_state);
                llmk_attach_route_policy_print_status();
            } else {
                Print(L"\r\nNo SSM model loaded. Use /ssm_load first.\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/attach_policy_audit", 20) == 0) {
            llmk_attach_route_policy_print_audit();
            continue;
        }
        if (my_strncmp(prompt, "/attach_policy_diff", 19) == 0) {
            llmk_attach_route_policy_print_diff();
            continue;
        }
        if (my_strncmp(prompt, "/attach_policy_sync_force", 25) == 0) {
            int changed_external = 0;
            int changed_dual = 0;
            EFI_STATUS ast = llmk_attach_route_policy_load_best_effort(&changed_external, &changed_dual);
            if (EFI_ERROR(ast)) {
                Print(L"\r\n[AttachPolicy] warning: forced sync from repl.cfg failed (%r)\r\n\r\n", ast);
            } else {
                llmk_attach_route_policy_print_apply_command_result(LLMK_ATTACH_POLICY_APPLY_SYNC_FORCE, 1, changed_external, changed_dual);
            }
            continue;
        }
        if (my_strncmp(prompt, "/attach_policy_sync", 19) == 0) {
            int was_needed = 0;
            int changed_external = 0;
            int changed_dual = 0;
            EFI_STATUS ast = llmk_attach_route_policy_apply_saved_if_needed_best_effort(&was_needed, &changed_external, &changed_dual);
            if (EFI_ERROR(ast)) {
                Print(L"\r\n[AttachPolicy] warning: sync from repl.cfg failed (%r)\r\n\r\n", ast);
            } else if (!was_needed) {
                llmk_attach_route_policy_print_apply_command_result(LLMK_ATTACH_POLICY_APPLY_SYNC, 0, changed_external, changed_dual);
            } else {
                llmk_attach_route_policy_print_apply_command_result(LLMK_ATTACH_POLICY_APPLY_SYNC, 1, changed_external, changed_dual);
            }
            continue;
        }
        if (my_strncmp(prompt, "/attach_policy", 14) == 0) {
            const char *arg = prompt + 14;
            EFI_STATUS persist_st = EFI_SUCCESS;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (!arg[0] || my_strcmp(arg, "status") == 0) {
                llmk_attach_route_policy_print_status();
                continue;
            }
            if (my_strcmp(arg, "audit") == 0) {
                llmk_attach_route_policy_print_audit();
                continue;
            }
            if (my_strcmp(arg, "diff") == 0) {
                llmk_attach_route_policy_print_diff();
                continue;
            }
            if (my_strcmp(arg, "sync") == 0) {
                int was_needed = 0;
                int changed_external = 0;
                int changed_dual = 0;
                EFI_STATUS ast = llmk_attach_route_policy_apply_saved_if_needed_best_effort(&was_needed, &changed_external, &changed_dual);
                if (EFI_ERROR(ast)) {
                    Print(L"\r\n[AttachPolicy] warning: sync from repl.cfg failed (%r)\r\n\r\n", ast);
                } else if (!was_needed) {
                    llmk_attach_route_policy_print_apply_command_result(LLMK_ATTACH_POLICY_APPLY_SYNC, 0, changed_external, changed_dual);
                } else {
                    llmk_attach_route_policy_print_apply_command_result(LLMK_ATTACH_POLICY_APPLY_SYNC, 1, changed_external, changed_dual);
                }
                continue;
            }
            if (my_strcmp(arg, "sync_force") == 0) {
                int changed_external = 0;
                int changed_dual = 0;
                EFI_STATUS ast = llmk_attach_route_policy_load_best_effort(&changed_external, &changed_dual);
                if (EFI_ERROR(ast)) {
                    Print(L"\r\n[AttachPolicy] warning: forced sync from repl.cfg failed (%r)\r\n\r\n", ast);
                } else {
                    llmk_attach_route_policy_print_apply_command_result(LLMK_ATTACH_POLICY_APPLY_SYNC_FORCE, 1, changed_external, changed_dual);
                }
                continue;
            }
            if (my_strncmp(arg, "reset", 5) == 0 && (arg[5] == 0 || arg[5] == ' ' || arg[5] == '\t')) {
                SomaRoute route = SOMA_ROUTE_EXTERNAL;
                const char *route_arg = arg + 5;
                while (*route_arg == ' ' || *route_arg == '\t') route_arg++;
                if (!route_arg[0]) {
                    llmk_attach_route_policy_reset(SOMA_ROUTE_EXTERNAL);
                    llmk_attach_route_policy_reset(SOMA_ROUTE_DUAL);
                    persist_st = llmk_attach_route_policy_persist_best_effort();
                    Print(L"\r\n[AttachPolicy] reset routes=external,dual");
                    if (EFI_ERROR(persist_st)) {
                        Print(L" (persist=warn status=%r)", persist_st);
                    }
                    Print(L"\r\n\r\n");
                    continue;
                }
                if (!llmk_attach_route_policy_parse_route(route_arg, &route)) {
                    Print(L"\r\nUsage: /attach_policy reset [external|dual]\r\n\r\n");
                    continue;
                }
                llmk_attach_route_policy_reset(route);
                persist_st = llmk_attach_route_policy_persist_best_effort();
                Print(L"\r\n[AttachPolicy] reset route=%s", llmk_soma_route_name_wide(route));
                if (EFI_ERROR(persist_st)) {
                    Print(L" (persist=warn status=%r)", persist_st);
                }
                Print(L"\r\n\r\n");
                continue;
            }
            {
                char route_buf[16];
                int ri = 0;
                SomaRoute route = SOMA_ROUTE_EXTERNAL;
                LlmkAttachRoutePolicyConfig *slot = NULL;
                float temp = 0.0f;
                float top_p = 0.0f;
                float rep = 0.0f;
                int max_tokens = 0;
                while (arg[ri] && arg[ri] != ' ' && arg[ri] != '\t' && ri + 1 < (int)sizeof(route_buf)) {
                    route_buf[ri] = arg[ri];
                    ri++;
                }
                route_buf[ri] = 0;
                if (!llmk_attach_route_policy_parse_route(route_buf, &route)) {
                    Print(L"\r\nUsage: /attach_policy [status|audit|diff|sync|sync_force|reset [external|dual]|<external|dual> <temp> <top_p> <rep> <max_tokens>]\r\n\r\n");
                    continue;
                }
                arg += ri;
                while (*arg == ' ' || *arg == '\t') arg++;
                if (!llmk_cfg_parse_f32(arg, &temp)) {
                    Print(L"\r\nUsage: /attach_policy [status|audit|diff|sync|sync_force|reset [external|dual]|<external|dual> <temp> <top_p> <rep> <max_tokens>]\r\n\r\n");
                    continue;
                }
                while (*arg && *arg != ' ' && *arg != '\t') arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
                if (!llmk_cfg_parse_f32(arg, &top_p)) {
                    Print(L"\r\nUsage: /attach_policy [status|audit|diff|sync|sync_force|reset [external|dual]|<external|dual> <temp> <top_p> <rep> <max_tokens>]\r\n\r\n");
                    continue;
                }
                while (*arg && *arg != ' ' && *arg != '\t') arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
                if (!llmk_cfg_parse_f32(arg, &rep)) {
                    Print(L"\r\nUsage: /attach_policy [status|audit|diff|sync|sync_force|reset [external|dual]|<external|dual> <temp> <top_p> <rep> <max_tokens>]\r\n\r\n");
                    continue;
                }
                while (*arg && *arg != ' ' && *arg != '\t') arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
                if (!llmk_cfg_parse_i32(arg, &max_tokens)) {
                    Print(L"\r\nUsage: /attach_policy [status|audit|diff|sync|sync_force|reset [external|dual]|<external|dual> <temp> <top_p> <rep> <max_tokens>]\r\n\r\n");
                    continue;
                }

                if (temp < 0.10f) temp = 0.10f;
                if (temp > 5.0f) temp = 5.0f;
                if (top_p < 0.0f) top_p = 0.0f;
                if (top_p > 1.0f) top_p = 1.0f;
                if (rep < 1.0f) rep = 1.0f;
                if (rep > 3.0f) rep = 3.0f;
                if (max_tokens < 1) max_tokens = 1;
                if (max_tokens > 512) max_tokens = 512;

                slot = llmk_attach_route_policy_config_slot(route);
                if (!slot) {
                    Print(L"\r\nUsage: /attach_policy [status|audit|diff|sync|sync_force|reset [external|dual]|<external|dual> <temp> <top_p> <rep> <max_tokens>]\r\n\r\n");
                    continue;
                }
                slot->temperature_milli = (int)(temp * 1000.0f + 0.5f);
                slot->top_p_milli = (int)(top_p * 1000.0f + 0.5f);
                slot->repetition_penalty_milli = (int)(rep * 1000.0f + 0.5f);
                slot->max_tokens = max_tokens;
                persist_st = llmk_attach_route_policy_persist_best_effort();

                Print(L"\r\n[AttachPolicy] route=%s temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d",
                      llmk_soma_route_name_wide(route),
                      slot->temperature_milli / 1000,
                      slot->temperature_milli % 1000,
                      slot->top_p_milli / 1000,
                      slot->top_p_milli % 1000,
                      slot->repetition_penalty_milli / 1000,
                      slot->repetition_penalty_milli % 1000,
                      slot->max_tokens);
                if (EFI_ERROR(persist_st)) {
                    Print(L" (persist=warn status=%r)", persist_st);
                }
                Print(L"\r\n\r\n");
                continue;
            }
        }
        if (my_strncmp(prompt, "/seed ", 6) == 0) {
            unsigned int val = 0;
            int i = 6;
            while (prompt[i] >= '0' && prompt[i] <= '9') {
                val = val * 10u + (unsigned int)(prompt[i] - '0');
                i++;
            }
            if (val == 0) val = 1;
            if (g_oosi_v3_valid) g_oosi_v3_ctx.rng_state = val ^ 0xDEADBEEFu;
            Print(L"\r\nSSM seed=%u (rng_state=0x%08X)\r\n\r\n",
                  val, g_oosi_v3_valid ? g_oosi_v3_ctx.rng_state : 0);
            continue;
        }
        if (my_strncmp(prompt, "/ssm_selftest", 13) == 0) {
            Print(L"\r\n[selftest] BPE tokenizer: ");
            {
                int ids[16];
                int n = llmk_oo_infer_tokenize("The future", ids, 16);
                if (n > 0) {
                    Print(L"OK (%d tokens:", n);
                    for (int si = 0; si < n && si < 8; si++) Print(L" %d", ids[si]);
                    Print(L")\r\n");
                    // Round-trip decode
                    Print(L"[selftest] Decode: ");
                    for (int si = 0; si < n; si++) {
                        char dbuf[32]; int dlen;
                        dlen = llmk_oo_infer_decode_token(ids[si], dbuf, sizeof(dbuf));
                        if (dlen > 0) {
                            for (int ci = 0; ci < dlen; ci++) {
                                CHAR16 c16[2]; c16[0] = (CHAR16)(unsigned char)dbuf[ci]; c16[1] = 0;
                                Print(c16);
                            }
                        } else {
                            Print(L"<tok%d>", ids[si]);
                        }
                    }
                    Print(L"\r\n");
                } else {
                    Print(L"FAIL (returned %d — using byte fallback)\r\n", n);
                }
            }
            Print(L"[selftest] SSM model: %s\r\n",
                  g_oosi_v3_valid ? L"loaded" : L"NOT loaded");
            if (g_oosi_v3_valid) {
                Print(L"[selftest] dims: d=%d n=%d Di=%d S=%d V=%d\r\n",
                      g_oosi_v3_weights.d_model, g_oosi_v3_weights.n_layer,
                      g_oosi_v3_weights.d_inner, g_oosi_v3_weights.d_state,
                      g_oosi_v3_weights.vocab_size);
                Print(L"[selftest] precomputed exp(A): %s\r\n",
                      g_oosi_v3_ctx.neg_exp_A
                          ? (g_oosi_v3_weights.neg_exp_A_data
                              ? L"YES (baked in binary)"
                              : L"YES (runtime)")
                          : L"no (on-the-fly)");
                Print(L"[selftest] rng_state=0x%08X\r\n", g_oosi_v3_ctx.rng_state);
            }
            Print(L"\r\n");
            continue;
        }

        // ── SomaMind commands ────────────────────────────────────────────
        if (my_strncmp(prompt, "/soma_status", 12) == 0) {
            LlmkAttachRoutePolicyPreview attach_external_policy;
            LlmkAttachRoutePolicyPreview attach_dual_policy;
            if (!g_soma_initialized) {
                soma_router_init(&g_soma_router);
                soma_dna_init_default(&g_soma_dna);
                g_soma_initialized = 1;
            }
            g_soma_router.soma_model_ready = (g_mind_runtime_state.core_active || g_oosi_v3_valid ||
                                    (g_oosi_weights_valid && llmk_oo_infer_is_ready())) ? 1 : 0;
            g_soma_router.external_model_ready = g_mind_runtime_state.attach_active ? 1 : 0;
            llmk_attach_route_policy_preview(SOMA_ROUTE_EXTERNAL, &attach_external_policy);
            llmk_attach_route_policy_preview(SOMA_ROUTE_DUAL, &attach_dual_policy);
            Print(L"\r\n[SomaMind Router]\r\n");
            Print(L"  model_topology:\r\n");
            Print(L"    internal (OO core backbone): %s\r\n",
                  g_soma_router.soma_model_ready ? L"LOADED" : L"not loaded");
            Print(L"    external (validated attach): %s\r\n",
                  g_soma_router.external_model_ready ? L"LOADED" : L"not loaded");
            Print(L"  routing_stats:\r\n");
            Print(L"    total=%d reflex=%d internal=%d external=%d\r\n",
                  g_soma_router.total_routed, g_soma_router.reflex_count,
                  g_soma_router.internal_count, g_soma_router.external_count);
            Print(L"  confidence_threshold=");
            { int th = (int)(g_soma_router.confidence_threshold * 100.0f);
              Print(L"%d%%\r\n", th); }
            Print(L"  attach_policy_preview:\r\n");
            Print(L"    external active=%d applied=%d temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
                attach_external_policy.active,
                attach_external_policy.applied,
                attach_external_policy.temperature_milli / 1000,
                attach_external_policy.temperature_milli % 1000,
                attach_external_policy.top_p_milli / 1000,
                attach_external_policy.top_p_milli % 1000,
                attach_external_policy.repetition_penalty_milli / 1000,
                attach_external_policy.repetition_penalty_milli % 1000,
                attach_external_policy.max_tokens);
            Print(L"    dual     active=%d applied=%d temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
                attach_dual_policy.active,
                attach_dual_policy.applied,
                attach_dual_policy.temperature_milli / 1000,
                attach_dual_policy.temperature_milli % 1000,
                attach_dual_policy.top_p_milli / 1000,
                attach_dual_policy.top_p_milli % 1000,
                attach_dual_policy.repetition_penalty_milli / 1000,
                attach_dual_policy.repetition_penalty_milli % 1000,
                attach_dual_policy.max_tokens);
            Print(L"  dna_generation=%d  dna_hash=0x%08X\r\n",
                  g_soma_dna.generation, soma_dna_hash(&g_soma_dna));
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_dna", 9) == 0) {
            if (!g_soma_initialized) {
                soma_router_init(&g_soma_router);
                soma_dna_init_default(&g_soma_dna);
                g_soma_initialized = 1;
            }
            Print(L"\r\n[OO Digital DNA]\r\n");
            Print(L"  magic=0x%08X version=%d generation=%d\r\n",
                  g_soma_dna.magic, g_soma_dna.version, g_soma_dna.generation);
            Print(L"  parent_hash=0x%08X\r\n", g_soma_dna.parent_hash);
            { int cb = (int)(g_soma_dna.cognition_bias * 100.0f);
              Print(L"  cognition_bias=%d%% (0=Solar/Logic, 100=Lunar/Creative)\r\n", cb); }
            { int ts = (int)(g_soma_dna.temperature_solar * 100.0f);
              int tl = (int)(g_soma_dna.temperature_lunar * 100.0f);
              Print(L"  temp_solar=%d.%02d  temp_lunar=%d.%02d\r\n",
                    ts/100, ts%100, tl/100, tl%100); }
            { int ps = (int)(g_soma_dna.top_p_solar * 100.0f);
              int pl = (int)(g_soma_dna.top_p_lunar * 100.0f);
              Print(L"  top_p_solar=%d.%02d  top_p_lunar=%d.%02d\r\n",
                    ps/100, ps%100, pl/100, pl%100); }
            { int pr = (int)(g_soma_dna.pressure_sensitivity * 100.0f);
              Print(L"  pressure_sensitivity=%d.%02d\r\n", pr/100, pr%100); }
            { int lr = (int)(g_soma_dna.learning_rate * 1000.0f);
              Print(L"  learning_rate=%d.%03d\r\n", lr/1000, lr%1000); }
            Print(L"  domain_mask=0x%02X\r\n", g_soma_dna.domain_mask);
            Print(L"  interactions=%d escalations=%d\r\n",
                  g_soma_dna.total_interactions, g_soma_dna.escalations);
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_mutate", 12) == 0) {
            if (!g_soma_initialized) {
                soma_router_init(&g_soma_router);
                soma_dna_init_default(&g_soma_dna);
                g_soma_initialized = 1;
            }
            float mag = g_soma_dna.learning_rate;
            int i = 12;
            while (prompt[i] == ' ') i++;
            if (prompt[i] >= '0' && prompt[i] <= '9') {
                unsigned int v = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    v = v * 10u + (unsigned int)(prompt[i] - '0');
                    i++;
                }
                mag = (float)v / 100.0f;
            }
            uint32_t rng = g_oosi_v3_valid ? g_oosi_v3_ctx.rng_state : 0x12345678u;
            uint32_t old_hash = soma_dna_hash(&g_soma_dna);
            soma_dna_mutate(&g_soma_dna, &rng, mag);
            uint32_t new_hash = soma_dna_hash(&g_soma_dna);
            if (g_oosi_v3_valid) g_oosi_v3_ctx.rng_state = rng;
            Print(L"\r\n[SomaMind] DNA mutated: gen %d -> %d\r\n",
                  g_soma_dna.generation - 1, g_soma_dna.generation);
            { int m = (int)(mag * 100.0f);
              Print(L"  magnitude=%d%%\r\n", m); }
            Print(L"  hash 0x%08X -> 0x%08X\r\n\r\n", old_hash, new_hash);
            continue;
        }
        if (my_strncmp(prompt, "/soma_route ", 12) == 0) {
            LlmkAttachRoutePolicyPreview attach_policy;
            if (!g_soma_initialized) {
                soma_router_init(&g_soma_router);
                soma_dna_init_default(&g_soma_dna);
                g_soma_initialized = 1;
            }
            g_soma_router.soma_model_ready = (g_mind_runtime_state.core_active || g_oosi_v3_valid ||
                                              (g_oosi_weights_valid && llmk_oo_infer_is_ready())) ? 1 : 0;
            g_soma_router.external_model_ready = g_mind_runtime_state.attach_active ? 1 : 0;
            const char *text = prompt + 12;
            int tlen = 0;
            while (text[tlen]) tlen++;
            SomaRouteResult rr = soma_route(&g_soma_router, text, tlen);
            const CHAR16 *rname = L"UNKNOWN";
            switch (rr.route) {
                case SOMA_ROUTE_REFLEX:   rname = L"REFLEX";   break;
                case SOMA_ROUTE_INTERNAL: rname = L"INTERNAL"; break;
                case SOMA_ROUTE_EXTERNAL: rname = L"EXTERNAL"; break;
                case SOMA_ROUTE_DUAL:     rname = L"DUAL";     break;
            }
            const CHAR16 *dname = L"UNKNOWN";
            switch (rr.domain) {
                case SOMA_DOMAIN_SYSTEM:   dname = L"SYSTEM";   break;
                case SOMA_DOMAIN_POLICY:   dname = L"POLICY";   break;
                case SOMA_DOMAIN_CHAT:     dname = L"CHAT";     break;
                case SOMA_DOMAIN_CODE:     dname = L"CODE";     break;
                case SOMA_DOMAIN_MATH:     dname = L"MATH";     break;
                case SOMA_DOMAIN_CREATIVE: dname = L"CREATIVE"; break;
                default: break;
            }
            { int cf = (int)(rr.confidence * 100.0f);
              Print(L"\r\n[SomaMind Route]\r\n  route=%s  domain=%s  confidence=%d%%\r\n",
                    rname, dname, cf); }
            Print(L"  external_attach=%s\r\n",
                g_mind_runtime_state.attach_active ? L"validated" :
                g_mind_runtime_state.attach_requested ? L"requested-not-active" : L"none");
            llmk_attach_route_policy_preview(rr.route, &attach_policy);
            if (attach_policy.active) {
                Print(L"  attach_policy active=%d applied=%d temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d\r\n",
                    attach_policy.active,
                    attach_policy.applied,
                    attach_policy.temperature_milli / 1000,
                    attach_policy.temperature_milli % 1000,
                    attach_policy.top_p_milli / 1000,
                    attach_policy.top_p_milli % 1000,
                    attach_policy.repetition_penalty_milli / 1000,
                    attach_policy.repetition_penalty_milli % 1000,
                    attach_policy.max_tokens);
            }
            if (rr.reflex_response) {
                Print(L"  reflex_response: ");
                for (int ri = 0; ri < rr.reflex_response_len; ri++)
                    Print(L"%c", (CHAR16)rr.reflex_response[ri]);
                Print(L"\r\n");
            }
            Print(L"\r\n");
            continue;
        }
        // ── SomaMind Dual Core commands ──────────────────────────────────────
        if (my_strncmp(prompt, "/soma_dual", 10) == 0) {
            const char *arg = prompt + 10;
            while (*arg == ' ') arg++;
            if (*arg == '0') {
                g_soma_dual_enabled = 0;
                Print(L"\r\n[SomaMind] Dual Core DISABLED — standard sampling\r\n\r\n");
            } else if (*arg == '1') {
                if (!g_soma_dual_buf) {
                    Print(L"\r\n[SomaMind] Dual Core not available (no model loaded yet)\r\n\r\n");
                } else {
                    g_soma_dual_enabled = 1;
                    Print(L"\r\n[SomaMind] Dual Core ENABLED ☀Solar+🌙Lunar fusion active\r\n\r\n");
                }
            } else {
                // No arg: show status
                Print(L"\r\n[SomaMind] Dual Core: %s\r\n",
                      g_soma_dual_enabled ? L"ENABLED" : L"DISABLED");
                Print(L"  buf_ready=%s\r\n", g_soma_dual_buf ? L"yes" : L"no (load model first)");
                Print(L"  Usage: /soma_dual 1   enable\r\n");
                Print(L"         /soma_dual 0   disable\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_dual_stats", 16) == 0) {
            if (!g_soma_dual_buf) {
                Print(L"\r\n[SomaMind] Dual Core not initialized (load model first)\r\n\r\n");
            } else {
                SomaDualStats *st = &g_soma_dual.stats;
                Print(L"\r\n[SomaMind Dual Core Stats]\r\n");
                Print(L"  total_tokens  : %d\r\n", st->total_tokens);
                Print(L"  solar_chosen  : %d\r\n", st->solar_chosen);
                Print(L"  lunar_chosen  : %d\r\n", st->lunar_chosen);
                Print(L"  agreements    : %d\r\n", st->agreements);
                Print(L"  disagreements : %d\r\n", st->disagreements);
                { int conf_pct = (int)(st->avg_confidence * 100.0f);
                  Print(L"  avg_confidence: %d%%\r\n", conf_pct); }
                Print(L"\r\n");
            }
            continue;
        }
        // ── SomaMind SMB commands ────────────────────────────────────────────
        if (my_strncmp(prompt, "/soma_smb_stats", 15) == 0) {
            Print(L"\r\n[SomaMind SMB — Synaptic Memory Bus]\r\n");
            Print(L"  slots_used   : %d / %d\r\n", g_soma_smb.count, SOMA_SMB_CAPACITY);
            Print(L"  turn         : %d\r\n", (int)g_soma_smb.turn);
            Print(L"  total_writes : %d\r\n", g_soma_smb.total_writes);
            Print(L"  total_hits   : %d\r\n", g_soma_smb.total_hits);
            Print(L"  total_misses : %d\r\n", g_soma_smb.total_misses);
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_smb_dump", 14) == 0) {
            Print(L"\r\n[SomaMind SMB — Memory Dump]\r\n");
            int shown = 0;
            for (int si = 0; si < g_soma_smb.count; si++) {
                SomaSmbSlot *s = &g_soma_smb.slots[si];
                if (s->relevance < 0.01f) continue;
                int rel_pct  = (int)(s->relevance * 100.0f);
                int conf_pct = (int)(s->confidence * 100.0f);
                Print(L"  [%d] turn=%d dom=%d rel=%d%% conf=%d%% hash=0x%08X gist:",
                      si, (int)s->turn,
                      (int)s->domain, rel_pct, conf_pct,
                      (unsigned)s->input_hash);
                for (int gi = 0; gi < s->gist_len; gi++)
                    Print(L"%d ", (int)s->gist[gi]);
                Print(L"\r\n");
                shown++;
            }
            if (shown == 0) Print(L"  (empty)\r\n");
            Print(L"\r\n");
            continue;
        }
        // ── SomaMind Dream + Meta commands ──────────────────────────────────
        if (my_strncmp(prompt, "/soma_dream", 11) == 0) {
            if (g_soma_smb.count < 2) {
                Print(L"\r\n[SomaMind] Not enough SMB data yet (need >=2 interactions)\r\n\r\n");
            } else {
                SomaDreamSummary ds = soma_dream_run(&g_soma_dream, &g_soma_smb);
                Print(L"\r\n[SomaMind Dream Summary]\r\n");
                Print(L"  total_slots    : %d\r\n", ds.total_slots);
                Print(L"  dream_quality  : %d%%\r\n", ds.dream_quality);
                Print(L"  dominant_domain: %d\r\n", (int)ds.dominant_domain);
                Print(L"  solar_count    : %d\r\n", ds.solar_count);
                Print(L"  lunar_count    : %d\r\n", ds.lunar_count);
                { int cp = (int)(ds.avg_confidence * 100.0f);
                  Print(L"  avg_confidence : %d%%\r\n", cp); }
                { int bd = (int)(ds.recommended_bias_delta * 1000.0f);
                  Print(L"  bias_delta     : %d/1000\r\n", bd); }
                Print(L"  top_gist: ");
                for (int gi = 0; gi < 3 && ds.top_gist_counts[gi] > 0; gi++)
                    Print(L"%d(x%d) ", (int)ds.top_gist_tokens[gi], ds.top_gist_counts[gi]);
                Print(L"\r\n");
                // Apply dream to DNA
                const char *arg = prompt + 11;
                while (*arg == ' ') arg++;
                if (*arg == 'a' || *arg == 'A') {
                    soma_dream_apply(&g_soma_dream, &g_soma_dna, &ds, 1);
                    Print(L"  [APPLIED to DNA — gen=%d]\r\n", (int)g_soma_dna.generation);
                } else {
                    Print(L"  (use /soma_dream apply to apply to DNA)\r\n");
                }
                Print(L"\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_meta", 10) == 0) {
            Print(L"\r\n[SomaMind Meta-Evolution]\r\n");
            Print(L"  total_evals    : %d\r\n", g_soma_meta.total_evaluations);
            Print(L"  mutations      : %d\r\n", g_soma_meta.mutations_applied);
            Print(L"  stagnation     : %d\r\n", g_soma_meta.stagnation_count);
            { int bp = (int)(g_soma_meta.best_score * 100.0f);
              Print(L"  best_score     : %d%%\r\n", bp); }
            Print(L"  history (last %d):\r\n", g_soma_meta.history_count);
            int n = g_soma_meta.history_count;
            int start = (g_soma_meta.history_head - n + SOMA_META_HISTORY) & (SOMA_META_HISTORY - 1);
            for (int hi = 0; hi < n; hi++) {
                int idx = (start + hi) & (SOMA_META_HISTORY - 1);
                SomaMetaFitness *mf = &g_soma_meta.history[idx];
                int sp = (int)(mf->score * 100.0f);
                int cp = (int)(mf->confidence_contrib * 100.0f);
                int rp = (int)(mf->reflex_contrib * 100.0f);
                Print(L"    gen=%d score=%d%% conf=%d%% ref=%d%%\r\n",
                      mf->generation, sp, cp, rp);
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_evolve", 12) == 0) {
            int mutated = soma_meta_cycle(&g_soma_meta, &g_soma_dna,
                                          &g_soma_router, &g_soma_dual,
                                          &g_soma_smb, 0 /*force*/);
            if (mutated)
                Print(L"\r\n[SomaMind] DNA mutated (gen=%d hash=0x%08X)\r\n\r\n",
                      (int)g_soma_dna.generation, soma_dna_hash(&g_soma_dna));
            else
                Print(L"\r\n[SomaMind] DNA scored (no mutation needed, score=%d%%)\r\n\r\n",
                      (int)(g_soma_meta.best_score * 100.0f));
            continue;
        }
        // ── Phase V: Multi-Reality Sampling ─────────────────────────────────
        if (my_strncmp(prompt, "/multireal", 10) == 0) {
            const char *arg = prompt + 10;
            while (*arg == ' ') arg++;
            if (*arg == 0 || my_strncmp(arg, "status", 6) == 0) {
                int tot = g_multireal_stats.total_tokens;
                int sp = tot > 0 ? (g_multireal_stats.solar_wins  * 100 / tot) : 0;
                int lp = tot > 0 ? (g_multireal_stats.lunar_wins  * 100 / tot) : 0;
                int ap = tot > 0 ? (g_multireal_stats.argmax_wins * 100 / tot) : 0;
                Print(L"\r\n[MultiReal] enabled=%d  tokens=%d\r\n"
                      L"  solar=%d(%d%%)  lunar=%d(%d%%)  argmax=%d(%d%%)\r\n\r\n",
                      g_multireal_enabled, tot,
                      g_multireal_stats.solar_wins,  sp,
                      g_multireal_stats.lunar_wins,  lp,
                      g_multireal_stats.argmax_wins, ap);
            } else if (my_strncmp(arg, "on", 2) == 0) {
                g_multireal_enabled = 1;
                Print(L"\r\n[MultiReal] enabled (3-way solar/lunar/argmax per token)\r\n\r\n");
            } else if (my_strncmp(arg, "off", 3) == 0) {
                g_multireal_enabled = 0;
                Print(L"\r\n[MultiReal] disabled\r\n\r\n");
            }
            continue;
        }
        // ── Phase W: Speculative Decoding commands ───────────────────────
        if (my_strncmp(prompt, "/specdecode", 11) == 0) {
            const char *arg = prompt + 11;
            while (*arg == ' ') arg++;
            if (*arg == 0 || my_strncmp(arg, "status", 6) == 0) {
                int tot = g_soma_spec.stats.total_drafted;
                int acc = g_soma_spec.stats.total_accepted;
                int sp  = tot > 0 ? (acc * 100 / tot) : 0;
                Print(L"\r\n[SpecDecode] enabled=%d  buf=%s  vocab=%d\r\n"
                      L"  cycles=%d  drafted=%d  accepted=%d(%d%%)  rejected=%d\r\n"
                      L"  full_accepts=%d  avg_speedup=%d%%\r\n\r\n",
                      g_soma_spec.enabled,
                      g_soma_spec_buf ? L"ready" : L"no-model",
                      g_soma_spec.vocab_size,
                      g_soma_spec.stats.total_cycles, tot, acc, sp,
                      g_soma_spec.stats.total_rejected,
                      g_soma_spec.stats.full_accepts,
                      (int)(g_soma_spec.stats.avg_speedup * 100.0f));
            } else if (my_strncmp(arg, "on", 2) == 0) {
                if (!g_soma_spec_buf) {
                    Print(L"\r\n[SpecDecode] ERROR: load model first (/ssm_load)\r\n\r\n");
                } else {
                    g_soma_spec.enabled = 1;
                    Print(L"\r\n[SpecDecode] enabled (draft+verify pipeline)\r\n\r\n");
                }
            } else if (my_strncmp(arg, "off", 3) == 0) {
                g_soma_spec.enabled = 0;
                Print(L"\r\n[SpecDecode] disabled\r\n\r\n");
            } else if (my_strncmp(arg, "threshold", 9) == 0) {
                // /specdecode threshold 0.7
                const char *tv = arg + 9; while (*tv == ' ') tv++;
                int t_int = 0; int t_frac = 0; int in_frac = 0; int frac_div = 1;
                while (*tv) {
                    if (*tv >= '0' && *tv <= '9') {
                        if (!in_frac) t_int = t_int * 10 + (*tv - '0');
                        else { t_frac = t_frac * 10 + (*tv - '0'); frac_div *= 10; }
                    } else if (*tv == '.' || *tv == ',') in_frac = 1;
                    tv++;
                }
                float thr = (float)t_int + (frac_div > 1 ? (float)t_frac / (float)frac_div : 0.0f);
                if (thr < 0.0f) thr = 0.0f; if (thr > 1.0f) thr = 1.0f;
                g_soma_spec.accept_threshold = thr;
                Print(L"\r\n[SpecDecode] threshold=%d/100\r\n\r\n", (int)(thr * 100.0f));
            }
            continue;
        }
        // ── Phase Y: Swarm Net commands ──────────────────────────────────
        if (my_strncmp(prompt, "/swarm_net", 10) == 0) {
            const char *arg = prompt + 10;
            while (*arg == ' ') arg++;
            if (*arg == 0 || my_strncmp(arg, "status", 6) == 0) {
                soma_swarm_net_print_status(&g_soma_swarm_net);
            } else if (my_strncmp(arg, "on", 2) == 0) {
                if (!g_oosi_v3_valid) {
                    Print(L"\r\n[SwarmNet] ERROR: load model first (/ssm_load)\r\n\r\n");
                } else {
                    g_soma_swarm_net.enabled = 1;
                    Print(L"\r\n[SwarmNet] enabled (peer_id=%d — publish+consensus active)\r\n\r\n",
                          g_soma_swarm_net.my_peer_id);
                }
            } else if (my_strncmp(arg, "off", 3) == 0) {
                g_soma_swarm_net.enabled = 0;
                Print(L"\r\n[SwarmNet] disabled\r\n\r\n");
            } else if (my_strncmp(arg, "peer", 4) == 0) {
                const char *pv = arg + 4; while (*pv == ' ') pv++;
                int pid = 0;
                while (*pv >= '0' && *pv <= '9') { pid = pid * 10 + (*pv++ - '0'); }
                if (pid >= 0 && pid < SWARM_NET_PEERS) {
                    g_soma_swarm_net.my_peer_id = pid;
                    Print(L"\r\n[SwarmNet] peer_id set to %d\r\n\r\n", pid);
                } else {
                    Print(L"\r\n[SwarmNet] ERROR: peer_id must be 0..%d\r\n\r\n", SWARM_NET_PEERS - 1);
                }
            } else if (my_strncmp(arg, "addr", 4) == 0) {
                // /swarm_net addr <hex_phys_addr>  — set shared memory base address
                const char *av = arg + 4; while (*av == ' ') av++;
                unsigned long long addr = 0;
                if (av[0] == '0' && (av[1] == 'x' || av[1] == 'X')) av += 2;
                while ((*av >= '0' && *av <= '9') || (*av >= 'a' && *av <= 'f') || (*av >= 'A' && *av <= 'F')) {
                    int nib = (*av >= 'a') ? (*av - 'a' + 10) : (*av >= 'A') ? (*av - 'A' + 10) : (*av - '0');
                    addr = (addr << 4) | (unsigned long long)nib;
                    av++;
                }
                g_soma_swarm_net.base_addr = addr;
                g_soma_swarm_net.use_fixed_addr = (addr != 0) ? 1 : 0;
                Print(L"\r\n[SwarmNet] base_addr=0x%llX use_fixed=%d\r\n\r\n",
                      addr, g_soma_swarm_net.use_fixed_addr);
            }
            continue;
        }
        // ── Swarm commands ───────────────────────────────────────────────
        if (my_strncmp(prompt, "/soma_swarm_mode", 16) == 0) {
            const char *arg = prompt + 16;
            while (*arg == ' ') arg++;
            if (my_strncmp(arg, "majority", 8) == 0) {
                g_soma_swarm.mode = SOMA_SWARM_MAJORITY;
                Print(L"\r\n[Swarm] mode=MAJORITY\r\n\r\n");
            } else if (my_strncmp(arg, "weighted", 8) == 0) {
                g_soma_swarm.mode = SOMA_SWARM_WEIGHTED;
                Print(L"\r\n[Swarm] mode=WEIGHTED\r\n\r\n");
            } else if (my_strncmp(arg, "confident", 9) == 0) {
                g_soma_swarm.mode = SOMA_SWARM_CONFIDENT;
                Print(L"\r\n[Swarm] mode=CONFIDENT\r\n\r\n");
            } else {
                Print(L"\r\n[Swarm] current mode=%d  (majority=0 weighted=1 confident=2)\r\n\r\n",
                      (int)g_soma_swarm.mode);
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_swarm_stats", 17) == 0) {
            Print(L"\r\n[Swarm] agents=%d mode=%d enabled=%d ready=%d\r\n",
                  SOMA_SWARM_AGENTS,
                  (int)g_soma_swarm.mode,
                  g_soma_swarm.enabled,
                  g_soma_swarm.ready);
            for (int ai = 0; ai < SOMA_SWARM_AGENTS; ai++) {
                const SomaSwarmAgent *ag = &g_soma_swarm.agents[ai];
                Print(L"  [%d] fitness=%d%% votes=%d temp=%d/100 bias=%d/100\r\n",
                      ai,
                      (int)(ag->fitness * 100.0f),
                      (int)ag->votes_cast,
                      (int)(ag->dna.temperature_solar * 100.0f),
                      (int)(ag->dna.cognition_bias * 100.0f));
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_swarm", 11) == 0) {
            const char *arg = prompt + 11;
            while (*arg == ' ') arg++;
            if (*arg == '0') {
                g_soma_swarm.enabled = 0;
                Print(L"\r\n[Swarm] disabled\r\n\r\n");
            } else if (*arg == '1') {
                if (!g_soma_swarm.ready) {
                    Print(L"\r\n[Swarm] not ready — load model first (/ssm_load)\r\n\r\n");
                } else {
                    g_soma_swarm.enabled = 1;
                    Print(L"\r\n[Swarm] enabled (agents=%d mode=%d)\r\n\r\n",
                          SOMA_SWARM_AGENTS, (int)g_soma_swarm.mode);
                }
            } else {
                Print(L"\r\n[Swarm] status: %s  /soma_swarm [0|1]\r\n\r\n",
                      g_soma_swarm.enabled ? L"ON" : L"OFF");
            }
            continue;
        }
        // ── Reflex commands ──────────────────────────────────────────────
        if (my_strncmp(prompt, "/soma_reflex_test", 17) == 0) {
            const char *arg = prompt + 17;
            while (*arg == ' ') arg++;
            if (*arg) {
                SomaReflexResult rf = soma_reflex_scan(&g_soma_reflex, arg);
                if (rf.triggered) {
                    Print(L"\r\n[Reflex] injection: ");
                    for (int ri = 0; rf.injection[ri]; ri++)
                        Print(L"%c", (CHAR16)(unsigned char)rf.injection[ri]);
                    Print(L"vars_resolved=%d vars_failed=%d\r\n\r\n",
                          rf.vars_resolved, rf.vars_failed);
                } else {
                    Print(L"\r\n[Reflex] no math pattern detected\r\n\r\n");
                }
            } else {
                Print(L"\r\n[Reflex] usage: /soma_reflex_test <expr>\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_reflex", 12) == 0) {
            const char *arg = prompt + 12;
            while (*arg == ' ') arg++;
            if (*arg == '0') {
                g_soma_reflex.enabled = 0;
                Print(L"\r\n[Reflex] disabled\r\n\r\n");
            } else if (*arg == '1') {
                g_soma_reflex.enabled = 1;
                Print(L"\r\n[Reflex] enabled\r\n\r\n");
            } else {
                Print(L"\r\n[Reflex] status: %s  scans=%d triggers=%d resolved=%d\r\n\r\n",
                      g_soma_reflex.enabled ? L"ON" : L"OFF",
                      g_soma_reflex.total_scans,
                      g_soma_reflex.total_triggers,
                      g_soma_reflex.total_resolved);
            }
            continue;
        }
        // ── Logic Reflex commands ─────────────────────────────────────────
        if (my_strncmp(prompt, "/soma_logic_test", 16) == 0) {
            const char *arg = prompt + 16;
            while (*arg == ' ') arg++;
            if (*arg) {
                SomaLogicResult lg = soma_logic_scan(&g_soma_logic, arg);
                if (lg.triggered) {
                    Print(L"\r\n[Logic] injection: ");
                    for (int li = 0; lg.injection[li]; li++)
                        Print(L"%c", (CHAR16)(unsigned char)lg.injection[li]);
                    Print(L"  derived=%d barbara=%d contradict=%d\r\n\r\n",
                          lg.derived_count, lg.barbara_count, lg.contradiction);
                } else {
                    Print(L"\r\n[Logic] no logical pattern detected\r\n\r\n");
                }
            } else {
                Print(L"\r\n[Logic] usage: /soma_logic_test <sentence>\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_logic", 11) == 0) {
            const char *arg = prompt + 11;
            while (*arg == ' ') arg++;
            if (*arg == '0') {
                g_soma_logic.enabled = 0;
                Print(L"\r\n[Logic] disabled\r\n\r\n");
            } else if (*arg == '1') {
                g_soma_logic.enabled = 1;
                Print(L"\r\n[Logic] enabled\r\n\r\n");
            } else {
                Print(L"\r\n[Logic] status: %s  scans=%d triggers=%d derived=%d contradictions=%d\r\n\r\n",
                      g_soma_logic.enabled ? L"ON" : L"OFF",
                      g_soma_logic.total_scans,
                      g_soma_logic.total_triggers,
                      g_soma_logic.total_derived,
                      g_soma_logic.total_contradictions);
            }
            continue;
        }
        // ── Memory Reflex commands (Phase H) ─────────────────────────────
        if (my_strncmp(prompt, "/soma_memory_test", 17) == 0) {
            const char *arg = prompt + 17;
            while (*arg == ' ') arg++;
            if (*arg) {
                SomaMemResult mr = soma_memory_scan(&g_soma_memory, arg);
                if (mr.triggered) {
                    Print(L"\r\n[Memory] Match found! turn=%d sim=%d%%\r\n",
                          mr.match_turn, mr.match_similarity);
                    Print(L"[Memory] Past prompt : ");
                    for (int mi = 0; mr.match_prompt[mi]; mi++)
                        Print(L"%c", (CHAR16)(unsigned char)mr.match_prompt[mi]);
                    Print(L"\r\n[Memory] Past response: ");
                    for (int mi = 0; mr.match_response[mi]; mi++)
                        Print(L"%c", (CHAR16)(unsigned char)mr.match_response[mi]);
                    Print(L"\r\n[Memory] Injection: ");
                    for (int mi = 0; mr.injection[mi]; mi++)
                        Print(L"%c", (CHAR16)(unsigned char)mr.injection[mi]);
                    Print(L"\r\n\r\n");
                } else {
                    Print(L"\r\n[Memory] No match in %d entries (turns=%d)\r\n\r\n",
                          g_soma_memory.count, g_soma_memory.total_turns);
                }
            } else {
                Print(L"\r\n[Memory] usage: /soma_memory_test <prompt>\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_memory_stats", 18) == 0) {
            Print(L"\r\n[Memory] Phase H: Session Memory & Journal Reflex\r\n");
            Print(L"  status    : %s\r\n", g_soma_memory.enabled ? L"ON" : L"OFF");
            Print(L"  boot_count: %d\r\n",  g_soma_memory.boot_count);
            Print(L"  turns     : %d\r\n",  g_soma_memory.total_turns);
            Print(L"  entries   : %d / %d\r\n", g_soma_memory.count, SOMA_MEM_MAX_ENTRIES);
            Print(L"  triggers  : %d\r\n",  g_soma_memory.total_triggers);
            if (g_soma_memory.model_name[0]) {
                Print(L"  model     : ");
                for (int mi = 0; g_soma_memory.model_name[mi]; mi++)
                    Print(L"%c", (CHAR16)(unsigned char)g_soma_memory.model_name[mi]);
                Print(L"\r\n");
            }
            // Show last 4 entries
            if (g_soma_memory.count > 0) {
                Print(L"  recent entries:\r\n");
                int shown = 0;
                for (int ei = SOMA_MEM_MAX_ENTRIES - 1; ei >= 0 && shown < 4; ei--) {
                    int idx = (g_soma_memory.head - 1 - shown + SOMA_MEM_MAX_ENTRIES) % SOMA_MEM_MAX_ENTRIES;
                    SomaMemEntry *e = &g_soma_memory.entries[idx];
                    if (!e->valid) continue;
                    Print(L"    [%d] turn=%d  prompt=", shown, e->turn);
                    for (int ci = 0; e->prompt[ci] && ci < 40; ci++)
                        Print(L"%c", (CHAR16)(unsigned char)e->prompt[ci]);
                    Print(L"\r\n");
                    shown++;
                }
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_memory", 12) == 0) {
            const char *arg = prompt + 12;
            while (*arg == ' ') arg++;
            if (*arg == '0') {
                g_soma_memory.enabled = 0;
                Print(L"\r\n[Memory] disabled\r\n\r\n");
            } else if (*arg == '1') {
                g_soma_memory.enabled = 1;
                Print(L"\r\n[Memory] enabled\r\n\r\n");
            } else {
                Print(L"\r\n[Memory] status: %s  turns=%d entries=%d/%d triggers=%d\r\n\r\n",
                      g_soma_memory.enabled ? L"ON" : L"OFF",
                      g_soma_memory.total_turns,
                      g_soma_memory.count, SOMA_MEM_MAX_ENTRIES,
                      g_soma_memory.total_triggers);
            }
            continue;
        }
        // ── Phase I: Journal commands ────────────────────────────────────
        if (my_strncmp(prompt, "/soma_journal_save", 18) == 0) {
            if (!g_root) { Print(L"\r\n[Journal] ERROR: no EFI root\r\n\r\n"); continue; }
            int n = soma_journal_save(&g_soma_memory, g_root, g_soma_journal_total_turns);
            if (n >= 0)
                Print(L"\r\n[Journal] Saved %d entries to soma_journal.bin\r\n\r\n", n);
            else
                Print(L"\r\n[Journal] ERROR: save failed\r\n\r\n");
            g_soma_journal_turns_since_save = 0;
            continue;
        }
        if (my_strncmp(prompt, "/soma_journal_load", 18) == 0) {
            if (!g_root) { Print(L"\r\n[Journal] ERROR: no EFI root\r\n\r\n"); continue; }
            unsigned int prev = 0;
            int n = soma_journal_load(&g_soma_memory, g_root, &prev);
            if (n >= 0) {
                g_soma_journal_total_turns = prev;
                Print(L"\r\n[Journal] Loaded %d entries (total_turns=%d, sessions=%d)\r\n\r\n",
                      n, (int)prev, g_soma_memory.boot_count);
            } else if (n == -1) {
                Print(L"\r\n[Journal] No file found\r\n\r\n");
            } else {
                Print(L"\r\n[Journal] ERROR: corrupt or unreadable\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/soma_journal_clear", 19) == 0) {
            if (!g_root) { Print(L"\r\n[Journal] ERROR: no EFI root\r\n\r\n"); continue; }
            int r = soma_journal_clear(g_root);
            g_soma_journal_total_turns = 0;
            g_soma_journal_turns_since_save = 0;
            Print(r == 0 ? L"\r\n[Journal] Cleared\r\n\r\n"
                         : L"\r\n[Journal] ERROR: clear failed\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_journal_stats", 19) == 0) {
            Print(L"\r\n[Journal] Runtime: total_turns=%d  turns_since_save=%d  autosave_every=%d\r\n",
                  (int)g_soma_journal_total_turns,
                  g_soma_journal_turns_since_save,
                  SOMA_JOURNAL_AUTOSAVE_EVERY);
            if (g_root) {
                SomaJournalStats js;
                int r = soma_journal_read_stats(g_root, &js);
                if (r == 0)
                    Print(L"[Journal] Disk:    entries=%d  total_turns=%d  sessions=%d\r\n",
                          js.loaded, js.total_turns, js.boot_count);
                else
                    Print(L"[Journal] Disk:    no file (or unreadable)\r\n");
            }
            Print(L"\r\n");
            continue;
        }
        // ── Phase J: Cortex commands ─────────────────────────────────────
        if (my_strncmp(prompt, "/cortex_load", 12) == 0) {
            const char *arg = prompt + 12;
            while (*arg == ' ') arg++;
            if (!arg[0]) {
                Print(L"\r\nUsage: /cortex_load <small_ooss_model.bin>\r\n");
                Print(L"  Load a compact oo-model OOSS (15M-130M params) as cortex brain.\r\n\r\n");
                continue;
            }
            if (!g_root) { Print(L"\r\n[Cortex] ERROR: no EFI root\r\n\r\n"); continue; }
            // Open file on EFI partition
            CHAR16 cpath16[192];
            ascii_to_char16(cpath16, arg, (int)(sizeof(cpath16)/sizeof(cpath16[0])));
            EFI_FILE_HANDLE cfh = NULL;
            EFI_STATUS cst = uefi_call_wrapper(g_root->Open, 5, g_root, &cfh,
                                               cpath16, EFI_FILE_MODE_READ, 0ULL);
            if (EFI_ERROR(cst)) {
                Print(L"\r\n[Cortex] ERROR: cannot open %s (%r)\r\n\r\n", cpath16, cst);
                continue;
            }
            // Get size
            EFI_FILE_INFO *cfi = NULL; UINTN cfisz = SIZE_OF_EFI_FILE_INFO + 256;
            uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, cfisz, (void **)&cfi);
            if (!cfi) { uefi_call_wrapper(cfh->Close, 1, cfh); Print(L"\r\n[Cortex] ERROR: alloc\r\n\r\n"); continue; }
            uefi_call_wrapper(cfh->GetInfo, 4, cfh, &gEfiFileInfoGuid, &cfisz, cfi);
            UINT64 csz = cfi->FileSize;
            uefi_call_wrapper(BS->FreePool, 1, cfi);
            Print(L"\r\n[Cortex] Loading %s (%d MB)...\r\n", cpath16, (int)(csz/(1024*1024)));
            // Allocate from ZONE_C arena (dedicated cortex weights slot)
            void *cbuf = llmk_arena_alloc(&g_zones, LLMK_ARENA_ZONE_C, csz, 64);
            if (!cbuf) {
                uefi_call_wrapper(cfh->Close, 1, cfh);
                Print(L"[Cortex] ERROR: ZONE_C arena full (%d MB needed)\r\n\r\n", (int)(csz/(1024*1024)));
                continue;
            }
            // Read file
            UINT8 *cdst = (UINT8 *)cbuf; UINT64 crem = csz; int cok = 1;
            while (crem > 0) {
                UINTN chunk = (crem > 4*1024*1024) ? 4*1024*1024 : (UINTN)crem;
                cst = uefi_call_wrapper(cfh->Read, 3, cfh, &chunk, cdst);
                if (EFI_ERROR(cst) || chunk == 0) { cok = 0; break; }
                cdst += chunk; crem -= chunk;
            }
            uefi_call_wrapper(cfh->Close, 1, cfh);
            if (!cok) { Print(L"[Cortex] ERROR: read failed\r\n\r\n"); continue; }
            // Allocate runtime buffers from SCRATCH arena
            // Peek header to get dimensions
            OosiV3Header *ch = (OosiV3Header *)cbuf;
            if (ch->magic != OOSI_V3_MAGIC) {
                Print(L"[Cortex] ERROR: not an OOSS v3 file (magic=0x%X)\r\n\r\n", ch->magic);
                continue;
            }
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
            if (!g_soma_cortex.scratch || !g_soma_cortex.logits || !g_soma_cortex.h_state ||
                !g_soma_cortex.conv_buf || !g_soma_cortex.conv_pos) {
                Print(L"[Cortex] ERROR: SCRATCH arena exhausted\r\n\r\n");
                continue;
            }
            int cr = soma_cortex_load(&g_soma_cortex, cbuf, csz, arg);
            if (cr == 0) {
                Print(L"[Cortex] OK: loaded %s (d=%d n=%d v=%d)\r\n",
                      cpath16, cD, cN, cV);
                Print(L"[Cortex] domain/safety routing active. Use /cortex [0|1] to toggle.\r\n\r\n");
            } else {
                Print(L"[Cortex] ERROR: soma_cortex_load failed (code %d)\r\n\r\n", cr);
            }
            (void)cDt; (void)cHd;
            continue;
        }
        if (my_strncmp(prompt, "/cortex_infer", 13) == 0) {
            const char *arg = prompt + 13;
            while (*arg == ' ') arg++;
            if (!arg[0]) { Print(L"\r\nUsage: /cortex_infer <text>\r\n\r\n"); continue; }
            if (!g_soma_cortex.loaded) {
                Print(L"\r\n[Cortex] Not loaded. Use /cortex_load <file>\r\n\r\n"); continue;
            }
            SomaCortexResult cr = soma_cortex_run(&g_soma_cortex, arg);
            if (cr.valid) {
                Print(L"\r\n[Cortex] domain=%d conf=%d%% safety=%d%s prefix=\"",
                      cr.domain, cr.domain_conf, cr.safety_score,
                      cr.safety_flagged ? L" [FLAGGED]" : L"");
                for (int pi = 0; pi < cr.prefix_len; pi++)
                    Print(L"%c", (CHAR16)(unsigned char)cr.prefix[pi]);
                Print(L"\"\r\n[Cortex] tokens=%d ids:", cr.n_tokens);
                for (int ti = 0; ti < cr.n_tokens && ti < 8; ti++)
                    Print(L" %d", cr.token_ids[ti]);
                Print(L"\r\n\r\n");
            } else {
                Print(L"\r\n[Cortex] inference failed\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/cortex_stats", 13) == 0) {
            Print(L"\r\n[Cortex] loaded=%s enabled=%s model=%s\r\n",
                  g_soma_cortex.loaded ? L"yes" : L"no",
                  g_soma_cortex.enabled ? L"yes" : L"no",
                  g_soma_cortex.loaded ? L"ok" : L"none");
            if (g_soma_cortex.loaded) {
                Print(L"  calls=%d flagged=%d\r\n",
                      g_soma_cortex.total_calls, g_soma_cortex.total_flagged);
                Print(L"  d_model=%d n_layer=%d vocab=%d\r\n",
                      g_soma_cortex.weights.d_model,
                      g_soma_cortex.weights.n_layer,
                      g_soma_cortex.weights.vocab_size);
                // Print model name
                Print(L"  model: ");
                for (int mi = 0; g_soma_cortex.model_name[mi]; mi++)
                    Print(L"%c", (CHAR16)(unsigned char)g_soma_cortex.model_name[mi]);
                Print(L"\r\n");
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/cortex", 7) == 0) {
            const char *arg = prompt + 7;
            while (*arg == ' ') arg++;
            if (*arg == '0') {
                g_soma_cortex.enabled = 0;
                Print(L"\r\n[Cortex] disabled\r\n\r\n");
            } else if (*arg == '1') {
                if (!g_soma_cortex.loaded) {
                    Print(L"\r\n[Cortex] ERROR: not loaded yet. Use /cortex_load first.\r\n\r\n");
                } else {
                    g_soma_cortex.enabled = 1;
                    Print(L"\r\n[Cortex] enabled\r\n\r\n");
                }
            } else {
                Print(L"\r\n[Cortex] %s  calls=%d flagged=%d\r\n\r\n",
                      g_soma_cortex.loaded ? (g_soma_cortex.enabled ? L"ACTIVE" : L"LOADED/DISABLED") : L"NOT LOADED",
                      g_soma_cortex.total_calls, g_soma_cortex.total_flagged);
            }
            continue;
        }
        // ─── Phase K: Export commands ─────────────────────────────────────
        if (my_strncmp(prompt, "/soma_export_stats", 18) == 0) {
            // Get EFI root
            EFI_FILE_PROTOCOL *xroot = NULL;
            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
            EFI_STATUS xst = LibLocateProtocol(&FileSystemProtocol, (void **)&fs);
            if (!EFI_ERROR(xst) && fs)
                uefi_call_wrapper(fs->OpenVolume, 2, fs, &xroot);
            int cnt = soma_export_count(xroot);
            if (xroot) uefi_call_wrapper(xroot->Close, 1, xroot);
            if (cnt >= 0)
                Print(L"\r\n[Export] soma_train.jsonl: %d records\r\n\r\n", cnt);
            else
                Print(L"\r\n[Export] soma_train.jsonl: not found\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/soma_export", 12) == 0) {
            // Check append flag
            const char *xarg = prompt + 12;
            while (*xarg == ' ') xarg++;
            int do_append = 1;
            if (xarg[0] == '0') do_append = 0;  // /soma_export 0 = overwrite

            EFI_FILE_PROTOCOL *xroot = NULL;
            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
            EFI_STATUS xst = LibLocateProtocol(&FileSystemProtocol, (void **)&fs);
            if (!EFI_ERROR(xst) && fs)
                uefi_call_wrapper(fs->OpenVolume, 2, fs, &xroot);

            // Use last cortex result for domain/safety if available
            int xdom = g_soma_cortex.loaded ? (int)g_soma_cortex.total_calls : 0;
            (void)xdom;
            SomaExportResult xr = soma_export_write(&g_soma_memory, xroot,
                                                    do_append, 0, 100);
            if (xroot) uefi_call_wrapper(xroot->Close, 1, xroot);

            if (xr.error)
                Print(L"\r\n[Export] ERROR writing soma_train.jsonl\r\n\r\n");
            else
                Print(L"\r\n[Export] OK: %d records written (%s), %d skipped\r\n\r\n",
                      xr.lines_written,
                      xr.appended ? L"appended" : L"created",
                      xr.lines_skipped);
            continue;
        }
        // ─── Phase M: Warden commands ─────────────────────────────────────
        if (my_strncmp(prompt, "/warden_reset", 13) == 0) {
            soma_warden_reset(&g_soma_warden, &g_soma_router);
            Print(L"\r\n[Warden] Pressure reset to NONE. Router threshold=0.85\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/warden_status", 14) == 0) {
            char wbuf[128];
            soma_warden_status_str(&g_soma_warden, wbuf, 128);
            Print(L"\r\n[Warden] ");
            for (int wi = 0; wbuf[wi]; wi++)
                Print(L"%c", (CHAR16)(unsigned char)wbuf[wi]);
            Print(L"\r\n");
            Print(L"  updates=%d esc=%d relief=%d safety_floor=%d\r\n",
                  g_soma_warden.total_updates,
                  g_soma_warden.escalation_count,
                  g_soma_warden.relief_count,
                  g_soma_warden.cortex_safety_floor);
            Print(L"  sentinel_tripped=%s last_err=%d dt_cycles=%llu\r\n\r\n",
                  g_soma_warden.last_sentinel_tripped ? L"YES" : L"no",
                  g_soma_warden.last_sentinel_error,
                  (unsigned long long)g_soma_warden.last_dt_cycles);
            continue;
        }
        // ─── Phase P: Immunion mode commands ─────────────────────────────────
        if (my_strncmp(prompt, "/immunion_mode", 14) == 0) {
            const char *arg = prompt + 14;
            while (*arg == ' ') arg++;
            ImmunionMode newmode = IMMUNION_MODE_RECORD;
            if (arg[0] == 'o' && arg[1] == 'f' && arg[2] == 'f')
                newmode = IMMUNION_MODE_OFF;
            else if (arg[0] == 'a' && arg[1] == 'c' && arg[2] == 't')
                newmode = IMMUNION_MODE_ACT;
            immunion_set_mode(&g_immunion, newmode);
            Print(L"\r\n[Immunion] Mode set to: %s\r\n\r\n",
                  newmode == IMMUNION_MODE_OFF ? L"off" :
                  newmode == IMMUNION_MODE_ACT ? L"act" : L"record");
            continue;
        }
        if (my_strncmp(prompt, "/immunion_status", 16) == 0) {
            Print(L"\r\n[Immunion] mode=%s  patterns=%d  reactions=%d  warden_esc=%d  session_imm=%d\r\n\r\n",
                  g_immunion.mode == IMMUNION_MODE_OFF    ? L"off" :
                  g_immunion.mode == IMMUNION_MODE_ACT    ? L"act" : L"record",
                  (int)g_immunion.patterns_recorded,
                  (int)g_immunion.reactions_triggered,
                  g_soma_warden.immunion_escalations,
                  g_soma_session.immunion_reactions);
            continue;
        }
        // ─── Phase R: Symbion + Memory Semantic Tag commands ─────────────────
        if (my_strncmp(prompt, "/sym_mode", 9) == 0) {
            const char *arg = prompt + 9;
            while (*arg == ' ') arg++;
            SymbionMode smode = SYMBION_MODE_WATCH;
            if (arg[0] == 'o' && arg[1] == 'f' && arg[2] == 'f')
                smode = SYMBION_MODE_OFF;
            else if (arg[0] == 'a' && arg[1] == 'd')
                smode = SYMBION_MODE_ADAPT;
            symbion_set_mode(&g_symbion, smode);
            Print(L"\r\n[Symbion] Mode set to: %s\r\n\r\n",
                  smode == SYMBION_MODE_OFF ? L"off" :
                  smode == SYMBION_MODE_ADAPT ? L"adapt" : L"watch");
            continue;
        }
        if (my_strncmp(prompt, "/sym_status", 11) == 0) {
            Print(L"\r\n[Symbion] mode=%s  samples=%d  adaptations=%d\r\n",
                  g_symbion.mode == SYMBION_MODE_OFF  ? L"off" :
                  g_symbion.mode == SYMBION_MODE_ADAPT ? L"adapt" : L"watch",
                  (int)g_symbion.samples_taken,
                  (int)g_symbion.adaptations_applied);
            // Memory domain distribution
            static const char *dnames[7] = {
                "GENERAL","MEMORY","SYSTEM","REASONING","PLANNING","MATH","LANGUAGE"
            };
            Print(L"  domain distribution in memory:\r\n");
            for (int di = 0; di < 7; di++) {
                int cnt = soma_memory_count_domain(&g_soma_memory, (unsigned char)di);
                if (cnt > 0) {
                    Print(L"    [%d] %-9s : %d entries\r\n",
                          di, (const CHAR16 *)L"", cnt);
                    // Print domain name as ASCII
                    Print(L"    [%d] ", di);
                    for (int ci = 0; dnames[di][ci]; ci++)
                        Print(L"%c", (CHAR16)(unsigned char)dnames[di][ci]);
                    Print(L" : %d entries\r\n", cnt);
                }
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/sym_search", 11) == 0) {
            const char *arg = prompt + 11;
            while (*arg == ' ') arg++;
            // Parse domain number or name
            unsigned char tgt_domain = 255;
            if (*arg >= '0' && *arg <= '6') tgt_domain = (unsigned char)(*arg - '0');
            else if (arg[0] == 'm' && arg[1] == 'a') tgt_domain = 5; // math
            else if (arg[0] == 's' && arg[1] == 'y') tgt_domain = 2; // system
            else if (arg[0] == 'c') tgt_domain = 6;                   // chat/creative
            Print(L"\r\n[Sym-Search] domain=%d\r\n", (int)tgt_domain);
            int shown = 0;
            for (int si = 0; si < SOMA_MEM_MAX_ENTRIES; si++) {
                SomaMemEntry *me = &g_soma_memory.entries[si];
                if (!me->valid) continue;
                if (tgt_domain != 255 && me->domain != tgt_domain) continue;
                Print(L"  [t%d dom=%d] ", me->turn, (int)me->domain);
                for (int ci = 0; me->prompt[ci] && ci < 60; ci++)
                    Print(L"%c", (CHAR16)(unsigned char)me->prompt[ci]);
                Print(L"\r\n");
                shown++;
            }
            if (!shown) Print(L"  (no entries match)\r\n");
            Print(L"\r\n");
            continue;
        }
        // ─── Phase S: Pheromion Gradient commands ────────────────────────────
        if (my_strncmp(prompt, "/pheromion_mode", 15) == 0) {
            const char *arg = prompt + 15;
            while (*arg == ' ') arg++;
            PheromionMode pm = PHEROMION_MODE_TRACE;
            if (arg[0] == 'o' && arg[1] == 'f')
                pm = PHEROMION_MODE_OFF;
            else if (arg[0] == 'b')
                pm = PHEROMION_MODE_BOOST;
            pheromion_set_mode(&g_pheromion, pm);
            Print(L"\r\n[Pheromion] Mode set to: %s\r\n\r\n",
                  pm == PHEROMION_MODE_OFF  ? L"off" :
                  pm == PHEROMION_MODE_BOOST ? L"boost" : L"trace");
            continue;
        }
        if (my_strncmp(prompt, "/pheromion_status", 17) == 0) {
            uint32_t top = pheromion_top_path(&g_pheromion);
            Print(L"\r\n[Pheromion] mode=%s  top_path=%u\r\n",
                  g_pheromion.mode == PHEROMION_MODE_OFF   ? L"off" :
                  g_pheromion.mode == PHEROMION_MODE_BOOST ? L"boost" : L"trace",
                  (unsigned)top);
            static const char *dnames[7] = {
                "UNKNOWN","SYSTEM","POLICY","CHAT","CODE","MATH","CREATIVE"
            };
            static const char *rnames[4] = { "REFLEX","INTERNAL","EXTERNAL","DUAL" };
            for (int pi = 0; pi < PHEROMION_SLOT_MAX; pi++) {
                uint32_t pid = g_pheromion.slots[pi].path_id;
                uint32_t hc  = g_pheromion.slots[pi].hit_count;
                if (pid == 0xFFFFFFFFu || hc == 0) continue;
                if (pid >= 100 && pid <= 106) {
                    Print(L"  domain[%u] ", pid - 100);
                    const char *dn = dnames[pid - 100];
                    for (int ci = 0; dn[ci]; ci++) Print(L"%c", (CHAR16)(unsigned char)dn[ci]);
                    Print(L" : %u hits\r\n", hc);
                } else if (pid >= 200 && pid <= 203) {
                    Print(L"  route[%u]  ", pid - 200);
                    const char *rn = rnames[pid - 200];
                    for (int ci = 0; rn[ci]; ci++) Print(L"%c", (CHAR16)(unsigned char)rn[ci]);
                    Print(L" : %u hits\r\n", hc);
                }
            }
            Print(L"\r\n");
            continue;
        }
        /* ---- Novel Engine Commands ---- */
        if (my_strncmp(prompt, "/limbion_status", 15) == 0) {
            char lbuf[128];
            limbion_format_context(&g_limbion, lbuf, sizeof(lbuf));
            Print(L"\r\n[Limbion] ");
            for (int i = 0; lbuf[i]; i++) Print(L"%c", (CHAR16)(unsigned char)lbuf[i]);
            Print(L"\r\n  valence=%d arousal=%d\r\n\r\n",
                  (int)g_limbion.valence, (int)g_limbion.arousal);
            continue;
        }
        if (my_strncmp(prompt, "/chronion_status", 16) == 0) {
            Print(L"\r\n[Chronion] boot=%u steps_boot=%lu tokens_lifetime=%lu\r\n\r\n",
                  (unsigned)g_chronion.boot_count,
                  (unsigned long)g_chronion.steps_this_boot,
                  (unsigned long)g_chronion.tokens_lifetime);
            continue;
        }
        if (my_strncmp(prompt, "/trophion_status", 16) == 0) {
            char tbuf[128];
            trophion_format_context(&g_trophion, tbuf, sizeof(tbuf));
            Print(L"\r\n[Trophion] ");
            for (int i = 0; tbuf[i]; i++) Print(L"%c", (CHAR16)(unsigned char)tbuf[i]);
            Print(L"\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/mirrorion_status", 17) == 0) {
            Print(L"\r\n[Mirrorion] questions=%lu answers=%lu flushes=%u pending=%d\r\n",
                  (unsigned long)g_mirrorion.total_questions,
                  (unsigned long)g_mirrorion.total_answers,
                  (unsigned)g_mirrorion.flush_count,
                  (int)g_mirrorion.has_pending);
            if (g_mirrorion.has_pending) {
                Print(L"  pending_q: ");
                for (int i = 0; g_mirrorion.pending_question[i]; i++)
                    Print(L"%c", (CHAR16)(unsigned char)g_mirrorion.pending_question[i]);
                Print(L"\r\n");
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/mirrorion_flush", 16) == 0) {
            char mbuf[512];
            int n = mirrorion_flush_jsonl(&g_mirrorion, mbuf, sizeof(mbuf));
            Print(L"\r\n[Mirrorion] flushed %d bytes\r\n", n);
            if (n > 0) {
                for (int i = 0; i < n && mbuf[i]; i++)
                    Print(L"%c", (CHAR16)(unsigned char)mbuf[i]);
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/thanatosion_status", 19) == 0) {
            Print(L"\r\n[Thanatosion] deaths=%u rebirths=%u voluntary=%u dying_pressure=%d/%d\r\n",
                  (unsigned)g_thanatosion.total_deaths,
                  (unsigned)g_thanatosion.total_rebirths,
                  (unsigned)g_thanatosion.voluntary_deaths,
                  (int)g_thanatosion.dying_pressure_steps,
                  (int)g_thanatosion.dying_pressure_limit);
            if (g_thanatosion.last_death.cause_detail[0]) {
                Print(L"  last_cause: ");
                for (int i = 0; g_thanatosion.last_death.cause_detail[i]; i++)
                    Print(L"%c", (CHAR16)(unsigned char)g_thanatosion.last_death.cause_detail[i]);
                Print(L"\r\n");
            }
            Print(L"\r\n");
            continue;
        }
        /* ── Ghost engine ─────────────────────────────────────────────── */
        if (my_strncmp(prompt, "/ghost_status", 13) == 0) {
            static const char *gmode[] = { "off", "send", "recv" };
            static const char *gchan[] = { "led", "pcspk" };
            const char *gmn = gmode[g_ghost.mode < 3 ? g_ghost.mode : 0];
            const char *gcn = gchan[g_ghost.channel < 2 ? g_ghost.channel : 0];
            Print(L"\r\n[Ghost] mode=");
            for (int i = 0; gmn[i]; i++) Print(L"%c", (CHAR16)(unsigned char)gmn[i]);
            Print(L" channel=");
            for (int i = 0; gcn[i]; i++) Print(L"%c", (CHAR16)(unsigned char)gcn[i]);
            Print(L" sent=%u recv=%u ring_len=%u\r\n\r\n",
                  (unsigned)g_ghost.tokens_sent,
                  (unsigned)g_ghost.tokens_recv,
                  (unsigned)g_ghost.ring_len);
            continue;
        }
        if (my_strncmp(prompt, "/ghost_mode ", 12) == 0) {
            const char *arg = prompt + 12;
            if (my_strncmp(arg, "send", 4) == 0)      ghost_set_mode(&g_ghost, GHOST_MODE_SEND);
            else if (my_strncmp(arg, "recv", 4) == 0) ghost_set_mode(&g_ghost, GHOST_MODE_RECV);
            else                                       ghost_set_mode(&g_ghost, GHOST_MODE_OFF);
            Print(L"\r\n[Ghost] mode set\r\n\r\n");
            continue;
        }
        /* ── Morphion engine ──────────────────────────────────────────── */
        if (my_strncmp(prompt, "/morphion_status", 16) == 0) {
            /* Decode vendor string: EBX/EDX/ECX as 4 ascii chars each */
            uint32_t vb = g_morphion.probe.vendor_ebx;
            uint32_t vd = g_morphion.probe.vendor_edx;
            uint32_t vc = g_morphion.probe.vendor_ecx;
            char vendor[13];
            vendor[0]  = (char)( vb        & 0xFF); vendor[1]  = (char)((vb >>  8) & 0xFF);
            vendor[2]  = (char)((vb >> 16) & 0xFF); vendor[3]  = (char)((vb >> 24) & 0xFF);
            vendor[4]  = (char)( vd        & 0xFF); vendor[5]  = (char)((vd >>  8) & 0xFF);
            vendor[6]  = (char)((vd >> 16) & 0xFF); vendor[7]  = (char)((vd >> 24) & 0xFF);
            vendor[8]  = (char)( vc        & 0xFF); vendor[9]  = (char)((vc >>  8) & 0xFF);
            vendor[10] = (char)((vc >> 16) & 0xFF); vendor[11] = (char)((vc >> 24) & 0xFF);
            vendor[12] = 0;
            int avx2   = (int)((g_morphion.probe.features_ebx >> 5)  & 1);
            int avx512 = (int)((g_morphion.probe.features_ebx >> 16) & 1);
            Print(L"\r\n[Morphion] vendor=");
            for (int i = 0; i < 12; i++) if (vendor[i] >= 0x20) Print(L"%c", (CHAR16)(unsigned char)vendor[i]);
            Print(L" features_ebx=0x%08X  AVX2=%d AVX512F=%d  modules=%u\r\n\r\n",
                  (unsigned)g_morphion.probe.features_ebx,
                  avx2, avx512,
                  (unsigned)g_morphion.module_count);
            continue;
        }
        if (my_strncmp(prompt, "/morphion_probe", 15) == 0) {
            morphion_set_mode(&g_morphion, MORPHION_MODE_PROBE);
            morphion_probe(&g_morphion);
            Print(L"\r\n[Morphion] probe complete — use /morphion_status\r\n\r\n");
            continue;
        }
        /* ── Conscience engine ────────────────────────────────────────── */
        if (my_strncmp(prompt, "/conscience_status", 18) == 0) {
            static const char *pname[] = { "f32", "f16", "q8", "q4" };
            ConscienceSample cs; conscience_sample(&g_conscience, &cs);
            const char *prn = pname[g_conscience.current_precision < 4 ? g_conscience.current_precision : 0];
            Print(L"\r\n[Conscience] mode=");
            const char *cmn = conscience_mode_name_ascii(g_conscience.mode);
            for (int i = 0; cmn[i]; i++) Print(L"%c", (CHAR16)(unsigned char)cmn[i]);
            Print(L" precision=");
            for (int i = 0; prn[i]; i++) Print(L"%c", (CHAR16)(unsigned char)prn[i]);
            Print(L" samples=%u downgrades=%u stress=%u%%\r\n\r\n",
                  (unsigned)g_conscience.samples_taken,
                  (unsigned)g_conscience.downgrades_triggered,
                  (unsigned)cs.stress);
            continue;
        }
        if (my_strncmp(prompt, "/conscience_mode ", 17) == 0) {
            const char *arg = prompt + 17;
            if      (my_strncmp(arg, "act",   3) == 0) conscience_set_mode(&g_conscience, CONSCIENCE_MODE_ACT);
            else if (my_strncmp(arg, "watch", 5) == 0) conscience_set_mode(&g_conscience, CONSCIENCE_MODE_WATCH);
            else                                        conscience_set_mode(&g_conscience, CONSCIENCE_MODE_OFF);
            Print(L"\r\n[Conscience] mode set\r\n\r\n");
            continue;
        }
        /* ── Collectivion engine ──────────────────────────────────────── */
        if (my_strncmp(prompt, "/collectivion_status", 20) == 0) {
            Print(L"\r\n[Collectivion] mode=");
            const char *colm = collectivion_mode_name_ascii(g_collectivion.mode);
            for (int i = 0; colm[i]; i++) Print(L"%c", (CHAR16)(unsigned char)colm[i]);
            Print(L" node_id=%u sent=%u recv=%u\r\n\r\n",
                  (unsigned)g_collectivion.node_id,
                  (unsigned)g_collectivion.broadcasts_sent,
                  (unsigned)g_collectivion.broadcasts_recv);
            continue;
        }
        if (my_strncmp(prompt, "/collectivion_mode ", 19) == 0) {
            const char *arg = prompt + 19;
            if      (my_strncmp(arg, "active",  6) == 0) collectivion_set_mode(&g_collectivion, COLLECTIVION_MODE_ACTIVE);
            else if (my_strncmp(arg, "passive", 7) == 0) collectivion_set_mode(&g_collectivion, COLLECTIVION_MODE_PASSIVE);
            else                                          collectivion_set_mode(&g_collectivion, COLLECTIVION_MODE_OFF);
            Print(L"\r\n[Collectivion] mode set\r\n\r\n");
            continue;
        }
        /* ── NeuralFS engine ──────────────────────────────────────────── */
        if (my_strncmp(prompt, "/neuralfs_status", 16) == 0) {
            Print(L"\r\n[NeuralFS] mode=");
            const char *nfm = neuralfs_mode_name_ascii(g_neuralfs.mode);
            for (int i = 0; nfm[i]; i++) Print(L"%c", (CHAR16)(unsigned char)nfm[i]);
            Print(L" blobs=%u queries=%u\r\n\r\n",
                  (unsigned)g_neuralfs.blobs_indexed,
                  (unsigned)g_neuralfs.queries_done);
            continue;
        }
        if (my_strncmp(prompt, "/neuralfs_query ", 16) == 0) {
            const char *query = prompt + 16;
            NeuralfsMatch matches[4];
            uint32_t found = neuralfs_query(&g_neuralfs, query, matches, 4);
            Print(L"\r\n[NeuralFS] query: ");
            for (int i = 0; query[i]; i++) Print(L"%c", (CHAR16)(unsigned char)query[i]);
            Print(L"\r\n");
            if (found == 0) {
                Print(L"  (no match)\r\n\r\n");
            } else {
                for (uint32_t i = 0; i < found; i++) {
                    Print(L"  blob=%u score=%u\r\n",
                          (unsigned)matches[i].blob_id,
                          (unsigned)matches[i].score);
                }
                Print(L"\r\n");
            }
            continue;
        }
        /* ── Evolvion codegen ─────────────────────────────────────────── */
        if (my_strncmp(prompt, "/evolvion_codegen", 17) == 0) {
            evolvion_set_mode(&g_evolvion, EVOLVION_MODE_LIVE);
            evolvion_record_need(&g_evolvion, EVOLVION_NEED_COMPUTE, "generate bare-metal C function stub for OO kernel extension");
            Print(L"\r\n[Evolvion] codegen need recorded — run /ssm_infer to materialize\r\n\r\n");
            continue;
        }
        /* ── OO-NET Distributed OO ───────────────────────────────────── */
        if (my_strncmp(prompt, "/oo_net_status", 14) == 0) {
            Print(L"\r\n[OO-NET] node_id=%u  sent=%u recv=%u merges=%u  ghost_pkts_sent=%u recv=%u\r\n",
                  (unsigned)g_collectivion.node_id,
                  (unsigned)g_collectivion.broadcasts_sent,
                  (unsigned)g_collectivion.broadcasts_recv,
                  (unsigned)g_collectivion.dna_merges,
                  (unsigned)g_ghost.pkts_sent,
                  (unsigned)g_ghost.pkts_recv);
            Print(L"  peers:");
            int any = 0;
            for (int _i = 0; _i < COLLECTIVION_PEER_MAX; _i++) {
                if (g_collectivion.peers[_i].node_id != 0xFFFFFFFFU) {
                    Print(L" [node=%u seq=%u dna=0x%08X]",
                          (unsigned)g_collectivion.peers[_i].node_id,
                          (unsigned)g_collectivion.peers[_i].last_seen_seq,
                          (unsigned)g_collectivion.peers[_i].dna_hash);
                    any = 1;
                }
            }
            if (!any) Print(L" (none yet)");
            Print(L"\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_net_hello", 13) == 0) {
            collectivion_set_mode(&g_collectivion, COLLECTIVION_MODE_ACTIVE);
            ghost_set_mode(&g_ghost, GHOST_MODE_SEND);
            collectivion_send_hello(&g_collectivion, &g_ghost);
            Print(L"\r\n[OO-NET] HELLO broadcast (node_id=%u)\r\n\r\n",
                  (unsigned)g_collectivion.node_id);
            continue;
        }
        if (my_strncmp(prompt, "/oo_net_send ", 13) == 0) {
            const char *txt = prompt + 13;
            collectivion_set_mode(&g_collectivion, COLLECTIVION_MODE_ACTIVE);
            ghost_set_mode(&g_ghost, GHOST_MODE_SEND);
            collectivion_send_text(&g_collectivion, &g_ghost, txt);
            Print(L"\r\n[OO-NET] TEXT sent: ");
            for (int _k = 0; txt[_k]; _k++) Print(L"%c", (CHAR16)(unsigned char)txt[_k]);
            Print(L"\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_net_sync_dna", 16) == 0) {
            collectivion_set_mode(&g_collectivion, COLLECTIVION_MODE_ACTIVE);
            ghost_set_mode(&g_ghost, GHOST_MODE_SEND);
            uint32_t dh = soma_dna_hash(&g_soma_dna);
            collectivion_sync_dna(&g_collectivion, &g_ghost,
                                  dh, 0.0f, 0.0f);
            Print(L"\r\n[OO-NET] DNA_SYNC broadcast (hash=0x%08X)\r\n\r\n",
                  (unsigned)dh);
            continue;
        }
        if (my_strncmp(prompt, "/oo_net_recv", 12) == 0) {
            ghost_set_mode(&g_ghost, GHOST_MODE_RECV);
            float dt = 0.f, dp = 0.f;
            int n = collectivion_recv_all(&g_collectivion, &g_ghost, &dt, &dp);
            if (n == 0) {
                Print(L"\r\n[OO-NET] no packets received\r\n\r\n");
            } else {
                Print(L"\r\n[OO-NET] %d packet(s) processed", n);
                if (g_collectivion.dna_merges > 0)
                    Print(L" | DNA delta: temp=%d.%02d topp=%d.%02d",
                          (int)dt, (int)(dt*100)%100,
                          (int)dp, (int)(dp*100)%100);
                Print(L"\r\n\r\n");
            }
            continue;
        }
        if (my_strncmp(prompt, "/oo_probe_pci", 13) == 0) {
            oo_driver_probe_pci(&g_oo_driver_probe);
            Print(L"\r\n[OO-Driver] PCI scan: %d device(s), %d unknown\r\n",
                  (int)g_oo_driver_probe.device_count,
                  (int)g_oo_driver_probe.unknown_count);
            for (uint8_t _i = 0; _i < g_oo_driver_probe.device_count; _i++) {
                OoPciDevice *_d = &g_oo_driver_probe.devices[_i];
                const char *_vn = oo_pci_vendor_name(_d->vendor_id);
                const char *_cn = oo_pci_class_name(_d->class_code, _d->subclass);
                Print(L"  [%d] %02X:%02X  %04X:%04X  ",
                      (int)_i, (unsigned)_d->bus, (unsigned)_d->dev,
                      (unsigned)_d->vendor_id, (unsigned)_d->device_id);
                for (int _k = 0; _vn[_k]; _k++) Print(L"%c", (CHAR16)(unsigned char)_vn[_k]);
                Print(L"/");
                for (int _k = 0; _cn[_k]; _k++) Print(L"%c", (CHAR16)(unsigned char)_cn[_k]);
                Print(L"%s\r\n", _d->known ? L"" : L" [UNKNOWN - needs driver]");
            }
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_driver_gen ", 15) == 0) {
            /* Parse vid:did hex from argument, e.g. "8086:100E" */
            const char *arg = prompt + 15;
            uint16_t vid = 0, did = 0;
            int got_colon = 0;
            for (int _c = 0; arg[_c] && _c < 9; _c++) {
                char ch = arg[_c];
                if (ch == ':') { got_colon = 1; continue; }
                uint8_t nibble = (ch >= '0' && ch <= '9') ? (uint8_t)(ch - '0') :
                                 (ch >= 'A' && ch <= 'F') ? (uint8_t)(ch - 'A' + 10) :
                                 (ch >= 'a' && ch <= 'f') ? (uint8_t)(ch - 'a' + 10) : 0xFF;
                if (nibble == 0xFF) break;
                if (!got_colon) vid = (uint16_t)((vid << 4) | nibble);
                else            did = (uint16_t)((did << 4) | nibble);
            }
            const char *_cn = oo_pci_class_name(0xFF, 0xFF);
            /* Find class from probe table */
            for (uint8_t _i = 0; _i < g_oo_driver_probe.device_count; _i++) {
                if (g_oo_driver_probe.devices[_i].vendor_id == vid &&
                    g_oo_driver_probe.devices[_i].device_id == did) {
                    _cn = oo_pci_class_name(g_oo_driver_probe.devices[_i].class_code,
                                            g_oo_driver_probe.devices[_i].subclass);
                    break;
                }
            }
            evolvion_set_mode(&g_evolvion, EVOLVION_MODE_LIVE);
            evolvion_build_driver_prompt(&g_evolvion, vid, did, _cn,
                                         oo_pci_vendor_name(vid),
                                         g_evolvion.codegen_buf,
                                         EVOLVION_CODEGEN_BUF);
            g_evolvion.drivers_generated++;
            Print(L"\r\n[OO-Driver] prompt built. Use /oo_driver_status then /ssm_infer\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_driver_status", 17) == 0) {
            Print(L"\r\n[OO-Driver] evolvion mode=");
            const char *_em = evolvion_mode_name_ascii(g_evolvion.mode);
            for (int _k = 0; _em[_k]; _k++) Print(L"%c", (CHAR16)(unsigned char)_em[_k]);
            Print(L" needs=%u attempts=%u drivers_gen=%u queue=%u\r\n",
                  (unsigned)g_evolvion.needs_recorded,
                  (unsigned)g_evolvion.codegen_attempts,
                  (unsigned)g_evolvion.drivers_generated,
                  (unsigned)g_evolvion.driver_need_count);
            if (g_evolvion.codegen_buf[0]) {
                Print(L"  prompt: ");
                for (int _k = 0; g_evolvion.codegen_buf[_k]; _k++)
                    Print(L"%c", (CHAR16)(unsigned char)g_evolvion.codegen_buf[_k]);
                Print(L"\r\n");
            }
            Print(L"\r\n");
            continue;
        }
        /* /oo_entropy_status — quantum RNG diagnostics */
        if (my_strncmp(prompt, "/oo_entropy_status", 18) == 0) {
            Print(L"\r\n[OO-Entropy] Quantum RNG status\r\n");
            Print(L"  rdrand_available : %s\r\n",
                  g_quantum_rng.rdrand_available == 1 ? L"YES (hardware TRNG)" :
                  g_quantum_rng.rdrand_available == -1 ? L"NO  (RDTSC fallback)" : L"not yet probed");
            Print(L"  rdrand_ok        : %u\r\n", (unsigned)g_quantum_rng.rdrand_ok);
            Print(L"  rdrand_fail      : %u\r\n", (unsigned)g_quantum_rng.rdrand_fail);
            Print(L"  rdtsc_mix_count  : %u  (per-token jitter injections)\r\n",
                  (unsigned)g_quantum_rng.rdtsc_mix_count);
            Print(L"  quantum_seeds    : %u  (full re-seeds from hardware)\r\n",
                  (unsigned)g_quantum_rng.quantum_seeds);
            Print(L"  seed_last        : 0x%08X\r\n", (unsigned)g_quantum_rng.seed_last);
            Print(L"  tip: /seed 0     - re-seed from hardware entropy now\r\n");
            Print(L"\r\n");
            continue;
        }
        /* /oo_self_model — OO introspection snapshot */
        if (my_strncmp(prompt, "/oo_self_model", 14) == 0) {
            oo_self_model_update(&g_oo_self_model, &g_zones,
                                  &g_conscience, &g_metabion,
                                  &g_soma_warden, &g_soma_dna,
                                  (unsigned int)g_metrics.total_decode_tokens);
            oo_self_model_to_prefix(&g_oo_self_model);
            oo_self_model_print(&g_oo_self_model);
            continue;
        }
        /* /nfs2_list — list all NeuralFS2 keys */
        if (my_strncmp(prompt, "/nfs2_list", 10) == 0) {
            _sh_cmd_ls(&g_oo_shell, &g_nfs2);
            continue;
        }
        /* /nfs2_write <key> <val> — write to NFS2 */
        if (my_strncmp(prompt, "/nfs2_write ", 12) == 0) {
            const char *rest = prompt + 12;
            while (*rest == ' ') rest++;
            const char *val = _sh_skip_word(rest);
            if (!*val) { Print(L"  usage: /nfs2_write <key> <value>\r\n"); continue; }
            char key[NFS2_NAME_MAX]; int ki = 0;
            while (rest[ki] && rest[ki] != ' ' && ki < NFS2_NAME_MAX-1) { key[ki] = rest[ki]; ki++; }
            key[ki] = '\0';
            int rc = nfs2_write(&g_nfs2, key, val);
            Print(L"  nfs2_write %s: %d\r\n", rc == 0 ? L"OK" : L"ERR", rc);
            continue;
        }
        /* /nfs2_read <key> — read from NFS2 */
        if (my_strncmp(prompt, "/nfs2_read ", 11) == 0) {
            const char *key = prompt + 11;
            while (*key == ' ') key++;
            const char *val = nfs2_read(&g_nfs2, key);
            if (!val) { Print(L"  (not found)\r\n"); continue; }
            Print(L"\r\n  ");
            for (int k = 0; val[k]; k++) {
                if (val[k] == '\n') Print(L"\r\n  ");
                else Print(L"%c", (CHAR16)(unsigned char)val[k]);
            }
            Print(L"\r\n");
            continue;
        }
        /* /nfs2_del <key> — delete from NFS2 */
        if (my_strncmp(prompt, "/nfs2_del ", 10) == 0) {
            const char *key = prompt + 10;
            while (*key == ' ') key++;
            int rc = nfs2_delete(&g_nfs2, key);
            Print(L"  nfs2_del %s: %d\r\n", rc == 0 ? L"OK" : L"ERR", rc);
            continue;
        }
        /* /shell — enter interactive OO shell */
        if (my_strncmp(prompt, "/shell", 6) == 0) {
            g_oo_shell.active = 1;
            Print(L"\r\n[OO Shell] v1.0 — type 'help' for commands, 'exit' to return\r\n");
            oo_self_model_update(&g_oo_self_model, &g_zones,
                                  &g_conscience, &g_metabion,
                                  &g_soma_warden, &g_soma_dna,
                                  (unsigned int)g_metrics.total_decode_tokens);
            while (g_oo_shell.active) {
                oo_shell_prompt(&g_oo_shell);
                char shell_line[OO_SHELL_LINE_MAX];
                EFI_STATUS srl = llmk_read_line(shell_line, OO_SHELL_LINE_MAX);
                if (EFI_ERROR(srl)) break;
                Print(L"\r\n");
                oo_shell_exec(&g_oo_shell, shell_line, &g_nfs2,
                              &g_oo_self_model, &g_zones);
            }
            continue;
        }
        if (my_strncmp(prompt, "/session_score", 14) == 0) {
            int sc = soma_session_score(&g_soma_session, &g_soma_warden);
            char sbuf[128];
            soma_session_status_str(&g_soma_session, sbuf, 128);
            Print(L"\r\n[Session] ");
            for (int si = 0; sbuf[si]; si++)
                Print(L"%c", (CHAR16)(unsigned char)sbuf[si]);
            Print(L"\r\n  fitness=%d%%  mutation_mag=", sc);
            // Print magnitude manually (2 decimal float)
            int mag_i = (int)(g_soma_session.mutation_magnitude * 100.0f + 0.5f);
            Print(L"0.%02d\r\n\r\n", mag_i);
            continue;
        }
        if (my_strncmp(prompt, "/dna_evolve_session", 19) == 0) {
            // Score first
            int sc = soma_session_score(&g_soma_session, &g_soma_warden);
            uint32_t rng = g_oosi_v3_valid ? g_oosi_v3_ctx.rng_state : 0xDEADBEEFu;
            uint32_t old_hash = soma_dna_hash(&g_soma_dna);
            int new_gen = soma_session_evolve_dna(&g_soma_session, &g_soma_dna, &rng);
            if (g_oosi_v3_valid) g_oosi_v3_ctx.rng_state = rng;
            uint32_t new_hash = soma_dna_hash(&g_soma_dna);
            int mag_i = (int)(g_soma_session.mutation_magnitude * 100.0f + 0.5f);
            Print(L"\r\n[DNA] Session-scored evolution: fitness=%d%% mag=0.%02d\r\n",
                  sc, mag_i);
            Print(L"  gen %d -> %d  hash 0x%08X -> 0x%08X\r\n",
                  new_gen - 1, new_gen, old_hash, new_hash);
            Print(L"  bias=%.2f conf_thr=%.2f temp_S=%.2f temp_L=%.2f\r\n",
                  (double)g_soma_dna.cognition_bias,
                  (double)g_soma_dna.confidence_threshold,
                  (double)g_soma_dna.temperature_solar,
                  (double)g_soma_dna.temperature_lunar);
            // Phase O: auto-save evolved DNA
            if (g_root) {
                int dret = soma_dna_save(&g_soma_dna, g_root);
                Print(L"  [DNA] %s\r\n\r\n",
                      dret == SOMA_DNA_PERSIST_OK ? L"Auto-saved to soma_dna.bin" : L"WARNING: save failed");
            } else {
                Print(L"  [DNA] (no EFI root — save skipped)\r\n\r\n");
            }
            continue;
        }
        // ─── Phase O: DNA Persistence commands ───────────────────────────
        if (my_strncmp(prompt, "/dna_save", 9) == 0) {
            if (!g_root) {
                Print(L"\r\n[DNA] ERROR: no EFI root\r\n\r\n"); continue;
            }
            int dret = soma_dna_save(&g_soma_dna, g_root);
            if (dret == SOMA_DNA_PERSIST_OK)
                Print(L"\r\n[DNA] Saved to soma_dna.bin (gen=%d hash=0x%08X)\r\n\r\n",
                      (int)g_soma_dna.generation, soma_dna_hash(&g_soma_dna));
            else
                Print(L"\r\n[DNA] ERROR: save failed (%d)\r\n\r\n", dret);
            continue;
        }
        if (my_strncmp(prompt, "/dna_load", 9) == 0) {
            if (!g_root) {
                Print(L"\r\n[DNA] ERROR: no EFI root\r\n\r\n"); continue;
            }
            int dret = soma_dna_load(&g_soma_dna, g_root);
            if (dret == SOMA_DNA_PERSIST_OK)
                Print(L"\r\n[DNA] Loaded: gen=%d bias=%.2f conf=%.2f hash=0x%08X\r\n\r\n",
                      (int)g_soma_dna.generation,
                      (double)g_soma_dna.cognition_bias,
                      (double)g_soma_dna.avg_confidence,
                      soma_dna_hash(&g_soma_dna));
            else if (dret == SOMA_DNA_PERSIST_NOT_FOUND)
                Print(L"\r\n[DNA] soma_dna.bin not found\r\n\r\n");
            else
                Print(L"\r\n[DNA] soma_dna.bin corrupt — defaults restored\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/dna_reset", 10) == 0) {
            soma_dna_init_default(&g_soma_dna);
            if (g_root) soma_dna_delete(g_root);
            Print(L"\r\n[DNA] Reset to defaults. soma_dna.bin deleted. gen=0\r\n\r\n");
            continue;
        }
        // Phase Q: show current DNA sampler blend
        if (my_strncmp(prompt, "/dna_sampler", 12) == 0) {
            float t_logic  = soma_dna_blend_temperature(&g_soma_dna, SOMA_DOMAIN_MATH,     g_temperature);
            float t_chat   = soma_dna_blend_temperature(&g_soma_dna, SOMA_DOMAIN_CHAT,     g_temperature);
            float t_create = soma_dna_blend_temperature(&g_soma_dna, SOMA_DOMAIN_CREATIVE, g_temperature);
            float tp_logic  = soma_dna_blend_top_p(&g_soma_dna, SOMA_DOMAIN_MATH,     g_top_p);
            float tp_chat   = soma_dna_blend_top_p(&g_soma_dna, SOMA_DOMAIN_CHAT,     g_top_p);
            float tp_create = soma_dna_blend_top_p(&g_soma_dna, SOMA_DOMAIN_CREATIVE, g_top_p);
            Print(L"\r\n[DNA-Sampler] gen=%d  bias=%d%%  pressure=%d\r\n",
                  (int)g_soma_dna.generation,
                  (int)(g_soma_dna.cognition_bias * 100.0f),
                  g_soma_warden.pressure_level);
            Print(L"  solar: temp=%d/1000 top_p=%d/1000\r\n",
                  (int)(g_soma_dna.temperature_solar * 1000.0f),
                  (int)(g_soma_dna.top_p_solar * 1000.0f));
            Print(L"  lunar: temp=%d/1000 top_p=%d/1000\r\n",
                  (int)(g_soma_dna.temperature_lunar * 1000.0f),
                  (int)(g_soma_dna.top_p_lunar * 1000.0f));
            Print(L"  blended → logic:  temp=%d/1000 top_p=%d/1000\r\n",
                  (int)(t_logic  * 1000.0f), (int)(tp_logic  * 1000.0f));
            Print(L"  blended → chat:   temp=%d/1000 top_p=%d/1000\r\n",
                  (int)(t_chat   * 1000.0f), (int)(tp_chat   * 1000.0f));
            Print(L"  blended → create: temp=%d/1000 top_p=%d/1000\r\n\r\n",
                  (int)(t_create * 1000.0f), (int)(tp_create * 1000.0f));
            continue;
        }
        // ── end SomaMind commands ────────────────────────────────────────

        if (my_strncmp(prompt, "/models", 7) == 0) {
            Print(L"\r\nModels (.bin/.gguf):\r\n");
            Print(L"Root:\r\n");
            llmk_models_ls_best_effort(NULL, 200);
            Print(L"\r\nmodels\\:\r\n");
            llmk_models_ls_best_effort(L"models", 200);
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/model_info", 11) == 0) {
            CHAR16 path16[192];
            path16[0] = 0;

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
                StrCpy(path16, L"model.bin");
            }

            if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF &&
                g_loaded_model_gguf_valid &&
                llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                llmk_print_gguf_summary_block(path16, &g_loaded_model_gguf);
                Print(L"\r\n");
                continue;
            }

            EFI_FILE_HANDLE f = NULL;
            EFI_STATUS st = llmk_open_read_file(&f, path16);
            if (EFI_ERROR(st) || !f) {
                Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", path16, st);
                continue;
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
                Print(L"\r\n");
                continue;
            }

            EFI_STATUS pst = uefi_call_wrapper(f->SetPosition, 2, f, 0);
            if (EFI_ERROR(pst)) {
                uefi_call_wrapper(f->Close, 1, f);
                Print(L"\r\nERROR: seek failed (%r)\r\n\r\n", pst);
                continue;
            }
            int hdr[7]; // SAFE: fixed-size BIN header (7 ints) read in one shot
            for (int k = 0; k < 7; k++) hdr[k] = 0;
            UINTN bytes = (UINTN)(7 * sizeof(int));
            EFI_STATUS rst = uefi_call_wrapper(f->Read, 3, f, &bytes, hdr);
            uefi_call_wrapper(f->Close, 1, f);
            if (EFI_ERROR(rst) || bytes != (UINTN)(7 * sizeof(int))) {
                Print(L"\r\nBIN: failed to read header (%r)\r\n\r\n", rst);
                continue;
            }

            int dim = hdr[0];
            int n_layers = hdr[2]; // SAFE: constant index into fixed-size hdr[7]
            int n_heads = hdr[3]; // SAFE: constant index into fixed-size hdr[7]
            int n_kv_heads = hdr[4]; // SAFE: constant index into fixed-size hdr[7]
            int vocab = hdr[5]; // SAFE: constant index into fixed-size hdr[7]
            int seq_len = hdr[6]; // SAFE: constant index into fixed-size hdr[7]
            int shared = (vocab < 0);
            if (vocab < 0) vocab = -vocab;
            Print(L"\r\nBIN model info:\r\n");
            Print(L"  file=%s\r\n", path16);
            Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d shared_cls=%d\r\n\r\n",
                  dim, n_layers, n_heads, n_kv_heads, vocab, seq_len, shared);
            continue;
        }
        if (my_strncmp(prompt, "/cat", 4) == 0) {
            int i = 4;
            while (prompt[i] == ' ') i++;
            if (prompt[i] == 0) {
                Print(L"\r\nUsage: /cat <path>\r\n\r\n");
                continue;
            }
            char p8[192];
            int n = 0;
            while (prompt[i] && n + 1 < (int)sizeof(p8)) p8[n++] = prompt[i++];
            p8[n] = 0;
            CHAR16 path16[256];
            ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));

            // Pre-resolve long filenames via FAT 8.3 alias so cat works on firmwares
            // with unreliable LFN opens.
            {
                EFI_FILE_HANDLE tf = NULL;
                CHAR16 picked[256];
                picked[0] = 0;
                EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, path16, &tf, picked,
                                                                  (int)(sizeof(picked) / sizeof(picked[0])),
                                                                  L"cat");
                if (EFI_ERROR(st) || !tf) {
                    Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", path16, st);
                    continue;
                }
                uefi_call_wrapper(tf->Close, 1, tf);
                if (picked[0]) {
                    llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), picked);
                }
            }

            llmk_fs_cat_best_effort(path16, 256U * 1024U);
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_handoff_info", 16) == 0) {
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
            llmk_oo_journal_cmd_best_effort("oo_handoff_info");
            continue;
        }
        if (my_strncmp(prompt, "/oo_handoff_apply", 17) == 0) {
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
            llmk_oo_journal_cmd_best_effort("oo_handoff_apply");
            continue;
        }
        if (my_strncmp(prompt, "/oo_handoff_receipt", 19) == 0) {
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
            llmk_oo_journal_cmd_best_effort("oo_handoff_receipt");
            continue;
        }
        if (my_strncmp(prompt, "/oo_continuity_status", 21) == 0) {
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
            llmk_oo_journal_cmd_best_effort("oo_continuity_status");
            continue;
        }
        if (my_strncmp(prompt, "/oo_reboot_probe", 16) == 0) {
            llmk_oo_reboot_probe_best_effort();
            continue;
        }

        // Minimal autorun controls in no-model mode.
        if (my_strncmp(prompt, "/autorun_stop", 13) == 0) {
            Print(L"\r\n[autorun] stopping\r\n\r\n");
            llmk_autorun_stop();
            continue;
        }

        // Minimal OO commands (no-model mode): supports journaling + persistence demos without LLM.
        if (prompt[0] == '/' && my_strncmp(prompt, "/oo", 3) == 0) {
            if (!llmk_oo_policy_check_prompt_and_warn(prompt)) {
                continue;
            }
        }
        if (my_strncmp(prompt, "/oo_infermini", 13) == 0) {
            const char *args = prompt + 13;
            while (*args == ' ' || *args == '\t') args++;
            llmk_oo_infermini_no_model(args);
            llmk_oo_journal_cmd_best_effort("oo_infermini");
            continue;
        }
        if (my_strncmp(prompt, "/oo_list", 8) == 0) {
            llmk_oo_list_print();
            llmk_oo_journal_cmd_best_effort("oo_list");
            continue;
        }
        if (my_strncmp(prompt, "/oo_status", 10) == 0) {
            int consult_enabled = g_cfg_oo_llm_consult;
            if (consult_enabled < 0) consult_enabled = g_cfg_oo_enable ? 1 : 0;
            int multi_enabled = g_cfg_oo_multi_actions;
            if (multi_enabled < 0) multi_enabled = (consult_enabled > 0) ? 1 : 0;

            Print(L"\r\nOO status (no model):\r\n");
            Print(L"  oo_enable=%d\r\n", g_cfg_oo_enable);
            Print(L"  llm_consult=%d multi_actions=%d\r\n", consult_enabled, multi_enabled);
            Print(L"  conf_gate=%d conf_threshold=%d\r\n", g_cfg_oo_conf_gate, g_cfg_oo_conf_threshold);
            llmk_oo_print_persistence_status_best_effort();
            Print(L"\r\nHint: /oo_new <goal>, /oo_list, /oo_log, /oo_jour, /oo_outcome, /oo_continuity_status, /oo_reboot_probe\r\n\r\n");
            llmk_oo_journal_cmd_best_effort("oo_status");
            continue;
        }
        if (my_strncmp(prompt, "/oo_consult_mock", 15) == 0) {
            int consult_enabled = g_cfg_oo_llm_consult;
            if (consult_enabled < 0) {
                consult_enabled = g_cfg_oo_enable;
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

            UINT64 ram_mb = llmk_get_conventional_ram_bytes_best_effort() / (1024ULL * 1024ULL);
            UINT32 mode = g_oo_last_mode_valid ? g_oo_last_mode : LLMK_OO_MODE_SAFE;
            UINT64 boots = 0;
            int cfg_ctx = 0;
            int cfg_seq = 0;
            {
                LlmkOoState s;
                if (llmk_oo_load_state_best_effort(&s)) {
                    boots = s.boot_count;
                    mode = s.mode;
                }
            }
            (void)llmk_repl_cfg_read_ctx_seq_best_effort(&cfg_ctx, &cfg_seq);

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
            llmk_oo_consult_process_suggestion(ram_mb, mode, boots, cfg_ctx, cfg_seq, sugg);
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_new", 7) == 0) {
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
        }
        if (my_strncmp(prompt, "/oo_save", 8) == 0) {
            // In no-model mode, use the default OO state file (best-effort).
            int n = 0;
            EFI_STATUS st = llmk_oo_save_to_file_best_effort(L"OOSTATE.TXT", &n);
            if (EFI_ERROR(st)) {
                Print(L"\r\nERROR: failed to write OOSTATE.TXT: %r\r\n\r\n", st);
            } else {
                Print(L"\r\nOK: wrote OOSTATE.TXT (%d bytes)\r\n\r\n", n);
                llmk_oo_journal_cmd_best_effort("oo_save");
            }
            continue;
        }
        if (my_strncmp(prompt, "/oo_load", 8) == 0) {
            void *buf = NULL;
            UINTN len = 0;
            EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOSTATE.TXT", &buf, &len);
            if (EFI_ERROR(st) || !buf || len == 0) {
                if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
                Print(L"\r\nERROR: failed to read OOSTATE.TXT: %r\r\n\r\n", st);
                continue;
            }
            int imported = llmk_oo_import((const char *)buf, (int)len);
            uefi_call_wrapper(BS->FreePool, 1, buf);
            if (imported < 0) {
                Print(L"\r\nERROR: parse failed\r\n\r\n");
            } else {
                Print(L"\r\nOK: loaded %d entity(s) from OOSTATE.TXT\r\n\r\n", imported);
                llmk_oo_journal_cmd_best_effort("oo_load");
            }
            continue;
        }
        if (my_strncmp(prompt, "/oo_log", 7) == 0) {
            Print(L"\r\n[oo_log] latest summary:\r\n");
            llmk_oo_print_last_consult_status_best_effort();
            Print(L"\r\n[oo_log] OOCONSULT.LOG tail:\r\n");
            llmk_oo_print_ooconsult_tail_best_effort(10);
            Print(L"\r\n");
            llmk_oo_journal_cmd_best_effort("oo_log");
            continue;
        }
        if (my_strncmp(prompt, "/oo_explain", 11) == 0) {
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
        }
        if (my_strncmp(prompt, "/oo_jour", 8) == 0 || my_strncmp(prompt, "/oo_journal", 11) == 0) {
            Print(L"\r\n[oo_jour] OOJOUR.LOG tail:\r\n");
            llmk_oo_print_oojour_tail_best_effort(10);
            Print(L"\r\n");
            llmk_oo_journal_cmd_best_effort("oo_jour");
            continue;
        }
        if (my_strncmp(prompt, "/oo_outcome", 11) == 0) {
            Print(L"\r\n[oo_outcome] OOOUTCOME.LOG tail:\r\n");
            llmk_oo_print_oooutcome_tail_best_effort(10);
            Print(L"\r\n");
            llmk_oo_journal_cmd_best_effort("oo_outcome");
            continue;
        }

        if (my_strncmp(prompt, "exit", 4) == 0 || my_strncmp(prompt, "quit", 4) == 0) {
            Print(L"\r\nBye.\r\n");
            return;
        }
        if (my_strncmp(prompt, "reboot", 6) == 0 || my_strncmp(prompt, "reset", 5) == 0) {
            Print(L"\r\nRebooting...\r\n");
            uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
            return;
        }
        if (my_strncmp(prompt, "shutdown", 8) == 0 || my_strncmp(prompt, "poweroff", 8) == 0) {
            Print(L"\r\nShutting down...\r\n");
            llmk_shutdown_best_effort();
            return;
        }

        // SSM / Mamba commands (no-model mode)
        if (my_strncmp(prompt, "/mind_status", 12) == 0) {
            llmk_mind_print_status();
            continue;
        }
        if (my_strncmp(prompt, "/mind_diag", 10) == 0) {
            llmk_mind_print_diag();
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_probe", 16) == 0) {
            const char *arg = prompt + 16;
            while (*arg == ' ' || *arg == '\t') arg++;
            float loop_pos = 1.0f;
            if (*arg) {
                float parsed = 0.0f;
                if (!llmk_cfg_parse_f32(arg, &parsed)) {
                    Print(L"\r\nUsage: /mind_halt_probe [loop_pos]\r\n");
                    Print(L"  Example: /mind_halt_probe 1.0\r\n\r\n");
                    continue;
                }
                loop_pos = parsed;
            }
            llmk_mind_halting_probe(loop_pos);
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_decide", 17) == 0) {
            const char *arg = prompt + 17;
            while (*arg == ' ' || *arg == '\t') arg++;
            float loop_pos = 1.0f;
            float threshold = 0.5f;
            if (*arg) {
                float parsed = 0.0f;
                if (!llmk_cfg_parse_f32(arg, &parsed)) {
                    Print(L"\r\nUsage: /mind_halt_decide [loop_pos] [threshold]\r\n");
                    Print(L"  Example: /mind_halt_decide 1.0 0.5\r\n\r\n");
                    continue;
                }
                loop_pos = parsed;
                while (*arg && *arg != ' ' && *arg != '\t') arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
                if (*arg) {
                    if (!llmk_cfg_parse_f32(arg, &parsed)) {
                        Print(L"\r\nUsage: /mind_halt_decide [loop_pos] [threshold]\r\n");
                        Print(L"  Example: /mind_halt_decide 1.0 0.5\r\n\r\n");
                        continue;
                    }
                    threshold = parsed;
                }
            }
            llmk_mind_halting_decide(loop_pos, threshold);
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_sweep", 16) == 0) {
            const char *arg = prompt + 16;
            while (*arg == ' ' || *arg == '\t') arg++;
            float start = 0.0f;
            float end = 4.0f;
            float step = 0.5f;
            float threshold = 0.5f;
            if (*arg) {
                float parsed = 0.0f;
                if (!llmk_cfg_parse_f32(arg, &parsed)) {
                    Print(L"\r\nUsage: /mind_halt_sweep [start] [end] [step] [threshold]\r\n");
                    Print(L"  Example: /mind_halt_sweep 0.0 4.0 0.5 0.5\r\n\r\n");
                    continue;
                }
                start = parsed;
                while (*arg && *arg != ' ' && *arg != '\t') arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
                if (*arg) {
                    if (!llmk_cfg_parse_f32(arg, &parsed)) {
                        Print(L"\r\nUsage: /mind_halt_sweep [start] [end] [step] [threshold]\r\n");
                        Print(L"  Example: /mind_halt_sweep 0.0 4.0 0.5 0.5\r\n\r\n");
                        continue;
                    }
                    end = parsed;
                    while (*arg && *arg != ' ' && *arg != '\t') arg++;
                    while (*arg == ' ' || *arg == '\t') arg++;
                    if (*arg) {
                        if (!llmk_cfg_parse_f32(arg, &parsed)) {
                            Print(L"\r\nUsage: /mind_halt_sweep [start] [end] [step] [threshold]\r\n");
                            Print(L"  Example: /mind_halt_sweep 0.0 4.0 0.5 0.5\r\n\r\n");
                            continue;
                        }
                        step = parsed;
                        while (*arg && *arg != ' ' && *arg != '\t') arg++;
                        while (*arg == ' ' || *arg == '\t') arg++;
                        if (*arg) {
                            if (!llmk_cfg_parse_f32(arg, &parsed)) {
                                Print(L"\r\nUsage: /mind_halt_sweep [start] [end] [step] [threshold]\r\n");
                                Print(L"  Example: /mind_halt_sweep 0.0 4.0 0.5 0.5\r\n\r\n");
                                continue;
                            }
                            threshold = parsed;
                        }
                    }
                }
            }
            llmk_mind_halting_sweep(start, end, step, threshold);
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy", 17) == 0) {
            const char *arg = prompt + 17;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (!*arg) {
                Print(L"\r\n[MindHaltPolicy] enabled=%d threshold=%d.%03d\r\n\r\n",
                      g_mind_runtime_halt_enabled,
                      (int)g_mind_runtime_halt_threshold,
                      (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
                continue;
            }

            float parsed = 0.0f;
            int parsed_threshold = 0;
            if (llmk_cfg_parse_f32(arg, &parsed)) {
                parsed_threshold = 1;
                g_mind_runtime_halt_threshold = parsed;
                if (g_mind_runtime_halt_threshold < 0.0f) g_mind_runtime_halt_threshold = 0.0f;
                if (g_mind_runtime_halt_threshold > 1.0f) g_mind_runtime_halt_threshold = 1.0f;
                while (*arg && *arg != ' ' && *arg != '\t') arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
            }

            if (*arg) {
                if ((arg[0] == 'o' || arg[0] == 'O') && (arg[1] == 'n' || arg[1] == 'N') && !arg[2]) {
                    g_mind_runtime_halt_enabled = 1;
                } else if ((arg[0] == 'o' || arg[0] == 'O') && (arg[1] == 'f' || arg[1] == 'F') && (arg[2] == 'f' || arg[2] == 'F') && !arg[3]) {
                    g_mind_runtime_halt_enabled = 0;
                } else {
                    Print(L"\r\nUsage: /mind_halt_policy [threshold] [on|off]\r\n");
                    Print(L"  Examples: /mind_halt_policy\r\n");
                    Print(L"            /mind_halt_policy 0.65\r\n");
                    Print(L"            /mind_halt_policy 0.65 on\r\n");
                    Print(L"            /mind_halt_policy off\r\n\r\n");
                    continue;
                }
            } else if (!parsed_threshold) {
                if ((arg[0] == 'o' || arg[0] == 'O') && (arg[1] == 'n' || arg[1] == 'N') && !arg[2]) {
                    g_mind_runtime_halt_enabled = 1;
                } else if ((arg[0] == 'o' || arg[0] == 'O') && (arg[1] == 'f' || arg[1] == 'F') && (arg[2] == 'f' || arg[2] == 'F') && !arg[3]) {
                    g_mind_runtime_halt_enabled = 0;
                } else {
                    Print(L"\r\nUsage: /mind_halt_policy [threshold] [on|off]\r\n");
                    Print(L"  Examples: /mind_halt_policy\r\n");
                    Print(L"            /mind_halt_policy 0.65\r\n");
                    Print(L"            /mind_halt_policy 0.65 on\r\n");
                    Print(L"            /mind_halt_policy off\r\n\r\n");
                    continue;
                }
            }

            Print(L"\r\n[MindHaltPolicy] enabled=%d threshold=%d.%03d\r\n\r\n",
                  g_mind_runtime_halt_enabled,
                  (int)g_mind_runtime_halt_threshold,
                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_save", 22) == 0) {
            EFI_STATUS pst = llmk_mind_persist_halt_policy_best_effort();
            if (EFI_ERROR(pst)) {
                Print(L"\r\n[MindHaltPolicy] warning: repl.cfg persistence failed (%r)\r\n\r\n", pst);
            } else {
                Print(L"\r\n[MindHaltPolicy] repl.cfg updated enabled=%d threshold=%d.%03d\r\n\r\n",
                      g_mind_runtime_halt_enabled,
                      (int)g_mind_runtime_halt_threshold,
                      (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
            }
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_load", 22) == 0) {
            EFI_STATUS lst = llmk_mind_load_halt_policy_best_effort();
            if (EFI_ERROR(lst)) {
                Print(L"\r\n[MindHaltPolicy] warning: repl.cfg load failed (%r)\r\n\r\n", lst);
            } else {
                Print(L"\r\n[MindHaltPolicy] repl.cfg loaded enabled=%d threshold=%d.%03d\r\n\r\n",
                      g_mind_runtime_halt_enabled,
                      (int)g_mind_runtime_halt_threshold,
                      (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
            }
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_apply_saved", 29) == 0) {
            int changed_enabled = 0;
            int changed_threshold = 0;
            EFI_STATUS ast = llmk_mind_apply_saved_halt_policy_best_effort(&changed_enabled, &changed_threshold);
            if (EFI_ERROR(ast)) {
                Print(L"\r\n[MindHaltPolicy] warning: saved policy apply failed (%r)\r\n\r\n", ast);
            } else {
                llmk_mind_print_apply_command_result(LLMK_MIND_HALT_APPLY_SAVED, 1, changed_enabled, changed_threshold);
            }
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_apply_saved_if_needed", 39) == 0) {
            int was_needed = 0;
            int changed_enabled = 0;
            int changed_threshold = 0;
            EFI_STATUS ast = llmk_mind_apply_saved_halt_policy_if_needed_best_effort(&was_needed, &changed_enabled, &changed_threshold);
            if (EFI_ERROR(ast)) {
                Print(L"\r\n[MindHaltPolicy] warning: conditional saved policy apply failed (%r)\r\n\r\n", ast);
            } else if (!was_needed) {
                llmk_mind_print_apply_command_result(LLMK_MIND_HALT_APPLY_SAVED_IF_NEEDED, 0, changed_enabled, changed_threshold);
            } else {
                llmk_mind_print_apply_command_result(LLMK_MIND_HALT_APPLY_SAVED_IF_NEEDED, 1, changed_enabled, changed_threshold);
            }
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_sync", 22) == 0) {
            int was_needed = 0;
            int changed_enabled = 0;
            int changed_threshold = 0;
            EFI_STATUS ast = llmk_mind_apply_saved_halt_policy_if_needed_best_effort(&was_needed, &changed_enabled, &changed_threshold);
            if (EFI_ERROR(ast)) {
                Print(L"\r\n[MindHaltPolicy] warning: sync from repl.cfg failed (%r)\r\n\r\n", ast);
            } else if (!was_needed) {
                llmk_mind_print_apply_command_result(LLMK_MIND_HALT_APPLY_SYNC, 0, changed_enabled, changed_threshold);
            } else {
                llmk_mind_print_apply_command_result(LLMK_MIND_HALT_APPLY_SYNC, 1, changed_enabled, changed_threshold);
            }
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_sync_force", 28) == 0) {
            int changed_enabled = 0;
            int changed_threshold = 0;
            EFI_STATUS ast = llmk_mind_apply_saved_halt_policy_best_effort(&changed_enabled, &changed_threshold);
            if (EFI_ERROR(ast)) {
                Print(L"\r\n[MindHaltPolicy] warning: forced sync from repl.cfg failed (%r)\r\n\r\n", ast);
            } else {
                llmk_mind_print_apply_command_result(LLMK_MIND_HALT_APPLY_SYNC_FORCE, 1, changed_enabled, changed_threshold);
            }
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_audit", 23) == 0) {
            llmk_mind_print_halt_policy_audit();
            continue;
        }
        if (my_strncmp(prompt, "/mind_audit", 11) == 0) {
            llmk_mind_print_global_audit();
            continue;
        }
        if (my_strncmp(prompt, "/mind_doctor", 12) == 0) {
            llmk_mind_print_doctor();
            continue;
        }
        if (my_strncmp(prompt, "/mind_next", 10) == 0) {
            llmk_mind_print_next();
            continue;
        }
        if (my_strncmp(prompt, "/mind_snapshot", 14) == 0) {
            const char *arg = prompt + 14;
            int strict_mode = 0;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (*arg) {
                if (my_strcmp(arg, "strict") == 0 || my_strcmp(arg, "raw") == 0) {
                    strict_mode = 1;
                } else {
                    Print(L"\r\nUsage: /mind_snapshot [strict]\r\n");
                    Print(L"  strict/raw removes the decorative header and keeps fixed key=value output.\r\n\r\n");
                    continue;
                }
            }
            llmk_mind_print_snapshot(strict_mode);
            continue;
        }
        if (my_strncmp(prompt, "/mind_ready", 11) == 0) {
            llmk_mind_print_ready();
            continue;
        }
        if (my_strncmp(prompt, "/mind_bootstrap_v1", 18) == 0) {
            llmk_mind_bootstrap_v1();
            continue;
        }
        if (my_strncmp(prompt, "/mind_path_v1", 13) == 0) {
            llmk_mind_print_path_v1();
            continue;
        }
        if (my_strncmp(prompt, "/oo_sidecar_audit", 17) == 0) {
            llmk_mind_print_sidecar_audit();
            continue;
        }
        if (my_strncmp(prompt, "/attach_audit", 13) == 0) {
            llmk_mind_print_attach_audit();
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_reset", 23) == 0) {
            llmk_mind_reset_halt_policy_defaults();
            Print(L"\r\n[MindHaltPolicy] runtime reset to V1 defaults enabled=%d threshold=%d.%03d\r\n\r\n",
                  g_mind_runtime_halt_enabled,
                  (int)g_mind_runtime_halt_threshold,
                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
            continue;
        }
        if (my_strncmp(prompt, "/mind_halt_policy_diff", 22) == 0) {
            llmk_mind_print_halt_policy_diff();
            continue;
        }
        if (my_strncmp(prompt, "/core_load", 10) == 0) {
            const char *arg = prompt + 10;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (!arg[0]) {
                Print(L"\r\nUsage: /core_load <somamind_core>\r\n");
                Print(L"  Register the internal OO-SomaMind core target.\r\n");
                Print(L"  In V1, this aliases to the current /ssm_load <file.mamb> path.\r\n\r\n");
                continue;
            }
            llmk_mind_bind_core_backbone_v1(arg, 1);
            continue;
        }
        if (my_strncmp(prompt, "/oo_sidecar", 11) == 0) {
            const char *arg = prompt + 11;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (!arg[0]) {
                Print(L"\r\nUsage: /oo_sidecar <file.ooss>\r\n");
                Print(L"  Register an OO-SomaMind sidecar extension for future OOSS loading.\r\n");
                Print(L"  Current state: validates header and keeps the OOSS blob resident in memory.\r\n\r\n");
                continue;
            }
            CHAR16 path16[192];
            ascii_to_char16(path16, arg, (int)(sizeof(path16) / sizeof(path16[0])));
            LlmkOoSidecarHeader hdr;
            UINTN raw_len = 0;
            EFI_STATUS st = llmk_mind_register_sidecar_best_effort(arg, &hdr, &raw_len);
            if (EFI_ERROR(st)) {
                if (st == EFI_COMPROMISED_DATA) {
                    Print(L"\r\n[Mind] OO sidecar invalid: %s\r\n", path16);
                    Print(L"  Expected magic=OOSS and a 36-byte minimum header.\r\n\r\n");
                } else {
                    Print(L"\r\n[Mind] OO sidecar open failed: %s (%r)\r\n", path16, st);
                    Print(L"  Need a valid OOSS file with a readable header.\r\n\r\n");
                }
                continue;
            }

            Print(L"\r\n[Mind] OO sidecar registered: %s\r\n", path16);
            Print(L"  Role: future OO-SomaMind enriched extension\r\n");
            Print(L"  Header: version=%u d_model=%u n_layer=%u vocab=%u halt_in=%u\r\n",
                  hdr.version, hdr.d_model, hdr.n_layer, hdr.vocab_size, hdr.halting_head_d_input);
            Print(L"  Blob: %lu bytes retained in memory\r\n", (UINT64)raw_len);
            if (g_mind_halting_view.ready) {
                Print(L"  Halting hook: ready (MLP layout discovered in sidecar)\r\n");
            } else {
                Print(L"  Halting hook: not ready (layout/size mismatch)\r\n");
            }
            if (g_mind_runtime_state.core_active) {
                Print(L"  State: core backbone active, sidecar blob resident, semantic loader pending.\r\n");
            } else {
                Print(L"  State: sidecar blob resident, waiting for core backbone activation.\r\n");
            }
            Print(L"  Rule: OOSS complements MAMB; it does not replace the V1 runtime backbone.\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/oo_sidecar_unload", 18) == 0) {
            if (g_mind_runtime_state.sidecar_requested) {
                Print(L"\r\n[Mind] OO sidecar removed: ");
                llmk_print_ascii(g_mind_runtime_state.sidecar_path);
                Print(L"\r\n");
            } else {
                Print(L"\r\n[Mind] No OO sidecar registered.\r\n");
            }
            llmk_mind_clear_sidecar();
            Print(L"  Core backbone remains unchanged.\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/attach_load", 12) == 0) {
            const char *arg = prompt + 12;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (!arg[0]) {
                Print(L"\r\nUsage: /attach_load <model>\r\n");
                Print(L"  Validate and attach an optional external model without redefining the OO core.\r\n\r\n");
                continue;
            }
            llmk_mind_set_attach_request(arg, "attach-model");
            CHAR16 path16[192];
            ascii_to_char16(path16, arg, (int)(sizeof(path16) / sizeof(path16[0])));
            LlmkModelFormat attach_fmt = LLMK_MODEL_FMT_UNKNOWN;
            EFI_STATUS at = llmk_mind_activate_attach_best_effort(arg, &attach_fmt);
            Print(L"\r\n[Mind] Attach model registered: %s\r\n", path16);
            Print(L"  Role: optional external extension\r\n");
            if (EFI_ERROR(at)) {
                llmk_mind_set_attach_validation("validation-failed");
                Print(L"  State: registered but inactive; validation failed (%r).\r\n", at);
                Print(L"  Next: re-run /attach_load <file> with a readable GGUF or BIN export.\r\n");
            } else {
                llmk_mind_mark_attach_active(attach_fmt);
                Print(L"  State: active attached model backend validated.\r\n");
                Print(L"  Format: %a\r\n", llmk_model_format_ascii(attach_fmt));
            }
            Print(L"  Rule: attached models do not redefine OO-SomaMind identity.\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/attach_unload", 14) == 0) {
            if (g_mind_runtime_state.attach_requested) {
                Print(L"\r\n[Mind] Attach model detached: ");
                llmk_print_ascii(g_mind_runtime_state.attach_path);
                Print(L"\r\n");
            } else {
                Print(L"\r\n[Mind] No attach model registered.\r\n");
            }
            llmk_mind_clear_attach();
            Print(L"  Core continuity preserved.\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/ssm_info", 9) == 0) {
            Print(L"\r\n[SSM] Mamba bare-metal engine v0.1\r\n");
            Print(L"  Commands: /ssm_load <file>, /ssm_infer <text>, /ssm_reset\r\n");
            Print(L"  Mind cmds: /core_load <file>, /mind_diag, /mind_halt_probe [x], /mind_halt_decide [x] [t], /mind_halt_sweep [a] [b] [s] [t], /mind_halt_policy [t] [on|off], /mind_halt_policy_save, /mind_halt_policy_load, /mind_halt_policy_apply_saved, /mind_halt_policy_apply_saved_if_needed, /mind_halt_policy_sync, /mind_halt_policy_sync_force, /mind_halt_policy_audit, /mind_audit, /mind_doctor, /mind_next, /mind_snapshot, /mind_ready, /mind_bootstrap_v1, /mind_path_v1, /oo_sidecar <file>, /oo_sidecar_audit, /oo_sidecar_unload, /attach_load <file>, /attach_audit, /attach_policy, /attach_policy_audit, /attach_policy_diff, /attach_policy_sync, /attach_policy_sync_force, /attach_unload, /mind_halt_policy_reset, /mind_halt_policy_diff, /mind_status\r\n");
            Print(L"  Weight format: MAMB binary\r\n");
            Print(L"  Exporters: runtime export_mamba_baremetal.py | oo-model export_mamb_binary.py\r\n");
            Print(L"  Architecture: Mamba SSM, freestanding, O(1) memory per token\r\n");
            Print(L"  Status: engine files compiled, weight loader ready\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/ssm_load", 9) == 0) {
            const char *arg = prompt + 9;
            while (*arg == ' ' || *arg == '\t') arg++;
            if (!arg[0]) {
                Print(L"\r\nUsage: /ssm_load <weight_file.bin>\r\n");
                Print(L"  Load OOSI v2 int8 weights (generated by export_int8.py).\r\n");
                Print(L"  File must be on the EFI partition (same folder as BOOTX64.EFI).\r\n\r\n");
                continue;
            }

            // ── Convert ASCII path to CHAR16 for EFI ─────────────────────
            CHAR16 oosi_path16[192];
            ascii_to_char16(oosi_path16, arg, (int)(sizeof(oosi_path16) / sizeof(oosi_path16[0])));
            Print(L"\r\n[OOSI] Loading: %s ...\r\n", oosi_path16);

            // ── Open EFI file ─────────────────────────────────────────────
            EFI_FILE_HANDLE oosi_f = NULL;
            EFI_STATUS ost = uefi_call_wrapper(g_root->Open, 5, g_root, &oosi_f,
                                               oosi_path16, EFI_FILE_MODE_READ, 0ULL);
            if (EFI_ERROR(ost)) {
                Print(L"[OOSI] ERROR: cannot open %s (%r)\r\n\r\n", oosi_path16, ost);
                continue;
            }

            // ── Get file size ─────────────────────────────────────────────
            EFI_FILE_INFO *finfo = NULL;
            UINTN finfo_sz = SIZE_OF_EFI_FILE_INFO + 512;
            // Use AllocatePool (not arena) — finfo is small and transient.
            {
                EFI_STATUS fpst = uefi_call_wrapper(BS->AllocatePool, 3,
                                                    EfiLoaderData, finfo_sz, (void **)&finfo);
                if (EFI_ERROR(fpst) || !finfo) {
                    Print(L"[OOSI] ERROR: alloc for file info failed (%r)\r\n\r\n", fpst);
                    uefi_call_wrapper(oosi_f->Close, 1, oosi_f);
                    continue;
                }
            }
            ost = uefi_call_wrapper(oosi_f->GetInfo, 4, oosi_f, &gEfiFileInfoGuid,
                                    &finfo_sz, finfo);
            if (EFI_ERROR(ost)) {
                Print(L"[OOSI] ERROR: GetInfo failed (%r)\r\n\r\n", ost);
                uefi_call_wrapper(oosi_f->Close, 1, oosi_f);
                continue;
            }
            UINT64 oosi_size = finfo->FileSize;
            Print(L"[OOSI] File size: %d MB\r\n", (int)(oosi_size / (1024*1024)));

            // ── Peek magic (4 bytes) to detect v2 vs v3 before zones_init ──
            UINT32 peek_magic = 0;
            {
                UINT8 mb[4] = {0,0,0,0}; UINTN msz = 4;
                uefi_call_wrapper(oosi_f->Read, 3, oosi_f, &msz, mb);
                if (msz == 4)
                    peek_magic = (UINT32)mb[0] | ((UINT32)mb[1]<<8)
                               | ((UINT32)mb[2]<<16) | ((UINT32)mb[3]<<24);
                UINT64 rewind = 0;
                uefi_call_wrapper(oosi_f->SetPosition, 2, oosi_f, rewind);
            }

            // ── Ensure zones initialized for WEIGHTS arena ────────────────
            // If boot happened in no-model mode, zones were not set up yet.
            if (BS && g_zones.zone_b_base == 0) {
                Print(L"[OOSI] Initializing memory zones (no-model boot path)...\r\n");
                Print(L"[OOSI] Requesting %d MB contiguous (weights=%d MB kv=32 scratch=20 acts=4)\r\n",
                      (int)(oosi_size / (1024*1024)) + 60,
                      (int)(oosi_size / (1024*1024)));
                LlmkZonesConfig ssm_cfg;
                ssm_cfg.weights_bytes     = oosi_size;
                // v3: SSM recurrent state (20MB h_state + 5MB conv_buf + 7MB margin = 32MB)
                // v2: 16MB KV cache
                ssm_cfg.kv_bytes = (peek_magic == OOSI_V3_MAGIC)
                                   ? 32ULL * 1024ULL * 1024ULL
                                   : 16ULL * 1024ULL * 1024ULL;
                ssm_cfg.scratch_bytes     = 20ULL * 1024ULL * 1024ULL; // 20MB: 13MB vocab + 7MB work
                ssm_cfg.activations_bytes =  4ULL * 1024ULL * 1024ULL;
                ssm_cfg.zone_c_bytes      =  4ULL * 1024ULL * 1024ULL;
                ssm_cfg.total_bytes       = ssm_cfg.weights_bytes + ssm_cfg.kv_bytes
                                          + ssm_cfg.scratch_bytes + ssm_cfg.activations_bytes
                                          + ssm_cfg.zone_c_bytes;
                EFI_STATUS zst = llmk_zones_init(BS, &ssm_cfg, &g_zones);
                if (EFI_ERROR(zst)) {
                    Print(L"[OOSI] ERROR: zones_init failed (%r)\r\n\r\n", zst);
                    uefi_call_wrapper(oosi_f->Close, 1, oosi_f);
                    if (finfo) uefi_call_wrapper(BS->FreePool, 1, finfo);
                    continue;
                }
            }

            // ── Allocate in WEIGHTS arena (cold zone) ─────────────────────
            void *oosi_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_WEIGHTS,
                                              oosi_size, 64);
            if (!oosi_buf) {
                Print(L"[OOSI] ERROR: not enough WEIGHTS arena space (%d MB needed)\r\n\r\n",
                      (int)(oosi_size / (1024*1024)));
                uefi_call_wrapper(oosi_f->Close, 1, oosi_f);
                if (finfo) uefi_call_wrapper(BS->FreePool, 1, finfo);
                continue;
            }

            // ── Read file in 4MB chunks ───────────────────────────────────
            UINT8 *dst = (UINT8 *)oosi_buf;
            UINT64 remaining = oosi_size;
            int read_ok = 1;
            while (remaining > 0) {
                UINTN chunk = (remaining > 4*1024*1024) ? 4*1024*1024 : (UINTN)remaining;
                ost = uefi_call_wrapper(oosi_f->Read, 3, oosi_f, &chunk, dst);
                if (EFI_ERROR(ost) || chunk == 0) { read_ok = 0; break; }
                dst += chunk;
                remaining -= chunk;
            }
            uefi_call_wrapper(oosi_f->Close, 1, oosi_f);
            if (!read_ok) {
                Print(L"[OOSI] ERROR: file read failed\r\n\r\n");
                continue;
            }

            // ── Parse OOSI binary — detect v2 or v3 ─────────────────────
            // Check magic in first 4 bytes
            UINT32 oosi_magic = 0;
            if (oosi_size >= 4) {
                const UINT8 *hbytes = (const UINT8 *)oosi_buf;
                oosi_magic = (UINT32)hbytes[0]
                           | ((UINT32)hbytes[1] <<  8)
                           | ((UINT32)hbytes[2] << 16)
                           | ((UINT32)hbytes[3] << 24);
            }

            SsmStatus sst = SSM_OK;

            if (oosi_magic == OOSI_V3_MAGIC) {
                // ── OOSI v3: full standalone Mamba ───────────────────────
                Print(L"[OOSI] Detected v3 format (full standalone Mamba)\r\n");
                sst = oosi_v3_load(&g_oosi_v3_weights, oosi_buf, oosi_size);
                if (sst != SSM_OK) {
                    Print(L"[OOSI] ERROR: oosi_v3_load failed (code %d)\r\n\r\n", sst);
                    continue;
                }
                sst = oosi_v3_validate(&g_oosi_v3_weights);
                if (sst != SSM_OK) {
                    Print(L"[OOSI] ERROR: v3 validation failed (code %d)\r\n\r\n", sst);
                    continue;
                }
                Print(L"[OOSI-v3] d_model=%d n_layer=%d vocab=%d d_inner=%d\r\n",
                      g_oosi_v3_weights.d_model, g_oosi_v3_weights.n_layer,
                      g_oosi_v3_weights.vocab_size, g_oosi_v3_weights.d_inner);

                // Allocate v3 runtime buffers from warm arenas
                int V3D  = g_oosi_v3_weights.d_model;
                int V3Di = g_oosi_v3_weights.d_inner;
                int V3S  = g_oosi_v3_weights.d_state;
                int V3Dc = g_oosi_v3_weights.d_conv;
                int V3Dt = g_oosi_v3_weights.dt_rank;
                int V3N  = g_oosi_v3_weights.n_layer;
                int V3V  = g_oosi_v3_weights.vocab_size;
                int V3Hd = (int)g_oosi_v3_weights.halt_d_input;

                UINT64 scratch_b = (UINT64)(3*V3D + 4*V3Di + V3Dt + 2*V3S + 4) * sizeof(ssm_f32);
                UINT64 h_state_b = (UINT64)V3N * V3Di * V3S * sizeof(ssm_f32);
                UINT64 conv_b    = (UINT64)V3N * V3Di * V3Dc * sizeof(ssm_f32);
                UINT64 conv_pb   = (UINT64)V3N * sizeof(int);

                g_v3_scratch  = llmk_arena_alloc(&g_zones, LLMK_ARENA_ACTIVATIONS, scratch_b, 64);
                g_v3_logits   = llmk_arena_alloc(&g_zones, LLMK_ARENA_ACTIVATIONS,
                                                  (UINT64)V3V * sizeof(ssm_f32), 16);
                g_v3_h_state  = llmk_arena_alloc(&g_zones, LLMK_ARENA_KV_CACHE, h_state_b, 64);
                g_v3_conv_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_KV_CACHE, conv_b, 64);
                g_v3_conv_pos = llmk_arena_alloc(&g_zones, LLMK_ARENA_ACTIVATIONS, conv_pb, 4);
                g_v3_halt_h1  = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, 512*sizeof(ssm_f32), 16);
                g_v3_halt_h2  = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH, 64*sizeof(ssm_f32), 16);
                g_v3_halt_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                                  (UINT64)(V3Hd + 1) * sizeof(ssm_f32), 16);
                // SomaMind Dual Core work buffer (one extra logit copy = vocab_size floats)
                g_soma_dual_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                                    (UINT64)V3V * sizeof(ssm_f32), 16);
                if (g_soma_dual_buf) {
                    soma_dual_init(&g_soma_dual, g_soma_dual_buf, V3V);
                    Print(L"[SomaMind] Dual Core initialized (vocab=%d)\r\n", V3V);
                }
                // SomaMind Swarm (reuses dual_buf — same vocab_size float slice)
                if (g_soma_dual_buf) {
                    soma_swarm_init(&g_soma_swarm, &g_soma_dna, g_soma_dual_buf, V3V);
                    Print(L"[SomaMind] Swarm initialized (%d agents, vocab=%d)\r\n",
                          SOMA_SWARM_AGENTS, V3V);
                }

                if (!g_v3_scratch || !g_v3_logits || !g_v3_h_state ||
                    !g_v3_conv_buf || !g_v3_conv_pos || !g_v3_halt_h1 ||
                    !g_v3_halt_h2 || !g_v3_halt_buf) {
                    Print(L"[OOSI-v3] ERROR: buffer alloc failed (out of arena space)\r\n\r\n");
                    Print(L"  Need ~%d MB KV-cache for h_state+conv_buf\r\n",
                          (int)((h_state_b + conv_b) / (1024*1024)));
                    continue;
                }

                sst = oosi_v3_gen_ctx_init(
                    &g_oosi_v3_ctx, &g_oosi_v3_weights,
                    g_v3_scratch, g_v3_logits,
                    g_v3_h_state, g_v3_conv_buf, g_v3_conv_pos,
                    g_v3_halt_h1, g_v3_halt_h2, g_v3_halt_buf,
                    0.80f,   // halt_threshold
                    0.7f,    // temperature
                    0.90f,   // top_p
                    0xCAFEBABEu,
                    (g_max_new_tokens > 0) ? g_max_new_tokens : 128
                );
                if (sst != SSM_OK) {
                    Print(L"[OOSI-v3] ERROR: gen_ctx_init failed (code %d)\r\n\r\n", sst);
                    continue;
                }
                g_oosi_v3_valid = 1;
                Print(L"[OOSI-v3] OK: full SSM inference ready. Use /ssm_infer <text>\r\n");
                // Phase H: record which model is loaded
                if (g_soma_memory.enabled)
                    soma_memory_set_model(&g_soma_memory, arg);

                // Precompute -exp(A_log) — skip if baked into binary (NEGA trailer)
                if (g_oosi_v3_ctx.neg_exp_A) {
                    Print(L"[OOSI-v3] Using baked neg_exp_A from binary (zero runtime cost)\r\n");
                } else {
                    UINT64 neg_exp_b = (UINT64)V3N * V3Di * V3S * sizeof(ssm_f32);
                    ssm_f32 *neg_exp_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_KV_CACHE, neg_exp_b, 64);
                    if (neg_exp_buf) {
                        oosi_v3_precompute_neg_exp_A(&g_oosi_v3_ctx, neg_exp_buf);
                        Print(L"[OOSI-v3] Precomputed exp(A_log): %d MB (runtime)\r\n",
                              (int)(neg_exp_b / (1024*1024)));
                    }
                }

                // ── Best-effort: load tokenizer from EFI volume ──
                // Try gpt_neox_tokenizer.bin first (50282 vocab), fall back to tokenizer.bin (32K vocab)
                {
                    EFI_FILE_HANDLE tok_f = NULL;
                    EFI_STATUS tok_st = uefi_call_wrapper(
                        g_root->Open, 5, g_root, &tok_f,
                        L"gpt_neox_tokenizer.bin",
                        EFI_FILE_MODE_READ, 0);
                    if (EFI_ERROR(tok_st) || !tok_f) {
                        tok_f = NULL;
                        tok_st = uefi_call_wrapper(
                            g_root->Open, 5, g_root, &tok_f,
                            L"tokenizer.bin",
                            EFI_FILE_MODE_READ, 0);
                    }
                    if (!EFI_ERROR(tok_st) && tok_f) {
                        EFI_FILE_INFO *tfi = NULL;
                        UINTN tfi_sz = sizeof(EFI_FILE_INFO) + 256;
                        uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, tfi_sz, (void**)&tfi);
                        if (tfi) {
                            tok_st = uefi_call_wrapper(tok_f->GetInfo, 4, tok_f,
                                         &gEfiFileInfoGuid, &tfi_sz, tfi);
                            if (!EFI_ERROR(tok_st)) {
                                UINT64 tok_sz = tfi->FileSize;
                                void *tok_buf = llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                                                  tok_sz, 16);
                                if (tok_buf) {
                                    UINTN rd = (UINTN)tok_sz;
                                    tok_st = uefi_call_wrapper(tok_f->Read, 3, tok_f, &rd, tok_buf);
                                    if (!EFI_ERROR(tok_st) && rd == (UINTN)tok_sz) {
                                        // Allocate vocab buffer in SCRATCH arena (~13MB for 50280 entries)
                                        int voc_cap = V3V > 0 ? V3V : 50282;
                                        UINT64 voc_bytes = (UINT64)voc_cap * sizeof(BpeVocabEntry);
                                        BpeVocabEntry *vbuf = (BpeVocabEntry *)llmk_arena_alloc(
                                            &g_zones, LLMK_ARENA_SCRATCH, voc_bytes, 16);
                                        if (vbuf) {
                                            SsmStatus ts = llmk_oo_infer_tokenizer_init(
                                                vbuf, voc_cap, tok_buf, tok_sz);
                                            if (ts == SSM_OK) {
                                                Print(L"[OOSI-v3] Tokenizer loaded (%d KB)\r\n",
                                                      (int)(tok_sz / 1024));
                                            } else {
                                                Print(L"[OOSI-v3] WARN: tokenizer parse failed (%d)\r\n", ts);
                                            }
                                        }
                                    }
                                }
                            }
                            uefi_call_wrapper(BS->FreePool, 1, tfi);
                        }
                        uefi_call_wrapper(tok_f->Close, 1, tok_f);
                    } else {
                        Print(L"[OOSI-v3] WARN: no tokenizer found (tried gpt_neox_tokenizer.bin, tokenizer.bin)\r\n");
                    }
                }
                // Phase W: allocate spec decode buf (vocab_size floats for softmax)
                {
                    UINT64 spec_bytes = (UINT64)(V3V > 0 ? V3V : 50282) * sizeof(ssm_f32);
                    g_soma_spec_buf = (ssm_f32 *)llmk_arena_alloc(
                        &g_zones, LLMK_ARENA_SCRATCH, spec_bytes, 16);
                    if (g_soma_spec_buf) {
                        soma_spec_init(&g_soma_spec, V3V > 0 ? V3V : 50282,
                                       g_soma_spec_buf);
                        Print(L"[SpecDecode] Ready (vocab=%d threshold=80%%)\r\n", g_soma_spec.vocab_size);
                    }
                }
                // Phase Y: record peer slot published on each inference turn
                g_soma_swarm_net.my_peer_id = 0;
                Print(L"\r\n");
                continue;
            }
            // ── OOSI v2: quantized x_proj/dt_proj subset ──────────────────
            sst = oosi_load(&g_oosi_weights, oosi_buf, oosi_size);
            if (sst != SSM_OK) {
                Print(L"[OOSI] ERROR: oosi_load failed (code %d)\r\n\r\n", sst);
                Print(L"  Is this a valid OOSI v2 binary (export_int8.py output)?\r\n\r\n");
                continue;
            }
            g_oosi_weights_valid = 1;
            Print(L"[OOSI] Weights parsed OK (d_model=%d n_layer=%d vocab=%d)\r\n",
                  (int)g_oosi_weights.header.d_model,
                  (int)g_oosi_weights.header.n_layer,
                  (int)g_oosi_weights.header.vocab_size);

            // ── Allocate warm scratch buffers ─────────────────────────────
            g_oosi_x_buf   = (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_ACTIVATIONS,
                                 G_OOSI_D_MODEL * sizeof(ssm_f32), 16);
            g_oosi_x_out   = (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_ACTIVATIONS,
                                 G_OOSI_D_MODEL * sizeof(ssm_f32), 16);
            g_oosi_scratch = (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                 8 * G_OOSI_D_INNER * sizeof(ssm_f32), 16);
            g_oosi_logits  = (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_ACTIVATIONS,
                                 G_OOSI_VOCAB * sizeof(ssm_f32), 16);
            g_oosi_halt_buf= (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                 (G_OOSI_D_MODEL + 1) * sizeof(ssm_f32), 16);
            g_oosi_halt_h1 = (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                 512 * sizeof(ssm_f32), 16);
            g_oosi_halt_h2 = (ssm_f32 *)llmk_arena_alloc(&g_zones, LLMK_ARENA_SCRATCH,
                                 64 * sizeof(ssm_f32), 16);

            if (!g_oosi_x_buf || !g_oosi_x_out || !g_oosi_scratch ||
                !g_oosi_logits || !g_oosi_halt_buf || !g_oosi_halt_h1 || !g_oosi_halt_h2) {
                Print(L"[OOSI] ERROR: scratch buffer allocation failed\r\n\r\n");
                g_oosi_weights_valid = 0;
                continue;
            }

            // ── Init OO inference engine ──────────────────────────────────
            // Note: MAMB float32 weights not loaded yet — passing NULL is OK,
            // the engine will use OOSI int8 projections only (hybrid mode).
            sst = llmk_oo_infer_init(
                &g_oosi_weights, NULL,  // oosi + mamb (mamb=NULL = standalone embedding mode)
                g_oosi_x_buf, g_oosi_x_out, g_oosi_scratch,
                g_oosi_logits, g_oosi_halt_buf, g_oosi_halt_h1, g_oosi_halt_h2,
                0.85f,  // halt_threshold (higher = more output)
                0.5f,   // temperature (lower = less random in embedding mode)
                0.85f,  // top_p
                0xDEADBEEFu,
                (g_max_new_tokens > 12) ? 12 : g_max_new_tokens  // cap at 12 in standalone
            );
            if (sst != SSM_OK) {
                Print(L"[OOSI] WARNING: llmk_oo_infer_init failed (code %d) - inference disabled\r\n\r\n", sst);
                g_oosi_weights_valid = 0;
                continue;
            }

            // ── Load GPT-NeoX tokenizer (gpt_neox_tokenizer.bin) ──────────
            {
                void *tok_raw = NULL; UINTN tok_len = 0;
                EFI_STATUS tst = llmk_read_entire_file_best_effort(
                        L"gpt_neox_tokenizer.bin", &tok_raw, &tok_len);
                if (EFI_ERROR(tst) || !tok_raw || tok_len == 0) {
                    if (tok_raw) uefi_call_wrapper(BS->FreePool, 1, tok_raw);
                    tok_raw = NULL; tok_len = 0;
                    tst = llmk_read_entire_file_best_effort(
                            L"tokenizer.bin", &tok_raw, &tok_len);
                }
                if (!EFI_ERROR(tst) && tok_raw && tok_len > 0) {
                    // BpeVocabEntry[vocab_size] buffer — allocate from SCRATCH arena
                    int vocab_sz = (int)g_oosi_weights.header.vocab_size;
                    if (vocab_sz <= 0) vocab_sz = 50280; // GPT-NeoX default
                    UINTN vocab_buf_sz = (UINTN)vocab_sz * sizeof(BpeVocabEntry);
                    BpeVocabEntry *vbuf = llmk_arena_alloc(&g_zones,
                            LLMK_ARENA_SCRATCH, vocab_buf_sz, 8);
                    if (vbuf) {
                        SsmStatus tss = llmk_oo_infer_tokenizer_init(
                                vbuf, vocab_sz, tok_raw, (uint64_t)tok_len);
                        if (tss == SSM_OK)
                            Print(L"[OOSI] Tokenizer loaded (vocab=%d, %d bytes)\r\n",
                                  vocab_sz, (int)tok_len);
                        else
                            Print(L"[OOSI] WARNING: tokenizer init failed (code %d) - char-level fallback\r\n", tss);
                    } else {
                        Print(L"[OOSI] WARNING: no SCRATCH arena space for vocab buf - char-level fallback\r\n");
                    }
                    uefi_call_wrapper(BS->FreePool, 1, tok_raw);
                } else {
                    Print(L"[OOSI] WARNING: tokenizer file not found - char-level fallback\r\n");
                }
            }

            llmk_mind_mark_core_active(arg, "oosi-v2");
            Print(L"[OOSI] OK: inference engine ready. Use /ssm_infer <text>\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/ssm_infer", 10) == 0) {
            const char *text = prompt + 10;
            while (*text == ' ' || *text == '\t') text++;
            if (!text[0]) {
                Print(L"\r\nUsage: /ssm_infer <text>\r\n");
                Print(L"  Run Mamba OOSI inference. Load with /ssm_load first.\r\n\r\n");
                continue;
            }
            if (!g_oosi_v3_valid && (!g_oosi_weights_valid || !llmk_oo_infer_is_ready())) {
                Print(L"\r\n[OOSI] No model loaded. Use /ssm_load <file.bin> first.\r\n\r\n");
                continue;
            }

            // ── SomaMind Router: consult before inference ────────────────
            // Store route data for post-inference SMB write
            uint32_t soma_input_hash = 0;
            SomaDomain soma_domain_used = SOMA_DOMAIN_CHAT;
            SomaRoute soma_route_used = SOMA_ROUTE_EXTERNAL;
            SomaCoreUsed soma_core_used = SOMA_CORE_SOLAR;
            float soma_first_conf = 0.0f;
            int attach_route_advisory = 0;
            LlmkAttachRoutePolicyState attach_policy_state;
            SetMem(&attach_policy_state, sizeof(attach_policy_state), 0);
            if (g_soma_initialized) {
                g_soma_router.soma_model_ready = (g_mind_runtime_state.core_active || g_oosi_v3_valid ||
                                                  (g_oosi_weights_valid && llmk_oo_infer_is_ready())) ? 1 : 0;
                g_soma_router.external_model_ready = g_mind_runtime_state.attach_active ? 1 : 0;
                int tlen = 0;
                { const char *p = text; while (*p) { tlen++; p++; } }
                soma_input_hash = soma_smb_hash(text, tlen);
                SomaRouteResult rr = soma_route(&g_soma_router, text, tlen);
                soma_domain_used = rr.domain;
                soma_route_used  = rr.route;
                attach_route_advisory = (g_mind_runtime_state.attach_active &&
                                         (rr.route == SOMA_ROUTE_EXTERNAL || rr.route == SOMA_ROUTE_DUAL)) ? 1 : 0;

                // Phase S: pheromion trail — emit domain + route signals
                pheromion_touch(&g_pheromion, 100u + (uint32_t)rr.domain);
                pheromion_touch(&g_pheromion, 200u + (uint32_t)rr.route);

                // REFLEX: instant response, no inference needed
                if (rr.route == SOMA_ROUTE_REFLEX && rr.reflex_response) {
                    Print(L"\r\n[SomaMind:REFLEX] ");
                    for (int ri = 0; ri < rr.reflex_response_len; ri++)
                        Print(L"%c", (CHAR16)rr.reflex_response[ri]);
                    Print(L"\r\n\r\n");
                    g_soma_dna.total_interactions++;
                    g_soma_dna.successful_reflexes++;
                    continue;
                }

                // Apply DNA-driven sampling for v3 inference
                if (g_oosi_v3_valid) {
                    float bias = g_soma_dna.cognition_bias;
                    switch (rr.domain) {
                        case SOMA_DOMAIN_SYSTEM:
                        case SOMA_DOMAIN_POLICY:
                        case SOMA_DOMAIN_CODE:
                        case SOMA_DOMAIN_MATH:
                            // Logic-heavy → Solar-biased
                            g_oosi_v3_ctx.temperature = g_soma_dna.temperature_solar
                                + bias * 0.15f * (g_soma_dna.temperature_lunar - g_soma_dna.temperature_solar);
                            g_oosi_v3_ctx.top_p = g_soma_dna.top_p_solar
                                + bias * 0.15f * (g_soma_dna.top_p_lunar - g_soma_dna.top_p_solar);
                            break;
                        case SOMA_DOMAIN_CREATIVE:
                            // Creative → Lunar-biased
                            g_oosi_v3_ctx.temperature = g_soma_dna.temperature_lunar
                                - (1.0f - bias) * 0.15f * (g_soma_dna.temperature_lunar - g_soma_dna.temperature_solar);
                            g_oosi_v3_ctx.top_p = g_soma_dna.top_p_lunar
                                - (1.0f - bias) * 0.15f * (g_soma_dna.top_p_lunar - g_soma_dna.top_p_solar);
                            break;
                        default:
                            // CHAT/UNKNOWN: balanced blend
                            g_oosi_v3_ctx.temperature = g_soma_dna.temperature_solar
                                + bias * (g_soma_dna.temperature_lunar - g_soma_dna.temperature_solar);
                            g_oosi_v3_ctx.top_p = g_soma_dna.top_p_solar
                                + bias * (g_soma_dna.top_p_lunar - g_soma_dna.top_p_solar);
                            break;
                    }

                    if (g_boot_verbose) {
                        const CHAR16 *dname = L"?";
                        switch (rr.domain) {
                            case SOMA_DOMAIN_SYSTEM:   dname = L"SYS"; break;
                            case SOMA_DOMAIN_POLICY:   dname = L"POL"; break;
                            case SOMA_DOMAIN_CHAT:     dname = L"CHT"; break;
                            case SOMA_DOMAIN_CODE:     dname = L"COD"; break;
                            case SOMA_DOMAIN_MATH:     dname = L"MTH"; break;
                            case SOMA_DOMAIN_CREATIVE: dname = L"CRE"; break;
                            default: break;
                        }
                        Print(L"[SomaMind] domain=%s temp=%d/1000 top_p=%d/1000\r\n",
                              dname,
                              (int)(g_oosi_v3_ctx.temperature * 1000),
                              (int)(g_oosi_v3_ctx.top_p * 1000));
                    }
                }
                g_soma_dna.total_interactions++;
            }
            // ── end SomaMind Router ──────────────────────────────────────

            // ── Phase J: Cortex pre-routing ─────────────────────────────────
            // Run small oo-model cortex to classify domain + safety BEFORE Mamba-2.8B.
            if (g_soma_cortex.loaded && g_soma_cortex.enabled) {
                SomaCortexResult cr = soma_cortex_run(&g_soma_cortex, text);
                if (cr.valid) {
                    // Safety gate: warn on low safety score
                    if (cr.safety_flagged) {
                        Print(L"[Cortex] WARNING: safety_score=%d (threshold=%d) — continuing\r\n",
                              cr.safety_score, SOMA_CORTEX_SAFE_THRESHOLD);
                    }
                    if (g_boot_verbose) {
                        Print(L"[Cortex] domain=%d conf=%d%% safety=%d%s\r\n",
                              cr.domain, cr.domain_conf, cr.safety_score,
                              cr.safety_flagged ? L" [FLAG]" : L"");
                    }
                }
            }

            // ── SomaMind Reflex: symbolic math + logic + memory pre-solve ───
            // Builds augmented prompt: [MEM:...]\n[MATH:...]\n[LOGIC:...]\n<original>
            char reflex_prompt[512 + SOMA_REFLEX_INJECT_MAX + SOMA_LOGIC_INJECT_MAX + SOMA_MEM_INJECT_MAX];
            char attach_prompt[768 + SOMA_REFLEX_INJECT_MAX + SOMA_LOGIC_INJECT_MAX + SOMA_MEM_INJECT_MAX];
            const char *infer_text = text;
            if (g_soma_initialized && (g_soma_reflex.enabled || g_soma_logic.enabled || g_soma_memory.enabled)) {
                int ip = 0;
                // Memory reflex (Phase H) — injects historical context first
                if (g_soma_memory.enabled) {
                    SomaMemResult mr = soma_memory_scan(&g_soma_memory, text);
                    if (mr.triggered) {
                        for (int mi = 0; mi < mr.injection_len && ip < (int)sizeof(reflex_prompt) - 2; mi++)
                            reflex_prompt[ip++] = mr.injection[mi];
                        if (g_boot_verbose) {
                            Print(L"[Reflex/Memory] ");
                            for (int mi = 0; mr.injection[mi] && mi < 70; mi++)
                                Print(L"%c", (CHAR16)(unsigned char)mr.injection[mi]);
                            Print(L"\r\n");
                        }
                    }
                }
                // Math reflex
                if (g_soma_reflex.enabled) {
                    SomaReflexResult rf = soma_reflex_scan(&g_soma_reflex, text);
                    if (rf.triggered) {
                        for (int ri = 0; ri < rf.injection_len && ip < (int)sizeof(reflex_prompt) - 2; ri++)
                            reflex_prompt[ip++] = rf.injection[ri];
                        if (g_boot_verbose) {
                            Print(L"[Reflex/Math] ");
                            for (int ri = 0; rf.injection[ri] && ri < 60; ri++)
                                Print(L"%c", (CHAR16)(unsigned char)rf.injection[ri]);
                            Print(L"\r\n");
                        }
                    }
                }
                // Logic reflex
                if (g_soma_logic.enabled) {
                    SomaLogicResult lg = soma_logic_scan(&g_soma_logic, text);
                    if (lg.triggered) {
                        for (int li = 0; li < lg.injection_len && ip < (int)sizeof(reflex_prompt) - 2; li++)
                            reflex_prompt[ip++] = lg.injection[li];
                        if (g_boot_verbose) {
                            Print(L"[Reflex/Logic] ");
                            for (int li = 0; lg.injection[li] && li < 70; li++)
                                Print(L"%c", (CHAR16)(unsigned char)lg.injection[li]);
                            Print(L"\r\n");
                        }
                    }
                }
                if (ip > 0) {
                    for (int ti = 0; text[ti] && ip < (int)sizeof(reflex_prompt) - 1; ti++)
                        reflex_prompt[ip++] = text[ti];
                    reflex_prompt[ip] = 0;
                    infer_text = reflex_prompt;
                }
            }

            if (attach_route_advisory) {
                int ap = 0;
                const char *prefix = "[ATTACH advisory role=secondary";
                for (int i = 0; prefix[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                    attach_prompt[ap++] = prefix[i];

                if (g_mind_runtime_state.attach_format[0]) {
                    const char *fmt_prefix = " format=";
                    for (int i = 0; fmt_prefix[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                        attach_prompt[ap++] = fmt_prefix[i];
                    for (int i = 0; g_mind_runtime_state.attach_format[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                        attach_prompt[ap++] = g_mind_runtime_state.attach_format[i];
                }

                if (g_mind_runtime_state.attach_kind[0]) {
                    const char *kind_prefix = " kind=";
                    for (int i = 0; kind_prefix[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                        attach_prompt[ap++] = kind_prefix[i];
                    for (int i = 0; g_mind_runtime_state.attach_kind[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                        attach_prompt[ap++] = g_mind_runtime_state.attach_kind[i];
                }

                if (g_mind_runtime_state.attach_last_validation[0]) {
                    const char *val_prefix = " validation=";
                    for (int i = 0; val_prefix[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                        attach_prompt[ap++] = val_prefix[i];
                    for (int i = 0; g_mind_runtime_state.attach_last_validation[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                        attach_prompt[ap++] = g_mind_runtime_state.attach_last_validation[i];
                }

                if (ap < (int)sizeof(attach_prompt) - 2) attach_prompt[ap++] = ']';
                if (ap < (int)sizeof(attach_prompt) - 2) attach_prompt[ap++] = '\n';
                for (int i = 0; infer_text[i] && ap < (int)sizeof(attach_prompt) - 1; i++)
                    attach_prompt[ap++] = infer_text[i];
                attach_prompt[ap] = 0;
                infer_text = attach_prompt;
                Print(L"[AttachRoute] advisory-secondary route=%s format=%a\r\n",
                      soma_route_used == SOMA_ROUTE_DUAL ? L"DUAL" : L"EXTERNAL",
                      llmk_model_format_ascii(
                          llmk_ascii_streq(g_mind_runtime_state.attach_format, "gguf") ? LLMK_MODEL_FMT_GGUF :
                          llmk_ascii_streq(g_mind_runtime_state.attach_format, "bin") ? LLMK_MODEL_FMT_BIN :
                          LLMK_MODEL_FMT_UNKNOWN));
            }

            if (g_oosi_v3_valid && attach_route_advisory) {
                llmk_attach_route_policy_begin(soma_route_used, &attach_policy_state);
                if (attach_policy_state.applied) {
                    Print(L"[AttachRoute] policy route=%s temp=%d/1000 top_p=%d/1000 rep=%d/1000 max_tokens=%d\r\n",
                          llmk_soma_route_name_wide(soma_route_used),
                          (int)(g_oosi_v3_ctx.temperature * 1000.0f + 0.5f),
                          (int)(g_oosi_v3_ctx.top_p * 1000.0f + 0.5f),
                          (int)(g_oosi_v3_ctx.repetition_penalty * 1000.0f + 0.5f),
                          g_oosi_v3_ctx.max_tokens);
                }
            }

            Print(L"\r\n[OOSI] Input: ");
            llmk_print_ascii(infer_text);
            Print(L"\r\n[OOSI] Thinking...\r\n");

            // ── OOSI v3: full standalone Mamba SSM inference ─────────────
            if (g_oosi_v3_valid) {
                // Tokenize using v2 tokenizer (shared BPE vocab)
                int prompt_tokens[512];
                int prompt_len = llmk_oo_infer_tokenize(infer_text, prompt_tokens, 512);
                if (prompt_len <= 0) {
                    // Fallback: encode raw bytes as token IDs
                    prompt_len = 0;
                    for (int i = 0; text[i] && prompt_len < 512; i++)
                        prompt_tokens[prompt_len++] = (unsigned char)text[i] + 3;
                }

                Print(L"[OOSI-v3] Prompt tokens: %d — generating (SSM recurrent)...\r\n",
                      prompt_len);
                if (g_boot_verbose) {
                    Print(L"[OOSI-v3] tok_ids:");
                    for (int di = 0; di < prompt_len && di < 5; di++)
                        Print(L" %d", prompt_tokens[di]);
                    if (prompt_len > 5) Print(L" ...");
                    Print(L"\r\n");
                }

                // Simple blocking generate (tokens decoded inline)
                int out_ids[256];
                int n_out = 0;
                UINT64 gen_start_tsc = __rdtsc();

                oosi_v3_gen_ctx_reset(&g_oosi_v3_ctx);
                // Feed prompt (all except last — last token fed in generate loop)
                for (int pi = 0; pi < prompt_len - 1; pi++)
                    oosi_v3_forward_one(&g_oosi_v3_ctx, prompt_tokens[pi]);
                g_oosi_v3_ctx.tokens_generated = 0;

                // Generate — first call uses last prompt token
                int last = (prompt_len > 0) ? prompt_tokens[prompt_len - 1] : 0;
                Print(L"[OOSI-v3] ");
                while (n_out < g_oosi_v3_ctx.max_tokens && n_out < 256) {
                    OosiV3HaltResult r = oosi_v3_forward_one(&g_oosi_v3_ctx, last);

                    // SomaMind Dual Core: override token if enabled + buffer ready
                    if (g_soma_dual_enabled && g_soma_dual_buf && g_soma_initialized) {
                        SomaDualResult dr = soma_dual_sample(
                            &g_soma_dual,
                            g_oosi_v3_ctx.logits,
                            g_soma_dual.vocab_size,
                            &g_soma_dna,
                            &g_oosi_v3_ctx.rng_state);
                        r.token = dr.selected_token;
                        // Capture for SMB (first token only)
                        if (n_out == 0) {
                            soma_first_conf = dr.confidence;
                            soma_core_used  = dr.core_used;
                        }
                        if (g_boot_verbose && n_out == 0) {
                            const CHAR16 *core = (dr.core_used == SOMA_CORE_SOLAR) ? L"Solar"
                                               : (dr.core_used == SOMA_CORE_LUNAR) ? L"Lunar"
                                               : L"Fused";
                            int conf_pct = (int)(dr.confidence * 100.0f);
                            Print(L"[Dual:%s conf=%d%%]\r\n[OOSI-v3] ", core, conf_pct);
                        }
                        // Swarm override (if enabled, runs after dual core)
                        if (g_soma_swarm.enabled && g_soma_swarm.ready) {
                            SomaSwarmResult sr = soma_swarm_vote(
                                &g_soma_swarm,
                                g_oosi_v3_ctx.logits,
                                g_soma_dual.vocab_size);
                            g_soma_swarm_last = sr;
                            r.token = sr.selected_token;
                            if (n_out == 0) {
                                // Average agent confidences
                                float avg_conf = 0.0f;
                                for (int _ai = 0; _ai < SOMA_SWARM_AGENTS; _ai++)
                                    avg_conf += sr.agent_confidence[_ai];
                                avg_conf /= (float)SOMA_SWARM_AGENTS;
                                soma_first_conf = avg_conf;
                            }
                            if (g_boot_verbose && n_out == 0) {
                                Print(L"[Swarm:mode=%d spread=%d conf=%d%%]\r\n[OOSI-v3] ",
                                      (int)sr.mode_used,
                                      sr.vote_spread,
                                      (int)(sr.agent_confidence[sr.winning_agent] * 100.0f));
                            }
                        }
                        // ── Phase V: Multi-Reality Selection (inside dual scope, dr valid) ──
                        if (g_multireal_enabled) {
                            int vsz = g_soma_dual.vocab_size > 0 ? g_soma_dual.vocab_size : 50282;
                            int argmax_tok = 0;
                            ssm_f32 best_raw = g_oosi_v3_ctx.logits[0];
                            for (int _vi = 1; _vi < vsz; _vi++) {
                                if (g_oosi_v3_ctx.logits[_vi] > best_raw) {
                                    best_raw = g_oosi_v3_ctx.logits[_vi]; argmax_tok = _vi;
                                }
                            }
                            ssm_f32 solar_raw = g_oosi_v3_ctx.logits[dr.solar_token];
                            ssm_f32 lunar_raw = g_oosi_v3_ctx.logits[dr.lunar_token];
                            int winner = 2; int win_tok = argmax_tok; ssm_f32 win_raw = best_raw;
                            if (solar_raw > win_raw) { win_raw = solar_raw; win_tok = dr.solar_token; winner = 0; }
                            if (lunar_raw > win_raw) { win_tok = dr.lunar_token; winner = 1; }
                            r.token = win_tok;
                            g_multireal_stats.total_tokens++;
                            if (winner == 0)      g_multireal_stats.solar_wins++;
                            else if (winner == 1) g_multireal_stats.lunar_wins++;
                            else                  g_multireal_stats.argmax_wins++;
                            (void)win_raw;
                        }
                    } else if (n_out == 0) {
                        // Standard path: capture confidence from logits
                        soma_first_conf = soma_dual_confidence(g_oosi_v3_ctx.logits,
                                                               g_soma_dual.vocab_size > 0
                                                               ? g_soma_dual.vocab_size : 50282);
                    }
                    // ── Phase W: Speculative Decoding verify step ────────────────
                    if (g_soma_spec.enabled && g_soma_spec_buf && n_out == 0) {
                        int draft_tok = 0; float draft_p = 0.0f;
                        soma_spec_draft(&g_soma_spec, g_oosi_v3_ctx.logits,
                                        1, &draft_tok, &draft_p);
                        int corr = r.token;
                        int ok = soma_spec_verify_one(&g_soma_spec,
                                                      g_oosi_v3_ctx.logits,
                                                      draft_tok, draft_p,
                                                      &g_oosi_v3_ctx.rng_state, &corr);
                        if (!ok) r.token = corr;
                        (void)ok;
                    }
                    // ── Phase Y: Swarm Net — publish local vote + apply consensus ─
                    if (g_soma_swarm_net.enabled && g_soma_swarm_net.initialized) {
                        int local_tok = r.token;
                        float local_prob = (g_oosi_v3_ctx.logits && local_tok < 50282)
                            ? g_oosi_v3_ctx.logits[local_tok] : 0.5f;
                        // Publish this turn's vote to our peer slot
                        soma_swarm_net_publish(&g_soma_swarm_net,
                                               &local_tok, &local_prob, 1,
                                               soma_first_conf > 0.0f ? soma_first_conf : 0.5f,
                                               &g_soma_dna,
                                               (unsigned int)(g_soma_smb.tick_count & 0xFFFFFFFF));
                        // Read peer consensus (single-machine: just echoes back own vote)
                        SomaSwarmNetResult nr = soma_swarm_net_consensus(
                            &g_soma_swarm_net, g_oosi_v3_ctx.logits,
                            g_soma_dual.vocab_size > 0 ? g_soma_dual.vocab_size : 50282,
                            (unsigned int)(g_soma_smb.tick_count & 0xFFFFFFFF));
                        if (nr.n_peers_used > 1 && nr.consensus_token >= 0) {
                            r.token = nr.consensus_token; // Apply net consensus
                        }
                    }

                    // Verbose diagnostic (boot_verbose=2)
                    if (g_boot_verbose >= 2 && n_out < 2) {
                        Print(L"\r\n[DBG] tok#%d: id=%d RAW_logits[0..4]=",
                              n_out, r.token);
                        for (int qi = 0; qi < 5; qi++) {
                            int iv = (int)(g_oosi_v3_ctx.dbg_raw_logits[qi] * 100.0f);
                            Print(L"%d ", iv);
                        }
                        int imin = (int)(g_oosi_v3_ctx.dbg_raw_min * 100.0f);
                        int imax = (int)(g_oosi_v3_ctx.dbg_raw_max * 100.0f);
                        Print(L" min=%d max=%d\r\n", imin, imax);
                        Print(L"[DBG] embed_scale[%d]=", last);
                        {
                            ssm_f32 es = g_oosi_v3_ctx.w->embed_scale[last];
                            int ies = (int)(es * 10000.0f);
                            Print(L"%d/10000", ies);
                        }
                        Print(L" temp=%d/1000 top_p=%d/1000\r\n",
                              (int)(g_oosi_v3_ctx.temperature * 1000.0f),
                              (int)(g_oosi_v3_ctx.top_p * 1000.0f));
                        Print(L"[OOSI-v3] ");
                    }
                    out_ids[n_out++] = r.token;
                    // Print decoded token via proper UTF-8 handler
                    char tok_str[32]; int tok_len_out = 0;
                    tok_len_out = llmk_oo_infer_decode_token(r.token,
                                      tok_str, sizeof(tok_str));
                    if (tok_len_out > 0) {
                        uefi_print_utf8_bytes(tok_str, tok_len_out);
                    } else {
                        // Debug: show raw token ID when decode fails
                        Print(L"<tok%d>", r.token);
                    }
                    last = r.token;
                    if (r.halted) {
                        Print(L"\r\n[OOSI-v3] halted (halt_prob=%.2f)\r\n",
                              (double)r.halt_prob);
                        break;
                    }
                    {
                        float mind_logit = 0.0f;
                        float mind_prob = 0.0f;
                        if (llmk_mind_runtime_should_halt((float)n_out, &mind_logit, &mind_prob)) {
                            Print(L"\r\n[MindHaltRuntime] halted at loop_pos=%d.%03d halt_prob=%d.%03d threshold=%d.%03d\r\n",
                                  n_out,
                                  0,
                                  (int)mind_prob,
                                  (int)((mind_prob >= 0.0f ? mind_prob - (int)mind_prob : ((int)mind_prob - mind_prob)) * 1000.0f),
                                  (int)g_mind_runtime_halt_threshold,
                                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
                            break;
                        }
                    }
                    if (r.token == 0) {
                        Print(L"\r\n[OOSI-v3] EOS token\r\n");
                        break;
                    }
                }
                {
                    UINT64 gen_elapsed = __rdtsc() - gen_start_tsc;
                    UINT64 ms = 0;
                    if (tsc_per_sec > 0 && gen_elapsed > 0)
                        ms = (gen_elapsed * 1000) / tsc_per_sec;
                    if (ms > 0 && n_out > 0) {
                        UINT64 tok_per_sec_x10 = ((UINT64)n_out * 10000) / ms;
                        Print(L"\r\n[OOSI-v3] %d tokens in %lu ms (%lu.%lu tok/s)\r\n\r\n",
                              n_out, (unsigned long)ms,
                              (unsigned long)(tok_per_sec_x10 / 10),
                              (unsigned long)(tok_per_sec_x10 % 10));
                    } else {
                        Print(L"\r\n[OOSI-v3] Done: %d tokens generated\r\n\r\n", n_out);
                    }
                }
                llmk_attach_route_policy_end(&attach_policy_state);
                // SomaMind SMB: record this interaction into memory bus
                if (g_soma_initialized && n_out > 0) {
                    uint16_t gist_ids[SOMA_SMB_GIST_LEN];
                    int glen = n_out < SOMA_SMB_GIST_LEN ? n_out : SOMA_SMB_GIST_LEN;
                    for (int gi = 0; gi < glen; gi++)
                        gist_ids[gi] = (uint16_t)(out_ids[gi] & 0xFFFF);
                    soma_smb_write(&g_soma_smb,
                                   soma_input_hash,
                                   soma_domain_used,
                                   soma_route_used,
                                   soma_core_used,
                                   soma_first_conf,
                                   gist_ids, glen);
                    soma_smb_tick(&g_soma_smb);
                    // Swarm: update agent fitness + periodic evolution
                    if (g_soma_swarm.enabled && g_soma_swarm.ready) {
                        soma_swarm_update_fitness(&g_soma_swarm, &g_soma_swarm_last, soma_first_conf);
                        if ((g_soma_smb.turn & 0xF) == 0) // every 16 turns
                            soma_swarm_evolve(&g_soma_swarm);
                    }
                    // Meta-cycle: score fitness + auto-mutate DNA if stagnating
                    {
                        int mutated = soma_meta_cycle(&g_soma_meta, &g_soma_dna,
                                                       &g_soma_router, &g_soma_dual,
                                                       &g_soma_smb, 8);
                        if (mutated && g_boot_verbose)
                            Print(L"[SomaMind:Meta] DNA auto-mutated (stagnation=%d)\r\n",
                                  g_soma_meta.mutations_applied);
                    }
                    // Auto-dream every 8 turns if enough data
                    if (soma_dream_should_run(&g_soma_dream, &g_soma_smb, 8)) {
                        SomaDreamSummary ds = soma_dream_run(&g_soma_dream, &g_soma_smb);
                        if (ds.total_slots >= 2) {
                            soma_dream_apply(&g_soma_dream, &g_soma_dna, &ds, 1 /*safe*/);
                            if (g_boot_verbose)
                                Print(L"[SomaMind:Dream] auto-applied (quality=%d%% bias_d=%d/1000)\r\n",
                                      ds.dream_quality,
                                      (int)(ds.recommended_bias_delta * 1000));
                        }
                    }
                    // Phase H: record this interaction in session memory
                    if (g_soma_memory.enabled && n_out > 0) {
                        // Build a short ASCII response summary from first decoded tokens
                        char resp_summary[SOMA_MEM_RESPONSE_LEN];
                        int rs = 0;
                        for (int ri = 0; ri < n_out && rs < SOMA_MEM_RESPONSE_LEN - 1; ri++) {
                            char tb[32]; int tl = 0;
                            tl = llmk_oo_infer_decode_token(out_ids[ri], tb, sizeof(tb));
                            for (int ci = 0; ci < tl && rs < SOMA_MEM_RESPONSE_LEN - 1; ci++)
                                resp_summary[rs++] = tb[ci];
                        }
                        resp_summary[rs] = 0;
                        soma_memory_record_tagged(&g_soma_memory, text, resp_summary,
                                                  (unsigned char)soma_domain_used);
                        // Phase R: feed symbion with post-inference performance sample
                        {
                            SymbionSample ssamp;
                            ssamp.latency_raw = (uint32_t)(g_sentinel.last_dt_cycles >> 10);
                            ssamp.retries     = (uint32_t)g_soma_warden.violations_since_reset;
                            ssamp.stress      = (uint8_t)(g_soma_warden.pressure_level * 30 > 100
                                                ? 100 : g_soma_warden.pressure_level * 30);
                            symbion_feed(&g_symbion, &ssamp);
                        }
                        // Phase I: autosave journal every N turns
                        g_soma_journal_total_turns++;
                        g_soma_journal_turns_since_save++;
                        if (g_root && g_soma_journal_turns_since_save >= SOMA_JOURNAL_AUTOSAVE_EVERY) {
                            int jsaved = soma_journal_save(&g_soma_memory, g_root,
                                                           g_soma_journal_total_turns);
                            g_soma_journal_turns_since_save = 0;
                            if (g_boot_verbose)
                                Print(L"[SomaJournal] Auto-saved %d entries (total=%d)\r\n",
                                      jsaved, (int)g_soma_journal_total_turns);
                        }
                        // Phase M: warden pressure update (sentinel → router feedback)
                        int warden_escalated_this_turn = 0;
                        {
                            int wpress_prev = g_soma_warden.pressure_level;
                            int wpress = soma_warden_update(&g_soma_warden,
                                                            &g_sentinel, &g_zones,
                                                            &g_soma_router,
                                                            (int)g_soma_journal_total_turns);
                            // Phase P: immunion sync (before session record)
                            {
                                static int s_last_imm_reactions = 0;
                                int imm_new = soma_warden_immunion_sync(
                                    &g_soma_warden, &g_immunion,
                                    &g_soma_router,
                                    (int)g_soma_journal_total_turns);
                                soma_session_immunion_record(&g_soma_session, imm_new);
                                if (imm_new > 0) s_last_imm_reactions += imm_new;
                                (void)s_last_imm_reactions;
                            }
                            warden_escalated_this_turn = (wpress > wpress_prev ||
                                g_soma_warden.immunion_escalations > 0) ? 1 : 0;
                            if (wpress >= SOMA_PRESSURE_HIGH && g_boot_verbose) {
                                char wbuf[96];
                                soma_warden_status_str(&g_soma_warden, wbuf, 96);
                                Print(L"[Warden] ");
                                for (int wi = 0; wbuf[wi]; wi++)
                                    Print(L"%c", (CHAR16)(unsigned char)wbuf[wi]);
                                Print(L"\r\n");
                            }
                        }
                        // Phase N: session fitness record
                        {
                            // Cortex flagged this turn?
                            int cflag = g_soma_cortex.loaded ?
                                (g_soma_cortex.total_flagged > g_soma_session.cortex_flagged +
                                 g_soma_session.turns_total ? 1 : 0) : 0;
                            // Route used: default EXTERNAL (big model ran)
                            int route_used = 2; // SOMA_ROUTE_EXTERNAL
                            soma_session_record(&g_soma_session, route_used,
                                                850, /* confidence ~85% (default) */
                                                cflag,
                                                warden_escalated_this_turn);
                        }
                    }
                }
                continue;
            }

            // Tokenize
            int prompt_tokens[256];
            int prompt_len = llmk_oo_infer_tokenize(infer_text, prompt_tokens, 256);
            if (prompt_len <= 0) {
                Print(L"[OOSI] ERROR: tokenization failed\r\n\r\n");
                continue;
            }

            // Run inference
            OoThinkResult result;
            int n = llmk_oo_infer_think(prompt_tokens, prompt_len, &result);
            if (n <= 0) {
                Print(L"[OOSI] ERROR: inference failed (n=%d)\r\n\r\n", n);
                continue;
            }

            // Display result
            Print(L"[OOSI] Output (%d tokens, %d loops, P=%.2f%s):\r\n",
                  result.tokens_generated,
                  result.loops_taken,
                  result.final_halt_prob,
                  result.halted_naturally ? L"" : L" max");
            Print(L"  ");
            llmk_print_ascii(result.text);
            Print(L"\r\n\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/ssm_reset", 10) == 0) {
            llmk_mind_clear_core();
            Print(L"\r\n[SSM] State reset (no active model to reset)\r\n\r\n");
            continue;
        }

        Print(L"\r\nNo model loaded. Use /models then set repl.cfg: model=<file> and reboot.\r\n\r\n");
    }
}

typedef struct {
    char kind[64]; // SAFE: bounded extracted event kind from sovereign handoff JSON
    char severity[64]; // SAFE: bounded extracted event severity from sovereign handoff JSON
    char summary[160];
} LlmkHandoffEvent;

static const char *llmk_json_skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static int llmk_ascii_eq_n(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static const char *llmk_json_find_key_value(const char *buf, UINTN len, const char *key, const char *start_at) {
    if (!buf || !key) return NULL;
    int key_len = 0;
    while (key[key_len]) key_len++;
    if (key_len <= 0) return NULL;
    const char *start = start_at ? start_at : buf;
    const char *end = buf + len;
    for (const char *p = start; p + key_len + 3 < end; p++) {
        if (*p != '"') continue;
        if (!llmk_ascii_eq_n(p + 1, key, key_len)) continue;
        if (p[1 + key_len] != '"') continue;
        const char *v = llmk_json_skip_ws(p + 1 + key_len + 1, end);
        if (v >= end || *v != ':') continue;
        v = llmk_json_skip_ws(v + 1, end);
        if (v >= end) return NULL;
        return v;
    }
    return NULL;
}

static int llmk_json_extract_string_value(const char *value, const char *end, char *out, int out_cap) {
    if (!value || !out || out_cap <= 0) return 0;
    value = llmk_json_skip_ws(value, end);
    if (value >= end || *value != '"') return 0;
    value++;
    int n = 0;
    while (value < end && *value) {
        if (*value == '"') break;
        if (*value == '\\' && value + 1 < end) {
            value++;
        }
        if (n + 1 < out_cap) out[n++] = *value;
        value++;
    }
    out[n] = 0;
    return (value < end && *value == '"');
}

static int llmk_json_extract_int_value(const char *value, const char *end, int *out) {
    if (!value || !out) return 0;
    value = llmk_json_skip_ws(value, end);
    int sign = 1;
    if (value < end && *value == '-') {
        sign = -1;
        value++;
    }
    if (value >= end || *value < '0' || *value > '9') return 0;
    int acc = 0;
    while (value < end && *value >= '0' && *value <= '9') {
        acc = acc * 10 + (*value - '0');
        value++;
    }
    *out = acc * sign;
    return 1;
}

static int llmk_json_extract_u64_value(const char *value, const char *end, UINT64 *out) {
    if (!value || !out) return 0;
    value = llmk_json_skip_ws(value, end);
    if (value >= end || *value < '0' || *value > '9') return 0;
    UINT64 acc = 0;
    while (value < end && *value >= '0' && *value <= '9') {
        acc = (acc * 10ULL) + (UINT64)(*value - '0');
        value++;
    }
    *out = acc;
    return 1;
}

static int llmk_json_value_is_null(const char *value, const char *end) {
    value = llmk_json_skip_ws(value, end);
    if (!value || value + 4 > end) return 0;
    return (value[0] == 'n' && value[1] == 'u' && value[2] == 'l' && value[3] == 'l') ? 1 : 0;
}

static int llmk_json_extract_string_key_range(const char *buf, UINTN len, const char *key, char *out, int out_cap) {
    const char *v = llmk_json_find_key_value(buf, len, key, buf);
    if (!v) return 0;
    return llmk_json_extract_string_value(v, buf + len, out, out_cap);
}

static int llmk_json_extract_int_key_range(const char *buf, UINTN len, const char *key, int *out) {
    const char *v = llmk_json_find_key_value(buf, len, key, buf);
    if (!v) return 0;
    return llmk_json_extract_int_value(v, buf + len, out);
}

static int llmk_json_extract_u64_key_range(const char *buf, UINTN len, const char *key, UINT64 *out) {
    const char *v = llmk_json_find_key_value(buf, len, key, buf);
    if (!v) return 0;
    return llmk_json_extract_u64_value(v, buf + len, out);
}

static int llmk_json_extract_optional_string_key_range(const char *buf, UINTN len, const char *key,
                                                       char *out, int out_cap, int *out_present) {
    if (!out || out_cap <= 0 || !out_present) return 0;
    out[0] = 0;
    const char *v = llmk_json_find_key_value(buf, len, key, buf);
    if (!v) {
        *out_present = 0;
        return 1;
    }
    *out_present = 1;
    if (llmk_json_value_is_null(v, buf + len)) {
        out[0] = 0;
        return 1;
    }
    return llmk_json_extract_string_value(v, buf + len, out, out_cap);
}

static int llmk_json_extract_recent_events(const char *buf, UINTN len, LlmkHandoffEvent *out, int cap) {
    if (!buf || !out || cap <= 0) return 0;
    const char *v = llmk_json_find_key_value(buf, len, "recent_events", buf);
    if (!v) return 0;
    const char *end = buf + len;
    v = llmk_json_skip_ws(v, end);
    if (v >= end || *v != '[') return 0;
    v++;
    int count = 0;
    while (v < end && count < cap) {
        v = llmk_json_skip_ws(v, end);
        if (v >= end || *v == ']') break;
        if (*v != '{') {
            v++;
            continue;
        }
        const char *obj = v;
        int depth = 0;
        int in_string = 0;
        while (v < end) {
            char c = *v;
            if (c == '"' && (v == obj || v[-1] != '\\')) in_string = !in_string;
            if (!in_string) {
                if (c == '{') depth++;
                else if (c == '}') {
                    depth--;
                    if (depth == 0) {
                        v++;
                        break;
                    }
                }
            }
            v++;
        }
        UINTN obj_len = (UINTN)(v - obj);
        out[count].kind[0] = 0;
        out[count].severity[0] = 0;
        out[count].summary[0] = 0;
        if (llmk_json_extract_string_key_range(obj, obj_len, "kind", out[count].kind, (int)sizeof(out[count].kind)) &&
            llmk_json_extract_string_key_range(obj, obj_len, "severity", out[count].severity, (int)sizeof(out[count].severity)) &&
            llmk_json_extract_string_key_range(obj, obj_len, "summary", out[count].summary, (int)sizeof(out[count].summary))) {
            count++;
        }
    }
    return count;
}

static const char *llmk_oo_mode_name_ascii(UINT32 mode) {
    switch (mode) {
        case LLMK_OO_MODE_NORMAL: return "normal";
        case LLMK_OO_MODE_DEGRADED: return "degraded";
        case LLMK_OO_MODE_SAFE: return "safe";
        default: return "unknown";
    }
}

static int llmk_oo_mode_from_ascii(const char *s, UINT32 *out_mode) {
    if (!s || !out_mode) return 0;
    if (my_strncmp(s, "normal", 6) == 0 && s[6] == 0) {
        *out_mode = LLMK_OO_MODE_NORMAL;
        return 1;
    }
    if (my_strncmp(s, "degraded", 8) == 0 && s[8] == 0) {
        *out_mode = LLMK_OO_MODE_DEGRADED;
        return 1;
    }
    if (my_strncmp(s, "safe", 4) == 0 && s[4] == 0) {
        *out_mode = LLMK_OO_MODE_SAFE;
        return 1;
    }
    return 0;
}

static const char *llmk_djibion_mode_name_ascii(DjibionMode mode) {
    switch (mode) {
        case DJIBION_MODE_OFF: return "off";
        case DJIBION_MODE_OBSERVE: return "observe";
        case DJIBION_MODE_ENFORCE: return "enforce";
        default: return "unknown";
    }
}

static int llmk_djibion_mode_from_ascii(const char *s, DjibionMode *out_mode) {
    if (!s || !out_mode) return 0;
    if (my_strncmp(s, "off", 3) == 0 && s[3] == 0) {
        *out_mode = DJIBION_MODE_OFF;
        return 1;
    }
    if (my_strncmp(s, "observe", 7) == 0 && s[7] == 0) {
        *out_mode = DJIBION_MODE_OBSERVE;
        return 1;
    }
    if (my_strncmp(s, "enforce", 7) == 0 && s[7] == 0) {
        *out_mode = DJIBION_MODE_ENFORCE;
        return 1;
    }
    return 0;
}

static EFI_STATUS llmk_oo_write_handoff_receipt_best_effort(const char *organism_id,
                                                            const char *mode,
                                                            const char *enforcement,
                                                            UINT64 continuity_epoch,
                                                            int recovery_present,
                                                            const char *last_recovery_reason) {
    if (!g_root || !organism_id || !mode || !enforcement) return EFI_NOT_READY;

    char out[512];
    int p = 0;
    out[0] = 0;
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "organism_id=");
    llmk_ascii_append_str(out, (int)sizeof(out), &p, organism_id);
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\nmode=");
    llmk_ascii_append_str(out, (int)sizeof(out), &p, mode);
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\npolicy_enforcement=");
    llmk_ascii_append_str(out, (int)sizeof(out), &p, enforcement);
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\ncontinuity_epoch=");
    llmk_ascii_append_u64(out, (int)sizeof(out), &p, continuity_epoch);
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\nlast_recovery_reason=");
    if (recovery_present && last_recovery_reason && last_recovery_reason[0]) {
        llmk_ascii_append_str(out, (int)sizeof(out), &p, last_recovery_reason);
    } else {
        llmk_ascii_append_str(out, (int)sizeof(out), &p, "none");
    }
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\n");

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, L"OOHANDOFF.TXT");
    if (EFI_ERROR(st) || !f) return st;

    st = llmk_file_write_bytes(f, out, (UINTN)p);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static int llmk_handoff_receipt_extract_value(const char *buf, const char *key, char *out, int out_cap) {
    if (!buf || !key || !out || out_cap <= 0) return 0;
    out[0] = 0;
    int key_len = 0;
    while (key[key_len]) key_len++;
    const char *p = buf;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        const char *line_end = p;
        while (line_end > line && (line_end[-1] == '\r' || line_end[-1] == '\n')) line_end--;
        if ((line_end - line) > key_len && llmk_ascii_eq_n(line, key, key_len) && line[key_len] == '=') {
            int n = 0;
            const char *v = line + key_len + 1;
            while (v < line_end && n + 1 < out_cap) out[n++] = *v++;
            out[n] = 0;
            return 1;
        }
        if (*p == '\n') p++;
    }
    return 0;
}

static void llmk_oo_handoff_receipt_best_effort(const CHAR16 *path) {
    const CHAR16 *load_name = path && path[0] ? path : L"OOHANDOFF.TXT";
    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(load_name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[oo_handoff_receipt] ERROR: read failed: %s (%r)\r\n\r\n", load_name, st);
        return;
    }

    char *buf = NULL;
    EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
    if (EFI_ERROR(st2) || !buf) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[oo_handoff_receipt] ERROR: OOM\r\n\r\n");
        return;
    }
    CopyMem(buf, raw, raw_len);
    buf[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    char organism_id[96];
    char mode[64];
    char policy_enforcement[64];
    char continuity_epoch[64];
    char last_recovery_reason[128];
    organism_id[0] = 0;
    mode[0] = 0;
    policy_enforcement[0] = 0;
    continuity_epoch[0] = 0;
    last_recovery_reason[0] = 0;

    int ok = 1;
    if (!llmk_handoff_receipt_extract_value(buf, "organism_id", organism_id, (int)sizeof(organism_id))) ok = 0;
    if (!llmk_handoff_receipt_extract_value(buf, "mode", mode, (int)sizeof(mode))) ok = 0;
    if (!llmk_handoff_receipt_extract_value(buf, "policy_enforcement", policy_enforcement, (int)sizeof(policy_enforcement))) ok = 0;
    if (!llmk_handoff_receipt_extract_value(buf, "continuity_epoch", continuity_epoch, (int)sizeof(continuity_epoch))) ok = 0;
    if (!llmk_handoff_receipt_extract_value(buf, "last_recovery_reason", last_recovery_reason, (int)sizeof(last_recovery_reason))) ok = 0;

    if (!ok) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"\r\n[oo_handoff_receipt] ERROR: invalid receipt format\r\n\r\n");
        return;
    }

    Print(L"\r\n[oo_handoff_receipt] file=%s\r\n", load_name);
    Print(L"[oo_handoff_receipt] organism_id="); llmk_print_ascii(organism_id); Print(L"\r\n");
    Print(L"[oo_handoff_receipt] mode="); llmk_print_ascii(mode); Print(L"\r\n");
    Print(L"[oo_handoff_receipt] policy_enforcement="); llmk_print_ascii(policy_enforcement); Print(L"\r\n");
    Print(L"[oo_handoff_receipt] continuity_epoch="); llmk_print_ascii(continuity_epoch); Print(L"\r\n");
    Print(L"[oo_handoff_receipt] last_recovery_reason="); llmk_print_ascii(last_recovery_reason); Print(L"\r\n\r\n");

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_continuity_status_best_effort(const CHAR16 *path) {
    const CHAR16 *load_name = path && path[0] ? path : L"OOHANDOFF.TXT";

    char receipt_organism_id[96];
    char receipt_mode[64];
    char receipt_policy[64];
    char receipt_epoch[64];
    char receipt_recovery_reason[128];
    int receipt_ok = 0;
    receipt_organism_id[0] = 0;
    receipt_mode[0] = 0;
    receipt_policy[0] = 0;
    receipt_epoch[0] = 0;
    receipt_recovery_reason[0] = 0;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS receipt_st = llmk_read_entire_file_best_effort(load_name, &raw, &raw_len);
    if (!EFI_ERROR(receipt_st) && raw && raw_len > 0) {
        char *buf = NULL;
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
        if (!EFI_ERROR(st2) && buf) {
            CopyMem(buf, raw, raw_len);
            buf[raw_len] = 0;
            if (llmk_handoff_receipt_extract_value(buf, "organism_id", receipt_organism_id, (int)sizeof(receipt_organism_id)) &&
                llmk_handoff_receipt_extract_value(buf, "mode", receipt_mode, (int)sizeof(receipt_mode)) &&
                llmk_handoff_receipt_extract_value(buf, "policy_enforcement", receipt_policy, (int)sizeof(receipt_policy)) &&
                llmk_handoff_receipt_extract_value(buf, "continuity_epoch", receipt_epoch, (int)sizeof(receipt_epoch)) &&
                llmk_handoff_receipt_extract_value(buf, "last_recovery_reason", receipt_recovery_reason, (int)sizeof(receipt_recovery_reason))) {
                receipt_ok = 1;
            }
            uefi_call_wrapper(BS->FreePool, 1, buf);
        }
    }
    if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);

    LlmkOoState local;
    LlmkOoState recovery;
    int local_ok = llmk_oo_load_state_best_effort(&local);
    int recovery_ok = llmk_oo_load_recovery_best_effort(&recovery);

    const char *summary = "aligned";
    const char *reason = "exact";
    UINT32 receipt_mode_value = LLMK_OO_MODE_SAFE;
    int receipt_mode_valid = 0;

    if (!receipt_ok) {
        summary = "stale";
        reason = "no_receipt";
    } else if (!local_ok) {
        summary = "stale";
        reason = "no_local_state";
    } else if (!recovery_ok) {
        summary = "stale";
        reason = "no_recovery_checkpoint";
    } else if (!llmk_oo_mode_from_ascii(receipt_mode, &receipt_mode_value)) {
        summary = "divergent";
        reason = "invalid_receipt_mode";
    } else {
        receipt_mode_valid = 1;
        if (local.mode < receipt_mode_value) {
            summary = "divergent";
            reason = "local_weaker_than_receipt";
        } else if (recovery.mode != local.mode || recovery.boot_count != local.boot_count) {
            summary = "divergent";
            reason = "recovery_mismatch";
        } else if (local.mode > receipt_mode_value) {
            summary = "aligned";
            reason = "local_safer_than_receipt";
        }
    }

    Print(L"\r\n[oo_continuity] receipt.file=%s\r\n", load_name);
    Print(L"[oo_continuity] receipt.present=%d\r\n", receipt_ok ? 1 : 0);
    if (receipt_ok) {
        Print(L"[oo_continuity] receipt.organism_id="); llmk_print_ascii(receipt_organism_id); Print(L"\r\n");
        Print(L"[oo_continuity] receipt.mode="); llmk_print_ascii(receipt_mode); Print(L"\r\n");
        Print(L"[oo_continuity] receipt.policy="); llmk_print_ascii(receipt_policy); Print(L"\r\n");
        Print(L"[oo_continuity] receipt.epoch="); llmk_print_ascii(receipt_epoch); Print(L"\r\n");
        Print(L"[oo_continuity] receipt.last_recovery_reason="); llmk_print_ascii(receipt_recovery_reason); Print(L"\r\n");
    }
    Print(L"[oo_continuity] local.present=%d\r\n", local_ok ? 1 : 0);
    if (local_ok) {
        Print(L"[oo_continuity] local.boot_count=%lu\r\n", (UINT64)local.boot_count);
        Print(L"[oo_continuity] local.mode="); llmk_print_ascii(llmk_oo_mode_name_ascii(local.mode)); Print(L"\r\n");
    }
    Print(L"[oo_continuity] recovery.present=%d\r\n", recovery_ok ? 1 : 0);
    if (recovery_ok) {
        Print(L"[oo_continuity] recovery.boot_count=%lu\r\n", (UINT64)recovery.boot_count);
        Print(L"[oo_continuity] recovery.mode="); llmk_print_ascii(llmk_oo_mode_name_ascii(recovery.mode)); Print(L"\r\n");
    }
    if (receipt_ok && receipt_mode_valid) {
        Print(L"[oo_continuity] receipt_mode_rank=%lu\r\n", (UINT64)receipt_mode_value);
    }
    Print(L"[oo_continuity] summary="); llmk_print_ascii(summary); Print(L"\r\n");
    Print(L"[oo_continuity] reason="); llmk_print_ascii(reason); Print(L"\r\n\r\n");
}

static void llmk_oo_print_persistence_status_best_effort(void) {
    LlmkOoState local;
    LlmkOoState recovery;
    int local_ok = llmk_oo_load_state_best_effort(&local);
    int recovery_ok = llmk_oo_load_recovery_best_effort(&recovery);

    void *raw = NULL;
    UINTN raw_len = 0;
    UINTN consult_len = 0;
    UINTN jour_len = 0;
    UINTN outcome_len = 0;
    int consult_ok = 0;
    int jour_ok = 0;
    int outcome_ok = 0;

    if (!EFI_ERROR(llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &raw, &raw_len))) {
        consult_ok = 1;
        consult_len = raw_len;
    }
    if (raw) { uefi_call_wrapper(BS->FreePool, 1, raw); raw = NULL; }

    raw_len = 0;
    if (!EFI_ERROR(llmk_read_entire_file_best_effort(L"OOJOUR.LOG", &raw, &raw_len))) {
        jour_ok = 1;
        jour_len = raw_len;
    }
    if (raw) { uefi_call_wrapper(BS->FreePool, 1, raw); raw = NULL; }

    raw_len = 0;
    if (!EFI_ERROR(llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &raw, &raw_len))) {
        outcome_ok = 1;
        outcome_len = raw_len;
    }
    if (raw) { uefi_call_wrapper(BS->FreePool, 1, raw); raw = NULL; }

    char receipt_mode[64];
    char receipt_epoch[64];
    int receipt_ok = 0;
    int receipt_mode_valid = 0;
    UINT32 receipt_mode_value = LLMK_OO_MODE_SAFE;
    receipt_mode[0] = 0;
    receipt_epoch[0] = 0;

    raw_len = 0;
    if (!EFI_ERROR(llmk_read_entire_file_best_effort(L"OOHANDOFF.TXT", &raw, &raw_len)) && raw && raw_len > 0) {
        char *buf = NULL;
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
        if (!EFI_ERROR(st2) && buf) {
            CopyMem(buf, raw, raw_len);
            buf[raw_len] = 0;
            if (llmk_handoff_receipt_extract_value(buf, "mode", receipt_mode, (int)sizeof(receipt_mode)) &&
                llmk_handoff_receipt_extract_value(buf, "continuity_epoch", receipt_epoch, (int)sizeof(receipt_epoch))) {
                receipt_ok = 1;
                receipt_mode_valid = llmk_oo_mode_from_ascii(receipt_mode, &receipt_mode_value);
            }
            uefi_call_wrapper(BS->FreePool, 1, buf);
        }
    }
    if (raw) { uefi_call_wrapper(BS->FreePool, 1, raw); raw = NULL; }

    const char *continuity = "stale";
    if (receipt_ok && local_ok && recovery_ok && receipt_mode_valid) {
        if (local.mode < receipt_mode_value) continuity = "divergent";
        else if (recovery.mode != local.mode || recovery.boot_count != local.boot_count) continuity = "divergent";
        else continuity = "aligned";
    } else if (!receipt_ok) {
        continuity = "no_receipt";
    } else if (!local_ok) {
        continuity = "no_local_state";
    } else if (!recovery_ok) {
        continuity = "no_recovery";
    } else if (!receipt_mode_valid) {
        continuity = "invalid_receipt";
    }

    Print(L"[OO Persistence]\r\n");
    Print(L"  OOSTATE.BIN    present=%d", local_ok ? 1 : 0);
    if (local_ok) {
        Print(L" boot_count=%lu mode=%s", (UINT64)local.boot_count, llmk_oo_mode_name(local.mode));
    }
    Print(L"\r\n");
    Print(L"  OORECOV.BIN    present=%d", recovery_ok ? 1 : 0);
    if (recovery_ok) {
        Print(L" boot_count=%lu mode=%s", (UINT64)recovery.boot_count, llmk_oo_mode_name(recovery.mode));
    }
    Print(L"\r\n");
    Print(L"  OOJOUR.LOG     present=%d bytes=%lu\r\n", jour_ok ? 1 : 0, (UINT64)jour_len);
    Print(L"  OOCONSULT.LOG  present=%d bytes=%lu\r\n", consult_ok ? 1 : 0, (UINT64)consult_len);
    if (consult_ok) {
        llmk_oo_print_last_consult_status_best_effort();
    }
    Print(L"  OOOUTCOME.LOG  present=%d bytes=%lu\r\n", outcome_ok ? 1 : 0, (UINT64)outcome_len);
    if (local_ok) {
        UINT32 meta = llmk_oo_get_last_action_meta(local.flags);
        UINT32 apply_boot_low8 = llmk_oo_get_last_apply_boot_low8(local.flags);
        if (meta != 0 && apply_boot_low8 != 0) {
            UINT32 action_id = (meta & 0x3Fu);
            UINT32 apply_mode = (meta >> 6u) & 0x3u;
            UINT32 want_boot_low8 = (UINT32)((apply_boot_low8 + 1u) & 0xFFu);
            Print(L"  pending.action=%a next_boot_low8=%lu apply_mode=%s\r\n",
                  llmk_oo_action_name(action_id),
                  (UINT64)want_boot_low8,
                  llmk_oo_mode_name(apply_mode));
        }
    }
    Print(L"  OOHANDOFF.TXT  present=%d", receipt_ok ? 1 : 0);
    if (receipt_ok) {
        Print(L" epoch="); llmk_print_ascii(receipt_epoch);
        Print(L" mode="); llmk_print_ascii(receipt_mode);
    }
    Print(L"\r\n");
    Print(L"  continuity="); llmk_print_ascii(continuity); Print(L"\r\n\r\n");
}

static void llmk_oo_reboot_probe_best_effort(void) {
    const CHAR16 *probe_name = L"OOREBOOT.TXT";
    LlmkOoState local;
    LlmkOoState recovery;
    int local_ok = llmk_oo_load_state_best_effort(&local);
    int recovery_ok = llmk_oo_load_recovery_best_effort(&recovery);

    if (!local_ok || !recovery_ok) {
        Print(L"\r\n[oo_reboot_probe] ERROR: missing OO state (local=%d recovery=%d)\r\n\r\n",
              local_ok ? 1 : 0, recovery_ok ? 1 : 0);
        llmk_oo_journal_event_load_state_best_effort("reboot_probe_missing_state");
        return;
    }

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(probe_name, &raw, &raw_len);
    if (!EFI_ERROR(st) && raw && raw_len > 0) {
        char *buf = NULL;
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
        if (EFI_ERROR(st2) || !buf) {
            if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
            Print(L"\r\n[oo_reboot_probe] ERROR: OOM\r\n\r\n");
            return;
        }

        CopyMem(buf, raw, raw_len);
        buf[raw_len] = 0;
        uefi_call_wrapper(BS->FreePool, 1, raw);
        raw = NULL;

        char armed_boot_txt[64]; // SAFE: decimal boot_count text extracted from a short key=value file
        char armed_mode_txt[32]; // SAFE: mode token is one of normal|degraded|safe
        UINT64 armed_boot = 0;
        UINT32 armed_mode = LLMK_OO_MODE_SAFE;
        int got_boot = llmk_handoff_receipt_extract_value(buf, "armed_boot_count", armed_boot_txt, (int)sizeof(armed_boot_txt));
        int got_mode = llmk_handoff_receipt_extract_value(buf, "armed_mode", armed_mode_txt, (int)sizeof(armed_mode_txt));
        int boot_ok = got_boot && llmk_cfg_parse_u64(armed_boot_txt, &armed_boot);
        int mode_ok = got_mode && llmk_oo_mode_from_ascii(armed_mode_txt, &armed_mode);
        uefi_call_wrapper(BS->FreePool, 1, buf);

        int boot_advanced = boot_ok && local.boot_count > armed_boot;
        int recovery_match = (recovery.mode == local.mode && recovery.boot_count == local.boot_count) ? 1 : 0;
        int mode_ok_now = mode_ok ? (local.mode >= armed_mode) : 0;
        int verified = boot_advanced && recovery_match && mode_ok_now;

        Print(L"\r\n[oo_reboot_probe] armed.present=1\r\n");
        if (boot_ok) {
            Print(L"[oo_reboot_probe] armed.boot_count=%lu\r\n", armed_boot);
        } else {
            Print(L"[oo_reboot_probe] armed.boot_count=invalid\r\n");
        }
        if (mode_ok) {
            Print(L"[oo_reboot_probe] armed.mode="); llmk_print_ascii(armed_mode_txt); Print(L"\r\n");
        } else {
            Print(L"[oo_reboot_probe] armed.mode=invalid\r\n");
        }
        Print(L"[oo_reboot_probe] current.boot_count=%lu\r\n", (UINT64)local.boot_count);
        Print(L"[oo_reboot_probe] current.mode="); llmk_print_ascii(llmk_oo_mode_name_ascii(local.mode)); Print(L"\r\n");
        Print(L"[oo_reboot_probe] boot_advanced=%d\r\n", boot_advanced ? 1 : 0);
        Print(L"[oo_reboot_probe] recovery_match=%d\r\n", recovery_match ? 1 : 0);
        Print(L"[oo_reboot_probe] mode_ok=%d\r\n", mode_ok_now ? 1 : 0);
        Print(L"[oo_reboot_probe] verified=%d\r\n", verified ? 1 : 0);

        if (verified) {
            (void)llmk_delete_file_best_effort(probe_name);
            Print(L"[oo_reboot_probe] summary=pass\r\n\r\n");
            llmk_oo_journal_event_load_state_best_effort("reboot_probe_verified");
        } else {
            Print(L"[oo_reboot_probe] summary=fail\r\n\r\n");
            llmk_oo_journal_event_load_state_best_effort("reboot_probe_failed");
        }
        return;
    }
    if (raw) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        raw = NULL;
    }

    char out[160];
    int p = 0;
    out[0] = 0;
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "armed_boot_count=");
    llmk_ascii_append_u64(out, (int)sizeof(out), &p, local.boot_count);
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\narmed_mode=");
    llmk_ascii_append_str(out, (int)sizeof(out), &p, llmk_oo_mode_name_ascii(local.mode));
    llmk_ascii_append_str(out, (int)sizeof(out), &p, "\r\n");

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, probe_name);
    if (EFI_ERROR(st) || !f) {
        Print(L"\r\n[oo_reboot_probe] ERROR: cannot arm probe (%r)\r\n\r\n", st);
        llmk_oo_journal_event_load_state_best_effort("reboot_probe_arm_failed");
        return;
    }

    st = llmk_file_write_bytes(f, out, (UINTN)p);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || EFI_ERROR(flush_st)) {
        Print(L"\r\n[oo_reboot_probe] ERROR: cannot flush probe (%r/%r)\r\n\r\n", st, flush_st);
        llmk_oo_journal_event_load_state_best_effort("reboot_probe_arm_failed");
        return;
    }

    Print(L"\r\n[oo_reboot_probe] armed.present=0\r\n");
    Print(L"[oo_reboot_probe] armed.boot_count=%lu\r\n", (UINT64)local.boot_count);
    Print(L"[oo_reboot_probe] armed.mode="); llmk_print_ascii(llmk_oo_mode_name_ascii(local.mode)); Print(L"\r\n");
    {
        UINT16 boot_current = 0;
        EFI_STATUS bootnext_st = EFI_NOT_FOUND;
        int bootnext_ok = llmk_best_effort_set_bootnext_to_bootcurrent(&boot_current, &bootnext_st);
        if (bootnext_ok) {
            Print(L"[oo_reboot_probe] bootnext.current=%u\r\n", (unsigned int)boot_current);
            Print(L"[oo_reboot_probe] bootnext.armed=1\r\n");
        } else {
            Print(L"[oo_reboot_probe] bootnext.armed=0 status=%r\r\n", bootnext_st);
        }
    }
    Print(L"[oo_reboot_probe] action=rebooting\r\n\r\n");
    llmk_oo_journal_event_load_state_best_effort("reboot_probe_arm");
    uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
}

static int llmk_best_effort_set_bootnext_to_bootcurrent(UINT16 *boot_current_out, EFI_STATUS *status_out) {
    EFI_GUID global_guid = EFI_GLOBAL_VARIABLE;
    UINT16 boot_current = 0;
    UINTN data_size = sizeof(boot_current);
    UINT32 attrs = 0;
    EFI_STATUS st = uefi_call_wrapper(RT->GetVariable, 5,
                                      L"BootCurrent",
                                      &global_guid,
                                      &attrs,
                                      &data_size,
                                      &boot_current);
    if (boot_current_out) *boot_current_out = 0;
    if (status_out) *status_out = st;
    if (EFI_ERROR(st) || data_size != sizeof(boot_current)) {
        return 0;
    }

    st = uefi_call_wrapper(RT->SetVariable, 5,
                           L"BootNext",
                           &global_guid,
                           EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                           sizeof(boot_current),
                           &boot_current);
    if (boot_current_out) *boot_current_out = boot_current;
    if (status_out) *status_out = st;
    return EFI_ERROR(st) ? 0 : 1;
}

static void llmk_oo_handoff_info_best_effort(const CHAR16 *path) {
    const CHAR16 *load_name = path && path[0] ? path : L"sovereign_export.json";
    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(load_name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[oo_handoff] ERROR: read failed: %s (%r)\r\n\r\n", load_name, st);
        return;
    }

    char *buf = NULL;
    EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
    if (EFI_ERROR(st2) || !buf) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[oo_handoff] ERROR: OOM\r\n\r\n");
        return;
    }
    CopyMem(buf, raw, raw_len);
    buf[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    int schema_version = 0;
    int active_goal_count = 0;
    UINT64 continuity_epoch = 0;
    char export_kind[64]; // SAFE: bounded handoff export kind buffer
    char organism_id[96];
    char mode[64]; // SAFE: bounded handoff mode buffer
    char enforcement[64]; // SAFE: bounded handoff enforcement buffer
    char last_recovery_reason[128];
    int recovery_present = 0;
    export_kind[0] = 0;
    organism_id[0] = 0;
    mode[0] = 0;
    enforcement[0] = 0;
    last_recovery_reason[0] = 0;

    int ok = 1;
    if (!llmk_json_extract_int_key_range(buf, raw_len, "schema_version", &schema_version)) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "export_kind", export_kind, (int)sizeof(export_kind))) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "organism_id", organism_id, (int)sizeof(organism_id))) ok = 0;
    if (!llmk_json_extract_u64_key_range(buf, raw_len, "continuity_epoch", &continuity_epoch)) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "mode", mode, (int)sizeof(mode))) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "enforcement", enforcement, (int)sizeof(enforcement))) ok = 0;
    if (!llmk_json_extract_int_key_range(buf, raw_len, "active_goal_count", &active_goal_count)) ok = 0;
    if (!llmk_json_extract_optional_string_key_range(buf, raw_len, "last_recovery_reason",
                                                     last_recovery_reason, (int)sizeof(last_recovery_reason),
                                                     &recovery_present)) ok = 0;

    if (!ok || schema_version != 1 || my_strncmp(export_kind, "oo_sovereign_handoff", 20) != 0 || export_kind[20] != 0) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"\r\n[oo_handoff] ERROR: invalid or unsupported handoff format\r\n\r\n");
        return;
    }

    LlmkHandoffEvent events[16]; // SAFE: bounded recent-event extraction buffer for handoff info
    int n_events = llmk_json_extract_recent_events(buf, raw_len, events, (int)(sizeof(events) / sizeof(events[0])));

    Print(L"\r\n[oo_handoff] file=%s\r\n", load_name);
    Print(L"[oo_handoff] schema_version=%d\r\n", schema_version);
    Print(L"[oo_handoff] export_kind="); llmk_print_ascii(export_kind); Print(L"\r\n");
    Print(L"[oo_handoff] organism_id="); llmk_print_ascii(organism_id); Print(L"\r\n");
    Print(L"[oo_handoff] continuity_epoch=%lu\r\n", continuity_epoch);
    Print(L"[oo_handoff] mode="); llmk_print_ascii(mode); Print(L"\r\n");
    Print(L"[oo_handoff] last_recovery_reason=");
    if (recovery_present && last_recovery_reason[0]) llmk_print_ascii(last_recovery_reason);
    else Print(L"none");
    Print(L"\r\n");
    Print(L"[oo_handoff] policy.enforcement="); llmk_print_ascii(enforcement); Print(L"\r\n");
    Print(L"[oo_handoff] active_goal_count=%d\r\n", active_goal_count);
    if (n_events > 0) {
        int start = (n_events > 3) ? (n_events - 3) : 0;
        Print(L"[oo_handoff] recent_events:\r\n");
        for (int i = start; i < n_events; i++) {
            Print(L"  - ");
            llmk_print_ascii(events[i].kind);
            Print(L" [");
            llmk_print_ascii(events[i].severity);
            Print(L"] ");
            llmk_print_ascii(events[i].summary);
            Print(L"\r\n");
        }
    } else {
        Print(L"[oo_handoff] recent_events=(none)\r\n");
    }
    Print(L"\r\n");
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_handoff_apply_best_effort(const CHAR16 *path) {
    const CHAR16 *load_name = path && path[0] ? path : L"sovereign_export.json";
    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(load_name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[oo_handoff_apply] ERROR: read failed: %s (%r)\r\n\r\n", load_name, st);
        return;
    }

    char *buf = NULL;
    EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
    if (EFI_ERROR(st2) || !buf) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[oo_handoff_apply] ERROR: OOM\r\n\r\n");
        return;
    }
    CopyMem(buf, raw, raw_len);
    buf[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    int schema_version = 0;
    UINT64 continuity_epoch = 0;
    char export_kind[64];
    char organism_id[96];
    char mode[64];
    char enforcement[64];
    char last_recovery_reason[128];
    int recovery_present = 0;
    export_kind[0] = 0;
    organism_id[0] = 0;
    mode[0] = 0;
    enforcement[0] = 0;
    last_recovery_reason[0] = 0;

    int ok = 1;
    if (!llmk_json_extract_int_key_range(buf, raw_len, "schema_version", &schema_version)) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "export_kind", export_kind, (int)sizeof(export_kind))) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "organism_id", organism_id, (int)sizeof(organism_id))) ok = 0;
    if (!llmk_json_extract_u64_key_range(buf, raw_len, "continuity_epoch", &continuity_epoch)) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "mode", mode, (int)sizeof(mode))) ok = 0;
    if (!llmk_json_extract_string_key_range(buf, raw_len, "enforcement", enforcement, (int)sizeof(enforcement))) ok = 0;
    if (!llmk_json_extract_optional_string_key_range(buf, raw_len, "last_recovery_reason",
                                                     last_recovery_reason, (int)sizeof(last_recovery_reason),
                                                     &recovery_present)) ok = 0;

    if (!ok || schema_version != 1 || my_strncmp(export_kind, "oo_sovereign_handoff", 20) != 0 || export_kind[20] != 0) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"\r\n[oo_handoff_apply] ERROR: invalid or unsupported handoff format\r\n\r\n");
        return;
    }

    UINT32 host_mode = LLMK_OO_MODE_SAFE;
    DjibionMode host_policy = DJIBION_MODE_OFF;
    if (!llmk_oo_mode_from_ascii(mode, &host_mode)) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"\r\n[oo_handoff_apply] ERROR: unsupported mode='");
        llmk_print_ascii(mode);
        Print(L"'\r\n\r\n");
        return;
    }
    if (!llmk_djibion_mode_from_ascii(enforcement, &host_policy)) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"\r\n[oo_handoff_apply] ERROR: unsupported policy.enforcement='");
        llmk_print_ascii(enforcement);
        Print(L"'\r\n\r\n");
        return;
    }

    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"\r\n[oo_handoff_apply] ERROR: local OO state unavailable\r\n\r\n");
        return;
    }

    UINT32 local_mode_before = s.mode;
    DjibionMode local_policy_before = g_djibion.mode;
    int mode_changed = 0;
    int mode_kept_safer = 0;
    EFI_STATUS write_st = EFI_SUCCESS;
    EFI_STATUS receipt_st = EFI_SUCCESS;

    if (host_mode > s.mode) {
        s.mode = host_mode;
        s.magic = LLMK_OO_STATE_MAGIC;
        s.version = LLMK_OO_STATE_VER;
        s.size = (UINT32)sizeof(LlmkOoState);
        s.checksum = llmk_oo_state_checksum(&s);
        write_st = llmk_oo_write_state_best_effort(&s);
        if (!EFI_ERROR(write_st)) {
            (void)llmk_oo_write_recovery_best_effort(&s);
            g_oo_last_mode = s.mode;
            g_oo_last_mode_valid = 1;
            mode_changed = 1;
            llmk_oo_journal_append_best_effort(&s, "handoff_apply_mode_raise");
        }
    } else if (host_mode < s.mode) {
        mode_kept_safer = 1;
    }

    int policy_changed = 0;
    int policy_kept_stricter = 0;
    if ((int)host_policy > (int)g_djibion.mode) {
        djibion_set_mode(&g_djibion, host_policy);
        policy_changed = 1;
        llmk_oo_journal_event_load_state_best_effort("handoff_apply_policy_raise");
    } else if ((int)host_policy < (int)g_djibion.mode) {
        policy_kept_stricter = 1;
    }

    receipt_st = llmk_oo_write_handoff_receipt_best_effort(organism_id, mode, enforcement,
                                                           continuity_epoch, recovery_present,
                                                           last_recovery_reason);
    llmk_oo_journal_event_load_state_best_effort("handoff_apply_continuity_seen");
    if (recovery_present && last_recovery_reason[0]) {
        llmk_oo_journal_event_load_state_best_effort("handoff_apply_recovery_seen");
    }

    Print(L"\r\n[oo_handoff_apply] file=%s\r\n", load_name);
    Print(L"[oo_handoff_apply] organism_id="); llmk_print_ascii(organism_id); Print(L"\r\n");
    Print(L"[oo_handoff_apply] host.continuity_epoch=%lu\r\n", continuity_epoch);
    Print(L"[oo_handoff_apply] host.mode="); llmk_print_ascii(mode); Print(L"\r\n");
    Print(L"[oo_handoff_apply] host.last_recovery_reason=");
    if (recovery_present && last_recovery_reason[0]) llmk_print_ascii(last_recovery_reason);
    else Print(L"none");
    Print(L"\r\n");
    Print(L"[oo_handoff_apply] local.mode.before="); llmk_print_ascii(llmk_oo_mode_name_ascii(local_mode_before)); Print(L"\r\n");
    if (mode_changed) {
        Print(L"[oo_handoff_apply] mode_result=applied ");
        llmk_print_ascii(llmk_oo_mode_name_ascii(s.mode));
        Print(L"\r\n");
    } else if (EFI_ERROR(write_st)) {
        Print(L"[oo_handoff_apply] mode_result=write_failed (%r)\r\n", write_st);
    } else if (mode_kept_safer) {
        Print(L"[oo_handoff_apply] mode_result=kept_local_safer ");
        llmk_print_ascii(llmk_oo_mode_name_ascii(local_mode_before));
        Print(L"\r\n");
    } else {
        Print(L"[oo_handoff_apply] mode_result=unchanged ");
        llmk_print_ascii(llmk_oo_mode_name_ascii(local_mode_before));
        Print(L"\r\n");
    }

    Print(L"[oo_handoff_apply] host.policy.enforcement="); llmk_print_ascii(enforcement); Print(L"\r\n");
    Print(L"[oo_handoff_apply] local.policy.before="); llmk_print_ascii(llmk_djibion_mode_name_ascii(local_policy_before)); Print(L"\r\n");
    if (policy_changed) {
        Print(L"[oo_handoff_apply] policy_result=applied ");
        llmk_print_ascii(llmk_djibion_mode_name_ascii(g_djibion.mode));
        Print(L"\r\n");
    } else if (policy_kept_stricter) {
        Print(L"[oo_handoff_apply] policy_result=kept_local_stricter ");
        llmk_print_ascii(llmk_djibion_mode_name_ascii(local_policy_before));
        Print(L"\r\n");
    } else {
        Print(L"[oo_handoff_apply] policy_result=unchanged ");
        llmk_print_ascii(llmk_djibion_mode_name_ascii(local_policy_before));
        Print(L"\r\n");
    }
    if (EFI_ERROR(receipt_st)) {
        Print(L"[oo_handoff_apply] continuity_result=receipt_write_failed (%r)\r\n", receipt_st);
    } else {
        Print(L"[oo_handoff_apply] continuity_result=recorded\r\n");
    }
    Print(L"\r\n");

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

