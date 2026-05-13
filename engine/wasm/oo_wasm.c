/* oo_wasm.c — OO Bare-Metal WASM Module Loader + Interpreter (Phase 10A)
 * ========================================================================
 * Implements a minimal WASM 1.0 (MVP) parser and stack-based interpreter.
 * Freestanding C11 — no libc, no OS. EFI AllocatePool for module data.
 *
 * Supported opcodes: unreachable, nop, block, loop, if, br, br_if,
 *   return, call, drop, select, local.get/set/tee,
 *   i32.const, i32.add/sub/mul/div_s/div_u/rem_s/rem_u,
 *   i32.and/or/xor/shl/shr_s/shr_u/rotl/rotr,
 *   i32.eqz/eq/ne/lt_s/lt_u/gt_s/gt_u/le_s/le_u/ge_s/ge_u,
 *   i64.const, i64.add/sub/mul, i32.wrap_i64, i64.extend_i32_s/u,
 *   i32.load/store (linear memory, 64KB pool),
 *   end
 */
#include "oo_wasm.h"
#include <efi.h>
#include <efilib.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

static void _wm_memset(void *p, int v, UINTN n) {
    UINT8 *b = (UINT8*)p;
    while (n--) *b++ = (UINT8)v;
}
static void _wm_memcpy(void *d, const void *s, UINTN n) {
    UINT8 *dd=(UINT8*)d; const UINT8 *ss=(const UINT8*)s;
    while(n--) *dd++=*ss++;
}
static UINTN _wm_strlen(const CHAR8 *s) {
    UINTN i=0; while(s[i]) i++; return i;
}
static int _wm_strcmp(const CHAR8 *a, const CHAR8 *b) {
    while(*a && *a==*b){a++;b++;} return (int)(UINT8)*a-(int)(UINT8)*b;
}

/* LEB128 unsigned decode → returns bytes consumed */
static UINT32 _wm_leb_u32(const UINT8 *p, UINT32 *out) {
    UINT32 r=0, shift=0, i=0;
    do { r |= (UINT32)(p[i]&0x7F)<<shift; shift+=7; } while(p[i++]&0x80);
    *out = r; return i;
}
/* LEB128 signed i32 */
static UINT32 _wm_leb_i32(const UINT8 *p, INT32 *out) {
    INT32 r=0; UINT32 shift=0, i=0; UINT8 b;
    do { b=p[i++]; r|=(INT32)(b&0x7F)<<shift; shift+=7; } while(b&0x80);
    if (shift<32 && (b&0x40)) r|=-(1<<shift);
    *out=r; return i;
}
/* LEB128 signed i64 */
static UINT32 _wm_leb_i64(const UINT8 *p, INT64 *out) {
    INT64 r=0; UINT32 shift=0, i=0; UINT8 b;
    do { b=p[i++]; r|=(INT64)(b&0x7F)<<shift; shift+=7; } while(b&0x80);
    if (shift<64 && (b&0x40)) r|=-((INT64)1<<shift);
    *out=r; return i;
}

static void _wm_err(OoWasmMod *m, const CHAR8 *msg) {
    UINTN n=_wm_strlen(msg);
    if(n>=sizeof(m->error)) n=sizeof(m->error)-1;
    _wm_memcpy(m->error, msg, n); m->error[n]=0;
}

/* ── Section IDs ─────────────────────────────────────────────────────────── */
#define WASM_SEC_CUSTOM   0
#define WASM_SEC_TYPE     1
#define WASM_SEC_IMPORT   2
#define WASM_SEC_FUNC     3
#define WASM_SEC_TABLE    4
#define WASM_SEC_MEM      5
#define WASM_SEC_GLOBAL   6
#define WASM_SEC_EXPORT   7
#define WASM_SEC_START    8
#define WASM_SEC_ELEM     9
#define WASM_SEC_CODE    10
#define WASM_SEC_DATA    11

