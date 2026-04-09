static void llmk_mind_release_sidecar_blob(void) {
    if (g_mind_sidecar_blob) {
        uefi_call_wrapper(BS->FreePool, 1, g_mind_sidecar_blob);
        g_mind_sidecar_blob = NULL;
    }
    g_mind_sidecar_blob_len = 0;
}

static void llmk_mind_clear_halting_view(void) {
    SetMem(&g_mind_halting_view, sizeof(g_mind_halting_view), 0);
}

static void llmk_mind_clear_core(void) {
    g_mind_runtime_state.core_requested = 0;
    g_mind_runtime_state.core_active = 0;
    g_mind_runtime_state.core_path[0] = 0;
    g_mind_runtime_state.core_kind[0] = 0;
    g_mind_runtime_state.sidecar_active = 0;
}

static void llmk_mind_clear_sidecar(void) {
    llmk_mind_release_sidecar_blob();
    llmk_mind_clear_halting_view();
    g_mind_runtime_state.sidecar_requested = 0;
    g_mind_runtime_state.sidecar_active = 0;
    g_mind_runtime_state.sidecar_path[0] = 0;
    g_mind_runtime_state.sidecar_kind[0] = 0;
    g_mind_runtime_state.sidecar_version = 0;
    g_mind_runtime_state.sidecar_d_model = 0;
    g_mind_runtime_state.sidecar_n_layer = 0;
    g_mind_runtime_state.sidecar_d_state = 0;
    g_mind_runtime_state.sidecar_d_conv = 0;
    g_mind_runtime_state.sidecar_expand = 0;
    g_mind_runtime_state.sidecar_vocab_size = 0;
    g_mind_runtime_state.sidecar_halting_d_input = 0;
    g_mind_runtime_state.sidecar_header_valid = 0;
    g_mind_runtime_state.sidecar_last_validation[0] = 0;
}

static void llmk_mind_clear_attach(void) {
    g_mind_runtime_state.attach_requested = 0;
    g_mind_runtime_state.attach_active = 0;
    g_mind_runtime_state.attach_path[0] = 0;
    g_mind_runtime_state.attach_kind[0] = 0;
    g_mind_runtime_state.attach_format[0] = 0;
    g_mind_runtime_state.attach_last_validation[0] = 0;
}

static void llmk_mind_set_core_request(const char *path, const char *kind) {
    llmk_copy_ascii_bounded(g_mind_runtime_state.core_path, (int)sizeof(g_mind_runtime_state.core_path), path);
    llmk_copy_ascii_bounded(g_mind_runtime_state.core_kind, (int)sizeof(g_mind_runtime_state.core_kind), kind ? kind : "somamind-core");
    g_mind_runtime_state.core_requested = (path && path[0]) ? 1 : 0;
    g_mind_runtime_state.core_active = 0;
}

static void llmk_mind_mark_core_active(const char *path, const char *kind) {
    llmk_mind_set_core_request(path, kind);
    g_mind_runtime_state.core_active = g_mind_runtime_state.core_requested;
    if (g_mind_runtime_state.sidecar_requested) {
        g_mind_runtime_state.sidecar_active = 0;
    }
}

static void llmk_mind_set_sidecar_request(const char *path, const char *kind) {
    llmk_copy_ascii_bounded(g_mind_runtime_state.sidecar_path, (int)sizeof(g_mind_runtime_state.sidecar_path), path);
    llmk_copy_ascii_bounded(g_mind_runtime_state.sidecar_kind, (int)sizeof(g_mind_runtime_state.sidecar_kind), kind ? kind : "ooss-sidecar");
    g_mind_runtime_state.sidecar_requested = (path && path[0]) ? 1 : 0;
    g_mind_runtime_state.sidecar_active = 0;
}

static void llmk_mind_mark_sidecar_active(UINTN blob_len) {
    g_mind_runtime_state.sidecar_active = g_mind_runtime_state.sidecar_requested ? 1 : 0;
    g_mind_sidecar_blob_len = blob_len;
}

static UINT32 llmk_u32le_read(const UINT8 *p) {
    if (!p) return 0;
    return ((UINT32)p[0]) |
           ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static int llmk_ooss_parse_header(const void *buf, UINTN len, LlmkOoSidecarHeader *out) {
    if (!buf || !out || len < 36) return 0;
    const UINT8 *p = (const UINT8 *)buf;
    out->magic = llmk_u32le_read(p + 0);
    out->version = llmk_u32le_read(p + 4);
    out->d_model = llmk_u32le_read(p + 8);
    out->n_layer = llmk_u32le_read(p + 12);
    out->d_state = llmk_u32le_read(p + 16);
    out->d_conv = llmk_u32le_read(p + 20);
    out->expand = llmk_u32le_read(p + 24);
    out->vocab_size = llmk_u32le_read(p + 28);
    out->halting_head_d_input = llmk_u32le_read(p + 32);
    return out->magic == LLMK_OOSS_MAGIC;
}

static UINT64 llmk_ooss_expected_halting_offset_bytes(const LlmkOoSidecarHeader *hdr) {
    if (!hdr) return 0;
    UINT64 d_model = (UINT64)hdr->d_model;
    UINT64 n_layer = (UINT64)hdr->n_layer;
    UINT64 d_state = (UINT64)hdr->d_state;
    UINT64 expand = (UINT64)hdr->expand;
    UINT64 vocab_size = (UINT64)hdr->vocab_size;
    UINT64 d_inner = d_model * expand;
    UINT64 dt_rank = (d_model + 15ULL) / 16ULL;
    if (dt_rank == 0) dt_rank = 1;

    UINT64 per_layer = ((dt_rank + 2ULL * d_state) * d_inner);
    per_layer += (d_inner * dt_rank);
    per_layer += d_inner;
    per_layer *= 4ULL;

    return 36ULL + (n_layer * per_layer) + (vocab_size * d_model * 4ULL);
}

static int llmk_ooss_bind_halting_view(const LlmkOoSidecarHeader *hdr, const void *blob, UINTN blob_len) {
    llmk_mind_clear_halting_view();
    if (!hdr || !blob) return 0;

    UINT64 offset = llmk_ooss_expected_halting_offset_bytes(hdr);
    UINT64 d_input = (UINT64)hdr->halting_head_d_input;
    UINT64 hidden0 = 512ULL;
    UINT64 hidden1 = 64ULL;
    UINT64 d_output = 1ULL;

    UINT64 needed = offset;
    needed += hidden0 * d_input * 4ULL;
    needed += hidden0 * 4ULL;
    needed += hidden1 * hidden0 * 4ULL;
    needed += hidden1 * 4ULL;
    needed += d_output * hidden1 * 4ULL;
    needed += d_output * 4ULL;
    if (needed > (UINT64)blob_len) return 0;

    const UINT8 *base = (const UINT8 *)blob;
    const UINT8 *p = base + offset;
    g_mind_halting_view.layer0_weight = (const float *)p; p += hidden0 * d_input * 4ULL;
    g_mind_halting_view.layer0_bias   = (const float *)p; p += hidden0 * 4ULL;
    g_mind_halting_view.layer2_weight = (const float *)p; p += hidden1 * hidden0 * 4ULL;
    g_mind_halting_view.layer2_bias   = (const float *)p; p += hidden1 * 4ULL;
    g_mind_halting_view.layer4_weight = (const float *)p; p += d_output * hidden1 * 4ULL;
    g_mind_halting_view.layer4_bias   = (const float *)p;
    g_mind_halting_view.d_input = (UINT32)d_input;
    g_mind_halting_view.hidden0 = (UINT32)hidden0;
    g_mind_halting_view.hidden1 = (UINT32)hidden1;
    g_mind_halting_view.d_output = (UINT32)d_output;
    g_mind_halting_view.ready = 1;
    return 1;
}

static void llmk_mind_store_sidecar_header(const LlmkOoSidecarHeader *hdr) {
    if (!hdr) return;
    g_mind_runtime_state.sidecar_version = hdr->version;
    g_mind_runtime_state.sidecar_d_model = hdr->d_model;
    g_mind_runtime_state.sidecar_n_layer = hdr->n_layer;
    g_mind_runtime_state.sidecar_d_state = hdr->d_state;
    g_mind_runtime_state.sidecar_d_conv = hdr->d_conv;
    g_mind_runtime_state.sidecar_expand = hdr->expand;
    g_mind_runtime_state.sidecar_vocab_size = hdr->vocab_size;
    g_mind_runtime_state.sidecar_halting_d_input = hdr->halting_head_d_input;
    g_mind_runtime_state.sidecar_header_valid = 1;
}

static void llmk_mind_set_sidecar_validation(const char *msg) {
    llmk_copy_ascii_bounded(
        g_mind_runtime_state.sidecar_last_validation,
        (int)sizeof(g_mind_runtime_state.sidecar_last_validation),
        msg ? msg : ""
    );
}

static void llmk_mind_set_attach_request(const char *path, const char *kind) {
    llmk_copy_ascii_bounded(g_mind_runtime_state.attach_path, (int)sizeof(g_mind_runtime_state.attach_path), path);
    llmk_copy_ascii_bounded(g_mind_runtime_state.attach_kind, (int)sizeof(g_mind_runtime_state.attach_kind), kind ? kind : "attach-model");
    g_mind_runtime_state.attach_format[0] = 0;
    g_mind_runtime_state.attach_requested = (path && path[0]) ? 1 : 0;
    g_mind_runtime_state.attach_active = 0;
    llmk_copy_ascii_bounded(g_mind_runtime_state.attach_last_validation,
                            (int)sizeof(g_mind_runtime_state.attach_last_validation),
                            g_mind_runtime_state.attach_requested ? "requested-not-validated" : "");
}

static void llmk_mind_set_attach_validation(const char *msg) {
    llmk_copy_ascii_bounded(
        g_mind_runtime_state.attach_last_validation,
        (int)sizeof(g_mind_runtime_state.attach_last_validation),
        msg ? msg : ""
    );
}

static int llmk_ascii_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void llmk_mind_store_attach_format(LlmkModelFormat fmt) {
    const char *fmt_s = "unknown";
    if (fmt == LLMK_MODEL_FMT_GGUF) fmt_s = "gguf";
    else if (fmt == LLMK_MODEL_FMT_BIN) fmt_s = "bin";
    llmk_copy_ascii_bounded(g_mind_runtime_state.attach_format,
                            (int)sizeof(g_mind_runtime_state.attach_format),
                            fmt_s);
}

static void llmk_mind_mark_attach_active(LlmkModelFormat fmt) {
    g_mind_runtime_state.attach_active = g_mind_runtime_state.attach_requested ? 1 : 0;
    llmk_copy_ascii_bounded(g_mind_runtime_state.attach_kind,
                            (int)sizeof(g_mind_runtime_state.attach_kind),
                            (fmt == LLMK_MODEL_FMT_GGUF) ? "attach-gguf" :
                            (fmt == LLMK_MODEL_FMT_BIN) ? "attach-bin" : "attach-model");
    llmk_mind_store_attach_format(fmt);
    llmk_mind_set_attach_validation("validated");
}

/* Forward declarations for mind halt policy helpers (defined later) */
static EFI_STATUS llmk_mind_persist_halt_policy_best_effort(void);
static EFI_STATUS llmk_mind_query_halt_policy_cfg_best_effort(
    int *out_enabled, float *out_threshold,
    int *out_found_enabled, int *out_found_threshold);
static EFI_STATUS llmk_mind_load_halt_policy_best_effort(void);
static EFI_STATUS llmk_mind_apply_saved_halt_policy_best_effort(int *out_changed_enabled, int *out_changed_threshold);
static EFI_STATUS llmk_mind_apply_saved_halt_policy_if_needed_best_effort(int *out_was_needed, int *out_changed_enabled, int *out_changed_threshold);
static EFI_STATUS llmk_attach_route_policy_query_cfg_best_effort(
    LlmkAttachRoutePolicyConfig *out_external,
    LlmkAttachRoutePolicyConfig *out_dual,
    int *out_found_external_fields,
    int *out_found_dual_fields);
static void llmk_attach_route_policy_get_default(SomaRoute route, LlmkAttachRoutePolicyConfig *out);
static void llmk_attach_route_policy_preview(SomaRoute route, LlmkAttachRoutePolicyPreview *out);
static EFI_STATUS llmk_attach_route_policy_query_sync_state_best_effort(
    LlmkAttachRoutePolicyConfig *out_external,
    LlmkAttachRoutePolicyConfig *out_dual,
    int *out_found_external_fields,
    int *out_found_dual_fields,
    int *out_found_any,
    int *out_in_sync);
static EFI_STATUS llmk_attach_route_policy_load_best_effort(int *out_changed_external, int *out_changed_dual);
static EFI_STATUS llmk_attach_route_policy_apply_saved_if_needed_best_effort(int *out_was_needed, int *out_changed_external, int *out_changed_dual);
static EFI_STATUS llmk_read_entire_file_best_effort(const CHAR16 *name, void **out_buf, UINTN *out_len);
static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name);
static void llmk_mind_bind_core_backbone_v1(const char *path, int via_core_alias);
static EFI_STATUS llmk_mind_register_sidecar_best_effort(const char *path, LlmkOoSidecarHeader *out_hdr, UINTN *out_raw_len);
static EFI_STATUS llmk_mind_activate_attach_best_effort(const char *path, LlmkModelFormat *out_fmt);
static void llmk_mind_collect_runtime_snapshot(LlmkMindRuntimeSnapshot *out);
static void llmk_mind_select_next_action_from_snapshot(const LlmkMindRuntimeSnapshot *snapshot, const CHAR16 **out_action, const CHAR16 **out_reason);
static void llmk_mind_select_next_action(const CHAR16 **out_action, const CHAR16 **out_reason, int *out_ready, int *out_core_ready, int *out_halt_ready, int *out_sidecar_ready);
static const char *llmk_model_format_ascii(LlmkModelFormat fmt);
static void llmk_attach_route_policy_print_audit(void);
static void llmk_attach_route_policy_print_diff(void);
static void llmk_attach_route_policy_print_apply_mode(LlmkAttachPolicyApplyMode mode);
static void llmk_attach_route_policy_print_apply_effect(void);
static void llmk_attach_route_policy_print_apply_command_result(LlmkAttachPolicyApplyMode mode, int was_needed, int changed_external, int changed_dual);

static void llmk_mind_record_halt_apply(LlmkMindHaltApplyMode mode, int changed_enabled, int changed_threshold) {
    g_mind_runtime_halt_apply_seen = 1;
    g_mind_runtime_halt_apply_mode = mode;
    g_mind_runtime_halt_apply_changed_enabled = changed_enabled ? 1 : 0;
    g_mind_runtime_halt_apply_changed_threshold = changed_threshold ? 1 : 0;
}

