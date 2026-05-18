#include "oo_wasm.h"
#include <efi.h>
#include <efilib.h>
#include "oo-bus/hermes/oo_bus_channels.h"
#include "oo-bus/hermes/oo_bus_router.h"

#define WASM_SEC_TYPE     1
#define WASM_SEC_FUNC     3
#define WASM_SEC_EXPORT   7
#define WASM_SEC_CODE    10
#define WASM_SEC_DATA    11

static EFI_FILE_HANDLE g_oo_wasm_root = (EFI_FILE_HANDLE)0;

static void _wm_memset(void *p, int v, UINTN n) {
    UINT8 *b = (UINT8 *)p;
    while (n--) *b++ = (UINT8)v;
}

static void _wm_memcpy(void *d, const void *s, UINTN n) {
    UINT8 *dd = (UINT8 *)d;
    const UINT8 *ss = (const UINT8 *)s;
    while (n--) *dd++ = *ss++;
}

static UINTN _wm_strlen(const CHAR8 *s) {
    UINTN i = 0;
    while (s && s[i]) i++;
    return i;
}

static int _wm_strcmp(const CHAR8 *a, const CHAR8 *b) {
    if (!a || !b) return 1;
    while (*a && *a == *b) { a++; b++; }
    return (int)(UINT8)*a - (int)(UINT8)*b;
}

static UINT32 _wm_leb_u32(const UINT8 *p, UINT32 *out) {
    UINT32 r = 0, shift = 0, i = 0;
    do {
        r |= (UINT32)(p[i] & 0x7F) << shift;
        shift += 7;
    } while (p[i++] & 0x80);
    *out = r;
    return i;
}

static UINT32 _wm_leb_i32(const UINT8 *p, INT32 *out) {
    INT32 r = 0;
    UINT32 shift = 0, i = 0;
    UINT8 b;
    do {
        b = p[i++];
        r |= (INT32)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 32 && (b & 0x40)) r |= -((INT32)1 << shift);
    *out = r;
    return i;
}

static UINT32 _wm_leb_i64(const UINT8 *p, INT64 *out) {
    INT64 r = 0;
    UINT32 shift = 0, i = 0;
    UINT8 b;
    do {
        b = p[i++];
        r |= (INT64)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 64 && (b & 0x40)) r |= -((INT64)1 << shift);
    *out = r;
    return i;
}

static void _wm_err(OoWasmMod *m, const CHAR8 *msg) {
    UINTN n;
    if (!m) return;
    n = _wm_strlen(msg);
    if (n >= sizeof(m->error)) n = sizeof(m->error) - 1;
    _wm_memcpy(m->error, msg, n);
    m->error[n] = 0;
}

static void oo_wasm_copy_reason(char *dst, const char *src) {
    int i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < 48) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int oo_wasm_trip(OoWasmSandbox *ws, OoWasmTrapCode code,
                        uint32_t pc, uint32_t offset, uint32_t size,
                        const char *reason) {
    OoBusSentinelTrip trip;
    if (!ws) return -1;
    ws->trapped = 1;
    ws->last_error = (uint32_t)code;
    ws->last_pc = pc;
    ws->last_offset = offset;
    ws->last_access_size = size;
    ws->trap_count++;
    oo_wasm_copy_reason(ws->last_reason, reason);
    trip.error_code = (uint32_t)code;
    trip.ptr = (((uint64_t)pc) << 32) | (uint64_t)offset;
    trip.size = (uint64_t)size;
    oo_bus_post(OO_CH_SENTINEL_TRIP, &trip, (uint32_t)sizeof(trip));
    return -1;
}

const char *oo_wasm_trap_name(uint32_t code) {
    switch ((OoWasmTrapCode)code) {
        case OO_WASM_TRAP_NONE:     return "ok";
        case OO_WASM_TRAP_OOB:      return "oob";
        case OO_WASM_TRAP_CORRUPT:  return "corrupt";
        case OO_WASM_TRAP_BUDGET:   return "budget";
        case OO_WASM_TRAP_DEPTH:    return "depth";
        case OO_WASM_TRAP_DISABLED: return "disabled";
        default:                    return "unknown";
    }
}

