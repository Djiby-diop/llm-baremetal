// oo_shell.h — OO UEFI Shell: bare-metal interactive shell (Phase G)
//
// A Unix-inspired interactive shell layer over the OO REPL and NeuralFS v2.
// Activated via /shell command. Type "exit" to return to OO REPL.
//
// Built-in commands:
//   ls              - list all NeuralFS2 keys
//   cat <key>       - print value of key
//   write <key> <v> - write/update key with value
//   append <k> <v>  - append to existing key
//   rm <key>        - delete key
//   mem             - show arena free memory
//   stat            - show OO self-model status
//   entropy         - show quantum RNG stats
//   echo <text>     - print text
//   clear           - clear terminal (print many newlines)
//   pwd             - print current virtual path
//   cd <path>       - change virtual namespace prefix
//   history         - show command history
//   help            - show all commands
//   exit / quit     - return to OO REPL
//
// History: 16 entries, circular ring.
// Virtual CWD: prefix for NFS2 keys (e.g., "cd /sys" makes "mem" → "/sys/mem")
//
// Freestanding C11, no libc, no malloc. Header-only (inline/static).

#pragma once

#include "oo_neuralfs2.h"
#include "oo_self_model.h"
#include "oo_quantum_rng.h"

// ============================================================
// Constants
// ============================================================

#define OO_SHELL_HIST_SIZE   16
#define OO_SHELL_LINE_MAX    128
#define OO_SHELL_CWD_MAX     64

// ============================================================
// State
// ============================================================

typedef struct {
    char history[OO_SHELL_HIST_SIZE][OO_SHELL_LINE_MAX];
    int  hist_head;      // next write position
    int  hist_count;     // total entries (capped at OO_SHELL_HIST_SIZE)
    char cwd[OO_SHELL_CWD_MAX];  // current virtual directory
    int  active;         // 1 if shell mode is running
    unsigned int commands_run;
} OoShell;

static OoShell g_oo_shell = {{{0}}, 0, 0, "/", 0, 0};

// ============================================================
// String helpers
// ============================================================

