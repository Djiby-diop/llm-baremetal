#ifndef OO_ENGINE_WASM_OO_WASM_H
#define OO_ENGINE_WASM_OO_WASM_H

#include <stdint.h>
#include <efi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OO_WASM_DEFAULT_BUDGET      100000u
#define OO_WASM_DEFAULT_MEMORY_MAX   65536u
#define OO_WASM_DEFAULT_CALL_DEPTH      32u

#define OO_WASM_MAX_FUNCS     64
#define OO_WASM_MAX_EXPORTS   64
#define OO_WASM_MAX_LOCALS    32
#define OO_WASM_MAX_STACK     256
#define OO_WASM_MAX_MEMORY    (64 * 1024u)
#define OO_WASM_MAX_CODE_SIZE (256 * 1024u)

typedef enum {
    OO_WASM_TRAP_NONE     = 0,
    OO_WASM_TRAP_OOB      = 2,
    OO_WASM_TRAP_CORRUPT  = 3,
    OO_WASM_TRAP_BUDGET   = 4,
    OO_WASM_TRAP_DEPTH    = 5,
    OO_WASM_TRAP_DISABLED = 6,
} OoWasmTrapCode;

typedef enum {
    OO_WASM_I32 = 0x7F,
    OO_WASM_I64 = 0x7E,
    OO_WASM_F32 = 0x7D,
    OO_WASM_F64 = 0x7C,
} OoWasmValType;

typedef struct {
    uint8_t  enabled;
    uint8_t  trapped;
    uint16_t reserved0;

    uint32_t instruction_budget;
    uint32_t instructions_used;
    uint32_t trap_count;

    uint32_t current_call_depth;
    uint32_t max_call_depth;

    uint32_t memory_size;
    uint32_t memory_limit;

    uint32_t last_error;
    uint32_t last_pc;
    uint32_t last_offset;
    uint32_t last_access_size;

    char     last_reason[48];
} OoWasmSandbox;

typedef struct {
    OoWasmValType type;
    union {
        INT32  i32;
        INT64  i64;
        UINT32 u32;
        UINT64 u64;
    };
} OoWasmVal;

typedef struct {
    UINT8 param_count;
    UINT8 result_count;
    OoWasmValType params[8];
    OoWasmValType results[4];
} OoWasmFuncType;

typedef struct {
    UINT32        type_idx;
    UINT32        code_offset;
    UINT32        code_size;
    UINT8         local_count;
    OoWasmValType locals[OO_WASM_MAX_LOCALS];
} OoWasmFunc;

typedef struct {
    CHAR8  name[64];
    UINT8  kind;
    UINT32 index;
} OoWasmExport;

typedef struct {
    UINT8   *data;
    UINTN    data_size;

    UINT32   type_count;
    OoWasmFuncType types[OO_WASM_MAX_FUNCS];

    UINT32   func_count;
    OoWasmFunc funcs[OO_WASM_MAX_FUNCS];

    UINT32   export_count;
    OoWasmExport exports[OO_WASM_MAX_EXPORTS];

    UINT8    mem[OO_WASM_MAX_MEMORY];
    UINT32   mem_pages;

    int      loaded;
    CHAR8    error[128];
} OoWasmMod;

extern OoWasmSandbox g_wasm_sandbox;
extern OoWasmMod     g_wasm_mod;

void oo_wasm_init(OoWasmSandbox *ws);
void oo_wasm_reset(OoWasmSandbox *ws);
void oo_wasm_set_enabled(OoWasmSandbox *ws, int enabled);
void oo_wasm_set_budget(OoWasmSandbox *ws, uint32_t budget);
void oo_wasm_set_memory_limit(OoWasmSandbox *ws, uint32_t bytes);
void oo_wasm_set_call_depth_limit(OoWasmSandbox *ws, uint32_t depth);
int  oo_wasm_begin(OoWasmSandbox *ws, uint32_t memory_size);
int  oo_wasm_consume_instructions(OoWasmSandbox *ws, uint32_t count, uint32_t pc);
int  oo_wasm_enter_call(OoWasmSandbox *ws, uint32_t pc);
void oo_wasm_leave_call(OoWasmSandbox *ws);
int  oo_wasm_check_bounds(OoWasmSandbox *ws, uint32_t offset, uint32_t size, uint32_t pc);
int  oo_wasm_mem_read_u32(OoWasmSandbox *ws, const uint8_t *mem,
                          uint32_t offset, uint32_t pc, uint32_t *out_value);
int  oo_wasm_mem_write_u32(OoWasmSandbox *ws, uint8_t *mem,
                           uint32_t offset, uint32_t pc, uint32_t value);
const char *oo_wasm_trap_name(uint32_t code);

void       oo_wasm_bind_root(EFI_FILE_HANDLE root);
EFI_STATUS oo_wasm_load_file(OoWasmSandbox *ws, OoWasmMod *mod,
                             EFI_FILE_HANDLE root, const CHAR16 *path);
EFI_STATUS oo_wasm_load_buf(OoWasmSandbox *ws, OoWasmMod *mod,
                            const UINT8 *buf, UINTN size);
EFI_STATUS oo_wasm_call(OoWasmSandbox *ws, OoWasmMod *mod,
                        const CHAR8 *func_name, OoWasmVal *args, UINT32 nargs,
                        OoWasmVal *ret);
EFI_STATUS oo_wasm_call_i32(OoWasmSandbox *ws, OoWasmMod *mod,
                            const CHAR8 *func_name, INT32 *args, UINT32 nargs,
                            INT32 *result);
void       oo_wasm_unload(OoWasmMod *mod);
void       oo_wasm_print_info(const OoWasmMod *mod, const OoWasmSandbox *ws);

#ifdef __cplusplus
}
#endif

#endif