void oo_wasm_init(OoWasmSandbox *ws) {
    if (!ws) return;
    ws->enabled = 1;
    ws->trapped = 0;
    ws->reserved0 = 0;
    ws->instruction_budget = OO_WASM_DEFAULT_BUDGET;
    ws->instructions_used = 0;
    ws->trap_count = 0;
    ws->current_call_depth = 0;
    ws->max_call_depth = OO_WASM_DEFAULT_CALL_DEPTH;
    ws->memory_size = 0;
    ws->memory_limit = OO_WASM_DEFAULT_MEMORY_MAX;
    ws->last_error = OO_WASM_TRAP_NONE;
    ws->last_pc = 0;
    ws->last_offset = 0;
    ws->last_access_size = 0;
    ws->last_reason[0] = 0;
}

void oo_wasm_reset(OoWasmSandbox *ws) {
    if (!ws) return;
    ws->trapped = 0;
    ws->instructions_used = 0;
    ws->current_call_depth = 0;
    ws->memory_size = 0;
    ws->last_error = OO_WASM_TRAP_NONE;
    ws->last_pc = 0;
    ws->last_offset = 0;
    ws->last_access_size = 0;
    ws->last_reason[0] = 0;
}

void oo_wasm_set_enabled(OoWasmSandbox *ws, int enabled) {
    if (!ws) return;
    ws->enabled = (enabled != 0) ? 1u : 0u;
}

void oo_wasm_set_budget(OoWasmSandbox *ws, uint32_t budget) {
    if (!ws) return;
    ws->instruction_budget = (budget == 0) ? OO_WASM_DEFAULT_BUDGET : budget;
}

void oo_wasm_set_memory_limit(OoWasmSandbox *ws, uint32_t bytes) {
    if (!ws) return;
    ws->memory_limit = (bytes == 0) ? OO_WASM_DEFAULT_MEMORY_MAX : bytes;
}

void oo_wasm_set_call_depth_limit(OoWasmSandbox *ws, uint32_t depth) {
    if (!ws) return;
    ws->max_call_depth = (depth == 0) ? OO_WASM_DEFAULT_CALL_DEPTH : depth;
}

int oo_wasm_begin(OoWasmSandbox *ws, uint32_t memory_size) {
    if (!ws) return -1;
    oo_wasm_reset(ws);
    if (!ws->enabled) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_DISABLED, 0, 0, 0, "sandbox disabled");
    }
    ws->memory_size = memory_size;
    if (memory_size > ws->memory_limit) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_OOB, 0, memory_size, memory_size, "memory limit");
    }
    return 0;
}

int oo_wasm_consume_instructions(OoWasmSandbox *ws, uint32_t count, uint32_t pc) {
    uint32_t next_used;
    if (!ws) return -1;
    if (ws->trapped) return -1;
    next_used = ws->instructions_used + count;
    if (next_used < ws->instructions_used) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_CORRUPT, pc, 0, count, "instruction counter overflow");
    }
    ws->instructions_used = next_used;
    if (ws->instructions_used > ws->instruction_budget) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_BUDGET, pc, ws->instructions_used, count, "instruction budget exceeded");
    }
    return 0;
}

int oo_wasm_enter_call(OoWasmSandbox *ws, uint32_t pc) {
    if (!ws) return -1;
    if (ws->trapped) return -1;
    if (ws->current_call_depth >= ws->max_call_depth) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_DEPTH, pc, ws->current_call_depth, 1, "call depth exceeded");
    }
    ws->current_call_depth++;
    return 0;
}

void oo_wasm_leave_call(OoWasmSandbox *ws) {
    if (!ws) return;
    if (ws->current_call_depth > 0) ws->current_call_depth--;
}