static inline int _sh_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static inline int _sh_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline int _sh_startswith(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

static inline const char *_sh_skip_word(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    return s;
}

static inline void _sh_strncpy(char *dst, const char *src, int cap) {
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Build a NFS2 key with CWD prefix: cwd + "/" + name
// e.g., cwd="/sys" name="mem" → "/sys/mem"
static inline void _sh_full_key(const OoShell *sh, const char *name,
                                 char *out, int cap) {
    int ci = 0;
    const char *c = sh->cwd;
    // Copy cwd (skip trailing slash except root)
    int clen = _sh_strlen(c);
    if (clen > 1 && c[clen-1] == '/') clen--;
    for (int i = 0; i < clen && ci < cap-2; i++) out[ci++] = c[i];
    if (name[0] != '/') out[ci++] = '/';
    for (int i = 0; name[i] && ci < cap-1; i++) out[ci++] = name[i];
    out[ci] = '\0';
}

// ============================================================
// History
// ============================================================

static inline void _sh_hist_push(OoShell *sh, const char *line) {
    _sh_strncpy(sh->history[sh->hist_head], line, OO_SHELL_LINE_MAX);
    sh->hist_head = (sh->hist_head + 1) % OO_SHELL_HIST_SIZE;
    if (sh->hist_count < OO_SHELL_HIST_SIZE) sh->hist_count++;
}

static inline void _sh_hist_print(const OoShell *sh) {
    Print(L"\r\n[shell] history (%d entries)\r\n", sh->hist_count);
    int start = (sh->hist_count < OO_SHELL_HIST_SIZE)
                ? 0
                : sh->hist_head;
    for (int i = 0; i < sh->hist_count; i++) {
        int idx = (start + i) % OO_SHELL_HIST_SIZE;
        Print(L"  %2d  ", sh->hist_count - sh->hist_count + i + 1);
        for (int k = 0; sh->history[idx][k]; k++)
            Print(L"%c", (CHAR16)(unsigned char)sh->history[idx][k]);
        Print(L"\r\n");
    }
    Print(L"\r\n");
}

// ============================================================
// Command implementations
// ============================================================

static inline void _sh_cmd_ls(const OoShell *sh, const Nfs2Store *nfs) {
    Print(L"\r\n[shell:ls] NeuralFS2 records in '%s'\r\n",
          sh->cwd[1] == '\0' ? L"/" : L"?");
    int found = 0;
    for (int i = 0; i < NFS2_MAX_RECORDS; i++) {
        if (!(nfs->records[i].flags & NFS2_FLAG_USED)) continue;
        // Filter: only show keys that start with cwd (or all if cwd="/")
        const char *rname = nfs->records[i].name;
        int clen = _sh_strlen(sh->cwd);
        if (sh->cwd[0] == '/' && sh->cwd[1] == '\0') {
            // root: show all
        } else {
            if (!_sh_startswith(rname, sh->cwd)) continue;
        }
        Print(L"  ");
        for (int k = 0; rname[k]; k++)
            Print(L"%c", (CHAR16)(unsigned char)rname[k]);
        Print(L"  (%uB, writes=%u)\r\n",
              nfs->records[i].data_len, nfs->records[i].write_count);
        found++;
    }
    if (!found) Print(L"  (empty)\r\n");
    Print(L"  total: %u/%u records, %u writes\r\n\r\n",
          nfs->record_count, NFS2_MAX_RECORDS, nfs->total_writes);
}

static inline void _sh_cmd_cat(const OoShell *sh, Nfs2Store *nfs, const char *arg) {
    char key[NFS2_NAME_MAX];
    _sh_full_key(sh, arg, key, NFS2_NAME_MAX);
    const char *val = nfs2_read(nfs, key);
    if (!val) {
        // Try bare name too
        val = nfs2_read(nfs, arg);
        if (!val) { Print(L"  (not found: "); for (int k=0;key[k];k++) Print(L"%c",(CHAR16)(unsigned char)key[k]); Print(L")\r\n"); return; }
    }
    Print(L"\r\n");
    for (int k = 0; val[k]; k++) {
        if (val[k] == '\n') Print(L"\r\n");
        else Print(L"%c", (CHAR16)(unsigned char)val[k]);
    }
    Print(L"\r\n\r\n");
}

static inline void _sh_cmd_write(OoShell *sh, Nfs2Store *nfs,
                                  const char *key_arg, const char *val) {
    char key[NFS2_NAME_MAX];
    _sh_full_key(sh, key_arg, key, NFS2_NAME_MAX);
    int rc = nfs2_write(nfs, key, val);
    if (rc == 0) { Print(L"  OK: "); for(int k=0;key[k];k++) Print(L"%c",(CHAR16)(unsigned char)key[k]); Print(L"\r\n"); }
    else Print(L"  ERR: %d\r\n", rc);
}

static inline void _sh_cmd_rm(OoShell *sh, Nfs2Store *nfs, const char *arg) {
    char key[NFS2_NAME_MAX];
    _sh_full_key(sh, arg, key, NFS2_NAME_MAX);
    int rc = nfs2_delete(nfs, key);
    if (rc == 0) { Print(L"  deleted: "); for(int k=0;key[k];k++) Print(L"%c",(CHAR16)(unsigned char)key[k]); Print(L"\r\n"); }
    else Print(L"  ERR: %d (not found or readonly)\r\n", rc);
}

static inline void _sh_cmd_mem(const LlmkZones *zones) {
    Print(L"\r\n[shell:mem] Arena free memory\r\n");
    if (!zones) { Print(L"  (zones not available)\r\n\r\n"); return; }
    Print(L"  weights  : %u MB free\r\n",
          (unsigned)(_osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_WEIGHTS))));
    Print(L"  kv_cache : %u MB free\r\n",
          (unsigned)(_osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_KV_CACHE))));
    Print(L"  scratch  : %u MB free\r\n",
          (unsigned)(_osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_SCRATCH))));
    Print(L"  zone_c   : %u MB free\r\n",
          (unsigned)(_osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_ZONE_C))));
    Print(L"\r\n");
}

