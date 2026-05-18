#include "oo-kernel/repl/repl.h"
#include <efi.h>
#include <efilib.h>
#include "oo-engine/wasm/oo_wasm.h"

extern OoWasmSandbox g_wasm_sandbox;
extern OoWasmMod g_wasm_mod;

static void wasm_ascii_to_char16(CHAR16 *dst, int cap, const char *src) {
    int i = 0;
    if (!dst || cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i + 1 < cap) {
        dst[i] = (CHAR16)(unsigned char)src[i];
        i++;
    }
    dst[i] = 0;
}

static int wasm_parse_i32_arg(const char **sp, INT32 *out) {
    const char *p = *sp;
    INT32 sign = 1;
    INT32 v = 0;
    int seen = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') {
        seen = 1;
        v = v * 10 + (INT32)(*p - '0');
        p++;
    }
    *sp = p;
    if (!seen) return 0;
    *out = sign * v;
    return 1;
}

static uint32_t wasm_parse_u32(const char *s, uint32_t fallback) {
    uint32_t v = 0;
    int seen = 0;
    if (!s) return fallback;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') {
        seen = 1;
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
    }
    return seen ? v : fallback;
}

static int cmd_wasm_status(const char *args, ReplInferState *s) {
    (void)args;
    (void)s;
    Print(L"[WASM] enabled=%d trapped=%d traps=%lu\r\n",
          (unsigned long)g_wasm_sandbox.enabled,
          (unsigned long)g_wasm_sandbox.trapped,
          (unsigned long)g_wasm_sandbox.trap_count);
    Print(L"[WASM] budget=%lu used=%lu depth=%lu/%lu mem=%lu/%lu\r\n",
          (unsigned long)g_wasm_sandbox.instruction_budget,
          (unsigned long)g_wasm_sandbox.instructions_used,
          (unsigned long)g_wasm_sandbox.current_call_depth,
          (unsigned long)g_wasm_sandbox.max_call_depth,
          (unsigned long)g_wasm_sandbox.memory_size,
          (unsigned long)g_wasm_sandbox.memory_limit);
    Print(L"[WASM] last=%a reason=%a pc=%lu off=%lu size=%lu\r\n",
          oo_wasm_trap_name(g_wasm_sandbox.last_error),
          g_wasm_sandbox.last_reason,
          (unsigned long)g_wasm_sandbox.last_pc,
          (unsigned long)g_wasm_sandbox.last_offset,
          (unsigned long)g_wasm_sandbox.last_access_size);
    Print(L"[WASM] module_loaded=%d module_size=%lu exports=%lu funcs=%lu\r\n",
          (unsigned long)g_wasm_mod.loaded,
          (unsigned long)g_wasm_mod.data_size,
          (unsigned long)g_wasm_mod.export_count,
          (unsigned long)g_wasm_mod.func_count);
    return 0;
}

static int cmd_wasm_budget(const char *args, ReplInferState *s) {
    uint32_t budget;
    (void)s;
    if (!args || !*args) {
        Print(L"Usage: /wasm_budget <instructions>\r\n");
        return 0;
    }
    budget = wasm_parse_u32(args, OO_WASM_DEFAULT_BUDGET);
    oo_wasm_set_budget(&g_wasm_sandbox, budget);
    Print(L"[WASM] instruction_budget=%lu\r\n", (unsigned long)g_wasm_sandbox.instruction_budget);
    return 0;
}

static int cmd_wasm_reset(const char *args, ReplInferState *s) {
    (void)args;
    (void)s;
    oo_wasm_reset(&g_wasm_sandbox);
    Print(L"[WASM] sandbox state reset\r\n");
    return 0;
}

static int cmd_wasm_enable(const char *args, ReplInferState *s) {
    (void)s;
    if (!args || !*args) {
        Print(L"Usage: /wasm_enable <0|1>\r\n");
        return 0;
    }
    oo_wasm_set_enabled(&g_wasm_sandbox, (*args == '0') ? 0 : 1);
    Print(L"[WASM] enabled=%d\r\n", (unsigned long)g_wasm_sandbox.enabled);
    return 0;
}

