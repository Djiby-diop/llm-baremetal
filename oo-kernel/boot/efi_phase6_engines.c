#include "oo-kernel/boot/efi_entry.h"
#include "oo-bus/hermes/oo_bus_channels.h"
#include "oo-engine/wasm/oo_wasm.h"
#include "oo-engine/ssm/oo_somamind_v1.h"
#include "oo-kernel/repl/oo_repl.h"

extern SomaMindV1 g_somamind;
extern OoWasmSandbox g_wasm_sandbox;
extern OoWasmMod g_wasm_mod;

static OoBootCtx *g_sm_tool_ctx = (OoBootCtx *)0;

static int sm_tool_save_memory(const char *args, char *out, int cap) {
    const char *m = "memory_saved";
    int i = 0;
    (void)args;
    if (g_sm_tool_ctx) oo_repl_try(g_sm_tool_ctx, "/kvc save");
    while (m[i] && i < cap - 1) { out[i] = m[i]; i++; }
    out[i] = '\0';
    return i;
}

static int sm_tool_list_memory(const char *args, char *out, int cap) {
    const char *m = "memory_listed";
    int i = 0;
    (void)args;
    if (g_sm_tool_ctx) oo_repl_try(g_sm_tool_ctx, "/pressure");
    while (m[i] && i < cap - 1) { out[i] = m[i]; i++; }
    out[i] = '\0';
    return i;
}

static int sm_tool_train(const char *args, char *out, int cap) {
    const char *m = "train_flushed";
    int i = 0;
    (void)args;
    if (g_sm_tool_ctx) oo_repl_try(g_sm_tool_ctx, "/train flush");
    while (m[i] && i < cap - 1) { out[i] = m[i]; i++; }
    out[i] = '\0';
    return i;
}

static int sm_tool_smp_status(const char *args, char *out, int cap) {
    const char *m = "smp_status_printed";
    int i = 0;
    (void)args;
    if (g_sm_tool_ctx) oo_repl_try(g_sm_tool_ctx, "/splitbrain");
    while (m[i] && i < cap - 1) { out[i] = m[i]; i++; }
    out[i] = '\0';
    return i;
}

static int sm_tool_oo_status(const char *args, char *out, int cap) {
    const char *m = "oo_status_printed";
    int i = 0;
    (void)args;
    if (g_sm_tool_ctx) oo_repl_try(g_sm_tool_ctx, "/soma_state");
    while (m[i] && i < cap - 1) { out[i] = m[i]; i++; }
    out[i] = '\0';
    return i;
}

static int sm_tool_shutdown(const char *args, char *out, int cap) {
    const char *m = "shutdown_requested";
    int i = 0;
    (void)args;
    if (g_sm_tool_ctx) oo_repl_try(g_sm_tool_ctx, "/oo_ext_help");
    while (m[i] && i < cap - 1) { out[i] = m[i]; i++; }
    out[i] = '\0';
    return i;
}

EFI_STATUS efi_phase6_engines(OoBootCtx *ctx) {
    llmk_overlay_stage(7, 7);

    oo_bus_init(NULL, ctx);
    oo_wasm_init(&g_wasm_sandbox);
    oo_wasm_unload(&g_wasm_mod);
    oo_wasm_bind_root(ctx->efi_root);

    g_sm_tool_ctx = ctx;
    sm_init(&g_somamind, 384);
    sm_register_tool(&g_somamind, "save_memory", sm_tool_save_memory);
    sm_register_tool(&g_somamind, "list_memory", sm_tool_list_memory);
    sm_register_tool(&g_somamind, "train",       sm_tool_train);
    sm_register_tool(&g_somamind, "smp_status",  sm_tool_smp_status);
    sm_register_tool(&g_somamind, "oo_status",   sm_tool_oo_status);
    sm_register_tool(&g_somamind, "shutdown",    sm_tool_shutdown);

    Print(L"[SM] SomaMind V1 ready (dim=%d budget=384 tools=%d)\r\n",
          (UINTN)SOMAMIND_HIDDEN_DIM, (UINTN)g_somamind.tools.n_tools);

    ctx->phase6_ok = 1;
    return EFI_SUCCESS;
}