static inline void _sh_cmd_help(void) {
    Print(L"\r\n[OO Shell] commands:\r\n");
    Print(L"  ls                - list keys (current dir)\r\n");
    Print(L"  cat <key>         - print value\r\n");
    Print(L"  write <key> <val> - write/update key\r\n");
    Print(L"  append <key> <v>  - append to key\r\n");
    Print(L"  rm <key>          - delete key\r\n");
    Print(L"  mem               - arena memory usage\r\n");
    Print(L"  stat              - OO self-model status\r\n");
    Print(L"  entropy           - quantum RNG diagnostics\r\n");
    Print(L"  echo <text>       - print text\r\n");
    Print(L"  clear             - clear screen\r\n");
    Print(L"  pwd               - current virtual directory\r\n");
    Print(L"  cd <path>         - change virtual directory\r\n");
    Print(L"  history           - command history\r\n");
    Print(L"  help              - this help\r\n");
    Print(L"  exit / quit       - return to OO REPL\r\n");
    Print(L"\r\n");
}

// ============================================================
// oo_shell_exec — execute one shell command line
//
// Returns 0 to continue, 1 to exit shell mode.
// ============================================================

static inline int oo_shell_exec(
    OoShell        *sh,
    const char     *line,
    Nfs2Store      *nfs,
    OoSelfModel    *self_model,
    const LlmkZones *zones)
{
    if (!sh || !line) return 0;

    // skip leading spaces
    while (*line == ' ') line++;
    if (!line[0]) return 0;

    _sh_hist_push(sh, line);
    sh->commands_run++;

    // exit / quit
    if (_sh_strcmp(line, "exit") == 0 || _sh_strcmp(line, "quit") == 0) {
        Print(L"\r\n[shell] returning to OO REPL\r\n\r\n");
        sh->active = 0;
        return 1;
    }

    // help
    if (_sh_strcmp(line, "help") == 0 || _sh_strcmp(line, "?") == 0) {
        _sh_cmd_help();
        return 0;
    }

    // history
    if (_sh_strcmp(line, "history") == 0) {
        _sh_hist_print(sh);
        return 0;
    }

    // clear
    if (_sh_strcmp(line, "clear") == 0) {
        for (int i = 0; i < 40; i++) Print(L"\r\n");
        return 0;
    }

    // pwd
    if (_sh_strcmp(line, "pwd") == 0) {
        Print(L"\r\n  ");
        for (int k = 0; sh->cwd[k]; k++)
            Print(L"%c", (CHAR16)(unsigned char)sh->cwd[k]);
        Print(L"\r\n\r\n");
        return 0;
    }

    // cd <path>
    if (_sh_startswith(line, "cd ") || _sh_strcmp(line, "cd") == 0) {
        const char *arg = line + 3;
        while (*arg == ' ') arg++;
        if (!*arg || _sh_strcmp(arg, "/") == 0) {
            sh->cwd[0] = '/'; sh->cwd[1] = '\0';
        } else if (arg[0] == '/') {
            _sh_strncpy(sh->cwd, arg, OO_SHELL_CWD_MAX);
        } else {
            // relative: append to cwd
            int cur = _sh_strlen(sh->cwd);
            if (sh->cwd[cur-1] != '/' && cur < OO_SHELL_CWD_MAX-2) sh->cwd[cur++] = '/';
            _sh_strncpy(sh->cwd + cur, arg, OO_SHELL_CWD_MAX - cur);
        }
        Print(L"  cwd: ");
        for (int k = 0; sh->cwd[k]; k++)
            Print(L"%c", (CHAR16)(unsigned char)sh->cwd[k]);
        Print(L"\r\n");
        return 0;
    }

    // ls
    if (_sh_strcmp(line, "ls") == 0) {
        _sh_cmd_ls(sh, nfs);
        return 0;
    }

    // mem
    if (_sh_strcmp(line, "mem") == 0) {
        _sh_cmd_mem(zones);
        return 0;
    }

    // stat
    if (_sh_strcmp(line, "stat") == 0) {
        if (self_model) {
            oo_self_model_to_prefix(self_model);
            oo_self_model_print(self_model);
        } else {
            Print(L"  (self_model not available)\r\n");
        }
        return 0;
    }

    // entropy
    if (_sh_strcmp(line, "entropy") == 0) {
        Print(L"\r\n[shell:entropy] RDRAND=%s ok=%u fail=%u mix=%u seeds=%u last=0x%08X\r\n\r\n",
              g_quantum_rng.rdrand_available == 1 ? L"hw" : L"sw",
              g_quantum_rng.rdrand_ok, g_quantum_rng.rdrand_fail,
              g_quantum_rng.rdtsc_mix_count, g_quantum_rng.quantum_seeds,
              g_quantum_rng.seed_last);
        return 0;
    }

    // echo <text>
    if (_sh_startswith(line, "echo ")) {
        const char *txt = line + 5;
        for (int k = 0; txt[k]; k++) Print(L"%c", (CHAR16)(unsigned char)txt[k]);
        Print(L"\r\n");
        return 0;
    }

    // cat <key>
    if (_sh_startswith(line, "cat ")) {
        _sh_cmd_cat(sh, nfs, line + 4);
        return 0;
    }

    // rm <key>
    if (_sh_startswith(line, "rm ")) {
        _sh_cmd_rm(sh, nfs, line + 3);
        return 0;
    }

    // write <key> <val>
    if (_sh_startswith(line, "write ")) {
        const char *rest = line + 6;
        while (*rest == ' ') rest++;
        const char *val = _sh_skip_word(rest);
        if (!*val) { Print(L"  usage: write <key> <value>\r\n"); return 0; }
        // extract key
        char key[NFS2_NAME_MAX];
        int ki = 0;
        while (rest[ki] && rest[ki] != ' ' && ki < NFS2_NAME_MAX-1)
            key[ki] = rest[ki++];
        key[ki] = '\0';
        _sh_cmd_write(sh, nfs, key, val);
        return 0;
    }

    // append <key> <val>
    if (_sh_startswith(line, "append ")) {
        const char *rest = line + 7;
        while (*rest == ' ') rest++;
        const char *val = _sh_skip_word(rest);
        if (!*val) { Print(L"  usage: append <key> <value>\r\n"); return 0; }
        char key[NFS2_NAME_MAX]; int ki = 0;
        while (rest[ki] && rest[ki] != ' ' && ki < NFS2_NAME_MAX-1)
            key[ki] = rest[ki++];
        key[ki] = '\0';
        char fkey[NFS2_NAME_MAX];
        _sh_full_key(sh, key, fkey, NFS2_NAME_MAX);
        int rc = nfs2_append(nfs, fkey, val);
        Print(L"  append %s: %d\r\n", rc == 0 ? L"OK" : L"ERR", rc);
        return 0;
    }

    // Unknown
    Print(L"  unknown command: ");
    for (int k = 0; line[k] && k < 32; k++)
        Print(L"%c", (CHAR16)(unsigned char)line[k]);
    Print(L"  (type 'help')\r\n");
    return 0;
}

// ============================================================
// oo_shell_prompt — print prompt line
// ============================================================

static inline void oo_shell_prompt(const OoShell *sh) {
    Print(L"oo:");
    for (int k = 0; sh->cwd[k]; k++)
        Print(L"%c", (CHAR16)(unsigned char)sh->cwd[k]);
    Print(L"$ ");
}