int oo_wasm_check_bounds(OoWasmSandbox *ws, uint32_t offset, uint32_t size, uint32_t pc) {
    uint32_t end;
    if (!ws) return -1;
    if (ws->trapped) return -1;
    if (size == 0) return 0;
    end = offset + size;
    if (end < offset) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_CORRUPT, pc, offset, size, "offset overflow");
    }
    if (end > ws->memory_size || end > ws->memory_limit) {
        return oo_wasm_trip(ws, OO_WASM_TRAP_OOB, pc, offset, size, "out-of-bounds memory access");
    }
    return 0;
}

int oo_wasm_mem_read_u32(OoWasmSandbox *ws, const uint8_t *mem,
                         uint32_t offset, uint32_t pc, uint32_t *out_value) {
    if (!mem || !out_value) return -1;
    if (oo_wasm_check_bounds(ws, offset, 4u, pc) != 0) return -1;
    *out_value =
        ((uint32_t)mem[offset + 0]) |
        ((uint32_t)mem[offset + 1] << 8) |
        ((uint32_t)mem[offset + 2] << 16) |
        ((uint32_t)mem[offset + 3] << 24);
    return 0;
}

int oo_wasm_mem_write_u32(OoWasmSandbox *ws, uint8_t *mem,
                          uint32_t offset, uint32_t pc, uint32_t value) {
    if (!mem) return -1;
    if (oo_wasm_check_bounds(ws, offset, 4u, pc) != 0) return -1;
    mem[offset + 0] = (uint8_t)(value & 0xFFu);
    mem[offset + 1] = (uint8_t)((value >> 8) & 0xFFu);
    mem[offset + 2] = (uint8_t)((value >> 16) & 0xFFu);
    mem[offset + 3] = (uint8_t)((value >> 24) & 0xFFu);
    return 0;
}

void oo_wasm_bind_root(EFI_FILE_HANDLE root) {
    g_oo_wasm_root = root;
}

static void oo_wasm_mod_clear(OoWasmMod *m) {
    if (!m) return;
    _wm_memset(m, 0, sizeof(*m));
}

static int _parse_types(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off = 0, count = 0, n;
    n = _wm_leb_u32(p + off, &count); off += n;
    if (count > OO_WASM_MAX_FUNCS) count = OO_WASM_MAX_FUNCS;
    m->type_count = count;
    for (UINT32 i = 0; i < count && off < size; i++) {
        if (p[off++] != 0x60) { _wm_err(m, (const CHAR8 *)"bad functype"); return 0; }
        UINT32 pc = 0; n = _wm_leb_u32(p + off, &pc); off += n;
        m->types[i].param_count = (UINT8)(pc > 8 ? 8 : pc);
        for (UINT32 k = 0; k < pc && off < size; k++) {
            if (k < 8) m->types[i].params[k] = (OoWasmValType)p[off];
            off++;
        }
        UINT32 rc = 0; n = _wm_leb_u32(p + off, &rc); off += n;
        m->types[i].result_count = (UINT8)(rc > 4 ? 4 : rc);
        for (UINT32 k = 0; k < rc && off < size; k++) {
            if (k < 4) m->types[i].results[k] = (OoWasmValType)p[off];
            off++;
        }
    }
    return 1;
}

static int _parse_funcs(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off = 0, count = 0, n;
    n = _wm_leb_u32(p + off, &count); off += n;
    if (count > OO_WASM_MAX_FUNCS) count = OO_WASM_MAX_FUNCS;
    m->func_count = count;
    for (UINT32 i = 0; i < count && off < size; i++) {
        UINT32 ti = 0; n = _wm_leb_u32(p + off, &ti); off += n;
        m->funcs[i].type_idx = ti;
    }
    return 1;
}

static int _parse_exports(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off = 0, count = 0, n;
    n = _wm_leb_u32(p + off, &count); off += n;
    if (count > OO_WASM_MAX_EXPORTS) count = OO_WASM_MAX_EXPORTS;
    m->export_count = count;
    for (UINT32 i = 0; i < count && off < size; i++) {
        UINT32 nl = 0, cp;
        n = _wm_leb_u32(p + off, &nl); off += n;
        cp = nl < 63 ? nl : 63;
        _wm_memcpy(m->exports[i].name, p + off, cp);
        m->exports[i].name[cp] = 0;
        off += nl;
        m->exports[i].kind = p[off++];
        {
            UINT32 idx = 0;
            n = _wm_leb_u32(p + off, &idx); off += n;
            m->exports[i].index = idx;
        }
    }
    return 1;
}