static EFI_STATUS llmk_mind_query_halt_policy_sync_state_best_effort(
    int *out_enabled,
    float *out_threshold,
    int *out_found_enabled,
    int *out_found_threshold,
    int *out_found_any,
    int *out_in_sync
) {
    int cfg_enabled = g_mind_runtime_halt_enabled;
    float cfg_threshold = g_mind_runtime_halt_threshold;
    int found_enabled = 0;
    int found_threshold = 0;
    EFI_STATUS st = llmk_mind_query_halt_policy_cfg_best_effort(&cfg_enabled, &cfg_threshold, &found_enabled, &found_threshold);

    if (out_found_enabled) *out_found_enabled = found_enabled;
    if (out_found_threshold) *out_found_threshold = found_threshold;

    if (EFI_ERROR(st) || (!found_enabled && !found_threshold)) {
        if (out_found_any) *out_found_any = 0;
        if (out_in_sync) *out_in_sync = 0;
        return st;
    }

    {
        int eff_enabled = found_enabled ? cfg_enabled : g_mind_runtime_halt_enabled;
        float eff_threshold = found_threshold ? cfg_threshold : g_mind_runtime_halt_threshold;
        float diff = eff_threshold - g_mind_runtime_halt_threshold;
        if (diff < 0.0f) diff = -diff;
        if (out_enabled) *out_enabled = eff_enabled;
        if (out_threshold) *out_threshold = eff_threshold;
        if (out_found_any) *out_found_any = 1;
        if (out_in_sync) *out_in_sync = (eff_enabled == g_mind_runtime_halt_enabled && diff < 0.0005f) ? 1 : 0;
    }
    return EFI_SUCCESS;
}

static void llmk_mind_collect_runtime_snapshot(LlmkMindRuntimeSnapshot *out) {
    LlmkMindRuntimeSnapshot snapshot;
    SetMem(&snapshot, sizeof(snapshot), 0);

    snapshot.cfg_enabled = g_mind_runtime_halt_enabled;
    snapshot.cfg_threshold = g_mind_runtime_halt_threshold;
    snapshot.cfg_st = llmk_mind_query_halt_policy_sync_state_best_effort(
        &snapshot.cfg_enabled,
        &snapshot.cfg_threshold,
        &snapshot.found_enabled,
        &snapshot.found_threshold,
        &snapshot.found_any,
        &snapshot.in_sync
    );

    snapshot.core_ready = g_mind_runtime_state.core_active ? 1 : 0;
    snapshot.halt_ready = (!EFI_ERROR(snapshot.cfg_st) && snapshot.found_any && snapshot.in_sync) ? 1 : 0;
    snapshot.sidecar_ready = (!g_mind_runtime_state.sidecar_requested) ||
                             (g_mind_runtime_state.sidecar_active && g_mind_runtime_state.sidecar_header_valid && g_mind_halting_view.ready);
    snapshot.ready = (snapshot.core_ready && snapshot.halt_ready && snapshot.sidecar_ready) ? 1 : 0;
    if ((g_mind_runtime_state.core_requested && !g_mind_runtime_state.core_active && g_mind_runtime_state.core_path[0]) ||
        (EFI_ERROR(snapshot.cfg_st) || !snapshot.found_any || !snapshot.in_sync) ||
        (g_mind_runtime_state.sidecar_requested && !g_mind_runtime_state.sidecar_active && g_mind_runtime_state.sidecar_path[0]) ||
        (g_mind_runtime_state.attach_requested && !g_mind_runtime_state.attach_active &&
         g_mind_runtime_state.attach_path[0] &&
         !llmk_ascii_streq(g_mind_runtime_state.attach_last_validation, "validation-failed"))) {
        snapshot.bootstrap_can_help = 1;
    }

    if (out) *out = snapshot;
}

static void llmk_mind_print_halt_apply_mode(LlmkMindHaltApplyMode mode) {
    switch (mode) {
        case LLMK_MIND_HALT_APPLY_SAVED: Print(L"apply_saved"); break;
        case LLMK_MIND_HALT_APPLY_SAVED_IF_NEEDED: Print(L"apply_saved_if_needed"); break;
        case LLMK_MIND_HALT_APPLY_SYNC: Print(L"sync"); break;
        case LLMK_MIND_HALT_APPLY_SYNC_FORCE: Print(L"sync_force"); break;
        default: Print(L"never"); break;
    }
}

static void llmk_mind_print_halt_apply_effect(void) {
    if (g_mind_runtime_halt_apply_changed_enabled || g_mind_runtime_halt_apply_changed_threshold) Print(L"runtime-updated");
    else if (g_mind_runtime_halt_apply_mode == LLMK_MIND_HALT_APPLY_SYNC_FORCE) Print(L"forced-reload-no-delta");
    else Print(L"no-op-already-in-sync");
}

static void llmk_attach_route_policy_record_apply(LlmkAttachPolicyApplyMode mode, int changed_external, int changed_dual) {
    g_attach_policy_apply_seen = 1;
    g_attach_policy_apply_mode = mode;
    g_attach_policy_apply_changed_external = changed_external ? 1 : 0;
    g_attach_policy_apply_changed_dual = changed_dual ? 1 : 0;
}

static void llmk_attach_route_policy_print_apply_mode(LlmkAttachPolicyApplyMode mode) {
    switch (mode) {
        case LLMK_ATTACH_POLICY_APPLY_SYNC: Print(L"sync"); break;
        case LLMK_ATTACH_POLICY_APPLY_SYNC_FORCE: Print(L"sync_force"); break;
        default: Print(L"never"); break;
    }
}

static void llmk_attach_route_policy_print_apply_effect(void) {
    if (g_attach_policy_apply_changed_external || g_attach_policy_apply_changed_dual) Print(L"runtime-updated");
    else if (g_attach_policy_apply_mode == LLMK_ATTACH_POLICY_APPLY_SYNC_FORCE) Print(L"forced-reload-no-delta");
    else Print(L"no-op-already-in-sync");
}

static void llmk_attach_route_policy_print_apply_command_result(LlmkAttachPolicyApplyMode mode, int was_needed, int changed_external, int changed_dual) {
    llmk_attach_route_policy_record_apply(mode, changed_external, changed_dual);

    if (mode == LLMK_ATTACH_POLICY_APPLY_SYNC) {
        if (!was_needed) {
            Print(L"\r\n[AttachPolicy] sync skipped; runtime already matched repl.cfg changed.external=%d changed.dual=%d sync=in-sync\r\n\r\n",
                  changed_external, changed_dual);
        } else {
            Print(L"\r\n[AttachPolicy] sync applied from repl.cfg changed.external=%d changed.dual=%d sync=in-sync\r\n\r\n",
                  changed_external, changed_dual);
        }
        return;
    }

    if (mode == LLMK_ATTACH_POLICY_APPLY_SYNC_FORCE) {
        Print(L"\r\n[AttachPolicy] forced sync reloaded repl.cfg changed.external=%d changed.dual=%d sync=in-sync\r\n\r\n",
              changed_external, changed_dual);
    }
}

static void llmk_mind_print_apply_command_result(LlmkMindHaltApplyMode mode, int was_needed, int changed_enabled, int changed_threshold) {
    llmk_mind_record_halt_apply(mode, changed_enabled, changed_threshold);

    if (mode == LLMK_MIND_HALT_APPLY_SAVED) {
        Print(L"\r\n[MindHaltPolicy] saved policy applied enabled=%d threshold=%d.%03d changed.enabled=%d changed.threshold=%d sync=in-sync\r\n\r\n",
              g_mind_runtime_halt_enabled,
              (int)g_mind_runtime_halt_threshold,
              (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f),
              changed_enabled,
              changed_threshold);
        return;
    }

    if (mode == LLMK_MIND_HALT_APPLY_SAVED_IF_NEEDED) {
        if (!was_needed) {
            Print(L"\r\n[MindHaltPolicy] saved policy already matched runtime; no apply needed enabled=%d threshold=%d.%03d sync=in-sync\r\n\r\n",
                  g_mind_runtime_halt_enabled,
                  (int)g_mind_runtime_halt_threshold,
                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
        } else {
            Print(L"\r\n[MindHaltPolicy] saved policy conditionally applied enabled=%d threshold=%d.%03d changed.enabled=%d changed.threshold=%d sync=in-sync\r\n\r\n",
                  g_mind_runtime_halt_enabled,
                  (int)g_mind_runtime_halt_threshold,
                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f),
                  changed_enabled,
                  changed_threshold);
        }
        return;
    }

    if (mode == LLMK_MIND_HALT_APPLY_SYNC) {
        if (!was_needed) {
            Print(L"\r\n[MindHaltPolicy] sync skipped; runtime already matched repl.cfg enabled=%d threshold=%d.%03d sync=in-sync\r\n\r\n",
                  g_mind_runtime_halt_enabled,
                  (int)g_mind_runtime_halt_threshold,
                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
        } else {
            Print(L"\r\n[MindHaltPolicy] sync applied from repl.cfg enabled=%d threshold=%d.%03d changed.enabled=%d changed.threshold=%d sync=in-sync\r\n\r\n",
                  g_mind_runtime_halt_enabled,
                  (int)g_mind_runtime_halt_threshold,
                  (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f),
                  changed_enabled,
                  changed_threshold);
        }
        return;
    }

    if (mode == LLMK_MIND_HALT_APPLY_SYNC_FORCE) {
        Print(L"\r\n[MindHaltPolicy] forced sync reloaded repl.cfg enabled=%d threshold=%d.%03d changed.enabled=%d changed.threshold=%d sync=in-sync\r\n\r\n",
              g_mind_runtime_halt_enabled,
              (int)g_mind_runtime_halt_threshold,
              (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f),
              changed_enabled,
              changed_threshold);
    }
}

static void llmk_mind_print_halt_policy_audit(void) {
    int persisted_enabled = g_mind_runtime_halt_enabled;
    float persisted_threshold = g_mind_runtime_halt_threshold;
    int found_enabled = 0;
    int found_threshold = 0;
    int found_any = 0;
    int in_sync = 0;
    EFI_STATUS st = llmk_mind_query_halt_policy_sync_state_best_effort(
        &persisted_enabled,
        &persisted_threshold,
        &found_enabled,
        &found_threshold,
        &found_any,
        &in_sync);

    Print(L"\r\n[MindHaltPolicyAudit]\r\n");
    Print(L"  runtime.enabled=%d runtime.threshold=%d.%03d\r\n",
          g_mind_runtime_halt_enabled,
          (int)g_mind_runtime_halt_threshold,
          (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));

    if (EFI_ERROR(st) || !found_any) {
        Print(L"  persisted=(not found in repl.cfg)\r\n");
        Print(L"  sync=unknown\r\n");
        Print(L"  suggested_action=/mind_halt_policy_save\r\n");
    } else {
        Print(L"  persisted.enabled=%d persisted.threshold=%d.%03d\r\n",
              persisted_enabled,
              (int)persisted_threshold,
              (int)((persisted_threshold >= 0.0f ? persisted_threshold - (int)persisted_threshold : ((int)persisted_threshold - persisted_threshold)) * 1000.0f));
        Print(L"  sync=");
        if (in_sync) Print(L"in-sync\r\n");
        else Print(L"runtime!=repl.cfg\r\n");
        Print(L"  suggested_action=");
        if (in_sync) Print(L"already-synchronized\r\n");
        else Print(L"/mind_halt_policy_sync\r\n");
    }

    Print(L"  last_apply=");
    if (!g_mind_runtime_halt_apply_seen) {
        Print(L"never\r\n\r\n");
        return;
    }
    llmk_mind_print_halt_apply_mode(g_mind_runtime_halt_apply_mode);
    Print(L" effect=");
    llmk_mind_print_halt_apply_effect();
    Print(L" changed.enabled=%d changed.threshold=%d\r\n\r\n",
          g_mind_runtime_halt_apply_changed_enabled,
          g_mind_runtime_halt_apply_changed_threshold);
}

static void llmk_mind_print_sidecar_audit(void) {
    Print(L"\r\n[MindSidecarAudit]\r\n");
    Print(L"  requested=%d active=%d\r\n",
          g_mind_runtime_state.sidecar_requested,
          g_mind_runtime_state.sidecar_active);

    if (!g_mind_runtime_state.sidecar_requested) {
        Print(L"  sidecar=none\r\n");
        Print(L"  suggested_action=/oo_sidecar <file.ooss>\r\n\r\n");
        return;
    }

    Print(L"  kind="); llmk_print_ascii(g_mind_runtime_state.sidecar_kind[0] ? g_mind_runtime_state.sidecar_kind : "ooss-sidecar"); Print(L"\r\n");
    Print(L"  path="); llmk_print_ascii(g_mind_runtime_state.sidecar_path); Print(L"\r\n");
    Print(L"  bytes=%lu\r\n", (UINT64)g_mind_sidecar_blob_len);
    Print(L"  header_valid=%d\r\n", g_mind_runtime_state.sidecar_header_valid);
    if (g_mind_runtime_state.sidecar_last_validation[0]) {
        Print(L"  validation="); llmk_print_ascii(g_mind_runtime_state.sidecar_last_validation); Print(L"\r\n");
    }
    if (g_mind_runtime_state.sidecar_header_valid) {
        Print(L"  header.version=%u\r\n", g_mind_runtime_state.sidecar_version);
        Print(L"  header.d_model=%u header.n_layer=%u\r\n",
              g_mind_runtime_state.sidecar_d_model,
              g_mind_runtime_state.sidecar_n_layer);
        Print(L"  header.d_state=%u header.d_conv=%u expand=%u\r\n",
              g_mind_runtime_state.sidecar_d_state,
              g_mind_runtime_state.sidecar_d_conv,
              g_mind_runtime_state.sidecar_expand);
        Print(L"  header.vocab=%u halt_in=%u\r\n",
              g_mind_runtime_state.sidecar_vocab_size,
              g_mind_runtime_state.sidecar_halting_d_input);
    }
    Print(L"  halting_hook=");
    if (g_mind_halting_view.ready) {
        Print(L"ready in=%u h0=%u h1=%u out=%u\r\n",
              g_mind_halting_view.d_input,
              g_mind_halting_view.hidden0,
              g_mind_halting_view.hidden1,
              g_mind_halting_view.d_output);
    } else {
        Print(L"not-ready\r\n");
    }
    Print(L"  core_link=");
    if (g_mind_runtime_state.core_active) Print(L"core-active\r\n");
    else if (g_mind_runtime_state.core_requested) Print(L"core-requested-not-active\r\n");
    else Print(L"no-core-bound\r\n");
    Print(L"  suggested_action=");
    if (!g_mind_runtime_state.sidecar_header_valid) Print(L"replace-or-reload-sidecar\r\n");
    else if (!g_mind_halting_view.ready) Print(L"inspect-sidecar-layout\r\n");
    else if (!g_mind_runtime_state.core_active) Print(L"/core_load <file.mamb>\r\n");
    else Print(L"sidecar-ready-for-v1-halting\r\n");
    Print(L"\r\n");
}

static void llmk_mind_print_attach_audit(void) {
    Print(L"\r\n[MindAttachAudit]\r\n");
    Print(L"  requested=%d active=%d\r\n",
          g_mind_runtime_state.attach_requested,
          g_mind_runtime_state.attach_active);

    if (!g_mind_runtime_state.attach_requested) {
        Print(L"  attach=none\r\n");
        Print(L"  suggested_action=/attach_load <file>\r\n");
        Print(L"  rule=attach-optional-core-primary\r\n\r\n");
        return;
    }

    Print(L"  kind="); llmk_print_ascii(g_mind_runtime_state.attach_kind[0] ? g_mind_runtime_state.attach_kind : "attach-model"); Print(L"\r\n");
    Print(L"  format="); llmk_print_ascii(g_mind_runtime_state.attach_format[0] ? g_mind_runtime_state.attach_format : "unknown"); Print(L"\r\n");
    Print(L"  path="); llmk_print_ascii(g_mind_runtime_state.attach_path); Print(L"\r\n");
    Print(L"  backend=");
    if (g_mind_runtime_state.attach_active) Print(L"validated-attach-backend\r\n");
    else Print(L"registered-not-active\r\n");
    Print(L"  validation="); llmk_print_ascii(g_mind_runtime_state.attach_last_validation[0] ? g_mind_runtime_state.attach_last_validation : "not-recorded"); Print(L"\r\n");
    Print(L"  core_link=");
    if (g_mind_runtime_state.core_active) Print(L"core-active-attach-secondary\r\n");
    else if (g_mind_runtime_state.core_requested) Print(L"core-requested-not-active\r\n");
    else Print(L"no-core-bound-attach-alone\r\n");
    Print(L"  suggested_action=");
    if (!g_mind_runtime_state.core_active) Print(L"/core_load <file.mamb>\r\n");
    else if (!g_mind_runtime_state.attach_active) Print(L"/attach_load <file>\r\n");
    else Print(L"attach-ready\r\n");
    Print(L"  rule=attach-must-not-redefine-oo-core\r\n\r\n");
}

static void llmk_mind_print_global_audit(void) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    LlmkMindRuntimeSnapshot snapshot;
    llmk_mind_collect_runtime_snapshot(&snapshot);
    llmk_mind_select_next_action_from_snapshot(&snapshot, &next_action, &next_reason);

    Print(L"\r\n[MindAudit]\r\n");
    Print(L"  scope=topology+halt_policy+attach_policy+sidecar+attach\r\n");
    Print(L"  identity=oo-somamind-core-primary\r\n");
    Print(L"  audit_sections=5\r\n");
    llmk_mind_print_halt_policy_audit();
    llmk_attach_route_policy_print_audit();
    llmk_mind_print_sidecar_audit();
    llmk_mind_print_attach_audit();
    Print(L"  ready=%d\r\n", snapshot.ready);
    Print(L"  readiness.core=%s\r\n", snapshot.core_ready ? L"ready" : L"not-ready");
    Print(L"  readiness.halt_policy=%s\r\n", snapshot.halt_ready ? L"ready" : L"not-ready");
    Print(L"  readiness.sidecar=%s\r\n", snapshot.sidecar_ready ? L"ready" : L"not-ready");
    Print(L"  next_action=%s\r\n", next_action);
    Print(L"  next_reason=%s\r\n\r\n", next_reason);
}