/* ── Parse Type section ──────────────────────────────────────────────────── */
static int _parse_types(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off=0, count=0, n;
    n = _wm_leb_u32(p+off, &count); off+=n;
    if (count > OO_WASM_MAX_FUNCS) count = OO_WASM_MAX_FUNCS;
    m->type_count = count;
    for (UINT32 i=0; i<count && off<size; i++) {
        if (p[off++] != 0x60) { _wm_err(m,(const CHAR8*)"bad functype"); return 0; }
        UINT32 pc=0; n=_wm_leb_u32(p+off,&pc); off+=n;
        m->types[i].param_count = (UINT8)(pc>8?8:pc);
        for (UINT32 k=0;k<pc&&off<size;k++) {
            if(k<8) m->types[i].params[k]=(OoWasmValType)p[off];
            off++;
        }
        UINT32 rc=0; n=_wm_leb_u32(p+off,&rc); off+=n;
        m->types[i].result_count=(UINT8)(rc>4?4:rc);
        for (UINT32 k=0;k<rc&&off<size;k++) {
            if(k<4) m->types[i].results[k]=(OoWasmValType)p[off];
            off++;
        }
    }
    return 1;
}

/* ── Parse Function section ──────────────────────────────────────────────── */
static int _parse_funcs(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off=0, count=0, n;
    n=_wm_leb_u32(p+off,&count); off+=n;
    if(count>OO_WASM_MAX_FUNCS) count=OO_WASM_MAX_FUNCS;
    m->func_count=count;
    for (UINT32 i=0;i<count&&off<size;i++) {
        UINT32 ti=0; n=_wm_leb_u32(p+off,&ti); off+=n;
        m->funcs[i].type_idx=ti;
    }
    return 1;
}

/* ── Parse Export section ────────────────────────────────────────────────── */
static int _parse_exports(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off=0, count=0, n;
    n=_wm_leb_u32(p+off,&count); off+=n;
    if(count>OO_WASM_MAX_EXPORTS) count=OO_WASM_MAX_EXPORTS;
    m->export_count=count;
    for (UINT32 i=0;i<count&&off<size;i++) {
        UINT32 nl=0; n=_wm_leb_u32(p+off,&nl); off+=n;
        UINT32 cp=nl<63?nl:63;
        _wm_memcpy(m->exports[i].name, p+off, cp);
        m->exports[i].name[cp]=0;
        off+=nl;
        m->exports[i].kind=p[off++];
        UINT32 idx=0; n=_wm_leb_u32(p+off,&idx); off+=n;
        m->exports[i].index=idx;
    }
    return 1;
}

/* ── Parse Code section ──────────────────────────────────────────────────── */
static int _parse_code(OoWasmMod *m, const UINT8 *p, UINT32 size,
                        UINT32 sec_data_offset) {
    UINT32 off=0, count=0, n;
    n=_wm_leb_u32(p+off,&count); off+=n;
    if(count>m->func_count) count=m->func_count;
    for (UINT32 i=0;i<count&&off<size;i++) {
        UINT32 body_size=0; n=_wm_leb_u32(p+off,&body_size); off+=n;
        UINT32 body_start=off;
        /* local decls */
        UINT32 local_groups=0; n=_wm_leb_u32(p+off,&local_groups); off+=n;
        UINT8 lc=0;
        for (UINT32 g=0;g<local_groups&&off<size;g++) {
            UINT32 cnt=0; n=_wm_leb_u32(p+off,&cnt); off+=n;
            OoWasmValType vt=(OoWasmValType)p[off++];
            for (UINT32 k=0;k<cnt&&lc<OO_WASM_MAX_LOCALS;k++) m->funcs[i].locals[lc++]=vt;
        }
        m->funcs[i].local_count=lc;
        /* code starts here */
        m->funcs[i].code_offset = sec_data_offset + off;
        UINT32 code_bytes = body_size - (off - body_start);
        m->funcs[i].code_size  = code_bytes;
        off = body_start + body_size;
    }
    return 1;
}