static int _parse_code(OoWasmMod *m, const UINT8 *p, UINT32 size, UINT32 sec_data_offset) {
    UINT32 off = 0, count = 0, n;
    n = _wm_leb_u32(p + off, &count); off += n;
    if (count > m->func_count) count = m->func_count;
    for (UINT32 i = 0; i < count && off < size; i++) {
        UINT32 body_size = 0, body_start, local_groups = 0;
        UINT8 lc = 0;
        n = _wm_leb_u32(p + off, &body_size); off += n;
        body_start = off;
        n = _wm_leb_u32(p + off, &local_groups); off += n;
        for (UINT32 g = 0; g < local_groups && off < size; g++) {
            UINT32 cnt = 0;
            OoWasmValType vt;
            n = _wm_leb_u32(p + off, &cnt); off += n;
            vt = (OoWasmValType)p[off++];
            for (UINT32 k = 0; k < cnt && lc < OO_WASM_MAX_LOCALS; k++) {
                m->funcs[i].locals[lc++] = vt;
            }
        }
        m->funcs[i].local_count = lc;
        m->funcs[i].code_offset = sec_data_offset + off;
        m->funcs[i].code_size = body_size - (off - body_start);
        off = body_start + body_size;
    }
    return 1;
}

static int _parse_data(OoWasmSandbox *ws, OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off = 0, count = 0, n;
    n = _wm_leb_u32(p + off, &count); off += n;
    for (UINT32 i = 0; i < count && off < size; i++) {
        UINT32 mem_idx = 0, dl = 0;
        UINT32 addr = 0;
        INT32 v = 0;
        n = _wm_leb_u32(p + off, &mem_idx); off += n;
        if (p[off] == 0x41) {
            off++;
            n = _wm_leb_i32(p + off, &v); off += n;
            addr = (UINT32)v;
        }
        if (p[off] == 0x0B) off++;
        n = _wm_leb_u32(p + off, &dl); off += n;
        if (oo_wasm_check_bounds(ws, addr, dl, off) == 0) {
            _wm_memcpy(m->mem + addr, p + off, dl);
        }
        off += dl;
    }
    return 1;
}

typedef struct {
    OoWasmVal slots[OO_WASM_MAX_STACK];
    INT32 top;
} _WasmStack;

static void _stk_push(_WasmStack *s, OoWasmVal v) {
    if (s->top + 1 < OO_WASM_MAX_STACK) s->slots[++s->top] = v;
}

static OoWasmVal _stk_pop(_WasmStack *s) {
    OoWasmVal z;
    _wm_memset(&z, 0, sizeof(z));
    if (s->top < 0) return z;
    return s->slots[s->top--];
}

static OoWasmVal _stk_peek(_WasmStack *s) {
    OoWasmVal z;
    _wm_memset(&z, 0, sizeof(z));
    if (s->top < 0) return z;
    return s->slots[s->top];
}

static OoWasmVal _mki32(INT32 v) { OoWasmVal x; x.type = OO_WASM_I32; x.i32 = v; return x; }
static OoWasmVal _mki64(INT64 v) { OoWasmVal x; x.type = OO_WASM_I64; x.i64 = v; return x; }