static void llmk_mind_print_doctor(void) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    LlmkMindRuntimeSnapshot snapshot;
    llmk_mind_collect_runtime_snapshot(&snapshot);
    llmk_mind_select_next_action(&next_action, &next_reason, NULL, NULL, NULL, NULL);

    Print(L"\r\n[MindDoctor]\r\n");
    Print(L"  goal=produce-next-safe-runtime-fixes\r\n");
    Print(L"  identity=oo-core-first\r\n");
    Print(L"  lanes=auto-fixable,manual-follow-up\r\n");

    int auto_step = 1;
    int manual_step = 1;
    int auto_count = 0;
    int manual_count = 0;
    int bootstrap_can_help = snapshot.bootstrap_can_help;

    Print(L"  [auto-fixable]\r\n");
    if (bootstrap_can_help) {
        Print(L"    step%u=/mind_bootstrap_v1  ; apply the shortest safe batch of stored/runtime fixes\r\n", auto_step++);
        auto_count++;
    }
    if (!g_mind_runtime_state.core_requested) {
        Print(L"    step%u=/core_load <file.mamb>  ; bind the internal OO-SomaMind core backbone first\r\n", auto_step++);
        auto_count++;
    } else if (!g_mind_runtime_state.core_active && !g_mind_runtime_state.core_path[0]) {
        Print(L"    step%u=/core_load <file.mamb>  ; requested core is not active and no reusable path is stored\r\n", auto_step++);
        auto_count++;
    }

    if (!bootstrap_can_help) {
        if (EFI_ERROR(snapshot.cfg_st) || !snapshot.found_any) {
            Print(L"    step%u=/mind_halt_policy_save  ; persist runtime halt policy into repl.cfg\r\n", auto_step++);
            auto_count++;
        } else if (!snapshot.in_sync) {
            Print(L"    step%u=/mind_halt_policy_sync  ; align runtime halt policy with repl.cfg\r\n", auto_step++);
            auto_count++;
        }
    }

    if (!g_mind_runtime_state.sidecar_requested) {
        Print(L"    optional=/oo_sidecar <file.ooss>  ; only if enriched semantics are desired\r\n");
    } else if (!g_mind_runtime_state.sidecar_active && !g_mind_runtime_state.sidecar_path[0]) {
        Print(L"    step%u=/oo_sidecar <file.ooss>  ; requested sidecar is inactive and no reusable path is stored\r\n", auto_step++);
        auto_count++;
    } else if (g_mind_runtime_state.sidecar_active && !g_mind_runtime_state.sidecar_header_valid) {
        Print(L"    step%u=/oo_sidecar <file.ooss>  ; current sidecar header is invalid, reload a valid OOSS file\r\n", auto_step++);
        auto_count++;
    }

    Print(L"  [manual-follow-up]\r\n");
    if (g_mind_runtime_state.sidecar_requested && g_mind_runtime_state.sidecar_active &&
        g_mind_runtime_state.sidecar_header_valid && !g_mind_halting_view.ready) {
        Print(L"    step%u=inspect sidecar layout/export  ; HaltingHead view is not ready yet\r\n", manual_step++);
        manual_count++;
    }

    if (g_mind_runtime_state.attach_requested && !g_mind_runtime_state.attach_active &&
        (!g_mind_runtime_state.attach_path[0] || !bootstrap_can_help)) {
        Print(L"    step%u=/attach_load <file>  ; attach is registered but not active, so revalidate or reload it\r\n", manual_step++);
        manual_count++;
    }

    if (auto_count == 0 && manual_count == 0) {
        Print(L"  status=healthy-v1-runtime\r\n");
        Print(L"  suggestion=use /mind_audit for a full snapshot\r\n\r\n");
        Print(L"  next_action=%s\r\n", next_action);
        Print(L"  next_reason=%s\r\n\r\n", next_reason);
        return;
    }

    Print(L"  summary=auto:%u manual:%u\r\n", (UINT32)auto_count, (UINT32)manual_count);
    if (auto_count > 0 && manual_count == 0) Print(L"  next=/mind_bootstrap_v1\r\n");
    else if (auto_count > 0) Print(L"  next=/mind_bootstrap_v1 then review manual-follow-up\r\n");
    else Print(L"  next=manual-follow-up required\r\n");
    Print(L"  next_action=%s\r\n", next_action);
    Print(L"  next_reason=%s\r\n\r\n", next_reason);
}

static void llmk_mind_select_next_action_from_snapshot(
    const LlmkMindRuntimeSnapshot *snapshot,
    const CHAR16 **out_action,
    const CHAR16 **out_reason
) {
    if (!snapshot) return;

    if (snapshot->ready) {
        if (out_action) *out_action = L"/mind_audit";
        if (out_reason) *out_reason = L"runtime already ready; audit is the next useful snapshot";
        return;
    }

    if (snapshot->bootstrap_can_help) {
        if (out_action) *out_action = L"/mind_bootstrap_v1";
        if (out_reason) *out_reason = L"stored/runtime-safe fixes are available right now";
        return;
    }

    if (!g_mind_runtime_state.core_requested ||
        (g_mind_runtime_state.core_requested && !g_mind_runtime_state.core_active && !g_mind_runtime_state.core_path[0])) {
        if (out_action) *out_action = L"/core_load <file.mamb>";
        if (out_reason) *out_reason = L"core backbone is missing or cannot be reactivated automatically";
        return;
    }

    if (g_mind_runtime_state.sidecar_requested) {
        if (!g_mind_runtime_state.sidecar_active && !g_mind_runtime_state.sidecar_path[0]) {
            if (out_action) *out_action = L"/oo_sidecar <file.ooss>";
            if (out_reason) *out_reason = L"requested sidecar is inactive and no reusable path is stored";
            return;
        }
        if (g_mind_runtime_state.sidecar_active && !g_mind_runtime_state.sidecar_header_valid) {
            if (out_action) *out_action = L"/oo_sidecar <file.ooss>";
            if (out_reason) *out_reason = L"current sidecar header is invalid";
            return;
        }
        if (g_mind_runtime_state.sidecar_active && g_mind_runtime_state.sidecar_header_valid && !g_mind_halting_view.ready) {
            if (out_action) *out_action = L"inspect sidecar layout/export";
            if (out_reason) *out_reason = L"HaltingHead view is not ready and requires manual follow-up";
            return;
        }
    }

    if (g_mind_runtime_state.attach_requested && !g_mind_runtime_state.attach_active) {
        if (out_action) *out_action = L"/attach_load <file>";
        if (out_reason) *out_reason = L"attach request is stored but the attached model is not active";
        return;
    }

    if (out_action) *out_action = L"/mind_doctor";
    if (out_reason) *out_reason = L"state is not fully ready and needs a broader corrective snapshot";
}

static void llmk_mind_select_next_action(
    const CHAR16 **out_action,
    const CHAR16 **out_reason,
    int *out_ready,
    int *out_core_ready,
    int *out_halt_ready,
    int *out_sidecar_ready
) {
    LlmkMindRuntimeSnapshot snapshot;
    llmk_mind_collect_runtime_snapshot(&snapshot);
    if (out_ready) *out_ready = snapshot.ready;
    if (out_core_ready) *out_core_ready = snapshot.core_ready;
    if (out_halt_ready) *out_halt_ready = snapshot.halt_ready;
    if (out_sidecar_ready) *out_sidecar_ready = snapshot.sidecar_ready;
    llmk_mind_select_next_action_from_snapshot(&snapshot, out_action, out_reason);
}

static void llmk_mind_print_ready(void) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    int core_ready = 0;
    int halt_ready = 0;
    int sidecar_ready = 0;
    int ready = 0;
    llmk_mind_select_next_action(&next_action, &next_reason, &ready, &core_ready, &halt_ready, &sidecar_ready);

    Print(L"\r\n[MindReady]\r\n");
    Print(L"  ready=%d\r\n", ready);
    Print(L"  core=%s\r\n", core_ready ? L"ready" : L"not-ready");
    Print(L"  halt_policy=%s\r\n", halt_ready ? L"ready" : L"not-ready");
    Print(L"  sidecar=%s\r\n", sidecar_ready ? L"ready" : L"not-ready");
    Print(L"  attach=optional\r\n");
    Print(L"  scenario=v1-runtime\r\n");
    Print(L"  next=%s\r\n", next_action);
    Print(L"  next_reason=%s\r\n\r\n", next_reason);
}

static void llmk_mind_bootstrap_v1(void) {
    int actions = 0;
    int blockers = 0;
    LlmkMindRuntimeSnapshot snapshot;
    llmk_mind_collect_runtime_snapshot(&snapshot);

    Print(L"\r\n[MindBootstrapV1]\r\n");
    Print(L"  mode=conservative-safe-autofix\r\n");
    Print(L"  identity=oo-core-first\r\n");

    if (!g_mind_runtime_state.core_requested) {
        Print(L"  blocker=/core_load <file.mamb> required before V1 runtime can be ready\r\n");
        blockers++;
    } else if (!g_mind_runtime_state.core_active) {
        if (g_mind_runtime_state.core_path[0]) {
            llmk_mind_bind_core_backbone_v1(g_mind_runtime_state.core_path, 1);
            if (g_mind_runtime_state.core_active) {
                Print(L"  action=/core_load <stored.mamb> auto-applied from saved request\r\n");
                actions++;
            } else {
                Print(L"  blocker=core requested but activation from stored path did not stick\r\n");
                blockers++;
            }
        } else {
            Print(L"  blocker=core requested but no stored path is available; re-run /core_load <file.mamb>\r\n");
            blockers++;
        }
    }

    if (EFI_ERROR(snapshot.cfg_st) || !snapshot.found_any) {
        EFI_STATUS pst = llmk_mind_persist_halt_policy_best_effort();
        if (EFI_ERROR(pst)) {
            Print(L"  blocker=unable to persist runtime halt policy to repl.cfg (%r)\r\n", pst);
            blockers++;
        } else {
            Print(L"  action=/mind_halt_policy_save auto-applied\r\n");
            actions++;
        }
    } else if (!snapshot.in_sync) {
        int was_needed = 0;
        int changed_enabled = 0;
        int changed_threshold = 0;
        EFI_STATUS sst = llmk_mind_apply_saved_halt_policy_if_needed_best_effort(&was_needed, &changed_enabled, &changed_threshold);
        if (EFI_ERROR(sst)) {
            Print(L"  blocker=unable to sync halt policy from repl.cfg (%r)\r\n", sst);
            blockers++;
        } else if (was_needed) {
            llmk_mind_record_halt_apply(LLMK_MIND_HALT_APPLY_SYNC, changed_enabled, changed_threshold);
            Print(L"  action=/mind_halt_policy_sync auto-applied\r\n");
            actions++;
        }
    }

    if (g_mind_runtime_state.sidecar_requested &&
        (!g_mind_runtime_state.sidecar_active || !g_mind_runtime_state.sidecar_header_valid || !g_mind_halting_view.ready)) {
        if (g_mind_runtime_state.sidecar_path[0]) {
            EFI_STATUS sct = llmk_mind_register_sidecar_best_effort(g_mind_runtime_state.sidecar_path, NULL, NULL);
            if (EFI_ERROR(sct)) {
                Print(L"  blocker=unable to re-activate requested sidecar from stored path (%r)\r\n", sct);
                blockers++;
            } else {
                Print(L"  action=/oo_sidecar <stored.ooss> auto-applied from saved request\r\n");
                actions++;
            }
        } else {
            Print(L"  blocker=sidecar requested but no stored path is available; re-run /oo_sidecar <file.ooss>\r\n");
            blockers++;
        }
    }

    if (g_mind_runtime_state.sidecar_requested) {
        if (!g_mind_runtime_state.sidecar_active) {
            Print(L"  blocker=sidecar requested but not active yet; reload with /oo_sidecar <file.ooss>\r\n");
            blockers++;
        } else if (!g_mind_runtime_state.sidecar_header_valid) {
            Print(L"  blocker=sidecar registered but header invalid; reload with /oo_sidecar <file.ooss>\r\n");
            blockers++;
        } else if (!g_mind_halting_view.ready) {
            Print(L"  blocker=sidecar present but HaltingHead layout not ready; inspect/export sidecar\r\n");
            blockers++;
        }
    }

    if (g_mind_runtime_state.attach_requested && !g_mind_runtime_state.attach_active) {
        if (g_mind_runtime_state.attach_path[0]) {
            LlmkModelFormat attach_fmt = LLMK_MODEL_FMT_UNKNOWN;
            EFI_STATUS at = llmk_mind_activate_attach_best_effort(g_mind_runtime_state.attach_path, &attach_fmt);
            if (EFI_ERROR(at)) {
                llmk_mind_set_attach_validation("validation-failed");
                Print(L"  note=attach remains optional; stored attach validation failed (%r)\r\n", at);
            } else {
                llmk_mind_mark_attach_active(attach_fmt);
                Print(L"  action=/attach_load <stored.file> auto-applied from saved request format=%a\r\n",
                      llmk_model_format_ascii(attach_fmt));
                actions++;
            }
        } else {
            Print(L"  note=attach remains optional; no stored attach path is available\r\n");
        }
    }

    llmk_mind_collect_runtime_snapshot(&snapshot);

    {
        const CHAR16 *next_action = L"/mind_doctor";
        const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
        llmk_mind_select_next_action_from_snapshot(&snapshot, &next_action, &next_reason);
        Print(L"  actions=%u blockers=%u ready=%d\r\n", (UINT32)actions, (UINT32)blockers, snapshot.ready);
        Print(L"  next_action=%s\r\n", next_action);
        Print(L"  next_reason=%s\r\n\r\n", next_reason);
    }
}