static int cmd_wasm_info(const char *args, ReplInferState *s) {
    (void)args;
    (void)s;
    oo_wasm_print_info(&g_wasm_mod, &g_wasm_sandbox);
    return 0;
}

static int cmd_wasm_unload(const char *args, ReplInferState *s) {
    (void)args;
    (void)s;
    oo_wasm_unload(&g_wasm_mod);
    Print(L"[WASM] module unloaded\r\n");
    return 0;
}

static int cmd_wasm_load(const char *args, ReplInferState *s) {
    CHAR16 path16[128];
    EFI_STATUS st;
    (void)s;
    if (!args || !*args) {
        Print(L"Usage: /wasm_load <path>\r\n");
        return 0;
    }
    wasm_ascii_to_char16(path16, 128, args);
    st = oo_wasm_load_file(&g_wasm_sandbox, &g_wasm_mod, (EFI_FILE_HANDLE)0, path16);
    if (EFI_ERROR(st)) {
        Print(L"[WASM] load failed: %r error=%a\r\n", st, g_wasm_mod.error);
    } else {
        oo_wasm_print_info(&g_wasm_mod, &g_wasm_sandbox);
    }
    return 0;
}

static int cmd_wasm_call(const char *args, ReplInferState *s) {
    CHAR8 func_name[64];
    INT32 values[8];
    UINT32 nargs = 0;
    UINTN i = 0;
    INT32 result = 0;
    EFI_STATUS st;
    const char *p = args;
    (void)s;
    if (!args || !*args) {
        Print(L"Usage: /wasm_call <func> [i32 args...]\r\n");
        return 0;
    }
    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != ' ' && i + 1 < sizeof(func_name)) {
        func_name[i++] = (CHAR8)*p++;
    }
    func_name[i] = 0;
    while (*p && nargs < 8) {
        INT32 v = 0;
        if (!wasm_parse_i32_arg(&p, &v)) break;
        values[nargs++] = v;
    }
    st = oo_wasm_call_i32(&g_wasm_sandbox, &g_wasm_mod, func_name, values, nargs, &result);
    if (EFI_ERROR(st)) {
        Print(L"[WASM] call failed: %r error=%a trap=%a\r\n",
              st, g_wasm_mod.error, oo_wasm_trap_name(g_wasm_sandbox.last_error));
    } else {
        Print(L"[WASM] result=%d (0x%08x)\r\n", result, (UINT32)result);
    }
    return 0;
}

static const ReplCmd CMD_WASM_STATUS = { "/wasm_status", "Show WASM sandbox state", cmd_wasm_status };
static const ReplCmd CMD_WASM_BUDGET = { "/wasm_budget", "Set WASM instruction budget", cmd_wasm_budget };
static const ReplCmd CMD_WASM_RESET  = { "/wasm_reset", "Reset WASM sandbox trap/runtime state", cmd_wasm_reset };
static const ReplCmd CMD_WASM_ENABLE = { "/wasm_enable", "Enable or disable WASM sandbox", cmd_wasm_enable };
static const ReplCmd CMD_WASM_INFO   = { "/wasm_info", "Show loaded WASM module info", cmd_wasm_info };
static const ReplCmd CMD_WASM_UNLOAD = { "/wasm_unload", "Unload current WASM module", cmd_wasm_unload };
static const ReplCmd CMD_WASM_LOAD   = { "/wasm_load", "Load a WASM module from EFI filesystem", cmd_wasm_load };
static const ReplCmd CMD_WASM_CALL   = { "/wasm_call", "Call an exported WASM i32 function", cmd_wasm_call };

void cmd_wasm_register(void) {
    repl_register_cmd(&CMD_WASM_STATUS);
    repl_register_cmd(&CMD_WASM_BUDGET);
    repl_register_cmd(&CMD_WASM_RESET);
    repl_register_cmd(&CMD_WASM_ENABLE);
    repl_register_cmd(&CMD_WASM_INFO);
    repl_register_cmd(&CMD_WASM_UNLOAD);
    repl_register_cmd(&CMD_WASM_LOAD);
    repl_register_cmd(&CMD_WASM_CALL);
}