static EFI_STATUS _exec(OoWasmSandbox *ws, OoWasmMod *m, UINT32 func_idx,
                        OoWasmVal *args, UINT32 nargs, OoWasmVal *ret_val) {
    OoWasmFunc *fn;
    OoWasmFuncType *ft;
    OoWasmVal locals[OO_WASM_MAX_LOCALS];
    _WasmStack stk;
    const UINT8 *code;
    UINT32 ip = 0, code_size;
    INT32 block_depth = 0;

    if (func_idx >= m->func_count) return EFI_NOT_FOUND;
    if (oo_wasm_enter_call(ws, func_idx) != 0) return EFI_ABORTED;

    fn = &m->funcs[func_idx];
    ft = &m->types[fn->type_idx];
    _wm_memset(locals, 0, sizeof(locals));
    for (UINT32 i = 0; i < nargs && i < (UINT32)ft->param_count; i++) locals[i] = args[i];

    stk.top = -1;
    code = m->data + fn->code_offset;
    code_size = fn->code_size;

    while (ip < code_size) {
        UINT8 op = code[ip++];
        if (oo_wasm_consume_instructions(ws, 1, ip - 1) != 0) {
            oo_wasm_leave_call(ws);
            return EFI_ABORTED;
        }

        switch (op) {
            case 0x00:
                _wm_err(m, (const CHAR8 *)"unreachable");
                oo_wasm_trip(ws, OO_WASM_TRAP_CORRUPT, ip - 1, 0, 0, "unreachable");
                oo_wasm_leave_call(ws);
                return EFI_ABORTED;
            case 0x01:
                break;
            case 0x02:
            case 0x03:
                ip++;
                block_depth++;
                break;
            case 0x04: {
                OoWasmVal cond;
                ip++;
                cond = _stk_pop(&stk);
                if (!cond.i32) {
                    INT32 d = 1;
                    while (ip < code_size && d > 0) {
                        UINT8 b = code[ip++];
                        if (b == 0x02 || b == 0x03 || b == 0x04) d++;
                        else if (b == 0x0B) d--;
                        else if (b == 0x05 && d == 1) d = 0;
                    }
                } else {
                    block_depth++;
                }
                break;
            }
            case 0x05: {
                INT32 d = 1;
                while (ip < code_size && d > 0) {
                    UINT8 b = code[ip++];
                    if (b == 0x02 || b == 0x03 || b == 0x04) d++;
                    else if (b == 0x0B) d--;
                }
                if (block_depth > 0) block_depth--;
                break;
            }
            case 0x0B:
                if (block_depth > 0) { block_depth--; break; }
                goto done;
            case 0x0C: {
                UINT32 depth = 0;
                ip += _wm_leb_u32(code + ip, &depth);
                goto done;
            }
            case 0x0D: {
                UINT32 depth = 0;
                OoWasmVal c;
                ip += _wm_leb_u32(code + ip, &depth);
                c = _stk_pop(&stk);
                if (c.i32) goto done;
                break;
            }
            case 0x0F:
                goto done;
            case 0x10: {
                UINT32 fi = 0;
                ip += _wm_leb_u32(code + ip, &fi);
                if (fi < m->func_count) {
                    OoWasmFuncType *cft = &m->types[m->funcs[fi].type_idx];
                    OoWasmVal cargs[8];
                    UINT32 nca = cft->param_count > 8 ? 8 : cft->param_count;
                    OoWasmVal cret;
                    _wm_memset(&cret, 0, sizeof(cret));
                    for (INT32 k = (INT32)nca - 1; k >= 0; k--) cargs[k] = _stk_pop(&stk);
                    if (EFI_ERROR(_exec(ws, m, fi, cargs, nca, &cret))) {
                        oo_wasm_leave_call(ws);
                        return EFI_ABORTED;
                    }
                    if (cft->result_count > 0) _stk_push(&stk, cret);
                }
                break;
            }
            case 0x1A:
                _stk_pop(&stk);
                break;
            case 0x1B: {
                OoWasmVal c = _stk_pop(&stk), b = _stk_pop(&stk), a = _stk_pop(&stk);
                _stk_push(&stk, c.i32 ? a : b);
                break;
            }
            case 0x20: {
                UINT32 li = 0; ip += _wm_leb_u32(code + ip, &li);
                _stk_push(&stk, locals[li < OO_WASM_MAX_LOCALS ? li : 0]);
                break;
            }
            case 0x21: {
                UINT32 li = 0; ip += _wm_leb_u32(code + ip, &li);
                if (li < OO_WASM_MAX_LOCALS) locals[li] = _stk_pop(&stk); else _stk_pop(&stk);
                break;
            }
            case 0x22: {
                UINT32 li = 0; ip += _wm_leb_u32(code + ip, &li);
                if (li < OO_WASM_MAX_LOCALS) locals[li] = _stk_peek(&stk);
                break;
            }
            case 0x41: {
                INT32 v = 0; ip += _wm_leb_i32(code + ip, &v); _stk_push(&stk, _mki32(v)); break;
            }
            case 0x42: {
                INT64 v = 0; ip += _wm_leb_i64(code + ip, &v); _stk_push(&stk, _mki64(v)); break;
            }
            case 0x28: {
                UINT32 align = 0, offset = 0, addr = 0, val = 0;
                OoWasmVal base;
                ip += _wm_leb_u32(code + ip, &align);
                ip += _wm_leb_u32(code + ip, &offset);
                base = _stk_pop(&stk);
                addr = (UINT32)base.i32 + offset;
                if (oo_wasm_mem_read_u32(ws, m->mem, addr, ip - 1, &val) != 0) {
                    oo_wasm_leave_call(ws);
                    return EFI_ABORTED;
                }
                _stk_push(&stk, _mki32((INT32)val));
                break;
            }
            case 0x36: {
                UINT32 align = 0, offset = 0, addr = 0;
                OoWasmVal value, base;
                ip += _wm_leb_u32(code + ip, &align);
                ip += _wm_leb_u32(code + ip, &offset);
                value = _stk_pop(&stk);
                base = _stk_pop(&stk);
                addr = (UINT32)base.i32 + offset;
                if (oo_wasm_mem_write_u32(ws, m->mem, addr, ip - 1, value.u32) != 0) {
                    oo_wasm_leave_call(ws);
                    return EFI_ABORTED;
                }
                break;
            }
            case 0x45: { OoWasmVal a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 == 0 ? 1 : 0)); break; }
            case 0x46: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 == b.i32 ? 1 : 0)); break; }
            case 0x47: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 != b.i32 ? 1 : 0)); break; }
            case 0x48: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 <  b.i32 ? 1 : 0)); break; }
            case 0x49: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.u32 <  b.u32 ? 1 : 0)); break; }
            case 0x4A: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 >  b.i32 ? 1 : 0)); break; }
            case 0x4B: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.u32 >  b.u32 ? 1 : 0)); break; }
            case 0x4C: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 <= b.i32 ? 1 : 0)); break; }
            case 0x4E: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 >= b.i32 ? 1 : 0)); break; }
            case 0x6A: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 + b.i32)); break; }
            case 0x6B: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 - b.i32)); break; }
            case 0x6C: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 * b.i32)); break; }
            case 0x6D: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(b.i32 ? a.i32 / b.i32 : 0)); break; }
            case 0x6E: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(b.u32 ? (INT32)(a.u32 / b.u32) : 0)); break; }
            case 0x6F: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(b.i32 ? a.i32 % b.i32 : 0)); break; }
            case 0x70: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(b.u32 ? (INT32)(a.u32 % b.u32) : 0)); break; }
            case 0x71: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 & b.i32)); break; }
            case 0x72: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 | b.i32)); break; }
            case 0x73: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 ^ b.i32)); break; }
            case 0x74: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 << (b.i32 & 31))); break; }
            case 0x75: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32(a.i32 >> (b.i32 & 31))); break; }
            case 0x76: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki32((INT32)(a.u32 >> (b.u32 & 31)))); break; }
            case 0x7C: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki64(a.i64 + b.i64)); break; }
            case 0x7D: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki64(a.i64 - b.i64)); break; }
            case 0x7E: { OoWasmVal b = _stk_pop(&stk), a = _stk_pop(&stk); _stk_push(&stk, _mki64(a.i64 * b.i64)); break; }
            case 0xA7: { OoWasmVal a = _stk_pop(&stk); _stk_push(&stk, _mki32((INT32)a.i64)); break; }
            case 0xAC: { OoWasmVal a = _stk_pop(&stk); _stk_push(&stk, _mki64((INT64)a.i32)); break; }
            case 0xAD: { OoWasmVal a = _stk_pop(&stk); _stk_push(&stk, _mki64((INT64)(UINT32)a.u32)); break; }
            default:
                break;
        }
    }