static void llmk_mind_print_path_v1(void) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    LlmkMindRuntimeSnapshot snapshot;
    int step = 1;
    llmk_mind_collect_runtime_snapshot(&snapshot);
    llmk_mind_select_next_action_from_snapshot(&snapshot, &next_action, &next_reason);

    Print(L"\r\n[MindPathV1]\r\n");
    Print(L"  goal=shortest-next-v1-sequence\r\n");
    Print(L"  ready=%d\r\n", snapshot.ready);

    if (snapshot.ready) {
        Print(L"  status=runtime-ready\r\n");
        Print(L"  next=/mind_audit\r\n");
        Print(L"  next_action=%s\r\n", next_action);
        Print(L"  next_reason=%s\r\n\r\n", next_reason);
        return;
    }

    if (snapshot.bootstrap_can_help) {
        Print(L"  step%u=/mind_bootstrap_v1  ; shortest safe shortcut from current state\r\n", step++);
    }

    if (!g_mind_runtime_state.core_requested) {
        Print(L"  step%u=/core_load <file.mamb>  ; core backbone is still missing\r\n", step++);
    } else if (!g_mind_runtime_state.core_active && !g_mind_runtime_state.core_path[0]) {
        Print(L"  step%u=/core_load <file.mamb>  ; requested core has no reusable stored path\r\n", step++);
    }

    if (!snapshot.bootstrap_can_help) {
        if (EFI_ERROR(snapshot.cfg_st) || !snapshot.found_any) {
            Print(L"  step%u=/mind_halt_policy_save  ; persist current runtime halt policy first\r\n", step++);
        } else if (!snapshot.in_sync) {
            Print(L"  step%u=/mind_halt_policy_sync  ; align runtime halt policy with repl.cfg\r\n", step++);
        }
    }

    if (g_mind_runtime_state.sidecar_requested) {
        if (!g_mind_runtime_state.sidecar_active && !g_mind_runtime_state.sidecar_path[0]) {
            Print(L"  step%u=/oo_sidecar <file.ooss>  ; requested sidecar has no reusable stored path\r\n", step++);
        } else if (g_mind_runtime_state.sidecar_active && !g_mind_runtime_state.sidecar_header_valid) {
            Print(L"  step%u=/oo_sidecar <file.ooss>  ; current sidecar header is invalid\r\n", step++);
        } else if (g_mind_runtime_state.sidecar_active && g_mind_runtime_state.sidecar_header_valid && !g_mind_halting_view.ready) {
            Print(L"  step%u=inspect sidecar layout/export  ; HaltingHead view is still unavailable\r\n", step++);
        }
    } else {
        Print(L"  optional=/oo_sidecar <file.ooss>  ; only if enriched V1 halting is desired\r\n");
    }

    Print(L"  step%u=/mind_ready  ; confirm the final V1 readiness verdict\r\n", step);
    Print(L"  next_action=%s\r\n", next_action);
    Print(L"  next_reason=%s\r\n\r\n", next_reason);
}

static void llmk_mind_print_next(void) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    int ready = 0;
    llmk_mind_select_next_action(&next_action, &next_reason, &ready, NULL, NULL, NULL);

    Print(L"\r\n[MindNext]\r\n");
    Print(L"  goal=single-best-next-action\r\n");
    Print(L"  ready=%d\r\n", ready);
    Print(L"  action=%s\r\n", next_action);
    Print(L"  reason=%s\r\n\r\n", next_reason);
}

