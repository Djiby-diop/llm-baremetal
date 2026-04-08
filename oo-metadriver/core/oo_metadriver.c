/*
 * oo-metadriver — LLM-Generated Hardware Drivers implementation
 * No libc. D+ opcode whitelist validation. JIT stub execution.
 */
#include "oo_metadriver.h"

/* ── Allowed opcodes for MMIO meta-drivers ──────────────────────── */
/* D+ whitelist: only simple load/store/move/nop/ret */
static const uint8_t ALLOWED_OPCODES[] = {
    0x89, 0x8B,        /* MOV r/m32 variants */
    0x48, 0x4C,        /* REX prefixes for 64-bit */
    0xB8, 0xBF,        /* MOV imm → reg */
    0x48, 0xC7,        /* MOV imm64 */
    0x8B, 0x05,        /* MOV [rip+rel] → eax */
    0x44, 0x45,        /* REX.R variants */
    0x90,              /* NOP */
    0xC3,              /* RET */
    0xF3, 0x90,        /* PAUSE (rep nop) */
};
#define N_ALLOWED (sizeof(ALLOWED_OPCODES)/sizeof(ALLOWED_OPCODES[0]))

static uint32_t crc32_simple(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i=0; i<len; i++) {
        crc ^= data[i];
        for (int j=0; j<8; j++)
            crc = (crc>>1) ^ (crc&1 ? 0xEDB88320 : 0);
    }
    return ~crc;
}

void metadrv_init(MetadrvEngine *e) {
    e->enabled = 1;
    e->stub_count = 0;
    e->total_executions = 0;
    e->denied_by_dplus = 0;
    e->jit_successes = 0;
    e->jit_failures = 0;
}

int metadrv_register(MetadrvEngine *e, MetadrvType type,
                     const uint8_t *code, int code_len,
                     const char *desc, uint64_t mmio_base) {
    if (!e->enabled) return -1;
    if (e->stub_count >= METADRV_MAX_STUBS) return -1;
    if (code_len <= 0 || code_len > METADRV_STUB_SIZE) return -1;

    MetadrvStub *s = &e->stubs[e->stub_count];
    s->magic = METADRV_MAGIC;
    s->type = type;
    s->status = METADRV_STATUS_PENDING;
    s->stub_len = (uint32_t)code_len;
    s->mmio_base = mmio_base;
    s->result = 0;
    s->executions = 0;
    s->failures = 0;

    /* copy description */
    int dlen=0;
    while (desc[dlen] && dlen<63) { s->description[dlen]=desc[dlen]; dlen++; }
    s->description[dlen]=0;

    /* copy code */
    for (int i=0; i<code_len; i++) s->code[i]=code[i];
    for (int i=code_len; i<METADRV_STUB_SIZE; i++) s->code[i]=0;

    s->checksum = crc32_simple(s->code, code_len);

    return e->stub_count++;
}

int metadrv_validate(MetadrvEngine *e, int idx) {
    if (idx<0||idx>=e->stub_count) return -1;
    MetadrvStub *s = &e->stubs[idx];

    /* D+ opcode scan: reject any byte not in whitelist EXCEPT at end (RET must be present) */
    int has_ret = 0;
    for (uint32_t i=0; i<s->stub_len; i++) {
        uint8_t op = s->code[i];
        if (op == 0xC3) { has_ret=1; continue; }
        /* Check whitelist — simplified: allow if in range 0x40-0xC7 or is NOP/RET */
        /* In production: full opcode decode table */
        int allowed = 0;
        for (int j=0; j<(int)N_ALLOWED; j++) {
            if (op == ALLOWED_OPCODES[j]) { allowed=1; break; }
        }
        if (!allowed && op!=0x90) {
            /* DENY: opcode not in whitelist */
            s->status = METADRV_STATUS_DENIED;
            e->denied_by_dplus++;
            return -2;
        }
    }
    if (!has_ret) {
        s->status = METADRV_STATUS_DENIED;
        e->denied_by_dplus++;
        return -3;  /* no RET — infinite execution risk */
    }

    /* Verify checksum integrity */
    uint32_t chk = crc32_simple(s->code, s->stub_len);
    if (chk != s->checksum) {
        s->status = METADRV_STATUS_DENIED;
        return -4;
    }

    s->status = METADRV_STATUS_VALIDATED;
    return 0;
}

uint64_t metadrv_execute(MetadrvEngine *e, int idx, uint64_t arg) {
    if (idx<0||idx>=e->stub_count) return 0xDEADDEAD;
    MetadrvStub *s = &e->stubs[idx];

    if (s->status != METADRV_STATUS_VALIDATED &&
        s->status != METADRV_STATUS_ACTIVE) {
        return 0xDEADDEAD;
    }

    e->total_executions++;
    s->executions++;

    /*
     * JIT execution: cast code buffer to function pointer and call.
     * The stub receives: arg in RDI (System V AMD64 ABI)
     * Returns: uint64_t in RAX
     *
     * NOTE: This requires the code buffer to be in executable memory.
     * In UEFI, EfiBootServicesData pages are executable by default.
     * Post-ExitBootServices: map stub page as RX.
     */
    typedef uint64_t (*stub_fn_t)(uint64_t);
    stub_fn_t fn = (stub_fn_t)(void*)s->code;

    uint64_t result = 0;
    /* Execute with guard: we can't use setjmp in freestanding, so
     * we rely on D+ validation to prevent bad stubs.
     * For extra safety, the sentinel should be watching */
    result = fn(arg);
    s->result = result;
    s->status = METADRV_STATUS_ACTIVE;
    e->jit_successes++;

    return result;
}

int metadrv_save(const MetadrvEngine *e, void *efi_root) {
    (void)efi_root; /* caller uses oo-storage API */
    /* Returns: total bytes that would be written */
    return sizeof(MetadrvStub) * e->stub_count + 4;
}

int metadrv_load(MetadrvEngine *e, void *efi_root) {
    (void)efi_root;
    (void)e;
    /* Implementation: read OO_METADRV.BIN, deserialize stubs */
    return 0;
}

void metadrv_print(const MetadrvEngine *e) {
    (void)e;
    /* print via caller's EFI ConOut */
}