done:
    if (ret_val && stk.top >= 0) *ret_val = _stk_pop(&stk);
    oo_wasm_leave_call(ws);
    return ws->trapped ? EFI_ABORTED : EFI_SUCCESS;
}

EFI_STATUS oo_wasm_load_buf(OoWasmSandbox *ws, OoWasmMod *m, const UINT8 *buf, UINTN size) {
    UINT32 off = 8;
    if (!ws || !m || !buf) return EFI_INVALID_PARAMETER;

    oo_wasm_unload(m);
    oo_wasm_mod_clear(m);

    if (size < 8) { _wm_err(m, (const CHAR8 *)"too small"); return EFI_INVALID_PARAMETER; }
    if (buf[0] != 0x00 || buf[1] != 'a' || buf[2] != 's' || buf[3] != 'm') {
        _wm_err(m, (const CHAR8 *)"bad magic");
        return EFI_INVALID_PARAMETER;
    }
    if (buf[4] != 0x01 || buf[5] != 0 || buf[6] != 0 || buf[7] != 0) {
        _wm_err(m, (const CHAR8 *)"bad version");
        return EFI_INVALID_PARAMETER;
    }

    if (size > OO_WASM_MAX_CODE_SIZE) {
        _wm_err(m, (const CHAR8 *)"module too large");
        return EFI_BAD_BUFFER_SIZE;
    }

    {
        EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, size, (void **)&m->data);
        if (EFI_ERROR(st)) {
            _wm_err(m, (const CHAR8 *)"alloc fail");
            return st;
        }
        _wm_memcpy(m->data, buf, size);
        m->data_size = size;
    }

    if (oo_wasm_begin(ws, OO_WASM_MAX_MEMORY) != 0) {
        _wm_err(m, (const CHAR8 *)"sandbox begin failed");
        return EFI_ABORTED;
    }

    while (off < (UINT32)size) {
        UINT8 sid = m->data[off++];
        UINT32 sec_size = 0, n;
        UINT32 sec_start;
        n = _wm_leb_u32(m->data + off, &sec_size); off += n;
        sec_start = off;

        switch (sid) {
            case WASM_SEC_TYPE:   _parse_types(m,  m->data + off, sec_size); break;
            case WASM_SEC_FUNC:   _parse_funcs(m,  m->data + off, sec_size); break;
            case WASM_SEC_EXPORT: _parse_exports(m, m->data + off, sec_size); break;
            case WASM_SEC_CODE:   _parse_code(m,   m->data + off, sec_size, off); break;
            case WASM_SEC_DATA:   _parse_data(ws, m, m->data + off, sec_size); break;
            default: break;
        }
        off = sec_start + sec_size;
    }

    m->mem_pages = 1;
    m->loaded = 1;
    return EFI_SUCCESS;
}