static void llmk_mind_print_snapshot(int strict_mode) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    const CHAR16 *prefix = strict_mode ? L"" : L"  ";
    LlmkMindRuntimeSnapshot snapshot;
    LlmkAttachRoutePolicyPreview attach_external_policy;
    LlmkAttachRoutePolicyPreview attach_dual_policy;
    LlmkAttachRoutePolicyConfig persisted_external_cfg;
    LlmkAttachRoutePolicyConfig persisted_dual_cfg;
    int attach_cfg_found_external = 0;
    int attach_cfg_found_dual = 0;
    int attach_cfg_found_any = 0;
    int attach_cfg_in_sync = 0;
    EFI_STATUS attach_cfg_st;
    llmk_mind_collect_runtime_snapshot(&snapshot);
    llmk_mind_select_next_action_from_snapshot(&snapshot, &next_action, &next_reason);
    llmk_attach_route_policy_preview(SOMA_ROUTE_EXTERNAL, &attach_external_policy);
    llmk_attach_route_policy_preview(SOMA_ROUTE_DUAL, &attach_dual_policy);
    llmk_attach_route_policy_get_default(SOMA_ROUTE_EXTERNAL, &persisted_external_cfg);
    llmk_attach_route_policy_get_default(SOMA_ROUTE_DUAL, &persisted_dual_cfg);
    attach_cfg_st = llmk_attach_route_policy_query_sync_state_best_effort(
        &persisted_external_cfg,
        &persisted_dual_cfg,
        &attach_cfg_found_external,
        &attach_cfg_found_dual,
        &attach_cfg_found_any,
        &attach_cfg_in_sync);

    if (!strict_mode) Print(L"\r\n[MindSnapshot]\r\n");
    Print(L"%sformat=kv-v1\r\n", prefix);
    Print(L"%sschema=llmk-mind-snapshot-v5\r\n", prefix);
    Print(L"%sfield_order=fixed\r\n", prefix);
    Print(L"%sstrict=%d\r\n", prefix, strict_mode ? 1 : 0);
    Print(L"%sidentity=oo-somamind-core-primary\r\n", prefix);
    Print(L"%sscenario=v1-runtime\r\n", prefix);
    Print(L"%sready=%d\r\n", prefix, snapshot.ready);
    Print(L"%score_ready=%d\r\n", prefix, snapshot.core_ready);
    Print(L"%shalt_ready=%d\r\n", prefix, snapshot.halt_ready);
    Print(L"%ssidecar_ready=%d\r\n", prefix, snapshot.sidecar_ready);
    Print(L"%score_requested=%d\r\n", prefix, g_mind_runtime_state.core_requested);
    Print(L"%score_active=%d\r\n", prefix, g_mind_runtime_state.core_active);
    Print(L"%ssidecar_requested=%d\r\n", prefix, g_mind_runtime_state.sidecar_requested);
    Print(L"%ssidecar_active=%d\r\n", prefix, g_mind_runtime_state.sidecar_active);
    Print(L"%ssidecar_header_valid=%d\r\n", prefix, g_mind_runtime_state.sidecar_header_valid);
    Print(L"%shalting_hook_ready=%d\r\n", prefix, g_mind_halting_view.ready ? 1 : 0);
    Print(L"%sattach_requested=%d\r\n", prefix, g_mind_runtime_state.attach_requested);
    Print(L"%sattach_active=%d\r\n", prefix, g_mind_runtime_state.attach_active);
    Print(L"%sattach_kind=", prefix); llmk_print_ascii(g_mind_runtime_state.attach_kind[0] ? g_mind_runtime_state.attach_kind : "attach-model"); Print(L"\r\n");
    Print(L"%sattach_format=", prefix); llmk_print_ascii(g_mind_runtime_state.attach_format[0] ? g_mind_runtime_state.attach_format : "unknown"); Print(L"\r\n");
    Print(L"%sattach_validation=", prefix); llmk_print_ascii(g_mind_runtime_state.attach_last_validation[0] ? g_mind_runtime_state.attach_last_validation : "not-recorded"); Print(L"\r\n");
        Print(L"%sattach_policy_external_cfg_temp=%d.%03d\r\n", prefix,
            g_attach_policy_external_cfg.temperature_milli / 1000,
            g_attach_policy_external_cfg.temperature_milli % 1000);
        Print(L"%sattach_policy_external_cfg_top_p=%d.%03d\r\n", prefix,
            g_attach_policy_external_cfg.top_p_milli / 1000,
            g_attach_policy_external_cfg.top_p_milli % 1000);
        Print(L"%sattach_policy_external_cfg_rep=%d.%03d\r\n", prefix,
            g_attach_policy_external_cfg.repetition_penalty_milli / 1000,
            g_attach_policy_external_cfg.repetition_penalty_milli % 1000);
        Print(L"%sattach_policy_external_cfg_max_tokens=%d\r\n", prefix,
            g_attach_policy_external_cfg.max_tokens);
        Print(L"%sattach_policy_dual_cfg_temp=%d.%03d\r\n", prefix,
            g_attach_policy_dual_cfg.temperature_milli / 1000,
            g_attach_policy_dual_cfg.temperature_milli % 1000);
        Print(L"%sattach_policy_dual_cfg_top_p=%d.%03d\r\n", prefix,
            g_attach_policy_dual_cfg.top_p_milli / 1000,
            g_attach_policy_dual_cfg.top_p_milli % 1000);
        Print(L"%sattach_policy_dual_cfg_rep=%d.%03d\r\n", prefix,
            g_attach_policy_dual_cfg.repetition_penalty_milli / 1000,
            g_attach_policy_dual_cfg.repetition_penalty_milli % 1000);
        Print(L"%sattach_policy_dual_cfg_max_tokens=%d\r\n", prefix,
            g_attach_policy_dual_cfg.max_tokens);
        Print(L"%sattach_policy_external_active=%d\r\n", prefix, attach_external_policy.active);
        Print(L"%sattach_policy_external_applied=%d\r\n", prefix, attach_external_policy.applied);
        Print(L"%sattach_policy_external_temp=%d.%03d\r\n", prefix,
            attach_external_policy.temperature_milli / 1000,
            attach_external_policy.temperature_milli % 1000);
        Print(L"%sattach_policy_external_top_p=%d.%03d\r\n", prefix,
            attach_external_policy.top_p_milli / 1000,
            attach_external_policy.top_p_milli % 1000);
        Print(L"%sattach_policy_external_rep=%d.%03d\r\n", prefix,
            attach_external_policy.repetition_penalty_milli / 1000,
            attach_external_policy.repetition_penalty_milli % 1000);
        Print(L"%sattach_policy_external_max_tokens=%d\r\n", prefix, attach_external_policy.max_tokens);
        Print(L"%sattach_policy_dual_active=%d\r\n", prefix, attach_dual_policy.active);
        Print(L"%sattach_policy_dual_applied=%d\r\n", prefix, attach_dual_policy.applied);
        Print(L"%sattach_policy_dual_temp=%d.%03d\r\n", prefix,
            attach_dual_policy.temperature_milli / 1000,
            attach_dual_policy.temperature_milli % 1000);
        Print(L"%sattach_policy_dual_top_p=%d.%03d\r\n", prefix,
            attach_dual_policy.top_p_milli / 1000,
            attach_dual_policy.top_p_milli % 1000);
        Print(L"%sattach_policy_dual_rep=%d.%03d\r\n", prefix,
            attach_dual_policy.repetition_penalty_milli / 1000,
            attach_dual_policy.repetition_penalty_milli % 1000);
        Print(L"%sattach_policy_dual_max_tokens=%d\r\n", prefix, attach_dual_policy.max_tokens);
        Print(L"%sattach_policy_persisted_status=", prefix);
        if (EFI_ERROR(attach_cfg_st) || !attach_cfg_found_any) Print(L"not-found\r\n");
        else if (attach_cfg_found_external == 4 && attach_cfg_found_dual == 4) Print(L"available\r\n");
        else Print(L"partial\r\n");
        Print(L"%sattach_policy_persisted_external_fields=%d\r\n", prefix, attach_cfg_found_external);
        Print(L"%sattach_policy_persisted_dual_fields=%d\r\n", prefix, attach_cfg_found_dual);
        Print(L"%sattach_policy_persisted_sync=", prefix);
        if (EFI_ERROR(attach_cfg_st) || !attach_cfg_found_any) Print(L"unknown\r\n");
        else if (attach_cfg_in_sync) Print(L"in-sync\r\n");
        else Print(L"runtime!=repl.cfg\r\n");
        Print(L"%sattach_policy_persisted_external_temp=%d.%03d\r\n", prefix,
            persisted_external_cfg.temperature_milli / 1000,
            persisted_external_cfg.temperature_milli % 1000);
        Print(L"%sattach_policy_persisted_external_top_p=%d.%03d\r\n", prefix,
            persisted_external_cfg.top_p_milli / 1000,
            persisted_external_cfg.top_p_milli % 1000);
        Print(L"%sattach_policy_persisted_external_rep=%d.%03d\r\n", prefix,
            persisted_external_cfg.repetition_penalty_milli / 1000,
            persisted_external_cfg.repetition_penalty_milli % 1000);
        Print(L"%sattach_policy_persisted_external_max_tokens=%d\r\n", prefix,
            persisted_external_cfg.max_tokens);
        Print(L"%sattach_policy_persisted_dual_temp=%d.%03d\r\n", prefix,
            persisted_dual_cfg.temperature_milli / 1000,
            persisted_dual_cfg.temperature_milli % 1000);
        Print(L"%sattach_policy_persisted_dual_top_p=%d.%03d\r\n", prefix,
            persisted_dual_cfg.top_p_milli / 1000,
            persisted_dual_cfg.top_p_milli % 1000);
        Print(L"%sattach_policy_persisted_dual_rep=%d.%03d\r\n", prefix,
            persisted_dual_cfg.repetition_penalty_milli / 1000,
            persisted_dual_cfg.repetition_penalty_milli % 1000);
        Print(L"%sattach_policy_persisted_dual_max_tokens=%d\r\n", prefix,
            persisted_dual_cfg.max_tokens);
    Print(L"%shalt_policy_enabled=%d\r\n", prefix, g_mind_runtime_halt_enabled);
    Print(L"%shalt_policy_threshold=%d.%03d\r\n", prefix,
          (int)g_mind_runtime_halt_threshold,
          (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
    Print(L"%scfg_found_any=%d\r\n", prefix, snapshot.found_any);
    Print(L"%scfg_in_sync=%d\r\n", prefix, snapshot.in_sync);
    if (EFI_ERROR(snapshot.cfg_st)) Print(L"%scfg_status=error\r\n", prefix);
    else if (!snapshot.found_any) Print(L"%scfg_status=not-found\r\n", prefix);
    else Print(L"%scfg_status=available\r\n", prefix);
    if (!EFI_ERROR(snapshot.cfg_st) && snapshot.found_any) {
        Print(L"%scfg_enabled=%d\r\n", prefix, snapshot.cfg_enabled);
        Print(L"%scfg_threshold=%d.%03d\r\n", prefix,
              (int)snapshot.cfg_threshold,
              (int)((snapshot.cfg_threshold >= 0.0f ? snapshot.cfg_threshold - (int)snapshot.cfg_threshold : ((int)snapshot.cfg_threshold - snapshot.cfg_threshold)) * 1000.0f));
    } else {
        Print(L"%scfg_enabled=0\r\n", prefix);
        Print(L"%scfg_threshold=0.000\r\n", prefix);
    }
    Print(L"%sbootstrap_can_help=%d\r\n", prefix, snapshot.bootstrap_can_help);
    Print(L"%snext_action=%s\r\n", prefix, next_action);
    Print(L"%snext_reason=%s\r\n\r\n", prefix, next_reason);
}

static void llmk_mind_print_status(void) {
    const CHAR16 *next_action = L"/mind_doctor";
    const CHAR16 *next_reason = L"state is not fully ready and needs a broader corrective snapshot";
    LlmkMindRuntimeSnapshot snapshot;
    LlmkAttachRoutePolicyConfig persisted_external_cfg;
    LlmkAttachRoutePolicyConfig persisted_dual_cfg;
    int attach_cfg_found_external = 0;
    int attach_cfg_found_dual = 0;
    int attach_cfg_found_any = 0;
    int attach_cfg_in_sync = 0;
    EFI_STATUS attach_cfg_st;
    llmk_mind_collect_runtime_snapshot(&snapshot);
    llmk_mind_select_next_action_from_snapshot(&snapshot, &next_action, &next_reason);
    llmk_attach_route_policy_get_default(SOMA_ROUTE_EXTERNAL, &persisted_external_cfg);
    llmk_attach_route_policy_get_default(SOMA_ROUTE_DUAL, &persisted_dual_cfg);
    attach_cfg_st = llmk_attach_route_policy_query_sync_state_best_effort(
        &persisted_external_cfg,
        &persisted_dual_cfg,
        &attach_cfg_found_external,
        &attach_cfg_found_dual,
        &attach_cfg_found_any,
        &attach_cfg_in_sync);

    Print(L"\r\n[Mind] OO-SomaMind runtime topology\r\n");
    Print(L"  identity: internal native core\r\n");
    Print(L"  v1 execution path: MAMB backbone via /ssm_load\r\n");
    Print(L"  halt_policy: ");
    if (g_mind_runtime_halt_enabled) {
        Print(L"enabled threshold=%d.%03d\r\n",
              (int)g_mind_runtime_halt_threshold,
              (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
    } else {
        Print(L"disabled threshold=%d.%03d\r\n",
              (int)g_mind_runtime_halt_threshold,
              (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
    }
    {
        Print(L"  halt_policy_cfg: ");
        if (!EFI_ERROR(snapshot.cfg_st) && snapshot.found_any) {
            Print(L"enabled=%d threshold=%d.%03d sync=",
                  snapshot.cfg_enabled,
                  (int)snapshot.cfg_threshold,
                  (int)(((snapshot.cfg_threshold >= 0.0f ? snapshot.cfg_threshold - (int)snapshot.cfg_threshold : ((int)snapshot.cfg_threshold - snapshot.cfg_threshold)) * 1000.0f)));
            if (snapshot.in_sync) Print(L"in-sync\r\n");
            else Print(L"runtime!=repl.cfg\r\n");
        } else {
            Print(L"not found\r\n");
        }
    }
    Print(L"  halt_policy_last_apply: ");
    if (!g_mind_runtime_halt_apply_seen) {
        Print(L"never\r\n");
    } else {
        Print(L"mode=");
        llmk_mind_print_halt_apply_mode(g_mind_runtime_halt_apply_mode);
        Print(L" changed.enabled=%d changed.threshold=%d result=",
              g_mind_runtime_halt_apply_changed_enabled,
              g_mind_runtime_halt_apply_changed_threshold);
          llmk_mind_print_halt_apply_effect();
          Print(L"\r\n");
    }

    Print(L"  core: ");
    if (g_mind_runtime_state.core_requested) {
        if (g_mind_runtime_state.core_active) Print(L"active\r\n");
        else Print(L"requested\r\n");
        Print(L"    kind="); llmk_print_ascii(g_mind_runtime_state.core_kind[0] ? g_mind_runtime_state.core_kind : "somamind-core"); Print(L"\r\n");
        Print(L"    path="); llmk_print_ascii(g_mind_runtime_state.core_path); Print(L"\r\n");
        Print(L"    route=");
        if (g_mind_runtime_state.core_active) Print(L"active via /ssm_load\r\n");
        else Print(L"registered; waiting for runtime bind\r\n");
    } else {
        Print(L"not requested\r\n");
    }

    Print(L"  sidecar: ");
    if (g_mind_runtime_state.sidecar_requested) {
        if (g_mind_runtime_state.sidecar_active) Print(L"active\r\n");
        else Print(L"requested\r\n");
        Print(L"    kind="); llmk_print_ascii(g_mind_runtime_state.sidecar_kind[0] ? g_mind_runtime_state.sidecar_kind : "ooss-sidecar"); Print(L"\r\n");
        Print(L"    path="); llmk_print_ascii(g_mind_runtime_state.sidecar_path); Print(L"\r\n");
        if (g_mind_sidecar_blob && g_mind_sidecar_blob_len > 0) {
            Print(L"    bytes=%lu\r\n", (UINT64)g_mind_sidecar_blob_len);
        }
        if (g_mind_runtime_state.sidecar_last_validation[0]) {
            Print(L"    validation="); llmk_print_ascii(g_mind_runtime_state.sidecar_last_validation); Print(L"\r\n");
        }
        if (g_mind_runtime_state.sidecar_header_valid) {
            Print(L"    header: version=%u d_model=%u n_layer=%u vocab=%u halt_in=%u\r\n",
                  g_mind_runtime_state.sidecar_version,
                  g_mind_runtime_state.sidecar_d_model,
                  g_mind_runtime_state.sidecar_n_layer,
                  g_mind_runtime_state.sidecar_vocab_size,
                  g_mind_runtime_state.sidecar_halting_d_input);
        }
        Print(L"    halting_hook=");
        if (g_mind_halting_view.ready) {
            Print(L"ready (in=%u h0=%u h1=%u out=%u)\r\n",
                  g_mind_halting_view.d_input,
                  g_mind_halting_view.hidden0,
                  g_mind_halting_view.hidden1,
                  g_mind_halting_view.d_output);
        } else {
            Print(L"not ready\r\n");
        }
        Print(L"    route=");
        if (g_mind_runtime_state.sidecar_active && g_mind_halting_view.ready) Print(L"OOSS blob resident; HaltingHead can early-stop active decode loops\r\n");
        else if (g_mind_runtime_state.sidecar_active) Print(L"OOSS blob resident in memory; semantic loader pending\r\n");
        else if (g_mind_runtime_state.core_active) Print(L"registered; waiting for OO sidecar loader\r\n");
        else Print(L"registered; core backbone not active yet\r\n");
    } else {
        Print(L"none\r\n");
    }

    Print(L"  attach: ");
    if (g_mind_runtime_state.attach_requested) {
        if (g_mind_runtime_state.attach_active) Print(L"active\r\n");
        else Print(L"requested\r\n");
        Print(L"    kind="); llmk_print_ascii(g_mind_runtime_state.attach_kind[0] ? g_mind_runtime_state.attach_kind : "attach-model"); Print(L"\r\n");
        Print(L"    format="); llmk_print_ascii(g_mind_runtime_state.attach_format[0] ? g_mind_runtime_state.attach_format : "unknown"); Print(L"\r\n");
        Print(L"    path="); llmk_print_ascii(g_mind_runtime_state.attach_path); Print(L"\r\n");
        Print(L"    validation="); llmk_print_ascii(g_mind_runtime_state.attach_last_validation[0] ? g_mind_runtime_state.attach_last_validation : "not-recorded"); Print(L"\r\n");
        Print(L"    route=");
        if (g_mind_runtime_state.attach_active) Print(L"validated attach backend; attach remains secondary to the OO core\r\n");
        else Print(L"registered but inactive; re-run /attach_load to validate the file again\r\n");
    } else {
        Print(L"none\r\n");
    }

    Print(L"  attach_policy_cfg: ");
    if (EFI_ERROR(attach_cfg_st) || !attach_cfg_found_any) {
        Print(L"not found\r\n");
    } else {
        if (attach_cfg_found_external == 4 && attach_cfg_found_dual == 4) Print(L"available sync=");
        else Print(L"partial sync=");
        if (attach_cfg_in_sync) Print(L"in-sync\r\n");
        else Print(L"runtime!=repl.cfg\r\n");
        Print(L"    persisted.external=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d fields=%d/4\r\n",
              persisted_external_cfg.temperature_milli / 1000,
              persisted_external_cfg.temperature_milli % 1000,
              persisted_external_cfg.top_p_milli / 1000,
              persisted_external_cfg.top_p_milli % 1000,
              persisted_external_cfg.repetition_penalty_milli / 1000,
              persisted_external_cfg.repetition_penalty_milli % 1000,
              persisted_external_cfg.max_tokens,
              attach_cfg_found_external);
        Print(L"    persisted.dual=temp=%d.%03d top_p=%d.%03d rep=%d.%03d max_tokens=%d fields=%d/4\r\n",
              persisted_dual_cfg.temperature_milli / 1000,
              persisted_dual_cfg.temperature_milli % 1000,
              persisted_dual_cfg.top_p_milli / 1000,
              persisted_dual_cfg.top_p_milli % 1000,
              persisted_dual_cfg.repetition_penalty_milli / 1000,
              persisted_dual_cfg.repetition_penalty_milli % 1000,
              persisted_dual_cfg.max_tokens,
              attach_cfg_found_dual);
    }
    Print(L"  attach_policy_last_apply: ");
    if (!g_attach_policy_apply_seen) {
        Print(L"never\r\n");
    } else {
        Print(L"mode=");
        llmk_attach_route_policy_print_apply_mode(g_attach_policy_apply_mode);
        Print(L" changed.external=%d changed.dual=%d result=",
              g_attach_policy_apply_changed_external,
              g_attach_policy_apply_changed_dual);
        llmk_attach_route_policy_print_apply_effect();
        Print(L"\r\n");
    }

    Print(L"  ready=%d\r\n", snapshot.ready);
    Print(L"  readiness.core=%s\r\n", snapshot.core_ready ? L"ready" : L"not-ready");
    Print(L"  readiness.halt_policy=%s\r\n", snapshot.halt_ready ? L"ready" : L"not-ready");
    Print(L"  readiness.sidecar=%s\r\n", snapshot.sidecar_ready ? L"ready" : L"not-ready");
    Print(L"  next_action=%s\r\n", next_action);
    Print(L"  next_reason=%s\r\n", next_reason);
    Print(L"  note: attach models are optional and must not redefine the OO core.\r\n\r\n");
}

static void llmk_mind_print_halt_policy_diff(void) {
    int cfg_enabled = g_mind_runtime_halt_enabled;
    float cfg_threshold = g_mind_runtime_halt_threshold;
    int found_enabled = 0;
    int found_threshold = 0;
    int found_any = 0;
    int in_sync = 0;
    EFI_STATUS st = llmk_mind_query_halt_policy_sync_state_best_effort(&cfg_enabled, &cfg_threshold, &found_enabled, &found_threshold, &found_any, &in_sync);

    Print(L"\r\n[MindHaltPolicyDiff]\r\n");
    Print(L"  runtime.enabled=%d runtime.threshold=%d.%03d\r\n",
          g_mind_runtime_halt_enabled,
          (int)g_mind_runtime_halt_threshold,
          (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));

    if (EFI_ERROR(st) || !found_any) {
        Print(L"  persisted=(not found in repl.cfg)\r\n\r\n");
        return;
    }

    float delta = g_mind_runtime_halt_threshold - cfg_threshold;
    float abs_delta = delta < 0.0f ? -delta : delta;

    Print(L"  persisted.enabled=%d persisted.threshold=%d.%03d\r\n",
          cfg_enabled,
          (int)cfg_threshold,
          (int)((cfg_threshold >= 0.0f ? cfg_threshold - (int)cfg_threshold : ((int)cfg_threshold - cfg_threshold)) * 1000.0f));
    Print(L"  delta.threshold=%d.%03d\r\n",
          (int)delta,
          (int)((delta >= 0.0f ? delta - (int)delta : ((int)delta - delta)) * 1000.0f));
    Print(L"  sync=");
    if (in_sync && abs_delta < 0.0005f) Print(L"in-sync\r\n\r\n");
    else Print(L"runtime!=repl.cfg\r\n\r\n");
}

static void llmk_mind_print_diag(void) {
    Print(L"\r\n[MindDiag] OO-SomaMind runtime diagnostics\r\n");
    Print(L"  core.requested=%d core.active=%d\r\n",
        g_mind_runtime_state.core_requested,
        g_mind_runtime_state.core_active);
    Print(L"  sidecar.requested=%d sidecar.active=%d sidecar.header_valid=%d\r\n",
        g_mind_runtime_state.sidecar_requested,
        g_mind_runtime_state.sidecar_active,
        g_mind_runtime_state.sidecar_header_valid);
    Print(L"  attach.requested=%d attach.active=%d\r\n",
        g_mind_runtime_state.attach_requested,
        g_mind_runtime_state.attach_active);

    Print(L"  core.path=");
    if (g_mind_runtime_state.core_path[0]) llmk_print_ascii(g_mind_runtime_state.core_path);
    else Print(L"(none)");
    Print(L"\r\n");

    Print(L"  sidecar.path=");
    if (g_mind_runtime_state.sidecar_path[0]) llmk_print_ascii(g_mind_runtime_state.sidecar_path);
    else Print(L"(none)");
    Print(L"\r\n");

    Print(L"  attach.path=");
    if (g_mind_runtime_state.attach_path[0]) llmk_print_ascii(g_mind_runtime_state.attach_path);
    else Print(L"(none)");
    Print(L"\r\n");

    Print(L"  attach.kind=");
    if (g_mind_runtime_state.attach_kind[0]) llmk_print_ascii(g_mind_runtime_state.attach_kind);
    else Print(L"(none)");
    Print(L"\r\n");

    Print(L"  attach.format=");
    if (g_mind_runtime_state.attach_format[0]) llmk_print_ascii(g_mind_runtime_state.attach_format);
    else Print(L"(none)");
    Print(L"\r\n");

    Print(L"  attach.validation=");
    if (g_mind_runtime_state.attach_last_validation[0]) llmk_print_ascii(g_mind_runtime_state.attach_last_validation);
    else Print(L"(none)");
    Print(L"\r\n");

    Print(L"  sidecar.blob_ptr=0x%lx sidecar.blob_bytes=%lu\r\n",
        (UINT64)(UINTN)g_mind_sidecar_blob,
        (UINT64)g_mind_sidecar_blob_len);

    Print(L"  sidecar.validation=");
    if (g_mind_runtime_state.sidecar_last_validation[0]) llmk_print_ascii(g_mind_runtime_state.sidecar_last_validation);
    else Print(L"(none)");
    Print(L"\r\n");

    if (g_mind_runtime_state.sidecar_header_valid) {
      Print(L"  sidecar.header.version=%u\r\n", g_mind_runtime_state.sidecar_version);
      Print(L"  sidecar.header.d_model=%u n_layer=%u d_state=%u d_conv=%u expand=%u\r\n",
          g_mind_runtime_state.sidecar_d_model,
          g_mind_runtime_state.sidecar_n_layer,
          g_mind_runtime_state.sidecar_d_state,
          g_mind_runtime_state.sidecar_d_conv,
          g_mind_runtime_state.sidecar_expand);
      Print(L"  sidecar.header.vocab=%u halt_in=%u\r\n",
          g_mind_runtime_state.sidecar_vocab_size,
          g_mind_runtime_state.sidecar_halting_d_input);
    }

    Print(L"  halting.ready=%d halting.enabled=%d halting.threshold=%d.%03d\r\n",
        g_mind_halting_view.ready,
        g_mind_runtime_halt_enabled,
        (int)g_mind_runtime_halt_threshold,
        (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
    if (g_mind_halting_view.ready) {
            Print(L"  halting.runtime_policy=decode-stop@%d.%03d when sidecar.active=1 and halting.enabled=1\r\n",
                    (int)g_mind_runtime_halt_threshold,
                    (int)((g_mind_runtime_halt_threshold >= 0.0f ? g_mind_runtime_halt_threshold - (int)g_mind_runtime_halt_threshold : ((int)g_mind_runtime_halt_threshold - g_mind_runtime_halt_threshold)) * 1000.0f));
      Print(L"  halting.shape in=%u h0=%u h1=%u out=%u\r\n",
          g_mind_halting_view.d_input,
          g_mind_halting_view.hidden0,
          g_mind_halting_view.hidden1,
          g_mind_halting_view.d_output);
      Print(L"  halting.ptrs w0=0x%lx b0=0x%lx w2=0x%lx b2=0x%lx w4=0x%lx b4=0x%lx\r\n",
          (UINT64)(UINTN)g_mind_halting_view.layer0_weight,
          (UINT64)(UINTN)g_mind_halting_view.layer0_bias,
          (UINT64)(UINTN)g_mind_halting_view.layer2_weight,
          (UINT64)(UINTN)g_mind_halting_view.layer2_bias,
          (UINT64)(UINTN)g_mind_halting_view.layer4_weight,
          (UINT64)(UINTN)g_mind_halting_view.layer4_bias);
    }

    Print(L"\r\n");
}

static float llmk_mind_sigmoid(float x) {
    if (x >= 0.0f) {
        float z = fast_exp(-x);
        return 1.0f / (1.0f + z);
    }
    float z = fast_exp(x);
    return z / (1.0f + z);
}

static int llmk_mind_halting_eval(float loop_pos, float *out_logit, float *out_prob) {
    if (!g_mind_halting_view.ready) {
        return 0;
    }

    UINT32 d_input = g_mind_halting_view.d_input;
    UINT32 hidden0 = g_mind_halting_view.hidden0;
    UINT32 hidden1 = g_mind_halting_view.hidden1;
    if (d_input == 0 || hidden0 == 0 || hidden1 == 0) {
        return 0;
    }

    float h0[512];
    float h1[64];
    UINT32 loop_idx = d_input - 1;

    for (UINT32 i = 0; i < hidden0; i++) {
        float acc = g_mind_halting_view.layer0_bias[i];
        acc += g_mind_halting_view.layer0_weight[i * d_input + loop_idx] * loop_pos;
        h0[i] = acc > 0.0f ? acc : 0.0f;
    }

    for (UINT32 j = 0; j < hidden1; j++) {
        float acc = g_mind_halting_view.layer2_bias[j];
        const float *w = g_mind_halting_view.layer2_weight + (j * hidden0);
        for (UINT32 i = 0; i < hidden0; i++) acc += w[i] * h0[i];
        h1[j] = acc > 0.0f ? acc : 0.0f;
    }

    float logit = g_mind_halting_view.layer4_bias[0];
    for (UINT32 j = 0; j < hidden1; j++) {
        logit += g_mind_halting_view.layer4_weight[j] * h1[j];
    }
    float prob = llmk_mind_sigmoid(logit);

    if (out_logit) *out_logit = logit;
    if (out_prob) *out_prob = prob;
    return 1;
}

static int llmk_mind_runtime_should_halt(float loop_pos, float *out_logit, float *out_prob) {
    float logit = 0.0f;
    float prob = 0.0f;
    if (!g_mind_runtime_halt_enabled || !g_mind_runtime_state.sidecar_active || !g_mind_halting_view.ready) {
        return 0;
    }
    if (!llmk_mind_halting_eval(loop_pos, &logit, &prob)) {
        return 0;
    }
    if (out_logit) *out_logit = logit;
    if (out_prob) *out_prob = prob;
    return prob >= g_mind_runtime_halt_threshold;
}

static void llmk_mind_reset_halt_policy_defaults(void) {
    g_mind_runtime_halt_enabled = 1;
    g_mind_runtime_halt_threshold = LLMK_MIND_RUNTIME_HALT_THRESHOLD;
}

static void llmk_mind_halting_probe(float loop_pos) {
    float logit = 0.0f;
    float prob = 0.0f;
    if (!g_mind_halting_view.ready) {
        Print(L"\r\n[MindHalt] Halting hook not ready. Load a compatible OOSS sidecar first.\r\n\r\n");
        return;
    }
    if (!llmk_mind_halting_eval(loop_pos, &logit, &prob)) {
        Print(L"\r\n[MindHalt] Invalid halting view dimensions.\r\n\r\n");
        return;
    }

    Print(L"\r\n[MindHalt] loop_pos=%d.%03d\r\n",
          (int)loop_pos,
          (int)((loop_pos >= 0.0f ? loop_pos - (int)loop_pos : ((int)loop_pos - loop_pos)) * 1000.0f));
    Print(L"  logit=%d.%03d\r\n",
          (int)logit,
          (int)((logit >= 0.0f ? logit - (int)logit : ((int)logit - logit)) * 1000.0f));
    Print(L"  halt_prob=%d.%03d\r\n",
          (int)prob,
          (int)((prob >= 0.0f ? prob - (int)prob : ((int)prob - prob)) * 1000.0f));
    Print(L"  interpretation=");
    if (prob >= 0.80f) Print(L"high-stop-bias\r\n\r\n");
    else if (prob >= 0.50f) Print(L"balanced-stop-bias\r\n\r\n");
    else Print(L"continue-bias\r\n\r\n");
}

static void llmk_mind_halting_decide(float loop_pos, float threshold) {
    float logit = 0.0f;
    float prob = 0.0f;
    if (!g_mind_halting_view.ready) {
        Print(L"\r\n[MindHalt] Halting hook not ready. Load a compatible OOSS sidecar first.\r\n\r\n");
        return;
    }
    if (!llmk_mind_halting_eval(loop_pos, &logit, &prob)) {
        Print(L"\r\n[MindHalt] Invalid halting view dimensions.\r\n\r\n");
        return;
    }
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;

    Print(L"\r\n[MindHaltDecision] loop_pos=%d.%03d threshold=%d.%03d\r\n",
          (int)loop_pos,
          (int)((loop_pos >= 0.0f ? loop_pos - (int)loop_pos : ((int)loop_pos - loop_pos)) * 1000.0f),
          (int)threshold,
          (int)((threshold >= 0.0f ? threshold - (int)threshold : ((int)threshold - threshold)) * 1000.0f));
    Print(L"  logit=%d.%03d halt_prob=%d.%03d\r\n",
          (int)logit,
          (int)((logit >= 0.0f ? logit - (int)logit : ((int)logit - logit)) * 1000.0f),
          (int)prob,
          (int)((prob >= 0.0f ? prob - (int)prob : ((int)prob - prob)) * 1000.0f));
    Print(L"  decision=");
    if (prob >= threshold) Print(L"HALT\r\n\r\n");
    else Print(L"CONTINUE\r\n\r\n");
}

static void llmk_mind_halting_sweep(float start, float end, float step, float threshold) {
    if (!g_mind_halting_view.ready) {
        Print(L"\r\n[MindHalt] Halting hook not ready. Load a compatible OOSS sidecar first.\r\n\r\n");
        return;
    }
    if (step <= 0.0f) {
        Print(L"\r\n[MindHaltSweep] step must be > 0.0\r\n\r\n");
        return;
    }
    if (end < start) {
        float tmp = start;
        start = end;
        end = tmp;
    }
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;

    Print(L"\r\n[MindHaltSweep] start=%d.%03d end=%d.%03d step=%d.%03d threshold=%d.%03d\r\n",
          (int)start,
          (int)((start >= 0.0f ? start - (int)start : ((int)start - start)) * 1000.0f),
          (int)end,
          (int)((end >= 0.0f ? end - (int)end : ((int)end - end)) * 1000.0f),
          (int)step,
          (int)((step >= 0.0f ? step - (int)step : ((int)step - step)) * 1000.0f),
          (int)threshold,
          (int)((threshold >= 0.0f ? threshold - (int)threshold : ((int)threshold - threshold)) * 1000.0f));

    int samples = 0;
    for (float loop_pos = start; loop_pos <= end + 0.0001f && samples < 256; loop_pos += step, samples++) {
        float logit = 0.0f;
        float prob = 0.0f;
        if (!llmk_mind_halting_eval(loop_pos, &logit, &prob)) {
            Print(L"  invalid halting view dimensions\r\n\r\n");
            return;
        }

        Print(L"  x=%d.%03d logit=%d.%03d halt_prob=%d.%03d decision=",
              (int)loop_pos,
              (int)((loop_pos >= 0.0f ? loop_pos - (int)loop_pos : ((int)loop_pos - loop_pos)) * 1000.0f),
              (int)logit,
              (int)((logit >= 0.0f ? logit - (int)logit : ((int)logit - logit)) * 1000.0f),
              (int)prob,
              (int)((prob >= 0.0f ? prob - (int)prob : ((int)prob - prob)) * 1000.0f));
        if (prob >= threshold) Print(L"HALT\r\n");
        else Print(L"CONTINUE\r\n");
    }

    if (samples == 256) {
        Print(L"  note=sweep capped at 256 samples\r\n");
    }
    Print(L"\r\n");
}

static void llmk_mind_bind_core_backbone_v1(const char *path, int via_core_alias) {
    if (!path || !path[0]) return;
    llmk_mind_mark_core_active(path, "mamb-runtime");

    CHAR16 path16[192];
    ascii_to_char16(path16, path, (int)(sizeof(path16) / sizeof(path16[0])));

    Print(L"\r\n[SSM] Loading: %s ...\r\n", path16);
    if (via_core_alias) {
        Print(L"  Alias: /core_load -> /ssm_load\r\n");
        Print(L"  Identity: internal OO-SomaMind core\r\n");
    }
    Print(L"  Bound as OO-SomaMind V1 core backbone.\r\n");
    if (g_mind_runtime_state.sidecar_requested) {
        Print(L"  Sidecar note: OOSS sidecar registered but loader not wired yet.\r\n");
    }
    Print(L"  (SSM inference integration: see engine/ssm/ -- hook into Stage 3)\r\n\r\n");
}

static EFI_STATUS llmk_mind_register_sidecar_best_effort(const char *path, LlmkOoSidecarHeader *out_hdr, UINTN *out_raw_len) {
    if (!path || !path[0]) return EFI_INVALID_PARAMETER;

    CHAR16 path16[192];
    ascii_to_char16(path16, path, (int)(sizeof(path16) / sizeof(path16[0])));

    void *raw = NULL;
    UINTN raw_len = 0;
    llmk_mind_set_sidecar_validation("checking header");
    EFI_STATUS st = llmk_read_entire_file_best_effort(path16, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len < 36) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        llmk_mind_set_sidecar_validation("open/read failed");
        return EFI_ERROR(st) ? st : EFI_BAD_BUFFER_SIZE;
    }

    LlmkOoSidecarHeader hdr;
    if (!llmk_ooss_parse_header(raw, raw_len, &hdr)) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        llmk_mind_set_sidecar_validation("invalid OOSS header");
        return EFI_COMPROMISED_DATA;
    }

    llmk_mind_release_sidecar_blob();

    llmk_mind_set_sidecar_request(path, "ooss-sidecar");
    llmk_mind_store_sidecar_header(&hdr);
    g_mind_sidecar_blob = raw;
    llmk_mind_mark_sidecar_active(raw_len);
    if (llmk_ooss_bind_halting_view(&hdr, g_mind_sidecar_blob, g_mind_sidecar_blob_len)) {
        llmk_mind_set_sidecar_validation("valid OOSS header; halting hook ready");
    } else {
        llmk_mind_set_sidecar_validation("valid OOSS header; halting hook unavailable");
    }

    if (out_hdr) *out_hdr = hdr;
    if (out_raw_len) *out_raw_len = raw_len;
    return EFI_SUCCESS;
}

static void llmk_model_set_loaded_path(const CHAR16 *path) {
    g_loaded_model_path16_canary = 0xD1B1D1B1u;
    g_loaded_model_gguf_valid = 0;
    SetMem(&g_loaded_model_gguf, sizeof(g_loaded_model_gguf), 0);
    if (!path) {
        g_loaded_model_path16[0] = 0;
        return;
    }
    // Truncate safely
    UINTN n = StrLen(path);
    if (n >= (sizeof(g_loaded_model_path16) / sizeof(g_loaded_model_path16[0]))) {
        n = (sizeof(g_loaded_model_path16) / sizeof(g_loaded_model_path16[0])) - 1;
    }
    for (UINTN i = 0; i < n; i++) g_loaded_model_path16[i] = path[i];
    g_loaded_model_path16[n] = 0;
}

static void llmk_debug_print_loaded_model_path(const CHAR16 *tag) {
    if (!tag) tag = L"(tag)";
    Print(L"[dbg] %s: loaded_model_path16_canary=0x%08x\r\n", tag, (UINT32)g_loaded_model_path16_canary);
    if (g_loaded_model_path16[0]) {
        Print(L"[dbg] %s: loaded_model_path=%s\r\n", tag, g_loaded_model_path16);
        Print(L"[dbg] %s: loaded_model_path_u16[0..7]=", tag);
        for (int i = 0; i < 8; i++) {
            Print(L"%04x ", (UINT16)g_loaded_model_path16[i]);
            if (g_loaded_model_path16[i] == 0) break;
        }
        Print(L"\r\n");
    } else {
        Print(L"[dbg] %s: loaded_model_path=(empty)\r\n", tag);
    }
}

static void llmk_print_gguf_summary_block(const CHAR16 *path16, const GgufSummary *s) {
    if (!s) return;
    Print(L"\r\nGGUF model info:\r\n");
    Print(L"  file=%s\r\n", path16 ? path16 : L"(unknown)");
    Print(L"  version=%u tensors=%lu kv=%lu header_bytes=%lu\r\n",
          (unsigned)s->version,
          (UINT64)s->tensor_count,
          (UINT64)s->kv_count,
          (UINT64)s->header_bytes);
    Print(L"  arch="); llmk_print_ascii(s->architecture[0] ? s->architecture : "(unknown)"); Print(L"\r\n");
    Print(L"  name="); llmk_print_ascii(s->name[0] ? s->name : "(none)"); Print(L"\r\n");
    Print(L"  file_type=%lu\r\n", (UINT64)s->file_type);
    if (s->context_length)   Print(L"  ctx=%lu\r\n", (UINT64)s->context_length);
    if (s->embedding_length) Print(L"  dim=%lu\r\n", (UINT64)s->embedding_length);
    if (s->block_count)      Print(L"  layers=%lu\r\n", (UINT64)s->block_count);
    if (s->head_count)       Print(L"  heads=%lu\r\n", (UINT64)s->head_count);
    if (s->head_count_kv)    Print(L"  kv_heads=%lu\r\n", (UINT64)s->head_count_kv);
    if (s->vocab_size)       Print(L"  vocab=%lu\r\n", (UINT64)s->vocab_size);
    if (s->tokenizer_model[0]) { Print(L"  tokenizer="); llmk_print_ascii(s->tokenizer_model); Print(L"\r\n"); }
}

#ifndef LLMB_BUILD_ID
#define LLMB_BUILD_ID L"(unknown)"
#endif

// Forward decl (defined later).
static int uefi_wall_us(unsigned long long *out_us);
static void llmk_print_ascii(const char *s);

typedef struct {
    const CHAR16 *name;
    unsigned long long us;
} LlmkBootMark;

static LlmkBootMark g_boot_marks[16]; // SAFE: fixed-size boot mark ring; bounded by g_boot_mark_count check
static int g_boot_mark_count = 0;


static unsigned long long g_overlay_stage_start_us = 0;
static unsigned long long g_overlay_stage_prev_us = 0;

void llmk_overlay_stage(UINT32 stage_index_1based, UINT32 stage_count) {
    InterfaceFx_Stage(stage_index_1based, stage_count);

    unsigned long long us;
    if (!uefi_wall_us(&us)) return;
    if (g_overlay_stage_start_us == 0) {
        g_overlay_stage_start_us = us;
        g_overlay_stage_prev_us = us;
    }

    unsigned long long delta_us = (us >= g_overlay_stage_prev_us) ? (us - g_overlay_stage_prev_us) : 0;
    unsigned long long total_us = (us >= g_overlay_stage_start_us) ? (us - g_overlay_stage_start_us) : 0;
    g_overlay_stage_prev_us = us;

    InterfaceFx_SetTimingMs((UINT32)(delta_us / 1000ULL), (UINT32)(total_us / 1000ULL));
}

void llmk_boot_mark(const CHAR16 *name) {
    if (!name) return;
    if (g_boot_mark_count >= (int)(sizeof(g_boot_marks) / sizeof(g_boot_marks[0]))) return;
    unsigned long long us;
    if (!uefi_wall_us(&us)) return;
    g_boot_marks[g_boot_mark_count].name = name;
    g_boot_marks[g_boot_mark_count].us = us;
    g_boot_mark_count++;
}

static void llmk_boot_print_timing_summary(void) {
    if (g_boot_mark_count < 2) return;
    // Keep it compact; seconds-of-day is fine for short boots.
    Print(L"\r\n[boot] timing (ms):\r\n");
    unsigned long long base = g_boot_marks[0].us;
    unsigned long long prev = base;
    for (int i = 1; i < g_boot_mark_count; i++) {
        unsigned long long curr = g_boot_marks[i].us;
        unsigned long long delta = (curr >= prev) ? (curr - prev) : 0;
        unsigned long long total = (curr >= base) ? (curr - base) : 0;
        Print(L"  +%5lu  (%5lu total)  %s\r\n", (UINT64)(delta / 1000ULL), (UINT64)(total / 1000ULL), g_boot_marks[i].name);
        prev = curr;
    }
    Print(L"\r\n");
}

static EFI_STATUS llmk_peek_magic4(EFI_FILE_HANDLE f, UINT8 out_magic[4]) {
    if (!f || !out_magic) return EFI_INVALID_PARAMETER;
    UINT64 pos = 0;
    EFI_STATUS st = uefi_call_wrapper(f->GetPosition, 2, f, &pos);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(f->SetPosition, 2, f, 0);
    if (EFI_ERROR(st)) return st;
    UINTN n = 4;
    st = uefi_call_wrapper(f->Read, 3, f, &n, out_magic);
    // restore position best-effort
    uefi_call_wrapper(f->SetPosition, 2, f, pos);
    if (EFI_ERROR(st)) return st;
    if (n != 4) return EFI_END_OF_FILE;
    return EFI_SUCCESS;
}

static LlmkModelFormat llmk_detect_model_format(EFI_FILE_HANDLE f) {
    UINT8 m[4]; // SAFE: fixed-size magic header (4 bytes) read via llmk_peek_magic4
    EFI_STATUS st = llmk_peek_magic4(f, m);
    if (EFI_ERROR(st)) return LLMK_MODEL_FMT_UNKNOWN;
    if (m[0] == 'G' && m[1] == 'G' && m[2] == 'U' && m[3] == 'F') return LLMK_MODEL_FMT_GGUF;
    // .bin (llama2.c weights) does not have a magic; treat as BIN by default.
    return LLMK_MODEL_FMT_BIN;
}

static const char *llmk_model_format_ascii(LlmkModelFormat fmt) {
    if (fmt == LLMK_MODEL_FMT_GGUF) return "gguf";
    if (fmt == LLMK_MODEL_FMT_BIN) return "bin";
    return "unknown";
}

static int llmk_bin_header_looks_valid(const int hdr[7]) {
    int dim = hdr[0];
    int hidden_dim = hdr[1];
    int n_layers = hdr[2];
    int n_heads = hdr[3];
    int n_kv_heads = hdr[4];
    int vocab_size = hdr[5];
    int seq_len = hdr[6];

    if (dim <= 0 || hidden_dim <= 0 || n_layers <= 0 || n_heads <= 0 || n_kv_heads <= 0) return 0;
    if (n_kv_heads > n_heads) return 0;
    if (vocab_size == 0) return 0;
    if (seq_len <= 0) return 0;
    return 1;
}

static EFI_STATUS llmk_mind_activate_attach_best_effort(const char *path, LlmkModelFormat *out_fmt) {
    if (out_fmt) *out_fmt = LLMK_MODEL_FMT_UNKNOWN;
    if (!path || !path[0]) return EFI_INVALID_PARAMETER;

    CHAR16 path16[192];
    ascii_to_char16(path16, path, (int)(sizeof(path16) / sizeof(path16[0])));

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, path16);
    if (EFI_ERROR(st) || !f) return EFI_NOT_FOUND;

    LlmkModelFormat fmt = llmk_detect_model_format(f);
    if (fmt == LLMK_MODEL_FMT_UNKNOWN) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_UNSUPPORTED;
    }

    if (fmt == LLMK_MODEL_FMT_GGUF) {
        GgufSummary summary;
        st = gguf_read_summary(f, &summary);
        uefi_call_wrapper(f->Close, 1, f);
        if (EFI_ERROR(st)) return st;
    } else {
        EFI_STATUS pst = uefi_call_wrapper(f->SetPosition, 2, f, 0);
        if (EFI_ERROR(pst)) {
            uefi_call_wrapper(f->Close, 1, f);
            return pst;
        }

        int hdr[7]; // SAFE: fixed-size BIN header (7 ints) read in one shot
        for (int k = 0; k < 7; k++) hdr[k] = 0;
        UINTN bytes = (UINTN)(7 * sizeof(int));
        EFI_STATUS rst = uefi_call_wrapper(f->Read, 3, f, &bytes, hdr);
        uefi_call_wrapper(f->Close, 1, f);
        if (EFI_ERROR(rst)) return rst;
        if (bytes != (UINTN)(7 * sizeof(int))) return EFI_END_OF_FILE;
        if (!llmk_bin_header_looks_valid(hdr)) return EFI_LOAD_ERROR;
    }

    if (out_fmt) *out_fmt = fmt;
    return EFI_SUCCESS;
}

static int llmk_char16_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int llmk_char16_streq_ci(const CHAR16 *a, const CHAR16 *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (llmk_char16_tolower((int)*a) != llmk_char16_tolower((int)*b)) return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static int llmk_char16_endswith_ci(const CHAR16 *s, const CHAR16 *suffix) {
    if (!s || !suffix) return 0;
    UINTN sl = StrLen(s);
    UINTN su = StrLen(suffix);
    if (su == 0) return 1;
    if (sl < su) return 0;
    const CHAR16 *p = s + (sl - su);
    for (UINTN i = 0; i < su; i++) {
        if (llmk_char16_tolower((int)p[i]) != llmk_char16_tolower((int)suffix[i])) return 0;
    }
    return 1;
}

static int llmk_char16_startswith_ci(const CHAR16 *s, const CHAR16 *prefix) {
    if (!s || !prefix) return 0;
    UINTN sl = StrLen(s);
    UINTN pl = StrLen(prefix);
    if (pl == 0) return 1;
    if (sl < pl) return 0;
    for (UINTN i = 0; i < pl; i++) {
        if (llmk_char16_tolower((int)s[i]) != llmk_char16_tolower((int)prefix[i])) return 0;
    }
    return 1;
}

static int llmk_char16_has_dot_ext(const CHAR16 *s) {
    if (!s) return 0;
    // extension exists if there is a '.' after the last path separator.
    const CHAR16 *last_sep = NULL;
    const CHAR16 *last_dot = NULL;
    for (const CHAR16 *p = s; *p; p++) {
        if (*p == L'\\' || *p == L'/') last_sep = p;
        if (*p == L'.') last_dot = p;
    }
    if (!last_dot) return 0;
    if (last_sep && last_dot < last_sep) return 0;
    // require something after dot
    return last_dot[1] != 0; // SAFE: constant index checks for presence of an extension char
}

static void llmk_char16_copy_cap(CHAR16 *dst, int cap, const CHAR16 *src);
static void llmk_cfg_copy_ascii_token(char *dst, int cap, const char *src);

static CHAR16 llmk_char16_toupper(CHAR16 c) {
    if (c >= L'a' && c <= L'z') return (CHAR16)(c - (L'a' - L'A'));
    return c;
}

static int llmk_char16_is_alnum(CHAR16 c) {
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 1;
    if (c >= L'0' && c <= L'9') return 1;
    return 0;
}

static int llmk_char16_has_tilde(const CHAR16 *s) {
    if (!s) return 0;
    for (const CHAR16 *p = s; *p; p++) if (*p == L'~') return 1;
    return 0;
}

// Test/diagnostic knob: when enabled, the FAT83 helper will prefer opening the 8.3 alias
// (if available) even if the long filename open succeeds.
// Default is 0 (off).
static int g_cfg_fat83_force = 0;

// Operating Organism (OO) v0: when enabled, the kernel will maintain a tiny persistent
// state file + append-only journal (best-effort) on the boot volume.
// Default is 0 (off).
static int g_cfg_oo_enable = 0;
// OO M3: optional override for Zone-B minimum total (in MB).
// -1: use policy defaults (SAFE=512, DEGRADED=640).
// 0+: force this minimum (0 disables the floor; intended for deterministic tests).
static int g_cfg_oo_min_total_mb = -1;
// OO M5: LLM consult (default=1 if oo_enable=1, else 0).
static int g_cfg_oo_llm_consult = -1;
// OO M5.1: Multi-action parsing (default=1 if oo_llm_consult=1, else 0).
static int g_cfg_oo_multi_actions = -1;
// OO M5.2: Auto-apply actions (0=off, 1=conservative, 2=aggressive).
static int g_cfg_oo_auto_apply = 0;
// OO M5.2: Throttling flag (1 auto-apply per boot).
static int g_oo_auto_applied_this_boot = 0;
// OO M7.2: bounded multi-step plan (0=off, 1=on).
static int g_cfg_oo_plan_enable = 0;
// OO M7.2: max auto-applies per boot window.
static int g_cfg_oo_plan_max_actions = 2;
// OO M7.2: count of auto-applies already performed this boot.
static int g_oo_auto_applied_count_this_boot = 0;
// OO M5.3: Log consultations to OOCONSULT.LOG (default=1 if oo_llm_consult=1).
static int g_cfg_oo_consult_log = -1;
// OO M7: Confidence gate (0=off/log-only, 1=enforced for auto-apply).
static int g_cfg_oo_conf_gate = 0;
// OO M7: Confidence threshold [0..100], default 60.
static int g_cfg_oo_conf_threshold = 60;

// OO M4: optional network read-only tick (best-effort; never required to boot).
// Default is 0 (off).
static int g_cfg_oo_net = 0;
// Optional: URL hint to fetch a signed manifest from (placeholder for now).
static char g_cfg_oo_manifest_url[192];

static UINT64 llmk_get_conventional_ram_bytes_best_effort(void) {
    if (!BS) return 0;

    UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;

    EFI_STATUS st = uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, map, &map_key, &desc_size, &desc_version);
    if (st != EFI_BUFFER_TOO_SMALL || map_size == 0 || desc_size == 0) return 0;

    // Leave slack so a follow-up GetMemoryMap doesn't race map growth.
    map_size += desc_size * 8;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, map_size, (void **)&map);
    if (EFI_ERROR(st) || !map) return 0;

    st = uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, map, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(st) || desc_size == 0) {
        uefi_call_wrapper(BS->FreePool, 1, map);
        return 0;
    }

    UINT64 total = 0;
    UINT8 *p = (UINT8 *)map;
    UINT8 *end = p + map_size;
    while (p + desc_size <= end) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)p;
        if (d->Type == EfiConventionalMemory) {
            total += (UINT64)d->NumberOfPages * 4096ULL;
        }
        p += desc_size;
    }

    uefi_call_wrapper(BS->FreePool, 1, map);
    return total;
}