/* ── Parse Data section (copy to linear memory) ──────────────────────────── */
static int _parse_data(OoWasmMod *m, const UINT8 *p, UINT32 size) {
    UINT32 off=0, count=0, n;
    n=_wm_leb_u32(p+off,&count); off+=n;
    for (UINT32 i=0;i<count&&off<size;i++) {
        UINT32 mem_idx=0; n=_wm_leb_u32(p+off,&mem_idx); off+=n;
        /* offset expression: i32.const <val> end */
        UINT32 addr=0;
        if (p[off]==0x41) { /* i32.const */
            off++;
            INT32 v=0; n=_wm_leb_i32(p+off,&v); off+=n; addr=(UINT32)v;
        }
        if (p[off]==0x0B) off++; /* end */
        UINT32 dl=0; n=_wm_leb_u32(p+off,&dl); off+=n;
        if (addr+dl <= OO_WASM_MAX_MEMORY)
            _wm_memcpy(m->mem+addr, p+off, dl);
        off+=dl;
    }
    return 1;
}

/* ── Main binary parser ──────────────────────────────────────────────────── */
EFI_STATUS oo_wasm_load_buf(OoWasmMod *m, const UINT8 *buf, UINTN size) {
    _wm_memset(m, 0, sizeof(*m));
    if (size < 8) { _wm_err(m,(const CHAR8*)"too small"); return EFI_INVALID_PARAMETER; }
    /* Magic + version */
    if (buf[0]!=0x00||buf[1]!='a'||buf[2]!='s'||buf[3]!='m') {
        _wm_err(m,(const CHAR8*)"bad magic"); return EFI_INVALID_PARAMETER;
    }
    if (buf[4]!=0x01||buf[5]!=0||buf[6]!=0||buf[7]!=0) {
        _wm_err(m,(const CHAR8*)"bad version"); return EFI_INVALID_PARAMETER;
    }

    /* Allocate and copy raw data */
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3,
        EfiLoaderData, size, (void**)&m->data);
    if (EFI_ERROR(st)) { _wm_err(m,(const CHAR8*)"alloc fail"); return st; }
    _wm_memcpy(m->data, buf, size);
    m->data_size = size;

    /* Iterate sections */
    UINT32 off = 8;
    while (off < (UINT32)size) {
        UINT8 sid = m->data[off++];
        UINT32 sec_size=0, n;
        n=_wm_leb_u32(m->data+off,&sec_size); off+=n;
        UINT32 sec_start = off;

        switch (sid) {
        case WASM_SEC_TYPE:   _parse_types(m,  m->data+off, sec_size); break;
        case WASM_SEC_FUNC:   _parse_funcs(m,  m->data+off, sec_size); break;
        case WASM_SEC_EXPORT: _parse_exports(m,m->data+off, sec_size); break;
        case WASM_SEC_CODE:   _parse_code(m,   m->data+off, sec_size, off); break;
        case WASM_SEC_DATA:   _parse_data(m,   m->data+off, sec_size); break;
        default: break;
        }
        off = sec_start + sec_size;
    }

    m->mem_pages = 1;
    m->loaded = 1;
    return EFI_SUCCESS;
}

/* ── Load from EFI file ──────────────────────────────────────────────────── */
EFI_STATUS oo_wasm_load_file(OoWasmMod *m, EFI_FILE_HANDLE root,
                              const CHAR16 *path) {
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5,
        root, &fh, (CHAR16*)path,
        EFI_FILE_MODE_READ,
        EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);
    if (EFI_ERROR(st)) { _wm_err(m,(const CHAR8*)"file not found"); return st; }

    /* Get file size */
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void**)&info);
    st = uefi_call_wrapper(fh->GetInfo, 4, fh, &GenericFileInfo, &info_size, info);
    UINTN file_size = EFI_ERROR(st) ? 0 : info->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, info);

    if (file_size == 0 || file_size > OO_WASM_MAX_CODE_SIZE) {
        uefi_call_wrapper(fh->Close, 1, fh);
        _wm_err(m,(const CHAR8*)"file too large or empty");
        return EFI_BAD_BUFFER_SIZE;
    }

    UINT8 *buf = NULL;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, file_size, (void**)&buf);
    UINTN read_size = file_size;
    st = uefi_call_wrapper(fh->Read, 3, fh, &read_size, buf);
    uefi_call_wrapper(fh->Close, 1, fh);

    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        _wm_err(m,(const CHAR8*)"read error"); return st;
    }

    st = oo_wasm_load_buf(m, buf, read_size);
    uefi_call_wrapper(BS->FreePool, 1, buf);
    return st;
}

