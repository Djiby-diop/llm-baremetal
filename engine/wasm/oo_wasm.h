/* oo_wasm.h — OO Bare-Metal WASM Module Loader (Phase 10A)
 * =========================================================
 * Minimal WebAssembly binary format parser + interpreter.
 * No libc, no OS. Freestanding C11. EFI file load.
 *
 * Supports:
 *   - MVP (WASM 1.0) binary format
 *   - i32/i64 arithmetic + local vars + call
 *   - EFI filesystem load
 *
 * Usage:
 *   OoWasmMod mod;
 *   oo_wasm_load_file(&mod, root, L"module.wasm");
 *   INT32 result;
 *   oo_wasm_call_i32(&mod, "add", &arg, 1, &result);
 */
#ifndef OO_WASM_H
#define OO_WASM_H

#include <efi.h>

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define OO_WASM_MAX_FUNCS     64
#define OO_WASM_MAX_EXPORTS   64
#define OO_WASM_MAX_LOCALS    32
#define OO_WASM_MAX_STACK     256
#define OO_WASM_MAX_MEMORY    (64 * 1024)   /* 64 KB linear memory */
#define OO_WASM_MAX_CODE_SIZE (256 * 1024)  /* 256 KB max module size */

/* ── WASM value type ─────────────────────────────────────────────────────── */
typedef enum {
    OO_WASM_I32 = 0x7F,
    OO_WASM_I64 = 0x7E,
    OO_WASM_F32 = 0x7D,
    OO_WASM_F64 = 0x7C,
} OoWasmValType;

/* ── WASM value ──────────────────────────────────────────────────────────── */
typedef struct {
    OoWasmValType type;
    union {
        INT32  i32;
        INT64  i64;
        UINT32 u32;
        UINT64 u64;
    };
} OoWasmVal;

/* ── Function type (signature) ───────────────────────────────────────────── */
typedef struct {
    UINT8 param_count;
    UINT8 result_count;
    OoWasmValType params[8];
    OoWasmValType results[4];
} OoWasmFuncType;

/* ── Function code entry ─────────────────────────────────────────────────── */
typedef struct {
    UINT32        type_idx;
    UINT32        code_offset;  /* byte offset in mod->data */
    UINT32        code_size;
    UINT8         local_count;
    OoWasmValType locals[OO_WASM_MAX_LOCALS];
} OoWasmFunc;

/* ── Export entry ────────────────────────────────────────────────────────── */
typedef struct {
    CHAR8  name[64];
    UINT8  kind;          /* 0=func, 1=table, 2=mem, 3=global */
    UINT32 index;
} OoWasmExport;

/* ── Module ──────────────────────────────────────────────────────────────── */
typedef struct {
    UINT8   *data;          /* EFI AllocatePool'd raw bytes */
    UINTN    data_size;

    UINT32   type_count;
    OoWasmFuncType types[OO_WASM_MAX_FUNCS];

    UINT32   func_count;
    OoWasmFunc funcs[OO_WASM_MAX_FUNCS];

    UINT32   export_count;
    OoWasmExport exports[OO_WASM_MAX_EXPORTS];

    UINT8    mem[OO_WASM_MAX_MEMORY]; /* linear memory */
    UINT32   mem_pages;

    int      loaded;
    CHAR8    error[128];
} OoWasmMod;

/* ── API ─────────────────────────────────────────────────────────────────── */

EFI_STATUS oo_wasm_load_file(OoWasmMod *mod, EFI_FILE_HANDLE root,
                              const CHAR16 *path);

EFI_STATUS oo_wasm_load_buf(OoWasmMod *mod,
                             const UINT8 *buf, UINTN size);

/* Call exported function — args[nargs] in, ret = first result (may be NULL) */
EFI_STATUS oo_wasm_call(OoWasmMod *mod,
                         const CHAR8 *func_name,
                         OoWasmVal *args, UINT32 nargs,
                         OoWasmVal *ret);

EFI_STATUS oo_wasm_call_i32(OoWasmMod *mod,
                              const CHAR8 *func_name,
                              INT32 *args, UINT32 nargs,
                              INT32 *result);

void oo_wasm_unload(OoWasmMod *mod);
void oo_wasm_print_info(const OoWasmMod *mod);

/* REPL: /wasm_load <path>, /wasm_call <func> [i32 args], /wasm_info */
int oo_wasm_repl_cmd(OoWasmMod *mod, EFI_FILE_HANDLE root,
                     const char *cmd);

#endif /* OO_WASM_H */