static int llmk_dir_contains_leaf_ci(EFI_FILE_HANDLE Root, const CHAR16 *dir_path, const CHAR16 *leaf) {
    if (!Root || !leaf || !leaf[0]) return 0;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;
    if (!dir_path || !dir_path[0] || llmk_char16_streq_ci(dir_path, L".") || llmk_char16_streq_ci(dir_path, L"\\")) {
        dir = Root;
        close_dir = 0;
    } else {
        EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &dir, (CHAR16 *)dir_path, EFI_FILE_MODE_READ, 0);
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

    int found = 0;
    while (!found) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (!info->FileName) continue;
        if (llmk_char16_streq_ci(info->FileName, L".") || llmk_char16_streq_ci(info->FileName, L"..")) continue;
        if (llmk_char16_streq_ci(info->FileName, leaf)) {
            found = 1;
            break;
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
    return found;
}

static EFI_STATUS llmk_open_read_with_fat83_fallback(EFI_FILE_HANDLE Root,
                                                    const CHAR16 *path,
                                                    EFI_FILE_HANDLE *out_file,
                                                    CHAR16 *out_picked,
                                                    int out_picked_cap,
                                                    const CHAR16 *why_tag) {
    if (!out_file) return EFI_INVALID_PARAMETER;
    *out_file = NULL;
    if (out_picked && out_picked_cap > 0) out_picked[0] = 0;
    if (!Root || !path || !path[0]) return EFI_INVALID_PARAMETER;

    EFI_FILE_HANDLE direct_f = NULL;
    EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &direct_f, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    int direct_ok = (!EFI_ERROR(st) && direct_f);

    // Some UEFI FAT drivers are unreliable with long filenames. Best-effort: if open fails,
    // try the common 8.3 alias pattern FIRST6~N.EXT (N=1..9).
    // In test mode (fat83_force=1), prefer the alias when available to make the fallback path
    // deterministic under QEMU/OVMF.
    // This is intentionally conservative: only attempts if the leaf is not already a short alias.
    if (llmk_char16_has_tilde(path)) {
        if (direct_ok) {
            *out_file = direct_f;
            if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, path);
            return EFI_SUCCESS;
        }
        return st;
    }

    // Split into dir prefix and leaf.
    const CHAR16 *leaf = path;
    const CHAR16 *last_sep = NULL;
    for (const CHAR16 *p = path; *p; p++) {
        if (*p == L'\\' || *p == L'/') last_sep = p;
    }
    if (last_sep) leaf = last_sep + 1;
    if (!leaf || !leaf[0]) return st;

    // Safety: only attempt alias fallback if the requested leaf actually exists in the directory listing.
    // This prevents accidental wrong-file opens when the user misspells a name that happens to share the
    // same FIRST6 prefix with another file.
    if (last_sep) {
        CHAR16 dir_path[256];
        int cap = (int)(sizeof(dir_path) / sizeof(dir_path[0]));
        int k = 0;
        for (const CHAR16 *p = path; *p && p < last_sep && k < cap - 1; p++) dir_path[k++] = *p;
        dir_path[k] = 0;
        if (k <= 0) {
            if (!llmk_dir_contains_leaf_ci(Root, NULL, leaf)) {
                if (direct_ok) {
                    *out_file = direct_f;
                    if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, path);
                    return EFI_SUCCESS;
                }
                return st;
            }
        } else {
            if (!llmk_dir_contains_leaf_ci(Root, dir_path, leaf)) {
                if (direct_ok) {
                    *out_file = direct_f;
                    if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, path);
                    return EFI_SUCCESS;
                }
                return st;
            }
        }
    } else {
        if (!llmk_dir_contains_leaf_ci(Root, NULL, leaf)) {
            if (direct_ok) {
                *out_file = direct_f;
                if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, path);
                return EFI_SUCCESS;
            }
            return st;
        }
    }

    // If direct open succeeded and we're not forcing alias preference, just return it.
    if (direct_ok && !g_cfg_fat83_force) {
        *out_file = direct_f;
        if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, path);
        return EFI_SUCCESS;
    }

    // Find extension (leaf_base . leaf_ext)
    const CHAR16 *dot = NULL;
    for (const CHAR16 *p = leaf; *p; p++) {
        if (*p == L'.') dot = p;
    }
    const CHAR16 *leaf_base = leaf;
    const CHAR16 *leaf_ext = NULL;
    int base_len = 0;
    if (dot && dot > leaf) {
        base_len = (int)(dot - leaf);
        leaf_ext = dot + 1;
    } else {
        base_len = (int)StrLen(leaf);
        leaf_ext = NULL;
    }
    if (base_len <= 0) return st;

    // Build sanitized uppercase base/ext (for alias generation).
    CHAR16 base_s[64];
    CHAR16 ext_s[16]; // SAFE: sanitized extension (<=3 chars) + NUL; bounded writes
    int bn = 0;
    for (int i = 0; i < base_len && bn < (int)(sizeof(base_s) / sizeof(base_s[0])) - 1; i++) {
        CHAR16 c = leaf_base[i];
        if (llmk_char16_is_alnum(c)) {
            base_s[bn++] = llmk_char16_toupper(c);
        }
    }
    base_s[bn] = 0;
    int en = 0;
    if (leaf_ext) {
        for (const CHAR16 *p = leaf_ext; *p && en < (int)(sizeof(ext_s) / sizeof(ext_s[0])) - 1; p++) {
            CHAR16 c = *p;
            if (llmk_char16_is_alnum(c)) {
                ext_s[en++] = llmk_char16_toupper(c);
            }
            if (en >= 3) break;
        }
    }
    ext_s[en] = 0;
    if (bn <= 0) return st;

    // FIRST6~N + optional .EXT
    CHAR16 prefix6[8]; // SAFE: FIRST6 + optional chars + NUL; bounded by p6<6
    int p6 = 0;
    for (int i = 0; i < bn && p6 < 6; i++) {
        prefix6[p6++] = base_s[i];
    }
    prefix6[p6] = 0;
    if (p6 <= 0) return st;

    for (int n = 1; n <= 9; n++) {
        CHAR16 alias_leaf[32]; // SAFE: 8.3 alias leaf (FIRST6~N[.EXT]) fits; built via StrCpy/StrCat with bounded parts
        alias_leaf[0] = 0;
        StrCpy(alias_leaf, prefix6);
        StrCat(alias_leaf, L"~");
        {
            CHAR16 digit[2]; // SAFE: single digit + NUL
            digit[0] = (CHAR16)(L'0' + n);
            digit[1] = 0; // SAFE: constant index into fixed-size digit[2]
            StrCat(alias_leaf, digit);
        }
        if (en > 0) {
            StrCat(alias_leaf, L".");
            StrCat(alias_leaf, ext_s);
        }

        CHAR16 candidate[256];
        candidate[0] = 0;
        if (last_sep) {
            // Copy prefix including separator.
            int cap = (int)(sizeof(candidate) / sizeof(candidate[0]));
            int k = 0;
            for (const CHAR16 *p = path; *p && p <= last_sep && k < cap - 1; p++) candidate[k++] = *p;
            candidate[k] = 0;
            if (k >= cap - 1) continue;
            if ((UINTN)k + StrLen(alias_leaf) + 1 >= (UINTN)cap) continue;
            StrCat(candidate, alias_leaf);
        } else {
            llmk_char16_copy_cap(candidate, (int)(sizeof(candidate) / sizeof(candidate[0])), alias_leaf);
        }

        EFI_FILE_HANDLE ff = NULL;
        EFI_STATUS fst = uefi_call_wrapper(Root->Open, 5, Root, &ff, candidate, EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(fst) && ff) {
            Print(L"[fat] open fallback ok (%s): %s -> %s\r\n", why_tag ? why_tag : L"open", path, candidate);
            if (direct_ok && direct_f) {
                uefi_call_wrapper(direct_f->Close, 1, direct_f);
                direct_f = NULL;
            }
            *out_file = ff;
            if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, candidate);
            return EFI_SUCCESS;
        }
    }

    // Alias attempts failed. If direct open worked, return it.
    if (direct_ok && direct_f) {
        *out_file = direct_f;
        if (out_picked && out_picked_cap > 0) llmk_char16_copy_cap(out_picked, out_picked_cap, path);
        return EFI_SUCCESS;
    }
    return st;
}