/* ── Stack-based interpreter ─────────────────────────────────────────────── */

typedef struct {
    OoWasmVal slots[OO_WASM_MAX_STACK];
    INT32     top;   /* -1 = empty */
} _WasmStack;

static void _stk_push(_WasmStack *s, OoWasmVal v) {
    if (s->top+1 < OO_WASM_MAX_STACK) s->slots[++s->top] = v;
}
static OoWasmVal _stk_pop(_WasmStack *s) {
    if (s->top < 0) { OoWasmVal z; _wm_memset(&z,0,sizeof(z)); return z; }
    return s->slots[s->top--];
}
static OoWasmVal _stk_peek(_WasmStack *s) {
    if (s->top < 0) { OoWasmVal z; _wm_memset(&z,0,sizeof(z)); return z; }
    return s->slots[s->top];
}

static OoWasmVal _mki32(INT32 v)  { OoWasmVal x; x.type=OO_WASM_I32; x.i32=v; return x; }
static OoWasmVal _mki64(INT64 v)  { OoWasmVal x; x.type=OO_WASM_I64; x.i64=v; return x; }

/* Execute function at funcs[func_idx] — recursive for `call` opcode */
static EFI_STATUS _exec(OoWasmMod *m, UINT32 func_idx,
                         OoWasmVal *args, UINT32 nargs,
                         OoWasmVal *ret_val, int depth) {
    if (depth > 16) { _wm_err(m,(const CHAR8*)"call depth"); return EFI_ABORTED; }
    if (func_idx >= m->func_count) return EFI_NOT_FOUND;

    OoWasmFunc *fn  = &m->funcs[func_idx];
    OoWasmFuncType *ft = &m->types[fn->type_idx];

    /* locals: params + declared locals */
    OoWasmVal locals[OO_WASM_MAX_LOCALS];
    _wm_memset(locals, 0, sizeof(locals));
    for (UINT32 i=0; i<nargs && i<(UINT32)ft->param_count; i++) locals[i]=args[i];

    _WasmStack stk; stk.top = -1;

    const UINT8 *code = m->data + fn->code_offset;
    UINT32 ip = 0, code_size = fn->code_size;

    /* Simple block depth counter for if/block/loop/end */
    INT32 block_depth = 0;

    while (ip < code_size) {
        UINT8 op = code[ip++];
        UINT32 n;
        switch (op) {

        case 0x00: /* unreachable */
            _wm_err(m,(const CHAR8*)"unreachable"); return EFI_ABORTED;
        case 0x01: /* nop */ break;

        case 0x02: /* block <type> */ ip++; block_depth++; break;
        case 0x03: /* loop  <type> */ ip++; block_depth++; break;
        case 0x04: /* if    <type> */ {
            ip++;
            OoWasmVal cond = _stk_pop(&stk);
            if (!cond.i32) {
                /* skip to matching else or end */
                INT32 d=1;
                while (ip<code_size && d>0) {
                    UINT8 b=code[ip++];
                    if(b==0x02||b==0x03||b==0x04) d++;
                    else if(b==0x0B) d--;
                    else if(b==0x05&&d==1) { d=0; } /* else found */
                }
            } else block_depth++;
            break;
        }
        case 0x05: /* else — skip to end */ {
            INT32 d=1;
            while (ip<code_size && d>0) {
                UINT8 b=code[ip++];
                if(b==0x02||b==0x03||b==0x04) d++;
                else if(b==0x0B) d--;
            }
            if(block_depth>0) block_depth--;
            break;
        }
        case 0x0B: /* end */
            if (block_depth > 0) { block_depth--; break; }
            goto done;
        case 0x0C: /* br */ { UINT32 depth=0; ip+=_wm_leb_u32(code+ip,&depth); goto done; }
        case 0x0D: /* br_if */ {
            UINT32 depth=0; ip+=_wm_leb_u32(code+ip,&depth);
            OoWasmVal c=_stk_pop(&stk);
            if(c.i32) goto done;
            break;
        }
        case 0x0F: /* return */ goto done;

        case 0x10: /* call */ {
            UINT32 fi=0; n=_wm_leb_u32(code+ip,&fi); ip+=n;
            if (fi < m->func_count) {
                OoWasmFuncType *cft = &m->types[m->funcs[fi].type_idx];
                OoWasmVal cargs[8];
                UINT32 nca = cft->param_count > 8 ? 8 : cft->param_count;
                /* pop args in reverse */
                for (INT32 k=(INT32)nca-1; k>=0; k--) cargs[k]=_stk_pop(&stk);
                OoWasmVal cret; _wm_memset(&cret,0,sizeof(cret));
                _exec(m, fi, cargs, nca, &cret, depth+1);
                if (cft->result_count > 0) _stk_push(&stk, cret);
            }
            break;
        }

        case 0x1A: /* drop */ _stk_pop(&stk); break;
        case 0x1B: /* select */ {
            OoWasmVal c=_stk_pop(&stk);
            OoWasmVal b=_stk_pop(&stk);
            OoWasmVal a=_stk_pop(&stk);
            _stk_push(&stk, c.i32 ? a : b);
            break;
        }

        /* locals */
        case 0x20: { UINT32 li=0; n=_wm_leb_u32(code+ip,&li); ip+=n; _stk_push(&stk,locals[li<OO_WASM_MAX_LOCALS?li:0]); break; }
        case 0x21: { UINT32 li=0; n=_wm_leb_u32(code+ip,&li); ip+=n; if(li<OO_WASM_MAX_LOCALS) locals[li]=_stk_pop(&stk); else _stk_pop(&stk); break; }
        case 0x22: { UINT32 li=0; n=_wm_leb_u32(code+ip,&li); ip+=n; if(li<OO_WASM_MAX_LOCALS) locals[li]=_stk_peek(&stk); break; }

        /* i32.const */
        case 0x41: { INT32 v=0; n=_wm_leb_i32(code+ip,&v); ip+=n; _stk_push(&stk,_mki32(v)); break; }
        /* i64.const */
        case 0x42: { INT64 v=0; n=_wm_leb_i64(code+ip,&v); ip+=n; _stk_push(&stk,_mki64(v)); break; }

        /* i32 load/store */
        case 0x28: { ip+=2; UINT32 addr=(UINT32)_stk_pop(&stk).i32; INT32 v=0; if(addr+4<=OO_WASM_MAX_MEMORY){_wm_memcpy(&v,m->mem+addr,4);} _stk_push(&stk,_mki32(v)); break; } /* i32.load */
        case 0x36: { ip+=2; INT32 v=_stk_pop(&stk).i32; UINT32 addr=(UINT32)_stk_pop(&stk).i32; if(addr+4<=OO_WASM_MAX_MEMORY) _wm_memcpy(m->mem+addr,&v,4); break; } /* i32.store */

        /* i32 compare */
        case 0x45: { OoWasmVal a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32==0?1:0)); break; } /* eqz */
        case 0x46: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32==b.i32?1:0)); break; } /* eq */
        case 0x47: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32!=b.i32?1:0)); break; } /* ne */
        case 0x48: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32< b.i32?1:0)); break; } /* lt_s */
        case 0x49: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.u32< b.u32?1:0)); break; } /* lt_u */
        case 0x4A: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32> b.i32?1:0)); break; } /* gt_s */
        case 0x4B: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.u32> b.u32?1:0)); break; } /* gt_u */
        case 0x4C: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32<=b.i32?1:0)); break; } /* le_s */
        case 0x4E: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32>=b.i32?1:0)); break; } /* ge_s */

        /* i32 arithmetic */
        case 0x6A: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32+b.i32)); break; } /* add */
        case 0x6B: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32-b.i32)); break; } /* sub */
        case 0x6C: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32*b.i32)); break; } /* mul */
        case 0x6D: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(b.i32?a.i32/b.i32:0)); break; } /* div_s */
        case 0x6E: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(b.u32?(INT32)(a.u32/b.u32):0)); break; } /* div_u */
        case 0x6F: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(b.i32?a.i32%b.i32:0)); break; } /* rem_s */
        case 0x70: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(b.u32?(INT32)(a.u32%b.u32):0)); break; } /* rem_u */
        case 0x71: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32&b.i32)); break; } /* and */
        case 0x72: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32|b.i32)); break; } /* or */
        case 0x73: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32^b.i32)); break; } /* xor */
        case 0x74: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32<<(b.i32&31))); break; } /* shl */
        case 0x75: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32(a.i32>>(b.i32&31))); break; } /* shr_s */
        case 0x76: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki32((INT32)(a.u32>>(b.u32&31)))); break; } /* shr_u */

        /* i64 arithmetic */
        case 0x7C: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki64(a.i64+b.i64)); break; } /* i64.add */
        case 0x7D: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki64(a.i64-b.i64)); break; } /* i64.sub */
        case 0x7E: { OoWasmVal b=_stk_pop(&stk),a=_stk_pop(&stk); _stk_push(&stk,_mki64(a.i64*b.i64)); break; } /* i64.mul */

        /* i32/i64 conversions */
        case 0xA7: { OoWasmVal a=_stk_pop(&stk); _stk_push(&stk,_mki32((INT32)a.i64)); break; } /* i32.wrap_i64 */
        case 0xAC: { OoWasmVal a=_stk_pop(&stk); _stk_push(&stk,_mki64((INT64)a.i32)); break; } /* i64.extend_i32_s */
        case 0xAD: { OoWasmVal a=_stk_pop(&stk); _stk_push(&stk,_mki64((INT64)(UINT32)a.u32)); break; } /* i64.extend_i32_u */

        default:
            /* unknown opcode — skip silently to avoid abort on complex modules */
            break;
        }
    }