EFI_STATUS oo_wasm_load_file(OoWasmSandbox *ws, OoWasmMod *m,
                             EFI_FILE_HANDLE root, const CHAR16 *path) {
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    UINTN file_size = 0;
    UINT8 *buf = NULL;
    UINTN read_size;

    if (!root && g_oo_wasm_root) root = g_oo_wasm_root;
    if (!root || !path) return EFI_INVALID_PARAMETER;

    st = uefi_call_wrapper(root->Open, 5, root, &fh, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) {
        _wm_err(m, (const CHAR8 *)"file not found");
        return st;
    }

    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
    st = uefi_call_wrapper(fh->GetInfo, 4, fh, &GenericFileInfo, &info_size, info);
    if (!EFI_ERROR(st)) file_size = info->FileSize;
    if (info) uefi_call_wrapper(BS->FreePool, 1, info);

    if (file_size == 0 || file_size > OO_WASM_MAX_CODE_SIZE) {
        uefi_call_wrapper(fh->Close, 1, fh);
        _wm_err(m, (const CHAR8 *)"file too large or empty");
        return EFI_BAD_BUFFER_SIZE;
    }

    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, file_size, (void **)&buf);
    read_size = file_size;
    st = uefi_call_wrapper(fh->Read, 3, fh, &read_size, buf);
    uefi_call_wrapper(fh->Close, 1, fh);
    if (EFI_ERROR(st)) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        _wm_err(m, (const CHAR8 *)"read error");
        return st;
    }

    st = oo_wasm_load_buf(ws, m, buf, read_size);
    if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
    return st;
}