static int llmk_try_guess_existing_fat83_alias(EFI_FILE_HANDLE Root,
                                               const CHAR16 *dir_path,
                                               const CHAR16 *leaf,
                                               CHAR16 *out_alias,
                                               int out_alias_cap) {
    if (out_alias && out_alias_cap > 0) out_alias[0] = 0;
    if (!Root || !leaf || !leaf[0] || !out_alias || out_alias_cap <= 1) return 0;
    if (llmk_char16_has_tilde(leaf)) return 0;

    const CHAR16 *dot = NULL;
    for (const CHAR16 *p = leaf; *p; p++) {
        if (*p == L'.') dot = p;
    }

    const CHAR16 *leaf_base = leaf;
    const CHAR16 *leaf_ext = NULL;
    int base_len = 0;
    if (dot && dot > leaf) {
        base_len = (int)(dot - leaf);
        leaf_ext = dot + 1;
    } else {
        base_len = (int)StrLen(leaf);
    }
    if (base_len <= 0) return 0;

    CHAR16 base_s[64]; // SAFE: sanitized base name scratch buffer; bounded by sizeof(base_s)-1
    CHAR16 ext_s[16]; // SAFE: sanitized extension scratch buffer; bounded to 3 chars + NUL
    int bn = 0;
    int en = 0;
    for (int i = 0; i < base_len && bn < (int)(sizeof(base_s) / sizeof(base_s[0])) - 1; i++) {
        CHAR16 c = leaf_base[i];
        if (llmk_char16_is_alnum(c)) base_s[bn++] = llmk_char16_toupper(c);
    }
    base_s[bn] = 0;
    if (leaf_ext) {
        for (const CHAR16 *p = leaf_ext; *p && en < (int)(sizeof(ext_s) / sizeof(ext_s[0])) - 1; p++) {
            CHAR16 c = *p;
            if (llmk_char16_is_alnum(c)) ext_s[en++] = llmk_char16_toupper(c);
            if (en >= 3) break;
        }
    }
    ext_s[en] = 0;
    if (bn <= 0) return 0;

    CHAR16 prefix6[8]; // SAFE: FIRST6 prefix + NUL
    int p6 = 0;
    for (int i = 0; i < bn && p6 < 6; i++) {
        prefix6[p6++] = base_s[i];
    }
    prefix6[p6] = 0;
    if (p6 <= 0) return 0;

    for (int n = 1; n <= 9; n++) {
        CHAR16 alias_leaf[32]; // SAFE: FAT 8.3 alias leaf (FIRST6~N[.EXT]) fits easily
        alias_leaf[0] = 0;
        StrCpy(alias_leaf, prefix6);
        StrCat(alias_leaf, L"~");
        {
            CHAR16 digit[2]; // SAFE: single digit + NUL
            digit[0] = (CHAR16)(L'0' + n);
            digit[1] = 0; // SAFE: terminator write within digit[2]
            StrCat(alias_leaf, digit);
        }
        if (en > 0) {
            StrCat(alias_leaf, L".");
            StrCat(alias_leaf, ext_s);
        }

        if (llmk_dir_contains_leaf_ci(Root, dir_path, alias_leaf)) {
            llmk_char16_copy_cap(out_alias, out_alias_cap, alias_leaf);
            return 1;
        }
    }
    return 0;
}