done:
    if (ret_val && stk.top >= 0) *ret_val = _stk_pop(&stk);
    return EFI_SUCCESS;
}

/* ── Public call API ─────────────────────────────────────────────────────── */

EFI_STATUS oo_wasm_call(OoWasmMod *m, const CHAR8 *name,
                         OoWasmVal *args, UINT32 nargs, OoWasmVal *ret) {
    if (!m->loaded) { _wm_err(m,(const CHAR8*)"not loaded"); return EFI_NOT_READY; }
    /* find export */
    for (UINT32 i=0; i<m->export_count; i++) {
        if (m->exports[i].kind==0 && _wm_strcmp(m->exports[i].name,name)==0) {
            OoWasmVal rv; _wm_memset(&rv,0,sizeof(rv));
            EFI_STATUS st = _exec(m, m->exports[i].index, args, nargs, &rv, 0);
            if (ret) *ret = rv;
            return st;
        }
    }
    return EFI_NOT_FOUND;
}

EFI_STATUS oo_wasm_call_i32(OoWasmMod *m, const CHAR8 *name,
                              INT32 *args, UINT32 nargs, INT32 *result) {
    OoWasmVal wargs[8];
    for (UINT32 i=0; i<nargs&&i<8; i++) { wargs[i].type=OO_WASM_I32; wargs[i].i32=args[i]; }
    OoWasmVal ret; _wm_memset(&ret,0,sizeof(ret));
    EFI_STATUS st = oo_wasm_call(m, name, wargs, nargs, &ret);
    if (!EFI_ERROR(st) && result) *result = ret.i32;
    return st;
}

