/*
 * oo_boot_bridge.c — OO Boot Bridge implementation
 *
 * Reads EFI/OO/OOBOOT.CFG at kernel startup, populates OoBridgeConfig,
 * and makes it available to the LLM inference engine.
 *
 * Designed to compile on:
 *   - bare UEFI (no libc, uses EFI file protocol)
 *   - POSIX/Windows host (uses fopen/fread for development)
 *
 * When UEFI_BUILD is defined, EFI file I/O is used.
 * Otherwise, standard C file I/O is used (for dev/test).
 */

#include "oo_boot_bridge.h"

#include <string.h>

#ifndef UEFI_BUILD
#include <stdio.h>
#include <stdlib.h>
#endif

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static int oo_str_startswith(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int oo_strlen(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static void oo_strncpy_safe(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int oo_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Parse unsigned long from decimal string */
static unsigned long oo_parse_ulong(const char *s) {
    unsigned long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/* Parse float as integer millis (no libc required): "0.700" → 700 */
static float oo_parse_float(const char *s) {
    long integer = 0;
    long frac    = 0;
    long frac_div = 1;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        integer = integer * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac = frac * 10 + (*s - '0');
            frac_div *= 10;
            s++;
        }
    }
    float result = (float)integer + (float)frac / (float)frac_div;
    return neg ? -result : result;
}

static OoRuntimeMode oo_parse_mode(const char *s) {
    if (oo_strcmp(s, "safe")         == 0) return OO_MODE_SAFE;
    if (oo_strcmp(s, "degraded")     == 0) return OO_MODE_DEGRADED;
    if (oo_strcmp(s, "experimental") == 0) return OO_MODE_EXPERIMENTAL;
    return OO_MODE_NORMAL;
}

static const char *oo_mode_str(OoRuntimeMode m) {
    switch (m) {
        case OO_MODE_SAFE:         return "safe";
        case OO_MODE_DEGRADED:     return "degraded";
        case OO_MODE_EXPERIMENTAL: return "experimental";
        default:                   return "normal";
    }
}

/* Trim trailing whitespace/CR/LF in place */
static void oo_trim_right(char *s) {
    int n = oo_strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

/* ── Parse a single key=value line ───────────────────────────────────────── */

static int oo_parse_line(
    OoBridgeConfig *cfg,
    const char *line
) {
    /* Skip comments and blank lines */
    if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') return 0;

    /* Find '=' */
    const char *eq = line;
    while (*eq && *eq != '=') eq++;
    if (*eq != '=') return 0;

    int klen = (int)(eq - line);
    if (klen <= 0 || klen >= OO_BRIDGE_KEY_LEN) return 0;

    const char *val = eq + 1;
    if (cfg->pair_count >= OO_BRIDGE_MAX_PAIRS) return 0;

    int idx = cfg->pair_count;
    /* Copy key */
    int i;
    for (i = 0; i < klen && i < OO_BRIDGE_KEY_LEN - 1; i++)
        cfg->keys[idx][i] = line[i];
    cfg->keys[idx][i] = '\0';

    /* Copy value */
    oo_strncpy_safe(cfg->values[idx], val, OO_BRIDGE_VAL_LEN);
    oo_trim_right(cfg->values[idx]);

    cfg->pair_count++;
    return 1;
}

/* ── Decode well-known fields ─────────────────────────────────────────────── */

static void oo_decode_fields(OoBridgeConfig *cfg) {
    for (int i = 0; i < cfg->pair_count; i++) {
        const char *k = cfg->keys[i];
        const char *v = cfg->values[i];

        if (oo_strcmp(k, "oo_organism_id")   == 0)
            oo_strncpy_safe(cfg->organism_id, v, OO_BRIDGE_KEY_LEN);
        else if (oo_strcmp(k, "oo_continuity_epoch") == 0)
            cfg->continuity_epoch = oo_parse_ulong(v);
        else if (oo_strcmp(k, "oo_boot_count") == 0)
            cfg->boot_count = oo_parse_ulong(v);
        else if (oo_strcmp(k, "oo_mode") == 0)
            cfg->mode = oo_parse_mode(v);
        else if (oo_strcmp(k, "oo_goals_pending") == 0)
            cfg->goals_pending = (int)oo_parse_ulong(v);
        else if (oo_strcmp(k, "oo_goals_doing") == 0)
            cfg->goals_doing = (int)oo_parse_ulong(v);
        else if (oo_strcmp(k, "oo_ts") == 0)
            cfg->ts = oo_parse_ulong(v);
        else if (oo_strcmp(k, "oo_journal_path") == 0)
            oo_strncpy_safe(cfg->journal_path, v, OO_BRIDGE_VAL_LEN);
        else if (oo_strcmp(k, "oo_kv_path") == 0)
            oo_strncpy_safe(cfg->kv_path, v, OO_BRIDGE_VAL_LEN);
        else if (oo_strcmp(k, "llm.max_tokens") == 0)
            cfg->max_tokens = (int)oo_parse_ulong(v);
        else if (oo_strcmp(k, "llm.temperature") == 0)
            cfg->temperature = oo_parse_float(v);
        else if (oo_strcmp(k, "llm.top_k") == 0)
            cfg->top_k = (int)oo_parse_ulong(v);
    }
}

/* ── Load OOBOOT.CFG ──────────────────────────────────────────────────────── */

/*
 * Portable (non-UEFI) implementation using fopen/fread.
 * On UEFI, replace with EFI_FILE_PROTOCOL read.
 */
#ifndef UEFI_BUILD

int oo_bridge_load(OoBridgeConfig *cfg, const char *root_path) {
    /* Zero the struct */
    memset(cfg, 0, sizeof(OoBridgeConfig));

    /* Build full path: root_path + "/" + OO_BRIDGE_CFG_NAME */
    char full_path[512];
    int rlen = oo_strlen(root_path);
    oo_strncpy_safe(full_path, root_path, 450);
    if (rlen > 0 && root_path[rlen-1] != '/' && root_path[rlen-1] != '\\')
        full_path[rlen++] = '/';
    oo_strncpy_safe(full_path + rlen, OO_BRIDGE_CFG_NAME, 60);

    FILE *f = fopen(full_path, "r");
    if (!f) {
        oo_strncpy_safe(cfg->load_error, "OOBOOT.CFG not found", 127);
        cfg->loaded = 0;
        return 0;
    }

    char line[OO_BRIDGE_VAL_LEN + OO_BRIDGE_KEY_LEN + 4];
    while (fgets(line, sizeof(line), f)) {
        oo_parse_line(cfg, line);
    }
    fclose(f);

    oo_decode_fields(cfg);
    cfg->loaded = 1;
    return 1;
}

#else  /* UEFI_BUILD */

/*
 * UEFI stub — requires EFI_FILE_PROTOCOL.
 * Caller must provide a loaded EFI file handle for the volume root.
 * Replace `oo_bridge_load_efi(cfg, RootDir)` call in your UEFI main.
 */
int oo_bridge_load(OoBridgeConfig *cfg, const char *root_path) {
    /* UEFI implementation: use EFI SimpleFileSystem to read OOBOOT.CFG */
    /* This is a placeholder — implement with EFI_FILE_PROTOCOL.Read() */
    memset(cfg, 0, sizeof(OoBridgeConfig));
    oo_strncpy_safe(cfg->load_error,
        "UEFI file I/O not yet implemented — use oo_bridge_load_efi()", 127);
    cfg->loaded = 0;
    return 0;
}

#endif /* UEFI_BUILD */

/* ── Public API ──────────────────────────────────────────────────────────── */

const char *oo_bridge_get(const OoBridgeConfig *cfg, const char *key) {
    for (int i = 0; i < cfg->pair_count; i++) {
        if (oo_strcmp(cfg->keys[i], key) == 0)
            return cfg->values[i];
    }
    return NULL;
}

void oo_bridge_apply_llm_overrides(
    const OoBridgeConfig *cfg,
    int   *max_tokens,
    float *temperature,
    int   *top_k
) {
    if (!cfg->loaded) return;
    if (cfg->max_tokens  > 0   && max_tokens)  *max_tokens  = cfg->max_tokens;
    if (cfg->temperature > 0.0f && temperature) *temperature = cfg->temperature;
    if (cfg->top_k       > 0   && top_k)       *top_k       = cfg->top_k;
}

void oo_bridge_print(const OoBridgeConfig *cfg) {
#ifndef UEFI_BUILD
    if (!cfg->loaded) {
        printf("[oo_bridge] NOT LOADED: %s\n", cfg->load_error);
        return;
    }
    printf("[oo_bridge] organism_id=%s epoch=%lu boot#%lu mode=%s\n",
        cfg->organism_id, cfg->continuity_epoch, cfg->boot_count,
        oo_mode_str(cfg->mode));
    printf("[oo_bridge] goals pending=%d doing=%d ts=%lu\n",
        cfg->goals_pending, cfg->goals_doing, cfg->ts);
    if (cfg->max_tokens  > 0)   printf("[oo_bridge] override max_tokens=%d\n",  cfg->max_tokens);
    if (cfg->temperature > 0.0f) printf("[oo_bridge] override temperature=%.3f\n", cfg->temperature);
    if (cfg->top_k       > 0)   printf("[oo_bridge] override top_k=%d\n",        cfg->top_k);
    printf("[oo_bridge] journal_path=%s\n", cfg->journal_path);
    printf("[oo_bridge] kv_path=%s\n",      cfg->kv_path);
#endif
}

int oo_bridge_journal_boot(const OoBridgeConfig *cfg) {
    if (!cfg->loaded || cfg->journal_path[0] == '\0') return 0;
#ifndef UEFI_BUILD
    FILE *f = fopen(cfg->journal_path, "a");
    if (!f) return 0;
    /* Write a minimal JSON event line — no external JSON lib required */
    fprintf(f,
        "{\"kind\":\"kernel_boot\",\"organism_id\":\"%s\","
        "\"mode\":\"%s\",\"continuity_epoch\":%lu,"
        "\"boot_count\":%lu,\"ts\":%lu}\n",
        cfg->organism_id,
        oo_mode_str(cfg->mode),
        cfg->continuity_epoch,
        cfg->boot_count,
        cfg->ts);
    fclose(f);
    return 1;
#else
    return 0; /* UEFI file I/O not yet implemented */
#endif
}

/* ── KV.BIN reader (inline, no dependencies) ─────────────────────────────── */

/* Match struct layout with memory_kv.rs:
 *   KEY_LEN=64, VAL_LEN=256, FLAGS_LEN=8 → RECORD_LEN=328
 *   flags[0..3] = version (u32 LE), flags[4] = status (0=empty,1=live,2=deleted)
 */
#define OO_KV_KEY_LEN    64
#define OO_KV_VAL_LEN   256
#define OO_KV_FLAGS_LEN   8
#define OO_KV_RECORD_LEN (OO_KV_KEY_LEN + OO_KV_VAL_LEN + OO_KV_FLAGS_LEN)
#define OO_KV_STATUS_LIVE 1

typedef struct {
    char     key  [OO_KV_KEY_LEN];
    char     value[OO_KV_VAL_LEN];
    unsigned char flags[OO_KV_FLAGS_LEN];
} OoKvRecord;

/*
 * Read a single key from KV.BIN.
 * Returns pointer to static buffer, or NULL if not found.
 * WARNING: not thread-safe (static buffer).
 */
#ifndef UEFI_BUILD
const char *oo_kv_get(const char *kv_path, const char *key) {
    static char result[OO_KV_VAL_LEN];
    FILE *f = fopen(kv_path, "rb");
    if (!f) return NULL;

    OoKvRecord rec;
    while (fread(&rec, OO_KV_RECORD_LEN, 1, f) == 1) {
        if (rec.flags[4] != OO_KV_STATUS_LIVE) continue;
        /* Key is null-padded to 64 bytes */
        if (oo_strcmp(rec.key, key) == 0) {
            oo_strncpy_safe(result, rec.value, OO_KV_VAL_LEN);
            fclose(f);
            return result;
        }
    }
    fclose(f);
    return NULL;
}
#endif

/* ── Test harness (compile with -DTEST_OO_BRIDGE) ─────────────────────────── */

#ifdef TEST_OO_BRIDGE
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Write a test OOBOOT.CFG */
    FILE *f = fopen("/tmp/OOBOOT.CFG", "w");
    assert(f);
    fprintf(f,
        "# test\n"
        "oo_organism_id=test-organism\n"
        "oo_continuity_epoch=3\n"
        "oo_boot_count=7\n"
        "oo_mode=safe\n"
        "oo_goals_pending=2\n"
        "oo_goals_doing=1\n"
        "oo_ts=1700000000\n"
        "oo_journal_path=/tmp/JOURNAL.JSONL\n"
        "oo_kv_path=/tmp/KV.BIN\n"
        "llm.max_tokens=200\n"
        "llm.temperature=0.800\n"
        "llm.top_k=40\n");
    fclose(f);

    /* Also create the directory structure expected */
    system("mkdir -p /tmp/EFI/OO && cp /tmp/OOBOOT.CFG /tmp/EFI/OO/");

    OoBridgeConfig cfg;
    int ok = oo_bridge_load(&cfg, "/tmp");
    assert(ok == 1);
    assert(cfg.loaded == 1);
    assert(oo_strcmp(cfg.organism_id, "test-organism") == 0);
    assert(cfg.continuity_epoch == 3);
    assert(cfg.boot_count == 7);
    assert(cfg.mode == OO_MODE_SAFE);
    assert(cfg.goals_pending == 2);
    assert(cfg.goals_doing == 1);
    assert(cfg.max_tokens == 200);
    assert(cfg.top_k == 40);
    assert(cfg.temperature > 0.79f && cfg.temperature < 0.81f);

    oo_bridge_print(&cfg);

    /* Test overrides */
    int mt = 100; float temp = 0.5f; int tk = 10;
    oo_bridge_apply_llm_overrides(&cfg, &mt, &temp, &tk);
    assert(mt == 200);
    assert(tk == 40);
    assert(temp > 0.79f && temp < 0.81f);

    /* Test get */
    assert(oo_strcmp(oo_bridge_get(&cfg, "oo_mode"), "safe") == 0);
    assert(oo_bridge_get(&cfg, "nonexistent") == NULL);

    /* Test journal */
    oo_bridge_journal_boot(&cfg);

    printf("ALL TESTS PASSED\n");
    return 0;
}
#endif /* TEST_OO_BRIDGE */