static void llmk_char16_copy_cap(CHAR16 *dst, int cap, const CHAR16 *src) {
    if (!dst || cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    int i = 0;
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int llmk_try_open_with_ext(EFI_FILE_HANDLE Root, const CHAR16 *base, const CHAR16 *ext, EFI_FILE_HANDLE *out_file, CHAR16 *out_path, int out_path_cap) {
    if (!Root || !base || !ext || !out_file) return 0;
    *out_file = NULL;

    CHAR16 path[192];
    path[0] = 0;
    llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), base);
    if (!llmk_char16_endswith_ci(path, ext)) {
        // Append extension
        UINTN cur = StrLen(path);
        UINTN exl = StrLen(ext);
        if (cur + exl + 1 >= (sizeof(path) / sizeof(path[0]))) return 0;
        StrCat(path, ext);
    }

    EFI_FILE_HANDLE f = NULL;
    CHAR16 picked[192];
    picked[0] = 0;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, path, &f, picked, (int)(sizeof(picked) / sizeof(picked[0])), L"model_ext");
    if (EFI_ERROR(st) || !f) return 0;
    *out_file = f;
    if (out_path && out_path_cap > 0) {
        if (picked[0]) llmk_char16_copy_cap(out_path, out_path_cap, picked);
        else llmk_char16_copy_cap(out_path, out_path_cap, path);
    }
    return 1;
}

static DjibionEngine g_djibion;
static DiopionEngine g_diopion;
static DiagnostionEngine g_diagnostion;
static MemorionEngine g_memorion;
static OrchestrionEngine g_orchestrion;
static CalibrionEngine g_calibrion;
static CompatibilionEngine g_compatibilion;
static EvolvionEngine g_evolvion;
static OoDriverProbe  g_oo_driver_probe; /* OO Driver System — PCI enumeration */
static SynaptionEngine g_synaption;
static ConscienceEngine g_conscience;
static NeuralfsEngine g_neuralfs;
static GhostEngine g_ghost;
static ImmunionEngine g_immunion;
static DreamionEngine g_dreamion;
static SymbionEngine g_symbion;
static CollectivionEngine g_collectivion;
static MetabionEngine g_metabion;
static CellionEngine g_cellion;
static MorphionEngine g_morphion;
static PheromionEngine g_pheromion;

/* Novel engines — Phase 2 (emotional, temporal, hunger, introspection, apoptosis) */
static LimbionEngine    g_limbion;      /* 2D affective state modulates inference */
static ChronionEngine   g_chronion;     /* temporal self-awareness (boot/step/DNA age) */
static TrophionEngine   g_trophion;     /* compute hunger (idle→verbose, gorged→terse) */
static MirrorionEngine  g_mirrorion;    /* self-introspection Q/A → OO_MIRROR.JSONL */
static ThanatosionEngine g_thanatosion; /* graceful death + DNA-preserving rebirth */

/* Phase E: OO Self-Model — runtime introspection */
static OoSelfModel g_oo_self_model;
/* Phase F: NeuralFS v2 — persistent RAM key-value store */
static Nfs2Store   g_nfs2;

// Forward declarations (used by early config loaders)
static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name);
static void llmk_cfg_trim(char **s);
static char llmk_cfg_tolower(char c);
static int llmk_cfg_streq_ci(const char *a, const char *b);
static int llmk_cfg_parse_i32(const char *s, int *out);
static int llmk_cfg_parse_f32(const char *s, float *out);
static int llmk_cfg_parse_bool(const char *s, int *out);
static void llmk_print_ascii(const char *s);

// Diopion burst runtime (sampling knobs override for N generations)
static int g_diopion_burst_active = 0;
static int g_diopion_burst_remaining = 0;
static int g_diopion_saved_max_gen_tokens = 0;
static int g_diopion_saved_top_k = 0;
static float g_diopion_saved_temperature = 0.0f;

static float llmk_temp_from_milli(UINT32 milli) {
    if (milli > 2000u) milli = 2000u;
    return (float)milli / 1000.0f;
}

static void llmk_diopion_burst_apply(UINT32 turns, UINT32 max_tokens, UINT32 topk, UINT32 temp_milli,
                                    int *io_max_gen_tokens, int *io_top_k, float *io_temperature) {
    if (!io_max_gen_tokens || !io_top_k || !io_temperature) return;
    if (turns == 0) return;

    if (!g_diopion_burst_active) {
        g_diopion_saved_max_gen_tokens = *io_max_gen_tokens;
        g_diopion_saved_top_k = *io_top_k;
        g_diopion_saved_temperature = *io_temperature;
        g_diopion_burst_active = 1;
    }

    g_diopion_burst_remaining = (int)turns;
    if (max_tokens > 0) *io_max_gen_tokens = (int)max_tokens;
    if (topk > 0) *io_top_k = (int)topk;
    if (temp_milli > 0) *io_temperature = llmk_temp_from_milli(temp_milli);
}

static void llmk_diopion_burst_finish_one(int *io_max_gen_tokens, int *io_top_k, float *io_temperature) {
    if (!g_diopion_burst_active) return;
    if (g_diopion_burst_remaining > 0) g_diopion_burst_remaining--;
    if (g_diopion_burst_remaining > 0) return;

    // Restore saved knobs
    if (io_max_gen_tokens) *io_max_gen_tokens = g_diopion_saved_max_gen_tokens;
    if (io_top_k) *io_top_k = g_diopion_saved_top_k;
    if (io_temperature) *io_temperature = g_diopion_saved_temperature;
    g_diopion_burst_active = 0;
}