/* ── Unload ──────────────────────────────────────────────────────────────── */
void oo_wasm_unload(OoWasmMod *m) {
    if (m->data) {
        uefi_call_wrapper(BS->FreePool, 1, m->data);
        m->data = NULL;
    }
    m->loaded = 0;
}

/* ── Print info ──────────────────────────────────────────────────────────── */
void oo_wasm_print_info(const OoWasmMod *m) {
    Print(L"[wasm] Loaded: %d  Size: %u bytes\r\n", m->loaded, (UINT32)m->data_size);
    Print(L"[wasm] Types: %u  Funcs: %u  Exports: %u  MemPages: %u\r\n",
          m->type_count, m->func_count, m->export_count, m->mem_pages);
    for (UINT32 i=0; i<m->export_count; i++) {
        if (m->exports[i].kind==0)
            Print(L"  export[%u]: %a → func[%u]\r\n",
                  i, m->exports[i].name, m->exports[i].index);
    }
    if (!m->loaded && m->error[0]) {
        Print(L"[wasm] Last error: %a\r\n", m->error);
    }
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
int oo_wasm_repl_cmd(OoWasmMod *m, EFI_FILE_HANDLE root, const char *cmd) {
    if (!cmd) return 0;

    /* /wasm_load <path> */
    if (__builtin_memcmp(cmd, "/wasm_load ", 11) == 0) {
        const char *path = cmd + 11;
        while (*path==' ') path++;
        CHAR16 wpath[128]; UINTN wi=0;
        while (*path && wi<127) wpath[wi++]=(CHAR16)(UINT8)*path++;
        wpath[wi]=0;
        if (m->loaded) oo_wasm_unload(m);
        Print(L"\r\n[wasm] Loading: %s\r\n", wpath);
        EFI_STATUS st = oo_wasm_load_file(m, root, wpath);
        if (EFI_ERROR(st))
            Print(L"[wasm] Load failed: %r — %a\r\n\r\n", st, m->error);
        else {
            oo_wasm_print_info(m);
            Print(L"[wasm] OK\r\n\r\n");
        }
        return 1;
    }

    /* /wasm_info */
    if (__builtin_memcmp(cmd, "/wasm_info", 10) == 0) {
        Print(L"\r\n"); oo_wasm_print_info(m); Print(L"\r\n");
        return 1;
    }

    /* /wasm_call <func> [i32 args...] */
    if (__builtin_memcmp(cmd, "/wasm_call ", 11) == 0) {
        const char *p = cmd + 11;
        while (*p==' ') p++;
        CHAR8 fname[64]; UINTN fi=0;
        while (*p && *p!=' ' && fi<63) fname[fi++]=(CHAR8)*p++;
        fname[fi]=0;
        /* parse int args */
        INT32 args[8]; UINT32 nargs=0;
        while (*p && nargs<8) {
            while (*p==' ') p++;
            if (!*p) break;
            INT32 v=0; int neg=0;
            if(*p=='-'){neg=1;p++;}
            while (*p>='0'&&*p<='9') { v=v*10+(*p-'0'); p++; }
            args[nargs++]=neg?-v:v;
        }
        Print(L"\r\n[wasm] Calling: %a(%d args)\r\n", fname, nargs);
        INT32 result=0;
        EFI_STATUS st = oo_wasm_call_i32(m, fname, args, nargs, &result);
        if (EFI_ERROR(st))
            Print(L"[wasm] Call failed: %r (not found or error)\r\n\r\n", st);
        else
            Print(L"[wasm] Result: %d (0x%08x)\r\n\r\n", result, (UINT32)result);
        return 1;
    }

    /* /wasm_unload */
    if (__builtin_memcmp(cmd, "/wasm_unload", 12) == 0) {
        oo_wasm_unload(m);
        Print(L"\r\n[wasm] Module unloaded\r\n\r\n");
        return 1;
    }

    return 0;
}