EFI_STATUS oo_wasm_call(OoWasmSandbox *ws, OoWasmMod *m,
                        const CHAR8 *name, OoWasmVal *args, UINT32 nargs,
                        OoWasmVal *ret) {
    if (!m || !m->loaded) {
        if (m) _wm_err(m, (const CHAR8 *)"not loaded");
        return EFI_NOT_READY;
    }

    if (oo_wasm_begin(ws, OO_WASM_MAX_MEMORY) != 0) {
        return EFI_ABORTED;
    }

    for (UINT32 i = 0; i < m->export_count; i++) {
        if (m->exports[i].kind == 0 && _wm_strcmp(m->exports[i].name, name) == 0) {
            OoWasmVal rv;
            _wm_memset(&rv, 0, sizeof(rv));
            if (ret) *ret = rv;
            return _exec(ws, m, m->exports[i].index, args, nargs, ret ? ret : &rv);
        }
    }

    _wm_err(m, (const CHAR8 *)"export not found");
    return EFI_NOT_FOUND;
}

EFI_STATUS oo_wasm_call_i32(OoWasmSandbox *ws, OoWasmMod *m,
                            const CHAR8 *name, INT32 *args, UINT32 nargs,
                            INT32 *result) {
    OoWasmVal wargs[8];
    OoWasmVal ret;
    _wm_memset(wargs, 0, sizeof(wargs));
    _wm_memset(&ret, 0, sizeof(ret));
    for (UINT32 i = 0; i < nargs && i < 8; i++) {
        wargs[i].type = OO_WASM_I32;
        wargs[i].i32 = args[i];
    }
    {
        EFI_STATUS st = oo_wasm_call(ws, m, name, wargs, nargs, &ret);
        if (!EFI_ERROR(st) && result) *result = ret.i32;
        return st;
    }
}

void oo_wasm_unload(OoWasmMod *m) {
    if (!m) return;
    if (m->data) {
        uefi_call_wrapper(BS->FreePool, 1, m->data);
        m->data = NULL;
    }
    m->loaded = 0;
}

void oo_wasm_print_info(const OoWasmMod *m, const OoWasmSandbox *ws) {
    if (!m) return;
    Print(L"[wasm] loaded=%d size=%u bytes exports=%u funcs=%u mem_pages=%u\r\n",
          m->loaded, (UINT32)m->data_size, m->export_count, m->func_count, m->mem_pages);
    if (ws) {
        Print(L"[wasm] sandbox=%a trapped=%d budget=%lu used=%lu depth=%lu/%lu\r\n",
              oo_wasm_trap_name(ws->last_error),
              (UINTN)ws->trapped,
              (unsigned long)ws->instruction_budget,
              (unsigned long)ws->instructions_used,
              (unsigned long)ws->current_call_depth,
              (unsigned long)ws->max_call_depth);
    }
    for (UINT32 i = 0; i < m->export_count; i++) {
        if (m->exports[i].kind == 0) {
            Print(L"  export[%u]: %a -> func[%u]\r\n",
                  i, m->exports[i].name, m->exports[i].index);
        }
    }
    if (!m->loaded && m->error[0]) {
        Print(L"[wasm] error=%a\r\n", m->error);
    }
}
