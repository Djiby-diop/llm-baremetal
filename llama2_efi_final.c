// REPL V3 - Full Interactive Chat Loop
// Type "quit" or "exit" to stop

#include <efi.h>
#include <efilib.h>
#include <efinet.h>
#include <stdint.h>

// GOP types + GUID are provided by gnu-efi headers (efiprot.h / efilib.h).

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#include <immintrin.h>
#endif

// djiblas optimized matmul
#define DJIBLAS_DISABLE_CPUID 0
#include "djiblas.h"

// LLM-Kernel primitives (zones + sentinel + post-mortem log)
#include "llmk_zones.h"
#include "llmk_log.h"
#include "llmk_sentinel.h"

// LLM-OO runtime (organism-oriented entities)
#include "llmk_oo.h"

// Djibion meta-engine (laws + triangulation + intent gating)
#include "djibion-engine/core/djibion.h"
#include "diopion-engine/core/diopion.h"
#include "diagnostion-engine/core/diagnostion.h"
#include "memorion-engine/core/memorion.h"
#include "orchestrion-engine/core/orchestrion.h"
#include "calibrion-engine/core/calibrion.h"
#include "compatibilion-engine/core/compatibilion.h"

// DjibMark - Omnipresent execution tracing
#include "djibmark.h"
#include "interface.h"      // Simple loading overlay (off by default)

// GGUF support
#include "gguf_loader.h"
#include "gguf_infer.h"

typedef enum {
    LLMK_MODEL_FMT_UNKNOWN = 0,
    LLMK_MODEL_FMT_BIN = 1,
    LLMK_MODEL_FMT_GGUF = 2,
} LlmkModelFormat;

static LlmkModelFormat g_loaded_model_format = LLMK_MODEL_FMT_UNKNOWN;
static CHAR16 g_loaded_model_path16[160];
static volatile UINT32 g_loaded_model_path16_canary = 0xD1B1D1B1u;
static GgufSummary g_loaded_model_gguf;
static int g_loaded_model_gguf_valid = 0;

// Forward decl used by early GGUF summary printer.
static void llmk_print_ascii(const char *s);

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

static LlmkBootMark g_boot_marks[16];
static int g_boot_mark_count = 0;

static unsigned long long g_overlay_stage_start_us = 0;
static unsigned long long g_overlay_stage_prev_us = 0;

static void llmk_overlay_stage(UINT32 stage_index_1based, UINT32 stage_count) {
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

static void llmk_boot_mark(const CHAR16 *name) {
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
    UINT8 m[4];
    EFI_STATUS st = llmk_peek_magic4(f, m);
    if (EFI_ERROR(st)) return LLMK_MODEL_FMT_UNKNOWN;
    if (m[0] == 'G' && m[1] == 'G' && m[2] == 'U' && m[3] == 'F') return LLMK_MODEL_FMT_GGUF;
    // .bin (llama2.c weights) does not have a magic; treat as BIN by default.
    return LLMK_MODEL_FMT_BIN;
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
    return last_dot[1] != 0;
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
    CHAR16 ext_s[16];
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
    CHAR16 prefix6[8];
    int p6 = 0;
    for (int i = 0; i < bn && p6 < 6; i++) {
        prefix6[p6++] = base_s[i];
    }
    prefix6[p6] = 0;
    if (p6 <= 0) return st;

    for (int n = 1; n <= 9; n++) {
        CHAR16 alias_leaf[32];
        alias_leaf[0] = 0;
        StrCpy(alias_leaf, prefix6);
        StrCat(alias_leaf, L"~");
        {
            CHAR16 digit[2];
            digit[0] = (CHAR16)(L'0' + n);
            digit[1] = 0;
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

// Forward declarations (used by early config loaders)
static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name);
static void llmk_cfg_trim(char **s);
static char llmk_cfg_tolower(char c);
static int llmk_cfg_streq_ci(const char *a, const char *b);
static int llmk_cfg_parse_i32(const char *s, int *out);
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

static void llmk_load_repl_cfg_diopion_best_effort(DiopionEngine *e) {
    if (!e) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    int applied = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "diopion_mode")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                diopion_set_mode(e, (DiopionMode)v);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_profile")) {
            if (val && val[0]) {
                // token -> enum
                if (llmk_cfg_streq_ci(val, "animal")) diopion_set_profile(e, DIOPION_PROFILE_ANIMAL);
                else if (llmk_cfg_streq_ci(val, "vegetal")) diopion_set_profile(e, DIOPION_PROFILE_VEGETAL);
                else if (llmk_cfg_streq_ci(val, "geom") || llmk_cfg_streq_ci(val, "geometric")) diopion_set_profile(e, DIOPION_PROFILE_GEOM);
                else if (llmk_cfg_streq_ci(val, "bio") || llmk_cfg_streq_ci(val, "biological")) diopion_set_profile(e, DIOPION_PROFILE_BIO);
                else diopion_set_profile(e, DIOPION_PROFILE_NONE);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_turns")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 16) v = 16;
                e->params.burst_turns_default = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_tokens") || llmk_cfg_streq_ci(key, "diopion_burst_max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 16) v = 16;
                if (v > 1024) v = 1024;
                e->params.burst_max_gen_tokens = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_topk")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 200) v = 200;
                e->params.burst_top_k = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "diopion_burst_temp_milli") || llmk_cfg_streq_ci(key, "diopion_burst_temp")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 50) v = 50;
                if (v > 2000) v = 2000;
                e->params.burst_temp_milli = (UINT32)v;
                applied = 1;
            }
        }
    }

    if (applied) {
        Print(L"[cfg] diopion: mode=");
        llmk_print_ascii(diopion_mode_name_ascii(e->mode));
        Print(L" profile=");
        llmk_print_ascii(diopion_profile_name_ascii(e->profile));
        Print(L"\r\n");
    }
}

static const CHAR16 *djibion_mode_name(DjibionMode m) {
    if (m == DJIBION_MODE_OFF) return L"off";
    if (m == DJIBION_MODE_OBSERVE) return L"observe";
    if (m == DJIBION_MODE_ENFORCE) return L"enforce";
    return L"?";
}

// Forward decl: used by early repl.cfg loaders before definition.
static int llmk_cfg_parse_bool(const char *s, int *out);

static int djibion_should_block(const DjibionEngine *e, const DjibionDecision *d) {
    if (!e || !d) return 0;
    if (e->mode != DJIBION_MODE_ENFORCE) return 0;
    return (d->verdict == DJIBION_VERDICT_REJECT || d->verdict == DJIBION_VERDICT_FREEZE);
}
#define DIM 288
#define HIDDEN_DIM 768
#define N_LAYERS 6
#define N_HEADS 6
#define N_KV_HEADS 6
#define VOCAB_SIZE 32000
#define SEQ_LEN 256
#define MAX_TOKENS 256

// Token ids used by this tiny tokenizer export.
// NOTE: encode() currently inserts BOS=1.
#define TOKEN_BOS 1
#define TOKEN_EOS 2

static int has_suffix_repeat(const int* tokens, int n_tokens, int span) {
    if (span <= 0) return 0;
    if (n_tokens < 2 * span) return 0;
    for (int i = 0; i < span; i++) {
        if (tokens[n_tokens - span + i] != tokens[n_tokens - 2 * span + i]) return 0;
    }
    return 1;
}

// AVX2 attention helpers live in attention_avx2.c (compiled with -mavx2)
float llmk_dot_f32_avx2(const float *a, const float *b, int n);
void llmk_axpy_f32_avx2(float *dst, const float *src, float alpha, int n);

static int g_attn_use_avx2 = 0;
// -1=auto, 0=force SSE2, 1=force AVX2 (only allowed if auto-detected AVX2 is enabled)
static int g_attn_force = -1;

// One-shot fail-safe test harness.
static int g_test_failsafe_active = 0;
static BOOLEAN g_test_failsafe_prev_strict_budget = FALSE;
static UINT64 g_test_failsafe_prev_prefill = 0;
static UINT64 g_test_failsafe_prev_decode = 0;

static void uefi_print_utf8_decode(const unsigned char *p, int len) {
    if (!p || len <= 0) return;

    // Convert UTF-8 bytes to UTF-16 and stream to the UEFI console.
    // Uses U+FFFD replacement on invalid sequences.
    CHAR16 out[256];
    int out_len = 0;

    int i = 0;
    while (i < len) {
        UINT32 cp = 0xFFFD;
        unsigned char b0 = p[i];

        if (b0 < 0x80) {
            cp = (UINT32)b0;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 < len) {
                unsigned char b1 = p[i + 1];
                if ((b1 & 0xC0) == 0x80) {
                    cp = ((UINT32)(b0 & 0x1F) << 6) | (UINT32)(b1 & 0x3F);
                    if (cp < 0x80) cp = 0xFFFD;
                    i += 2;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 < len) {
                unsigned char b1 = p[i + 1];
                unsigned char b2 = p[i + 2];
                if (((b1 & 0xC0) == 0x80) && ((b2 & 0xC0) == 0x80)) {
                    cp = ((UINT32)(b0 & 0x0F) << 12) | ((UINT32)(b1 & 0x3F) << 6) | (UINT32)(b2 & 0x3F);
                    if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                    i += 3;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 < len) {
                unsigned char b1 = p[i + 1];
                unsigned char b2 = p[i + 2];
                unsigned char b3 = p[i + 3];
                if (((b1 & 0xC0) == 0x80) && ((b2 & 0xC0) == 0x80) && ((b3 & 0xC0) == 0x80)) {
                    cp = ((UINT32)(b0 & 0x07) << 18) | ((UINT32)(b1 & 0x3F) << 12) | ((UINT32)(b2 & 0x3F) << 6) | (UINT32)(b3 & 0x3F);
                    if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
                    i += 4;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }

        if (out_len > (int)(sizeof(out) / sizeof(out[0])) - 3) {
            out[out_len] = 0;
            uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, out);
            out_len = 0;
        }

        if (cp <= 0xFFFF) {
            out[out_len++] = (CHAR16)cp;
        } else {
            cp -= 0x10000;
            out[out_len++] = (CHAR16)(0xD800 + (cp >> 10));
            out[out_len++] = (CHAR16)(0xDC00 + (cp & 0x3FF));
        }
    }

    if (out_len > 0) {
        out[out_len] = 0;
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, out);
    }
}

// -----------------------------------------------------------------------------
// Minimal serial debug (COM1) so QEMU -serial file captures key diagnostics.
// OVMF typically exposes COM1 at 0x3F8.
// -----------------------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)
static __inline__ UINT8 llmk_inb(UINT16 port) {
    UINT8 ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static __inline__ void llmk_outb(UINT16 port, UINT8 val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void llmk_serial_putc(UINT8 c) {
    const UINT16 COM1 = 0x3F8;
    const UINT16 LSR = (UINT16)(COM1 + 5);
    // Wait for THR empty (bit 5). Bounded spin to avoid hangs on platforms without a UART.
    for (UINT32 spin = 0; spin < 200000; spin++) {
        if (llmk_inb(LSR) & 0x20) {
            llmk_outb(COM1, c);
            return;
        }
    }
}

static void llmk_serial_write_char16(const CHAR16 *s) {
    if (!s) return;
    for (UINTN i = 0; s[i]; i++) {
        CHAR16 wc = s[i];
        UINT8 c = (wc >= 0x20 && wc < 0x7f) ? (UINT8)wc : (UINT8)'?';
        if (c == '\n') llmk_serial_putc('\r');
        llmk_serial_putc(c);
    }
}
#else
static void llmk_serial_write_char16(const CHAR16 *s) { (void)s; }
#endif

// Some generations still contain the classic mojibake sequence "ÔÇÖ" for U+2019.
// This can span token boundaries, so keep a small byte tail and repair across calls.
static unsigned char g_utf8_repair_tail[5];
static int g_utf8_repair_tail_len = 0;

// GOP transcript (best-effort): capture streamed UTF-8 output into an ASCII-ish ring buffer
// so we can render it later in the GOP UI.
static void llmk_tr_append_ascii_bytes(const unsigned char *bytes, int len);

static void uefi_print_utf8_bytes(const char *bytes, int len) {
    if (!bytes || len <= 0) return;

    typedef struct {
        unsigned char pat[6];
        unsigned char rep[3];
    } Mojimap;

    // Common mojibake seen in generations (CP437-ish smart punctuation).
    // Each pat is UTF-8 for the visible mojibake string; rep is UTF-8 for the intended punctuation.
    static const Mojimap maps[] = {
        // ÔÇÖ -> ’
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC3, 0x96 }, { 0xE2, 0x80, 0x99 } },
        // ÔÇ£ -> “
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC2, 0xA3 }, { 0xE2, 0x80, 0x9C } },
        // ÔÇØ -> ”
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC3, 0x98 }, { 0xE2, 0x80, 0x9D } },
        // ÔÇö -> —
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC3, 0xB6 }, { 0xE2, 0x80, 0x94 } },
        // ÔÇª -> …
        { { 0xC3, 0x94, 0xC3, 0x87, 0xC2, 0xAA }, { 0xE2, 0x80, 0xA6 } },
    };

    const int keep = 5; // pat_len - 1
    unsigned char inbuf[512];
    unsigned char outbuf[512];

    int offset = 0;
    while (offset < len) {
        int inlen = 0;
        for (int i = 0; i < g_utf8_repair_tail_len && inlen < (int)sizeof(inbuf); i++) {
            inbuf[inlen++] = g_utf8_repair_tail[i];
        }

        int cap = (int)sizeof(inbuf) - inlen;
        int take = len - offset;
        if (take > cap) take = cap;
        for (int i = 0; i < take; i++) {
            inbuf[inlen++] = (unsigned char)bytes[offset + i];
        }
        offset += take;

        if (inlen <= 0) return;

        if (inlen <= keep) {
            g_utf8_repair_tail_len = inlen;
            for (int i = 0; i < inlen; i++) g_utf8_repair_tail[i] = inbuf[i];
            continue;
        }

        int upto = inlen - keep;
        int outlen = 0;
        int j = 0;
        while (j < upto && outlen < (int)sizeof(outbuf)) {
            int matched = 0;
            if (j + 6 <= upto) {
                for (UINTN m = 0; m < (sizeof(maps) / sizeof(maps[0])); m++) {
                    const Mojimap *mm = &maps[m];
                    if (inbuf[j + 0] == mm->pat[0] && inbuf[j + 1] == mm->pat[1] && inbuf[j + 2] == mm->pat[2] &&
                        inbuf[j + 3] == mm->pat[3] && inbuf[j + 4] == mm->pat[4] && inbuf[j + 5] == mm->pat[5]) {
                        if (outlen + 3 <= (int)sizeof(outbuf)) {
                            outbuf[outlen++] = mm->rep[0];
                            outbuf[outlen++] = mm->rep[1];
                            outbuf[outlen++] = mm->rep[2];
                        }
                        j += 6;
                        matched = 1;
                        break;
                    }
                }
            }
            if (matched) continue;
            outbuf[outlen++] = inbuf[j++];
        }

        // Save tail for boundary-spanning repair.
        g_utf8_repair_tail_len = keep;
        for (int i = 0; i < keep; i++) g_utf8_repair_tail[i] = inbuf[upto + i];

        // Decode+print processed bytes.
        llmk_tr_append_ascii_bytes(outbuf, outlen);
        uefi_print_utf8_decode(outbuf, outlen);

        // If we ever filled the buffer before consuming all of upto, drop the remainder to avoid
        // stalling. This should be extremely rare with typical tokenizer pieces.
        // (We intentionally keep this minimal and avoid heap allocations.)
        if (j < upto) {
            // best-effort: continue printing remaining bytes directly (no repair inside this chunk)
            llmk_tr_append_ascii_bytes(inbuf + j, upto - j);
            uefi_print_utf8_decode(inbuf + j, upto - j);
        }
    }
}

static void uefi_print_utf8_flush(void) {
    if (g_utf8_repair_tail_len <= 0) return;
    uefi_print_utf8_decode(g_utf8_repair_tail, g_utf8_repair_tail_len);
    g_utf8_repair_tail_len = 0;
}

// Best-effort: enable AVX state (OSXSAVE + XCR0) in UEFI so AVX/AVX2 code can run.
// Without an OS, some firmwares leave XCR0 unset; QEMU/OVMF often does.
static inline void cpuidex_u32(UINT32 leaf, UINT32 subleaf, UINT32 *eax, UINT32 *ebx, UINT32 *ecx, UINT32 *edx) {
    UINT32 a, b, c, d;
    __asm__ volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
        : "memory"
    );
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static inline UINT64 read_cr4_u64(void) {
    UINT64 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4_u64(UINT64 v) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(v) : "memory");
}

static void enable_avx_best_effort(void) {
    UINT32 eax, ebx, ecx, edx;
    cpuidex_u32(1, 0, &eax, &ebx, &ecx, &edx);
    int has_xsave = (ecx & (1u << 26)) != 0;
    int has_avx_hw = (ecx & (1u << 28)) != 0;
    if (!has_xsave || !has_avx_hw) return;

    // Enable OSXSAVE in CR4 (bit 18).
    UINT64 cr4 = read_cr4_u64();
    if ((cr4 & (1ULL << 18)) == 0) {
        write_cr4_u64(cr4 | (1ULL << 18));
    }

    // Enable x87 (bit0), XMM (bit1), YMM (bit2) state in XCR0.
    UINT32 xcr0_lo, xcr0_hi;
    __asm__ volatile(
        "xgetbv"
        : "=a"(xcr0_lo), "=d"(xcr0_hi)
        : "c"(0)
        : "memory"
    );
    UINT32 new_lo = xcr0_lo | 0x7u;
    if (new_lo != xcr0_lo) {
        __asm__ volatile(
            "xsetbv"
            :: "a"(new_lo), "d"(xcr0_hi), "c"(0)
            : "memory"
        );
    }
}

static void apply_no_repeat_ngram(float* logits, int vocab_size, const int* tokens, int n_tokens, int ngram) {
    if (ngram < 2) return;
    if (n_tokens < ngram - 1) return;

    int prefix_len = ngram - 1;
    int prefix_start = n_tokens - prefix_len;
    int limit = n_tokens - ngram;
    for (int i = 0; i <= limit; i++) {
        int match = 1;
        for (int j = 0; j < prefix_len; j++) {
            if (tokens[i + j] != tokens[prefix_start + j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            int banned = tokens[i + prefix_len];
            if (banned >= 0 && banned < vocab_size) {
                // Large negative value to effectively zero it after softmax.
                logits[banned] = -1.0e9f;
            }
        }
    }
}

static inline float dot_f32_sse2(const float* a, const float* b, int n) {
#if defined(__x86_64__) || defined(_M_X64)
    __m128 sum = _mm_setzero_ps();
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        sum = _mm_add_ps(sum, _mm_mul_ps(va, vb));
    }
    float tmp[4];
    _mm_storeu_ps(tmp, sum);
    float total = tmp[0] + tmp[1] + tmp[2] + tmp[3];
    for (; i < n; i++) total += a[i] * b[i];
    return total;
#else
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += a[i] * b[i];
    return total;
#endif
}

static inline void axpy_f32_sse2(float* dst, const float* src, float a, int n) {
#if defined(__x86_64__) || defined(_M_X64)
    __m128 va = _mm_set1_ps(a);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 vd = _mm_loadu_ps(dst + i);
        __m128 vs = _mm_loadu_ps(src + i);
        vd = _mm_add_ps(vd, _mm_mul_ps(va, vs));
        _mm_storeu_ps(dst + i, vd);
    }
    for (; i < n; i++) dst[i] += a * src[i];
#else
    for (int i = 0; i < n; i++) dst[i] += a * src[i];
#endif
}

static inline float dot_f32_best(const float* a, const float* b, int n) {
    int use_avx2 = g_attn_use_avx2;
    if (g_attn_force == 0) use_avx2 = 0;
    else if (g_attn_force == 1) use_avx2 = 1;
    if (use_avx2) return llmk_dot_f32_avx2(a, b, n);
    return dot_f32_sse2(a, b, n);
}

static inline void axpy_f32_best(float* dst, const float* src, float a, int n) {
    int use_avx2 = g_attn_use_avx2;
    if (g_attn_force == 0) use_avx2 = 0;
    else if (g_attn_force == 1) use_avx2 = 1;
    if (use_avx2) { llmk_axpy_f32_avx2(dst, src, a, n); return; }
    axpy_f32_sse2(dst, src, a, n);
}

// ============================================================================
// HEAP ALLOCATOR
// ============================================================================

static char* heap_base = NULL;
static unsigned long heap_offset = 0;
static unsigned long heap_size = 0;

static LlmkZones g_zones;
static LlmkLog g_llmk_log;
static LlmkSentinel g_sentinel;
static int g_llmk_ready = 0;

// DjibMark global state
DjibMarkState g_djibmark_state = {0};

// Root volume handle (set after OpenVolume). Used for best-effort dumps to files.
static EFI_FILE_HANDLE g_root = NULL;

// GOP framebuffer (best-effort; may be unavailable on headless firmware paths)
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;
static UINT32 g_gop_w = 0;
static UINT32 g_gop_h = 0;
static UINT32 g_gop_ppsl = 0;
static EFI_GRAPHICS_PIXEL_FORMAT g_gop_pf = PixelFormatMax;
static EFI_PIXEL_BITMASK g_gop_mask = {0};
static volatile UINT32 *g_gop_fb32 = NULL;

// Mirror the REPL's KV position into a global so optional systems (like GOP TUI)
// can read it without threading local state through many functions.
static int g_llmk_kv_pos = 0;

// Minimal GOP-based TUI overlay (optional).
static int g_tui_enabled = 0;
static int g_tui_dirty = 0;
static int g_tui_last_id = 0;
static int g_tui_last_tick = 0;
static int g_tui_last_energy = 0;
static char g_tui_last_event[64] = "";

// Live generation counters (best-effort): updated during decode loop so the TUI can
// show progress even while the terminal output is streaming.
static int g_tui_gen_active = 0;
static int g_tui_gen_tokens = 0;

// GOP UI modes
// 0=status panel only (legacy)
// 1=log view (full-width)
// 2=split (log + files)
// 3=files focus (log + files, selection emphasized)
static int g_ui_mode = 0;

// Transcript ring buffer (ASCII-ish)
#define LLMK_TR_LINES 192
#define LLMK_TR_COLS  128
static char g_tr_lines[LLMK_TR_LINES][LLMK_TR_COLS];
static UINT32 g_tr_write = 0; // next write slot
static UINT32 g_tr_count = 0; // number of valid lines
static char g_tr_cur[LLMK_TR_COLS];
static int g_tr_cur_len = 0;
static int g_tr_scroll = 0; // how many lines back from newest

// GOP file browser (command-driven)
#define LLMK_FB_MAX_ENTRIES 96
typedef struct {
    CHAR16 name16[64];
    char name8[64];
    int is_dir;
    UINT64 size;
} LlmkFbEntry;

static CHAR16 g_fb_path16[128] = L"\\";
static char g_fb_path8[128] = "\\";
static LlmkFbEntry g_fb_entries[LLMK_FB_MAX_ENTRIES];
static int g_fb_count = 0;
static int g_fb_sel = 0;

#define LLMK_FB_PREVIEW_LINES 12
#define LLMK_FB_PREVIEW_COLS  96
static char g_fb_preview[LLMK_FB_PREVIEW_LINES][LLMK_FB_PREVIEW_COLS];
static int g_fb_preview_count = 0;

// Capture mode: used by /draw to collect model output (DSL) instead of printing it.
static int g_capture_mode = 0;
static char g_capture_buf[2048];
static int g_capture_len = 0;
static int g_capture_truncated = 0;

// /oo_auto persistent state (runs multiple think cycles back-to-back)
static int g_oo_auto_active = 0;
static int g_oo_auto_id = 0;
static int g_oo_auto_remaining = 0;
static int g_oo_auto_total = 0;
static char g_oo_auto_user[256];

// /oo_exec persistent state (agenda runner)
static int g_oo_exec_active = 0;
static int g_oo_exec_id = 0;
static int g_oo_exec_remaining = 0;
static int g_oo_exec_total = 0;
static int g_oo_exec_plan_if_empty = 0;
static char g_oo_exec_hint[256];

// M16.1: Runtime metrics (tokens/sec, latency, memory pressure, sentinel)
typedef struct {
    UINT64 session_start_cycles;
    UINT64 total_prefill_cycles;
    UINT64 total_decode_cycles;
    UINT32 total_prefill_tokens;
    UINT32 total_decode_tokens;
    UINT32 total_prefill_calls;
    UINT32 total_decode_calls;
    UINT64 last_prefill_cycles;
    UINT64 last_decode_cycles;
    UINT32 last_prefill_tokens;
    UINT32 last_decode_tokens;
    UINT32 sentinel_violations_total;
    UINT32 kv_cache_resets;
    UINT32 generation_count;
} LlmkRuntimeMetrics;

static LlmkRuntimeMetrics g_metrics = {0};

static void llmk_metrics_reset(void) {
    g_metrics.session_start_cycles = __rdtsc();
    g_metrics.total_prefill_cycles = 0;
    g_metrics.total_decode_cycles = 0;
    g_metrics.total_prefill_tokens = 0;
    g_metrics.total_decode_tokens = 0;
    g_metrics.total_prefill_calls = 0;
    g_metrics.total_decode_calls = 0;
    g_metrics.last_prefill_cycles = 0;
    g_metrics.last_decode_cycles = 0;
    g_metrics.last_prefill_tokens = 0;
    g_metrics.last_decode_tokens = 0;
    g_metrics.sentinel_violations_total = 0;
    g_metrics.kv_cache_resets = 0;
    g_metrics.generation_count = 0;
}

static void llmk_capture_reset(void) {
    g_capture_len = 0;
    g_capture_truncated = 0;
    g_capture_buf[0] = 0;
}

static void llmk_capture_append_ascii(const char *piece, int len) {
    if (!piece || len <= 0) return;
    if (g_capture_len >= (int)sizeof(g_capture_buf) - 1) {
        g_capture_truncated = 1;
        return;
    }
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)piece[i];
        // Keep a conservative ASCII subset. Map CR->LF and drop others.
        if (c == '\r') c = '\n';
        if (c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7E)) {
            if (c == '`') c = ' '; // avoid markdown fences
            g_capture_buf[g_capture_len++] = (char)c;
            if (g_capture_len >= (int)sizeof(g_capture_buf) - 1) {
                g_capture_truncated = 1;
                break;
            }
        }
    }
    g_capture_buf[g_capture_len] = 0;
}

static void llmk_capture_sanitize_inplace(void) {
    // Trim leading whitespace
    int start = 0;
    while (start < g_capture_len && (g_capture_buf[start] == ' ' || g_capture_buf[start] == '\n' || g_capture_buf[start] == '\t')) start++;
    if (start > 0) {
        for (int i = 0; i + start <= g_capture_len; i++) g_capture_buf[i] = g_capture_buf[i + start];
        g_capture_len -= start;
    }

    // Truncate at an END marker if present
    for (int i = 0; i + 2 < g_capture_len; i++) {
        if (g_capture_buf[i] == 'E' && g_capture_buf[i + 1] == 'N' && g_capture_buf[i + 2] == 'D') {
            g_capture_buf[i] = 0;
            g_capture_len = i;
            break;
        }
    }

    // Replace any non-useful characters
    for (int i = 0; i < g_capture_len; i++) {
        char c = g_capture_buf[i];
        if (!(c == '\n' || c == '\t' || c == ';' || c == '-' || c == '_' || c == ',' || c == '.' || c == ':' || c == '(' || c == ')' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ')) {
            g_capture_buf[i] = ' ';
        }
    }
    // Ensure null termination
    g_capture_buf[g_capture_len] = 0;
}

static int llmk_oo_build_think_prompt(int id, const char *user, char *out, int out_cap) {
    if (!out || out_cap <= 4) return 0;
    out[0] = 0;

    char goal[160];
    char dig[256];
    char tail[256];
    char next_action[96];
    goal[0] = 0;
    dig[0] = 0;
    tail[0] = 0;
    next_action[0] = 0;

    if (!llmk_oo_get_brief(id, goal, (int)sizeof(goal), dig, (int)sizeof(dig))) {
        return 0;
    }
    llmk_oo_get_notes_tail(id, tail, (int)sizeof(tail), 240);
    llmk_oo_agenda_peek(id, next_action, (int)sizeof(next_action));
    int todo = llmk_oo_agenda_count(id);

    int p = 0;
    const char *prefix = "OO_THINK. Respond concisely. Goal: ";
    for (const char *s = prefix; *s && p + 1 < out_cap; s++) out[p++] = *s;
    for (int k = 0; goal[k] && p + 1 < out_cap; k++) out[p++] = goal[k];

    if (dig[0]) {
        const char *d1 = "\nDigest: ";
        for (const char *s = d1; *s && p + 1 < out_cap; s++) out[p++] = *s;
        for (int k = 0; dig[k] && p + 1 < out_cap; k++) out[p++] = dig[k];
    }
    if (tail[0]) {
        const char *n1 = "\nNotes: ";
        for (const char *s = n1; *s && p + 1 < out_cap; s++) out[p++] = *s;
        for (int k = 0; tail[k] && p + 1 < out_cap; k++) out[p++] = tail[k];
    }

    if (next_action[0]) {
        const char *a1 = "\nNext action: ";
        for (const char *s = a1; *s && p + 1 < out_cap; s++) out[p++] = *s;
        for (int k = 0; next_action[k] && p + 1 < out_cap; k++) out[p++] = next_action[k];
        if (todo > 1) {
            const char *a2 = " (";
            for (const char *s = a2; *s && p + 1 < out_cap; s++) out[p++] = *s;
            // small itoa
            int v = todo;
            char rev[16];
            int rn = 0;
            while (v > 0 && rn < (int)sizeof(rev)) { rev[rn++] = (char)('0' + (v % 10)); v /= 10; }
            while (rn > 0 && p + 1 < out_cap) out[p++] = rev[--rn];
            const char *a3 = " total)";
            for (const char *s = a3; *s && p + 1 < out_cap; s++) out[p++] = *s;
        }
    }

    const char *u1 = "\nUser: ";
    for (const char *s = u1; *s && p + 1 < out_cap; s++) out[p++] = *s;
    if (user && user[0]) {
        for (const char *s = user; *s && p + 1 < out_cap; s++) out[p++] = *s;
    } else {
        const char *def = "next concrete action";
        for (const char *s = def; *s && p + 1 < out_cap; s++) out[p++] = *s;
    }

    const char *suf = "\nAnswer:\n";
    for (const char *s = suf; *s && p + 1 < out_cap; s++) out[p++] = *s;
    out[p] = 0;
    return 1;
}

static int llmk_ascii_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

// Parse an entity id from prompt, starting at *io_i.
// Accepts either "1" or "<1>" (optional whitespace inside the brackets).
// Updates *io_i to the first non-space char after the id (and optional '>').
static int llmk_parse_entity_id_allow_brackets(const char *prompt, int *io_i) {
    if (!prompt || !io_i) return 0;
    int i = *io_i;
    while (prompt[i] == ' ' || prompt[i] == '\t') i++;

    int had_bracket = 0;
    if (prompt[i] == '<') {
        had_bracket = 1;
        i++;
        while (prompt[i] == ' ' || prompt[i] == '\t') i++;
    }

    int id = 0;
    while (prompt[i] >= '0' && prompt[i] <= '9') {
        id = id * 10 + (prompt[i] - '0');
        i++;
    }

    while (prompt[i] == ' ' || prompt[i] == '\t') i++;
    if (had_bracket && prompt[i] == '>') i++;
    while (prompt[i] == ' ' || prompt[i] == '\t') i++;

    *io_i = i;
    return id;
}

static UINT32 llmk_u32_ctz(UINT32 x) {
    if (x == 0) return 32;
    UINT32 n = 0;
    while ((x & 1U) == 0U) { n++; x >>= 1; }
    return n;
}

static UINT32 llmk_u32_popcount(UINT32 x) {
    UINT32 n = 0;
    while (x) { x &= (x - 1U); n++; }
    return n;
}

static EFI_STATUS llmk_gop_init_best_effort(void) {
    g_gop = NULL;
    g_gop_fb32 = NULL;
    g_gop_w = g_gop_h = g_gop_ppsl = 0;
    g_gop_pf = PixelFormatMax;
    g_gop_mask.RedMask = g_gop_mask.GreenMask = g_gop_mask.BlueMask = g_gop_mask.ReservedMask = 0;

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateProtocol, 3, &gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop);
    if (EFI_ERROR(st) || !gop || !gop->Mode || !gop->Mode->Info) {
        return EFI_NOT_FOUND;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    if (info->PixelFormat == PixelBltOnly) {
        return EFI_UNSUPPORTED;
    }
    if (gop->Mode->FrameBufferBase == 0 || gop->Mode->FrameBufferSize < 4) {
        return EFI_UNSUPPORTED;
    }

    g_gop = gop;
    g_gop_w = info->HorizontalResolution;
    g_gop_h = info->VerticalResolution;
    g_gop_ppsl = info->PixelsPerScanLine;
    g_gop_pf = info->PixelFormat;
    g_gop_mask = info->PixelInformation;
    g_gop_fb32 = (volatile UINT32 *)(UINTN)gop->Mode->FrameBufferBase;

    // Sanity: require 32bpp-like stride.
    UINT64 needed = (UINT64)g_gop_ppsl * (UINT64)g_gop_h * 4ULL;
    if (needed > (UINT64)gop->Mode->FrameBufferSize) {
        // Still allow writes, but clamp later.
    }
    return EFI_SUCCESS;
}

// Force screen update after rendering (some firmware needs this trigger)
static void llmk_gop_force_update(void) {
    if (!g_gop || !g_gop_fb32) return;
    // Touch a single pixel at 0,0 to trigger screen refresh
    volatile UINT32 *fb = (volatile UINT32 *)g_gop_fb32;
    UINT32 old = fb[0];
    fb[0] = old ^ 0x00000001;  // Flip LSB
    fb[0] = old;               // Restore original
}

static void llmk_gop_put_pixel(UINT32 x, UINT32 y, UINT8 r, UINT8 g, UINT8 b);

// Forward decls (used by optional GOP TUI before their definitions).
static void llmk_ascii_copy_cap(char *dst, int dst_cap, const char *src);
static int llmk_ascii_append_u32(char *dst, int cap, int pos, UINT32 v);
static void llmk_print_ascii(const char *s);
static void llmk_tui_redraw_best_effort(void);
static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name);
static int llmk_char16_streq(const CHAR16 *a, const CHAR16 *b);

static void djibion_apply_transform_path(char *io_path, int cap, const DjibionDecision *d) {
    if (!io_path || cap <= 1 || !d) return;
    if (d->verdict != DJIBION_VERDICT_TRANSFORM) return;
    if (!d->transformed_arg0[0]) return;
    llmk_ascii_copy_cap(io_path, cap, d->transformed_arg0);
}

static void djibion_log_if_observe(const DjibionEngine *e, const char *act_name, const DjibionDecision *d) {
    if (!e || !d || !act_name) return;
    if (e->mode != DJIBION_MODE_OBSERVE) return;

    Print(L"[djibion] ");
    llmk_print_ascii(act_name);
    Print(L" verdict=%d risk=%d tri=%d/%d/%d reason=",
          (int)d->verdict,
          (int)d->risk,
          (int)d->tri.sense.score,
          (int)d->tri.structure.score,
          (int)d->tri.reality.score);
    if (d->reason[0]) llmk_print_ascii(d->reason);
    else Print(L"(none)");
    if (d->verdict == DJIBION_VERDICT_TRANSFORM && d->transformed_arg0[0]) {
        Print(L" transform->");
        llmk_print_ascii(d->transformed_arg0);
    }
    Print(L"\r\n");
}

static UINT32 llmk_gop_pack_rgb(UINT8 r, UINT8 g, UINT8 b, int *out_ok) {
    if (out_ok) *out_ok = 0;
    if (g_gop_pf == PixelBlueGreenRedReserved8BitPerColor) {
        if (out_ok) *out_ok = 1;
        return ((UINT32)b) | ((UINT32)g << 8) | ((UINT32)r << 16) | (0xFFU << 24);
    }
    if (g_gop_pf == PixelRedGreenBlueReserved8BitPerColor) {
        if (out_ok) *out_ok = 1;
        return ((UINT32)r) | ((UINT32)g << 8) | ((UINT32)b << 16) | (0xFFU << 24);
    }
    if (g_gop_pf == PixelBitMask) {
        UINT32 rm = g_gop_mask.RedMask;
        UINT32 gm = g_gop_mask.GreenMask;
        UINT32 bm = g_gop_mask.BlueMask;
        UINT32 rs = llmk_u32_ctz(rm);
        UINT32 gs = llmk_u32_ctz(gm);
        UINT32 bs = llmk_u32_ctz(bm);
        UINT32 rbits = llmk_u32_popcount(rm);
        UINT32 gbits = llmk_u32_popcount(gm);
        UINT32 bbits = llmk_u32_popcount(bm);
        UINT32 rmax = (rbits >= 32) ? 0xFFFFFFFFU : ((1U << rbits) - 1U);
        UINT32 gmax = (gbits >= 32) ? 0xFFFFFFFFU : ((1U << gbits) - 1U);
        UINT32 bmax = (bbits >= 32) ? 0xFFFFFFFFU : ((1U << bbits) - 1U);
        UINT32 rv = (rmax == 0) ? 0 : ((UINT32)r * rmax + 127U) / 255U;
        UINT32 gv = (gmax == 0) ? 0 : ((UINT32)g * gmax + 127U) / 255U;
        UINT32 bv = (bmax == 0) ? 0 : ((UINT32)b * bmax + 127U) / 255U;
        if (out_ok) *out_ok = 1;
        return ((rv << rs) & rm) | ((gv << gs) & gm) | ((bv << bs) & bm);
    }
    return 0;
}

static void llmk_gop_fill_rect_solid(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    if (w == 0 || h == 0) return;
    if (x >= g_gop_w || y >= g_gop_h) return;
    UINT32 x2 = x + w;
    UINT32 y2 = y + h;
    if (x2 > g_gop_w) x2 = g_gop_w;
    if (y2 > g_gop_h) y2 = g_gop_h;
    int ok = 0;
    UINT32 px = llmk_gop_pack_rgb(r, g, b, &ok);
    if (!ok) return;
    for (UINT32 yy = y; yy < y2; yy++) {
        UINTN row = (UINTN)yy * (UINTN)g_gop_ppsl;
        for (UINT32 xx = x; xx < x2; xx++) {
            g_gop_fb32[row + (UINTN)xx] = px;
        }
    }
}

typedef struct {
    char c;
    UINT8 rows[7]; // 5-bit rows, MSB on the left (bits 4..0)
} LlmkGlyph5x7;

static const LlmkGlyph5x7 g_font_5x7[] = {
    { ' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
    { '-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00} },
    { '_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F} },
    { '.', {0x00,0x00,0x00,0x00,0x00,0x06,0x06} },
    { ':', {0x00,0x06,0x06,0x00,0x06,0x06,0x00} },
    { '/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00} },
    { '<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02} },
    { '>', {0x08,0x04,0x02,0x01,0x02,0x04,0x08} },
    { '[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E} },
    { ']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E} },
    { '(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02} },
    { ')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08} },
    { '*', {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00} },
    { '#', {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00} },
    { '+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00} },
    { '=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00} },
    { '?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04} },
    { '0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E} },
    { '1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E} },
    { '2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F} },
    { '3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E} },
    { '4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02} },
    { '5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E} },
    { '6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E} },
    { '7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08} },
    { '8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E} },
    { '9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C} },
    { 'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E} },
    { 'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E} },
    { 'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E} },
    { 'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F} },
    { 'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10} },
    { 'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F} },
    { 'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E} },
    { 'J', {0x07,0x02,0x02,0x02,0x12,0x12,0x0C} },
    { 'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11} },
    { 'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F} },
    { 'M', {0x11,0x1B,0x15,0x11,0x11,0x11,0x11} },
    { 'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11} },
    { 'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10} },
    { 'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D} },
    { 'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11} },
    { 'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E} },
    { 'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04} },
    { 'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04} },
    { 'W', {0x11,0x11,0x11,0x11,0x15,0x1B,0x11} },
    { 'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11} },
    { 'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04} },
    { 'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F} },
};

static const UINT8 *llmk_font5x7_get(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (UINTN i = 0; i < (sizeof(g_font_5x7) / sizeof(g_font_5x7[0])); i++) {
        if (g_font_5x7[i].c == c) return g_font_5x7[i].rows;
    }
    // fallback
    for (UINTN i = 0; i < (sizeof(g_font_5x7) / sizeof(g_font_5x7[0])); i++) {
        if (g_font_5x7[i].c == '?') return g_font_5x7[i].rows;
    }
    return NULL;
}

static void llmk_tui_set_event(const char *msg) {
    if (!msg) { g_tui_last_event[0] = 0; return; }
    llmk_ascii_copy_cap(g_tui_last_event, (int)sizeof(g_tui_last_event), msg);
    g_tui_dirty = 1;
}

static void llmk_tr_clear(void) {
    g_tr_write = 0;
    g_tr_count = 0;
    g_tr_cur_len = 0;
    g_tr_scroll = 0;
    g_tr_cur[0] = 0;
    for (UINT32 i = 0; i < LLMK_TR_LINES; i++) g_tr_lines[i][0] = 0;
    g_tui_dirty = 1;
}

static void llmk_tr_push_line(const char *line) {
    if (!line) line = "";

    UINT32 idx = g_tr_write % LLMK_TR_LINES;
    g_tr_write = (g_tr_write + 1) % LLMK_TR_LINES;
    if (g_tr_count < LLMK_TR_LINES) g_tr_count++;

    int p = 0;
    for (const char *s = line; *s && p + 1 < (int)LLMK_TR_COLS; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        g_tr_lines[idx][p++] = (char)c;
    }
    g_tr_lines[idx][p] = 0;
    g_tui_dirty = 1;
}

static void llmk_tr_flush_cur_line(void) {
    g_tr_cur[g_tr_cur_len] = 0;
    llmk_tr_push_line(g_tr_cur);
    g_tr_cur_len = 0;
    g_tr_cur[0] = 0;
}

static void llmk_tr_note(const char *msg) {
    llmk_tr_push_line(msg);
}

static void llmk_tr_push_prefixed(const char *prefix, const char *msg) {
    char line[LLMK_TR_COLS];
    int p = 0;
    line[0] = 0;
    if (!prefix) prefix = "";
    if (!msg) msg = "";
    for (const char *s = prefix; *s && p + 1 < (int)sizeof(line); s++) line[p++] = *s;
    for (const char *s = msg; *s && p + 1 < (int)sizeof(line); s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        line[p++] = (char)c;
    }
    line[p] = 0;
    llmk_tr_push_line(line);
}

static const char *llmk_tr_get_line_by_age(UINT32 age_from_newest) {
    // age_from_newest=0 -> newest line
    if (g_tr_count == 0) return "";
    if (age_from_newest >= g_tr_count) age_from_newest = g_tr_count - 1;

    UINT32 newest = (g_tr_write + LLMK_TR_LINES - 1) % LLMK_TR_LINES;
    UINT32 idx = (newest + LLMK_TR_LINES - (age_from_newest % LLMK_TR_LINES)) % LLMK_TR_LINES;
    return g_tr_lines[idx];
}

static void llmk_tr_append_ascii_bytes(const unsigned char *bytes, int len) {
    if (!bytes || len <= 0) return;

    for (int i = 0; i < len; i++) {
        unsigned char c = bytes[i];
        if (c == 0) continue;
        if (c == '\r' || c == '\n') {
            llmk_tr_flush_cur_line();
            continue;
        }
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        if (g_tr_cur_len + 1 >= (int)sizeof(g_tr_cur)) {
            llmk_tr_flush_cur_line();
        }
        g_tr_cur[g_tr_cur_len++] = (char)c;
    }
    g_tr_cur[g_tr_cur_len] = 0;
}

static void llmk_tui_on_prompt_best_effort(const char *prompt) {
    if (!g_tui_enabled || !g_gop_fb32) return;
    if (!prompt || prompt[0] == 0) {
        llmk_tui_set_event("(empty)");
        llmk_tui_redraw_best_effort();
        return;
    }

    if (prompt[0] == '/') {
        char cmd[64];
        int n = 0;
        while (prompt[n] && !llmk_ascii_is_space(prompt[n]) && prompt[n] != ';' && n + 1 < (int)sizeof(cmd)) {
            cmd[n] = prompt[n];
            n++;
        }
        cmd[n] = 0;
        llmk_tui_set_event(cmd[0] ? cmd : "/");
    } else {
        llmk_tui_set_event("prompt");
    }

    llmk_tui_redraw_best_effort();
}

static void llmk_ascii_append_cap(char *dst, int dst_cap, const char *src) {
    if (!dst || dst_cap <= 0) return;
    if (!src) return;
    int n = 0;
    while (n < dst_cap && dst[n]) n++;
    if (n >= dst_cap - 1) return;
    int i = 0;
    while (src[i] && n + 1 < dst_cap) {
        dst[n++] = src[i++];
    }
    dst[n] = 0;
}

static void llmk_tui_append_u32(char *dst, int cap, UINT32 v) {
    if (!dst || cap <= 0) return;
    int pos = 0;
    while (pos < cap && dst[pos]) pos++;
    pos = llmk_ascii_append_u32(dst, cap, pos, v);
    if (pos < cap) dst[pos] = 0;
    else dst[cap - 1] = 0;
}

static void llmk_gop_draw_char5x7(UINT32 x, UINT32 y, UINT32 scale,
                                 UINT8 fg_r, UINT8 fg_g, UINT8 fg_b,
                                 UINT8 bg_r, UINT8 bg_g, UINT8 bg_b,
                                 char c) {
    const UINT8 *rows = llmk_font5x7_get(c);
    if (!rows) return;

    // Background cell (5x7 + 1 column gap)
    llmk_gop_fill_rect_solid(x, y, (5U + 1U) * scale, 7U * scale, bg_r, bg_g, bg_b);
    for (UINT32 yy = 0; yy < 7; yy++) {
        UINT8 bits = rows[yy] & 0x1FU;
        for (UINT32 xx = 0; xx < 5; xx++) {
            UINT8 on = (UINT8)((bits >> (4 - xx)) & 1U);
            if (on) {
                llmk_gop_fill_rect_solid(x + xx * scale, y + yy * scale, scale, scale, fg_r, fg_g, fg_b);
            }
        }
    }
}

static void llmk_gop_draw_text5x7(UINT32 x, UINT32 y, UINT32 scale,
                                 UINT8 fg_r, UINT8 fg_g, UINT8 fg_b,
                                 UINT8 bg_r, UINT8 bg_g, UINT8 bg_b,
                                 const char *text) {
    if (!text) return;
    UINT32 cx = x;
    for (const char *p = text; *p; p++) {
        llmk_gop_draw_char5x7(cx, y, scale, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, *p);
        cx += (5U + 1U) * scale;
    }
}

static void llmk_ui_draw_text_clipped(UINT32 x, UINT32 y, UINT32 scale,
                                     UINT8 fg_r, UINT8 fg_g, UINT8 fg_b,
                                     UINT8 bg_r, UINT8 bg_g, UINT8 bg_b,
                                     const char *text, int max_chars) {
    if (!text || max_chars <= 0) return;
    char tmp[256];
    int p = 0;
    for (const char *s = text; *s && p + 1 < (int)sizeof(tmp) && p < max_chars; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) c = '?';
        tmp[p++] = (char)c;
    }
    tmp[p] = 0;
    llmk_gop_draw_text5x7(x, y, scale, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, tmp);
}

static void llmk_char16_to_ascii_cap(char *dst, int cap, const CHAR16 *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int p = 0;
    for (int i = 0; src[i] && p + 1 < cap; i++) {
        UINT16 ch = (UINT16)src[i];
        char c = (ch < 0x80) ? (char)ch : '?';
        if ((unsigned char)c < 0x20) c = ' ';
        dst[p++] = c;
    }
    dst[p] = 0;
}

static void llmk_fb_clear_preview(void) {
    g_fb_preview_count = 0;
    for (int i = 0; i < LLMK_FB_PREVIEW_LINES; i++) g_fb_preview[i][0] = 0;
}

static int llmk_read_file_prefix_best_effort(const CHAR16 *path, UINTN max_bytes, void **out_buf, UINTN *out_len) {
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    if (!path || !out_buf || !out_len) return 0;
    if (!g_root) return 0;
    if (max_bytes == 0) return 0;
    if (max_bytes > (256U * 1024U)) max_bytes = (256U * 1024U);

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, path);
    if (EFI_ERROR(st) || !f) return 0;

    void *buf = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, max_bytes + 1, &buf);
    if (EFI_ERROR(st) || !buf) {
        uefi_call_wrapper(f->Close, 1, f);
        return 0;
    }

    UINTN want = max_bytes;
    EFI_STATUS st2 = uefi_call_wrapper(f->Read, 3, f, &want, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st2)) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }
    ((UINT8 *)buf)[want] = 0;
    *out_buf = buf;
    *out_len = want;
    return 1;
}

static void llmk_fb_build_preview_from_bytes(const void *raw, UINTN raw_len) {
    llmk_fb_clear_preview();
    if (!raw || raw_len == 0) return;

    const UINT8 *b = (const UINT8 *)raw;
    UINTN n = raw_len;
    if (n > (UINTN)(LLMK_FB_PREVIEW_LINES * LLMK_FB_PREVIEW_COLS * 2)) {
        n = (UINTN)(LLMK_FB_PREVIEW_LINES * LLMK_FB_PREVIEW_COLS * 2);
    }

    int line = 0;
    int col = 0;

    // UTF-16 BOM detection (LE/BE). Down-convert to ASCII-ish.
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        int is_le = (b[0] == 0xFF);
        UINTN chars = (n - 2) / 2;
        for (UINTN i = 0; i < chars; i++) {
            UINT8 lo = b[2 + i * 2 + 0];
            UINT8 hi = b[2 + i * 2 + 1];
            UINT16 ch = is_le ? (UINT16)(lo | ((UINT16)hi << 8)) : (UINT16)(hi | ((UINT16)lo << 8));
            if (ch == 0) break;
            char c = (ch < 0x80) ? (char)ch : '?';
            if (c == '\r') continue;
            if (c == '\n') { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; continue; }
            if (c == '\t') c = ' ';
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = '?';
            if (col + 1 >= LLMK_FB_PREVIEW_COLS) { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; }
            g_fb_preview[line][col++] = c;
        }
    } else {
        for (UINTN i = 0; i < n; i++) {
            UINT8 ch = b[i];
            if (ch == 0) break;
            char c = (char)ch;
            if (c == '\r') continue;
            if (c == '\n') { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; continue; }
            if (c == '\t') c = ' ';
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = '?';
            if (col + 1 >= LLMK_FB_PREVIEW_COLS) { g_fb_preview[line][col] = 0; line++; col = 0; if (line >= LLMK_FB_PREVIEW_LINES) break; }
            g_fb_preview[line][col++] = c;
        }
    }

    if (line < LLMK_FB_PREVIEW_LINES) {
        g_fb_preview[line][col] = 0;
        g_fb_preview_count = line + 1;
    } else {
        g_fb_preview_count = LLMK_FB_PREVIEW_LINES;
    }
}

static int llmk_fb_refresh_best_effort(void) {
    if (!g_root) return 0;

    for (int i = 0; i < LLMK_FB_MAX_ENTRIES; i++) {
        g_fb_entries[i].name16[0] = 0;
        g_fb_entries[i].name8[0] = 0;
        g_fb_entries[i].is_dir = 0;
        g_fb_entries[i].size = 0;
    }
    g_fb_count = 0;
    llmk_fb_clear_preview();

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!g_fb_path16[0] || llmk_char16_streq(g_fb_path16, L".") || llmk_char16_streq(g_fb_path16, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, g_fb_path16, &dir, NULL, 0, L"fb_dir");
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

    while (g_fb_count < LLMK_FB_MAX_ENTRIES) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (info->FileName && (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L".."))) {
            continue;
        }

        LlmkFbEntry *e = &g_fb_entries[g_fb_count++];
        e->is_dir = (info->Attribute & EFI_FILE_DIRECTORY) ? 1 : 0;
        e->size = info->FileSize;
        if (info->FileName) {
            StrnCpy(e->name16, info->FileName, (sizeof(e->name16) / sizeof(e->name16[0])) - 1);
            e->name16[(sizeof(e->name16) / sizeof(e->name16[0])) - 1] = 0;
            llmk_char16_to_ascii_cap(e->name8, (int)sizeof(e->name8), e->name16);
        } else {
            StrCpy(e->name16, L"(null)");
            llmk_ascii_copy_cap(e->name8, (int)sizeof(e->name8), "(null)");
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);

    if (g_fb_sel < 0) g_fb_sel = 0;
    if (g_fb_sel >= g_fb_count) g_fb_sel = (g_fb_count > 0) ? (g_fb_count - 1) : 0;
    return 1;
}

static void llmk_fb_preview_selected_best_effort(void) {
    llmk_fb_clear_preview();
    if (g_fb_count <= 0) return;
    if (g_fb_sel < 0 || g_fb_sel >= g_fb_count) return;
    if (g_fb_entries[g_fb_sel].is_dir) return;

    CHAR16 path[192];
    path[0] = 0;
    if (!g_fb_path16[0] || llmk_char16_streq(g_fb_path16, L"\\") || llmk_char16_streq(g_fb_path16, L".")) {
        // Root
        StrCpy(path, g_fb_entries[g_fb_sel].name16);
    } else {
        StrCpy(path, g_fb_path16);
        UINTN n = StrLen(path);
        if (n > 0 && path[n - 1] != L'\\') StrCat(path, L"\\");
        StrCat(path, g_fb_entries[g_fb_sel].name16);
    }

    void *buf = NULL;
    UINTN len = 0;
    if (!llmk_read_file_prefix_best_effort(path, 4096, &buf, &len)) return;
    llmk_fb_build_preview_from_bytes(buf, len);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_tui_redraw_best_effort(void) {
    if (!g_tui_enabled) return;
    if (!g_gop_fb32) return;

    const UINT32 scale = 2;
    const UINT32 char_w = (5U + 1U) * scale;
    const UINT32 line_h = 8U * scale;
    const UINT32 pad = 6;

    if (g_ui_mode == 0) {
        // Legacy status-only panel
        const UINT32 x = 8;
        const UINT32 y = 8;
        const UINT32 panel_w = 360;
        const UINT32 panel_h = (line_h * 6U) + pad * 2U;

        llmk_gop_fill_rect_solid(x, y, panel_w, panel_h, 0, 0, 32);
        llmk_gop_fill_rect_solid(x, y, panel_w, 1, 80, 80, 120);
        llmk_gop_fill_rect_solid(x, y + panel_h - 1, panel_w, 1, 80, 80, 120);

        char line1[96];
        char line2[96];
        char line3[96];
        char line4[96];
        char line5[96];
        char line6[96];
        line1[0] = line2[0] = line3[0] = line4[0] = line5[0] = line6[0] = 0;

        llmk_ascii_copy_cap(line1, (int)sizeof(line1), "LLMK UI [STATUS]");

        llmk_ascii_copy_cap(line2, (int)sizeof(line2), "KV_POS=");
        if (g_llmk_kv_pos > 0) llmk_tui_append_u32(line2, (int)sizeof(line2), (UINT32)g_llmk_kv_pos);
        else llmk_ascii_append_cap(line2, (int)sizeof(line2), "0");

        llmk_ascii_copy_cap(line3, (int)sizeof(line3), "OO_AUTO=");
        llmk_ascii_append_cap(line3, (int)sizeof(line3), g_oo_auto_active ? "1" : "0");
        llmk_ascii_append_cap(line3, (int)sizeof(line3), " OO_EXEC=");
        llmk_ascii_append_cap(line3, (int)sizeof(line3), g_oo_exec_active ? "1" : "0");

        llmk_ascii_copy_cap(line4, (int)sizeof(line4), "GEN=");
        llmk_ascii_append_cap(line4, (int)sizeof(line4), g_tui_gen_active ? "1" : "0");
        llmk_ascii_append_cap(line4, (int)sizeof(line4), " TOK=");
        llmk_tui_append_u32(line4, (int)sizeof(line4), (UINT32)g_tui_gen_tokens);

        llmk_ascii_copy_cap(line5, (int)sizeof(line5), "TICK=");
        llmk_tui_append_u32(line5, (int)sizeof(line5), (UINT32)g_tui_last_tick);
        llmk_ascii_append_cap(line5, (int)sizeof(line5), " ID=");
        llmk_tui_append_u32(line5, (int)sizeof(line5), (UINT32)g_tui_last_id);

        llmk_ascii_copy_cap(line6, (int)sizeof(line6), "EVT=");
        if (g_tui_last_event[0]) llmk_ascii_append_cap(line6, (int)sizeof(line6), g_tui_last_event);
        else llmk_ascii_append_cap(line6, (int)sizeof(line6), "(none)");

        UINT32 ty = y + pad;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 255, 255, 255, 0, 0, 32, line1);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 220, 255, 0, 0, 32, line2);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 255, 220, 0, 0, 32, line3);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 255, 220, 220, 0, 0, 32, line4);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 220, 220, 0, 0, 32, line5);
        ty += line_h;
        llmk_gop_draw_text5x7(x + pad, ty, scale, 220, 220, 220, 0, 0, 32, line6);

        llmk_gop_force_update();
        g_tui_dirty = 0;
        return;
    }

    // Split/log/files UI
    const UINT32 x0 = 8;
    const UINT32 y0 = 8;
    UINT32 w0 = (g_gop_w > 16) ? (g_gop_w - 16) : g_gop_w;
    UINT32 h0 = (g_gop_h > 16) ? (g_gop_h - 16) : g_gop_h;
    if (w0 < 320) w0 = 320;
    if (h0 < 200) h0 = 200;

    // Background (keep it moderate; redraws are throttled)
    llmk_gop_fill_rect_solid(x0, y0, w0, h0, 0, 0, 0);

    // Header
    const UINT32 header_h = line_h * 2U + pad * 2U;
    llmk_gop_fill_rect_solid(x0, y0, w0, header_h, 0, 0, 32);
    llmk_gop_fill_rect_solid(x0, y0 + header_h, w0, 1, 80, 80, 120);

    char hdr1[128];
    char hdr2[128];
    hdr1[0] = hdr2[0] = 0;

    llmk_ascii_copy_cap(hdr1, (int)sizeof(hdr1), "LLMK UI ");
    if (g_ui_mode == 1) llmk_ascii_append_cap(hdr1, (int)sizeof(hdr1), "[LOG]");
    else if (g_ui_mode == 2) llmk_ascii_append_cap(hdr1, (int)sizeof(hdr1), "[SPLIT]");
    else llmk_ascii_append_cap(hdr1, (int)sizeof(hdr1), "[FILES]");

    llmk_ascii_copy_cap(hdr2, (int)sizeof(hdr2), "KV=");
    llmk_tui_append_u32(hdr2, (int)sizeof(hdr2), (UINT32)g_llmk_kv_pos);
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), " GEN=");
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), g_tui_gen_active ? "1" : "0");
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), " TOK=");
    llmk_tui_append_u32(hdr2, (int)sizeof(hdr2), (UINT32)g_tui_gen_tokens);
    llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), " EVT=");
    if (g_tui_last_event[0]) llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), g_tui_last_event);
    else llmk_ascii_append_cap(hdr2, (int)sizeof(hdr2), "-");

    UINT32 ty = y0 + pad;
    llmk_ui_draw_text_clipped(x0 + pad, ty, scale, 255, 255, 255, 0, 0, 32, hdr1, (int)((w0 - pad * 2U) / char_w));
    ty += line_h;
    llmk_ui_draw_text_clipped(x0 + pad, ty, scale, 220, 220, 220, 0, 0, 32, hdr2, (int)((w0 - pad * 2U) / char_w));

    // Body panes
    UINT32 body_y = y0 + header_h + 1;
    UINT32 body_h = (y0 + h0 > body_y) ? ((y0 + h0) - body_y) : 0;
    if (body_h < line_h * 2U) {
        llmk_gop_force_update();
        g_tui_dirty = 0;
        return;
    }

    UINT32 log_x = x0;
    UINT32 log_y = body_y;
    UINT32 log_w = w0;
    UINT32 log_h = body_h;

    UINT32 files_x = 0, files_y = 0, files_w = 0, files_h = 0;
    int show_files = (g_ui_mode >= 2);
    if (show_files) {
        UINT32 split = (w0 * 2U) / 3U;
        if (split < 240) split = 240;
        if (split + 240 > w0) split = (w0 > 240) ? (w0 - 240) : w0;
        log_w = split;
        files_x = x0 + log_w + 1;
        files_y = body_y;
        files_w = (x0 + w0 > files_x) ? ((x0 + w0) - files_x) : 0;
        files_h = body_h;
        llmk_gop_fill_rect_solid(x0 + log_w, body_y, 1, body_h, 80, 80, 120);
    }

    // Log pane background
    llmk_gop_fill_rect_solid(log_x, log_y, log_w, log_h, 0, 0, 24);

    // Render newest lines at bottom-ish (simple top-down with scroll)
    int max_chars = (int)((log_w - pad * 2U) / char_w);
    int max_lines = (int)((log_h - pad * 2U) / line_h);
    if (max_lines < 1) max_lines = 1;

    UINT32 start_age = 0;
    if (g_tr_scroll < 0) g_tr_scroll = 0;
    if ((UINT32)g_tr_scroll > g_tr_count) g_tr_scroll = (int)g_tr_count;
    start_age = (UINT32)g_tr_scroll;

    // Draw from newest backwards.
    UINT32 ly = log_y + pad;
    for (int i = 0; i < max_lines; i++) {
        const char *line = llmk_tr_get_line_by_age(start_age + (UINT32)(max_lines - 1 - i));
        llmk_ui_draw_text_clipped(log_x + pad, ly, scale, 220, 220, 220, 0, 0, 24, line, max_chars);
        ly += line_h;
    }

    if (show_files && files_w > 0) {
        llmk_gop_fill_rect_solid(files_x, files_y, files_w, files_h, 0, 16, 0);
        int f_chars = (int)((files_w - pad * 2U) / char_w);
        int f_lines = (int)((files_h - pad * 2U) / line_h);
        if (f_lines < 1) f_lines = 1;

        // Path header
        char pbuf[128];
        llmk_ascii_copy_cap(pbuf, (int)sizeof(pbuf), "PATH=");
        llmk_ascii_append_cap(pbuf, (int)sizeof(pbuf), g_fb_path8[0] ? g_fb_path8 : "\\");
        llmk_ui_draw_text_clipped(files_x + pad, files_y + pad, scale, 220, 255, 220, 0, 16, 0, pbuf, f_chars);

        // List + preview
        int list_lines = f_lines - 1;
        if (list_lines < 1) list_lines = 1;
        int preview_lines = LLMK_FB_PREVIEW_LINES;
        if (list_lines > preview_lines + 2) {
            list_lines = list_lines - preview_lines - 1;
        }
        if (list_lines < 1) list_lines = 1;

        UINT32 fy = files_y + pad + line_h;
        for (int i = 0; i < list_lines; i++) {
            int idx = i;
            if (idx >= g_fb_count) break;
            char name_line[96];
            name_line[0] = 0;
            if (idx == g_fb_sel) llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "> ");
            else llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "  ");
            if (g_fb_entries[idx].is_dir) llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "[D] ");
            else llmk_ascii_append_cap(name_line, (int)sizeof(name_line), "    ");
            llmk_ascii_append_cap(name_line, (int)sizeof(name_line), g_fb_entries[idx].name8);
            llmk_ui_draw_text_clipped(files_x + pad, fy, scale,
                                      (idx == g_fb_sel) ? 255 : 200,
                                      (idx == g_fb_sel) ? 255 : 220,
                                      (idx == g_fb_sel) ? 255 : 200,
                                      0, 16, 0,
                                      name_line,
                                      f_chars);
            fy += line_h;
        }

        // Preview separator
        if (g_fb_preview_count > 0) {
            llmk_gop_fill_rect_solid(files_x, fy, files_w, 1, 80, 120, 80);
            fy += 2;
            for (int i = 0; i < g_fb_preview_count && i < LLMK_FB_PREVIEW_LINES; i++) {
                llmk_ui_draw_text_clipped(files_x + pad, fy, scale, 220, 220, 220, 0, 16, 0, g_fb_preview[i], f_chars);
                fy += line_h;
            }
        }
    }

    llmk_gop_force_update();
    g_tui_dirty = 0;
}

static void llmk_oo_on_step_gop(int id, int tick, int energy) {
    g_tui_last_id = id;
    g_tui_last_tick = tick;
    g_tui_last_energy = energy;
    if (!g_gop_fb32 || !g_gop_w || !g_gop_h) return;
    UINT32 x = (UINT32)((tick * 13 + id * 31) % (int)g_gop_w);
    UINT32 y = (UINT32)((tick * 7 + id * 17) % (int)g_gop_h);
    llmk_gop_put_pixel(x, y, 0, 255, 0);

    // Best-effort TUI refresh at a low cadence to avoid heavy overhead.
    if (g_tui_enabled && ((tick & 7) == 0 || g_tui_dirty)) {
        llmk_tui_redraw_best_effort();
    } else {
        llmk_gop_force_update();
    }
}

static void llmk_gop_put_pixel(UINT32 x, UINT32 y, UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    if (x >= g_gop_w || y >= g_gop_h) return;
    UINTN idx = (UINTN)y * (UINTN)g_gop_ppsl + (UINTN)x;

    int ok = 0;
    UINT32 px = llmk_gop_pack_rgb(r, g, b, &ok);
    if (!ok) return;
    g_gop_fb32[idx] = px;
}

static void llmk_gop_get_pixel(UINT32 x, UINT32 y, UINT8 *out_r, UINT8 *out_g, UINT8 *out_b) {
    if (!out_r || !out_g || !out_b) return;
    *out_r = *out_g = *out_b = 0;
    if (!g_gop_fb32) return;
    if (x >= g_gop_w || y >= g_gop_h) return;
    UINTN idx = (UINTN)y * (UINTN)g_gop_ppsl + (UINTN)x;
    UINT32 px = g_gop_fb32[idx];

    if (g_gop_pf == PixelBlueGreenRedReserved8BitPerColor) {
        *out_b = (UINT8)(px & 0xFFU);
        *out_g = (UINT8)((px >> 8) & 0xFFU);
        *out_r = (UINT8)((px >> 16) & 0xFFU);
    } else if (g_gop_pf == PixelRedGreenBlueReserved8BitPerColor) {
        *out_r = (UINT8)(px & 0xFFU);
        *out_g = (UINT8)((px >> 8) & 0xFFU);
        *out_b = (UINT8)((px >> 16) & 0xFFU);
    } else if (g_gop_pf == PixelBitMask) {
        UINT32 rm = g_gop_mask.RedMask;
        UINT32 gm = g_gop_mask.GreenMask;
        UINT32 bm = g_gop_mask.BlueMask;
        UINT32 rs = llmk_u32_ctz(rm);
        UINT32 gs = llmk_u32_ctz(gm);
        UINT32 bs = llmk_u32_ctz(bm);
        UINT32 rbits = llmk_u32_popcount(rm);
        UINT32 gbits = llmk_u32_popcount(gm);
        UINT32 bbits = llmk_u32_popcount(bm);
        UINT32 rmax = (rbits >= 32) ? 0xFFFFFFFFU : ((1U << rbits) - 1U);
        UINT32 gmax = (gbits >= 32) ? 0xFFFFFFFFU : ((1U << gbits) - 1U);
        UINT32 bmax = (bbits >= 32) ? 0xFFFFFFFFU : ((1U << bbits) - 1U);
        UINT32 rv = (rm == 0) ? 0 : ((px & rm) >> rs);
        UINT32 gv = (gm == 0) ? 0 : ((px & gm) >> gs);
        UINT32 bv = (bm == 0) ? 0 : ((px & bm) >> bs);
        *out_r = (rmax == 0) ? 0 : (UINT8)((rv * 255U) / rmax);
        *out_g = (gmax == 0) ? 0 : (UINT8)((gv * 255U) / gmax);
        *out_b = (bmax == 0) ? 0 : (UINT8)((bv * 255U) / bmax);
    }
}

static void llmk_gop_clear(UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    for (UINT32 y = 0; y < g_gop_h; y++) {
        for (UINT32 x = 0; x < g_gop_w; x++) {
            llmk_gop_put_pixel(x, y, r, g, b);
        }
    }
}

static void llmk_gop_fill_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT8 r, UINT8 g, UINT8 b) {
    if (!g_gop_fb32) return;
    if (w == 0 || h == 0) return;
    UINT32 x2 = x + w;
    UINT32 y2 = y + h;
    if (x >= g_gop_w || y >= g_gop_h) return;
    if (x2 > g_gop_w) x2 = g_gop_w;
    if (y2 > g_gop_h) y2 = g_gop_h;
    for (UINT32 yy = y; yy < y2; yy++) {
        for (UINT32 xx = x; xx < x2; xx++) {
            llmk_gop_put_pixel(xx, yy, r, g, b);
        }
    }
}

static const char* llmk_parse_word(const char *s, char *out, int out_cap) {
    if (!s || !out || out_cap <= 0) return s;
    while (*s && llmk_ascii_is_space(*s)) s++;
    int n = 0;
    while (*s && !llmk_ascii_is_space(*s) && *s != ';') {
        if (n + 1 < out_cap) out[n++] = *s;
        s++;
    }
    out[n] = 0;
    return s;
}

static const char* llmk_parse_i32(const char *s, int *out) {
    if (!s || !out) return s;
    while (*s && llmk_ascii_is_space(*s)) s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    int v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10 + (int)(*s - '0');
        s++;
    }
    if (!any) {
        *out = 0;
        return NULL;
    }
    *out = v * sign;
    return s;
}

static const char* llmk_skip_to_stmt_end(const char *s) {
    if (!s) return s;
    while (*s && *s != ';') s++;
    if (*s == ';') s++;
    return s;
}

static int llmk_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

// Last DSL parse error (ASCII). Used to provide useful feedback for /render and /draw.
static char g_last_dsl_error[96];

static void llmk_set_dsl_error(const char *msg, const char *arg) {
    // Store a short ASCII error message (best-effort).
    int p = 0;
    for (const char *s = msg; s && *s && p + 1 < (int)sizeof(g_last_dsl_error); s++) g_last_dsl_error[p++] = *s;
    if (arg) {
        if (p + 2 < (int)sizeof(g_last_dsl_error)) { g_last_dsl_error[p++] = ':'; g_last_dsl_error[p++] = ' '; }
        for (const char *s = arg; *s && p + 1 < (int)sizeof(g_last_dsl_error); s++) {
            char c = *s;
            if (c < 0x20 || c > 0x7E) c = '?';
            g_last_dsl_error[p++] = c;
        }
    }
    g_last_dsl_error[p] = 0;
}

static const char* llmk_find_first_op(const char *s) {
    if (!s) return NULL;
    // Try to find a plausible start of DSL inside a larger prose blob.
    for (const char *p = s; *p; p++) {
        if ((p[0] == 'c' && p[1] == 'l' && p[2] == 'e' && p[3] == 'a' && p[4] == 'r') ||
            (p[0] == 'r' && p[1] == 'e' && p[2] == 'c' && p[3] == 't') ||
            (p[0] == 'p' && p[1] == 'i' && p[2] == 'x' && p[3] == 'e' && p[4] == 'l')) {
            return p;
        }
    }
    return NULL;
}

static void llmk_apply_simple_autocorrect(char *buf) {
    // Best-effort fix for common typo seen in logs: "react" -> "rect".
    if (!buf) return;
    for (char *p = buf; p[0] && p[1] && p[2] && p[3] && p[4]; p++) {
        if (p[0] == 'r' && p[1] == 'e' && p[2] == 'a' && p[3] == 'c' && p[4] == 't') {
            p[2] = 'c';
            // p[3],p[4] already 'c','t' from "react"; make it "rect" by shifting left one.
            p[3] = 't';
            p[4] = ' ';
        }
    }
}

static void llmk_draw_fallback_center_square(int white) {
    if (!g_gop_fb32) return;
    llmk_gop_clear(0, 0, 0);
    UINT32 size = g_gop_w < g_gop_h ? g_gop_w : g_gop_h;
    size = size / 4;
    if (size < 32) size = 32;
    UINT32 x = (g_gop_w > size) ? ((g_gop_w - size) / 2) : 0;
    UINT32 y = (g_gop_h > size) ? ((g_gop_h - size) / 2) : 0;
    if (white) llmk_gop_fill_rect(x, y, size, size, 255, 255, 255);
    else llmk_gop_fill_rect(x, y, size, size, 255, 0, 0);
}

static int llmk_render_scene_dsl_ex(const char *dsl, int strict) {
    g_last_dsl_error[0] = 0;
    if (!dsl) { llmk_set_dsl_error("null dsl", NULL); return 0; }
    if (!g_gop_fb32) { llmk_set_dsl_error("no gop", NULL); return 0; }

    // If this is a prose blob, try to find the first DSL op.
    const char *s = dsl;
    const char *first = llmk_find_first_op(dsl);
    if (first) s = first;

    int any = 0;
    while (*s) {
        while (*s && (llmk_ascii_is_space(*s) || *s == ';')) s++;
        if (!*s) break;

        char op[16];
        const char *ns = llmk_parse_word(s, op, (int)sizeof(op));
        if (!ns) { llmk_set_dsl_error("parse op", NULL); return 0; }
        s = ns;

        if (llmk_streq(op, "clear")) {
            int r, g, b;
            s = llmk_parse_i32(s, &r); if (!s) { llmk_set_dsl_error("parse clear", NULL); return 0; }
            s = llmk_parse_i32(s, &g); if (!s) { llmk_set_dsl_error("parse clear", NULL); return 0; }
            s = llmk_parse_i32(s, &b); if (!s) { llmk_set_dsl_error("parse clear", NULL); return 0; }
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            llmk_gop_clear((UINT8)r, (UINT8)g, (UINT8)b);
            any = 1;
            s = llmk_skip_to_stmt_end(s);
        } else if (llmk_streq(op, "rect")) {
            int x, y, w, h, r, g, b;
            s = llmk_parse_i32(s, &x); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &y); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &w); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &h); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &r); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &g); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            s = llmk_parse_i32(s, &b); if (!s) { llmk_set_dsl_error("parse rect", NULL); return 0; }
            if (x < 0) x = 0; if (y < 0) y = 0;
            if (w < 0) w = 0; if (h < 0) h = 0;
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            llmk_gop_fill_rect((UINT32)x, (UINT32)y, (UINT32)w, (UINT32)h, (UINT8)r, (UINT8)g, (UINT8)b);
            any = 1;
            s = llmk_skip_to_stmt_end(s);
        } else if (llmk_streq(op, "pixel")) {
            int x, y, r, g, b;
            s = llmk_parse_i32(s, &x); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &y); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &r); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &g); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            s = llmk_parse_i32(s, &b); if (!s) { llmk_set_dsl_error("parse pixel", NULL); return 0; }
            if (x < 0) x = 0; if (y < 0) y = 0;
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            llmk_gop_put_pixel((UINT32)x, (UINT32)y, (UINT8)r, (UINT8)g, (UINT8)b);
            any = 1;
            s = llmk_skip_to_stmt_end(s);
        } else {
            if (strict) {
                llmk_set_dsl_error("unknown op", op);
                return 0;
            }
            // Non-strict: skip to ';' to avoid getting stuck.
            s = llmk_skip_to_stmt_end(s);
        }
    }
    if (!any && !g_last_dsl_error[0]) llmk_set_dsl_error("no ops", NULL);
    return any;
}

static int llmk_render_scene_dsl(const char *dsl) {
    return llmk_render_scene_dsl_ex(dsl, 0);
}

static EFI_STATUS llmk_open_binary_file(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    // Best-effort truncate by deleting existing file first.
    EFI_FILE_HANDLE existing = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &existing, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (!EFI_ERROR(st) && existing) {
        uefi_call_wrapper(existing->Delete, 1, existing);
        existing = NULL;
    }

    EFI_FILE_HANDLE f = NULL;
    st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                           EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st)) return st;
    uefi_call_wrapper(f->SetPosition, 2, f, 0);
    *out = f;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_file_write_bytes(EFI_FILE_HANDLE f, const void *buf, UINTN nb) {
    if (!f || (!buf && nb)) return EFI_INVALID_PARAMETER;
    if (nb == 0) return EFI_SUCCESS;
    return uefi_call_wrapper(f->Write, 3, f, &nb, (void *)buf);
}

static EFI_STATUS llmk_read_entire_file_best_effort(const CHAR16 *name, void **out_buf, UINTN *out_len) {
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    if (!g_root || !name || !out_buf || !out_len) return EFI_INVALID_PARAMETER;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, name, &f, NULL, 0, L"read_entire");
    if (EFI_ERROR(st) || !f) return st;

    // Get file size
    UINT64 file_size = 0;
    {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, NULL);
        if (s2 == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            s2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(s2) && info) {
                s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(s2)) file_size = info->FileSize;
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }

    if (file_size == 0) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_END_OF_FILE;
    }
    if (file_size > 1024 * 1024) {
        // Safety cap (1 MiB)
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_OUT_OF_RESOURCES;
    }

    void *buf = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)file_size, (void **)&buf);
    if (EFI_ERROR(st) || !buf) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN nb = (UINTN)file_size;
    st = uefi_call_wrapper(f->Read, 3, f, &nb, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || nb != (UINTN)file_size) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return EFI_LOAD_ERROR;
    }

    *out_buf = buf;
    *out_len = nb;
    return EFI_SUCCESS;
}

static void llmk_make_bak_name(const CHAR16 *src, CHAR16 *dst, int dst_cap) {
    if (!dst || dst_cap <= 0) return;
    dst[0] = 0;
    if (!src) return;

    int n = 0;
    while (src[n] && n + 1 < dst_cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;

    const CHAR16 *suffix = L".bak";
    int s = 0;
    while (suffix[s]) s++;
    if (n + s + 1 >= dst_cap) return;
    for (int i = 0; i < s; i++) dst[n + i] = suffix[i];
    dst[n + s] = 0;
}

static EFI_STATUS llmk_copy_file_best_effort(const CHAR16 *src, const CHAR16 *dst) {
    if (!src || !dst) return EFI_INVALID_PARAMETER;
    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(src, &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return st;
    }

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, dst);
    if (EFI_ERROR(st) || !f) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return st;
    }

    st = llmk_file_write_bytes(f, (const void *)buf, len);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_delete_file_best_effort(const CHAR16 *name) {
    if (!g_root || !name) return EFI_INVALID_PARAMETER;
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(st) || !f) return st;
    // Delete closes the handle.
    return uefi_call_wrapper(f->Delete, 1, f);
}

static EFI_STATUS llmk_open_binary_file_append(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st) || !f) return st;

    // Seek to end
    UINT64 file_size = 0;
    {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, NULL);
        if (s2 == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            s2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(s2) && info) {
                s2 = uefi_call_wrapper(f->GetInfo, 4, f, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(s2)) file_size = info->FileSize;
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }
    uefi_call_wrapper(f->SetPosition, 2, f, file_size);
    *out = f;
    return EFI_SUCCESS;
}

// ============================================================================
// OPERATING ORGANISM (OO) — v0 STATE + JOURNAL (best-effort)
// ============================================================================

#define LLMK_OO_STATE_MAGIC 0x54534F4Fu  // 'OOST' little-endian
#define LLMK_OO_STATE_VER   1u

enum {
    LLMK_OO_MODE_NORMAL   = 0,
    LLMK_OO_MODE_DEGRADED = 1,
    LLMK_OO_MODE_SAFE     = 2,
};

static UINT32 g_oo_last_mode = LLMK_OO_MODE_SAFE;
static int g_oo_last_mode_valid = 0;

// flags layout (packed counters; keep state struct fixed-size for v1)
//   bits  0..7   : consecutive recoveries (0-255)
//   bits  8..15  : consecutive stable boots (0-255)
//   bits 16..23  : last auto-apply action meta (low6=action_id, high2=apply_mode)
//   bits 24..31  : last auto-apply boot_count low8 (for next-boot metrics)
#define LLMK_OO_FLAGS_RC_MASK   0x000000FFu
#define LLMK_OO_FLAGS_SC_MASK   0x0000FF00u
#define LLMK_OO_FLAGS_SC_SHIFT  8u

#define LLMK_OO_FLAGS_LAST_ACTION_META_MASK   0x00FF0000u
#define LLMK_OO_FLAGS_LAST_ACTION_META_SHIFT  16u

#define LLMK_OO_FLAGS_LAST_APPLY_BOOT_MASK    0xFF000000u
#define LLMK_OO_FLAGS_LAST_APPLY_BOOT_SHIFT   24u

enum {
    LLMK_OO_ACTION_NONE       = 0,
    LLMK_OO_ACTION_REDUCE_CTX = 1,
    LLMK_OO_ACTION_REDUCE_SEQ = 2,
    LLMK_OO_ACTION_INCREASE_CTX = 3,
};

static UINT32 llmk_oo_get_rc(UINT32 flags) { return (flags & LLMK_OO_FLAGS_RC_MASK); }
static UINT32 llmk_oo_get_sc(UINT32 flags) { return (flags & LLMK_OO_FLAGS_SC_MASK) >> LLMK_OO_FLAGS_SC_SHIFT; }
static UINT32 llmk_oo_get_last_action_meta(UINT32 flags) {
    return (flags & LLMK_OO_FLAGS_LAST_ACTION_META_MASK) >> LLMK_OO_FLAGS_LAST_ACTION_META_SHIFT;
}
static UINT32 llmk_oo_get_last_apply_boot_low8(UINT32 flags) {
    return (flags & LLMK_OO_FLAGS_LAST_APPLY_BOOT_MASK) >> LLMK_OO_FLAGS_LAST_APPLY_BOOT_SHIFT;
}
static UINT32 llmk_oo_set_rc(UINT32 flags, UINT32 rc) {
    flags &= ~LLMK_OO_FLAGS_RC_MASK;
    flags |= (rc & 0xFFu);
    return flags;
}
static UINT32 llmk_oo_set_sc(UINT32 flags, UINT32 sc) {
    flags &= ~LLMK_OO_FLAGS_SC_MASK;
    flags |= ((sc & 0xFFu) << LLMK_OO_FLAGS_SC_SHIFT);
    return flags;
}
static UINT32 llmk_oo_set_last_action_meta(UINT32 flags, UINT32 meta) {
    flags &= ~LLMK_OO_FLAGS_LAST_ACTION_META_MASK;
    flags |= ((meta & 0xFFu) << LLMK_OO_FLAGS_LAST_ACTION_META_SHIFT);
    return flags;
}
static UINT32 llmk_oo_set_last_apply_boot_low8(UINT32 flags, UINT32 b) {
    flags &= ~LLMK_OO_FLAGS_LAST_APPLY_BOOT_MASK;
    flags |= ((b & 0xFFu) << LLMK_OO_FLAGS_LAST_APPLY_BOOT_SHIFT);
    return flags;
}

static const char *llmk_oo_action_name(UINT32 action_id) {
    switch (action_id) {
        case LLMK_OO_ACTION_REDUCE_CTX: return "reduce_ctx";
        case LLMK_OO_ACTION_REDUCE_SEQ: return "reduce_seq";
        case LLMK_OO_ACTION_INCREASE_CTX: return "increase_ctx";
        default: return "none";
    }
}

static int llmk_oo_action_is_reduction(UINT32 action_id) {
    return (action_id == LLMK_OO_ACTION_REDUCE_CTX || action_id == LLMK_OO_ACTION_REDUCE_SEQ) ? 1 : 0;
}

static int llmk_oo_action_is_increase(UINT32 action_id) {
    return (action_id == LLMK_OO_ACTION_INCREASE_CTX) ? 1 : 0;
}

// Forward decl (defined later in file).
static char *my_strstr(const char* haystack, const char* needle);

typedef struct {
    UINT32 magic;
    UINT32 version;
    UINT32 checksum; // FNV-1a over the struct with checksum=0
    UINT32 size;
    UINT64 boot_count;
    UINT32 mode;
    UINT32 flags;
} LlmkOoState;

static UINT32 llmk_fnv1a32(const void *data, UINTN len) {
    const UINT8 *p = (const UINT8 *)data;
    UINT32 h = 2166136261u;
    for (UINTN i = 0; i < len; i++) {
        h ^= (UINT32)p[i];
        h *= 16777619u;
    }
    return h;
}

static UINT32 llmk_oo_state_checksum(const LlmkOoState *s) {
    if (!s) return 0;
    LlmkOoState tmp = *s;
    tmp.checksum = 0;
    return llmk_fnv1a32(&tmp, (UINTN)sizeof(tmp));
}

static int llmk_oo_load_state_from_file_best_effort(const CHAR16 *name, LlmkOoState *out) {
    if (!out) return 0;
    out->magic = LLMK_OO_STATE_MAGIC;
    out->version = LLMK_OO_STATE_VER;
    out->checksum = 0;
    out->size = (UINT32)sizeof(LlmkOoState);
    out->boot_count = 0;
    out->mode = LLMK_OO_MODE_SAFE;
    out->flags = 0;
    if (!g_root) return 0;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(name, &buf, &len);
    if (EFI_ERROR(st) || !buf || len < (UINTN)sizeof(LlmkOoState)) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return 0;
    }

    LlmkOoState s;
    // Copy as raw bytes (avoid alignment assumptions).
    UINT8 *dst = (UINT8 *)&s;
    UINT8 *src = (UINT8 *)buf;
    for (UINTN i = 0; i < (UINTN)sizeof(LlmkOoState); i++) dst[i] = src[i];
    uefi_call_wrapper(BS->FreePool, 1, buf);

    if (s.magic != LLMK_OO_STATE_MAGIC) return 0;
    if (s.version != LLMK_OO_STATE_VER) return 0;
    if (s.size != (UINT32)sizeof(LlmkOoState)) return 0;
    UINT32 want = llmk_oo_state_checksum(&s);
    if (want == 0 || want != s.checksum) return 0;

    *out = s;
    return 1;
}

static int llmk_oo_load_state_best_effort(LlmkOoState *out) {
    return llmk_oo_load_state_from_file_best_effort(L"OOSTATE.BIN", out);
}

static int llmk_oo_load_recovery_best_effort(LlmkOoState *out) {
    return llmk_oo_load_state_from_file_best_effort(L"OORECOV.BIN", out);
}

static const CHAR16 *llmk_oo_mode_name(UINT32 mode) {
    switch (mode) {
        case LLMK_OO_MODE_NORMAL: return L"NORMAL";
        case LLMK_OO_MODE_DEGRADED: return L"DEGRADED";
        case LLMK_OO_MODE_SAFE: return L"SAFE";
        default: return L"UNKNOWN";
    }
}

static void llmk_ascii_append_char(char *buf, int cap, int *io_p, char c) {
    if (!buf || cap <= 0 || !io_p) return;
    int p = *io_p;
    if (p < 0) p = 0;
    if (p + 1 >= cap) return;
    buf[p++] = c;
    buf[p] = 0;
    *io_p = p;
}

static void llmk_ascii_append_str(char *buf, int cap, int *io_p, const char *s) {
    if (!buf || cap <= 0 || !io_p || !s) return;
    for (int i = 0; s[i]; i++) {
        llmk_ascii_append_char(buf, cap, io_p, s[i]);
    }
}

static void llmk_ascii_append_u64(char *buf, int cap, int *io_p, UINT64 v) {
    if (!buf || cap <= 0 || !io_p) return;
    char tmp[32];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    // reverse
    for (int i = n - 1; i >= 0; i--) {
        llmk_ascii_append_char(buf, cap, io_p, tmp[i]);
    }
}

static EFI_STATUS llmk_oo_write_state_best_effort(const LlmkOoState *s) {
    if (!s || !g_root) return EFI_NOT_READY;
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, L"OOSTATE.BIN");
    if (EFI_ERROR(st) || !f) return st;

    UINTN nb = (UINTN)sizeof(LlmkOoState);
    st = uefi_call_wrapper(f->Write, 3, f, &nb, (void *)s);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || nb != (UINTN)sizeof(LlmkOoState)) return EFI_LOAD_ERROR;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_oo_write_recovery_best_effort(const LlmkOoState *s) {
    if (!s || !g_root) return EFI_NOT_READY;
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, L"OORECOV.BIN");
    if (EFI_ERROR(st) || !f) return st;

    UINTN nb = (UINTN)sizeof(LlmkOoState);
    st = uefi_call_wrapper(f->Write, 3, f, &nb, (void *)s);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || nb != (UINTN)sizeof(LlmkOoState)) return EFI_LOAD_ERROR;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static void llmk_oo_jour_log_rotate_best_effort(void);

static void llmk_oo_outcome_log_rotate_best_effort(void) {
    if (!g_root) return;

    const UINTN max_bytes = 256 * 1024;
    const UINTN keep_bytes = 128 * 1024;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    if (len <= max_bytes) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    UINTN keep = keep_bytes;
    if (keep >= len) keep = len;
    UINTN start = len - keep;

    char *cbuf = (char *)buf;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"OOOUTCOME.LOG");
    if (!EFI_ERROR(st) && f) {
        UINTN nb = len - start;
        (void)llmk_file_write_bytes(f, (const void *)(cbuf + start), nb);
        (void)uefi_call_wrapper(f->Flush, 1, f);
        uefi_call_wrapper(f->Close, 1, f);
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_outcome_append_best_effort(UINT64 boot_count,
                                                UINT32 action_id,
                                                const char *expected_effect,
                                                const char *observed_effect,
                                                int improved) {
    if (!g_root) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file_append(&f, L"OOOUTCOME.LOG");
    if (EFI_ERROR(st) || !f) return;

    char line[256];
    int p = 0;
    line[0] = 0;

    llmk_ascii_append_str(line, (int)sizeof(line), &p, "[boot=");
    llmk_ascii_append_u64(line, (int)sizeof(line), &p, boot_count);
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "] action=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, llmk_oo_action_name(action_id));
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " expected=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, expected_effect ? expected_effect : "na");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " observed=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, observed_effect ? observed_effect : "na");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " improved=");
    if (improved < 0) {
        llmk_ascii_append_str(line, (int)sizeof(line), &p, "-1");
    } else {
        llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)improved);
    }
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "\r\n");

    UINTN nb = (UINTN)p;
    if (nb > 0) {
        uefi_call_wrapper(f->Write, 3, f, &nb, (void *)line);
        uefi_call_wrapper(f->Flush, 1, f);
    }
    uefi_call_wrapper(f->Close, 1, f);

    llmk_oo_outcome_log_rotate_best_effort();
}

static void llmk_oo_outcome_feedback_recent_best_effort(int *out_reduction_good,
                                                        int *out_reduction_bad,
                                                        int *out_increase_good,
                                                        int *out_increase_bad) {
    if (out_reduction_good) *out_reduction_good = 0;
    if (out_reduction_bad) *out_reduction_bad = 0;
    if (out_increase_good) *out_increase_good = 0;
    if (out_increase_bad) *out_increase_bad = 0;
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOOUTCOME.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = (len > 8192) ? (len - 8192) : 0;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') {
            start = i + 1;
            break;
        }
    }
    if (start >= len) start = 0;

    int considered = 0;
    char *p = cbuf + start;
    char *end = cbuf + len;
    while (p < end && considered < 16) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') {
            *p = 0;
            p++;
        }

        char *action = my_strstr(line, "action=");
        char *imp = my_strstr(line, "improved=");
        if (!action || !imp) continue;

        action += 7;
        imp += 9;
        if (*imp == '-') continue;
        int improved = (*imp == '1') ? 1 : 0;

        int is_reduce = 0;
        int is_increase = 0;
        if (my_strstr(action, "reduce_ctx") || my_strstr(action, "reduce_seq")) {
            is_reduce = 1;
        } else if (my_strstr(action, "increase_ctx")) {
            is_increase = 1;
        }
        if (!is_reduce && !is_increase) continue;

        considered++;
        if (is_reduce) {
            if (improved) {
                if (out_reduction_good) (*out_reduction_good)++;
            } else {
                if (out_reduction_bad) (*out_reduction_bad)++;
            }
        } else if (is_increase) {
            if (improved) {
                if (out_increase_good) (*out_increase_good)++;
            } else {
                if (out_increase_bad) (*out_increase_bad)++;
            }
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_journal_append_best_effort(const LlmkOoState *s, const char *event) {
    if (!g_root || !s) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file_append(&f, L"OOJOUR.LOG");
    if (EFI_ERROR(st) || !f) return;

    char line[192];
    int p = 0;
    line[0] = 0;
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "oo event=");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, event ? event : "boot");
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " boot=");
    llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)s->boot_count);
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " mode=");
    llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)s->mode);
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " rc=");
    llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)llmk_oo_get_rc(s->flags));
    llmk_ascii_append_str(line, (int)sizeof(line), &p, " sc=");
    llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)llmk_oo_get_sc(s->flags));
    llmk_ascii_append_str(line, (int)sizeof(line), &p, "\r\n");

    UINTN nb = (UINTN)p;
    if (nb > 0) {
        uefi_call_wrapper(f->Write, 3, f, &nb, (void *)line);
        uefi_call_wrapper(f->Flush, 1, f);
    }
    uefi_call_wrapper(f->Close, 1, f);

    // Enforce a max size cap (best-effort; never blocks boot).
    // Keep only the newest part of the log (FIFO truncation).
    llmk_oo_jour_log_rotate_best_effort();
}

static int llmk_oo_consult_metrics_tick_best_effort(LlmkOoState *s, char *out_event, int out_cap) {
    if (out_event && out_cap > 0) out_event[0] = 0;
    if (!s || !g_cfg_oo_enable) return 0;

    UINT32 meta = llmk_oo_get_last_action_meta(s->flags);
    UINT32 apply_boot_low8 = llmk_oo_get_last_apply_boot_low8(s->flags);
    if (meta == 0 || apply_boot_low8 == 0) return 0;

    UINT32 action_id = (meta & 0x3Fu);
    UINT32 apply_mode = (meta >> 6u) & 0x3u;

    UINT32 curr_boot_low8 = (UINT32)(s->boot_count & 0xFFu);
    UINT32 want = (UINT32)((apply_boot_low8 + 1u) & 0xFFu);
    if (curr_boot_low8 != want) return 0;

    // Improvement: mode numerically decreases (SAFE=2 -> DEGRADED=1 -> NORMAL=0)
    int improved = ((UINT32)s->mode < apply_mode) ? 1 : 0;
    Print(L"OK: OO consult metric: action=%a improved=%d\r\n", llmk_oo_action_name(action_id), improved);

    {
        const char *observed = improved ? "mode_improved" : "mode_not_improved";
        llmk_oo_outcome_append_best_effort((UINT64)s->boot_count,
                                           action_id,
                                           "mode_drop",
                                           observed,
                                           improved);
    }

    if (out_event && out_cap > 0) {
        int p = 0;
        out_event[0] = 0;
        llmk_ascii_append_str(out_event, out_cap, &p, "consult_metric action=");
        llmk_ascii_append_str(out_event, out_cap, &p, llmk_oo_action_name(action_id));
        llmk_ascii_append_str(out_event, out_cap, &p, " improved=");
        llmk_ascii_append_u64(out_event, out_cap, &p, (UINT64)improved);
        out_event[p] = 0;
    }

    // Clear metadata so we only report once.
    s->flags = llmk_oo_set_last_action_meta(s->flags, 0);
    s->flags = llmk_oo_set_last_apply_boot_low8(s->flags, 0);
    return 1;
}

static void llmk_oo_boot_tick_best_effort(void) {
    if (!g_cfg_oo_enable) return;
    if (!g_root) return;

    LlmkOoState s;
    int ok_primary = llmk_oo_load_state_best_effort(&s);
    const char *event = "boot";

    if (!ok_primary) {
        // Try rollback state.
        LlmkOoState r;
        int ok_rec = llmk_oo_load_recovery_best_effort(&r);
        if (ok_rec) {
            s = r;
            event = "recover";
            // Enter safe mode on recovery.
            s.mode = LLMK_OO_MODE_SAFE;
            {
                UINT32 rc = llmk_oo_get_rc(s.flags);
                if (rc < 255u) rc++;
                s.flags = llmk_oo_set_rc(s.flags, rc);
                s.flags = llmk_oo_set_sc(s.flags, 0);
            }
            Print(L"[oo] RECOVERY: OOSTATE invalid; using OORECOV rollback\r\n");
        } else {
            // Fresh init in safe mode.
            // Defaults already set by loader.
            event = "init";
            s.mode = LLMK_OO_MODE_SAFE;
            {
                UINT32 rc = llmk_oo_get_rc(s.flags);
                if (rc < 255u) rc++;
                s.flags = llmk_oo_set_rc(s.flags, rc);
                s.flags = llmk_oo_set_sc(s.flags, 0);
            }
            Print(L"[oo] RECOVERY: state missing/invalid; initializing SAFE\r\n");
        }
    } else {
        // Stable boot (state valid).
        s.flags = llmk_oo_set_rc(s.flags, 0);
        {
            UINT32 sc = llmk_oo_get_sc(s.flags);
            if (sc < 255u) sc++;
            s.flags = llmk_oo_set_sc(s.flags, sc);
        }

        // Minimal deterministic mode transition policy.
        // SAFE -> DEGRADED after 2 stable boots; DEGRADED -> NORMAL after 2 more.
        {
            UINT32 sc = llmk_oo_get_sc(s.flags);
            if (s.mode == LLMK_OO_MODE_SAFE && sc >= 2u) {
                s.mode = LLMK_OO_MODE_DEGRADED;
                s.flags = llmk_oo_set_sc(s.flags, 0);
                event = "mode_degraded";
            } else if (s.mode == LLMK_OO_MODE_DEGRADED && sc >= 2u) {
                s.mode = LLMK_OO_MODE_NORMAL;
                s.flags = llmk_oo_set_sc(s.flags, 0);
                event = "mode_normal";
            }
        }
    }

    g_oo_last_mode = s.mode;
    g_oo_last_mode_valid = 1;

    s.boot_count++;

    // M5.4: If an auto-apply happened last boot, emit one-shot metrics now.
    char metric_event[96];
    int has_metric = llmk_oo_consult_metrics_tick_best_effort(&s, metric_event, (int)sizeof(metric_event));

    s.magic = LLMK_OO_STATE_MAGIC;
    s.version = LLMK_OO_STATE_VER;
    s.size = (UINT32)sizeof(LlmkOoState);
    s.checksum = llmk_oo_state_checksum(&s);

    EFI_STATUS wst = llmk_oo_write_state_best_effort(&s);
    if (!EFI_ERROR(wst)) {
        // Refresh rollback checkpoint.
        llmk_oo_write_recovery_best_effort(&s);
    }
    llmk_oo_journal_append_best_effort(&s, event);
    if (has_metric && metric_event[0]) {
        llmk_oo_journal_append_best_effort(&s, metric_event);
    }

    if (!EFI_ERROR(wst)) {
        Print(L"OK: OO boot_count=%lu mode=%s\r\n", (UINT64)s.boot_count, llmk_oo_mode_name(s.mode));
    } else {
        Print(L"[oo] WARN: state write failed: %r\r\n", wst);
    }
}

// ============================================================================
// OO M4 — Network Read-only Tick (placeholder)
// - Never required for boot
// - Best-effort: detect if SNP is present; do not perform IO yet
// - Emits deterministic serial markers and journal events
// ============================================================================

static void llmk_oo_net_tick_best_effort(void) {
    if (!g_cfg_oo_enable || !g_cfg_oo_net) return;
    if (!g_root || !BS) return;

    EFI_GUID SnpGuid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;

    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                                     ByProtocol,
                                     &SnpGuid,
                                     NULL,
                                     &count,
                                     &handles);

    const int available = (!EFI_ERROR(st) && handles && count > 0);
    if (!available) {
        Print(L"OK: OO net: unavailable\r\n");

        EFI_FILE_HANDLE jf = NULL;
        if (!EFI_ERROR(llmk_open_binary_file_append(&jf, L"OOJOUR.LOG")) && jf) {
            char line[256];
            int p = 0;
            line[0] = 0;
            llmk_ascii_append_str(line, (int)sizeof(line), &p, "oo event=net_unavailable");
            if (g_cfg_oo_manifest_url[0]) {
                llmk_ascii_append_str(line, (int)sizeof(line), &p, " url=");
                llmk_ascii_append_str(line, (int)sizeof(line), &p, g_cfg_oo_manifest_url);
            }
            llmk_ascii_append_str(line, (int)sizeof(line), &p, "\r\n");
            UINTN nb = (UINTN)p;
            uefi_call_wrapper(jf->Write, 3, jf, &nb, (void *)line);
            uefi_call_wrapper(jf->Flush, 1, jf);
            uefi_call_wrapper(jf->Close, 1, jf);

            // Enforce max journal size (best-effort).
            llmk_oo_jour_log_rotate_best_effort();
        }

        if (handles) uefi_call_wrapper(BS->FreePool, 1, handles);
        return;
    }

    // Present, but still placeholder (no DHCP/HTTP stack here yet).
    Print(L"OK: OO net: present\r\n");

    {
        EFI_FILE_HANDLE jf = NULL;
        if (!EFI_ERROR(llmk_open_binary_file_append(&jf, L"OOJOUR.LOG")) && jf) {
            char line[192];
            int p = 0;
            line[0] = 0;
            llmk_ascii_append_str(line, (int)sizeof(line), &p, "oo event=net_present n=");
            llmk_ascii_append_u64(line, (int)sizeof(line), &p, (UINT64)count);
            llmk_ascii_append_str(line, (int)sizeof(line), &p, "\r\n");
            UINTN nb = (UINTN)p;
            uefi_call_wrapper(jf->Write, 3, jf, &nb, (void *)line);
            uefi_call_wrapper(jf->Flush, 1, jf);
            uefi_call_wrapper(jf->Close, 1, jf);

            // Enforce max journal size (best-effort).
            llmk_oo_jour_log_rotate_best_effort();
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, handles);
}

// OO M5: /oo_consult — LLM-based system health advisor (safety-first policy)
// Note: requires g_llmk_ready, weights loaded, etc. 
// Forward decl moved below after type definitions

static int llmk_char16_streq(const CHAR16 *a, const CHAR16 *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static void llmk_print_u64(UINT64 v) {
    // Print decimal without relying on format widths.
    CHAR16 buf[32];
    int p = 0;
    if (v == 0) {
        buf[p++] = L'0';
    } else {
        CHAR16 rev[32];
        int rn = 0;
        while (v > 0 && rn < (int)(sizeof(rev) / sizeof(rev[0]))) {
            rev[rn++] = (CHAR16)(L'0' + (v % 10));
            v /= 10;
        }
        while (rn > 0 && p + 1 < (int)(sizeof(buf) / sizeof(buf[0]))) buf[p++] = rev[--rn];
    }
    buf[p] = 0;
    Print(L"%s", buf);
}

static void llmk_fs_ls_best_effort(const CHAR16 *path, int max_entries) {
    if (!g_root) {
        Print(L"\r\nERROR: file system not ready\r\n\r\n");
        return;
    }
    if (max_entries <= 0) max_entries = 200;
    if (max_entries > 500) max_entries = 500;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!path || path[0] == 0 || llmk_char16_streq(path, L".") || llmk_char16_streq(path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, path, &dir, NULL, 0, L"ls_dir");
        if (EFI_ERROR(st) || !dir) {
            Print(L"\r\nERROR: cannot open %s: %r\r\n\r\n", (CHAR16 *)path, st);
            return;
        }
        close_dir = 1;
    }

    // Rewind
    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN printed = 0;
    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        Print(L"\r\nERROR: OOM\r\n\r\n");
        return;
    }

    while (printed < (UINTN)max_entries) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st)) {
            Print(L"\r\nERROR: ls read failed: %r\r\n\r\n", st);
            break;
        }
        if (sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        // Skip . and ..
        if (info->FileName && (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L".."))) {
            continue;
        }

        Print(L"  ");
        if (info->Attribute & EFI_FILE_DIRECTORY) {
            Print(L"<DIR> ");
        } else {
            Print(L"      ");
        }
        if (info->Attribute & EFI_FILE_DIRECTORY) {
            Print(L"      ");
        } else {
            llmk_print_u64(info->FileSize);
            // pad to ~10 chars (best-effort)
            Print(L" ");
        }
        Print(L" %s\r\n", info->FileName ? info->FileName : L"(null)");
        printed++;
    }

    if (printed == 0) {
        Print(L"  (empty)\r\n");
    }
    if (printed >= (UINTN)max_entries) {
        Print(L"  ... (truncated)\r\n");
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
}

static int llmk_is_model_file_name16(const CHAR16 *name) {
    if (!name || !name[0]) return 0;
    // tokenizer.bin is a required runtime asset, but it is not a model.
    if (llmk_char16_endswith_ci(name, L"tokenizer.bin")) return 0;
    if (llmk_char16_endswith_ci(name, L".bin")) return 1;
    if (llmk_char16_endswith_ci(name, L".gguf")) return 1;
    return 0;
}

static const CHAR16 *llmk_model_type_name16(const CHAR16 *name) {
    if (!name) return L"?";
    if (llmk_char16_endswith_ci(name, L".gguf")) return L"GGUF";
    if (llmk_char16_endswith_ci(name, L".bin")) return L"BIN";
    return L"?";
}

static int llmk_try_open_first_model_in_dir_best_effort(const CHAR16 *dir_path, EFI_FILE_HANDLE *out_f, CHAR16 *out_path, int out_cap) {
    if (out_f) *out_f = NULL;
    if (out_path && out_cap > 0) out_path[0] = 0;
    if (!g_root || !out_f || !out_path || out_cap <= 1) return 0;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, dir_path, &dir, NULL, 0, L"first_model_dir");
        if (EFI_ERROR(st) || !dir) {
            return 0;
        }
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
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        CHAR16 path[192];
        path[0] = 0;
        if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), info->FileName);
        } else {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), dir_path);
            // Ensure trailing backslash
            UINTN n = StrLen(path);
            if (n > 0 && path[n - 1] != L'\\' && n + 1 < (sizeof(path) / sizeof(path[0]))) {
                path[n] = L'\\';
                path[n + 1] = 0;
            }
            if (StrLen(path) + StrLen(info->FileName) + 1 < (sizeof(path) / sizeof(path[0]))) {
                StrCat(path, info->FileName);
            }
        }

        EFI_FILE_HANDLE f = NULL;
        CHAR16 picked[192];
        picked[0] = 0;
        EFI_STATUS fst = llmk_open_read_with_fat83_fallback(g_root, path, &f, picked,
                                                           (int)(sizeof(picked) / sizeof(picked[0])),
                                                           L"first_model");
        if (!EFI_ERROR(fst) && f) {
            *out_f = f;
            if (picked[0]) llmk_char16_copy_cap(out_path, out_cap, picked);
            else llmk_char16_copy_cap(out_path, out_cap, path);
            found = 1;
            break;
        }
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
    return found;
}

// Forward declarations used by model picker.
void read_user_input(CHAR16* buffer, int max_len);
void char16_to_char(char* dest, CHAR16* src, int max_len);

typedef struct {
    CHAR16 path[192];
    UINT64 size;
} LlmkModelEntry;

static int llmk_collect_models_in_dir(const CHAR16 *dir_path, LlmkModelEntry *out, int cap) {
    if (!g_root || !out || cap <= 0) return 0;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;
    if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, dir_path, &dir, NULL, 0, L"collect_models_dir");
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

    int count = 0;
    while (count < cap) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (!info->FileName) continue;
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        CHAR16 path[192];
        path[0] = 0;
        if (!dir_path || dir_path[0] == 0 || llmk_char16_streq(dir_path, L".") || llmk_char16_streq(dir_path, L"\\")) {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), info->FileName);
        } else {
            llmk_char16_copy_cap(path, (int)(sizeof(path) / sizeof(path[0])), dir_path);
            UINTN n = StrLen(path);
            if (n > 0 && path[n - 1] != L'\\' && n + 1 < (sizeof(path) / sizeof(path[0]))) {
                path[n] = L'\\';
                path[n + 1] = 0;
            }
            if (StrLen(path) + StrLen(info->FileName) + 1 < (sizeof(path) / sizeof(path[0]))) {
                StrCat(path, info->FileName);
            }
        }

        llmk_char16_copy_cap(out[count].path, (int)(sizeof(out[count].path) / sizeof(out[count].path[0])), path);
        out[count].size = info->FileSize;
        count++;
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
    return count;
}

static int llmk_collect_models(LlmkModelEntry *out, int cap) {
    int n = 0;
    if (cap <= 0) return 0;
    n += llmk_collect_models_in_dir(NULL, out + n, cap - n);
    if (n < cap) {
        n += llmk_collect_models_in_dir(L"models", out + n, cap - n);
    }
    return n;
}

static int llmk_model_picker(EFI_FILE_HANDLE *out_f, CHAR16 *out_path, int out_cap) {
    if (out_f) *out_f = NULL;
    if (out_path && out_cap > 0) out_path[0] = 0;
    if (!g_root || !out_f || !out_path || out_cap <= 1) return 0;

    LlmkModelEntry entries[48];
    int n = llmk_collect_models(entries, (int)(sizeof(entries) / sizeof(entries[0])));
    if (n <= 0) return 0;

    if (n == 1) {
        EFI_FILE_HANDLE f = NULL;
        CHAR16 picked[192];
        picked[0] = 0;
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, entries[0].path, &f, picked,
                                                          (int)(sizeof(picked) / sizeof(picked[0])),
                                                          L"picker_one");
        if (EFI_ERROR(st) || !f) return 0;
        *out_f = f;
        if (picked[0]) llmk_char16_copy_cap(out_path, out_cap, picked);
        else llmk_char16_copy_cap(out_path, out_cap, entries[0].path);
        return 1;
    }

    Print(L"\r\nModel picker:\r\n");
    for (int i = 0; i < n; i++) {
        UINT64 mb = entries[i].size / (1024ULL * 1024ULL);
        Print(L"  %d) %s  (%lu MB)\r\n", i + 1, entries[i].path, mb);
    }
    Print(L"  0) cancel\r\n\r\n");

    CHAR16 input16[64];
    char input8[64];
    Print(L"Select model number: ");
    read_user_input(input16, (int)(sizeof(input16) / sizeof(input16[0])));
    char16_to_char(input8, input16, (int)sizeof(input8));

    int sel = 0;
    int i = 0;
    while (input8[i] == ' ' || input8[i] == '\t') i++;
    while (input8[i] >= '0' && input8[i] <= '9') {
        sel = sel * 10 + (input8[i] - '0');
        i++;
    }
    if (sel <= 0 || sel > n) {
        Print(L"\r\nModel picker canceled.\r\n\r\n");
        return 0;
    }

    int idx = sel - 1;
    EFI_FILE_HANDLE f = NULL;
    CHAR16 picked[192];
    picked[0] = 0;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, entries[idx].path, &f, picked,
                                                      (int)(sizeof(picked) / sizeof(picked[0])),
                                                      L"picker_sel");
    if (EFI_ERROR(st) || !f) {
        Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", entries[idx].path, st);
        return 0;
    }
    *out_f = f;
    if (picked[0]) llmk_char16_copy_cap(out_path, out_cap, picked);
    else llmk_char16_copy_cap(out_path, out_cap, entries[idx].path);
    return 1;
}

static int llmk_try_open_first_model_best_effort(EFI_FILE_HANDLE *out_f, CHAR16 *out_path, int out_cap) {
    if (out_f) *out_f = NULL;
    if (out_path && out_cap > 0) out_path[0] = 0;
    if (!g_root || !out_f || !out_path || out_cap <= 1) return 0;

    // Prefer root models (common for single-model images), then /models
    if (llmk_try_open_first_model_in_dir_best_effort(NULL, out_f, out_path, out_cap)) return 1;
    if (llmk_try_open_first_model_in_dir_best_effort(L"models", out_f, out_path, out_cap)) return 1;
    return 0;
}

static void llmk_print_no_model_help(void) {
    Print(L"\r\nNo model loaded.\r\n");
    Print(L"Commands:\r\n");
    Print(L"  /models               List .bin/.gguf in root + models\\\r\n");
    Print(L"  /model_info [path]    Inspect a .bin/.gguf header/metadata\r\n");
    Print(L"  /cat <path>           Print a text file (e.g. repl.cfg)\r\n");
    Print(L"  reboot | reset        Reboot\r\n");
    Print(L"  shutdown              Power off\r\n");
    Print(L"  exit                  Return to UEFI shell\r\n\r\n");
    Print(L"To boot with a model: copy a supported .gguf/.bin to the USB root (or models\\)\r\n");
    Print(L"and set repl.cfg: model=<filename> then reboot.\r\n\r\n");
}

// Forward declarations for the no-model REPL (definitions appear later in this file).
void read_user_input(CHAR16* buffer, int max_len);
void char16_to_char(char* dest, CHAR16* src, int max_len);
int my_strncmp(const char* s1, const char* s2, int n);
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);
static void llmk_models_ls_best_effort(const CHAR16 *path, int max_entries);
static void llmk_fs_cat_best_effort(const CHAR16 *path, UINTN max_bytes);
static void llmk_print_diag(void);

static void llmk_repl_no_model_loop(void) {
    Print(L"OK: REPL ready (no model). Type /help\r\n\r\n");
    while (1) {
        CHAR16 user_input[512];
        char prompt[512];
        prompt[0] = 0;
        Print(L"llmk> ");
        read_user_input(user_input, 512);
        char16_to_char(prompt, user_input, 512);
        if (prompt[0] == 0) continue;

        if (my_strncmp(prompt, "/help", 5) == 0 || my_strncmp(prompt, "/commands", 9) == 0) {
            llmk_print_no_model_help();
            continue;
        }
        if (my_strncmp(prompt, "/diag", 5) == 0) {
            llmk_print_diag();
            continue;
        }
        if (my_strncmp(prompt, "/models", 7) == 0) {
            Print(L"\r\nModels (.bin/.gguf):\r\n");
            Print(L"Root:\r\n");
            llmk_models_ls_best_effort(NULL, 200);
            Print(L"\r\nmodels\\:\r\n");
            llmk_models_ls_best_effort(L"models", 200);
            Print(L"\r\n");
            continue;
        }
        if (my_strncmp(prompt, "/model_info", 11) == 0) {
            CHAR16 path16[192];
            path16[0] = 0;

            int i = 11;
            while (prompt[i] == ' ') i++;
            if (prompt[i] != 0) {
                char p8[160];
                int n = 0;
                while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(p8)) {
                    p8[n++] = prompt[i++];
                }
                p8[n] = 0;
                ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
            } else {
                StrCpy(path16, L"model.bin");
            }

            if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF &&
                g_loaded_model_gguf_valid &&
                llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                llmk_print_gguf_summary_block(path16, &g_loaded_model_gguf);
                Print(L"\r\n");
                continue;
            }

            EFI_FILE_HANDLE f = NULL;
            EFI_STATUS st = llmk_open_read_file(&f, path16);
            if (EFI_ERROR(st) || !f) {
                Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", path16, st);
                continue;
            }

            LlmkModelFormat fmt = llmk_detect_model_format(f);
            if (fmt == LLMK_MODEL_FMT_GGUF) {
                GgufSummary s;
                EFI_STATUS gst = gguf_read_summary(f, &s);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(gst)) {
                    Print(L"\r\nGGUF: failed to parse (%r)\r\n\r\n", gst);
                    continue;
                }
                llmk_print_gguf_summary_block(path16, &s);
                if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF && llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                    g_loaded_model_gguf = s;
                    g_loaded_model_gguf_valid = 1;
                }
                Print(L"\r\n");
                continue;
            }

            EFI_STATUS pst = uefi_call_wrapper(f->SetPosition, 2, f, 0);
            if (EFI_ERROR(pst)) {
                uefi_call_wrapper(f->Close, 1, f);
                Print(L"\r\nERROR: seek failed (%r)\r\n\r\n", pst);
                continue;
            }
            int hdr[7];
            for (int k = 0; k < 7; k++) hdr[k] = 0;
            UINTN bytes = (UINTN)(7 * sizeof(int));
            EFI_STATUS rst = uefi_call_wrapper(f->Read, 3, f, &bytes, hdr);
            uefi_call_wrapper(f->Close, 1, f);
            if (EFI_ERROR(rst) || bytes != (UINTN)(7 * sizeof(int))) {
                Print(L"\r\nBIN: failed to read header (%r)\r\n\r\n", rst);
                continue;
            }

            int dim = hdr[0];
            int n_layers = hdr[2];
            int n_heads = hdr[3];
            int n_kv_heads = hdr[4];
            int vocab = hdr[5];
            int seq_len = hdr[6];
            int shared = (vocab < 0);
            if (vocab < 0) vocab = -vocab;
            Print(L"\r\nBIN model info:\r\n");
            Print(L"  file=%s\r\n", path16);
            Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d shared_cls=%d\r\n\r\n",
                  dim, n_layers, n_heads, n_kv_heads, vocab, seq_len, shared);
            continue;
        }
        if (my_strncmp(prompt, "/cat", 4) == 0) {
            int i = 4;
            while (prompt[i] == ' ') i++;
            if (prompt[i] == 0) {
                Print(L"\r\nUsage: /cat <path>\r\n\r\n");
                continue;
            }
            char p8[192];
            int n = 0;
            while (prompt[i] && n + 1 < (int)sizeof(p8)) p8[n++] = prompt[i++];
            p8[n] = 0;
            CHAR16 path16[256];
            ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));

            // Pre-resolve long filenames via FAT 8.3 alias so cat works on firmwares
            // with unreliable LFN opens.
            {
                EFI_FILE_HANDLE tf = NULL;
                CHAR16 picked[256];
                picked[0] = 0;
                EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, path16, &tf, picked,
                                                                  (int)(sizeof(picked) / sizeof(picked[0])),
                                                                  L"cat");
                if (EFI_ERROR(st) || !tf) {
                    Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", path16, st);
                    continue;
                }
                uefi_call_wrapper(tf->Close, 1, tf);
                if (picked[0]) {
                    llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), picked);
                }
            }

            llmk_fs_cat_best_effort(path16, 256U * 1024U);
            Print(L"\r\n");
            continue;
        }

        if (my_strncmp(prompt, "exit", 4) == 0 || my_strncmp(prompt, "quit", 4) == 0) {
            Print(L"\r\nBye.\r\n");
            return;
        }
        if (my_strncmp(prompt, "reboot", 6) == 0 || my_strncmp(prompt, "reset", 5) == 0) {
            Print(L"\r\nRebooting...\r\n");
            uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
            return;
        }
        if (my_strncmp(prompt, "shutdown", 8) == 0 || my_strncmp(prompt, "poweroff", 8) == 0) {
            Print(L"\r\nShutting down...\r\n");
            uefi_call_wrapper(RT->ResetSystem, 4, EfiResetShutdown, EFI_SUCCESS, 0, NULL);
            return;
        }

        Print(L"\r\nNo model loaded. Use /models then set repl.cfg: model=<file> and reboot.\r\n\r\n");
    }
}

// Diagnostic:displayGOP resolution, memory, build-id, detected models, CPU features
static void llmk_print_diag(void) {
    Print(L"\r\n========== DIAGNOSTIC MODE ==========\r\n\r\n");
    
    // 1. Build ID
    Print(L"Build ID: %s\r\n\r\n", LLMB_BUILD_ID);
    
    // 2. GOP (Graphics Output Protocol) Info
    if (g_gop && g_gop_fb32) {
        Print(L"[GOP] Graphics:\r\n");
        Print(L"  Resolution:    %dx%d\r\n", (int)g_gop_w, (int)g_gop_h);
        Print(L"  Scan Line:     %d pixels\r\n", (int)g_gop_ppsl);
        Print(L"  Framebuffer:   0x%lx\r\n", (UINT64)(UINTN)g_gop_fb32);
        if (g_gop->Mode) {
            Print(L"  FB Size:       %lu bytes\r\n", (UINT64)g_gop->Mode->FrameBufferSize);
        }
        Print(L"  Pixel Format:  %d\r\n", (int)g_gop_pf);
    } else {
        Print(L"[GOP] Graphics:  Not available\r\n");
    }
    Print(L"\r\n");
    
    // 3. Memory Info
    UINT64 total_mem = llmk_get_conventional_ram_bytes_best_effort();
    if (total_mem > 0) {
        UINT64 mb = total_mem / (1024ULL * 1024ULL);
        Print(L"[Memory] Conventional RAM: %lu MiB\r\n\r\n", mb);
    } else {
        Print(L"[Memory] Unable to query\r\n\r\n");
    }
    
    // 4. CPU Features
    Print(L"[CPU] Features:\r\n");
    CPUFeatures cpu_features;
    djiblas_detect_cpu(&cpu_features);
    sgemm_kernel_t k = djiblas_get_best_kernel(&cpu_features);
    const CHAR16 *kernel_name = L"SCALAR";
    if (k == djiblas_sgemm_avx512) kernel_name = L"AVX512";
    else if (k == djiblas_sgemm_avx2) kernel_name = (cpu_features.has_fma ? L"AVX2+FMA" : L"AVX2");
    else if (k == djiblas_sgemm_sse2) kernel_name = L"SSE2";
    
    Print(L"  SSE2:          %s\r\n", cpu_features.has_sse2 ? L"Yes" : L"No");
    Print(L"  AVX:           %s\r\n", cpu_features.has_avx ? L"Yes" : L"No");
    Print(L"  AVX2:          %s\r\n", cpu_features.has_avx2 ? L"Yes" : L"No");
    Print(L"  FMA:           %s\r\n", cpu_features.has_fma ? L"Yes" : L"No");
    Print(L"  SGEMM Kernel:  %s\r\n", kernel_name);
    Print(L"  Attn SIMD:     %s\r\n", g_attn_use_avx2 ? L"AVX2" : L"SSE2");
    Print(L"\r\n");
    
    // 5. Detected Models
    Print(L"[Models] Detected paths:\r\n");
    Print(L"  Root:\r\n");
    llmk_models_ls_best_effort(NULL, 200);
    Print(L"  models\\:\r\n");
    llmk_models_ls_best_effort(L"models", 200);
    
    Print(L"\r\n========== END DIAGNOSTIC ==========\r\n\r\n");
}

static void llmk_models_ls_best_effort(const CHAR16 *path, int max_entries) {
    if (!g_root) {
        Print(L"\r\nERROR: file system not ready\r\n\r\n");
        return;
    }
    if (max_entries <= 0) max_entries = 200;
    if (max_entries > 500) max_entries = 500;

    EFI_FILE_HANDLE dir = NULL;
    int close_dir = 0;

    if (!path || path[0] == 0 || llmk_char16_streq(path, L".") || llmk_char16_streq(path, L"\\")) {
        dir = g_root;
        close_dir = 0;
    } else {
        EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, path, &dir, NULL, 0, L"models_ls_dir");
        if (EFI_ERROR(st) || !dir) {
            Print(L"\r\nERROR: cannot open %s: %r\r\n\r\n", (CHAR16 *)path, st);
            return;
        }
        close_dir = 1;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);

    UINTN printed = 0;
    UINTN matched = 0;
    UINTN bin_count = 0;
    UINTN gguf_count = 0;
    UINT64 total_bytes = 0;
    UINTN buf_cap = 1024;
    void *buf = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buf_cap, &buf);
    if (EFI_ERROR(st) || !buf) {
        if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
        Print(L"\r\nERROR: OOM\r\n\r\n");
        return;
    }

    while (printed < (UINTN)max_entries) {
        UINTN sz = buf_cap;
        st = uefi_call_wrapper(dir->Read, 3, dir, &sz, buf);
        if (EFI_ERROR(st)) {
            Print(L"\r\nERROR: ls read failed: %r\r\n\r\n", st);
            break;
        }
        if (sz == 0) break;

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (!info->FileName) continue;
        if (llmk_char16_streq(info->FileName, L".") || llmk_char16_streq(info->FileName, L"..")) continue;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        if (!llmk_is_model_file_name16(info->FileName)) continue;

        if (matched == 0) {
            Print(L"  size      type  name\r\n");
        }
        const CHAR16 *type = llmk_model_type_name16(info->FileName);
        Print(L"  ");
        llmk_print_u64(info->FileSize);
        Print(L" ");
        Print(L"%s", type);
        if (llmk_char16_streq(type, L"BIN")) {
            Print(L"   ");
            bin_count++;
        } else if (llmk_char16_streq(type, L"GGUF")) {
            Print(L"  ");
            gguf_count++;
        } else {
            Print(L"    ");
        }
        Print(L"%s\r\n", info->FileName);
        printed++;
        matched++;
        total_bytes += info->FileSize;
    }

    if (matched == 0) {
        Print(L"  (no .bin/.gguf found)\r\n");
    }
    if (printed >= (UINTN)max_entries) {
        Print(L"  ... (truncated)\r\n");
    }
    if (matched > 0) {
        Print(L"  summary: total=");
        llmk_print_u64((UINT64)matched);
        Print(L" bin=");
        llmk_print_u64((UINT64)bin_count);
        Print(L" gguf=");
        llmk_print_u64((UINT64)gguf_count);
        Print(L" bytes=");
        llmk_print_u64(total_bytes);
        Print(L"\r\n");
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    if (close_dir) uefi_call_wrapper(dir->Close, 1, dir);
}

static void llmk_fs_cat_best_effort(const CHAR16 *path, UINTN max_bytes) {
    if (max_bytes == 0) max_bytes = (256U * 1024U);
    if (max_bytes > (1024U * 1024U)) max_bytes = (1024U * 1024U);

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(path, &raw, &raw_len);
    if (EFI_ERROR(st)) {
        Print(L"\r\nERROR: cat failed: %r\r\n\r\n", st);
        return;
    }

    UINT8 *b = (UINT8 *)raw;
    UINTN n = raw_len;
    if (n > max_bytes) n = max_bytes;

    // UTF-16 BOM detection (LE/BE). Print best-effort ASCII.
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        int is_le = (b[0] == 0xFF);
        UINTN chars = (n - 2) / 2;
        for (UINTN i = 0; i < chars; i++) {
            UINT8 lo = b[2 + i * 2 + 0];
            UINT8 hi = b[2 + i * 2 + 1];
            UINT16 ch = is_le ? (UINT16)(lo | ((UINT16)hi << 8)) : (UINT16)(hi | ((UINT16)lo << 8));
            if (ch == 0) break;
            CHAR16 out = (ch < 0x80) ? (CHAR16)ch : L'?';
            Print(L"%c", out);
        }
    } else {
        for (UINTN i = 0; i < n; i++) {
            UINT8 c = b[i];
            if (c == 0) break;
            // Print ASCII; map CR to LF
            if (c == '\r') c = '\n';
            if (c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7E)) {
                Print(L"%c", (CHAR16)c);
            }
        }
    }
    Print(L"\r\n");

    if (raw_len > max_bytes) {
        Print(L"(truncated to %d bytes)\r\n", (int)max_bytes);
    }

    uefi_call_wrapper(BS->FreePool, 1, raw);
}

typedef struct {
    UINT32 magic;      // 'SNP1'
    UINT32 version;    // 1
    UINT32 dim;
    UINT32 n_layers;
    UINT32 n_heads;
    UINT32 n_kv_heads;
    UINT32 seq_len;
    UINT32 kv_dim;
    UINT32 kv_pos;
} LlmkSnapHeader;

#define LLMK_SNAP_MAGIC 0x31504E53u

static EFI_STATUS llmk_write_exact(EFI_FILE_HANDLE f, const void *src, UINTN total_bytes) {
    const UINT8 *p = (const UINT8 *)src;
    UINTN remaining = total_bytes;
    while (remaining > 0) {
        UINTN chunk = remaining;
        if (chunk > (8U * 1024U * 1024U)) chunk = (8U * 1024U * 1024U);
        UINTN nb = chunk;
        EFI_STATUS st = uefi_call_wrapper(f->Write, 3, f, &nb, (void *)p);
        if (EFI_ERROR(st)) return st;
        if (nb != chunk) return EFI_DEVICE_ERROR;
        p += nb;
        remaining -= nb;
    }
    return EFI_SUCCESS;
}

// ============================================================================
// AUTORUN SCRIPT (best-effort)
// ============================================================================

// Forward decl (autorun helpers use it before definition)
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);

static int g_autorun_active = 0;
static int g_autorun_shutdown_when_done = 0;
static char *g_autorun_buf = NULL;
static UINTN g_autorun_len = 0;
static UINTN g_autorun_pos = 0;

static void llmk_autorun_stop(void) {
    g_autorun_active = 0;
    g_autorun_shutdown_when_done = 0;
    g_autorun_pos = 0;
    g_autorun_len = 0;
    if (g_autorun_buf) {
        uefi_call_wrapper(BS->FreePool, 1, g_autorun_buf);
        g_autorun_buf = NULL;
    }
}

static int llmk_autorun_decode_to_ascii(void *raw, UINTN raw_len, char **out_ascii, UINTN *out_len) {
    if (out_ascii) *out_ascii = NULL;
    if (out_len) *out_len = 0;
    if (!raw || raw_len == 0 || !out_ascii || !out_len) return 0;

    UINT8 *b = (UINT8 *)raw;
    // Detect UTF-16 BOM (LE/BE). If present, down-convert to 7-bit ASCII best-effort.
    if (raw_len >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        int is_le = (b[0] == 0xFF);
        UINTN chars = (raw_len - 2) / 2;
        char *txt = NULL;
        EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, chars + 1, (void **)&txt);
        if (EFI_ERROR(st) || !txt) return 0;
        for (UINTN i = 0; i < chars; i++) {
            UINT8 lo = b[2 + i * 2 + 0];
            UINT8 hi = b[2 + i * 2 + 1];
            UINT16 ch = is_le ? (UINT16)(lo | ((UINT16)hi << 8)) : (UINT16)(hi | ((UINT16)lo << 8));
            if (ch == 0) { txt[i] = 0; *out_ascii = txt; *out_len = i; return 1; }
            txt[i] = (ch < 0x80) ? (char)ch : '?';
        }
        txt[chars] = 0;
        *out_ascii = txt;
        *out_len = chars;
        return 1;
    }

    // Assume ASCII/UTF-8 bytes; copy and NUL-terminate.
    char *txt = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&txt);
    if (EFI_ERROR(st) || !txt) return 0;
    for (UINTN i = 0; i < raw_len; i++) txt[i] = (char)b[i];
    txt[raw_len] = 0;
    *out_ascii = txt;
    *out_len = raw_len;
    return 1;
}

static int llmk_autorun_start(const CHAR16 *name, int shutdown_when_done) {
    if (!name) return 0;
    llmk_autorun_stop();

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        return 0;
    }

    char *txt = NULL;
    UINTN txt_len = 0;
    int ok = llmk_autorun_decode_to_ascii(raw, raw_len, &txt, &txt_len);
    uefi_call_wrapper(BS->FreePool, 1, raw);
    if (!ok || !txt) {
        if (txt) uefi_call_wrapper(BS->FreePool, 1, txt);
        return 0;
    }

    g_autorun_buf = txt;
    g_autorun_len = txt_len;
    g_autorun_pos = 0;
    g_autorun_active = 1;
    g_autorun_shutdown_when_done = shutdown_when_done ? 1 : 0;
    Print(L"[autorun] loaded %s (%d bytes)\r\n", name, (int)txt_len);
    return 1;
}

static void llmk_autorun_trim(char *s) {
    if (!s) return;
    // left trim
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
    if (i > 0) {
        int j = 0;
        while (s[i]) s[j++] = s[i++];
        s[j] = 0;
    }
    // right trim
    int n = 0;
    while (s[n]) n++;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    s[n] = 0;
}

static int llmk_autorun_next_line(char *out, int out_cap) {
    if (out && out_cap > 0) out[0] = 0;
    if (!g_autorun_active || !g_autorun_buf || g_autorun_pos >= g_autorun_len) return 0;
    if (!out || out_cap <= 1) return 0;

    // Skip empty lines and comments.
    while (g_autorun_pos < g_autorun_len) {
        int op = 0;
        // Read one line
        while (g_autorun_pos < g_autorun_len) {
            char c = g_autorun_buf[g_autorun_pos++];
            if (c == '\n') break;
            if (c == '\r') continue;
            if (op + 1 < out_cap) out[op++] = c;
        }
        out[op] = 0;
        llmk_autorun_trim(out);
        if (out[0] == 0) continue;
        if (out[0] == '#' || out[0] == ';') continue;
        return 1;
    }

    return 0;
}

static void llmk_autorun_print_file_best_effort(const CHAR16 *name, int max_lines) {
    if (!name) name = L"llmk-autorun.txt";
    if (max_lines <= 0) max_lines = 200;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(name, &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        Print(L"\r\n[autorun] cannot read %s (%r)\r\n\r\n", name, st);
        return;
    }

    char *txt = NULL;
    UINTN txt_len = 0;
    int ok = llmk_autorun_decode_to_ascii(raw, raw_len, &txt, &txt_len);
    uefi_call_wrapper(BS->FreePool, 1, raw);
    if (!ok || !txt) {
        if (txt) uefi_call_wrapper(BS->FreePool, 1, txt);
        Print(L"\r\n[autorun] decode failed\r\n\r\n");
        return;
    }

    Print(L"\r\n[autorun] %s:\r\n", name);
    UINTN pos = 0;
    int lines = 0;
    char linebuf[256];

    while (pos < txt_len && lines < max_lines) {
        int op = 0;
        while (pos < txt_len) {
            char c = txt[pos++];
            if (c == '\n') break;
            if (c == '\r') continue;
            if (op + 1 < (int)sizeof(linebuf)) linebuf[op++] = c;
        }
        linebuf[op] = 0;
        llmk_autorun_trim(linebuf);
        if (linebuf[0] == 0) continue;
        if (linebuf[0] == '#' || linebuf[0] == ';') continue;

        CHAR16 p16[300];
        ascii_to_char16(p16, linebuf, (int)(sizeof(p16) / sizeof(p16[0])));
        Print(L"  %s\r\n", p16);
        lines++;
    }

    if (lines == 0) {
        Print(L"  (no runnable lines)\r\n");
    } else if (pos < txt_len) {
        Print(L"  ... (truncated)\r\n");
    }
    Print(L"\r\n");
    uefi_call_wrapper(BS->FreePool, 1, txt);
}

static int llmk_ascii_append_u32(char *dst, int cap, int pos, UINT32 v) {
    if (!dst || cap <= 0) return pos;
    if (pos < 0) pos = 0;
    if (pos >= cap) return pos;

    char tmp[16];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        if (pos + 1 >= cap) break;
        dst[pos++] = tmp[i];
    }
    return pos;
}

// Forward declaration (llmk_save_ppm uses it before definition)
void* simple_alloc(unsigned long bytes);

static EFI_STATUS llmk_save_ppm(const CHAR16 *name) {
    if (!g_gop_fb32) return EFI_NOT_READY;
    if (!name) name = L"llmk-img.ppm";

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, name);
    if (EFI_ERROR(st)) return st;

    char header[64];
    int pos = 0;
    header[pos++] = 'P'; header[pos++] = '6'; header[pos++] = '\n';
    pos = llmk_ascii_append_u32(header, (int)sizeof(header), pos, g_gop_w);
    header[pos++] = ' ';
    pos = llmk_ascii_append_u32(header, (int)sizeof(header), pos, g_gop_h);
    header[pos++] = '\n';
    header[pos++] = '2'; header[pos++] = '5'; header[pos++] = '5'; header[pos++] = '\n';

    st = llmk_file_write_bytes(f, header, (UINTN)pos);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(f->Close, 1, f);
        return st;
    }

    // Row buffer: RGB bytes
    UINTN row_bytes = (UINTN)g_gop_w * 3U;
    UINT8 *row = (UINT8 *)simple_alloc((unsigned long)row_bytes);
    if (!row) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_OUT_OF_RESOURCES;
    }

    for (UINT32 y = 0; y < g_gop_h; y++) {
        UINTN off = 0;
        for (UINT32 x = 0; x < g_gop_w; x++) {
            UINT8 r, g, b;
            llmk_gop_get_pixel(x, y, &r, &g, &b);
            row[off++] = r;
            row[off++] = g;
            row[off++] = b;
        }
        st = llmk_file_write_bytes(f, row, row_bytes);
        if (EFI_ERROR(st)) {
            uefi_call_wrapper(f->Close, 1, f);
            return st;
        }
    }

    // Flush-before-close for persistence on real hardware
    uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    return EFI_SUCCESS;
}

static UINT64 g_budget_prefill_cycles = 0;
static UINT64 g_budget_decode_cycles = 0;

// Config (repl.cfg)
// By default, we do NOT auto-run llmk-autorun.txt at boot (to keep QEMU interactive).
static int g_cfg_autorun_autostart = 0;
static int g_cfg_autorun_shutdown_when_done = 0;
static CHAR16 g_cfg_autorun_file[96] = L"llmk-autorun.txt";

// Boot logging verbosity. 0=quiet (default), 1=verbose.
static int g_boot_verbose = 0;
// Boot logo. 1=show ASCII logo after repl.cfg is loaded, 0=skip.
static int g_boot_logo = 1;
// Boot diagnostic. 0=off (default), 1=show system info at boot.
static int g_boot_diag = 0;
static int g_cfg_loaded = 0;

// GGUF Q8_0 blob mode (keeps matrices quantized in RAM). 1=enabled (default), 0=force float32 load.
static int g_cfg_gguf_q8_blob = 1;
// Q8_0 matmul option: quantize activations (x) to Q8_0 for faster AVX2 int8 dot kernels.
// 0=off (default, higher fidelity)
// 1=on for all Q8 matmuls (fastest, most approximation)
// 2=FFN-only (w1/w3/w2), attention projections stay float (better quality/perf tradeoff)
static int g_cfg_q8_act_quant = 0;

typedef enum {
    LLMK_CHAT_FMT_YOU_AI = 0,
    LLMK_CHAT_FMT_LLAMA2 = 1,
    LLMK_CHAT_FMT_CHATML = 2,
    LLMK_CHAT_FMT_ALPACA = 3,
    LLMK_CHAT_FMT_RAW = 4,
} LlmkChatFormat;

// Chat formatting (default: You/AI). Used to wrap user turns for instruct/chat models.
static int g_cfg_chat_format = LLMK_CHAT_FMT_YOU_AI;
static char g_cfg_system_prompt[256] = {0};

// Model picker and context override (repl.cfg)
static int g_cfg_model_picker = 1;
static int g_cfg_ctx_len = 0;

// Rate-limit budget overrun prints (avoid flooding console).
static UINT32 g_budget_overruns_prefill = 0;
static UINT32 g_budget_overruns_decode = 0;

// Forward decl (used by repl.cfg loader before definition)
static void set_seed(unsigned int seed);
// Forward decl (used by model override reader before definition)
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);
// Forward decl (used by repl.cfg editor helper before definition)
static int my_strlen(const char* s);
// Forward decl (used by transformer_forward before definition)
static int llmk_has_avx2_cached(void);

static void llmk_reset_runtime_state(void) {
    // Budgets
    g_budget_prefill_cycles = 0;
    g_budget_decode_cycles = 0;
    g_budget_overruns_prefill = 0;
    g_budget_overruns_decode = 0;

    // Log (best-effort)
    if (g_llmk_log.capacity) {
        g_llmk_log.entries = 0;
        g_llmk_log.write_idx = 0;
    }

    // Sentinel
    g_sentinel.tripped = FALSE;
    g_sentinel.last_error = LLMK_OK;
    g_sentinel.last_reason[0] = 0;

    // UTF-8 repair tail
    uefi_print_utf8_flush();
}

static UINT64 llmk_u64_max(UINT64 a, UINT64 b) { return (a > b) ? a : b; }

static void llmk_budget_update(UINT64 *budget, UINT64 last_dt) {
    // Adaptive budget: target = last_dt * margin, then EMA to smooth.
    // Margin must tolerate pos growth and occasional slowdowns.
    const UINT64 margin = 6ULL;
    UINT64 target = last_dt * margin;
    if (target < 500000ULL) target = 500000ULL;
    if (*budget == 0) {
        *budget = target;
        return;
    }
    UINT64 prev = *budget;
    // If we started from a huge initial budget, snap down quickly once we have a real measurement.
    if (prev > target * 4ULL) {
        *budget = target;
        return;
    }
    // EMA: new = (7/8)*old + (1/8)*target
    *budget = ((*budget * 7ULL) + target) / 8ULL;
    // Never decrease too aggressively; keep at least 80% of previous.
    *budget = llmk_u64_max(*budget, (prev * 4ULL) / 5ULL);
}

static void* llmk_alloc_acts(UINT64 bytes, const CHAR16* tag) {
    if (!g_llmk_ready) return NULL;
    return llmk_sentinel_alloc(&g_sentinel, LLMK_ARENA_ACTIVATIONS, bytes, 16, tag);
}

static void* llmk_alloc_weights(UINT64 bytes, const CHAR16* tag) {
    if (!g_llmk_ready) return NULL;
    return llmk_sentinel_alloc(&g_sentinel, LLMK_ARENA_WEIGHTS, bytes, 64, tag);
}

static void* llmk_alloc_kv(UINT64 bytes, const CHAR16* tag) {
    if (!g_llmk_ready) return NULL;
    return llmk_sentinel_alloc(&g_sentinel, LLMK_ARENA_KV_CACHE, bytes, 64, tag);
}

void* simple_alloc(unsigned long bytes) {
    // Backward-compatible interface: route default allocations into ACTS arena
    // once the kernel allocator is initialized.
    if (g_llmk_ready) {
        return llmk_alloc_acts((UINT64)bytes, L"repl alloc");
    }
    if (heap_offset + bytes > heap_size) return NULL;
    void* ptr = heap_base + heap_offset;
    heap_offset += bytes;
    return ptr;
}

static EFI_STATUS read_exact(EFI_FILE_HANDLE file, void *dst, UINTN total_bytes) {
    UINT8 *p = (UINT8 *)dst;
    UINTN remaining = total_bytes;
    UINTN done = 0;
    UINTN next_report = 0;
    UINTN next_ui = 0;
    while (remaining > 0) {
        UINTN chunk = remaining;
        // Large reads can fail on some UEFI implementations; keep chunks modest.
        if (chunk > (16U * 1024U * 1024U)) chunk = (16U * 1024U * 1024U);
        UINTN got = chunk;
        EFI_STATUS st = uefi_call_wrapper(file->Read, 3, file, &got, p);
        if (EFI_ERROR(st)) return st;
        if (got == 0) return EFI_LOAD_ERROR;
        p += got;
        done += got;
        if (got > remaining) return EFI_LOAD_ERROR;
        remaining -= got;

        // Animated UI (cheap): update every ~8MB for large reads.
        if (total_bytes >= (64U * 1024U * 1024U)) {
            if (done >= next_ui) {
                InterfaceFx_Tick();
                InterfaceFx_ProgressBytes(done, total_bytes);
                next_ui = done + (8U * 1024U * 1024U);
            }
        }

        // Progress (avoid spamming): report every 64MB for large reads.
        if (total_bytes >= (128U * 1024U * 1024U)) {
            if (done >= next_report) {
                UINTN mb_done = done / (1024U * 1024U);
                UINTN mb_total = total_bytes / (1024U * 1024U);
                if (g_boot_verbose) {
                    Print(L"  Reading weights... %d / %d MB\r\n", (int)mb_done, (int)mb_total);
                }
                next_report = done + (64U * 1024U * 1024U);
            }
        }
    }
    return EFI_SUCCESS;
}

// ============================================================================
// BEST-EFFORT DUMP TO FILE (UTF-16LE)
// ============================================================================

static EFI_STATUS llmk_open_text_file(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = uefi_call_wrapper(g_root->Open, 5, g_root, &f, (CHAR16 *)name,
                                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st)) return st;

    // Truncate to 0 and write BOM.
    uefi_call_wrapper(f->SetPosition, 2, f, 0);
    UINT16 bom = 0xFEFF;
    UINTN nb = sizeof(bom);
    uefi_call_wrapper(f->Write, 3, f, &nb, &bom);

    *out = f;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_open_read_file(EFI_FILE_HANDLE *out, const CHAR16 *name) {
    if (!out) return EFI_INVALID_PARAMETER;
    *out = NULL;
    if (!g_root || !name) return EFI_NOT_READY;

    // Use FAT83 fallback to tolerate flaky long filename opens.
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, name, &f, NULL, 0, L"open_read_file");
    if (EFI_ERROR(st) || !f) return st;
    *out = f;
    return EFI_SUCCESS;
}

static int llmk_cfg_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char llmk_cfg_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static void llmk_cfg_trim(char **s) {
    if (!s || !*s) return;
    char *p = *s;
    while (llmk_cfg_is_space(*p)) p++;
    *s = p;
    char *end = p;
    while (*end) end++;
    while (end > p && llmk_cfg_is_space(end[-1])) end--;
    *end = 0;
}

static int llmk_cfg_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (llmk_cfg_tolower(*a) != llmk_cfg_tolower(*b)) return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static int llmk_read_cfg_model_best_effort(EFI_FILE_HANDLE Root, CHAR16 *out, int out_cap) {
    if (!out || out_cap <= 0) return 0;
    out[0] = 0;
    if (!Root) return 0;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, L"repl.cfg", &f, NULL, 0, L"cfg_open");
    if (EFI_ERROR(st) || !f) return 0;

    char buf[2048];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return 0;
    buf[sz] = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0 || val[0] == 0) continue;

        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "model") || llmk_cfg_streq_ci(key, "model_file") || llmk_cfg_streq_ci(key, "weights")) {
            ascii_to_char16(out, val, out_cap);
            // Trim again (defensive against trailing spaces).
            while (out[0] == L' ' || out[0] == L'\t') {
                for (int i = 0; i + 1 < out_cap; i++) out[i] = out[i + 1];
            }
            return (out[0] != 0);
        }
    }

    return 0;
}

static void llmk_load_repl_cfg_boot_best_effort(void) {
    // Best-effort: read repl.cfg early and pick only boot verbosity keys.
    // Supported keys:
    //   boot_verbose=0/1/2   (2 enables extra debug)
    //   boot_quiet=0/1  (inverse of boot_verbose)
    //   boot_logo=0/1
    //   boot_diag=0/1  (show system diagnostics: GOP/RAM/CPU/models)
    //   gguf_q8_blob=0/1  (enable/disable Q8_0 blob mode)
    //   q8_act_quant=0/1/2  (Q8 activation quantization mode)
    //   fat83_force=0/1 (test/diag: prefer FAT 8.3 alias opens)
    //   oo_enable=0/1 (OO v0: write oostate.bin + append oojour.log)
    //   oo_min_total_mb=<int> (OO M3: override Zone-B total minimum, in MB; 0 disables floor)
    //   oo_plan_enable=0/1 (OO M7.2: enable bounded multi-step auto-plan)
    //   oo_plan_max_actions=1..3 (OO M7.2: max auto-applies per boot window)
    //   oo_net=0/1 (OO M4: network read-only tick placeholder; never required)
    //   oo_manifest_url=<string> (OO M4: URL hint for a signed manifest fetch; placeholder)
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st) || !f) return;

    char buf[2048];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    char *p = buf;
    while (*p) {
        // Extract a line.
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR.
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        // Skip comments/empty.
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;

        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "boot_verbose") || llmk_cfg_streq_ci(key, "verbose_boot")) {
            int mode;
            if (llmk_cfg_parse_i32(val, &mode)) {
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                g_boot_verbose = mode;
            } else {
                int b;
                if (llmk_cfg_parse_bool(val, &b)) {
                    g_boot_verbose = (b != 0) ? 1 : 0;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "boot_quiet") || llmk_cfg_streq_ci(key, "quiet_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_boot_verbose = (b != 0) ? 0 : 1;
            }
        } else if (llmk_cfg_streq_ci(key, "boot_logo") || llmk_cfg_streq_ci(key, "logo_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_boot_logo = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "boot_diag") || llmk_cfg_streq_ci(key, "diag")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_boot_diag = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "gguf_q8_blob") || llmk_cfg_streq_ci(key, "q8_blob") || llmk_cfg_streq_ci(key, "gguf_blob")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_gguf_q8_blob = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "q8_act_quant") || llmk_cfg_streq_ci(key, "q8_act_quantize") || llmk_cfg_streq_ci(key, "q8_x_quant")) {
            int mode;
            if (llmk_cfg_parse_i32(val, &mode)) {
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                g_cfg_q8_act_quant = mode;
            } else {
                int b;
                if (llmk_cfg_parse_bool(val, &b)) {
                    g_cfg_q8_act_quant = (b != 0) ? 1 : 0;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "model_picker") || llmk_cfg_streq_ci(key, "model_menu")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_model_picker = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "ctx_len") || llmk_cfg_streq_ci(key, "context") || llmk_cfg_streq_ci(key, "context_len")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                g_cfg_ctx_len = v;
            }
        } else if (llmk_cfg_streq_ci(key, "fat83_force") || llmk_cfg_streq_ci(key, "force_fat83") || llmk_cfg_streq_ci(key, "fat83_prefer")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_fat83_force = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_enable") || llmk_cfg_streq_ci(key, "oo") || llmk_cfg_streq_ci(key, "organism")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_enable = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_min_total_mb") || llmk_cfg_streq_ci(key, "oo_zones_min_total_mb") || llmk_cfg_streq_ci(key, "oo_min_total")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < -1) v = -1;
                g_cfg_oo_min_total_mb = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_llm_consult") || llmk_cfg_streq_ci(key, "oo_consult")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_llm_consult = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_multi_actions") || llmk_cfg_streq_ci(key, "oo_multi")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_multi_actions = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_auto_apply") || llmk_cfg_streq_ci(key, "oo_auto")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                g_cfg_oo_auto_apply = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_plan_enable") || llmk_cfg_streq_ci(key, "oo_plan") || llmk_cfg_streq_ci(key, "oo_multi_plan")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_plan_enable = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_plan_max_actions") || llmk_cfg_streq_ci(key, "oo_plan_max") || llmk_cfg_streq_ci(key, "oo_max_actions")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > 3) v = 3;
                g_cfg_oo_plan_max_actions = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_consult_log") || llmk_cfg_streq_ci(key, "oo_log")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_consult_log = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_conf_gate") || llmk_cfg_streq_ci(key, "oo_confidence_gate") || llmk_cfg_streq_ci(key, "oo_conf_gate_enable")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_conf_gate = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_conf_threshold") || llmk_cfg_streq_ci(key, "oo_confidence_threshold")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                g_cfg_oo_conf_threshold = v;
            }
        } else if (llmk_cfg_streq_ci(key, "oo_net") || llmk_cfg_streq_ci(key, "oo_net_enable") || llmk_cfg_streq_ci(key, "oo_network")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_oo_net = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_manifest_url") || llmk_cfg_streq_ci(key, "oo_manifest") || llmk_cfg_streq_ci(key, "oo_manifest_uri")) {
            llmk_cfg_copy_ascii_token(g_cfg_oo_manifest_url, (int)sizeof(g_cfg_oo_manifest_url), val);
        }
    }
}

static void llmk_print_logo(void) {
    // Keep it ASCII-only (UEFI consoles vary) and within ~80 cols.
    Print(L"\r\n");
    Print(L" _      _      __  __              _        _ \r\n");
    Print(L"| |    | |    |  \\/  |            | |      | |\r\n");
    Print(L"| |    | |    | \\  / |  __ _  ___ | |_ __ _| |\r\n");
    Print(L"| |    | |    | |\\/| | / _` |/ __|| __/ _` | |\r\n");
    Print(L"| |____| |____| |  | || (_| |\\__ \\| || (_| | |\r\n");
    Print(L"|______|______|_|  |_| \\__,_||___/ \\__\\__,_|_|\r\n");
    Print(L"             Baremetal UEFI Chat REPL\r\n\r\n");

    // Serial-visible marker (keeps automated logs honest).
    llmk_serial_write_char16(L"[logo] printed\r\n");
}

static int llmk_cfg_parse_u64(const char *s, UINT64 *out) {
    if (!s || !out) return 0;
    UINT64 v = 0;
    int any = 0;
    while (*s && llmk_cfg_is_space(*s)) s++;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10ULL + (UINT64)(*s - '0');
        s++;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

static int llmk_cfg_parse_i32(const char *s, int *out) {
    if (!s || !out) return 0;
    int sign = 1;
    while (*s && llmk_cfg_is_space(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    int v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10 + (int)(*s - '0');
        s++;
    }
    if (!any) return 0;
    *out = v * sign;
    return 1;
}

static int llmk_cfg_parse_f32(const char *s, float *out) {
    if (!s || !out) return 0;
    int sign = 1;
    while (*s && llmk_cfg_is_space(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    float val = 0.0f;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        val = val * 10.0f + (float)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        float frac = 0.1f;
        while (*s >= '0' && *s <= '9') {
            any = 1;
            val += (float)(*s - '0') * frac;
            frac *= 0.1f;
            s++;
        }
    }
    if (!any) return 0;
    *out = val * (float)sign;
    return 1;
}

static int llmk_cfg_parse_bool(const char *s, int *out) {
    if (!s || !out) return 0;
    while (*s && llmk_cfg_is_space(*s)) s++;
    if (llmk_cfg_streq_ci(s, "1") || llmk_cfg_streq_ci(s, "true") || llmk_cfg_streq_ci(s, "on") || llmk_cfg_streq_ci(s, "yes")) {
        *out = 1;
        return 1;
    }
    if (llmk_cfg_streq_ci(s, "0") || llmk_cfg_streq_ci(s, "false") || llmk_cfg_streq_ci(s, "off") || llmk_cfg_streq_ci(s, "no")) {
        *out = 0;
        return 1;
    }
    int iv = 0;
    if (llmk_cfg_parse_i32(s, &iv)) {
        *out = (iv != 0);
        return 1;
    }
    return 0;
}

static void llmk_cfg_copy_ascii_token(char *dst, int cap, const char *src);

static void llmk_load_repl_cfg_best_effort(
    float *temperature,
    float *min_p,
    float *top_p,
    int *top_k,
    float *repeat_penalty,
    int *no_repeat_ngram,
    int *max_gen_tokens,
    int *stats_enabled,
    int *stop_on_you,
    int *stop_on_double_nl
) {
    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    int applied = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR and whitespace.
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        // key=value
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;

        // Lowercase key in-place (ASCII).
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "temp") || llmk_cfg_streq_ci(key, "temperature")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                *temperature = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "min_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                *min_p = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "top_p")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                *top_p = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "top_k")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 256) v = 256;
                *top_k = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "repeat") || llmk_cfg_streq_ci(key, "repeat_penalty")) {
            float v;
            if (llmk_cfg_parse_f32(val, &v)) {
                if (v <= 0.0f) v = 1.0f;
                *repeat_penalty = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "norepeat") || llmk_cfg_streq_ci(key, "no_repeat_ngram")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 16) v = 16;
                *no_repeat_ngram = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "max_tokens")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 1) v = 1;
                if (v > MAX_TOKENS) v = MAX_TOKENS;
                *max_gen_tokens = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "stats")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stats_enabled = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "stop_you")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stop_on_you = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "stop_nl")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                *stop_on_double_nl = b;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "seed")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                set_seed((unsigned int)v);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "budget")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                g_budget_prefill_cycles = v;
                g_budget_decode_cycles = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "budget_prefill")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                g_budget_prefill_cycles = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "budget_decode")) {
            UINT64 v;
            if (llmk_cfg_parse_u64(val, &v)) {
                g_budget_decode_cycles = v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "strict_budget")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_sentinel.cfg.strict_budget = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "attn")) {
            if (llmk_cfg_streq_ci(val, "auto")) {
                g_attn_force = -1;
                applied = 1;
            } else if (llmk_cfg_streq_ci(val, "sse2")) {
                g_attn_force = 0;
                applied = 1;
            } else if (llmk_cfg_streq_ci(val, "avx2")) {
                if (g_attn_use_avx2) {
                    g_attn_force = 1;
                    applied = 1;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "autorun_autostart") || llmk_cfg_streq_ci(key, "autorun")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_autorun_autostart = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autorun_shutdown") || llmk_cfg_streq_ci(key, "autorun_shutdown_when_done")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_autorun_shutdown_when_done = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "autorun_file") || llmk_cfg_streq_ci(key, "autorun_script")) {
            if (val && val[0]) {
                ascii_to_char16(g_cfg_autorun_file, val, (int)(sizeof(g_cfg_autorun_file) / sizeof(g_cfg_autorun_file[0])));
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "gguf_q8_blob") || llmk_cfg_streq_ci(key, "q8_blob") || llmk_cfg_streq_ci(key, "gguf_blob")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                g_cfg_gguf_q8_blob = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "q8_act_quant") || llmk_cfg_streq_ci(key, "q8_act_quantize") || llmk_cfg_streq_ci(key, "q8_x_quant")) {
            int mode;
            if (llmk_cfg_parse_i32(val, &mode)) {
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                g_cfg_q8_act_quant = mode;
                applied = 1;
            } else {
                int b;
                if (llmk_cfg_parse_bool(val, &b)) {
                    g_cfg_q8_act_quant = (b != 0) ? 1 : 0;
                    applied = 1;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "chat_format") || llmk_cfg_streq_ci(key, "prompt_format")) {
            char fmt[32];
            llmk_cfg_copy_ascii_token(fmt, (int)sizeof(fmt), val);
            if (llmk_cfg_streq_ci(fmt, "you_ai") || llmk_cfg_streq_ci(fmt, "you")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_YOU_AI;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "llama2") || llmk_cfg_streq_ci(fmt, "llama2_chat")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_LLAMA2;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "chatml") || llmk_cfg_streq_ci(fmt, "qwen") || llmk_cfg_streq_ci(fmt, "qwen2")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_CHATML;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "alpaca") || llmk_cfg_streq_ci(fmt, "instruction")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_ALPACA;
                applied = 1;
            } else if (llmk_cfg_streq_ci(fmt, "raw")) {
                g_cfg_chat_format = LLMK_CHAT_FMT_RAW;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "system_prompt") || llmk_cfg_streq_ci(key, "system")) {
            llmk_cfg_copy_ascii_token(g_cfg_system_prompt,
                                      (int)sizeof(g_cfg_system_prompt),
                                      val);
            applied = 1;
        }
    }

    if (applied) {
        g_cfg_loaded = 1;
        if (g_boot_verbose) {
            Print(L"[cfg] repl.cfg loaded\r\n");
        }
    }
}

static void llmk_cfg_copy_ascii_token(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;

    while (*src && llmk_cfg_is_space(*src)) src++;
    int quoted = 0;
    if (*src == '"') {
        quoted = 1;
        src++;
    }

    int p = 0;
    for (const char *s = src; *s && p + 1 < cap; s++) {
        if (quoted && *s == '"') break;
        char c = *s;
        if (c == '\r' || c == '\n') break;
        if ((unsigned char)c < 0x20) c = ' ';
        dst[p++] = c;
    }
    dst[p] = 0;

    while (p > 0 && llmk_cfg_is_space(dst[p - 1])) dst[--p] = 0;
}

static const char *llmk_chat_format_name_ascii(int fmt) {
    switch (fmt) {
        case LLMK_CHAT_FMT_LLAMA2: return "llama2";
        case LLMK_CHAT_FMT_CHATML: return "chatml";
        case LLMK_CHAT_FMT_ALPACA: return "alpaca";
        case LLMK_CHAT_FMT_RAW: return "raw";
        case LLMK_CHAT_FMT_YOU_AI:
        default: return "you_ai";
    }
}

static int llmk_prompt_append(char *dst, int cap, int p, const char *s) {
    if (!dst || cap <= 0) return p;
    if (!s) return p;
    while (*s && p + 1 < cap) {
        dst[p++] = *s++;
    }
    dst[p] = 0;
    return p;
}

static const char *llmk_build_chat_prompt(char *out, int cap, const char *user, int kv_pos) {
    if (!out || cap <= 0 || !user) return user;

    if (g_cfg_chat_format == LLMK_CHAT_FMT_RAW) {
        return user;
    }

    out[0] = 0;
    int p = 0;

    if (g_cfg_chat_format == LLMK_CHAT_FMT_YOU_AI) {
        const char *pre = (kv_pos == 0) ? "You: " : "\nYou: ";
        p = llmk_prompt_append(out, cap, p, pre);
        p = llmk_prompt_append(out, cap, p, user);
        p = llmk_prompt_append(out, cap, p, "\nAI: ");
        return out;
    }

    if (g_cfg_chat_format == LLMK_CHAT_FMT_LLAMA2) {
        if (kv_pos == 0 && g_cfg_system_prompt[0]) {
            p = llmk_prompt_append(out, cap, p, "[INST] <<SYS>>\n");
            p = llmk_prompt_append(out, cap, p, g_cfg_system_prompt);
            p = llmk_prompt_append(out, cap, p, "\n<</SYS>>\n\n");
            p = llmk_prompt_append(out, cap, p, user);
            p = llmk_prompt_append(out, cap, p, " [/INST]");
        } else {
            p = llmk_prompt_append(out, cap, p, "[INST] ");
            p = llmk_prompt_append(out, cap, p, user);
            p = llmk_prompt_append(out, cap, p, " [/INST]");
        }
        return out;
    }

    if (g_cfg_chat_format == LLMK_CHAT_FMT_CHATML) {
        if (kv_pos == 0 && g_cfg_system_prompt[0]) {
            p = llmk_prompt_append(out, cap, p, "<|im_start|>system\n");
            p = llmk_prompt_append(out, cap, p, g_cfg_system_prompt);
            p = llmk_prompt_append(out, cap, p, "<|im_end|>\n");
        }
        p = llmk_prompt_append(out, cap, p, "<|im_start|>user\n");
        p = llmk_prompt_append(out, cap, p, user);
        p = llmk_prompt_append(out, cap, p, "<|im_end|>\n<|im_start|>assistant\n");
        return out;
    }

    // Alpaca-style
    if (kv_pos == 0 && g_cfg_system_prompt[0]) {
        p = llmk_prompt_append(out, cap, p, "### Instruction:\n");
        p = llmk_prompt_append(out, cap, p, g_cfg_system_prompt);
        p = llmk_prompt_append(out, cap, p, "\n\n");
    } else {
        p = llmk_prompt_append(out, cap, p, "### Instruction:\n");
    }
    p = llmk_prompt_append(out, cap, p, user);
    p = llmk_prompt_append(out, cap, p, "\n\n### Response:\n");
    return out;
}

static void llmk_load_repl_cfg_oo_best_effort(
    int *oo_autoload,
    int *oo_autosave_every,
    char *oo_file_out,
    int oo_file_cap
) {
    if (oo_autoload) *oo_autoload = 0;
    if (oo_autosave_every) *oo_autosave_every = 0;
    if (oo_file_out && oo_file_cap > 0) oo_file_out[0] = 0;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    int autosave_set = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "oo_autoload") || llmk_cfg_streq_ci(key, "oo_load_on_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (oo_autoload) *oo_autoload = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_file") || llmk_cfg_streq_ci(key, "oo_state_file") || llmk_cfg_streq_ci(key, "oo_autoload_file")) {
            if (oo_file_out && oo_file_cap > 0) {
                llmk_cfg_copy_ascii_token(oo_file_out, oo_file_cap, val);
            }
        } else if (llmk_cfg_streq_ci(key, "oo_autosave") || llmk_cfg_streq_ci(key, "oo_autosave_on")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (!autosave_set && oo_autosave_every) {
                    *oo_autosave_every = (b != 0) ? 1 : 0;
                }
            }
        } else if (llmk_cfg_streq_ci(key, "oo_autosave_every") || llmk_cfg_streq_ci(key, "oo_autosave_n")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 1000) v = 1000;
                if (oo_autosave_every) *oo_autosave_every = v;
                autosave_set = 1;
            }
        }
    }
}

static void llmk_load_repl_cfg_snap_best_effort(
    int *snap_autoload,
    char *snap_file_out,
    int snap_file_cap
) {
    if (snap_autoload) *snap_autoload = 0;
    if (snap_file_out && snap_file_cap > 0) snap_file_out[0] = 0;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "snap_autoload") || llmk_cfg_streq_ci(key, "snap_load_on_boot")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                if (snap_autoload) *snap_autoload = (b != 0);
            }
        } else if (llmk_cfg_streq_ci(key, "snap_file") || llmk_cfg_streq_ci(key, "snap_autoload_file")) {
            if (snap_file_out && snap_file_cap > 0) {
                llmk_cfg_copy_ascii_token(snap_file_out, snap_file_cap, val);
            }
        }
    }
}

static void llmk_load_repl_cfg_djibion_best_effort(DjibionEngine *e) {
    if (!e) return;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_read_file(&f, L"repl.cfg");
    if (EFI_ERROR(st)) return;

    char buf[4096];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(f->Read, 3, f, &sz, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if (EFI_ERROR(st) || sz == 0) return;
    buf[sz] = 0;

    int applied = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;
        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (llmk_cfg_streq_ci(key, "djibion_mode")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                djibion_set_mode(e, (DjibionMode)v);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_prefix") || llmk_cfg_streq_ci(key, "djibion_fs_prefix") || llmk_cfg_streq_ci(key, "fs_mut_prefix")) {
            if (val && val[0]) {
                llmk_cfg_copy_ascii_token(e->laws.fs_mut_prefix, (int)sizeof(e->laws.fs_mut_prefix), val);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_delete") || llmk_cfg_streq_ci(key, "djibion_allow_fs_delete")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_fs_delete = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_write") || llmk_cfg_streq_ci(key, "djibion_allow_fs_write")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_fs_write = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_snap_load")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_snap_load = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_snap_save")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_snap_save = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_cfg_write") || llmk_cfg_streq_ci(key, "djibion_allow_config_write")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_cfg_write = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_max_write") || llmk_cfg_streq_ci(key, "djibion_max_fs_write_bytes")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > (1024 * 1024)) v = (1024 * 1024);
                e->laws.max_fs_write_bytes = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_max_snap") || llmk_cfg_streq_ci(key, "djibion_max_snap_bytes")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > (1024 * 1024 * 1024)) v = (1024 * 1024 * 1024);
                e->laws.max_snap_bytes = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_max_oo") || llmk_cfg_streq_ci(key, "djibion_max_oo_cycles")) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = 0;
                if (v > 64) v = 64;
                e->laws.max_oo_cycles = (UINT32)v;
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_autorun")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_autorun = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_oo_persist")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_oo_persist = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_oo_exec")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_oo_exec = (b != 0);
                applied = 1;
            }
        } else if (llmk_cfg_streq_ci(key, "djibion_allow_oo_auto")) {
            int b;
            if (llmk_cfg_parse_bool(val, &b)) {
                e->laws.allow_oo_auto = (b != 0);
                applied = 1;
            }
        }
    }

    if (applied) {
        Print(L"[cfg] djibion: mode=%s prefix=",
              (CHAR16 *)djibion_mode_name(e->mode));
        if (e->laws.fs_mut_prefix[0]) {
            llmk_print_ascii(e->laws.fs_mut_prefix);
        } else {
            Print(L"(none)");
        }
        Print(L"\r\n");
    }
}

static int llmk_cfg_line_has_key_ci(const char *line, const char *key) {
    if (!line || !key) return 0;

    const char *p = line;
    while (*p && llmk_cfg_is_space(*p)) p++;

    // Allow commented-out keys like: # snap_autoload=1
    if (*p == '#' || *p == ';') {
        p++;
        while (*p && llmk_cfg_is_space(*p)) p++;
    }

    // Parse key token up to '=' or whitespace.
    char kbuf[64];
    int kp = 0;
    while (*p && *p != '=' && !llmk_cfg_is_space(*p) && *p != '#' && *p != ';') {
        if (kp + 1 < (int)sizeof(kbuf)) {
            kbuf[kp++] = llmk_cfg_tolower(*p);
        }
        p++;
    }
    kbuf[kp] = 0;
    if (kbuf[0] == 0) return 0;

    // Skip spaces before '='.
    while (*p && llmk_cfg_is_space(*p)) p++;
    if (*p != '=') return 0;

    return llmk_cfg_streq_ci(kbuf, key);
}

static void llmk_cfg_out_append(char *out, int *op, int out_cap, const char *s) {
    if (!out || !op || out_cap <= 0 || !s) return;
    int p = *op;
    while (*s && p + 1 < out_cap) out[p++] = *s++;
    out[p] = 0;
    *op = p;
}

// Forward decl: used by OO auto-apply verification helpers.
static EFI_STATUS llmk_repl_cfg_set_kv_best_effort(const char *key, const char *val);

static int llmk_repl_cfg_read_ctx_seq_best_effort(int *out_ctx, int *out_seq) {
    if (out_ctx) *out_ctx = 0;
    if (out_seq) *out_seq = 0;
    if (!g_root) return 0;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"repl.cfg", &raw, &raw_len);
    if (EFI_ERROR(st) || !raw || raw_len == 0) {
        if (raw) uefi_call_wrapper(BS->FreePool, 1, raw);
        return 0;
    }

    // Make NUL-terminated ASCII buffer.
    char *buf = NULL;
    EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, raw_len + 1, (void **)&buf);
    if (EFI_ERROR(st2) || !buf) {
        uefi_call_wrapper(BS->FreePool, 1, raw);
        return 0;
    }
    CopyMem(buf, raw, raw_len);
    buf[raw_len] = 0;
    uefi_call_wrapper(BS->FreePool, 1, raw);

    int got_ctx = 0;
    int got_seq = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        // Trim CR.
        for (char *c = line; *c; c++) {
            if (*c == '\r') { *c = 0; break; }
        }

        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        // Strip inline comment.
        for (char *c = line; *c; c++) {
            if (*c == '#') { *c = 0; break; }
        }
        llmk_cfg_trim(&line);
        if (line[0] == 0) continue;

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        llmk_cfg_trim(&key);
        llmk_cfg_trim(&val);
        if (key[0] == 0) continue;

        for (char *k = key; *k; k++) *k = llmk_cfg_tolower(*k);

        if (!got_ctx && (llmk_cfg_streq_ci(key, "ctx_len") || llmk_cfg_streq_ci(key, "context") || llmk_cfg_streq_ci(key, "context_len"))) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                if (out_ctx) *out_ctx = v;
                got_ctx = 1;
            }
        } else if (!got_seq && (llmk_cfg_streq_ci(key, "seq_len") || llmk_cfg_streq_ci(key, "sequence") || llmk_cfg_streq_ci(key, "sequence_len"))) {
            int v;
            if (llmk_cfg_parse_i32(val, &v)) {
                if (v < 0) v = -v;
                if (out_seq) *out_seq = v;
                got_seq = 1;
            }
        }

        if (got_ctx && got_seq) break;
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
    return (got_ctx || got_seq);
}

static UINT64 llmk_oo_cfg_checksum_i64(int ctx_len, int seq_len, UINT64 ram_mb) {
    // Spec (M5.2): checksum = ctx_len XOR seq_len XOR (ram_mb << 8)
    UINT64 c = ((UINT64)((UINT32)ctx_len) ^ (UINT64)((UINT32)seq_len));
    c ^= (ram_mb << 8);
    return c;
}

static void llmk_oo_journal_event_load_state_best_effort(const char *event) {
    if (!g_root || !event) return;
    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) return;
    llmk_oo_journal_append_best_effort(&s, event);
}

static void llmk_oo_plan_checkpoint_best_effort(const char *tag) {
    if (!g_root) return;

    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) return;

    s.magic = LLMK_OO_STATE_MAGIC;
    s.version = LLMK_OO_STATE_VER;
    s.size = (UINT32)sizeof(LlmkOoState);
    s.checksum = llmk_oo_state_checksum(&s);

    (void)llmk_oo_write_recovery_best_effort(&s);

    char event[96];
    int p = 0;
    event[0] = 0;
    llmk_ascii_append_str(event, (int)sizeof(event), &p, "plan_checkpoint tag=");
    llmk_ascii_append_str(event, (int)sizeof(event), &p, tag ? tag : "default");
    event[p] = 0;

    llmk_oo_journal_append_best_effort(&s, event);
}

static void llmk_oo_record_last_auto_apply_best_effort(UINT64 boot_count, UINT32 apply_mode, UINT32 action_id) {
    if (!g_root) return;
    LlmkOoState s;
    if (!llmk_oo_load_state_best_effort(&s)) return;

    // meta: low6=action_id, high2=apply_mode
    UINT32 meta = ((apply_mode & 0x3u) << 6u) | (action_id & 0x3Fu);
    UINT32 boot_low8 = (UINT32)(boot_count & 0xFFu);

    s.flags = llmk_oo_set_last_action_meta(s.flags, meta);
    s.flags = llmk_oo_set_last_apply_boot_low8(s.flags, boot_low8);

    s.magic = LLMK_OO_STATE_MAGIC;
    s.version = LLMK_OO_STATE_VER;
    s.size = (UINT32)sizeof(LlmkOoState);
    s.checksum = llmk_oo_state_checksum(&s);
    EFI_STATUS wst = llmk_oo_write_state_best_effort(&s);
    if (!EFI_ERROR(wst)) {
        llmk_oo_write_recovery_best_effort(&s);
    }

    {
        const char *expected = llmk_oo_action_is_increase(action_id) ? "mode_stable" : "mode_drop";
        (void)apply_mode;
        llmk_oo_outcome_append_best_effort(boot_count,
                                           action_id,
                                           expected,
                                           "pending_next_boot",
                                           -1);
    }
}

static int llmk_oo_auto_apply_write_verify_best_effort(const char *action,
                                                      const char *key,
                                                      int old_ctx_hint,
                                                      int old_seq_hint,
                                                      int expected_ctx,
                                                      int expected_seq,
                                                      UINT64 ram_mb) {
    if (!action || !key) return 0;

    // Read current values from repl.cfg when available.
    int ctx_before = old_ctx_hint;
    int seq_before = old_seq_hint;
    {
        int rc = 0;
        int rs = 0;
        if (llmk_repl_cfg_read_ctx_seq_best_effort(&rc, &rs)) {
            if (rc > 0) ctx_before = rc;
            if (rs > 0) seq_before = rs;
        }
    }

    UINT64 c_before = llmk_oo_cfg_checksum_i64(ctx_before, seq_before, ram_mb);

    // Apply: write the intended key.
    {
        char val[32];
        int vp = 0;
        int v = 0;
        if (llmk_cfg_streq_ci(key, "ctx_len")) v = expected_ctx;
        else if (llmk_cfg_streq_ci(key, "seq_len")) v = expected_seq;
        else return 0;
        llmk_ascii_append_u64(val, (int)sizeof(val), &vp, (UINT64)v);
        val[vp] = 0;
        EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort(key, val);
        if (EFI_ERROR(st)) return 0;
    }

    // Re-read + verify.
    int ctx_after = 0;
    int seq_after = 0;
    if (!llmk_repl_cfg_read_ctx_seq_best_effort(&ctx_after, &seq_after)) {
        return 0;
    }

    // Fill missing key from expected (best-effort) so checksum check is meaningful.
    if (ctx_after <= 0) ctx_after = expected_ctx;
    if (seq_after <= 0) seq_after = expected_seq;

    // Range checks (spec guidance: ctx_len ∈ [16,4096] etc).
    if (ctx_after < 16 || ctx_after > 4096) return 0;
    if (seq_after < 16 || seq_after > 4096) return 0;

    // Ensure the modified key matches the expected value.
    if (llmk_cfg_streq_ci(key, "ctx_len") && ctx_after != expected_ctx) return 0;
    if (llmk_cfg_streq_ci(key, "seq_len") && seq_after != expected_seq) return 0;

    UINT64 c_after = llmk_oo_cfg_checksum_i64(ctx_after, seq_after, ram_mb);
    UINT64 c_expected = llmk_oo_cfg_checksum_i64(expected_ctx, expected_seq, ram_mb);

    // Verification checks: checksum matches expected post-state and changed.
    if (c_after != c_expected) return 0;
    (void)c_before; // currently used only as a spec-aligned pre-computation

    return 1;
}

static EFI_STATUS llmk_repl_cfg_set_kv_best_effort(const char *key, const char *val) {
    if (!key || !val) return EFI_INVALID_PARAMETER;
    if (!g_root) return EFI_NOT_READY;

    void *raw = NULL;
    UINTN raw_len = 0;
    EFI_STATUS read_st = llmk_read_entire_file_best_effort(L"repl.cfg", &raw, &raw_len);

    // Make a mutable NUL-terminated copy (ASCII).
    char *in = NULL;
    UINTN in_len = 0;
    if (!EFI_ERROR(read_st) && raw && raw_len > 0) {
        in_len = raw_len;
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)(in_len + 1), (void **)&in);
        if (EFI_ERROR(st2) || !in) {
            uefi_call_wrapper(BS->FreePool, 1, raw);
            return EFI_OUT_OF_RESOURCES;
        }
        CopyMem(in, raw, in_len);
        in[in_len] = 0;
        uefi_call_wrapper(BS->FreePool, 1, raw);
        raw = NULL;
        raw_len = 0;
    } else {
        // Missing/empty file: start fresh.
        const char *stub = "# repl.cfg (generated best-effort)\r\n";
        in_len = (UINTN)my_strlen(stub);
        EFI_STATUS st2 = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)(in_len + 1), (void **)&in);
        if (EFI_ERROR(st2) || !in) return EFI_OUT_OF_RESOURCES;
        CopyMem(in, stub, in_len);
        in[in_len] = 0;
        read_st = EFI_NOT_FOUND;
    }

    int out_cap = (int)in_len + 512;
    if (out_cap < 1024) out_cap = 1024;
    if (out_cap > 64 * 1024) out_cap = 64 * 1024;

    char *out = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, (UINTN)out_cap, (void **)&out);
    if (EFI_ERROR(st) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, in);
        return EFI_OUT_OF_RESOURCES;
    }
    int op = 0;
    out[0] = 0;

    int replaced = 0;
    char *p = in;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int had_nl = (*p == '\n');
        if (had_nl) { *p = 0; p++; }

        // Strip trailing CR for parsing/writing.
        int ll = (int)my_strlen(line);
        if (ll > 0 && line[ll - 1] == '\r') line[ll - 1] = 0;

        if (llmk_cfg_line_has_key_ci(line, key)) {
            llmk_cfg_out_append(out, &op, out_cap, key);
            llmk_cfg_out_append(out, &op, out_cap, "=");
            llmk_cfg_out_append(out, &op, out_cap, val);
            llmk_cfg_out_append(out, &op, out_cap, "\r\n");
            replaced = 1;
        } else {
            llmk_cfg_out_append(out, &op, out_cap, line);
            llmk_cfg_out_append(out, &op, out_cap, "\r\n");
        }

        if (!had_nl) break;
    }

    if (!replaced) {
        // Ensure trailing newline then append key.
        if (op >= 2 && !(out[op - 2] == '\r' && out[op - 1] == '\n')) {
            llmk_cfg_out_append(out, &op, out_cap, "\r\n");
        }
        llmk_cfg_out_append(out, &op, out_cap, key);
        llmk_cfg_out_append(out, &op, out_cap, "=");
        llmk_cfg_out_append(out, &op, out_cap, val);
        llmk_cfg_out_append(out, &op, out_cap, "\r\n");
    }

    // Backup previous file when it existed.
    if (!EFI_ERROR(read_st)) {
        CHAR16 bak[64];
        llmk_make_bak_name(L"repl.cfg", bak, (int)(sizeof(bak) / sizeof(bak[0])));
        llmk_copy_file_best_effort(L"repl.cfg", bak);
    }

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"repl.cfg");
    if (EFI_ERROR(st) || !f) {
        uefi_call_wrapper(BS->FreePool, 1, out);
        uefi_call_wrapper(BS->FreePool, 1, in);
        return st;
    }

    UINTN nb = (UINTN)my_strlen(out);
    st = llmk_file_write_bytes(f, out, nb);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, in);

    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_oo_save_to_file_best_effort(const CHAR16 *name, int *out_bytes) {
    if (out_bytes) *out_bytes = 0;
    if (!name) return EFI_INVALID_PARAMETER;

    char *blob = (char *)simple_alloc(32768);
    if (!blob) return EFI_OUT_OF_RESOURCES;

    int n = llmk_oo_export(blob, 32768);
    if (n < 0) return EFI_BUFFER_TOO_SMALL;

    EFI_FILE_HANDLE f = NULL;
    EFI_STATUS st = llmk_open_binary_file(&f, name);
    if (EFI_ERROR(st)) return st;

    st = llmk_file_write_bytes(f, blob, (UINTN)n);
    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
    uefi_call_wrapper(f->Close, 1, f);
    if (out_bytes) *out_bytes = n;
    if (EFI_ERROR(st)) return st;
    if (EFI_ERROR(flush_st)) return flush_st;
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_file_write_u16(EFI_FILE_HANDLE f, const CHAR16 *s) {
    if (!f || !s) return EFI_INVALID_PARAMETER;
    UINTN chars = (UINTN)StrLen((CHAR16 *)s);
    UINTN nb = chars * sizeof(CHAR16);
    if (nb == 0) return EFI_SUCCESS;
    return uefi_call_wrapper(f->Write, 3, f, &nb, (void *)s);
}

static EFI_STATUS llmk_dump_zones_to_file(EFI_FILE_HANDLE f, const LlmkZones *zones) {
    if (!f || !zones) return EFI_INVALID_PARAMETER;
    CHAR16 line[256];
    SPrint(line, sizeof(line), L"[llmk] Zone B: base=0x%lx size=%lu MiB\r\n",
           (UINT64)zones->zone_b_base, zones->zone_b_size / (1024ULL * 1024ULL));
    llmk_file_write_u16(f, line);
    for (int i = 0; i < LLMK_ARENA_COUNT; i++) {
        const LlmkArena *a = &zones->arenas[i];
        SPrint(line, sizeof(line), L"  [%s] base=0x%lx size=%lu MiB used=%lu MiB flags=0x%x\r\n",
               a->name,
               a->base,
               a->size / (1024ULL * 1024ULL),
               a->cursor / (1024ULL * 1024ULL),
               (unsigned)a->flags);
        llmk_file_write_u16(f, line);
    }
    llmk_file_write_u16(f, L"\r\n");
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_dump_sentinel_to_file(EFI_FILE_HANDLE f, const LlmkSentinel *s) {
    if (!f || !s) return EFI_INVALID_PARAMETER;
    CHAR16 line[256];
    SPrint(line, sizeof(line), L"[llmk][sentinel] enabled=%d strict=%d max_cycles=%lu last_err=%d reason=%s\r\n\r\n",
           s->cfg.enabled ? 1 : 0,
           s->cfg.strict_mode ? 1 : 0,
           s->cfg.max_cycles,
           (int)s->last_error,
           s->last_reason);
    llmk_file_write_u16(f, line);
    return EFI_SUCCESS;
}

static EFI_STATUS llmk_dump_log_to_file(EFI_FILE_HANDLE f, const LlmkLog *log, UINT32 max_entries) {
    if (!f || !log || !log->entries || log->capacity == 0) return EFI_INVALID_PARAMETER;

    UINT32 n = log->capacity;
    if (max_entries != 0 && max_entries < n) n = max_entries;

    CHAR16 line[256];
    SPrint(line, sizeof(line), L"[llmk][log] last %u events (ring cap=%u)\r\n", n, log->capacity);
    llmk_file_write_u16(f, line);

    UINT32 w = log->write_idx;
    for (UINT32 i = 0; i < n; i++) {
        UINT32 off = (w + log->capacity - 1 - i) % log->capacity;
        const LlmkLogEntry *e = &log->entries[off];
        if (e->tsc == 0 && e->code == 0 && e->msg[0] == 0) continue;
        SPrint(line, sizeof(line), L"  #%u tsc=%lu code=%u arena=%d ptr=0x%lx size=%lu msg=%s\r\n",
               i, e->tsc, e->code, e->arena, e->ptr, e->size, e->msg);
        llmk_file_write_u16(f, line);
    }
    llmk_file_write_u16(f, L"\r\n");
    return EFI_SUCCESS;
}

// ============================================================================
// MATH FUNCTIONS
// ============================================================================

float fast_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float xhalf = 0.5f * x;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - xhalf * x * x);
    x = x * (1.5f - xhalf * x * x);
    return 1.0f / x;
}

float fast_exp(float x) {
    if (x < -10.0f) return 0.0f;
    if (x > 10.0f) return 22026.0f;
    x = 1.0f + x / 256.0f;
    x *= x; x *= x; x *= x; x *= x;
    x *= x; x *= x; x *= x; x *= x;
    return x;
}

int my_strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == 0) return 0;
    }
    return 0;
}

// ============================================================================
// TRANSFORMER OPERATIONS
// ============================================================================

void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / fast_sqrt(ss);
    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

void matmul(float* xout, float* x, float* w, int n, int d) {
    // DjibLAS computes (column-major): C(m×n) = A(k×m)^T · B(k×n)
    // We want (row-major weights): xout(d) = W(d×n) · x(n)
    // Trick: W(d×n) row-major has the same memory layout as B(k×n_out)
    // column-major when k=n and n_out=d (because W[i*n + l] == B[l + k*i]).
    // Use A = x as a (k×1) column-major matrix.
    // Result C is (1×d) column-major, so it lands contiguous into xout.
    djiblas_sgemm_f32(
        /*m=*/1, /*n=*/d, /*k=*/n,
        /*A=*/x, /*lda=*/n,
        /*B=*/w, /*ldb=*/n,
        /*C=*/xout, /*ldc=*/1
    );
}

static UINT16 llmk_read_u16_unaligned(const void *p) {
    const UINT8 *b = (const UINT8 *)p;
    return (UINT16)((UINT16)b[0] | ((UINT16)b[1] << 8));
}

// IEEE-754 half -> float32. Handles normals/denormals/inf/nan.
static float llmk_fp16_to_fp32(UINT16 h) {
    UINT32 sign = (UINT32)(h >> 15) & 1u;
    UINT32 exp  = (UINT32)(h >> 10) & 0x1Fu;
    UINT32 mant = (UINT32)h & 0x3FFu;

    UINT32 out_sign = sign << 31;
    UINT32 out_exp;
    UINT32 out_mant;

    if (exp == 0) {
        if (mant == 0) {
            UINT32 u = out_sign;
            return *(float *)&u;
        }
        // subnormal
        exp = 1;
        while ((mant & 0x400u) == 0) {
            mant <<= 1;
            exp--;
        }
        mant &= 0x3FFu;
        out_exp  = (exp + (127 - 15)) << 23;
        out_mant = mant << 13;
    } else if (exp == 31) {
        // inf/nan
        out_exp  = 0xFFu << 23;
        out_mant = mant ? (mant << 13) : 0;
    } else {
        out_exp  = (exp + (127 - 15)) << 23;
        out_mant = mant << 13;
    }

    UINT32 u = out_sign | out_exp | out_mant;
    return *(float *)&u;
}

static UINT64 llmk_align_up_u64(UINT64 x, UINT64 a) {
    return (a == 0) ? x : ((x + a - 1ULL) / a) * a;
}

// GGML Q8_0 block format: fp16 scale + 32 int8 values.
// bytes_per_row = (cols/32) * 34.
static UINT64 llmk_q8_0_row_bytes(int cols) {
    if (cols <= 0) return 0;
    if ((cols % 32) != 0) return 0;
    return ((UINT64)cols / 32ULL) * 34ULL;
}

static void llmk_dequantize_q8_0_row(float *dst, const UINT8 *row_q8, int cols) {
    UINT64 rb = llmk_q8_0_row_bytes(cols);
    if (!dst || !row_q8 || rb == 0) return;

    const int nb = cols / 32;
    const UINT8 *p = row_q8;
    for (int b = 0; b < nb; b++) {
        UINT16 dh = llmk_read_u16_unaligned(p);
        float d = llmk_fp16_to_fp32(dh);
        const INT8 *qs = (const INT8 *)(p + 2);
        for (int i = 0; i < 32; i++) {
            dst[b * 32 + i] = d * (float)qs[i];
        }
        p += 34;
    }
}

// xout(d) = W(d×n) · x(n) where W is Q8_0 row-major blocks.
static void matmul_q8_0_scalar(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        // Q8_0 requires cols multiple of 32.
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    const UINT64 row_bytes = llmk_q8_0_row_bytes(n);
    const int nb = n / 32;

    for (int r = 0; r < d; r++) {
        const UINT8 *row = w_q8 + (UINTN)r * (UINTN)row_bytes;
        float acc = 0.0f;
        const UINT8 *p = row;
        for (int b = 0; b < nb; b++) {
            float dscale = llmk_fp16_to_fp32(llmk_read_u16_unaligned(p));
            const INT8 *qs = (const INT8 *)(p + 2);
            float sum = 0.0f;
            const float *xblk = x + b * 32;
            for (int i = 0; i < 32; i++) {
                sum += xblk[i] * (float)qs[i];
            }
            acc += dscale * sum;
            p += 34;
        }
        xout[r] = acc;
    }
}

#if defined(__x86_64__) || defined(_M_X64)
// Shared activation quant buffers for Q8_0 matmuls (used only when q8_act_quant!=0).
// Monotonic allocation is OK; we only grow a couple of times (dim/hidden_dim).
static float *g_q8_act_scales = NULL;
static INT8  *g_q8_act_qs = NULL;
static int g_q8_act_cap_n = 0;

static void llmk_q8_act_ensure(int n) {
    if (n <= 0) return;
    if ((n % 32) != 0) return;
    if (g_q8_act_cap_n >= n && g_q8_act_scales && g_q8_act_qs) return;

    const int nb = n / 32;
    g_q8_act_scales = (float *)simple_alloc((unsigned long)nb * sizeof(float));
    g_q8_act_qs = (INT8 *)simple_alloc((unsigned long)n * sizeof(INT8));
    g_q8_act_cap_n = n;
}

static void llmk_quantize_f32_to_q8_blocks(const float *x, int n, INT8 *out_qs, float *out_scales) {
    if (!x || !out_qs || !out_scales) return;
    if (n <= 0 || (n % 32) != 0) return;
    const int nb = n / 32;
    for (int b = 0; b < nb; b++) {
        const float *xb = x + b * 32;
        float max_abs = 0.0f;
        for (int i = 0; i < 32; i++) {
            float v = xb[i];
            if (v < 0.0f) v = -v;
            if (v > max_abs) max_abs = v;
        }
        float dscale = (max_abs > 0.0f) ? (max_abs / 127.0f) : 0.0f;
        out_scales[b] = dscale;
        float inv = (dscale > 0.0f) ? (1.0f / dscale) : 0.0f;
        INT8 *qdst = out_qs + b * 32;
        for (int i = 0; i < 32; i++) {
            float fv = xb[i] * inv;
            int iv = (fv >= 0.0f) ? (int)(fv + 0.5f) : (int)(fv - 0.5f);
            if (iv < -127) iv = -127;
            if (iv > 127) iv = 127;
            qdst[i] = (INT8)iv;
        }
    }
}

// Dot kernel for 32 signed int8 values using AVX2.
// Returns int32 sum(a[i] * b[i]).
__attribute__((target("avx2")))
static int llmk_dot_i8_32_avx2(const INT8 *a, const INT8 *b) {
    __m128i a0 = _mm_loadu_si128((const __m128i *)(a + 0));
    __m128i a1 = _mm_loadu_si128((const __m128i *)(a + 16));
    __m128i b0 = _mm_loadu_si128((const __m128i *)(b + 0));
    __m128i b1 = _mm_loadu_si128((const __m128i *)(b + 16));

    __m256i a16_0 = _mm256_cvtepi8_epi16(a0);
    __m256i a16_1 = _mm256_cvtepi8_epi16(a1);
    __m256i b16_0 = _mm256_cvtepi8_epi16(b0);
    __m256i b16_1 = _mm256_cvtepi8_epi16(b1);

    __m256i s0 = _mm256_madd_epi16(a16_0, b16_0);
    __m256i s1 = _mm256_madd_epi16(a16_1, b16_1);
    __m256i s = _mm256_add_epi32(s0, s1);

    __m128i lo = _mm256_castsi256_si128(s);
    __m128i hi = _mm256_extracti128_si256(s, 1);
    __m128i sum = _mm_add_epi32(lo, hi);
    __m128i shuf = _mm_shuffle_epi32(sum, _MM_SHUFFLE(2, 3, 0, 1));
    sum = _mm_add_epi32(sum, shuf);
    shuf = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
    sum = _mm_add_epi32(sum, shuf);
    return _mm_cvtsi128_si32(sum);
}

// AVX2 implementation: converts int8 weights to float on the fly.
// Compiled as AVX2 even when the TU default is SSE2.
__attribute__((target("avx2")))
static void matmul_q8_0_avx2(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    const UINT64 row_bytes = llmk_q8_0_row_bytes(n);
    const int nb = n / 32;

    for (int r = 0; r < d; r++) {
        const UINT8 *row = w_q8 + (UINTN)r * (UINTN)row_bytes;
        float acc = 0.0f;
        const UINT8 *p = row;

        for (int b = 0; b < nb; b++) {
            float dscale = llmk_fp16_to_fp32(llmk_read_u16_unaligned(p));
            const INT8 *qs = (const INT8 *)(p + 2);
            const float *xblk = x + b * 32;

            __m256 vacc = _mm256_setzero_ps();

            // 32 values per block, process 8 at a time.
            for (int i = 0; i < 32; i += 8) {
                // Load 8 int8 values (unaligned) and sign-extend to 8 int32.
                __m128i q8 = _mm_loadl_epi64((const __m128i *)(qs + i));
                __m256i q32 = _mm256_cvtepi8_epi32(q8);
                __m256 qf = _mm256_cvtepi32_ps(q32);

                __m256 xf = _mm256_loadu_ps(xblk + i);
                vacc = _mm256_add_ps(vacc, _mm256_mul_ps(xf, qf));
            }

            // Horizontal sum of vacc without requiring SSE3 (build uses -msse2).
            __m128 lo = _mm256_castps256_ps128(vacc);
            __m128 hi = _mm256_extractf128_ps(vacc, 1);
            __m128 sum128 = _mm_add_ps(lo, hi);
            __m128 shuf = _mm_shuffle_ps(sum128, sum128, _MM_SHUFFLE(2, 3, 0, 1));
            sum128 = _mm_add_ps(sum128, shuf);
            shuf = _mm_shuffle_ps(sum128, sum128, _MM_SHUFFLE(1, 0, 3, 2));
            sum128 = _mm_add_ps(sum128, shuf);
            float sum = _mm_cvtss_f32(sum128);
            acc += dscale * sum;
            p += 34;
        }

        xout[r] = acc;
    }
}

// AVX2 implementation: quantize activations (x) into Q8_0 blocks and use int8 dot-products.
// Faster on real AVX2 CPUs; adds extra approximation (beyond quantized weights).
__attribute__((target("avx2")))
static void matmul_q8_0_avx2_i8_prequant(float *xout, const INT8 *x_qs, const float *x_scales, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x_qs || !x_scales || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    const UINT64 row_bytes = llmk_q8_0_row_bytes(n);
    const int nb = n / 32;

    for (int r = 0; r < d; r++) {
        const UINT8 *row = w_q8 + (UINTN)r * (UINTN)row_bytes;
        float acc = 0.0f;
        const UINT8 *p = row;
        for (int b = 0; b < nb; b++) {
            float wscale = llmk_fp16_to_fp32(llmk_read_u16_unaligned(p));
            const INT8 *wqs = (const INT8 *)(p + 2);
            const INT8 *blk = x_qs + b * 32;
            int dot = llmk_dot_i8_32_avx2(blk, wqs);
            acc += (wscale * x_scales[b]) * (float)dot;
            p += 34;
        }
        xout[r] = acc;
    }
}

__attribute__((target("avx2")))
static void matmul_q8_0_avx2_i8(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

    llmk_q8_act_ensure(n);
    if (!g_q8_act_qs || !g_q8_act_scales) return;
    llmk_quantize_f32_to_q8_blocks(x, n, g_q8_act_qs, g_q8_act_scales);
    matmul_q8_0_avx2_i8_prequant(xout, g_q8_act_qs, g_q8_act_scales, w_q8, n, d);
}
#endif

static void matmul_q8_0(float *xout, const float *x, const UINT8 *w_q8, int n, int d) {
    if (!xout || !x || !w_q8) return;
    if ((n % 32) != 0) {
        for (int i = 0; i < d; i++) xout[i] = 0.0f;
        return;
    }

#if defined(__x86_64__) || defined(_M_X64)
    static int g_q8_kernel_inited = 0;
    static int g_q8_use_avx2 = 0;
    if (!g_q8_kernel_inited) {
        CPUFeatures f;
        djiblas_detect_cpu(&f);
        g_q8_use_avx2 = (f.has_avx2 != 0);
        g_q8_kernel_inited = 1;
    }
    if (g_q8_use_avx2) {
        if (g_cfg_q8_act_quant == 1) {
            matmul_q8_0_avx2_i8(xout, x, w_q8, n, d);
        } else {
            matmul_q8_0_avx2(xout, x, w_q8, n, d);
        }
        return;
    }
#endif

    matmul_q8_0_scalar(xout, x, w_q8, n, d);
}

void softmax(float* x, int size) {
    float max_val = x[0];
#if defined(__x86_64__) || defined(_M_X64)
    // SSE2 max reduction
    {
        __m128 vmax = _mm_set1_ps(max_val);
        int i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 v = _mm_loadu_ps(&x[i]);
            vmax = _mm_max_ps(vmax, v);
        }
        __m128 shuf = _mm_shuffle_ps(vmax, vmax, _MM_SHUFFLE(2, 3, 0, 1));
        vmax = _mm_max_ps(vmax, shuf);
        shuf = _mm_shuffle_ps(vmax, vmax, _MM_SHUFFLE(1, 0, 3, 2));
        vmax = _mm_max_ps(vmax, shuf);
        _mm_store_ss(&max_val, vmax);
        for (; i < size; i++) {
            if (x[i] > max_val) max_val = x[i];
        }
    }
#else
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) max_val = x[i];
    }
#endif

    float sum = 0.0f;
#if defined(__x86_64__) || defined(_M_X64)
    // Scalar exp, but vectorized accumulation + normalization.
    {
        __m128 vsum = _mm_setzero_ps();
        int i = 0;
        for (; i + 4 <= size; i += 4) {
            float e0 = fast_exp(x[i + 0] - max_val);
            float e1 = fast_exp(x[i + 1] - max_val);
            float e2 = fast_exp(x[i + 2] - max_val);
            float e3 = fast_exp(x[i + 3] - max_val);
            x[i + 0] = e0;
            x[i + 1] = e1;
            x[i + 2] = e2;
            x[i + 3] = e3;
            __m128 v = _mm_loadu_ps(&x[i]);
            vsum = _mm_add_ps(vsum, v);
        }
        __m128 shuf = _mm_shuffle_ps(vsum, vsum, _MM_SHUFFLE(2, 3, 0, 1));
        vsum = _mm_add_ps(vsum, shuf);
        shuf = _mm_shuffle_ps(vsum, vsum, _MM_SHUFFLE(1, 0, 3, 2));
        vsum = _mm_add_ps(vsum, shuf);
        _mm_store_ss(&sum, vsum);
        for (; i < size; i++) {
            x[i] = fast_exp(x[i] - max_val);
            sum += x[i];
        }

        float invsum = 1.0f / sum;
        __m128 vinv = _mm_set1_ps(invsum);
        i = 0;
        for (; i + 4 <= size; i += 4) {
            __m128 v = _mm_loadu_ps(&x[i]);
            v = _mm_mul_ps(v, vinv);
            _mm_storeu_ps(&x[i], v);
        }
        for (; i < size; i++) {
            x[i] *= invsum;
        }
    }
#else
    for (int i = 0; i < size; i++) {
        x[i] = fast_exp(x[i] - max_val);
        sum += x[i];
    }
    float invsum = 1.0f / sum;
    for (int i = 0; i < size; i++) {
        x[i] *= invsum;
    }
#endif
}

// ============================================================================
// STRUCTURES
// ============================================================================

typedef struct {
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int vocab_size;
    int seq_len;
} Config;

static UINT64 llmk_calc_kv_bytes_for_seq(const Config *cfg, int seq_len, int kv_dim) {
    if (!cfg || seq_len <= 0 || kv_dim <= 0) return 0;
    return (UINT64)cfg->n_layers * (UINT64)seq_len * (UINT64)kv_dim * (UINT64)sizeof(float) * 2ULL;
}

static UINT64 llmk_calc_state_bytes_for_seq(const Config *cfg, int seq_len, int kv_dim) {
    if (!cfg || seq_len <= 0 || kv_dim <= 0) return 0;

    UINT64 state_bytes = 0;
    state_bytes += (UINT64)cfg->dim * (UINT64)sizeof(float) * 3ULL; // x, xb, xb2
    state_bytes += (UINT64)cfg->hidden_dim * (UINT64)sizeof(float) * 2ULL; // hb, hb2
    state_bytes += (UINT64)cfg->dim * (UINT64)sizeof(float); // q
    state_bytes += (UINT64)kv_dim * (UINT64)sizeof(float) * 2ULL; // k, v
    state_bytes += (UINT64)cfg->n_heads * (UINT64)seq_len * (UINT64)sizeof(float); // att
    state_bytes += (UINT64)cfg->vocab_size * (UINT64)sizeof(float); // logits
    state_bytes += (UINT64)cfg->n_layers * (UINT64)seq_len * (UINT64)kv_dim * (UINT64)sizeof(float) * 2ULL; // key/value cache
    return state_bytes;
}

static UINT32 llmk_fnv1a32_update(UINT32 h, const void *data, UINTN len) {
    const UINT8 *p = (const UINT8 *)data;
    for (UINTN i = 0; i < len; i++) {
        h ^= (UINT32)p[i];
        h *= 16777619u;
    }
    return h;
}

static UINT32 llmk_memorion_ctx_hash32(const Config *config, const CHAR16 *model_filename) {
    UINT32 h = 2166136261u;
    if (config) {
        h = llmk_fnv1a32_update(h, &config->dim, sizeof(config->dim));
        h = llmk_fnv1a32_update(h, &config->n_layers, sizeof(config->n_layers));
        h = llmk_fnv1a32_update(h, &config->n_heads, sizeof(config->n_heads));
        h = llmk_fnv1a32_update(h, &config->n_kv_heads, sizeof(config->n_kv_heads));
        h = llmk_fnv1a32_update(h, &config->seq_len, sizeof(config->seq_len));
        h = llmk_fnv1a32_update(h, &config->vocab_size, sizeof(config->vocab_size));
    }
    if (model_filename) {
        char name8[128];
        llmk_char16_to_ascii_cap(name8, (int)sizeof(name8), model_filename);
        h = llmk_fnv1a32_update(h, name8, (UINTN)my_strlen(name8));
    }
    return h;
}

static void llmk_print_ctx(const Config *config,
                   const CHAR16 *model_name,
                   int kv_pos,
                   float temperature,
                   float min_p,
                   float top_p,
                   int top_k,
                   int no_repeat_ngram,
                   float repeat_penalty,
                   int max_gen_tokens) {
    Print(L"\r\nCTX\r\n");
    Print(L"  model=%s\r\n", model_name ? model_name : L"(unknown)");
    Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d\r\n",
        config->dim, config->n_layers, config->n_heads, config->n_kv_heads, config->vocab_size);
    Print(L"  seq_len=%d kv_pos=%d\r\n", config->seq_len, kv_pos);
    Print(L"  sample: temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
        (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
        (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
        (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
        top_k);
    Print(L"          norepeat=%d repeat=%d.%02d max_tokens=%d\r\n",
        no_repeat_ngram,
        (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
        max_gen_tokens);
    if (g_llmk_ready) {
      Print(L"  budget: prefill=%lu decode=%lu strict=%d overruns(p=%d d=%d)\r\n",
          g_budget_prefill_cycles, g_budget_decode_cycles,
          (int)g_sentinel.cfg.strict_budget,
          (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
    }
    Print(L"\r\n");
}

static void llmk_print_log(UINT32 n) {
    if (n == 0) n = 16;
    if (n > 128) n = 128;
    Print(L"\r\nLog (last %d):\r\n", (int)n);
    if (g_llmk_ready && g_llmk_log.capacity) {
        llmk_log_dump(&g_llmk_log, n);
    } else {
        Print(L"  (log not available)\r\n");
    }
    Print(L"\r\n");
}

static void llmk_print_ram_budget(void) {
    if (!g_llmk_ready) {
        Print(L"\r\nRAM budget: (llmk not ready)\r\n\r\n");
        return;
    }

    Print(L"\r\nRAM budget (Zone B):\r\n");
    for (int i = 0; i < LLMK_ARENA_COUNT; i++) {
        const LlmkArena *a = &g_zones.arenas[i];
        UINT64 used = llmk_arena_used_bytes(&g_zones, (LlmkArenaId)i);
        UINT64 rem = llmk_arena_remaining_bytes(&g_zones, (LlmkArenaId)i);
        UINT64 total = a->size;
        UINT64 used_mb = used / (1024ULL * 1024ULL);
        UINT64 total_mb = total / (1024ULL * 1024ULL);
        UINT64 rem_mb = rem / (1024ULL * 1024ULL);
        Print(L"  %s: used=%lu MB  free=%lu MB  total=%lu MB\r\n",
              a->name, used_mb, rem_mb, total_mb);
    }
    Print(L"\r\n");
}

typedef struct {
    int kind; // 0 = float32, 1 = Q8_0 blob

    // float32 pointers (always valid for norms; valid for matrices in float32 mode)
    float* token_embedding_table;
    float* rms_att_weight;
    float* wq;
    float* wk;
    float* wv;
    float* wo;
    float* rms_ffn_weight;
    float* w1;
    float* w2;
    float* w3;
    float* rms_final_weight;
    float* wcls;

    // Q8_0 pointers (valid in Q8_0 blob mode)
    const UINT8 *token_embedding_table_q8;
    const UINT8 *wq_q8;
    const UINT8 *wk_q8;
    const UINT8 *wv_q8;
    const UINT8 *wo_q8;
    const UINT8 *w1_q8;
    const UINT8 *w2_q8;
    const UINT8 *w3_q8;
    const UINT8 *wcls_q8;

    // Strides/sizes for Q8_0 blob addressing
    UINT64 tok_embd_row_bytes;
    UINT64 wq_layer_bytes;
    UINT64 wk_layer_bytes;
    UINT64 wv_layer_bytes;
    UINT64 wo_layer_bytes;
    UINT64 w1_layer_bytes;
    UINT64 w2_layer_bytes;
    UINT64 w3_layer_bytes;
} TransformerWeights;

static void llmk_print_cfg(const Config *config,
                           const CHAR16 *model_name,
                           const TransformerWeights *weights,
                           int kv_pos,
                           float temperature,
                           float min_p,
                           float top_p,
                           int top_k,
                           int no_repeat_ngram,
                           float repeat_penalty,
                           int max_gen_tokens) {

    Print(L"\r\nCFG\r\n");

    Print(L"  repl_cfg_loaded=%d\r\n", g_cfg_loaded);
    Print(L"  boot_verbose=%d\r\n", g_boot_verbose);

    Print(L"  gguf_q8_blob=%d\r\n", g_cfg_gguf_q8_blob ? 1 : 0);
    Print(L"  q8_act_quant=%d\r\n", g_cfg_q8_act_quant);
    Print(L"  model_picker=%d\r\n", g_cfg_model_picker ? 1 : 0);
    Print(L"  ctx_len_cfg=%d\r\n", g_cfg_ctx_len);
    Print(L"  chat_format=");
    llmk_print_ascii(llmk_chat_format_name_ascii(g_cfg_chat_format));
    Print(L"\r\n");
    Print(L"  system_prompt=");
    if (g_cfg_system_prompt[0]) {
        llmk_print_ascii(g_cfg_system_prompt);
    } else {
        Print(L"(empty)");
    }
    Print(L"\r\n");
    Print(L"  autorun_autostart=%d\r\n", g_cfg_autorun_autostart);
    Print(L"  autorun_shutdown_when_done=%d\r\n", g_cfg_autorun_shutdown_when_done);
    Print(L"  autorun_file=%s\r\n", g_cfg_autorun_file);

    // Runtime state
    if (g_loaded_model_path16[0]) {
        Print(L"  loaded_model_path=%s\r\n", g_loaded_model_path16);
    } else {
        Print(L"  loaded_model_path=(unknown)\r\n");
    }
    Print(L"  model=%s\r\n", model_name ? model_name : L"(unknown)");

    if (config) {
        Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
              config->dim, config->n_layers, config->n_heads, config->n_kv_heads, config->vocab_size, config->seq_len);
        Print(L"  kv_pos=%d\r\n", kv_pos);
    }

    if (weights) {
        Print(L"  weights_kind=%s\r\n", (weights->kind == 1) ? L"q8_0_blob" : L"float32");
        if (weights->kind == 1) {
            Print(L"  tok_embd_row_bytes=%lu\r\n", (UINT64)weights->tok_embd_row_bytes);
            Print(L"  wq_layer_bytes=%lu\r\n", (UINT64)weights->wq_layer_bytes);
            Print(L"  wk_layer_bytes=%lu\r\n", (UINT64)weights->wk_layer_bytes);
            Print(L"  wv_layer_bytes=%lu\r\n", (UINT64)weights->wv_layer_bytes);
            Print(L"  wo_layer_bytes=%lu\r\n", (UINT64)weights->wo_layer_bytes);
            Print(L"  w1_layer_bytes=%lu\r\n", (UINT64)weights->w1_layer_bytes);
            Print(L"  w2_layer_bytes=%lu\r\n", (UINT64)weights->w2_layer_bytes);
            Print(L"  w3_layer_bytes=%lu\r\n", (UINT64)weights->w3_layer_bytes);
        }
    } else {
        Print(L"  weights_kind=(unknown)\r\n");
    }

    // Attention SIMD mode
    const CHAR16 *attn_mode = L"auto";
    if (g_attn_force == 0) attn_mode = L"sse2 (forced)";
    else if (g_attn_force == 1) attn_mode = L"avx2 (forced)";
    Print(L"  attn_mode=%s\r\n", attn_mode);
    Print(L"  attn_auto=%s\r\n", g_attn_use_avx2 ? L"avx2" : L"sse2");

        Print(L"  sampling: temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
            (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
            (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
            (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
            top_k);
        Print(L"            norepeat=%d repeat=%d.%02d max_tokens=%d\r\n",
            no_repeat_ngram,
            (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
            max_gen_tokens);

        if (g_llmk_ready) {
          Print(L"  budgets: prefill_max=%lu decode_max=%lu strict=%d overruns(p=%d d=%d)\r\n",
              g_budget_prefill_cycles, g_budget_decode_cycles,
              (int)g_sentinel.cfg.strict_budget,
              (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
        }

    Print(L"\r\n");
}

typedef struct {
    float* x;
    float* xb;
    float* xb2;
    float* hb;
    float* hb2;
    float* q;
    float* k;
    float* v;
    float* att;
    float* logits;
    float* key_cache;
    float* value_cache;
} RunState;

typedef struct {
    char** vocab;
    float* vocab_scores;
    int vocab_size;
    int max_token_length;
} Tokenizer;

// Forward decl for M5 /oo_consult (needs Config, TransformerWeights, RunState, Tokenizer)
static void llmk_oo_consult_execute(Config *config, TransformerWeights *weights, 
                                    RunState *state, Tokenizer *tokenizer,
                                    float temperature, float min_p, float top_p, int top_k);

// ============================================================================
// FORWARD PASS
// ============================================================================

void transformer_forward(RunState* s, TransformerWeights* w, Config* p, int token, int pos) {
    UINT64 start_cycles = __rdtsc();
    int is_prefill = (pos == 0);
    
    // DjibMark: record entry into transformer (prefill vs decode determined by caller)
    if (is_prefill) {
        DJIBMARK_PREFILL();
    } else {
        DJIBMARK_DECODE();
    }
    
    int dim = p->dim;
    int hidden_dim = p->hidden_dim;
    int n_layers = p->n_layers;
    int n_heads = p->n_heads;
    int head_size = dim / n_heads;
    int kv_dim = (dim * p->n_kv_heads) / n_heads;
    int kv_mul = n_heads / p->n_kv_heads;

    const int q8_mode = g_cfg_q8_act_quant;
    const int use_i8_attn = (q8_mode == 1) && llmk_has_avx2_cached();
    const int use_i8_ffn = ((q8_mode == 1) || (q8_mode == 2)) && llmk_has_avx2_cached();
    const int use_i8_cls = (q8_mode == 1) && llmk_has_avx2_cached();
    
    // Copy embedding
    if (w->kind == 1) {
        const UINT8 *row = w->token_embedding_table_q8 + (UINTN)token * (UINTN)w->tok_embd_row_bytes;
        llmk_dequantize_q8_0_row(s->x, row, dim);
    } else {
        float* content_row = w->token_embedding_table + token * dim;
        for (int i = 0; i < dim; i++) {
            s->x[i] = content_row[i];
        }
    }
    
    // Forward all layers
    for (int l = 0; l < n_layers; l++) {
        // Attention RMSNorm
        rmsnorm(s->xb, s->x, w->rms_att_weight + l*dim, dim);
        
        // Q, K, V matrices
        if (w->kind == 1) {
            if (use_i8_attn) {
                llmk_q8_act_ensure(dim);
                llmk_quantize_f32_to_q8_blocks(s->xb, dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->q, g_q8_act_qs, g_q8_act_scales, w->wq_q8 + (UINTN)l * (UINTN)w->wq_layer_bytes, dim, dim);
                matmul_q8_0_avx2_i8_prequant(s->k, g_q8_act_qs, g_q8_act_scales, w->wk_q8 + (UINTN)l * (UINTN)w->wk_layer_bytes, dim, kv_dim);
                matmul_q8_0_avx2_i8_prequant(s->v, g_q8_act_qs, g_q8_act_scales, w->wv_q8 + (UINTN)l * (UINTN)w->wv_layer_bytes, dim, kv_dim);
            } else {
                matmul_q8_0(s->q, s->xb, w->wq_q8 + (UINTN)l * (UINTN)w->wq_layer_bytes, dim, dim);
                matmul_q8_0(s->k, s->xb, w->wk_q8 + (UINTN)l * (UINTN)w->wk_layer_bytes, dim, kv_dim);
                matmul_q8_0(s->v, s->xb, w->wv_q8 + (UINTN)l * (UINTN)w->wv_layer_bytes, dim, kv_dim);
            }
        } else {
            matmul(s->q, s->xb, w->wq + l*dim*dim, dim, dim);
            matmul(s->k, s->xb, w->wk + l*dim*kv_dim, dim, kv_dim);
            matmul(s->v, s->xb, w->wv + l*dim*kv_dim, dim, kv_dim);
        }
        
        // Store in KV cache
        int loff = l * p->seq_len * kv_dim;
        float* key_cache_row = s->key_cache + loff + pos * kv_dim;
        float* value_cache_row = s->value_cache + loff + pos * kv_dim;
        for (int i = 0; i < kv_dim; i++) {
            key_cache_row[i] = s->k[i];
            value_cache_row[i] = s->v[i];
        }
        
        // Multihead attention
        for (int h = 0; h < n_heads; h++) {
            float* q_h = s->q + h * head_size;
            int att_offset = h * p->seq_len;
            float inv_scale = 1.0f / fast_sqrt((float)head_size);
            
            // Attention scores
            for (int t = 0; t <= pos; t++) {
                float* k_t = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float score = dot_f32_best(q_h, k_t, head_size) * inv_scale;
                s->att[att_offset + t] = score;
            }
            
            // Softmax
            softmax(s->att + att_offset, pos + 1);
            
            // Weighted sum
            float* xb_h = s->xb + h * head_size;
            for (int i = 0; i < head_size; i++) xb_h[i] = 0.0f;
            
            for (int t = 0; t <= pos; t++) {
                float* v_t = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float a = s->att[att_offset + t];
                axpy_f32_best(xb_h, v_t, a, head_size);
            }
        }
        
        // Output projection
        if (w->kind == 1) {
            if (use_i8_attn) {
                llmk_q8_act_ensure(dim);
                llmk_quantize_f32_to_q8_blocks(s->xb, dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->xb2, g_q8_act_qs, g_q8_act_scales, w->wo_q8 + (UINTN)l * (UINTN)w->wo_layer_bytes, dim, dim);
            } else {
                matmul_q8_0(s->xb2, s->xb, w->wo_q8 + (UINTN)l * (UINTN)w->wo_layer_bytes, dim, dim);
            }
        } else {
            matmul(s->xb2, s->xb, w->wo + l*dim*dim, dim, dim);
        }
        
        // Residual
        for (int i = 0; i < dim; i++) {
            s->x[i] += s->xb2[i];
        }
        
        // FFN RMSNorm
        rmsnorm(s->xb, s->x, w->rms_ffn_weight + l*dim, dim);
        
        // FFN
        if (w->kind == 1) {
            if (use_i8_ffn) {
                llmk_q8_act_ensure(dim);
                llmk_quantize_f32_to_q8_blocks(s->xb, dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->hb, g_q8_act_qs, g_q8_act_scales, w->w1_q8 + (UINTN)l * (UINTN)w->w1_layer_bytes, dim, hidden_dim);
                matmul_q8_0_avx2_i8_prequant(s->hb2, g_q8_act_qs, g_q8_act_scales, w->w3_q8 + (UINTN)l * (UINTN)w->w3_layer_bytes, dim, hidden_dim);
            } else {
                matmul_q8_0(s->hb, s->xb, w->w1_q8 + (UINTN)l * (UINTN)w->w1_layer_bytes, dim, hidden_dim);
                matmul_q8_0(s->hb2, s->xb, w->w3_q8 + (UINTN)l * (UINTN)w->w3_layer_bytes, dim, hidden_dim);
            }
        } else {
            matmul(s->hb, s->xb, w->w1 + l*dim*hidden_dim, dim, hidden_dim);
            matmul(s->hb2, s->xb, w->w3 + l*dim*hidden_dim, dim, hidden_dim);
        }
        
        // SwiGLU
        for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            val *= (1.0f / (1.0f + fast_exp(-val)));
            s->hb[i] = val * s->hb2[i];
        }
        
        if (w->kind == 1) {
            if (use_i8_ffn) {
                llmk_q8_act_ensure(hidden_dim);
                llmk_quantize_f32_to_q8_blocks(s->hb, hidden_dim, g_q8_act_qs, g_q8_act_scales);
                matmul_q8_0_avx2_i8_prequant(s->xb, g_q8_act_qs, g_q8_act_scales, w->w2_q8 + (UINTN)l * (UINTN)w->w2_layer_bytes, hidden_dim, dim);
            } else {
                matmul_q8_0(s->xb, s->hb, w->w2_q8 + (UINTN)l * (UINTN)w->w2_layer_bytes, hidden_dim, dim);
            }
        } else {
            matmul(s->xb, s->hb, w->w2 + l*dim*hidden_dim, hidden_dim, dim);
        }
        
        // Residual
        for (int i = 0; i < dim; i++) {
            s->x[i] += s->xb[i];
        }
    }
    
    // Final RMSNorm
    rmsnorm(s->x, s->x, w->rms_final_weight, dim);
    
    // Classifier
    if (w->kind == 1) {
        if (use_i8_cls) {
            llmk_q8_act_ensure(dim);
            llmk_quantize_f32_to_q8_blocks(s->x, dim, g_q8_act_qs, g_q8_act_scales);
            matmul_q8_0_avx2_i8_prequant(s->logits, g_q8_act_qs, g_q8_act_scales, w->wcls_q8, dim, p->vocab_size);
        } else {
            matmul_q8_0(s->logits, s->x, w->wcls_q8, dim, p->vocab_size);
        }
    } else {
        matmul(s->logits, s->x, w->wcls, dim, p->vocab_size);
    }
    
    // M16.1: Capture transformer metrics
    UINT64 end_cycles = __rdtsc();
    UINT64 elapsed = (end_cycles > start_cycles) ? (end_cycles - start_cycles) : 0;
    
    if (is_prefill) {
        g_metrics.total_prefill_cycles += elapsed;
        g_metrics.total_prefill_tokens++;
        g_metrics.total_prefill_calls++;
        g_metrics.last_prefill_cycles = elapsed;
        g_metrics.last_prefill_tokens = 1;
    } else {
        g_metrics.total_decode_cycles += elapsed;
        g_metrics.total_decode_tokens++;
        g_metrics.total_decode_calls++;
        g_metrics.last_decode_cycles = elapsed;
        g_metrics.last_decode_tokens = 1;
    }
}

// Simple PRNG for sampling
static unsigned int g_sample_seed = 1234567;

static void set_seed(unsigned int seed) {
    // Avoid a zero seed getting stuck in some LCGs.
    if (seed == 0) seed = 1;
    g_sample_seed = seed;
}

static unsigned long long rdtsc(void) {
    unsigned int lo, hi;
    // Serialize via LFENCE to reduce reordering noise.
    __asm__ __volatile__("lfence\nrdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((unsigned long long)hi << 32) | lo;
}

// 0 means "unavailable / calibration failed".
static unsigned long long tsc_per_sec = 0;

// Cached CPU feature checks (avoid repeated CPUID).
static int llmk_has_avx2_cached(void) {
    static int inited = 0;
    static int has = 0;
    if (!inited) {
        CPUFeatures f;
        djiblas_detect_cpu(&f);
        has = (f.has_avx2 != 0);
        inited = 1;
    }
    return has;
}

// Best-effort wall-clock microsecond timestamp using UEFI GetTime.
// Returns 1 on success, 0 on failure.
static int uefi_wall_us(unsigned long long *out_us) {
    if (!out_us) return 0;
    if (!ST || !ST->RuntimeServices || !ST->RuntimeServices->GetTime) return 0;
    EFI_TIME t;
    EFI_STATUS st = uefi_call_wrapper(ST->RuntimeServices->GetTime, 2, &t, NULL);
    if (EFI_ERROR(st)) return 0;
    // Seconds-of-day is sufficient for short deltas (we handle midnight wrap).
    unsigned long long sod = (unsigned long long)t.Hour * 3600ULL + (unsigned long long)t.Minute * 60ULL + (unsigned long long)t.Second;
    unsigned long long us = sod * 1000000ULL;
    // Nanosecond is defined by EFI_TIME; firmware may provide 0.
    us += ((unsigned long long)t.Nanosecond) / 1000ULL;
    *out_us = us;
    return 1;
}

static void calibrate_tsc_once(void) {
    if (tsc_per_sec != 0) return;
    // Use UEFI Stall (microseconds) to estimate TSC frequency.
    // 500ms gives decent accuracy even on coarse/slow TSC emulation.
    unsigned long long t0 = rdtsc();
    uefi_call_wrapper(BS->Stall, 1, 500000);
    unsigned long long t1 = rdtsc();
    unsigned long long dt = (t1 > t0) ? (t1 - t0) : 0;
    // If dt is implausibly small, treat as unavailable.
    if (dt < 1000ULL) {
        tsc_per_sec = 0;
        return;
    }
    // 500ms -> multiply by 2 to get cycles/sec.
    tsc_per_sec = dt * 2ULL;
}

static float randf(void) {
    g_sample_seed = g_sample_seed * 1664525 + 1013904223;
    return (float)(g_sample_seed >> 8) / 16777216.0f;
}

// Sample with temperature + min_p + top-p + top-k + repetition penalty
int sample_advanced(float* logits, int n, float temperature, float min_p, float top_p, int top_k,
                    int* recent_tokens, int n_recent, float repeat_penalty) {
    // Apply repetition penalty
    if (repeat_penalty != 1.0f && n_recent > 0) {
        for (int i = 0; i < n_recent; i++) {
            int tok = recent_tokens[i];
            if (tok >= 0 && tok < n) {
                if (logits[tok] > 0) {
                    logits[tok] /= repeat_penalty;
                } else {
                    logits[tok] *= repeat_penalty;
                }
            }
        }
    }
    
    // Greedy if temp=0
    if (temperature <= 0.0f) {
        int max_i = 0;
        float max_val = logits[0];
        for (int i = 1; i < n; i++) {
            if (logits[i] > max_val) {
                max_val = logits[i];
                max_i = i;
            }
        }
        return max_i;
    }
    
    // Apply temperature
    for (int i = 0; i < n; i++) {
        logits[i] /= temperature;
    }
    
    // Softmax
    float max_val = logits[0];
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_val) max_val = logits[i];
    }
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        logits[i] = fast_exp(logits[i] - max_val);
        sum += logits[i];
    }
    for (int i = 0; i < n; i++) {
        logits[i] /= sum;
    }

    // Min-p filtering (relative to max probability)
    if (min_p > 0.0f) {
        float max_p = 0.0f;
        for (int i = 0; i < n; i++) {
            if (logits[i] > max_p) max_p = logits[i];
        }
        float thresh = min_p * max_p;
        float new_sum = 0.0f;
        for (int i = 0; i < n; i++) {
            if (logits[i] < thresh) {
                logits[i] = 0.0f;
            }
            new_sum += logits[i];
        }
        if (new_sum > 0.0f) {
            for (int i = 0; i < n; i++) {
                logits[i] /= new_sum;
            }
        }
    }
    
    // Top-k / Top-p sampling
    {
        // IMPORTANT: vocab is 32k; do NOT full-sort.
        // We maintain a small descending top-list.
        #define MAX_TOP_K 256
        static int top_idx[MAX_TOP_K];
        static float top_prob[MAX_TOP_K];
        int k = top_k;
        if (k < 0) k = 0;
        if (k > MAX_TOP_K) k = MAX_TOP_K;
        if (k == 0 || k > n) k = (n < MAX_TOP_K) ? n : MAX_TOP_K;

        int top_count = 0;
        for (int i = 0; i < n; i++) {
            float p = logits[i];
            if (top_count < k) {
                int j = top_count;
                while (j > 0 && top_prob[j - 1] < p) {
                    top_prob[j] = top_prob[j - 1];
                    top_idx[j] = top_idx[j - 1];
                    j--;
                }
                top_prob[j] = p;
                top_idx[j] = i;
                top_count++;
            } else if (p > top_prob[top_count - 1]) {
                int j = top_count - 1;
                while (j > 0 && top_prob[j - 1] < p) {
                    top_prob[j] = top_prob[j - 1];
                    top_idx[j] = top_idx[j - 1];
                    j--;
                }
                top_prob[j] = p;
                top_idx[j] = i;
            }
        }

        // If both are effectively "disabled" (top_p>=1 and top_k<=0), fall through to full sampling.
        if (top_k > 0 || top_p < 1.0f) {
            float mass = 0.0f;
            int cutoff = 0;
            for (int i = 0; i < top_count; i++) {
                mass += top_prob[i];
                cutoff++;
                if (top_p < 1.0f && mass >= top_p) break;
            }
            if (cutoff < 1) cutoff = 1;

            float r = randf() * mass;
            float cdf = 0.0f;
            for (int i = 0; i < cutoff; i++) {
                cdf += top_prob[i];
                if (r < cdf) {
                    return top_idx[i];
                }
            }
            return top_idx[cutoff - 1];
        }
        #undef MAX_TOP_K
    }
    
    // Sample from distribution
    float r = randf();
    float cumsum = 0.0f;
    for (int i = 0; i < n; i++) {
        cumsum += logits[i];
        if (r < cumsum) {
            return i;
        }
    }
    
    return n - 1;
}

int sample(float* logits, int n) {
    // Simple greedy for now (kept for compatibility)
    int max_i = 0;
    float max_val = logits[0];
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_val) {
            max_val = logits[i];
            max_i = i;
        }
    }
    return max_i;
}

// ============================================================================
// TOKENIZER
// ============================================================================

static int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int my_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static char *my_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

static void llmk_u64_to_str(UINT64 val, char *buf, int buf_size) {
    // Convert UINT64 to decimal string (no sprintf in UEFI)
    if (buf_size < 2) return;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    
    char tmp[32];
    int i = 0;
    while (val > 0 && i < 31) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    
    int j = 0;
    while (i > 0 && j < buf_size - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = 0;
}

int str_lookup(char* str, char** vocab, int vocab_size) {
    for (int i = 0; i < vocab_size; i++) {
        if (vocab[i] && my_strcmp(str, vocab[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void encode(char* text, int* tokens, int* n_tokens, int max_tokens, Tokenizer* t) {
    *n_tokens = 0;
    if (max_tokens <= 0) return;

    // Add BOS
    tokens[(*n_tokens)++] = TOKEN_BOS;
    if (*n_tokens >= max_tokens) return;

    // Greedy longest-match encoding
    char* str = text;
    while (*str && *n_tokens < max_tokens) {
        int best_id = -1;
        int best_len = 0;

        for (int len = 64; len > 0; len--) {
            char piece[65];
            int i = 0;
            for (i = 0; i < len && str[i]; i++) {
                piece[i] = str[i];
            }
            if (i != len) continue; // not enough chars remaining
            piece[i] = '\0';

            int id = str_lookup(piece, t->vocab, t->vocab_size);
            if (id >= 0) {
                best_id = id;
                best_len = len;
                break;
            }
        }

        if (best_id >= 0) {
            if (*n_tokens >= max_tokens) break;
            tokens[(*n_tokens)++] = best_id;
            str += best_len;
        } else {
            char single[2];
            single[0] = *str;
            single[1] = '\0';
            int id = str_lookup(single, t->vocab, t->vocab_size);
            if (id >= 0) {
                if (*n_tokens >= max_tokens) break;
                tokens[(*n_tokens)++] = id;
            }
            str++;
        }
    }
}

// ============================================================================
// KEYBOARD INPUT
// ============================================================================

#define LLMK_INPUT_HIST_MAX 32
#define LLMK_INPUT_HIST_MAXLEN 256

static CHAR16 g_input_hist[LLMK_INPUT_HIST_MAX][LLMK_INPUT_HIST_MAXLEN];
static int g_input_hist_count = 0; // <= LLMK_INPUT_HIST_MAX
static int g_input_hist_head = 0;  // next insert index (ring)

static int llmk_str16_len_cap(const CHAR16 *s, int cap) {
    if (!s || cap <= 0) return 0;
    int n = 0;
    while (n < cap && s[n]) n++;
    return n;
}

static void llmk_str16_copy_cap(CHAR16 *dst, int dst_cap, const CHAR16 *src) {
    if (!dst || dst_cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int n = 0;
    while (n + 1 < dst_cap && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static int llmk_str16_has_newline(const CHAR16 *s) {
    if (!s) return 0;
    for (const CHAR16 *p = s; *p; p++) {
        if (*p == L'\n' || *p == L'\r') return 1;
    }
    return 0;
}

static const CHAR16 *llmk_hist_get_nth_from_last(int n_from_last) {
    if (n_from_last < 0) return NULL;
    if (g_input_hist_count <= 0) return NULL;
    if (n_from_last >= g_input_hist_count) return NULL;

    int idx = g_input_hist_head - 1 - n_from_last;
    while (idx < 0) idx += LLMK_INPUT_HIST_MAX;
    idx %= LLMK_INPUT_HIST_MAX;
    return g_input_hist[idx];
}

static void llmk_hist_add_line(const CHAR16 *line) {
    if (!line || line[0] == 0) return;
    if (llmk_str16_has_newline(line)) return; // keep history simple (single-line only)

    // Avoid duplicates vs the last entry.
    if (g_input_hist_count > 0) {
        const CHAR16 *last = llmk_hist_get_nth_from_last(0);
        if (last && StrCmp((CHAR16 *)last, (CHAR16 *)line) == 0) return;
    }

    llmk_str16_copy_cap(g_input_hist[g_input_hist_head], LLMK_INPUT_HIST_MAXLEN, line);
    g_input_hist_head = (g_input_hist_head + 1) % LLMK_INPUT_HIST_MAX;
    if (g_input_hist_count < LLMK_INPUT_HIST_MAX) g_input_hist_count++;
}

static void llmk_console_erase_chars(int n) {
    for (int i = 0; i < n; i++) {
        Print(L"\b \b");
    }
}

static int g_tab_cycle_active = 0;
static int g_tab_cycle_index = -1;
static int g_tab_cycle_token_start = 0;
static char g_tab_cycle_prefix[64];

static void llmk_tab_cycle_reset(void) {
    g_tab_cycle_active = 0;
    g_tab_cycle_index = -1;
    g_tab_cycle_token_start = 0;
    g_tab_cycle_prefix[0] = 0;
}

static int llmk_ascii_startswith(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static char llmk_ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int llmk_ascii_startswith_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        char a = llmk_ascii_tolower(*s);
        char b = llmk_ascii_tolower(*prefix);
        if (a != b) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int llmk_ascii_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (needle[0] == 0) return 1;

    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (needle[j] && haystack[i + j]) {
            char a = llmk_ascii_tolower(haystack[i + j]);
            char b = llmk_ascii_tolower(needle[j]);
            if (a != b) break;
            j++;
        }
        if (needle[j] == 0) return 1;
    }
    return 0;
}

static int llmk_cmd_matches_filter(const char *name, const char *filter) {
    if (!name) return 0;
    if (!filter || !filter[0]) return 1;

    // If filter starts with '/', treat as a (case-insensitive) prefix.
    if (filter[0] == '/') {
        return llmk_ascii_startswith_ci(name, filter);
    }

    // Otherwise, treat as a (case-insensitive) substring.
    return llmk_ascii_contains_ci(name, filter);
}

static void llmk_ascii_copy_cap(char *dst, int dst_cap, const char *src) {
    if (!dst || dst_cap <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    int i = 0;
    for (; i < dst_cap - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static int llmk_ascii_has_dotdot(const char *s) {
    if (!s) return 0;
    for (const char *p = s; p[0] && p[1]; p++) {
        if (p[0] == '.' && p[1] == '.') return 1;
    }
    return 0;
}

static void llmk_print_ascii(const char *s) {
    if (!s) return;
    for (const char *p = s; *p; p++) {
        Print(L"%c", (CHAR16)(unsigned char)(*p));
    }
}

static int llmk_parse_optional_prefix(const char *prompt, int cmd_len, char *out, int out_cap) {
    if (!out || out_cap <= 0) return 0;
    out[0] = 0;
    if (!prompt || cmd_len <= 0) return 0;

    const char *p = prompt + cmd_len;
    while (*p && llmk_ascii_is_space(*p)) p++;
    if (*p == 0) return 0;

    int n = 0;
    while (*p && !llmk_ascii_is_space(*p) && n + 1 < out_cap) {
        out[n++] = *p++;
    }
    out[n] = 0;
    if (n <= 0) return 0;
    return 1;
}

typedef struct {
    const char *name;         // ASCII command, e.g. "/temp"
    const CHAR16 *desc;       // Wide description
} llmk_cmd_help_entry;

static const llmk_cmd_help_entry g_llmk_cmd_help[] = {
    { "/temp", L"Set temperature (0.0=greedy, 1.0=creative)" },
    { "/min_p", L"Set min_p (0.0-1.0, 0=off)" },
    { "/top_p", L"Set nucleus sampling (0.0-1.0)" },
    { "/top_k", L"Set top-k (0=off, typical 40-200)" },
    { "/norepeat", L"No-repeat ngram (0=off, typical 3-6)" },
    { "/repeat", L"Set repetition penalty (1.0=none, 1.5=strong)" },
    { "/sampling", L"Show sampling settings" },
    { "/preset", L"Apply sampling preset: stable|creative|greedy" },
    { "/preset_save", L"Apply preset and save to repl.cfg (Djibion allow_cfg_write required)" },
    { "/autostart_engines_on", L"Generate llmk-autorun.txt + enable autorun at boot (observe|enforce) [--run]" },
    { "/autostart_engines_off", L"Disable autorun_autostart in repl.cfg" },
    { "/max_tokens", L"Max generation tokens (1-256)" },
    { "/seed", L"RNG seed" },
    { "/stats", L"Print generation stats (0/1)" },
    { "/stop_you", L"Stop on \\nYou: pattern (0/1)" },
    { "/stop_nl", L"Stop on double newline (0/1)" },
    { "/model", L"Show loaded model config" },
    { "/model_info", L"Show model header (bin) or metadata (gguf)" },
    { "/models", L"List available .bin/.gguf files (root + models\\)" },
    { "/cpu", L"Show CPU SIMD status" },
    { "/ram", L"Show RAM budget (weights/kv/scratch/acts)" },
    { "/zones", L"Dump allocator zones + sentinel" },
    { "/budget", L"Set budgets in cycles (p=prefill, d=decode)" },
    { "/attn", L"Force attention SIMD path: auto|sse2|avx2" },
    { "/test_failsafe", L"One-shot strict budget trip" },
    { "/ctx", L"Show model + sampling + budgets" },
    { "/cfg", L"Show effective repl.cfg settings" },
    { "/log", L"Dump last n log entries" },
    { "/save_log", L"Write last n log entries to llmk-log.txt" },
    { "/save_dump", L"Write ctx+zones+sentinel+log to llmk-dump.txt" },
    { "/cls", L"Clear the screen" },
    { "/logo", L"Print startup ASCII logo" },
    { "/blas_bench", L"Benchmark Matrix Multiplication (Scalar vs SIMD)" },
    { "/q8_bench", L"Benchmark Q8_0 matmul (scalar vs AVX2)" },
    { "/q8_matvec", L"Benchmark Q8_0 model matvec (wq/wk/wv/wo/w1/w2/w3/cls)" },
    { "/gop", L"Show GOP framebuffer info" },
    { "/tui_on", L"Enable GOP TUI overlay" },
    { "/tui_off", L"Disable GOP TUI overlay" },
    { "/tui_toggle", L"Toggle GOP TUI overlay" },
    { "/tui_redraw", L"Force redraw GOP TUI overlay" },
    { "/tui_mode", L"Set GOP UI mode: status|log|split|files" },
    { "/tui_log_on", L"Show transcript log UI (GOP)" },
    { "/tui_log_off", L"Return to status-only UI" },
    { "/tui_log_clear", L"Clear transcript ring buffer" },
    { "/tui_log_up", L"Scroll transcript up (older)" },
    { "/tui_log_down", L"Scroll transcript down (newer)" },
    { "/tui_log_dump", L"Dump transcript to llmk-transcript.txt" },
    { "/fb", L"Open GOP file browser (same as /fb_on)" },
    { "/fb_on", L"Enable GOP file browser" },
    { "/fb_off", L"Disable GOP file browser" },
    { "/fb_refresh", L"Refresh file browser listing" },
    { "/fb_cd", L"File browser: change directory" },
    { "/fb_up", L"File browser: parent directory" },
    { "/fb_sel", L"File browser: select entry by index" },
    { "/fb_open", L"File browser: open selection (dir->cd, file->preview)" },
    { "/render", L"Render simple shapes to GOP framebuffer" },
    { "/save_img", L"Save GOP framebuffer as PPM (default llmk-img.ppm)" },
    { "/draw", L"Ask the model to output DSL and render it (GOP required)" },

    { "/fs_ls", L"List files in directory (default: root)" },
    { "/fs_cat", L"Print a text file (best-effort; truncated)" },
    { "/fs_write", L"Write text to file (truncate/create)" },
    { "/fs_append", L"Append text to file (create if missing)" },
    { "/fs_rm", L"Delete a file" },
    { "/fs_cp", L"Copy file (best-effort)" },
    { "/fs_mv", L"Move file (copy+delete best-effort)" },

    { "/snap_save", L"Save KV cache snapshot to file (fast resume)" },
    { "/snap_load", L"Load KV cache snapshot from file" },
    { "/snap_autoload_on", L"Enable snapshot auto-load at boot (writes repl.cfg)" },
    { "/snap_autoload_off", L"Disable snapshot auto-load at boot (writes repl.cfg)" },

    { "/oo_new", L"Create an entity (long-lived intention)" },
    { "/oo_list", L"List entities" },
    { "/oo_step", L"Advance one entity by one step" },
    { "/oo_run", L"Run n cooperative steps across entities" },
    { "/oo_kill", L"Kill an entity" },
    { "/oo_note", L"Append a note to entity memory" },
    { "/oo_plan", L"Add agenda action(s) (use ';' to add many; prio like +2)" },
    { "/oo_agenda", L"Show agenda action list" },
    { "/oo_next", L"Select next action (marks doing)" },
    { "/oo_done", L"Mark action #k done" },
    { "/oo_prio", L"Set priority for action #k" },
    { "/oo_edit", L"Edit text for action #k" },
    { "/oo_show", L"Show entity (goal/status/digest/notes tail)" },
    { "/oo_digest", L"Update digest + compress notes tail" },
    { "/oo_save", L"Save OO state to file (default oo-state.bin)" },
    { "/oo_load", L"Load OO state from file (default oo-state.bin)" },
    { "/oo_think", L"Ask the model, store answer in entity notes" },
    { "/oo_auto", L"Run n think->store->step cycles (auto; press 'q' or Esc to stop)" },
    { "/oo_auto_stop", L"Stop /oo_auto cycles" },
    { "/oo_exec", L"Run agenda items (n cycles). Stops when agenda empty unless --plan" },
    { "/oo_exec_stop", L"Stop /oo_exec" },

    { "/autorun", L"Run scripted REPL commands from file (default from repl.cfg)" },
    { "/autorun_stop", L"Stop autorun" },

    { "/reset", L"Clear budgets/log + untrip sentinel" },
    { "/clear", L"Clear KV cache (reset conversation context)" },
    { "/djibmarks", L"Show DjibMark execution trace" },
    { "/djibperf", L"DjibMark performance analysis by phase" },
    { "/djibion_on", L"Enable Djibion (observe mode)" },
    { "/djibion_off", L"Disable Djibion" },
    { "/djibion_enforce", L"Set Djibion mode: 0=off 1=observe 2=enforce" },
    { "/djibion_status", L"Show Djibion laws + counters" },
    { "/djibion_prefix", L"Set Djibion prefix for file actions (e.g. \\test_dir\\)" },
    { "/djibion_allow_delete", L"Set allow_fs_delete (0/1)" },
    { "/djibion_max_write", L"Set max_fs_write_bytes" },
    { "/djibion_max_oo", L"Set max_oo_cycles" },
    { "/djibion_max_snap", L"Set max_snap_bytes" },
    { "/djibion_allow_autorun", L"Set allow_autorun (0/1)" },
    { "/djibion_allow_snap_load", L"Set allow_snap_load (0/1)" },
    { "/djibion_allow_snap_save", L"Set allow_snap_save (0/1)" },
    { "/djibion_allow_cfg_write", L"Set allow_cfg_write (0/1)" },
    { "/djibion_allow_oo_persist", L"Set allow_oo_persist (0/1)" },

    { "/diopion_on", L"Enable Diopion (observe mode)" },
    { "/diopion_off", L"Disable Diopion" },
    { "/diopion_enforce", L"Set Diopion mode: 0=off 1=observe 2=enforce" },
    { "/diopion_profile", L"Set Diopion profile: none|animal|vegetal|geom|bio" },
    { "/diopion_burst", L"Burst sampling for N turns (temp/topk/max_tokens)" },
    { "/diopion_status", L"Show Diopion status + burst defaults" },

    { "/mem_on", L"Enable Memorion (manifest/check helpers)" },
    { "/mem_off", L"Disable Memorion" },
    { "/mem_status", L"Show Memorion status + counters" },
    { "/mem_snap_info", L"Print snapshot header info (default llmk-snap.bin)" },
    { "/mem_snap_check", L"Check snapshot compatibility vs current model" },
    { "/mem_manifest", L"Write manifest (optionally include snap header)" },

    { "/orch_on", L"Enable Orchestrion (observe mode)" },
    { "/orch_off", L"Disable Orchestrion" },
    { "/orch_enforce", L"Set Orchestrion mode: 0=off 1=observe 2=enforce" },
    { "/orch_status", L"Show Orchestrion status + pipeline state" },
    { "/orch_clear", L"Clear pipeline" },
    { "/orch_add", L"Add step(s) to pipeline (sep by ;)" },
    { "/orch_start", L"Start pipeline (optionally loops)" },
    { "/orch_pause", L"Pause pipeline" },
    { "/orch_resume", L"Resume pipeline" },
    { "/orch_stop", L"Stop pipeline" },

    { "/calib_on", L"Enable Calibrion (observe mode)" },
    { "/calib_off", L"Disable Calibrion" },
    { "/calib_enforce", L"Set Calibrion mode: 0=off 1=observe 2=enforce" },
    { "/calib_strategy", L"Set Calibrion strategy: none|entropy|length|quality|hybrid" },
    { "/calib_status", L"Show Calibrion status + recommendation" },
    { "/calib_reset", L"Reset Calibrion stats" },
    { "/calib_apply", L"Apply Calibrion recommendation to sampling" },

    { "/compat_on", L"Enable Compatibilion" },
    { "/compat_off", L"Disable Compatibilion" },
    { "/compat_status", L"Show platform capabilities" },
    { "/compat_probe", L"Re-probe CPU features" },

    { "/diag_on", L"Enable Diagnostion diagnostics" },
    { "/diag_off", L"Disable Diagnostion diagnostics" },
    { "/diag_status", L"Show diagnostics status + counters" },
    { "/diag_report", L"Write llmk-diag.txt report (or /diag_report <file>)" },
    { "/metrics", L"Export runtime performance metrics to LLMK_METRICS.LOG (JSON)" },
    { "/version", L"Show build version + features" },
    { "/diag", L"Display system diagnostics (GOP/RAM/CPU/models)" },
    { "/commands", L"List commands (optionally filtered)" },
    { "/help", L"Show help (optionally filtered)" },
};

static void llmk_print_commands_filtered(const char *filter) {
    int printed = 0;
    for (UINTN i = 0; i < (sizeof(g_llmk_cmd_help) / sizeof(g_llmk_cmd_help[0])); i++) {
        const char *name = g_llmk_cmd_help[i].name;
        if (!name) continue;
        if (!llmk_cmd_matches_filter(name, filter)) continue;
        Print(L"  ");
        llmk_print_ascii(name);
        Print(L"\r\n");
        printed++;
    }
    if (printed == 0) {
        Print(L"  (no matches)\r\n");
    }
}

static void llmk_print_help_filtered(const char *filter,
                                    float temperature, float min_p, float top_p,
                                    int top_k, int no_repeat_ngram, int max_gen_tokens,
                                    int stats_enabled, int stop_on_you, int stop_on_double_nl,
                                    float repeat_penalty) {
    Print(L"\r\nCommands:\r\n");
    if (filter && filter[0]) {
        Print(L"  (filter: ");
        llmk_print_ascii(filter);
        Print(L")\r\n");
    }

    int printed = 0;
    for (UINTN i = 0; i < (sizeof(g_llmk_cmd_help) / sizeof(g_llmk_cmd_help[0])); i++) {
        const char *name = g_llmk_cmd_help[i].name;
        const CHAR16 *desc = g_llmk_cmd_help[i].desc;
        if (!name || !desc) continue;
        if (!llmk_cmd_matches_filter(name, filter)) continue;

        Print(L"  ");
        llmk_print_ascii(name);
        Print(L" - %s\r\n", (CHAR16 *)desc);
        printed++;
    }

    if (printed == 0) {
        Print(L"  (no matches)\r\n");
    }

    Print(L"\r\nUsage:\r\n");
    Print(L"  /help [filter]     - Examples: /help dump ; /help /oo_\r\n");
    Print(L"  /commands [filter] - Examples: /commands save ; /commands /oo_\r\n\r\n");

    // Keep the long sections only for unfiltered help.
    if (!(filter && filter[0])) {
        Print(L"Multi-line input:\r\n");
        Print(L"  End a line with '\\' to continue; type ';;' on its own line to submit.\r\n");
        Print(L"  Use '\\\\' at end of line for a literal backslash.\r\n\r\n");
        Print(L"Render DSL:\r\n");
        Print(L"  clear R G B; rect X Y W H R G B; pixel X Y R G B\r\n\r\n");

        Print(L"Current settings:\r\n");
        Print(L"  Temperature: ");
        Print(L"%d.", (int)temperature);
        Print(L"%d\r\n", (int)((temperature - (int)temperature) * 100.0f));
        Print(L"  Min-p: ");
        Print(L"%d.", (int)min_p);
        Print(L"%d\r\n", (int)((min_p - (int)min_p) * 100.0f));
        Print(L"  Top-p: ");
        Print(L"%d.", (int)top_p);
        Print(L"%d\r\n", (int)((top_p - (int)top_p) * 100.0f));
        Print(L"  Top-k: %d\r\n", top_k);
        Print(L"  No-repeat ngram: %d\r\n", no_repeat_ngram);
        Print(L"  Max tokens: %d\r\n", max_gen_tokens);
        Print(L"  Stats: %s\r\n", stats_enabled ? L"on" : L"off");
        Print(L"  Stop on \\nYou:: %s\r\n", stop_on_you ? L"on" : L"off");
        Print(L"  Stop on double newline: %s\r\n", stop_on_double_nl ? L"on" : L"off");
        Print(L"  Repeat penalty: ");
        Print(L"%d.", (int)repeat_penalty);
        Print(L"%d\r\n\r\n", (int)((repeat_penalty - (int)repeat_penalty) * 100.0f));
    }
}

static int llmk_cmd_common_prefix_len(const char *a, const char *b) {
    int n = 0;
    if (!a || !b) return 0;
    while (a[n] && b[n] && a[n] == b[n]) n++;
    return n;
}

static void llmk_try_tab_complete_command(CHAR16 *buffer, int max_len, int *io_pos) {
    if (!buffer || !io_pos || max_len <= 1) return;
    int pos = *io_pos;
    if (pos <= 0) return;

    // Find current token start (we only complete the token that ends at the cursor).
    int token_start = pos;
    while (token_start > 0) {
        CHAR16 c = buffer[token_start - 1];
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') break;
        token_start--;
    }
    if (token_start >= pos) return;
    if (buffer[token_start] != L'/') return;

    static const char *cmds[] = {
        "/draw",
        "/temp",
        "/min_p",
        "/top_p",
        "/top_k",
        "/max_tokens",
        "/seed",
        "/stats",
        "/stop_you",
        "/stop_nl",
        "/norepeat",
        "/repeat",
        "/sampling",
        "/preset",
        "/preset_save",
        "/autostart_engines_on",
        "/autostart_engines_off",
        "/model",
        "/model_info",
        "/models",
        "/cpu",
        "/zones",
        "/budget",
        "/attn",
        "/test_failsafe",
        "/ctx",
        "/log",
        "/save_log",
        "/save_dump",
        "/diag_on",
        "/diag_off",
        "/diag_status",
        "/diag_report",
        "/mem_on",
        "/mem_off",
        "/mem_status",
        "/mem_snap_info",
        "/mem_snap_check",
        "/mem_manifest",
        "/orch_on",
        "/orch_off",
        "/orch_enforce",
        "/orch_status",
        "/orch_clear",
        "/orch_add",
        "/orch_start",
        "/orch_pause",
        "/orch_resume",
        "/orch_stop",
        "/calib_on",
        "/calib_off",
        "/calib_enforce",
        "/calib_strategy",
        "/calib_status",
        "/calib_reset",
        "/calib_apply",
        "/compat_on",
        "/compat_off",
        "/compat_status",
        "/compat_probe",
        "/gop",
        "/render",
        "/save_img",
        "/oo_new",
        "/oo_list",
        "/oo_kill",
        "/oo_step",
        "/oo_run",
        "/oo_note",
        "/oo_show",
        "/oo_digest",
        "/oo_plan",
        "/oo_agenda",
        "/oo_next",
        "/oo_done",
        "/oo_prio",
        "/oo_edit",
        "/oo_save",
        "/oo_load",
        "/oo_think",
        "/oo_auto",
        "/oo_auto_stop",
        "/autorun",
        "/autorun_stop",
        "/reset",
        "/clear",
        "/version",
        "/diag",
        "/djibmarks",
        "/djibperf",
        "/djibion_on",
        "/djibion_off",
        "/djibion_enforce",
        "/djibion_status",
        "/djibion_prefix",
        "/djibion_allow_delete",
        "/djibion_max_write",
        "/djibion_max_oo",
        "/djibion_max_snap",
        "/djibion_allow_autorun",
        "/djibion_allow_snap_load",
        "/djibion_allow_snap_save",
        "/djibion_allow_cfg_write",
        "/djibion_allow_oo_persist",
        "/diopion_on",
        "/diopion_off",
        "/diopion_enforce",
        "/diopion_profile",
        "/diopion_burst",
        "/diopion_status",
        "/logo",
        "/commands",
        "/help",
    };

    // If there's no active session (or token start changed), seed a new cycling session
    // from the token currently under the cursor.
    if (!g_tab_cycle_active || g_tab_cycle_token_start != token_start) {
        llmk_tab_cycle_reset();

        char prefix[64];
        int p = 0;
        for (int i = token_start; i < pos && p + 1 < (int)sizeof(prefix); i++) {
            CHAR16 c = buffer[i];
            if (c < 0x20 || c > 0x7E) return;
            prefix[p++] = (char)c;
        }
        prefix[p] = 0;
        if (p <= 1) return; // just "/" -> do nothing

        llmk_ascii_copy_cap(g_tab_cycle_prefix, (int)sizeof(g_tab_cycle_prefix), prefix);
        g_tab_cycle_active = 1;
        g_tab_cycle_index = -1;
        g_tab_cycle_token_start = token_start;
    } else {
        // Ensure the current token still begins with the session prefix. If not, restart.
        int p = (int)my_strlen(g_tab_cycle_prefix);
        if (p <= 1) {
            llmk_tab_cycle_reset();
            return;
        }
        if (pos - token_start < p) {
            llmk_tab_cycle_reset();
            return;
        }
        for (int i = 0; i < p; i++) {
            CHAR16 c = buffer[token_start + i];
            if ((c < 0x20 || c > 0x7E) || (char)c != g_tab_cycle_prefix[i]) {
                llmk_tab_cycle_reset();
                // Re-run once from scratch (no recursion).
                char prefix[64];
                int pp = 0;
                for (int j = token_start; j < pos && pp + 1 < (int)sizeof(prefix); j++) {
                    CHAR16 cj = buffer[j];
                    if (cj < 0x20 || cj > 0x7E) return;
                    prefix[pp++] = (char)cj;
                }
                prefix[pp] = 0;
                if (pp <= 1) return;
                llmk_ascii_copy_cap(g_tab_cycle_prefix, (int)sizeof(g_tab_cycle_prefix), prefix);
                g_tab_cycle_active = 1;
                g_tab_cycle_index = -1;
                g_tab_cycle_token_start = token_start;
                break;
            }
        }
    }

    // Build match list from the session prefix.
    const char *matches[64];
    int match_count = 0;
    const char *first = NULL;
    for (UINTN i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); i++) {
        if (llmk_ascii_startswith(cmds[i], g_tab_cycle_prefix)) {
            if (match_count < (int)(sizeof(matches) / sizeof(matches[0]))) {
                matches[match_count++] = cmds[i];
            }
            if (!first) first = cmds[i];
        }
    }

    if (match_count <= 0 || !first) {
        llmk_tab_cycle_reset();
        return;
    }

    int base_len = (int)my_strlen(g_tab_cycle_prefix);
    if (base_len <= 1) return;

    // Common prefix across matches.
    int common_len = (int)my_strlen(first);
    for (int i = 0; i < match_count; i++) {
        int cpl = llmk_cmd_common_prefix_len(first, matches[i]);
        if (cpl < common_len) common_len = cpl;
    }

    int cur_token_len = pos - token_start;

    // 1) If there's a longer common prefix, extend to it (first Tab behavior).
    if (common_len > base_len && cur_token_len < common_len) {
        for (int i = cur_token_len; i < common_len; i++) {
            if (pos + 1 >= max_len) break;
            char c = first[i];
            buffer[pos++] = (CHAR16)c;
            Print(L"%c", (CHAR16)c);
        }
        buffer[pos] = 0;
        *io_pos = pos;
        return;
    }

    // 2) Otherwise, cycle through full command candidates.
    if (g_tab_cycle_index < 0) g_tab_cycle_index = 0;
    else g_tab_cycle_index = (g_tab_cycle_index + 1) % match_count;

    const char *candidate = matches[g_tab_cycle_index];
    if (!candidate) return;

    // Replace current token with candidate.
    llmk_console_erase_chars(cur_token_len);
    pos = token_start;

    for (int i = 0; candidate[i] && pos + 1 < max_len; i++) {
        buffer[pos++] = (CHAR16)candidate[i];
        Print(L"%c", (CHAR16)candidate[i]);
    }
    buffer[pos] = 0;
    *io_pos = pos;
}

void read_user_input(CHAR16* buffer, int max_len) {
    int pos = 0;
    EFI_INPUT_KEY Key;
    int line_start = 0;

    // History browsing (single-line only, for simplicity).
    int hist_n = -1; // -1 = draft, 0 = last entry, 1 = one before...
    CHAR16 draft[LLMK_INPUT_HIST_MAXLEN];
    draft[0] = 0;
    
    while (pos < max_len - 1) {
        // Wait for key (Polling with UI Update for SentienceOS)
        while (1) {
             EFI_STATUS Status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
             if (!EFI_ERROR(Status)) break;
             InterfaceFx_Tick(); // Animate Desktop
             uefi_call_wrapper(BS->Stall, 1, 10000); // 10ms stall
        }
        // UINTN index;
        // uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
        // uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);

        // Any non-Tab key cancels completion cycling.
        if (Key.UnicodeChar != L'\t') {
            llmk_tab_cycle_reset();
        }
        
        // History navigation (only when still on the first line).
        if ((Key.ScanCode == SCAN_UP || Key.ScanCode == SCAN_DOWN) && line_start == 0) {
            if (g_input_hist_count <= 0) continue;

            if (Key.ScanCode == SCAN_UP) {
                if (hist_n + 1 >= g_input_hist_count) continue;
                if (hist_n < 0) {
                    // Save current draft on first UP.
                    llmk_str16_copy_cap(draft, (int)(sizeof(draft) / sizeof(draft[0])), buffer);
                }
                hist_n++;
            } else {
                // SCAN_DOWN
                if (hist_n < 0) continue;
                hist_n--;
            }

            // Erase current line and replace with history/draft.
            llmk_console_erase_chars(pos);
            pos = 0;

            const CHAR16 *src = NULL;
            if (hist_n >= 0) {
                src = llmk_hist_get_nth_from_last(hist_n);
            } else {
                src = draft;
            }
            if (!src) src = L"";

            // Copy + print.
            int slen = llmk_str16_len_cap(src, max_len - 1);
            for (int i = 0; i < slen; i++) {
                buffer[i] = src[i];
            }
            pos = slen;
            buffer[pos] = 0;
            if (pos > 0) {
                Print(L"%s", buffer);
            }
            continue;
        }

        // Tab completion (single-line only)
        if (Key.UnicodeChar == L'\t' && line_start == 0) {
            llmk_try_tab_complete_command(buffer, max_len, &pos);
            continue;
        }

        if (Key.UnicodeChar == 0x000D) {  // Enter
            // If the user ends the line with "\\\\", treat it as a literal "\\" and do NOT continue.
            if (pos >= 2 && buffer[pos - 2] == L'\\' && buffer[pos - 1] == L'\\') {
                pos -= 1;
                buffer[pos - 1] = L'\\';
            } else {
                // Multi-line continuation: if the current line ends with '\\', continue.
                if (pos > 0 && buffer[pos - 1] == L'\\') {
                    buffer[pos - 1] = L'\n';  // Replace \ with newline
                    Print(L"\r\n... ");
                    line_start = pos;
                    continue;
                }
            }

            // Multi-line terminator: line is exactly ";;" on its own line.
            if ((pos - line_start) == 2 && buffer[line_start] == L';' && buffer[line_start + 1] == L';') {
                // Remove terminator line and the preceding newline if present.
                if (line_start > 0 && buffer[line_start - 1] == L'\n') {
                    pos = line_start - 1;
                } else {
                    pos = line_start;
                }
                buffer[pos] = 0;
                Print(L"\r\n");
                break;
            }
            buffer[pos] = 0;
            Print(L"\r\n");
            break;
        } else if (Key.UnicodeChar == 0x0008) {  // Backspace
            if (pos > line_start) {
                pos--;
                Print(L"\b \b");
            }
        } else if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
            buffer[pos++] = Key.UnicodeChar;
            Print(L"%c", Key.UnicodeChar);
        }
    }
    
    buffer[pos] = 0;

    // Add to history (single-line only, non-empty).
    if (line_start == 0 && pos > 0 && !llmk_str16_has_newline(buffer)) {
        llmk_hist_add_line(buffer);
    }
}

void char16_to_char(char* dest, CHAR16* src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = (char)src[i];
    }
    dest[i] = 0;
}

static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len) {
    if (!dst || max_len <= 0) return;
    int i = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (; i < max_len - 1 && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x20 || c > 0x7E) {
            dst[i] = L'_';
        } else {
            dst[i] = (CHAR16)c;
        }
    }
    dst[i] = 0;
}

int check_quit_command(char* text) {
    // Check for "quit" or "exit"
    if (my_strcmp(text, "quit") == 0 || my_strcmp(text, "exit") == 0) {
        return 1;
    }
    return 0;
}

void reset_kv_cache(RunState* s, Config* p) {
    // Clear KV cache for new conversation
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int cache_size = p->n_layers * p->seq_len * kv_dim;
    
    for (int i = 0; i < cache_size; i++) {
        s->key_cache[i] = 0.0f;
        s->value_cache[i] = 0.0f;
    }
    
    // M16.1: Track KV cache resets
    g_metrics.kv_cache_resets++;
}

static EFI_STATUS llmk_snap_load_into_state_best_effort(RunState *state, const Config *config, int *io_kv_pos, const CHAR16 *in_name) {
    if (!state || !config || !io_kv_pos || !in_name) return EFI_INVALID_PARAMETER;
    if (!g_llmk_ready) return EFI_NOT_READY;

    EFI_FILE_HANDLE f = NULL;
    CHAR16 picked[192];
    picked[0] = 0;
    EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, in_name, &f, picked,
                                                      (int)(sizeof(picked) / sizeof(picked[0])),
                                                      L"snap_load");
    if (EFI_ERROR(st) || !f) return st;

    LlmkSnapHeader hdr;
    st = read_exact(f, &hdr, sizeof(hdr));
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(f->Close, 1, f);
        return st;
    }
    if (hdr.magic != LLMK_SNAP_MAGIC || hdr.version != 1) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_COMPROMISED_DATA;
    }
    if (hdr.dim != (UINT32)config->dim ||
        hdr.n_layers != (UINT32)config->n_layers ||
        hdr.n_heads != (UINT32)config->n_heads ||
        hdr.n_kv_heads != (UINT32)config->n_kv_heads ||
        hdr.seq_len != (UINT32)config->seq_len) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_INCOMPATIBLE_VERSION;
    }
    if (hdr.kv_pos == 0 || hdr.kv_pos > (UINT32)config->seq_len) {
        uefi_call_wrapper(f->Close, 1, f);
        return EFI_INVALID_PARAMETER;
    }

    // Clear caches, then load prefix.
    reset_kv_cache(state, (Config *)config);

    int kv_dim = (int)hdr.kv_dim;
    UINTN slice_floats = (UINTN)hdr.kv_pos * (UINTN)kv_dim;
    UINTN slice_bytes = slice_floats * sizeof(float);

    for (int l = 0; l < config->n_layers && !EFI_ERROR(st); l++) {
        float *base = state->key_cache + (UINTN)l * (UINTN)config->seq_len * (UINTN)kv_dim;
        st = read_exact(f, base, slice_bytes);
    }
    for (int l = 0; l < config->n_layers && !EFI_ERROR(st); l++) {
        float *base = state->value_cache + (UINTN)l * (UINTN)config->seq_len * (UINTN)kv_dim;
        st = read_exact(f, base, slice_bytes);
    }
    uefi_call_wrapper(f->Close, 1, f);

    if (EFI_ERROR(st)) {
        *io_kv_pos = 0;
        g_llmk_kv_pos = 0;
        return st;
    }

    *io_kv_pos = (int)hdr.kv_pos;
    g_llmk_kv_pos = *io_kv_pos;
    return EFI_SUCCESS;
}

// ============================================================================
// OO M5: LLM Consult Implementation
// ============================================================================

static void llmk_oo_log_consultation(UINT64 boot_count, UINT32 mode, UINT64 ram_mb,
                                     int ctx, int seq, const char *suggestion,
                                     const char *decision, int applied,
                                     int confidence_score, int confidence_threshold,
                                     int confidence_gate_enabled);

static int llmk_oo_confidence_score(UINT32 mode, UINT64 ram_mb, int ctx, int seq,
                                    int llm_len,
                                    int action_reduce_ctx, int action_reduce_seq,
                                    int action_increase, int action_reboot,
                                    int action_model, int action_stable,
                                    int *out_feedback_bias,
                                    int *out_feedback_good,
                                    int *out_feedback_bad) {
    int score = 50;
    int feedback_bias = 0;

    if (out_feedback_bias) *out_feedback_bias = 0;
    if (out_feedback_good) *out_feedback_good = 0;
    if (out_feedback_bad) *out_feedback_bad = 0;

    if (mode == LLMK_OO_MODE_NORMAL) score += 20;
    else if (mode == LLMK_OO_MODE_DEGRADED) score += 10;

    if (ram_mb >= 1024ULL) score += 15;
    else if (ram_mb >= 768ULL) score += 8;
    else score += 2;

    if (ctx <= 512) score += 5;
    else if (ctx > 2048) score -= 5;

    if (seq <= 1024) score += 5;
    else if (seq > 2048) score -= 5;

    if (llm_len <= 0) score -= 15;

    if (action_stable) score += 10;
    if (action_reduce_ctx || action_reduce_seq) score += 5;
    if (action_increase && mode != LLMK_OO_MODE_NORMAL) score -= 10;
    if (action_reboot || action_model) score -= 5;

    {
        int rg = 0, rb = 0, ig = 0, ib = 0;
        llmk_oo_outcome_feedback_recent_best_effort(&rg, &rb, &ig, &ib);

        int wants_reduce = (action_reduce_ctx || action_reduce_seq) ? 1 : 0;
        int wants_increase = action_increase ? 1 : 0;

        if (wants_reduce) {
            int delta = rg - rb;
            if (delta > 0) feedback_bias += (delta >= 3) ? 8 : 4;
            else if (delta < 0) feedback_bias -= ((-delta) >= 3) ? 10 : 5;
            if (out_feedback_good) *out_feedback_good += rg;
            if (out_feedback_bad) *out_feedback_bad += rb;
        }

        if (wants_increase) {
            int delta = ig - ib;
            if (delta > 0) feedback_bias += (delta >= 2) ? 6 : 3;
            else if (delta < 0) feedback_bias -= ((-delta) >= 2) ? 8 : 4;
            if (out_feedback_good) *out_feedback_good += ig;
            if (out_feedback_bad) *out_feedback_bad += ib;
        }
    }

    score += feedback_bias;
    if (out_feedback_bias) *out_feedback_bias = feedback_bias;

    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return score;
}

static void llmk_oo_consult_process_suggestion(UINT64 ram_mb, UINT32 mode, UINT64 boots,
                                               int ctx, int seq,
                                               const char *llm_suggestion) {
    if (!llm_suggestion) llm_suggestion = "";

    // Clamp suggestion length to local buffers.
    int llm_len = my_strlen(llm_suggestion);
    if (llm_len < 0) llm_len = 0;
    if (llm_len > 120) llm_len = 120;

    // Emit LLM suggestion marker (deterministic)
    Print(L"OK: OO LLM suggested: ");
    {
        char tmp[128];
        int tp = 0;
        for (int i = 0; i < llm_len && tp + 1 < (int)sizeof(tmp); i++) {
            char c = llm_suggestion[i];
            if (c < 0x20 || c > 0x7E) c = '_';
            if (c == '"') c = '\'';
            tmp[tp++] = c;
        }
        tmp[tp] = 0;
        llmk_print_ascii(tmp);
    }
    Print(L"\r\n");

    // 5. Parse suggestion for keywords (M5.1: detect ALL keywords)
    int action_reduce_ctx = 0;
    int action_reduce_seq = 0;
    int action_increase = 0;
    int action_reboot = 0;
    int action_model = 0;
    int action_stable = 0;

    // Simple substring search (case-insensitive)
    char lower[128];
    int copy_len = llm_len;
    if (copy_len > (int)sizeof(lower) - 1) copy_len = (int)sizeof(lower) - 1;
    for (int i = 0; i < copy_len; i++) {
        char c = llm_suggestion[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        lower[i] = c;
    }
    lower[copy_len] = 0;

    // Check for multi-actions feature flag (default: follows oo_llm_consult)
    int multi_enabled = g_cfg_oo_multi_actions;
    if (multi_enabled < 0) {
        multi_enabled = (g_cfg_oo_llm_consult > 0) ? 1 : 0;
    }

    // Detect reduce actions (ctx and/or seq)
    if (my_strstr(lower, "reduce") || my_strstr(lower, "lower") || my_strstr(lower, "decrease")) {
        if (my_strstr(lower, "ctx") || my_strstr(lower, "context")) action_reduce_ctx = 1;
        if (my_strstr(lower, "seq") || my_strstr(lower, "sequence")) action_reduce_seq = 1;
        // Generic "reduce" without target: default to ctx
        if (!action_reduce_ctx && !action_reduce_seq) action_reduce_ctx = 1;
    }

    // Detect increase (blocked in most cases)
    if (my_strstr(lower, "increase") || my_strstr(lower, "raise") || my_strstr(lower, "more")) {
        action_increase = 1;
    }

    // Detect system actions (reboot, model change)
    if (my_strstr(lower, "reboot") || my_strstr(lower, "restart")) action_reboot = 1;
    if (my_strstr(lower, "model") || my_strstr(lower, "switch")) action_model = 1;

    // Detect "stable" / no-op signal
    if (my_strstr(lower, "stable") || my_strstr(lower, "ok") || my_strstr(lower, "wait") || my_strstr(lower, "good")) {
        action_stable = 1;
    }

        int confidence_threshold = g_cfg_oo_conf_threshold;
        if (confidence_threshold < 0) confidence_threshold = 0;
        if (confidence_threshold > 100) confidence_threshold = 100;
        int confidence_gate_enabled = (g_cfg_oo_conf_gate != 0);
    int feedback_bias = 0;
    int feedback_good = 0;
    int feedback_bad = 0;
    int confidence_score = llmk_oo_confidence_score(mode, ram_mb, ctx, seq, llm_len,
                                                    action_reduce_ctx, action_reduce_seq,
                                                    action_increase, action_reboot,
                                                    action_model, action_stable,
                                                    &feedback_bias, &feedback_good, &feedback_bad);
        int confidence_gate_pass = (confidence_score >= confidence_threshold);
        int plan_hard_stop = (confidence_gate_enabled && !confidence_gate_pass) ? 1 : 0;
        const char *confidence_reason_id = (!confidence_gate_enabled) ? "OO_CONF_LOG_ONLY" :
                           (confidence_gate_pass ? "OO_CONF_GATE_PASS" : "OO_CONF_GATE_FAIL");

        int plan_enabled = ((g_cfg_oo_plan_enable != 0) && multi_enabled) ? 1 : 0;
        int plan_max_actions = g_cfg_oo_plan_max_actions;
        if (plan_max_actions < 1) plan_max_actions = 1;
        if (plan_max_actions > 3) plan_max_actions = 3;
        if (!plan_enabled) plan_max_actions = 1;

        int plan_remaining_budget = plan_max_actions - g_oo_auto_applied_count_this_boot;
        if (plan_remaining_budget < 0) plan_remaining_budget = 0;
        int plan_applied_now = 0;
        int plan_checkpoint_done = 0;

        Print(L"OK: OO confidence: score=%d threshold=%d gate=%a pass=%a reason_id=%a\r\n",
            confidence_score,
            confidence_threshold,
            confidence_gate_enabled ? "enforced" : "log_only",
            confidence_gate_pass ? "yes" : "no",
            confidence_reason_id);
        Print(L"OK: OO feedback: good=%d bad=%d bias=%d\r\n",
            feedback_good, feedback_bad, feedback_bias);
        Print(L"OK: OO plan: enabled=%a max=%d used=%d remain=%d hard_stop=%a reason_id=%a\r\n",
            plan_enabled ? "yes" : "no",
            plan_max_actions,
            g_oo_auto_applied_count_this_boot,
            plan_remaining_budget,
            plan_hard_stop ? "yes" : "no",
            plan_hard_stop ? "OO_PLAN_HARD_STOP" : "OO_PLAN_ACTIVE");

        if (!confidence_gate_enabled) {
            llmk_oo_journal_event_load_state_best_effort("confidence gate=log_only pass=yes reason_id=OO_CONF_LOG_ONLY");
        } else if (confidence_gate_pass) {
            llmk_oo_journal_event_load_state_best_effort("confidence gate=enforced pass=yes reason_id=OO_CONF_GATE_PASS");
        } else {
            llmk_oo_journal_event_load_state_best_effort("confidence gate=enforced pass=no reason_id=OO_CONF_GATE_FAIL");
        }

        if (plan_hard_stop) {
            llmk_oo_journal_event_load_state_best_effort("plan status=hard_stop reason_id=OO_PLAN_HARD_STOP");
        } else {
            llmk_oo_journal_event_load_state_best_effort("plan status=active reason_id=OO_PLAN_ACTIVE");
        }

    // 6. Policy decision (M5.1: apply ALL valid actions when multi_enabled)
    int actions_applied = 0;
    int actions_blocked = 0;
    char batch_summary[256];
    int batch_summary_pos = 0;
    batch_summary[0] = 0;

    // Priority filtering: stable cancels all, reboot primes others
    if (action_stable) {
        // Stable signal: no action needed
        Print(L"OK: OO policy decided: system_stable (reason=llm_reports_ok reason_id=OO_STABLE_OK)\r\n");
        actions_applied = 0;
        actions_blocked = (action_reduce_ctx + action_reduce_seq + action_increase + action_reboot + action_model - 1);
        llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "stable");
    } else if (action_reboot) {
        // Reboot primes others: log but don't auto-apply (v0)
        Print(L"OK: OO policy decided: logged_only (reason=reboot_not_auto reason_id=OO_REBOOT_LOG_ONLY)\r\n");
        actions_applied = 0;
        actions_blocked = (action_reduce_ctx + action_reduce_seq + action_increase + action_model);
        llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "reboot_logged");
    } else {
        // Apply reductions first (safe), then increases (blocked), then model

        // 6.1: Apply reduce_ctx (if detected and safe)
        if (action_reduce_ctx) {
            if (mode == LLMK_OO_MODE_SAFE || mode == LLMK_OO_MODE_DEGRADED) {
                int new_ctx = ctx / 2;
                if (new_ctx < 128) new_ctx = 128;
                if (new_ctx != ctx) {
                    // M5.2: Check auto-apply config and throttling
                    int can_auto_apply = (g_cfg_oo_auto_apply > 0) && (!plan_hard_stop) &&
                                         (plan_applied_now < plan_remaining_budget) &&
                                         (!confidence_gate_enabled || confidence_gate_pass);
                    int is_reduction = 1; // reduce_ctx is always a reduction

                    if (!can_auto_apply) {
                        // Auto-apply disabled or throttled
                        if (confidence_gate_enabled && !confidence_gate_pass) {
                                Print(L"OK: OO policy blocked: reduce_ctx (reason=confidence_below_threshold reason_id=OO_BLOCK_CONFIDENCE score=%d threshold=%d)\r\n",
                                  confidence_score, confidence_threshold);
                        } else if (g_cfg_oo_auto_apply == 0) {
                            Print(L"OK: OO policy simulation: reduce_ctx (would_apply_if_enabled, new=%d)\r\n", new_ctx);
                        } else if (plan_hard_stop) {
                            Print(L"OK: OO policy blocked: reduce_ctx (reason=hard_stop_active reason_id=OO_BLOCK_HARD_STOP, new=%d)\r\n", new_ctx);
                        } else {
                            Print(L"OK: OO policy throttled: reduce_ctx (reason=plan_budget_exhausted reason_id=OO_BLOCK_PLAN_BUDGET, new=%d)\r\n", new_ctx);
                        }
                        actions_blocked++;
                    } else if (g_cfg_oo_auto_apply == 1 && !is_reduction) {
                        // Conservative mode: only reductions (this branch won't hit for reduce_ctx)
                        Print(L"OK: OO policy blocked: reduce_ctx (reason=conservative_mode)\r\n");
                        actions_blocked++;
                    } else {
                        if (!plan_checkpoint_done) {
                            llmk_oo_plan_checkpoint_best_effort("pre_auto_apply");
                            plan_checkpoint_done = 1;
                        }
                        // Auto-apply enabled: write + verify per M5.2
                        int ok = llmk_oo_auto_apply_write_verify_best_effort("reduce_ctx",
                                                                            "ctx_len",
                                                                            ctx,
                                                                            seq,
                                                                            new_ctx,
                                                                            seq,
                                                                            ram_mb);
                        if (ok) {
                            Print(L"OK: OO auto-apply: reduce_ctx (old=%d new=%d check=pass reason_id=OO_APPLY_OK)\r\n", ctx, new_ctx);
                            llmk_oo_journal_event_load_state_best_effort("auto_apply action=reduce_ctx result=success reason_id=OO_APPLY_OK");
                            llmk_oo_record_last_auto_apply_best_effort(boots, mode, LLMK_OO_ACTION_REDUCE_CTX);
                            actions_applied++;
                            plan_applied_now++;
                            g_oo_auto_applied_count_this_boot++;
                            g_oo_auto_applied_this_boot = (g_oo_auto_applied_count_this_boot > 0) ? 1 : 0;
                            if (batch_summary_pos > 0) llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, ",");
                            llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "reduce_ctx");
                        } else {
                            // Revert to previous value (best-effort)
                            char oval[32];
                            int op = 0;
                            llmk_ascii_append_u64(oval, (int)sizeof(oval), &op, (UINT64)ctx);
                            oval[op] = 0;
                            llmk_repl_cfg_set_kv_best_effort("ctx_len", oval);
                            Print(L"ERROR: OO auto-apply verification failed: reduce_ctx (reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED, reverting)\r\n");
                            llmk_oo_journal_event_load_state_best_effort("auto_apply action=reduce_ctx result=failed reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED");
                            llmk_oo_journal_event_load_state_best_effort("plan_hard_stop reason=verify_failed action=reduce_ctx reason_id=OO_PLAN_HARD_STOP");
                            plan_hard_stop = 1;
                            actions_blocked++;
                        }
                    }
                } else {
                    Print(L"OK: OO policy blocked: reduce_ctx (reason=already_at_min)\r\n");
                    actions_blocked++;
                }
            } else {
                Print(L"OK: OO policy blocked: reduce_ctx (reason=normal_mode_no_auto_reduce)\r\n");
                actions_blocked++;
            }
        }

        // 6.2: Apply reduce_seq (if detected, multi_enabled, and safe)
        if (action_reduce_seq && multi_enabled) {
            if (mode == LLMK_OO_MODE_SAFE && ram_mb < 1024ULL) {
                int new_seq = seq / 2;
                if (new_seq < 128) new_seq = 128;
                if (new_seq != seq) {
                    // M5.2: Check auto-apply config and throttling
                    int can_auto_apply = (g_cfg_oo_auto_apply > 0) && (!plan_hard_stop) &&
                                         (plan_applied_now < plan_remaining_budget) &&
                                         (!confidence_gate_enabled || confidence_gate_pass);

                    if (!can_auto_apply) {
                        if (confidence_gate_enabled && !confidence_gate_pass) {
                                Print(L"OK: OO policy blocked: reduce_seq (reason=confidence_below_threshold reason_id=OO_BLOCK_CONFIDENCE score=%d threshold=%d)\r\n",
                                  confidence_score, confidence_threshold);
                        } else if (g_cfg_oo_auto_apply == 0) {
                            Print(L"OK: OO policy simulation: reduce_seq (would_apply_if_enabled, new=%d)\r\n", new_seq);
                        } else if (plan_hard_stop) {
                            Print(L"OK: OO policy blocked: reduce_seq (reason=hard_stop_active reason_id=OO_BLOCK_HARD_STOP, new=%d)\r\n", new_seq);
                        } else {
                            Print(L"OK: OO policy throttled: reduce_seq (reason=plan_budget_exhausted reason_id=OO_BLOCK_PLAN_BUDGET, new=%d)\r\n", new_seq);
                        }
                        actions_blocked++;
                    } else {
                        if (!plan_checkpoint_done) {
                            llmk_oo_plan_checkpoint_best_effort("pre_auto_apply");
                            plan_checkpoint_done = 1;
                        }
                        int ok = llmk_oo_auto_apply_write_verify_best_effort("reduce_seq",
                                                                            "seq_len",
                                                                            ctx,
                                                                            seq,
                                                                            ctx,
                                                                            new_seq,
                                                                            ram_mb);
                        if (ok) {
                            Print(L"OK: OO auto-apply: reduce_seq (old=%d new=%d check=pass reason_id=OO_APPLY_OK)\r\n", seq, new_seq);
                            llmk_oo_journal_event_load_state_best_effort("auto_apply action=reduce_seq result=success reason_id=OO_APPLY_OK");
                            llmk_oo_record_last_auto_apply_best_effort(boots, mode, LLMK_OO_ACTION_REDUCE_SEQ);
                            actions_applied++;
                            plan_applied_now++;
                            g_oo_auto_applied_count_this_boot++;
                            g_oo_auto_applied_this_boot = (g_oo_auto_applied_count_this_boot > 0) ? 1 : 0;
                            if (batch_summary_pos > 0) llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, ",");
                            llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "reduce_seq");
                        } else {
                            char oval[32];
                            int op = 0;
                            llmk_ascii_append_u64(oval, (int)sizeof(oval), &op, (UINT64)seq);
                            oval[op] = 0;
                            llmk_repl_cfg_set_kv_best_effort("seq_len", oval);
                            Print(L"ERROR: OO auto-apply verification failed: reduce_seq (reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED, reverting)\r\n");
                            llmk_oo_journal_event_load_state_best_effort("auto_apply action=reduce_seq result=failed reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED");
                            llmk_oo_journal_event_load_state_best_effort("plan_hard_stop reason=verify_failed action=reduce_seq reason_id=OO_PLAN_HARD_STOP");
                            plan_hard_stop = 1;
                            actions_blocked++;
                        }
                    }
                } else {
                    Print(L"OK: OO policy blocked: reduce_seq (reason=already_at_min)\r\n");
                    actions_blocked++;
                }
            } else {
                Print(L"OK: OO policy blocked: reduce_seq (reason=not_safe_low_ram)\r\n");
                actions_blocked++;
            }
        } else if (action_reduce_seq && !multi_enabled) {
            Print(L"OK: OO policy blocked: reduce_seq (reason=multi_actions_disabled)\r\n");
            actions_blocked++;
        }

        // 6.3: Handle increases (M5.2: allow in aggressive mode if conditions met)
        if (action_increase) {
            // M5.2: Aggressive mode can apply increases if RAM>=1GB and mode=NORMAL
            int can_increase = (g_cfg_oo_auto_apply == 2) && (mode == LLMK_OO_MODE_NORMAL) && (ram_mb >= 1024ULL);
            int can_auto_apply = (g_cfg_oo_auto_apply > 0) && (!plan_hard_stop) &&
                                 (plan_applied_now < plan_remaining_budget) &&
                                 (!confidence_gate_enabled || confidence_gate_pass);

            if (can_increase && can_auto_apply) {
                // Apply increase: double ctx (capped at 2048)
                int new_ctx = ctx * 2;
                if (new_ctx > 2048) new_ctx = 2048;
                if (new_ctx != ctx) {
                    if (!plan_checkpoint_done) {
                        llmk_oo_plan_checkpoint_best_effort("pre_auto_apply");
                        plan_checkpoint_done = 1;
                    }
                    int ok = llmk_oo_auto_apply_write_verify_best_effort("increase_ctx",
                                                                        "ctx_len",
                                                                        ctx,
                                                                        seq,
                                                                        new_ctx,
                                                                        seq,
                                                                        ram_mb);
                    if (ok) {
                        Print(L"OK: OO auto-apply: increase_ctx (old=%d new=%d check=pass mode=aggressive reason_id=OO_APPLY_OK)\r\n", ctx, new_ctx);
                        llmk_oo_journal_event_load_state_best_effort("auto_apply action=increase_ctx result=success reason_id=OO_APPLY_OK");
                        llmk_oo_record_last_auto_apply_best_effort(boots, mode, LLMK_OO_ACTION_INCREASE_CTX);
                        actions_applied++;
                        plan_applied_now++;
                        g_oo_auto_applied_count_this_boot++;
                        g_oo_auto_applied_this_boot = (g_oo_auto_applied_count_this_boot > 0) ? 1 : 0;
                        if (batch_summary_pos > 0) llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, ",");
                        llmk_ascii_append_str(batch_summary, (int)sizeof(batch_summary), &batch_summary_pos, "increase_ctx");
                    } else {
                        char oval[32];
                        int op = 0;
                        llmk_ascii_append_u64(oval, (int)sizeof(oval), &op, (UINT64)ctx);
                        oval[op] = 0;
                        llmk_repl_cfg_set_kv_best_effort("ctx_len", oval);
                        Print(L"ERROR: OO auto-apply verification failed: increase_ctx (reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED, reverting)\r\n");
                        llmk_oo_journal_event_load_state_best_effort("auto_apply action=increase_ctx result=failed reason=verify_failed reason_id=OO_APPLY_VERIFY_FAILED");
                        llmk_oo_journal_event_load_state_best_effort("plan_hard_stop reason=verify_failed action=increase_ctx reason_id=OO_PLAN_HARD_STOP");
                        plan_hard_stop = 1;
                        actions_blocked++;
                    }
                } else {
                    Print(L"OK: OO policy blocked: increase_ctx (reason=already_at_max)\r\n");
                    actions_blocked++;
                }
            } else {
                const char *block_reason = (!can_auto_apply && confidence_gate_enabled && !confidence_gate_pass) ? "confidence_below_threshold" :
                                           (!can_auto_apply && g_cfg_oo_auto_apply == 0) ? "auto_apply_disabled" :
                                           (!can_auto_apply && plan_hard_stop) ? "hard_stop_active" :
                                           (!can_auto_apply) ? "plan_budget_exhausted" :
                                           (mode == LLMK_OO_MODE_SAFE) ? "safe_mode_no_increase" :
                                           (ram_mb < 1024ULL) ? "low_ram_no_increase" :
                                           (g_cfg_oo_auto_apply < 2) ? "conservative_mode_no_increase" :
                                           "increase_blocked";
                Print(L"OK: OO policy blocked: increase (reason=%a reason_id=OO_BLOCK_DYNAMIC)\r\n", block_reason);
                actions_blocked++;
            }
        }

        // 6.4: Log model change (not auto-applied in v0)
        if (action_model) {
            Print(L"OK: OO policy decided: logged_only (reason=model_change_not_auto reason_id=OO_MODEL_LOG_ONLY)\r\n");
            actions_blocked++;
        }

        // If no actions applied/blocked, mark as no actionable keywords
        if (actions_applied == 0 && actions_blocked == 0) {
            Print(L"OK: OO policy decided: ignored (reason=no_actionable_keyword reason_id=OO_NO_ACTIONABLE_KEYWORD)\r\n");
        }
    }

    // Emit batch summary (M5.1)
    if (multi_enabled && (actions_applied > 0 || actions_blocked > 0)) {
        Print(L"OK: OO policy batch: %d actions applied, %d blocked\r\n", actions_applied, actions_blocked);
    }

    // M5.3: Log consultation to OOCONSULT.LOG
    {
        // Determine decision string for log
        const char *decision_str = "unknown";
        if (action_stable) {
            decision_str = "stable";
        } else if (action_reboot) {
            decision_str = "reboot_logged";
        } else if (actions_applied == 0 && actions_blocked == 0) {
            decision_str = "ignored";
        } else if (multi_enabled && (actions_applied > 0 || actions_blocked > 0)) {
            // Multi-action: use batch_summary as decision
            decision_str = batch_summary[0] ? batch_summary : "multi";
        } else if (action_reduce_ctx) {
            decision_str = "reduce_ctx";
        } else if (action_reduce_seq) {
            decision_str = "reduce_seq";
        } else if (action_increase) {
            decision_str = "increase_blocked";
        } else if (action_model) {
            decision_str = "model_logged";
        }

        llmk_oo_log_consultation(boots, mode, ram_mb, ctx, seq, llm_suggestion,
                                 decision_str, (actions_applied > 0) ? 1 : 0,
                                 confidence_score, confidence_threshold,
                                 confidence_gate_enabled);
    }

    // 7. Log to journal (best-effort)
    if (g_root) {
        char jlog[256];
        int jp = 0;
        if (multi_enabled && (actions_applied > 0 || actions_blocked > 0)) {
            llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, "oo event=consult_multi actions=[");
            llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, batch_summary);
            llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, "] applied=");
            llmk_ascii_append_u64(jlog, (int)sizeof(jlog), &jp, (UINT64)actions_applied);
            llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, " blocked=");
            llmk_ascii_append_u64(jlog, (int)sizeof(jlog), &jp, (UINT64)actions_blocked);
        } else {
            llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, "oo event=consult decision=");
            if (action_stable) {
                llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, "stable");
            } else if (actions_applied > 0) {
                llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, batch_summary);
            } else {
                llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, "ignored");
            }
        }
        llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, " score=");
        llmk_ascii_append_u64(jlog, (int)sizeof(jlog), &jp, (UINT64)confidence_score);
        llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, " threshold=");
        llmk_ascii_append_u64(jlog, (int)sizeof(jlog), &jp, (UINT64)confidence_threshold);
        llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, " gate=");
        llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, confidence_gate_enabled ? "enforced" : "log_only");
        llmk_ascii_append_str(jlog, (int)sizeof(jlog), &jp, "\r\n");

        EFI_FILE_HANDLE jf = NULL;
        if (!EFI_ERROR(llmk_open_binary_file_append(&jf, L"OOJOUR.LOG"))) {
            UINTN nb = (UINTN)jp;
            uefi_call_wrapper(jf->Write, 3, jf, &nb, (void *)jlog);
            uefi_call_wrapper(jf->Flush, 1, jf);
            uefi_call_wrapper(jf->Close, 1, jf);

            // Enforce max journal size (best-effort).
            llmk_oo_jour_log_rotate_best_effort();
        }
    }
}

// M5.3: Log consultation to OOCONSULT.LOG (append-only)
// Spec: cap log at 64KB and truncate oldest (FIFO) when exceeded.
#define LLMK_OO_CONSULT_LOG_MAX_BYTES  (64u * 1024u)
#define LLMK_OO_CONSULT_LOG_KEEP_BYTES (32u * 1024u)

// Journal cap (same policy as consult log): cap at 64KB and keep newest 32KB.
#define LLMK_OO_JOUR_LOG_MAX_BYTES  (64u * 1024u)
#define LLMK_OO_JOUR_LOG_KEEP_BYTES (32u * 1024u)

static void llmk_oo_jour_log_rotate_best_effort(void) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOJOUR.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    if (len <= (UINTN)LLMK_OO_JOUR_LOG_MAX_BYTES) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    UINTN keep = (UINTN)LLMK_OO_JOUR_LOG_KEEP_BYTES;
    if (keep >= len) keep = len;
    UINTN start = len - keep;

    // Align to line boundary (avoid starting mid-line).
    char *cbuf = (char *)buf;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') { start = i + 1; break; }
    }
    if (start >= len) start = 0;

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"OOJOUR.LOG");
    if (!EFI_ERROR(st) && f) {
        UINTN nb = len - start;
        (void)llmk_file_write_bytes(f, (const void *)(cbuf + start), nb);
        (void)uefi_call_wrapper(f->Flush, 1, f);
        uefi_call_wrapper(f->Close, 1, f);
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_consult_log_rotate_best_effort(void) {
    if (!g_root) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    if (len <= (UINTN)LLMK_OO_CONSULT_LOG_MAX_BYTES) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        return;
    }

    UINTN keep = (UINTN)LLMK_OO_CONSULT_LOG_KEEP_BYTES;
    if (keep >= len) keep = len;
    UINTN start = len - keep;

    // Try to align on a line boundary to avoid starting mid-line.
    char *cbuf = (char *)buf;
    for (UINTN i = start; i < len; i++) {
        if (cbuf[i] == '\n') { start = i + 1; break; }
    }
    if (start >= len) start = 0;

    EFI_FILE_HANDLE f = NULL;
    st = llmk_open_binary_file(&f, L"OOCONSULT.LOG");
    if (!EFI_ERROR(st) && f) {
        UINTN nb = len - start;
        (void)llmk_file_write_bytes(f, (const void *)(cbuf + start), nb);
        (void)uefi_call_wrapper(f->Flush, 1, f);
        uefi_call_wrapper(f->Close, 1, f);
    }

    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_log_consultation(UINT64 boot_count, UINT32 mode, UINT64 ram_mb, 
                                     int ctx, int seq, const char *suggestion, 
                                     const char *decision, int applied,
                                     int confidence_score, int confidence_threshold,
                                     int confidence_gate_enabled) {
    // Check if logging enabled
    int log_enabled = g_cfg_oo_consult_log;
    if (log_enabled < 0) {
        log_enabled = (g_cfg_oo_llm_consult > 0) ? 1 : 0;
    }
    if (!log_enabled || !g_root) return;

    // Build log line: [boot=N] mode=MODE ram=MB ctx=val seq=val suggestion="..." decision=action applied=0|1
    char logline[256];
    int lp = 0;
    
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "[boot=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, boot_count);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "] mode=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, 
                         (mode == LLMK_OO_MODE_NORMAL) ? "NORMAL" : 
                         (mode == LLMK_OO_MODE_DEGRADED) ? "DEGRADED" : "SAFE");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " ram=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, ram_mb);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " ctx=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)ctx);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " seq=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)seq);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " suggestion=\"");
    
    // Truncate suggestion to 60 chars max
    int slen = 0;
    while (suggestion[slen] && slen < 60 && lp + 1 < (int)sizeof(logline) - 40) {
        logline[lp++] = suggestion[slen++];
    }
    if (suggestion[slen]) {
        llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "...");
    }
    
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "\" decision=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, decision);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " applied=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)applied);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " score=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)confidence_score);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " threshold=");
    llmk_ascii_append_u64(logline, (int)sizeof(logline), &lp, (UINT64)confidence_threshold);
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, " gate=");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, confidence_gate_enabled ? "enforced" : "log_only");
    llmk_ascii_append_str(logline, (int)sizeof(logline), &lp, "\r\n");
    logline[lp] = 0;

    // Append to OOCONSULT.LOG
    EFI_FILE_HANDLE logf = NULL;
    if (!EFI_ERROR(llmk_open_binary_file_append(&logf, L"OOCONSULT.LOG"))) {
        UINTN nb = (UINTN)lp;
        uefi_call_wrapper(logf->Write, 3, logf, &nb, (void *)logline);
        uefi_call_wrapper(logf->Flush, 1, logf);
        uefi_call_wrapper(logf->Close, 1, logf);
        Print(L"OK: OO consult logged to OOCONSULT.LOG\r\n");

        // Enforce max size (best-effort; no boot impact).
        llmk_oo_consult_log_rotate_best_effort();
    }
}

static void llmk_oo_print_ooconsult_tail_best_effort(int max_lines) {
    if (!g_root || max_lines <= 0) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOCONSULT.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_log] (no OOCONSULT.LOG)\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = 0;
    int lines = 0;
    for (UINTN i = len; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            lines++;
            if (lines > max_lines) {
                start = i;
                break;
            }
        }
    }
    if (start >= len) start = 0;

    UINTN out_len = len - start;
    char *out = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, out_len + 1, (void **)&out)) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_log] (OOM printing tail)\r\n");
        return;
    }

    CopyMem(out, cbuf + start, out_len);
    out[out_len] = 0;

    Print(L"%a", out);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_print_oojour_tail_best_effort(int max_lines) {
    if (!g_root || max_lines <= 0) return;

    void *buf = NULL;
    UINTN len = 0;
    EFI_STATUS st = llmk_read_entire_file_best_effort(L"OOJOUR.LOG", &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        if (buf) uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_jour] (no OOJOUR.LOG)\r\n");
        return;
    }

    char *cbuf = (char *)buf;
    UINTN start = 0;
    int lines = 0;
    for (UINTN i = len; i > 0; i--) {
        if (cbuf[i - 1] == '\n') {
            lines++;
            if (lines > max_lines) {
                start = i;
                break;
            }
        }
    }
    if (start >= len) start = 0;

    UINTN out_len = len - start;
    char *out = NULL;
    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePool, 3, EfiBootServicesData, out_len + 1, (void **)&out)) || !out) {
        uefi_call_wrapper(BS->FreePool, 1, buf);
        Print(L"[oo_jour] (OOM printing tail)\r\n");
        return;
    }

    CopyMem(out, cbuf + start, out_len);
    out[out_len] = 0;

    Print(L"%a", out);

    uefi_call_wrapper(BS->FreePool, 1, out);
    uefi_call_wrapper(BS->FreePool, 1, buf);
}

static void llmk_oo_consult_execute(Config *config, TransformerWeights *weights, 
                                    RunState *state, Tokenizer *tokenizer,
                                    float temperature, float min_p, float top_p, int top_k) {
    if (!config || !weights || !state || !tokenizer) return;

    // 1. Collect system state
    UINT64 ram_mb = llmk_get_conventional_ram_bytes_best_effort() / (1024ULL * 1024ULL);
    UINT32 mode = g_oo_last_mode_valid ? g_oo_last_mode : LLMK_OO_MODE_SAFE;
    int ctx = config->seq_len;
    int seq = config->seq_len;
    UINT64 boots = 0;

    // Load current boot count from state (best-effort)
    LlmkOoState s;
    if (llmk_oo_load_state_best_effort(&s)) {
        boots = s.boot_count;
        mode = s.mode;
    }

    const char *mode_str = (mode == LLMK_OO_MODE_NORMAL) ? "NORMAL" :
                           (mode == LLMK_OO_MODE_DEGRADED) ? "DEGRADED" : "SAFE";
    Print(L"[obs][oo] consult_start mode=%a ram=%lu ctx=%d seq=%d boots=%lu\r\n",
          mode_str, ram_mb, ctx, seq, boots);

    // Read tail of journal (last 3 lines, best-effort)
    char journal_tail[256];
    journal_tail[0] = 0;
    if (g_root) {
        EFI_FILE_HANDLE jf = NULL;
        if (!EFI_ERROR(llmk_open_binary_file_append(&jf, L"OOJOUR.LOG"))) {
            UINT64 pos = 0;
            if (!EFI_ERROR(uefi_call_wrapper(jf->GetPosition, 2, jf, &pos)) && pos > 0) {
                // Seek backwards up to 256 bytes
                UINT64 seek_start = (pos > 256ULL) ? (pos - 256ULL) : 0ULL;
                uefi_call_wrapper(jf->SetPosition, 2, jf, seek_start);
                UINTN nr = 256;
                char tmp[256];
                if (!EFI_ERROR(uefi_call_wrapper(jf->Read, 3, jf, &nr, tmp)) && nr > 0) {
                    // Extract last 3 lines (simplistic: look for last 3 \n)
                    int nl_count = 0;
                    int start_idx = (int)nr - 1;
                    while (start_idx >= 0 && nl_count < 3) {
                        if (tmp[start_idx] == '\n') nl_count++;
                        start_idx--;
                    }
                    start_idx++;
                    if (start_idx < 0) start_idx = 0;
                    int jt_p = 0;
                    for (int i = start_idx; i < (int)nr && jt_p + 1 < (int)sizeof(journal_tail); i++) {
                        char c = tmp[i];
                        if (c == '\r' || c == '\n') c = ' ';
                        journal_tail[jt_p++] = c;
                    }
                    journal_tail[jt_p] = 0;
                }
            }
            uefi_call_wrapper(jf->Close, 1, jf);
        }
    }

    // 2. Compose prompt (compact, <256 chars)
    // Check if multi-actions is enabled (for prompt adaptation)
    int multi_enabled_for_prompt = g_cfg_oo_multi_actions;
    if (multi_enabled_for_prompt < 0) {
        multi_enabled_for_prompt = (g_cfg_oo_llm_consult > 0) ? 1 : 0;
    }

    char prompt_buf[256];
    int pp = 0;
    prompt_buf[0] = 0;
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, "System: mode=");
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, 
                         (mode == LLMK_OO_MODE_NORMAL) ? "NORMAL" : 
                         (mode == LLMK_OO_MODE_DEGRADED) ? "DEGRADED" : "SAFE");
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, " ram=");
    llmk_ascii_append_u64(prompt_buf, (int)sizeof(prompt_buf), &pp, ram_mb);
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, "MB ctx=");
    llmk_ascii_append_u64(prompt_buf, (int)sizeof(prompt_buf), &pp, (UINT64)ctx);
    llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, " boots=");
    llmk_ascii_append_u64(prompt_buf, (int)sizeof(prompt_buf), &pp, boots);
    if (journal_tail[0]) {
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, " log=[");
        // Truncate if needed
        int jl = 0;
        while (journal_tail[jl] && pp + 1 < (int)sizeof(prompt_buf) - 32) {
            prompt_buf[pp++] = journal_tail[jl++];
        }
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, "]");
    }
    
    // Adapt prompt for multi-action mode (M5.1)
    if (multi_enabled_for_prompt) {
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, 
                             ". Suggest 1-3 brief actions (max 20 words):");
    } else {
        llmk_ascii_append_str(prompt_buf, (int)sizeof(prompt_buf), &pp, 
                             ". Suggest ONE brief action (max 10 words):");
    }
    prompt_buf[pp] = 0;

    Print(L"[oo_consult] Prompt: ");
    llmk_print_ascii(prompt_buf);
    Print(L"\r\n\r\n");

    // 3. Tokenize prompt
    int prompt_tokens[128];
    int n_prompt = 0;
    {
        int cap = (int)(sizeof(prompt_tokens) / sizeof(prompt_tokens[0]));
        encode(prompt_buf, prompt_tokens, &n_prompt, cap, tokenizer);
        if (n_prompt <= 0) {
            Print(L"[oo_consult] ERROR: tokenization failed\r\n");
            return;
        }
    }

    // 4. Generate LLM suggestion (low creativity: temp=0.3, max_tokens=32)
    char llm_suggestion[128];
    llm_suggestion[0] = 0;
    int llm_len = 0;

    {
        // Prefill
        int pos = 0;
        for (int i = 0; i < n_prompt; i++) {
            if (g_llmk_ready) {
                llmk_sentinel_phase_start(&g_sentinel, LLMK_PHASE_PREFILL);
                transformer_forward(state, weights, config, prompt_tokens[i], pos);
                llmk_sentinel_phase_end(&g_sentinel);
            } else {
                transformer_forward(state, weights, config, prompt_tokens[i], pos);
            }
            pos++;
        }

        // Decode (max 32 tokens)
        int token = prompt_tokens[n_prompt - 1];
        float saved_temp = temperature;
        int saved_topk = top_k;
        temperature = 0.3f;
        top_k = 20;

        for (int step = 0; step < 32; step++) {
            if (pos >= config->seq_len) break;

            // Sample
            int next = sample_advanced(state->logits, config->vocab_size, temperature, 
                                      min_p, top_p, top_k, NULL, 0, 1.0f);
            if (next == 2 || next == 1) break; // EOS/BOS

            // Decode token
            char *piece = tokenizer->vocab[next];
            if (piece) {
                int plen = my_strlen(piece);
                if (llm_len + plen + 1 < (int)sizeof(llm_suggestion)) {
                    for (int k = 0; k < plen; k++) llm_suggestion[llm_len++] = piece[k];
                    llm_suggestion[llm_len] = 0;
                }
            }

            // Forward
            token = next;
            if (g_llmk_ready) {
                llmk_sentinel_phase_start(&g_sentinel, LLMK_PHASE_DECODE);
                transformer_forward(state, weights, config, token, pos);
                llmk_sentinel_phase_end(&g_sentinel);
            } else {
                transformer_forward(state, weights, config, token, pos);
            }
            pos++;
        }

        temperature = saved_temp;
        top_k = saved_topk;
    }

    Print(L"[obs][oo] consult_gen prompt_tok=%d out_chars=%d\r\n", n_prompt, llm_len);

    llmk_oo_consult_process_suggestion(ram_mb, mode, boots, ctx, seq, llm_suggestion);
}

// ============================================================================
// MAIN
// ============================================================================

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    // Djibion meta-engine defaults: off unless enabled by the user.
    djibion_init(&g_djibion);

    // Diopion complementary engine defaults: off unless enabled by the user.
    diopion_init(&g_diopion);

    // Diagnostion engine defaults: on (diagnostics are safe).
    diagnostion_init(&g_diagnostion);

    // Memorion engine defaults: on (read-only helpers + explicit manifest writes).
    memorion_init(&g_memorion);

    // Orchestrion engine defaults: off (workflow runner).
    orchestrion_init(&g_orchestrion);

    // Calibrion engine defaults: off (auto-tuning sampling).
    calibrion_init(&g_calibrion);

    // Compatibilion engine defaults: on (platform detection).
    compatibilion_init(&g_compatibilion);
    compatibilion_probe_cpu(&g_compatibilion);
    compatibilion_set_platform(&g_compatibilion, COMPAT_PLAT_UEFI | COMPAT_PLAT_FAT32);

    // Initialize DjibMark tracing system
    djibmark_init();
    DJIBMARK_BOOT();

    // Disable the UEFI watchdog timer (large model loads can take minutes).
    // If not disabled, firmware may reset/reboot mid-load and it looks like a hang.
    uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);

    // 1. Clear Screen
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);

    // 2. Try to show image splash
    // Note: If successful, it covers the screen.
    ShowCyberpunkSplash(ImageHandle, SystemTable);

    // 3. Always show Text Banner (User requested boot interface to remain standard)
    // The splash function now handles its own display + pause + clear.
    Print(L"\r\n");
    Print(L"    __    __    __  ___\r\n");
    Print(L"   / /   / /   /  |/  /\r\n");
    Print(L"  / /   / /   / /|_/ /\r\n");
    Print(L" / /___/ /___/ /  / /\r\n");
    Print(L"/_____/_____/_/  /_/\r\n\r\n");
    Print(L"    ____  ___    ____  ________  __________________    __\r\n");
    Print(L"   / __ )/   |  / __ \\/ ____/  |/  / ____/_  __/   |  / /\r\n");
    Print(L"  / __  / /| | / /_/ / __/ / /|_/ / __/   / / / /| | / /\r\n");
    Print(L" / /_/ / ___ |/ _, _/ /___/ /  / / /___  / / / ___ |/ /___\r\n");
    Print(L"/_____/_/  |_/_/ |_/_____/_/  /_/_____/ /_/ /_/  |_/_____/\r\n\r\n");
    Print(L"LLM Baremetal UEFI - LLAMA2 Chat REPL\r\n");
    Print(L"--------------------------------------------------------------------------\r\n");
    Print(L"Tips: /help | /logo | /compat_status | /calib_status | /orch_status\r\n\r\n");
    
    if (!g_boot_verbose) {
        Print(L"Booting... (set boot_verbose=1 in repl.cfg for details; 2 for debug)\r\n\r\n");
    }

    llmk_boot_mark(L"banner");
    
    // ========================================================================
    // [1/7] File System
    // ========================================================================
    llmk_overlay_stage(1, 7);
    if (g_boot_verbose) {
        Print(L"[1/7] Opening file system...\r\n");
    }
    
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &LoadedImageProtocol, &LoadedImage);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: LoadedImage protocol failed\r\n");
        return status;
    }
    
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &FileSystemProtocol, &FileSystem);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: FileSystem protocol failed\r\n");
        return status;
    }
    
    EFI_FILE_HANDLE Root;
    status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: OpenVolume failed\r\n");
        return status;
    }

    // Persist root handle for best-effort dumps.
    g_root = Root;

    // Best-effort: read boot verbosity from repl.cfg now that the FS is ready.
    llmk_load_repl_cfg_boot_best_effort();

    if (g_boot_logo) {
        llmk_print_logo();
    }

    if (g_boot_verbose) {
        Print(L"OK: File system ready\r\n\r\n");
    }

    llmk_boot_mark(L"fs_ready");

    // OO M1: best-effort persistent boot tick (writes OOSTATE.BIN + appends OOJOUR.LOG)
    // Opt-in via repl.cfg: oo_enable=1
    llmk_oo_boot_tick_best_effort();

    // OO M4: best-effort network read-only tick (placeholder)
    // Opt-in via repl.cfg: oo_net=1 (and oo_enable=1)
    llmk_oo_net_tick_best_effort();

    // Best-effort enable AVX/AVX2 state before feature detection.
    enable_avx_best_effort();

    // CPU feature detection (djiblas)
    {
        CPUFeatures cpu_features;
        djiblas_detect_cpu(&cpu_features);
        sgemm_kernel_t k = djiblas_get_best_kernel(&cpu_features);
        const CHAR16 *name = L"SCALAR";
        if (k == djiblas_sgemm_avx512) name = L"AVX512";
        else if (k == djiblas_sgemm_avx2) name = (cpu_features.has_fma ? L"AVX2+FMA" : L"AVX2");
        else if (k == djiblas_sgemm_sse2) name = L"SSE2";

        // Attention SIMD dispatch: only use AVX2 if firmware/OS state supports it.
        g_attn_use_avx2 = (cpu_features.has_avx2 && cpu_features.has_avx);

        if (g_boot_verbose) {
            Print(L"[DJIBLAS] SGEMM kernel: %s (sse2=%d avx=%d avx2=%d fma=%d)\r\n\r\n",
                  name,
                  (int)cpu_features.has_sse2,
                  (int)cpu_features.has_avx,
                  (int)cpu_features.has_avx2,
                  (int)cpu_features.has_fma);
            Print(L"[ATTN] SIMD path: %s\r\n\r\n", g_attn_use_avx2 ? L"AVX2" : L"SSE2");
        }
    }

    llmk_boot_mark(L"cpu_detect");

    // Best-effort graphics init (GOP). Optional: REPL still works without it.
    {
        EFI_STATUS gst = llmk_gop_init_best_effort();
        if (!EFI_ERROR(gst)) {
            if (g_boot_verbose) {
                Print(L"[GOP] Framebuffer ready: %dx%d (ppsl=%d)\r\n\r\n", (int)g_gop_w, (int)g_gop_h, (int)g_gop_ppsl);
            }

            // Feed platform info into Compatibilion.
            compatibilion_set_gop(&g_compatibilion, (uint32_t)g_gop_w, (uint32_t)g_gop_h);
        } else {
            if (g_boot_verbose) {
                Print(L"[GOP] Not available (%r)\r\n\r\n", gst);
            }
        }
    }

    llmk_boot_mark(L"gop_init");

    // Show diagnostic info if requested via repl.cfg: boot_diag=1
    if (g_boot_diag) {
        llmk_print_diag();
    }

    // LLM-OO runtime: init early, then optionally hook to GOP for heartbeat.
    llmk_oo_init();
    llmk_oo_set_on_step(llmk_oo_on_step_gop);
    
    // ========================================================================
    // [2/7] Load Model Header
    // ========================================================================

    llmk_overlay_stage(2, 7);
    
    if (g_boot_verbose) {
        Print(L"[2/7] Loading model...\r\n");
    }

    unsigned long long startup_model_t0_us = 0;
    unsigned long long startup_model_select_done_us = 0;
    unsigned long long startup_model_prep_done_us = 0;
    (void)uefi_wall_us(&startup_model_t0_us);
    
    EFI_FILE_HANDLE ModelFile;
    CHAR16 *model_filename = NULL;
    {
        int cfg_model_override_requested = 0;
        int cfg_model_override_failed = 0;
        CHAR16 cfg_model_requested[128];
        cfg_model_requested[0] = 0;

        // Optional: allow repl.cfg to override which model file to open.
        // Example in repl.cfg:
        //   model=models\\my-instruct.bin
        //   model=models\\my-instruct.gguf
        //   model=models\\my-instruct      (no extension: tries .bin then .gguf)
        //   model=stories110M.bin
        CHAR16 cfg_model[128];
        cfg_model[0] = 0;
        if (llmk_read_cfg_model_best_effort(Root, cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])))) {
            cfg_model_override_requested = 1;
            llmk_char16_copy_cap(cfg_model_requested, (int)(sizeof(cfg_model_requested) / sizeof(cfg_model_requested[0])), cfg_model);
            EFI_FILE_HANDLE f = 0;
            EFI_STATUS st = EFI_NOT_FOUND;

            // If no extension is provided, try .bin first (for inference), then .gguf.
            if (!llmk_char16_has_dot_ext(cfg_model)) {
                CHAR16 picked[192];
                picked[0] = 0;
                if (llmk_try_open_with_ext(Root, cfg_model, L".bin", &f, picked, (int)(sizeof(picked) / sizeof(picked[0])))) {
                    llmk_char16_copy_cap(cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                    st = EFI_SUCCESS;
                } else if (llmk_try_open_with_ext(Root, cfg_model, L".gguf", &f, picked, (int)(sizeof(picked) / sizeof(picked[0])))) {
                    llmk_char16_copy_cap(cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                    st = EFI_SUCCESS;
                }
            } else {
                CHAR16 picked[192];
                picked[0] = 0;
                st = llmk_open_read_with_fat83_fallback(Root, cfg_model, &f, picked, (int)(sizeof(picked) / sizeof(picked[0])), L"model_cfg");
                if (!EFI_ERROR(st) && picked[0]) {
                    llmk_char16_copy_cap(cfg_model, (int)(sizeof(cfg_model) / sizeof(cfg_model[0])), picked);
                }
            }

            if (!EFI_ERROR(st) && f) {
                // Keep the file as-selected. GGUF inference support (or fallback) is decided later,
                // after we inspect the GGUF tensor types.
                // Always store the selected path in stable global storage.
                // (Early heap allocations can be overwritten later during weight mapping.)
                llmk_model_set_loaded_path(cfg_model);
                model_filename = g_loaded_model_path16;
                ModelFile = f;
                status = st;
            } else {
                Print(L"[cfg] WARNING: model override open failed: %s (%r)\r\n", cfg_model, st);
                Print(L"[cfg] hint: run /models to inspect available files, or set model=<name>.bin|.gguf\r\n");
                Print(L"[cfg] fallback: continuing with auto-detect candidates\r\n");
                cfg_model_override_failed = 1;
            }
        }

        if (model_filename != NULL) {
            // Using cfg override.
            goto model_selected;
        }

        // If the model picker is enabled and there are multiple models available,
        // prompt the user before auto-picking from the legacy candidate list.
        // This prevents e.g. stories110M.bin from bypassing the picker.
        if (g_cfg_model_picker != 0) {
            LlmkModelEntry entries2[2];
            int n_models = llmk_collect_models(entries2, (int)(sizeof(entries2) / sizeof(entries2[0])));
            if (n_models >= 2) {
                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[192];
                picked[0] = 0;

                int picked_ok = llmk_model_picker(&f, picked, (int)(sizeof(picked) / sizeof(picked[0])));
                if (picked_ok && f) {
                    ModelFile = f;
                    llmk_model_set_loaded_path(picked);
                    model_filename = g_loaded_model_path16;
                    status = EFI_SUCCESS;
                    goto model_selected;
                }

                // Picker was shown and the user canceled (or selection failed).
                // Keep the app alive so /models + /model_info are usable.
                InterfaceFx_End();
                llmk_repl_no_model_loop();
                return EFI_NOT_FOUND;
            }
        }

        // Try larger models first when present. Keep the list small and explicit
        // (UEFI shell users can rename the file to match one of these).
        CHAR16 *candidates[] = {
            L"stories300M.bin",
            L"stories260M.bin",
            L"stories200M.bin",
            L"stories110M.bin",
            L"stories15M.bin",
            L"model.bin",
        };
        const int n_candidates = (int)(sizeof(candidates) / sizeof(candidates[0]));
        EFI_STATUS last = EFI_NOT_FOUND;
        for (int i = 0; i < n_candidates; i++) {
            EFI_FILE_HANDLE f = 0;
            CHAR16 picked0[192];
            picked0[0] = 0;
            EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, candidates[i], &f, picked0,
                                                              (int)(sizeof(picked0) / sizeof(picked0[0])),
                                                              L"model_candidate");
            if (!EFI_ERROR(st) && f) {
                ModelFile = f;
                llmk_model_set_loaded_path(picked0[0] ? picked0 : candidates[i]);
                model_filename = g_loaded_model_path16;
                status = st;
                break;
            }
            // Also allow placing models under a /models directory.
            {
                CHAR16 path[96];
                StrCpy(path, L"models\\");
                StrCat(path, candidates[i]);
                CHAR16 picked1[192];
                picked1[0] = 0;
                st = llmk_open_read_with_fat83_fallback(Root, path, &f, picked1,
                                                       (int)(sizeof(picked1) / sizeof(picked1[0])),
                                                       L"model_candidate_models");
                if (!EFI_ERROR(st) && f) {
                    ModelFile = f;
                    llmk_model_set_loaded_path(picked1[0] ? picked1 : path);
                    model_filename = g_loaded_model_path16;
                    status = st;
                    break;
                }
            }
            last = st;
        }
        if (model_filename == NULL) {
            // Last-chance: model picker (menu), otherwise first match in root/models.
            EFI_FILE_HANDLE f = NULL;
            CHAR16 picked[192];
            picked[0] = 0;

            int picked_ok = 0;
            int picker_used = (g_cfg_model_picker != 0);
            if (picker_used) {
                picked_ok = llmk_model_picker(&f, picked, (int)(sizeof(picked) / sizeof(picked[0])));
            }
            if (!picked_ok && !picker_used) {
                picked[0] = 0;
                if (llmk_try_open_first_model_best_effort(&f, picked, (int)(sizeof(picked) / sizeof(picked[0])))) {
                    picked_ok = 1;
                }
            }

            if (picked_ok && f) {
                ModelFile = f;
                llmk_model_set_loaded_path(picked);
                model_filename = g_loaded_model_path16;
                status = EFI_SUCCESS;
            } else {
                Print(L"ERROR: Model file not found.\r\n");
                Print(L"Expected one of (root or models\\): stories300M.bin stories260M.bin stories200M.bin stories110M.bin stories15M.bin model.bin\r\n");
                Print(L"Last open status: %r\r\n", last);
                Print(L"Or set repl.cfg: model=<path> (supports .bin/.gguf)\r\n");
                Print(L"Tip: in no-model REPL use /models and /model_info <path>\r\n");
                // Do not exit: keep the app alive so /models + /model_info are usable.
                InterfaceFx_End();
                llmk_repl_no_model_loop();
                return last;
            }
        }
model_selected:
        ;

    (void)uefi_wall_us(&startup_model_select_done_us);

        if (g_cfg_oo_enable && cfg_model_override_requested && cfg_model_override_failed && model_filename != NULL) {
            Print(L"OK: OO model fallback: %s -> %s\r\n", cfg_model_requested, model_filename);
        }
    }
    
    // Record the selected model path for /model_info.
    llmk_model_set_loaded_path(model_filename);
    if (g_boot_verbose >= 2) llmk_debug_print_loaded_model_path(L"after_select");

    // Detect format early.
    g_loaded_model_format = llmk_detect_model_format(ModelFile);

    // GGUF inference support: F16/F32 and common quant types are supported by the loader.
    LlmkGgufPlan *gguf_plan = NULL;
    int use_gguf_inference = 0;
    int gguf_has_output_weight = 0;

    Config config;
    // Default-init in case of early exits.
    config.dim = 0;
    config.hidden_dim = 0;
    config.n_layers = 0;
    config.n_heads = 0;
    config.n_kv_heads = 0;
    config.vocab_size = 0;
    config.seq_len = 0;

    // In llama2.c format, a negative vocab_size indicates shared classifier weights.
    int shared_classifier = 0;

    if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF) {
        // Parse GGUF plan directly on startup path (avoid extra summary parse here).

        // Try to build a GGUF inference plan. If this fails (e.g., quantized GGUF), fall back to .bin.
        {
            int dim = 0, hidden = 0, layers = 0, heads = 0, kv = 0, vocab = 0, seq = 0;
            EFI_STATUS pst = llmk_gguf_build_plan(ModelFile, &gguf_plan, &dim, &hidden, &layers, &heads, &kv, &vocab, &seq, &gguf_has_output_weight);
            if (!EFI_ERROR(pst) && gguf_plan) {
                config.dim = dim;
                config.hidden_dim = hidden;
                config.n_layers = layers;
                config.n_heads = heads;
                config.n_kv_heads = kv;
                config.vocab_size = vocab;
                config.seq_len = seq;

                shared_classifier = gguf_has_output_weight ? 0 : 1;
                use_gguf_inference = 1;
                if (g_boot_verbose) {
                    Print(L"GGUF detected: ctx=%d dim=%d layers=%d heads=%d kv_heads=%d\r\n",
                          config.seq_len, config.dim, config.n_layers, config.n_heads, config.n_kv_heads);
                }
                Print(L"OK: GGUF inference enabled (F16/F32/Q4/Q5/Q8).\r\n\r\n");
            } else {
                Print(L"NOTE: GGUF inference unsupported (%r); searching for a .bin fallback...\r\n", pst);
            }
        }

        if (!use_gguf_inference) {
            uefi_call_wrapper(ModelFile->Close, 1, ModelFile);

            // Preferred fallback: sibling .bin next to the selected .gguf (same basename).
            if (model_filename && llmk_char16_endswith_ci(model_filename, L".gguf")) {
                CHAR16 alt[192];
                llmk_char16_copy_cap(alt, (int)(sizeof(alt) / sizeof(alt[0])), model_filename);
                // Find last '.' and overwrite.
                for (int k = (int)StrLen(alt) - 1; k >= 0; k--) {
                    if (alt[k] == L'.') {
                        alt[k] = 0;
                        break;
                    }
                    if (alt[k] == L'\\' || alt[k] == L'/') break;
                }
                if (StrLen(alt) + 4 < (sizeof(alt) / sizeof(alt[0]))) {
                    StrCat(alt, L".bin");
                    EFI_FILE_HANDLE fb = NULL;
                    CHAR16 picked[192];
                    picked[0] = 0;
                    EFI_STATUS fst = llmk_open_read_with_fat83_fallback(Root, alt, &fb, picked,
                                                                       (int)(sizeof(picked) / sizeof(picked[0])),
                                                                       L"gguf_sibling_bin");
                    if (!EFI_ERROR(fst) && fb) {
                        ModelFile = fb;
                        const CHAR16 *chosen = picked[0] ? picked : alt;
                        UINTN n = StrLen(chosen) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc((unsigned long)(n * sizeof(CHAR16)));
                        model_filename = stable ? stable : model_filename;
                        if (stable) StrCpy(stable, chosen);
                        llmk_model_set_loaded_path(model_filename);
                        g_loaded_model_format = LLMK_MODEL_FMT_BIN;
                        Print(L"OK: using sibling .bin fallback: %s\r\n\r\n", model_filename);
                        goto gguf_fallback_done;
                    }
                }
            }

            // Minimal fallback search (root and models\\) to avoid bricking boot.
            CHAR16 *fallbacks[] = {
                L"stories300M.bin",
                L"stories260M.bin",
                L"stories200M.bin",
                L"stories110M.bin",
                L"stories15M.bin",
                L"model.bin",
            };
            const int n_fallbacks = (int)(sizeof(fallbacks) / sizeof(fallbacks[0]));
            EFI_FILE_HANDLE fb = NULL;
            CHAR16 *fb_name = NULL;
            for (int fi = 0; fi < n_fallbacks; fi++) {
                EFI_FILE_HANDLE t = NULL;
                CHAR16 picked0[192];
                picked0[0] = 0;
                EFI_STATUS fst = llmk_open_read_with_fat83_fallback(Root, fallbacks[fi], &t, picked0,
                                                                   (int)(sizeof(picked0) / sizeof(picked0[0])),
                                                                   L"gguf_fallback_root");
                if (!EFI_ERROR(fst) && t) {
                    fb = t;
                    if (picked0[0]) {
                        UINTN n = StrLen(picked0) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc((unsigned long)(n * sizeof(CHAR16)));
                        fb_name = stable ? stable : fallbacks[fi];
                        if (stable) StrCpy(stable, picked0);
                    } else {
                        fb_name = fallbacks[fi];
                    }
                    break;
                }
                {
                    CHAR16 pth[96];
                    StrCpy(pth, L"models\\");
                    StrCat(pth, fallbacks[fi]);
                    CHAR16 picked1[192];
                    picked1[0] = 0;
                    fst = llmk_open_read_with_fat83_fallback(Root, pth, &t, picked1,
                                                            (int)(sizeof(picked1) / sizeof(picked1[0])),
                                                            L"gguf_fallback_models");
                    if (!EFI_ERROR(fst) && t) {
                        fb = t;
                        const CHAR16 *chosen = picked1[0] ? picked1 : pth;
                        UINTN n = StrLen(chosen) + 1;
                        CHAR16 *stable = (CHAR16 *)simple_alloc((unsigned long)(n * sizeof(CHAR16)));
                        fb_name = stable ? stable : fallbacks[fi];
                        if (stable) StrCpy(stable, chosen);
                        break;
                    }
                }
            }

            if (!fb || !fb_name) {
                Print(L"ERROR: no .bin fallback found. Use /model_info to inspect GGUF, or provide a .bin export for inference.\r\n");
                return EFI_UNSUPPORTED;
            }

            ModelFile = fb;
            model_filename = fb_name;
            llmk_model_set_loaded_path(model_filename);
            g_loaded_model_format = LLMK_MODEL_FMT_BIN;
            Print(L"OK: using .bin fallback: %s\r\n\r\n", model_filename);
        }
gguf_fallback_done:
        ;
    }

    (void)uefi_wall_us(&startup_model_prep_done_us);
    if (startup_model_t0_us && startup_model_prep_done_us && startup_model_prep_done_us >= startup_model_t0_us) {
        unsigned long long select_ms = (startup_model_select_done_us >= startup_model_t0_us)
                                       ? ((startup_model_select_done_us - startup_model_t0_us) / 1000ULL)
                                       : 0ULL;
        unsigned long long prep_ms = (startup_model_prep_done_us >= startup_model_select_done_us)
                                     ? ((startup_model_prep_done_us - startup_model_select_done_us) / 1000ULL)
                                     : 0ULL;
        const char *fmt_s = (g_loaded_model_format == LLMK_MODEL_FMT_GGUF) ? "gguf" :
                            (g_loaded_model_format == LLMK_MODEL_FMT_BIN) ? "bin" : "unknown";
        Print(L"[obs][startup] model_select_ms=%lu model_prepare_ms=%lu format=%a\r\n",
              (UINT64)select_ms, (UINT64)prep_ms, fmt_s);
    }

    UINTN bytes_to_read = 0;
    if (!use_gguf_inference) {
        bytes_to_read = 7 * sizeof(int);
        uefi_call_wrapper(ModelFile->Read, 3, ModelFile, &bytes_to_read, &config);

        shared_classifier = (config.vocab_size < 0);
        if (config.vocab_size < 0) config.vocab_size = -config.vocab_size;
    }

    // Some exported model files may *still* share classifier weights even if vocab_size is positive.
    // Detect this by comparing expected weights size vs actual file size.
    UINT64 model_file_size = 0;
    if (!use_gguf_inference) {
        EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
        UINTN info_size = 0;
        EFI_STATUS st = uefi_call_wrapper(ModelFile->GetInfo, 4, ModelFile, &FileInfoGuid, &info_size, NULL);
        if (st == EFI_BUFFER_TOO_SMALL && info_size > 0) {
            EFI_FILE_INFO *info = NULL;
            st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
            if (!EFI_ERROR(st) && info) {
                st = uefi_call_wrapper(ModelFile->GetInfo, 4, ModelFile, &FileInfoGuid, &info_size, info);
                if (!EFI_ERROR(st)) {
                    model_file_size = info->FileSize;
                }
                uefi_call_wrapper(BS->FreePool, 1, info);
            }
        }
    }

    // For GGUF, optionally load weights into a compact Q8_0 blob when possible.
    // This keeps matrices quantized in RAM and dequantizes on-the-fly during matmuls.
    int use_q8_blob = 0;
    UINT64 q8_blob_bytes = 0;
    if (g_cfg_gguf_q8_blob && use_gguf_inference && gguf_plan) {
        if (llmk_gguf_plan_supports_q8_0_blob(gguf_plan, shared_classifier)) {
            EFI_STATUS bst = llmk_gguf_calc_llama2_q8_0_blob_bytes(
                gguf_plan,
                config.dim,
                config.hidden_dim,
                config.n_layers,
                config.n_heads,
                config.n_kv_heads,
                config.vocab_size,
                config.seq_len,
                shared_classifier,
                &q8_blob_bytes
            );
            if (!EFI_ERROR(bst) && q8_blob_bytes > 0) {
                use_q8_blob = 1;
                if (g_boot_verbose) {
                    Print(L"[gguf] Q8_0 blob enabled: %lu MB\r\n", (UINT64)(q8_blob_bytes / (1024ULL * 1024ULL)));
                }
            } else {
                Print(L"NOTE: GGUF Q8_0 blob sizing failed (%r); using float32 load.\r\n", bst);
            }
        }
    } else if (!g_cfg_gguf_q8_blob && use_gguf_inference && gguf_plan) {
        if (g_boot_verbose) {
            Print(L"[gguf] Q8_0 blob disabled by repl.cfg; using float32 load.\r\n");
        }
    }
    
    if (g_boot_verbose) {
        if (g_boot_verbose >= 2) llmk_debug_print_loaded_model_path(L"before_model_loaded_print");
        char model8[192];
        llmk_char16_to_ascii_cap(model8, (int)sizeof(model8), g_loaded_model_path16);
        Print(L"OK: Model loaded: ");
        llmk_print_ascii(model8[0] ? model8 : "(unknown)");
        Print(L" (dim=%d, layers=%d, heads=%d, kv=%d, vocab=%d, seq=%d)\r\n\r\n",
              config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
    }

    // Always print minimal boot markers for CI/smoke tests (serial-friendly).
    {
        char model8[192];
        llmk_char16_to_ascii_cap(model8, (int)sizeof(model8), g_loaded_model_path16);
        Print(L"OK: Djibion boot\r\n");
        Print(L"OK: Model loaded: ");
        llmk_print_ascii(model8[0] ? model8 : "(unknown)");
        Print(L"\r\n");
        Print(L"OK: Version: %s\r\n\r\n", LLMB_BUILD_ID);
    }

    llmk_boot_mark(L"model_header_loaded");

    // ========================================================================
    // [3/7] Kernel zones + heap (auto-sized)
    // ========================================================================

    llmk_overlay_stage(3, 7);

    {
        int min_ctx = 64;
        int before_model = config.seq_len;
        int effective = config.seq_len;

        // Apply user-requested context length (can only reduce vs model).
        if (g_cfg_ctx_len > 0) {
            int target = g_cfg_ctx_len;
            if (target < 0) target = -target;
            if (target < min_ctx) target = min_ctx;
            if (target < effective) {
                if (g_boot_verbose) {
                    Print(L"[cfg] ctx_len=%d -> effective seq_len=%d (model=%d)\r\n",
                          g_cfg_ctx_len, target, before_model);
                }
                effective = target;
            }
        }

        // OO M3 (homeostasis): clamp effective context length in SAFE/DEGRADED.
        // Keep this deterministic and serial-visible when it triggers.
        if (g_cfg_oo_enable && g_oo_last_mode_valid) {
            int cap = 0;
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE) cap = 256;
            else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED) cap = 512;
            if (cap > 0 && effective > cap) {
                int from = effective;
                effective = cap;
                Print(L"OK: OO ctx_len clamp: %d -> %d (mode=%s)\r\n",
                      from, effective, llmk_oo_mode_name(g_oo_last_mode));
            }
        }

        if (effective < min_ctx) effective = min_ctx;
        if (effective < config.seq_len) {
            config.seq_len = effective;
        }
    }

    int kv_dim = (config.dim * config.n_kv_heads) / config.n_heads;
    int head_size = config.dim / config.n_heads;

    // OO M3 (homeostasis): RAM budget preflight.
    // Goal: avoid hard failures on low-memory guests by (1) allowing a smaller Zone-B minimum in SAFE/DEGRADED,
    // and (2) optionally reducing seq_len further if the estimated Zone-B total would exceed available RAM.
    if (g_cfg_oo_enable && g_oo_last_mode_valid && (g_oo_last_mode == LLMK_OO_MODE_SAFE || g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
        UINT64 sys_ram = llmk_get_conventional_ram_bytes_best_effort();
        if (sys_ram > 0) {
            const UINT64 reserve = 128ULL * 1024ULL * 1024ULL;
            UINT64 usable = (sys_ram > reserve) ? (sys_ram - reserve) : (sys_ram * 3ULL) / 4ULL;

            UINT64 min_total_policy = 0;
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE) min_total_policy = 512ULL * 1024ULL * 1024ULL;
            else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED) min_total_policy = 640ULL * 1024ULL * 1024ULL;

            if (g_cfg_oo_min_total_mb >= 0) {
                min_total_policy = (UINT64)g_cfg_oo_min_total_mb * 1024ULL * 1024ULL;
            }

            int seq_from = config.seq_len;
            int seq = config.seq_len;
            for (int iter = 0; iter < 8; iter++) {
                if (seq < 64) seq = 64;

                // Compute weights size (floats), with seq_len substituted for the freq_cis arrays.
                UINTN n_floats_base_pf = 0;
                n_floats_base_pf += (UINTN)config.vocab_size * (UINTN)config.dim;                   // token_embedding_table
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_att_weight
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wq
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wk
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wv
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wo
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_ffn_weight
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w1
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.hidden_dim * (UINTN)config.dim; // w2
                n_floats_base_pf += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w3
                n_floats_base_pf += (UINTN)config.dim;                                              // rms_final_weight
                n_floats_base_pf += (UINTN)seq * (UINTN)head_size / 2;                               // freq_cis_real
                n_floats_base_pf += (UINTN)seq * (UINTN)head_size / 2;                               // freq_cis_imag

                UINTN n_floats_with_cls_pf = n_floats_base_pf + (UINTN)config.vocab_size * (UINTN)config.dim;

                int shared_pf = shared_classifier;
                if (!use_q8_blob && model_file_size > 0) {
                    UINT64 available = model_file_size;
                    UINT64 header_bytes = (UINT64)(7 * sizeof(int));
                    if (available > header_bytes) available -= header_bytes;
                    UINT64 bytes_base = (UINT64)n_floats_base_pf * sizeof(float);
                    UINT64 bytes_with = (UINT64)n_floats_with_cls_pf * sizeof(float);

                    if (available < bytes_with && available >= bytes_base) shared_pf = 1;
                    else if (available >= bytes_with) shared_pf = 0;
                }

                UINT64 weights_u64 = use_q8_blob ? (UINT64)q8_blob_bytes
                                                : (UINT64)(shared_pf ? n_floats_base_pf : n_floats_with_cls_pf) * (UINT64)sizeof(float);

                UINT64 kv_bytes = llmk_calc_kv_bytes_for_seq(&config, seq, kv_dim);
                UINT64 state_u64 = llmk_calc_state_bytes_for_seq(&config, seq, kv_dim);

                UINT64 tokenizer_u64 = (UINT64)config.vocab_size * ((UINT64)sizeof(char*) + (UINT64)sizeof(float));
                tokenizer_u64 += 4ULL * 1024ULL * 1024ULL;

                UINT64 slack_u64 = 16ULL * 1024ULL * 1024ULL;
                UINT64 scratch_u64 = 32ULL * 1024ULL * 1024ULL;
                UINT64 zonec_u64 = 8ULL * 1024ULL * 1024ULL;

                UINT64 acts_u64 = (state_u64 >= kv_bytes ? (state_u64 - kv_bytes) : 0ULL) + tokenizer_u64 + slack_u64;
                UINT64 total = weights_u64 + kv_bytes + scratch_u64 + acts_u64 + zonec_u64;

                UINT64 min_total = min_total_policy;
                if (min_total > 0 && total < min_total) total = min_total;

                if (total <= usable) break;

                int next = seq / 2;
                if (next < 64) {
                    seq = 64;
                    break;
                }
                seq = next;
            }

            if (seq != seq_from) {
                Print(L"OK: OO ram preflight: seq_len %d -> %d (mode=%s)\r\n", seq_from, seq, llmk_oo_mode_name(g_oo_last_mode));
                config.seq_len = seq;
            }
        }
    }

    // Compute total weights size (floats)
    UINTN n_floats_base = 0;
    n_floats_base += (UINTN)config.vocab_size * (UINTN)config.dim;                   // token_embedding_table
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_att_weight
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wq
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wk
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)kv_dim;     // wv
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.dim; // wo
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim;                     // rms_ffn_weight
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w1
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.hidden_dim * (UINTN)config.dim; // w2
    n_floats_base += (UINTN)config.n_layers * (UINTN)config.dim * (UINTN)config.hidden_dim; // w3
    n_floats_base += (UINTN)config.dim;                                              // rms_final_weight
    n_floats_base += (UINTN)config.seq_len * (UINTN)head_size / 2;                   // freq_cis_real
    n_floats_base += (UINTN)config.seq_len * (UINTN)head_size / 2;                   // freq_cis_imag

    UINTN n_floats_with_cls = n_floats_base + (UINTN)config.vocab_size * (UINTN)config.dim;

    // If file size is known, use it to infer whether wcls is present.
    if (model_file_size > 0) {
        UINT64 available = model_file_size;
        UINT64 header_bytes = (UINT64)(7 * sizeof(int));
        if (available > header_bytes) available -= header_bytes;
        UINT64 bytes_base = (UINT64)n_floats_base * sizeof(float);
        UINT64 bytes_with = (UINT64)n_floats_with_cls * sizeof(float);

        if (available < bytes_with && available >= bytes_base) {
            shared_classifier = 1;
        } else if (available >= bytes_with) {
            shared_classifier = 0;
        }
    }

    UINTN n_floats = shared_classifier ? n_floats_base : n_floats_with_cls;
    UINTN weights_bytes = use_q8_blob ? (UINTN)q8_blob_bytes : (n_floats * sizeof(float));
    UINTN state_bytes = 0;
    state_bytes += (UINTN)config.dim * sizeof(float) * 3; // x, xb, xb2
    state_bytes += (UINTN)config.hidden_dim * sizeof(float) * 2; // hb, hb2
    state_bytes += (UINTN)config.dim * sizeof(float); // q
    state_bytes += (UINTN)kv_dim * sizeof(float) * 2; // k, v
    state_bytes += (UINTN)config.n_heads * (UINTN)config.seq_len * sizeof(float); // att
    state_bytes += (UINTN)config.vocab_size * sizeof(float); // logits
    state_bytes += (UINTN)config.n_layers * (UINTN)config.seq_len * (UINTN)kv_dim * sizeof(float) * 2; // key/value cache

    // Tokenizer: pointers + scores + strings (strings size varies; reserve a safe budget)
    UINTN tokenizer_bytes = (UINTN)config.vocab_size * (sizeof(char*) + sizeof(float));
    tokenizer_bytes += 4 * 1024 * 1024; // string storage budget

    UINTN slack_bytes = 16 * 1024 * 1024;
    heap_size = weights_bytes + state_bytes + tokenizer_bytes + slack_bytes;
    if (heap_size < 100ULL * 1024ULL * 1024ULL) heap_size = 100ULL * 1024ULL * 1024ULL;

    // Initialize LLM-Kernel Zone B arenas sized from the same accounting.
    // This makes the REPL and the kernel work together: all big allocations go through zones/sentinel.
    {
        UINT64 zonec_bytes = 8ULL * 1024ULL * 1024ULL;
        UINT64 scratch_bytes = 32ULL * 1024ULL * 1024ULL;

        // KV cache lives in its own arena.
        UINT64 kv_bytes = (UINT64)config.n_layers * (UINT64)config.seq_len * (UINT64)kv_dim * sizeof(float) * 2ULL;

        UINT64 weights_u64 = (UINT64)weights_bytes;
        UINT64 acts_u64 = (UINT64)(state_bytes - (UINTN)kv_bytes) + (UINT64)tokenizer_bytes + (UINT64)slack_bytes;

        // Total Zone B includes all arenas.
        UINT64 total = weights_u64 + kv_bytes + scratch_bytes + acts_u64 + zonec_bytes;

        // Legacy min: 768MB (or 1GB for larger totals). In OO SAFE/DEGRADED, allow a smaller floor.
        UINT64 default_min_total = (total > 768ULL * 1024ULL * 1024ULL) ? (1024ULL * 1024ULL * 1024ULL) : (768ULL * 1024ULL * 1024ULL);
        UINT64 min_total = default_min_total;
        if (g_cfg_oo_enable && g_oo_last_mode_valid) {
            if (g_oo_last_mode == LLMK_OO_MODE_SAFE) {
                min_total = 512ULL * 1024ULL * 1024ULL;
            } else if (g_oo_last_mode == LLMK_OO_MODE_DEGRADED) {
                min_total = 640ULL * 1024ULL * 1024ULL;
            }
        }
        if (g_cfg_oo_enable && g_oo_last_mode_valid && (g_oo_last_mode == LLMK_OO_MODE_SAFE || g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
            if (g_cfg_oo_min_total_mb >= 0) {
                min_total = (UINT64)g_cfg_oo_min_total_mb * 1024ULL * 1024ULL;
            }
        }
        if (g_cfg_oo_enable && g_oo_last_mode_valid && (g_oo_last_mode == LLMK_OO_MODE_SAFE || g_oo_last_mode == LLMK_OO_MODE_DEGRADED)) {
            if (min_total != default_min_total) {
                Print(L"OK: OO zones min_total=%luMB (mode=%s)\r\n", (UINT64)(min_total / (1024ULL * 1024ULL)), llmk_oo_mode_name(g_oo_last_mode));
            }
        }
        if (total < min_total) total = min_total;

        LlmkZonesConfig zcfg;
        zcfg.total_bytes = total;
        zcfg.weights_bytes = weights_u64;
        zcfg.kv_bytes = kv_bytes;
        zcfg.scratch_bytes = scratch_bytes;
        zcfg.activations_bytes = acts_u64;
        zcfg.zone_c_bytes = zonec_bytes;

        if (g_boot_verbose) {
            Print(L"[3/7] Init kernel zones (%d MB)...\r\n", (int)(total / (1024 * 1024)));
        }
        status = llmk_zones_init(BS, &zcfg, &g_zones);
        if (EFI_ERROR(status) && min_total > 0 && total > min_total) {
            // If the computed size can't be allocated (e.g. low guest RAM / fragmentation),
            // fall back to a smaller default so the REPL can still boot with smaller models.
            if (g_boot_verbose) {
                Print(L"[llmk] zones alloc failed, retrying with %d MB...\r\n", (int)(min_total / (1024 * 1024)));
            }
            zcfg.total_bytes = min_total;
            zcfg.weights_bytes = 0;
            zcfg.kv_bytes = 0;
            zcfg.scratch_bytes = 0;
            zcfg.activations_bytes = 0;
            zcfg.zone_c_bytes = 0;
            status = llmk_zones_init(BS, &zcfg, &g_zones);
        }
        if (EFI_ERROR(status)) {
            Print(L"ERROR: llmk_zones_init failed: %r\r\n", status);
            return status;
        }

        // Init Zone C log (best-effort)
        EFI_STATUS logst = llmk_log_init(&g_zones, &g_llmk_log);
        if (EFI_ERROR(logst)) {
            g_llmk_log.entries = 0;
            g_llmk_log.capacity = 0;
            g_llmk_log.write_idx = 0;
        }

        // Init sentinel
        LlmkSentinelConfig scfg;
        scfg.enabled = TRUE;
        // REPL: keep allocation failures fatal, but keep budget overruns non-fatal.
        // This lets us "activate budgets" without killing the whole session.
        scfg.strict_mode = FALSE;
        scfg.strict_alloc = TRUE;
        scfg.strict_budget = FALSE;
        scfg.max_cycles = 0;
        scfg.max_cycles_prefill = 0;
        scfg.max_cycles_decode = 0;
        scfg.log_violations = TRUE;

        status = llmk_sentinel_init(&g_sentinel, &g_zones, (g_llmk_log.capacity ? &g_llmk_log : 0), &scfg);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: llmk_sentinel_init failed: %r\r\n", status);
            return status;
        }

        g_llmk_ready = 1;

        // Feed memory info (best-effort) into Compatibilion.
        compatibilion_set_memory(&g_compatibilion, (uint64_t)g_zones.zone_b_size);

        if (g_boot_verbose) {
            llmk_zones_print(&g_zones);
            llmk_sentinel_print_status(&g_sentinel);
            Print(L"OK: Kernel allocator ready\r\n\r\n");
        }
    }
    
    // ========================================================================
    // [4/7] Weight Pointers
    // ========================================================================

    llmk_overlay_stage(4, 7);
    
    if (g_boot_verbose) {
        Print(L"[4/7] Mapping weights...\r\n");
    }
    bytes_to_read = weights_bytes;
    void* weights_mem_raw = (void*)llmk_alloc_weights((UINT64)bytes_to_read, L"weights");
    if (weights_mem_raw == NULL) {
        Print(L"ERROR: OOM while allocating weights (%d MB needed).\r\n", (int)(bytes_to_read / (1024 * 1024)));
        Print(L"Hint: use a smaller model, or GGUF Q8_0 blob (gguf_q8_blob=1), or reduce ctx_len in repl.cfg.\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    TransformerWeights weights;
    // Default init.
    weights.kind = 0;
    weights.token_embedding_table = NULL;
    weights.rms_att_weight = NULL;
    weights.wq = NULL;
    weights.wk = NULL;
    weights.wv = NULL;
    weights.wo = NULL;
    weights.rms_ffn_weight = NULL;
    weights.w1 = NULL;
    weights.w2 = NULL;
    weights.w3 = NULL;
    weights.rms_final_weight = NULL;
    weights.wcls = NULL;
    weights.token_embedding_table_q8 = NULL;
    weights.wq_q8 = NULL;
    weights.wk_q8 = NULL;
    weights.wv_q8 = NULL;
    weights.wo_q8 = NULL;
    weights.w1_q8 = NULL;
    weights.w2_q8 = NULL;
    weights.w3_q8 = NULL;
    weights.wcls_q8 = NULL;
    weights.tok_embd_row_bytes = 0;
    weights.wq_layer_bytes = 0;
    weights.wk_layer_bytes = 0;
    weights.wv_layer_bytes = 0;
    weights.wo_layer_bytes = 0;
    weights.w1_layer_bytes = 0;
    weights.w2_layer_bytes = 0;
    weights.w3_layer_bytes = 0;

    if (use_gguf_inference) {
        if (use_q8_blob) {
            status = llmk_gguf_load_into_llama2_q8_0_blob(
                ModelFile,
                gguf_plan,
                weights_mem_raw,
                q8_blob_bytes,
                config.dim,
                config.hidden_dim,
                config.n_layers,
                config.n_heads,
                config.n_kv_heads,
                config.vocab_size,
                config.seq_len,
                shared_classifier
            );
            if (gguf_plan) {
                llmk_gguf_free_plan(gguf_plan);
                gguf_plan = NULL;
            }
            if (EFI_ERROR(status)) {
                Print(L"ERROR: Failed to load GGUF Q8_0 blob weights (%r).\r\n", status);
                return EFI_LOAD_ERROR;
            }

            // Map Q8_0 blob layout into pointer fields.
            {
                const UINT64 A = 16;
                UINT8 *base = (UINT8 *)weights_mem_raw;
                UINT64 off = 0;

                UINT64 dim_u = (UINT64)config.dim;
                UINT64 hid_u = (UINT64)config.hidden_dim;
                UINT64 lay_u = (UINT64)config.n_layers;
                UINT64 vocab_u = (UINT64)config.vocab_size;
                UINT64 kv_dim_u = (UINT64)kv_dim;
                UINT64 head_size_u = (UINT64)head_size;

                UINT64 tok_row = llmk_q8_0_row_bytes(config.dim);
                UINT64 wq_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 wk_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 wo_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 w1_row  = llmk_q8_0_row_bytes(config.dim);
                UINT64 w2_row  = llmk_q8_0_row_bytes(config.hidden_dim);
                UINT64 w3_row  = llmk_q8_0_row_bytes(config.dim);
                if (!tok_row || !wq_row || !wk_row || !wo_row || !w1_row || !w2_row || !w3_row) {
                    Print(L"ERROR: Q8_0 blob requires dims multiple of 32 (dim=%d hidden=%d).\r\n", config.dim, config.hidden_dim);
                    return EFI_UNSUPPORTED;
                }

                weights.kind = 1;
                weights.tok_embd_row_bytes = tok_row;
                weights.wq_layer_bytes = (UINT64)config.dim * wq_row;
                weights.wk_layer_bytes = (UINT64)kv_dim * wk_row;
                weights.wv_layer_bytes = (UINT64)kv_dim * wk_row;
                weights.wo_layer_bytes = (UINT64)config.dim * wo_row;
                weights.w1_layer_bytes = (UINT64)config.hidden_dim * w1_row;
                weights.w2_layer_bytes = (UINT64)config.dim * w2_row;
                weights.w3_layer_bytes = (UINT64)config.hidden_dim * w3_row;

                // token_embedding_table (Q8_0) [vocab, dim]
                off = llmk_align_up_u64(off, A);
                weights.token_embedding_table_q8 = base + (UINTN)off;
                off += vocab_u * tok_row;

                // rms_att_weight (F32) [n_layers, dim]
                off = llmk_align_up_u64(off, A);
                weights.rms_att_weight = (float *)(base + (UINTN)off);
                off += lay_u * dim_u * 4ULL;

                // wq (Q8_0) per-layer [dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wq_q8 = base + (UINTN)off;
                off += lay_u * weights.wq_layer_bytes;

                // wk (Q8_0) per-layer [kv_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wk_q8 = base + (UINTN)off;
                off += lay_u * weights.wk_layer_bytes;

                // wv (Q8_0) per-layer [kv_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wv_q8 = base + (UINTN)off;
                off += lay_u * weights.wv_layer_bytes;

                // wo (Q8_0) per-layer [dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.wo_q8 = base + (UINTN)off;
                off += lay_u * weights.wo_layer_bytes;

                // rms_ffn_weight (F32) [n_layers, dim]
                off = llmk_align_up_u64(off, A);
                weights.rms_ffn_weight = (float *)(base + (UINTN)off);
                off += lay_u * dim_u * 4ULL;

                // w1 (Q8_0) per-layer [hidden_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.w1_q8 = base + (UINTN)off;
                off += lay_u * weights.w1_layer_bytes;

                // w2 (Q8_0) per-layer [dim, hidden_dim]
                off = llmk_align_up_u64(off, A);
                weights.w2_q8 = base + (UINTN)off;
                off += lay_u * weights.w2_layer_bytes;

                // w3 (Q8_0) per-layer [hidden_dim, dim]
                off = llmk_align_up_u64(off, A);
                weights.w3_q8 = base + (UINTN)off;
                off += lay_u * weights.w3_layer_bytes;

                // rms_final_weight (F32) [dim]
                off = llmk_align_up_u64(off, A);
                weights.rms_final_weight = (float *)(base + (UINTN)off);
                off += dim_u * 4ULL;

                // freq_cis_real + freq_cis_imag (F32 zeros) [seq_len * head_size / 2] each
                off = llmk_align_up_u64(off, A);
                off += (UINT64)config.seq_len * head_size_u / 2ULL * 4ULL;
                off += (UINT64)config.seq_len * head_size_u / 2ULL * 4ULL;

                // wcls (Q8_0) [vocab, dim] if not shared
                if (shared_classifier) {
                    weights.wcls_q8 = weights.token_embedding_table_q8;
                } else {
                    off = llmk_align_up_u64(off, A);
                    weights.wcls_q8 = base + (UINTN)off;
                    off += vocab_u * tok_row;
                }

                (void)hid_u;
                (void)kv_dim_u;
            }
        } else {
            float *weights_mem = (float *)weights_mem_raw;
            status = llmk_gguf_load_into_llama2_layout(
                ModelFile,
                gguf_plan,
                weights_mem,
                config.dim,
                config.hidden_dim,
                config.n_layers,
                config.n_heads,
                config.n_kv_heads,
                config.vocab_size,
                config.seq_len,
                shared_classifier
            );
            if (gguf_plan) {
                llmk_gguf_free_plan(gguf_plan);
                gguf_plan = NULL;
            }
            if (EFI_ERROR(status)) {
                Print(L"ERROR: Failed to load GGUF weights (%r).\r\n", status);
                return EFI_LOAD_ERROR;
            }

            float* weights_ptr = weights_mem;

            weights.kind = 0;
            weights.token_embedding_table = weights_ptr;
            weights_ptr += config.vocab_size * config.dim;
    
            weights.rms_att_weight = weights_ptr;
            weights_ptr += config.n_layers * config.dim;
    
            weights.wq = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.dim;
    
            weights.wk = weights_ptr;
            weights_ptr += config.n_layers * config.dim * kv_dim;
    
            weights.wv = weights_ptr;
            weights_ptr += config.n_layers * config.dim * kv_dim;
    
            weights.wo = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.dim;
    
            weights.rms_ffn_weight = weights_ptr;
            weights_ptr += config.n_layers * config.dim;
    
            weights.w1 = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.hidden_dim;
    
            weights.w2 = weights_ptr;
            weights_ptr += config.n_layers * config.hidden_dim * config.dim;
    
            weights.w3 = weights_ptr;
            weights_ptr += config.n_layers * config.dim * config.hidden_dim;
    
            weights.rms_final_weight = weights_ptr;
            weights_ptr += config.dim;
    
            // Skip freq_cis_real and freq_cis_imag (RoPE precomputed freqs)
            weights_ptr += config.seq_len * head_size / 2;  // freq_cis_real
            weights_ptr += config.seq_len * head_size / 2;  // freq_cis_imag

            weights.wcls = shared_classifier ? weights.token_embedding_table : weights_ptr;
        }
    } else {
        float *weights_mem = (float *)weights_mem_raw;
        status = read_exact(ModelFile, weights_mem, bytes_to_read);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: Failed to read weights (need model file + enough RAM).\r\n");
            return EFI_LOAD_ERROR;
        }

        float* weights_ptr = weights_mem;

        weights.kind = 0;
        weights.token_embedding_table = weights_ptr;
        weights_ptr += config.vocab_size * config.dim;

        weights.rms_att_weight = weights_ptr;
        weights_ptr += config.n_layers * config.dim;

        weights.wq = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.dim;

        weights.wk = weights_ptr;
        weights_ptr += config.n_layers * config.dim * kv_dim;

        weights.wv = weights_ptr;
        weights_ptr += config.n_layers * config.dim * kv_dim;

        weights.wo = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.dim;

        weights.rms_ffn_weight = weights_ptr;
        weights_ptr += config.n_layers * config.dim;

        weights.w1 = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.hidden_dim;

        weights.w2 = weights_ptr;
        weights_ptr += config.n_layers * config.hidden_dim * config.dim;

        weights.w3 = weights_ptr;
        weights_ptr += config.n_layers * config.dim * config.hidden_dim;

        weights.rms_final_weight = weights_ptr;
        weights_ptr += config.dim;

        // Skip freq_cis_real and freq_cis_imag (RoPE precomputed freqs)
        weights_ptr += config.seq_len * head_size / 2;  // freq_cis_real
        weights_ptr += config.seq_len * head_size / 2;  // freq_cis_imag

        weights.wcls = shared_classifier ? weights.token_embedding_table : weights_ptr;

    }

    uefi_call_wrapper(ModelFile->Close, 1, ModelFile);
    
    if (g_boot_verbose) {
        Print(L"OK: Weights mapped\r\n\r\n");
    }

    llmk_boot_mark(L"weights_mapped");
    
    // ========================================================================
    // [5/7] State Buffers
    // ========================================================================

    llmk_overlay_stage(5, 7);
    
    if (g_boot_verbose) {
        Print(L"[5/7] Allocating state buffers...\r\n");
    }
    
    RunState state;

    int ctx_min = 64;
    int ctx_try = config.seq_len;
    int alloc_ok = 0;
    while (!alloc_ok) {
        state.x = (float*)simple_alloc(config.dim * sizeof(float));
        state.xb = (float*)simple_alloc(config.dim * sizeof(float));
        state.xb2 = (float*)simple_alloc(config.dim * sizeof(float));
        state.hb = (float*)simple_alloc(config.hidden_dim * sizeof(float));
        state.hb2 = (float*)simple_alloc(config.hidden_dim * sizeof(float));
        state.q = (float*)simple_alloc(config.dim * sizeof(float));
        state.k = (float*)simple_alloc(kv_dim * sizeof(float));
        state.v = (float*)simple_alloc(kv_dim * sizeof(float));
        state.att = (float*)simple_alloc(config.n_heads * config.seq_len * sizeof(float));
        state.logits = (float*)simple_alloc(config.vocab_size * sizeof(float));
        state.key_cache = (float*)llmk_alloc_kv((UINT64)config.n_layers * (UINT64)config.seq_len * (UINT64)kv_dim * sizeof(float), L"key cache");
        state.value_cache = (float*)llmk_alloc_kv((UINT64)config.n_layers * (UINT64)config.seq_len * (UINT64)kv_dim * sizeof(float), L"value cache");

        alloc_ok = (state.x && state.xb && state.xb2 && state.hb && state.hb2 && state.q && state.k && state.v &&
                    state.att && state.logits && state.key_cache && state.value_cache);
        if (alloc_ok) break;

        Print(L"\r\nERROR: OOM while allocating state/KV (seq_len=%d).\r\n", config.seq_len);
        llmk_print_ram_budget();

        if (g_llmk_ready) {
            llmk_arena_wipe_and_reset(&g_zones, LLMK_ARENA_ACTIVATIONS, 0);
            llmk_arena_wipe_and_reset(&g_zones, LLMK_ARENA_KV_CACHE, 0);
        }

        if (ctx_try <= ctx_min) {
            Print(L"Hint: use a smaller model or lower ctx_len in repl.cfg.\r\n");
            return EFI_OUT_OF_RESOURCES;
        }

        ctx_try = ctx_try / 2;
        if (ctx_try < ctx_min) ctx_try = ctx_min;
        config.seq_len = ctx_try;
        Print(L"Retrying with smaller ctx_len=%d...\r\n\r\n", config.seq_len);
    }
    
    if (g_boot_verbose) {
        Print(L"OK: State buffers allocated\r\n\r\n");
    }

    llmk_boot_mark(L"state_alloc");
    
    // ========================================================================
    // [6/7] Tokenizer
    // ========================================================================

    llmk_overlay_stage(6, 7);
    
    if (g_boot_verbose) {
        Print(L"[6/7] Loading tokenizer...\r\n");
    }
    
    EFI_FILE_HANDLE TokFile;
    TokFile = NULL;
    status = llmk_open_read_with_fat83_fallback(Root, L"tokenizer.bin", &TokFile, NULL, 0, L"tokenizer");
    if (EFI_ERROR(status) || !TokFile) {
        Print(L"ERROR: Tokenizer file not found (%r)\r\n", status);
        return status;
    }
    
    Tokenizer tokenizer;
    bytes_to_read = sizeof(int);
    uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &tokenizer.max_token_length);
    
    tokenizer.vocab_size = config.vocab_size;
    tokenizer.vocab = (char**)simple_alloc(config.vocab_size * sizeof(char*));
    tokenizer.vocab_scores = (float*)simple_alloc(config.vocab_size * sizeof(float));
    
    for (int i = 0; i < config.vocab_size; i++) {
        // Keep the GOP loading overlay alive while parsing tokenizer.
        // Rate-limited to stay cheap in UEFI.
        if (((UINT32)i & 0xFFu) == 0u) {
            InterfaceFx_ProgressBytes((UINTN)(i + 1), (UINTN)config.vocab_size);
        }
        bytes_to_read = sizeof(float);
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &tokenizer.vocab_scores[i]);
        
        int len;
        bytes_to_read = sizeof(int);
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, &len);
        
        tokenizer.vocab[i] = (char*)simple_alloc(len + 1);
        bytes_to_read = len;
        uefi_call_wrapper(TokFile->Read, 3, TokFile, &bytes_to_read, tokenizer.vocab[i]);
        tokenizer.vocab[i][len] = '\0';
    }
    
    uefi_call_wrapper(TokFile->Close, 1, TokFile);

    // Loading finished: stop the animated overlay now.
    InterfaceFx_End();

    llmk_boot_mark(L"tokenizer_loaded");
    
    if (g_boot_verbose) {
        Print(L"OK: Tokenizer loaded (%d tokens)\r\n\r\n", tokenizer.vocab_size);

        llmk_boot_print_timing_summary();

        // ========================================================================
        // [7/7] Interactive REPL Loop
        // ========================================================================
        Print(L"[7/7] Entering chat loop...\r\n\r\n");

        Print(L"----------------------------------------\r\n");
        Print(L"  CHAT MODE ACTIVE\r\n");
        Print(L"  Type 'quit' or 'exit' to stop\r\n");
        Print(L"  Multi-line: end line with '\\\\' to continue; ';;' alone submits\r\n");
        Print(L"  Commands: use /help or /commands\r\n");
        Print(L"----------------------------------------\r\n\r\n");
    } else {
        Print(L"OK: REPL ready (/help)\r\n\r\n");
    }

    llmk_boot_mark(L"repl_ready");
    
    // Initialize runtime metrics
    llmk_metrics_reset();
    
    // Sampling parameters
    // Default sampling tuned for TinyStories (less looping, still creative).
    float temperature = 0.85f;
    float min_p = 0.05f;
    float top_p = 0.95f;
    int top_k = 80;
    float repeat_penalty = 1.15f;
    int no_repeat_ngram = 4;
    int max_gen_tokens = 160;
    int stats_enabled = 1;
    // Turn-based chat defaults: stop when the model starts the next user prompt.
    // Double-newline stopping is useful for TinyStories prose, but is too aggressive for chat.
    int stop_on_you = 1;
    int stop_on_double_nl = 0;

    // Optional config: repl.cfg (key=value). Best-effort; ignored if missing.
    llmk_load_repl_cfg_best_effort(
        &temperature,
        &min_p,
        &top_p,
        &top_k,
        &repeat_penalty,
        &no_repeat_ngram,
        &max_gen_tokens,
        &stats_enabled,
        &stop_on_you,
        &stop_on_double_nl
    );

    // Optional Diopion config (repl.cfg): burst defaults + profile.
    llmk_load_repl_cfg_diopion_best_effort(&g_diopion);

    // Optional Djibion config (repl.cfg): enables enforcement at boot.
    llmk_load_repl_cfg_djibion_best_effort(&g_djibion);

    if (g_cfg_loaded && g_boot_verbose) {
        Print(L"[cfg] autorun_autostart=%d file=%s shutdown_when_done=%d\r\n",
              g_cfg_autorun_autostart,
              g_cfg_autorun_file,
              g_cfg_autorun_shutdown_when_done);
    }

    // OO config defaults (best-effort from repl.cfg)
    int oo_autoload = 0;
    int oo_autosave_every = 0;
    char oo_file_ascii[96];
    oo_file_ascii[0] = 0;
    llmk_load_repl_cfg_oo_best_effort(&oo_autoload, &oo_autosave_every, oo_file_ascii, (int)sizeof(oo_file_ascii));

    CHAR16 oo_state_file[96];
    if (oo_file_ascii[0]) {
        ascii_to_char16(oo_state_file, oo_file_ascii, (int)(sizeof(oo_state_file) / sizeof(oo_state_file[0])));
    } else {
        StrCpy(oo_state_file, L"oo-state.bin");
    }

    if (oo_autoload) {
        // Djibion gate (best-effort) for boot-time OO autoload.
        CHAR16 load_name[96];
        StrCpy(load_name, oo_state_file);
        if (g_djibion.mode != DJIBION_MODE_OFF) {
            char file8[128];
            llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), load_name);
            DjibionDecision d;
            djibion_decide(&g_djibion, DJIBION_ACT_OO_LOAD, file8, 0, &d);
            djibion_log_if_observe(&g_djibion, "oo_autoload", &d);
            if (djibion_should_block(&g_djibion, &d)) {
                CHAR16 msg[160];
                ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                Print(L"[oo] autoload blocked by Djibion: %s\r\n", msg);
                goto oo_autoload_done;
            }
            if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                Print(L"[oo] autoload path transformed by Djibion -> ");
                llmk_print_ascii(d.transformed_arg0);
                Print(L"\r\n");
                ascii_to_char16(load_name, d.transformed_arg0, (int)(sizeof(load_name) / sizeof(load_name[0])));
            }
        }

        void *buf = NULL;
        UINTN len = 0;
        EFI_STATUS st = llmk_read_entire_file_best_effort(load_name, &buf, &len);
        CHAR16 bak[120];
        llmk_make_bak_name(load_name, bak, (int)(sizeof(bak) / sizeof(bak[0])));

        if (EFI_ERROR(st)) {
            // Fallback to .bak
            EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
            if (EFI_ERROR(st2)) {
                Print(L"[oo] autoload skipped (%r)\r\n", st);
            } else {
                int imported = llmk_oo_import((const char *)buf, (int)len);
                uefi_call_wrapper(BS->FreePool, 1, buf);
                if (imported < 0) {
                    Print(L"[oo] autoload failed (parse)\r\n");
                } else {
                    Print(L"[oo] autoloaded %d entr%s from %s\r\n", imported, (imported == 1) ? L"y" : L"ies", bak);
                }
            }
        } else {
            int imported = llmk_oo_import((const char *)buf, (int)len);
            uefi_call_wrapper(BS->FreePool, 1, buf);
            if (imported < 0) {
                // Fallback to .bak if main parse failed.
                EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
                if (EFI_ERROR(st2)) {
                    Print(L"[oo] autoload failed (parse)\r\n");
                } else {
                    imported = llmk_oo_import((const char *)buf, (int)len);
                    uefi_call_wrapper(BS->FreePool, 1, buf);
                    if (imported < 0) {
                        Print(L"[oo] autoload failed (parse)\r\n");
                    } else {
                        Print(L"[oo] autoloaded %d entr%s from %s\r\n", imported, (imported == 1) ? L"y" : L"ies", bak);
                    }
                }
            } else {
                Print(L"[oo] autoloaded %d entr%s from %s\r\n", imported, (imported == 1) ? L"y" : L"ies", load_name);
            }
        }
    }

oo_autoload_done:

    // Optional autorun: only if enabled in repl.cfg (autorun_autostart=1).
    if (g_cfg_autorun_autostart) {
        // Djibion gate for boot-time autorun (autostart).
        CHAR16 ar_name[96];
        StrCpy(ar_name, g_cfg_autorun_file);
        if (g_djibion.mode != DJIBION_MODE_OFF) {
            char file8[128];
            llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), ar_name);
            DjibionDecision d;
            djibion_decide(&g_djibion, DJIBION_ACT_AUTORUN, file8, 0, &d);
            djibion_log_if_observe(&g_djibion, "autorun_autostart", &d);
            if (djibion_should_block(&g_djibion, &d)) {
                CHAR16 msg[160];
                ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                Print(L"[cfg] autorun autostart blocked by Djibion: %s\r\n", msg);
                goto autorun_autostart_done;
            }
            if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                Print(L"[cfg] autorun autostart path transformed by Djibion -> ");
                llmk_print_ascii(d.transformed_arg0);
                Print(L"\r\n");
                ascii_to_char16(ar_name, d.transformed_arg0, (int)(sizeof(ar_name) / sizeof(ar_name[0])));
            }
        }
        llmk_autorun_start(ar_name, g_cfg_autorun_shutdown_when_done);
    }

autorun_autostart_done:
    
    int conversation_count = 0;
    
    // KV cache position tracking (persistent across prompts for context retention)
    int kv_pos = 0;
    g_llmk_kv_pos = 0;

    // Optional snapshot auto-resume (repl.cfg): snap_autoload=1
    {
        int snap_autoload = 0;
        char snap_file_ascii[96];
        snap_file_ascii[0] = 0;
        llmk_load_repl_cfg_snap_best_effort(&snap_autoload, snap_file_ascii, (int)sizeof(snap_file_ascii));
        if (snap_autoload) {
            CHAR16 snap_file[96];
            if (snap_file_ascii[0]) {
                ascii_to_char16(snap_file, snap_file_ascii, (int)(sizeof(snap_file) / sizeof(snap_file[0])));
            } else {
                StrCpy(snap_file, L"llmk-snap.bin");
            }

            // Djibion gate for boot-time snapshot autoload.
            if (g_djibion.mode != DJIBION_MODE_OFF) {
                char file8[128];
                llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), snap_file);
                DjibionDecision d;
                djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, file8, 0, &d);
                djibion_log_if_observe(&g_djibion, "snap_autoload", &d);
                if (djibion_should_block(&g_djibion, &d)) {
                    CHAR16 msg[160];
                    ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                    Print(L"[cfg] snapshot autoload blocked by Djibion: %s\r\n", msg);
                    goto snap_autoload_done;
                }
                if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                    Print(L"[cfg] snapshot autoload path transformed by Djibion -> ");
                    llmk_print_ascii(d.transformed_arg0);
                    Print(L"\r\n");
                    ascii_to_char16(snap_file, d.transformed_arg0, (int)(sizeof(snap_file) / sizeof(snap_file[0])));
                }
            }

            EFI_STATUS st = llmk_snap_load_into_state_best_effort(&state, &config, &kv_pos, snap_file);
            if (EFI_ERROR(st)) {
                Print(L"[snap] autoload skipped (%r)\r\n", st);
                llmk_tr_note("SNAP: autoload failed");
            } else {
                Print(L"[snap] autoloaded %s (kv_pos=%d)\r\n", snap_file, kv_pos);
                llmk_tr_note("SNAP: autoloaded");
            }
        }
    }

snap_autoload_done:
    
    // MAIN LOOP
    while (1) {
        conversation_count++;

        // capture-mode state (per-turn)
        // capture_kind: 0=none, 1=/draw, 2=/oo_think, 3=/oo_auto, 4=/oo_exec
        int capture_kind = 0;
        int draw_mode = 0;
        int oo_think_id = 0;
        int oo_auto_planning = 0;
        int oo_auto_action_k = 0;
        int oo_exec_planning = 0;
        int oo_exec_action_k = 0;
        char oo_think_user[256];
        oo_think_user[0] = 0;
        int saved_stop_on_you = stop_on_you;
        int saved_stop_on_double_nl = stop_on_double_nl;
        int saved_max_gen_tokens = max_gen_tokens;
        int draw_saved_sampling = 0;
        float saved_temperature = temperature;
        float saved_min_p = min_p;
        float saved_top_p = top_p;
        int saved_top_k = top_k;
        float saved_repeat_penalty = repeat_penalty;
        char draw_user_prompt[256];
        draw_user_prompt[0] = 0;

        // Current turn prompt (either user input or synthesized for /oo_auto)
        char prompt[512];
        prompt[0] = 0;

        if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
            // Allow user to interrupt exec mode between cycles.
            {
                EFI_INPUT_KEY key;
                EFI_STATUS kst = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
                if (!EFI_ERROR(kst)) {
                    if (key.UnicodeChar == L'q' || key.UnicodeChar == L'Q' || key.ScanCode == SCAN_ESC) {
                        Print(L"\r\n[oo_exec] interrupted by user\r\n\r\n");
                        g_oo_exec_active = 0;
                        g_oo_exec_id = 0;
                        g_oo_exec_remaining = 0;
                        g_oo_exec_total = 0;
                        g_oo_exec_plan_if_empty = 0;
                        g_oo_exec_hint[0] = 0;
                    }
                }
            }
        }

        if (g_oo_auto_active && g_oo_auto_id > 0 && g_oo_auto_remaining > 0) {
            // Allow user to interrupt auto mode between cycles.
            // Note: we poll once per cycle; keypresses are consumed here.
            {
                EFI_INPUT_KEY key;
                EFI_STATUS kst = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
                if (!EFI_ERROR(kst)) {
                    if (key.UnicodeChar == L'q' || key.UnicodeChar == L'Q' || key.ScanCode == SCAN_ESC) {
                        Print(L"\r\n[oo_auto] interrupted by user\r\n\r\n");
                        g_oo_auto_active = 0;
                        g_oo_auto_id = 0;
                        g_oo_auto_remaining = 0;
                        g_oo_auto_total = 0;
                        g_oo_auto_user[0] = 0;
                    }
                }
            }
        }

        if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
            int cycle = (g_oo_exec_total - g_oo_exec_remaining) + 1;
            if (cycle < 1) cycle = 1;
            Print(L"\r\n[oo_exec] cycle %d/%d...\r\n", cycle, (g_oo_exec_total > 0) ? g_oo_exec_total : cycle);

            // Prefer consuming agenda items.
            char cycle_action[96];
            cycle_action[0] = 0;
            int picked_k = 0;

            // String passed into llmk_oo_build_think_prompt (can include hint + action).
            char cycle_user_build[256];
            cycle_user_build[0] = 0;

            if (llmk_oo_agenda_next_ex(g_oo_exec_id, &picked_k, cycle_action, (int)sizeof(cycle_action))) {
                oo_exec_planning = 0;
                oo_exec_action_k = picked_k;
                {
                    // Store just the action as the per-cycle "user" (notes + done marker).
                    int up = 0;
                    for (const char *s = cycle_action; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                    oo_think_user[up] = 0;
                }

                {
                    CHAR16 a16[120];
                    ascii_to_char16(a16, cycle_action, (int)(sizeof(a16) / sizeof(a16[0])));
                    Print(L"[oo_exec] action #%d: %s\r\n", picked_k, a16);
                }

                // Build prompt input: optional hint + action.
                if (g_oo_exec_hint[0]) {
                    int p = 0;
                    const char *h = g_oo_exec_hint;
                    for (int k = 0; h[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = h[k];
                    const char *mid = " | action: ";
                    for (int k = 0; mid[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = mid[k];
                    for (int k = 0; cycle_action[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = cycle_action[k];
                    cycle_user_build[p] = 0;
                } else {
                    int p = 0;
                    for (int k = 0; cycle_action[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = cycle_action[k];
                    cycle_user_build[p] = 0;
                }
            } else {
                if (!g_oo_exec_plan_if_empty) {
                    Print(L"[oo_exec] agenda empty -> stopping\r\n\r\n");
                    g_oo_exec_active = 0;
                    g_oo_exec_id = 0;
                    g_oo_exec_remaining = 0;
                    g_oo_exec_total = 0;
                    g_oo_exec_plan_if_empty = 0;
                    g_oo_exec_hint[0] = 0;
                } else {
                    // Planning cycle: propose one next action and push it.
                    oo_exec_planning = 1;
                    oo_exec_action_k = 0;
                    {
                        const char *plan = "Propose ONE next concrete action (single line, no bullets, no extra text).";
                        int up = 0;
                        for (const char *s = plan; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                        oo_think_user[up] = 0;
                    }
                    {
                        int p = 0;
                        for (int k = 0; oo_think_user[k] && p + 1 < (int)sizeof(cycle_user_build); k++) cycle_user_build[p++] = oo_think_user[k];
                        cycle_user_build[p] = 0;
                    }
                    Print(L"[oo_exec] agenda empty -> planning next action\r\n");
                }
            }

            if (g_oo_exec_active && g_oo_exec_id > 0 && g_oo_exec_remaining > 0) {
                char new_prompt[512];
                if (!llmk_oo_build_think_prompt(g_oo_exec_id, cycle_user_build, new_prompt, (int)sizeof(new_prompt))) {
                    Print(L"[oo_exec] ERROR: unknown entity id=%d (stopping)\r\n\r\n", g_oo_exec_id);
                    g_oo_exec_active = 0;
                    g_oo_exec_id = 0;
                    g_oo_exec_remaining = 0;
                    g_oo_exec_total = 0;
                    g_oo_exec_plan_if_empty = 0;
                    g_oo_exec_hint[0] = 0;
                } else {
                    // Keep model context clean per-cycle.
                    reset_kv_cache(&state, &config);
                    kv_pos = 0;
                    g_llmk_kv_pos = kv_pos;

                    for (int k = 0; k < (int)sizeof(prompt); k++) {
                        prompt[k] = new_prompt[k];
                        if (new_prompt[k] == 0) break;
                    }
                    oo_think_id = g_oo_exec_id;

                    g_capture_mode = 1;
                    capture_kind = 4; // /oo_exec cycle
                    llmk_capture_reset();
                    stop_on_you = 0;
                    stop_on_double_nl = 1;
                    if (max_gen_tokens > 64) max_gen_tokens = 64;
                }
            }
        }

        if (g_oo_auto_active && g_oo_auto_id > 0 && g_oo_auto_remaining > 0) {
            int cycle = (g_oo_auto_total - g_oo_auto_remaining) + 1;
            if (cycle < 1) cycle = 1;
            Print(L"\r\n[oo_auto] cycle %d/%d...\r\n", cycle, (g_oo_auto_total > 0) ? g_oo_auto_total : cycle);

            // Auto-consume agenda: if an action exists, select it and mark it DOING.
            // Falls back to the configured /oo_auto prompt when agenda is empty.
            const char *cycle_user = g_oo_auto_user;
            char cycle_action[96];
            cycle_action[0] = 0;
            int picked_k = 0;
            if (llmk_oo_agenda_next_ex(g_oo_auto_id, &picked_k, cycle_action, (int)sizeof(cycle_action))) {
                cycle_user = cycle_action;
                oo_auto_action_k = picked_k;
                {
                    CHAR16 a16[120];
                    ascii_to_char16(a16, cycle_action, (int)(sizeof(a16) / sizeof(a16[0])));
                    Print(L"[oo_auto] action #%d: %s\r\n", picked_k, a16);
                }
                oo_auto_planning = 0;
            } else {
                // No agenda to execute: ask the model for exactly one next action and push it.
                // This does NOT consume a cycle (remaining is unchanged).
                oo_auto_planning = 1;
                oo_auto_action_k = 0;
                cycle_user = "Propose ONE next concrete action (single line, no bullets, no extra text).";
                Print(L"[oo_auto] agenda empty -> planning next action\r\n");
            }

            char new_prompt[512];
            if (!llmk_oo_build_think_prompt(g_oo_auto_id, cycle_user, new_prompt, (int)sizeof(new_prompt))) {
                Print(L"[oo_auto] ERROR: unknown entity id=%d (stopping)\r\n\r\n", g_oo_auto_id);
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
            } else {
                // /oo_auto builds a fresh prompt every cycle; keep model context clean per-cycle.
                reset_kv_cache(&state, &config);
                kv_pos = 0;
                g_llmk_kv_pos = kv_pos;

                for (int k = 0; k < (int)sizeof(prompt); k++) {
                    prompt[k] = new_prompt[k];
                    if (new_prompt[k] == 0) break;
                }
                // Per-cycle state for capture handler
                oo_think_id = g_oo_auto_id;
                {
                    int up = 0;
                    if (cycle_user && cycle_user[0]) {
                        for (const char *s = cycle_user; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                    }
                    oo_think_user[up] = 0;
                }
                oo_think_user[(int)sizeof(oo_think_user) - 1] = 0;

                g_capture_mode = 1;
                capture_kind = 3; // /oo_auto cycle
                llmk_capture_reset();
                stop_on_you = 0;
                stop_on_double_nl = 1;
                if (max_gen_tokens > 64) max_gen_tokens = 64;
            }
        }

        if (prompt[0] == 0) {
            // Autorun: consume next scripted line instead of blocking for keyboard input.
            if (g_autorun_active) {
                if (llmk_autorun_next_line(prompt, (int)sizeof(prompt))) {
                    CHAR16 p16[540];
                    ascii_to_char16(p16, prompt, (int)(sizeof(p16) / sizeof(p16[0])));
                    Print(L"You (autorun): %s\r\n", p16);
                    llmk_tr_push_prefixed("AUTO: ", prompt);
                } else {
                    Print(L"[autorun] done\r\n");
                    int shutdown = g_autorun_shutdown_when_done;
                    llmk_autorun_stop();
                    if (shutdown) {
                        Print(L"[autorun] shutting down\r\n");
                        uefi_call_wrapper(RT->ResetSystem, 4, EfiResetShutdown, EFI_SUCCESS, 0, NULL);
                    }
                }
            }

            // Orchestrion: inject next pipeline step (non-blocking) when running.
            if (prompt[0] == 0 && g_orchestrion.mode != ORCHESTRION_MODE_OFF) {
                const char *step = orchestrion_pipeline_next_step(&g_orchestrion);
                if (step && step[0]) {
                    int i = 0;
                    for (; step[i] && i + 1 < (int)sizeof(prompt); i++) prompt[i] = step[i];
                    prompt[i] = 0;

                    CHAR16 p16[540];
                    ascii_to_char16(p16, prompt, (int)(sizeof(p16) / sizeof(p16[0])));
                    Print(L"You (orch): %s\r\n", p16);
                    llmk_tr_push_prefixed("ORCH: ", prompt);
                }
            }

            // Read user input
            if (prompt[0] == 0) {
                CHAR16 user_input[512];
                Print(L"You: ");
                read_user_input(user_input, 512);

                // Convert to char
                char16_to_char(prompt, user_input, 512);
                if (prompt[0]) llmk_tr_push_prefixed("YOU: ", prompt);
            }
        }

        // If GOP TUI is enabled, refresh it for every prompt/command.
        llmk_tui_on_prompt_best_effort(prompt);

        // Special command: /draw uses the model to generate render DSL, captures it, then runs /render.
        // It intentionally consumes context like any other prompt; use /clear if you want a clean slate.
        if (my_strncmp(prompt, "/draw", 5) == 0) {
            if (!g_gop_fb32) {
                Print(L"\r\nERROR: GOP not available (cannot draw)\r\n\r\n");
                continue;
            }

            const char *q = prompt + 5;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == 0) {
                Print(L"\r\nUsage: /draw <prompt>\r\n");
                Print(L"  Example: /draw a futuristic NOOSPHERE logo\r\n\r\n");
                continue;
            }

            // Immediate feedback: /draw can be slow under TCG, so show progress + clear screen.
            Print(L"\r\n[draw] generating DSL (may take a while under emulation) ...\r\n");
            llmk_gop_clear(0, 0, 0);
            llmk_gop_force_update();

            // Save the user's raw request for fallback rendering.
            {
                int up = 0;
                for (const char *s = q; *s && up + 1 < (int)sizeof(draw_user_prompt); s++) {
                    draw_user_prompt[up++] = *s;
                }
                draw_user_prompt[up] = 0;
            }

            // Build a compact instruction prompt.
            // Keep it short (prompt buffer is 512 bytes).
            // Use concrete examples to override narrative bias from stories model.
            char new_prompt[512];
            int p = 0;
            const char *prefix = "INSTRUCTION: Output render DSL code only. Format: clear R G B; rect X Y W H R G B; pixel X Y R G B; Example: clear 0 0 0; rect 100 100 50 50 255 255 255; END. Now:";
            for (const char *s = prefix; *s && p + 1 < (int)sizeof(new_prompt); s++) new_prompt[p++] = *s;
            if (p + 1 < (int)sizeof(new_prompt)) new_prompt[p++] = ' ';
            for (const char *s = q; *s && p + 1 < (int)sizeof(new_prompt); s++) new_prompt[p++] = *s;
            const char *suffix = " OUTPUT:";
            for (const char *s = suffix; *s && p + 1 < (int)sizeof(new_prompt); s++) new_prompt[p++] = *s;
            if (p + 2 < (int)sizeof(new_prompt)) { new_prompt[p++] = '\n'; new_prompt[p++] = 0; }
            else new_prompt[(int)sizeof(new_prompt) - 1] = 0;

            // Swap in the synthesized prompt for this turn.
            for (int i = 0; i < (int)sizeof(prompt); i++) {
                prompt[i] = new_prompt[i];
                if (new_prompt[i] == 0) break;
            }

            // Configure capture mode.
            draw_mode = 1;
            g_capture_mode = 1;
            capture_kind = 1;
            llmk_capture_reset();

            // Force deterministic sampling for /draw (TinyStories models tend to drift into prose).
            draw_saved_sampling = 1;
            saved_temperature = temperature;
            saved_min_p = min_p;
            saved_top_p = top_p;
            saved_top_k = top_k;
            saved_repeat_penalty = repeat_penalty;
            temperature = 0.0f;
            min_p = 0.0f;
            top_p = 0.0f;
            top_k = 1;
            repeat_penalty = 1.0f;

            // Prefer to stop on double newline in case END never appears.
            stop_on_you = 0;
            stop_on_double_nl = 1;
            if (max_gen_tokens > 48) max_gen_tokens = 48;
        }
        
        // Check for quit
        if (check_quit_command(prompt)) {
            Print(L"\r\n");
            Print(L"----------------------------------------\r\n");
            Print(L"  Goodbye! Had %d conversations.\r\n", conversation_count - 1);
            Print(L"----------------------------------------\r\n\r\n");
            break;
        }

        // UX: accept common commands even if user forgets the leading '/'.
        // Example: "oo_note 1 hello" becomes "/oo_note 1 hello".
        // Also: typing "reset" (no slash) should not accidentally trigger generation.
        if (!draw_mode && prompt[0] != '/') {
            const char *s = prompt;
            while (*s == ' ' || *s == '\t') s++;

            int do_rewrite = 0;
            const char *rewrite_from = s;

            // Accept OO commands with args.
            if (my_strncmp(s, "oo_", 3) == 0) {
                do_rewrite = 1;
            } else {
                // Accept a small whitelist of exact commands (no args), ignoring leading/trailing whitespace.
                const char *cmd = NULL;
                int cmd_len = 0;
                if (my_strncmp(s, "reset", 5) == 0) {
                    cmd = "reset";
                    cmd_len = 5;
                } else if (my_strncmp(s, "help", 4) == 0) {
                    cmd = "help";
                    cmd_len = 4;
                } else if (my_strncmp(s, "version", 7) == 0) {
                    cmd = "version";
                    cmd_len = 7;
                } else if (my_strncmp(s, "ctx", 3) == 0) {
                    cmd = "ctx";
                    cmd_len = 3;
                } else if (my_strncmp(s, "log", 3) == 0) {
                    cmd = "log";
                    cmd_len = 3;
                } else if (my_strncmp(s, "zones", 5) == 0) {
                    cmd = "zones";
                    cmd_len = 5;
                } else if (my_strncmp(s, "cpu", 3) == 0) {
                    cmd = "cpu";
                    cmd_len = 3;
                }

                if (cmd) {
                    const char *t = s + cmd_len;
                    while (*t == ' ' || *t == '\t') t++;
                    if (*t == 0) {
                        do_rewrite = 1;
                        rewrite_from = cmd; // normalize: drop extra whitespace
                    }
                }
            }

            if (do_rewrite) {
                char tmp2[512];
                int p2 = 0;
                tmp2[p2++] = '/';
                for (int i = 0; rewrite_from[i] && p2 + 1 < (int)sizeof(tmp2); i++) tmp2[p2++] = rewrite_from[i];
                tmp2[p2] = 0;

                for (int i = 0; i < (int)sizeof(prompt); i++) {
                    prompt[i] = tmp2[i];
                    if (tmp2[i] == 0) break;
                }
            }
        }
        
        // Check for commands (except /draw which is handled above and falls through into generation)
        if (!draw_mode && prompt[0] == '/') {
            if (my_strncmp(prompt, "/temp ", 6) == 0) {
                float val = 0.0f;
                int i = 6;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                temperature = val;
                Print(L"  Temperature set to: ");
                Print(L"%d.", (int)temperature);
                Print(L"%d\r\n", (int)((temperature - (int)temperature) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/min_p ", 7) == 0) {
                float val = 0.0f;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                min_p = val;
                Print(L"  Min-p set to: ");
                Print(L"%d.", (int)min_p);
                Print(L"%d\r\n", (int)((min_p - (int)min_p) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/top_p ", 7) == 0) {
                float val = 0.0f;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                top_p = val;
                Print(L"  Top-p set to: ");
                Print(L"%d.", (int)top_p);
                Print(L"%d\r\n", (int)((top_p - (int)top_p) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/top_k ", 7) == 0) {
                int val = 0;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                if (val < 0) val = 0;
                if (val > 256) val = 256;
                top_k = val;
                Print(L"  Top-k set to: %d\r\n", top_k);
                continue;
            } else if (my_strncmp(prompt, "/max_tokens ", 12) == 0) {
                int val = 0;
                int i = 12;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                if (val < 1) val = 1;
                if (val > MAX_TOKENS) val = MAX_TOKENS;
                max_gen_tokens = val;
                Print(L"  Max tokens set to: %d\r\n", max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/seed ", 6) == 0) {
                unsigned int val = 0;
                int i = 6;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10u + (unsigned int)(prompt[i] - '0');
                    i++;
                }
                set_seed(val);
                Print(L"  Seed set to: %d\r\n", (int)g_sample_seed);
                continue;
            } else if (my_strncmp(prompt, "/stats ", 7) == 0) {
                int val = 0;
                int i = 7;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                stats_enabled = (val != 0);
                Print(L"  Stats: %s\r\n", stats_enabled ? L"on" : L"off");
                continue;
            } else if (my_strncmp(prompt, "/stop_you ", 10) == 0) {
                int val = 0;
                int i = 10;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                stop_on_you = (val != 0);
                Print(L"  Stop on \\nYou:: %s\r\n", stop_on_you ? L"on" : L"off");
                continue;
            } else if (my_strncmp(prompt, "/stop_nl ", 9) == 0) {
                int val = 0;
                int i = 9;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                stop_on_double_nl = (val != 0);
                Print(L"  Stop on double newline: %s\r\n", stop_on_double_nl ? L"on" : L"off");
                continue;
            } else if (my_strncmp(prompt, "/norepeat ", 10) == 0) {
                int val = 0;
                int i = 10;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10 + (prompt[i] - '0');
                    i++;
                }
                if (val < 0) val = 0;
                if (val > 16) val = 16;
                no_repeat_ngram = val;
                Print(L"  No-repeat ngram set to: %d\r\n", no_repeat_ngram);
                continue;
            } else if (my_strncmp(prompt, "/repeat ", 8) == 0) {
                float val = 0.0f;
                int i = 8;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    val = val * 10.0f + (prompt[i] - '0');
                    i++;
                }
                if (prompt[i] == '.') {
                    i++;
                    float frac = 0.1f;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val += (prompt[i] - '0') * frac;
                        frac /= 10.0f;
                        i++;
                    }
                }
                repeat_penalty = val;
                Print(L"  Repetition penalty set to: ");
                Print(L"%d.", (int)repeat_penalty);
                Print(L"%d\r\n", (int)((repeat_penalty - (int)repeat_penalty) * 100.0f));
                continue;
            } else if (my_strncmp(prompt, "/sampling", 9) == 0) {
                Print(L"\r\nSampling:\r\n");
                Print(L"  temp=");
                Print(L"%d.", (int)temperature);
                Print(L"%d\r\n", (int)((temperature - (int)temperature) * 100.0f));
                Print(L"  min_p=");
                Print(L"%d.", (int)min_p);
                Print(L"%d\r\n", (int)((min_p - (int)min_p) * 100.0f));
                Print(L"  top_p=");
                Print(L"%d.", (int)top_p);
                Print(L"%d\r\n", (int)((top_p - (int)top_p) * 100.0f));
                Print(L"  top_k=%d\r\n", top_k);
                Print(L"  norepeat=%d\r\n", no_repeat_ngram);
                Print(L"  repeat=");
                Print(L"%d.", (int)repeat_penalty);
                Print(L"%d\r\n", (int)((repeat_penalty - (int)repeat_penalty) * 100.0f));
                Print(L"  max_tokens=%d\r\n\r\n", max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/preset_save", 12) == 0) {
                int i = 12;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    Print(L"\r\nUsage:\r\n");
                    Print(L"  /preset_save stable|creative|greedy\r\n");
                    Print(L"  (persists to repl.cfg; Djibion allow_cfg_write must allow it)\r\n\r\n");
                    continue;
                }

                char name[32];
                int n = 0;
                while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(name)) {
                    name[n++] = prompt[i++];
                }
                name[n] = 0;

                int applied = 1;
                // Also hold the canonical string values for cfg persistence.
                const char *cfg_temp = NULL;
                const char *cfg_min_p = NULL;
                const char *cfg_top_p = NULL;
                const char *cfg_top_k = NULL;
                const char *cfg_repeat = NULL;
                const char *cfg_norepeat = NULL;

                if (llmk_cfg_streq_ci(name, "stable")) {
                    temperature = 0.70f;
                    min_p = 0.05f;
                    top_p = 0.90f;
                    top_k = 40;
                    repeat_penalty = 1.10f;
                    no_repeat_ngram = 4;
                    cfg_temp = "0.70";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.90";
                    cfg_top_k = "40";
                    cfg_repeat = "1.10";
                    cfg_norepeat = "4";
                } else if (llmk_cfg_streq_ci(name, "creative")) {
                    temperature = 1.00f;
                    min_p = 0.05f;
                    top_p = 0.95f;
                    top_k = 80;
                    repeat_penalty = 1.05f;
                    no_repeat_ngram = 3;
                    cfg_temp = "1.00";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.95";
                    cfg_top_k = "80";
                    cfg_repeat = "1.05";
                    cfg_norepeat = "3";
                } else if (llmk_cfg_streq_ci(name, "greedy") || llmk_cfg_streq_ci(name, "det") || llmk_cfg_streq_ci(name, "deterministic")) {
                    temperature = 0.00f;
                    min_p = 0.00f;
                    top_p = 0.00f;
                    top_k = 0;
                    repeat_penalty = 1.00f;
                    no_repeat_ngram = 0;
                    cfg_temp = "0.00";
                    cfg_min_p = "0.00";
                    cfg_top_p = "0.00";
                    cfg_top_k = "0";
                    cfg_repeat = "1.00";
                    cfg_norepeat = "0";
                } else {
                    applied = 0;
                }

                if (!applied || !cfg_temp || !cfg_min_p || !cfg_top_p || !cfg_top_k || !cfg_repeat || !cfg_norepeat) {
                    Print(L"  Unknown preset: ");
                    llmk_print_ascii(name);
                    Print(L"\r\n  Try: /preset_save stable | /preset_save creative | /preset_save greedy\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "sampling_preset", (UINT32)my_strlen(name), &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/preset_save): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                int ok = 1;
                EFI_STATUS st;
                st = llmk_repl_cfg_set_kv_best_effort("temp", cfg_temp);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("min_p", cfg_min_p);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("top_p", cfg_top_p);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("top_k", cfg_top_k);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("repeat_penalty", cfg_repeat);
                if (EFI_ERROR(st)) ok = 0;
                st = llmk_repl_cfg_set_kv_best_effort("no_repeat_ngram", cfg_norepeat);
                if (EFI_ERROR(st)) ok = 0;

                Print(L"  Preset applied + saved: ");
                llmk_print_ascii(name);
                Print(L"\r\n");
                if (!ok) {
                    Print(L"  WARNING: repl.cfg update had errors (settings applied in RAM)\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/preset", 7) == 0) {
                int i = 7;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    Print(L"\r\nPresets:\r\n");
                    Print(L"  /preset stable           - temp=0.70 top_p=0.90 top_k=40 min_p=0.05 repeat=1.10 norepeat=4\r\n");
                    Print(L"  /preset creative         - temp=1.00 top_p=0.95 top_k=80 min_p=0.05 repeat=1.05 norepeat=3\r\n");
                    Print(L"  /preset greedy           - temp=0.00 top_p=0.00 top_k=0  min_p=0.00 repeat=1.00 norepeat=0\r\n");
                    Print(L"  /preset stable --save    - same but persists to repl.cfg\r\n");
                    Print(L"  /preset_save stable      - same as --save\r\n\r\n");
                    continue;
                }

                char name[32];
                int n = 0;
                while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(name)) {
                    name[n++] = prompt[i++];
                }
                name[n] = 0;

                // Optional: persist to repl.cfg.
                int save_cfg = 0;
                while (prompt[i] == ' ') i++;
                if (prompt[i]) {
                    // Accept: --save or -s
                    if (my_strncmp(prompt + i, "--save", 6) == 0) {
                        save_cfg = 1;
                    } else if (my_strncmp(prompt + i, "-s", 2) == 0) {
                        save_cfg = 1;
                    }
                }

                int applied = 1;
                const char *cfg_temp = NULL;
                const char *cfg_min_p = NULL;
                const char *cfg_top_p = NULL;
                const char *cfg_top_k = NULL;
                const char *cfg_repeat = NULL;
                const char *cfg_norepeat = NULL;
                if (llmk_cfg_streq_ci(name, "stable")) {
                    temperature = 0.70f;
                    min_p = 0.05f;
                    top_p = 0.90f;
                    top_k = 40;
                    repeat_penalty = 1.10f;
                    no_repeat_ngram = 4;
                    cfg_temp = "0.70";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.90";
                    cfg_top_k = "40";
                    cfg_repeat = "1.10";
                    cfg_norepeat = "4";
                } else if (llmk_cfg_streq_ci(name, "creative")) {
                    temperature = 1.00f;
                    min_p = 0.05f;
                    top_p = 0.95f;
                    top_k = 80;
                    repeat_penalty = 1.05f;
                    no_repeat_ngram = 3;
                    cfg_temp = "1.00";
                    cfg_min_p = "0.05";
                    cfg_top_p = "0.95";
                    cfg_top_k = "80";
                    cfg_repeat = "1.05";
                    cfg_norepeat = "3";
                } else if (llmk_cfg_streq_ci(name, "greedy") || llmk_cfg_streq_ci(name, "det") || llmk_cfg_streq_ci(name, "deterministic")) {
                    temperature = 0.00f;
                    min_p = 0.00f;
                    top_p = 0.00f;
                    top_k = 0;
                    repeat_penalty = 1.00f;
                    no_repeat_ngram = 0;
                    cfg_temp = "0.00";
                    cfg_min_p = "0.00";
                    cfg_top_p = "0.00";
                    cfg_top_k = "0";
                    cfg_repeat = "1.00";
                    cfg_norepeat = "0";
                } else {
                    applied = 0;
                }

                if (!applied) {
                    Print(L"  Unknown preset: ");
                    llmk_print_ascii(name);
                    Print(L"\r\n  Try: /preset stable | /preset creative | /preset greedy\r\n");
                    continue;
                }

                Print(L"  Preset applied: ");
                llmk_print_ascii(name);

                if (save_cfg) {
                    // Djibion gate (best-effort)
                    if (g_djibion.mode != DJIBION_MODE_OFF) {
                        DjibionDecision d;
                        djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "sampling_preset", (UINT32)my_strlen(name), &d);
                        djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                        if (djibion_should_block(&g_djibion, &d)) {
                            CHAR16 msg[160];
                            ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                            Print(L"\r\nDJIBION: blocked (--save): %s\r\n", msg);
                            Print(L"\r\n");
                            continue;
                        }
                    }

                    int ok = 1;
                    EFI_STATUS st;
                    if (cfg_temp) {
                        st = llmk_repl_cfg_set_kv_best_effort("temp", cfg_temp);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_min_p) {
                        st = llmk_repl_cfg_set_kv_best_effort("min_p", cfg_min_p);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_top_p) {
                        st = llmk_repl_cfg_set_kv_best_effort("top_p", cfg_top_p);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_top_k) {
                        st = llmk_repl_cfg_set_kv_best_effort("top_k", cfg_top_k);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_repeat) {
                        st = llmk_repl_cfg_set_kv_best_effort("repeat_penalty", cfg_repeat);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;
                    if (cfg_norepeat) {
                        st = llmk_repl_cfg_set_kv_best_effort("no_repeat_ngram", cfg_norepeat);
                        if (EFI_ERROR(st)) ok = 0;
                    } else ok = 0;

                    if (ok) {
                        Print(L" (saved)");
                    } else {
                        Print(L" (save failed)");
                    }
                }

                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/autostart_engines_on", 20) == 0) {
                // Usage: /autostart_engines_on [observe|enforce] [--run]
                const char *p = prompt + 20;
                while (*p == ' ' || *p == '\t') p++;

                int mode = 1; // observe
                int run_now = 0;

                // Parse tokens in any order: observe|enforce|1|2 and --run
                while (*p) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == 0) break;

                    char tok[24];
                    int tp = 0;
                    while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) tok[tp++] = *p++;
                    tok[tp] = 0;

                    if (llmk_cfg_streq_ci(tok, "enforce") || llmk_cfg_streq_ci(tok, "2")) {
                        mode = 2;
                    } else if (llmk_cfg_streq_ci(tok, "observe") || llmk_cfg_streq_ci(tok, "1")) {
                        mode = 1;
                    } else if (llmk_cfg_streq_ci(tok, "--run")) {
                        run_now = 1;
                    } else if (llmk_cfg_streq_ci(tok, "--help") || llmk_cfg_streq_ci(tok, "-h")) {
                        Print(L"\r\nUsage:\r\n");
                        Print(L"  /autostart_engines_on observe [--run]\r\n");
                        Print(L"  /autostart_engines_on enforce [--run]\r\n\r\n");
                        continue;
                    }
                }

                const char *mode_name = (mode == 2) ? "enforce" : "observe";

                // Generate llmk-autorun.txt in the boot volume root.
                // Keep it ASCII + CRLF, allow comments.
                char script[1024];
                int sp = 0;
                script[0] = 0;
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "# llmk-autorun.txt (generated by /autostart_engines_on)\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "# Mode: ");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), mode_name);
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "\r\n\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/version\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/compat_on\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/compat_probe\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/compat_status\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/djibion_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/djibion_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/djibion_enforce 1\r\n");
                }

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/mem_on\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diag_on\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_enforce 1\r\n");
                }
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/orch_status\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_enforce 1\r\n");
                }
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_status\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/calib_apply\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_on\r\n");
                if (mode == 2) {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_enforce 2\r\n");
                } else {
                    llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_enforce 1\r\n");
                }
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/diopion_status\r\n");

                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/preset stable\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/sampling\r\n");
                llmk_cfg_out_append(script, &sp, (int)sizeof(script), "/ctx\r\n");

                // Djibion gate for file write (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_WRITE, "llmk-autorun.txt", (UINT32)my_strlen(script), &d);
                    djibion_log_if_observe(&g_djibion, "fs_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (autorun script write): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                {
                    CHAR16 path[64];
                    StrCpy(path, L"llmk-autorun.txt");
                    EFI_FILE_HANDLE f = NULL;
                    EFI_STATUS st = llmk_open_binary_file(&f, path);
                    if (EFI_ERROR(st) || !f) {
                        Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                        continue;
                    }
                    UINTN nbytes = (UINTN)my_strlen(script);
                    EFI_STATUS wst = llmk_file_write_bytes(f, script, nbytes);
                    EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                    uefi_call_wrapper(f->Close, 1, f);
                    if (EFI_ERROR(wst)) {
                        Print(L"\r\nERROR: write failed: %r\r\n\r\n", wst);
                        continue;
                    }
                    if (EFI_ERROR(flush_st)) {
                        Print(L"\r\nWARNING: flush failed: %r (file may not persist)\r\n\r\n", flush_st);
                    }
                }

                // Djibion gate for cfg write (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "autorun_autostart", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (repl.cfg update): %s\r\n\r\n", msg);
                        Print(L"OK: wrote llmk-autorun.txt; enable autorun manually in repl.cfg\r\n\r\n");
                        continue;
                    }
                }

                // Persist autorun settings.
                {
                    EFI_STATUS st;
                    st = llmk_repl_cfg_set_kv_best_effort("autorun_autostart", "1");
                    if (EFI_ERROR(st)) {
                        Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                        continue;
                    }
                    llmk_repl_cfg_set_kv_best_effort("autorun_shutdown_when_done", "0");
                    llmk_repl_cfg_set_kv_best_effort("autorun_file", "llmk-autorun.txt");

                    // Ensure Djibion allows autorun when enabled via repl.cfg.
                    llmk_repl_cfg_set_kv_best_effort("djibion_allow_autorun", "1");
                    llmk_repl_cfg_set_kv_best_effort("djibion_mode", (mode == 2) ? "2" : "1");
                }

                Print(L"\r\nOK: engines autostart enabled (mode=");
                llmk_print_ascii(mode_name);
                Print(L"). Reboot to apply.\r\n");

                if (run_now) {
                    Print(L"[autostart] launching autorun now...\r\n\r\n");
                    int ok = llmk_autorun_start(L"llmk-autorun.txt", 0);
                    if (!ok) {
                        Print(L"\r\nERROR: autorun start failed\r\n\r\n");
                    }
                } else {
                    Print(L"\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/autostart_engines_off", 21) == 0) {
                // Disable autorun_autostart.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "autorun_autostart", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/autostart_engines_off): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("autorun_autostart", "0");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                    continue;
                }
                Print(L"\r\nOK: autorun_autostart=0 (reboot to apply)\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/model", 6) == 0 && (prompt[6] == 0 || prompt[6] == ' ')) {
                Print(L"\r\nModel:\r\n");
                Print(L"  %s\r\n", model_filename ? model_filename : L"(none)");
                Print(L"Config:\r\n");
                Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n\r\n",
                      config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                continue;
            } else if (my_strncmp(prompt, "/model_info", 11) == 0) {
                // Usage:
                //   /model_info           -> info for current loaded model path
                //   /model_info <path>    -> info for a specific file
                CHAR16 path16[192];
                path16[0] = 0;

                // Parse optional path argument from ASCII prompt.
                int i = 11;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[160];
                    int n = 0;
                    while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(p8)) {
                        p8[n++] = prompt[i++];
                    }
                    p8[n] = 0;
                    ascii_to_char16(path16, p8, (int)(sizeof(path16) / sizeof(path16[0])));
                } else {
                    // Default to last loaded model path (if known)
                    if (g_loaded_model_path16[0]) {
                        llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), g_loaded_model_path16);
                    } else if (model_filename) {
                        llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), model_filename);
                    } else {
                        StrCpy(path16, L"model.bin");
                    }
                }

                if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF &&
                    g_loaded_model_gguf_valid &&
                    llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                    llmk_print_gguf_summary_block(path16, &g_loaded_model_gguf);
                    Print(L"\r\nNOTE: GGUF inference is not wired yet; use .bin for generation today.\r\n\r\n");
                    continue;
                }

                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[192];
                picked[0] = 0;
                EFI_STATUS st = llmk_open_read_with_fat83_fallback(Root, path16, &f, picked,
                                                                  (int)(sizeof(picked) / sizeof(picked[0])),
                                                                  L"model_info");
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %s (%r)\r\n\r\n", path16, st);
                    continue;
                }

                // If we opened via an 8.3 alias, reflect it in the printed file path.
                if (picked[0]) {
                    llmk_char16_copy_cap(path16, (int)(sizeof(path16) / sizeof(path16[0])), picked);
                }

                LlmkModelFormat fmt = llmk_detect_model_format(f);
                if (fmt == LLMK_MODEL_FMT_GGUF) {
                    GgufSummary s;
                    EFI_STATUS gst = gguf_read_summary(f, &s);
                    uefi_call_wrapper(f->Close, 1, f);
                    if (EFI_ERROR(gst)) {
                        Print(L"\r\nGGUF: failed to parse (%r)\r\n\r\n", gst);
                        continue;
                    }

                    llmk_print_gguf_summary_block(path16, &s);
                    if (g_loaded_model_format == LLMK_MODEL_FMT_GGUF && llmk_char16_streq_ci(path16, g_loaded_model_path16)) {
                        g_loaded_model_gguf = s;
                        g_loaded_model_gguf_valid = 1;
                    }
                    Print(L"\r\nNOTE: GGUF inference is not wired yet; use .bin for generation today.\r\n\r\n");
                    continue;
                }

                // Default: treat as llama2.c .bin header (7 ints)
                Config c;
                EFI_STATUS pst = uefi_call_wrapper(f->SetPosition, 2, f, 0);
                if (EFI_ERROR(pst)) {
                    uefi_call_wrapper(f->Close, 1, f);
                    Print(L"\r\nERROR: seek failed (%r)\r\n\r\n", pst);
                    continue;
                }
                UINTN bytes = 7 * sizeof(int);
                EFI_STATUS rst = uefi_call_wrapper(f->Read, 3, f, &bytes, &c);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(rst) || bytes != 7 * sizeof(int)) {
                    Print(L"\r\nBIN: failed to read header (%r)\r\n\r\n", rst);
                    continue;
                }

                int shared = (c.vocab_size < 0);
                if (c.vocab_size < 0) c.vocab_size = -c.vocab_size;
                Print(L"\r\nBIN model info:\r\n");
                Print(L"  file=%s\r\n", path16);
                Print(L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d shared_cls=%d\r\n\r\n",
                      c.dim, c.n_layers, c.n_heads, c.n_kv_heads, c.vocab_size, c.seq_len, shared);
                continue;
            } else if (my_strncmp(prompt, "/models", 7) == 0) {
                // Usage:
                //   /models            -> list root + models\\
                //   /models <dir>      -> list .bin/.gguf in a directory (e.g. models, \\models)
                CHAR16 path[128];
                path[0] = 0;

                int i = 7;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    char p8[96];
                    int n = 0;
                    while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(p8)) {
                        p8[n++] = prompt[i++];
                    }
                    p8[n] = 0;
                    ascii_to_char16(path, p8, (int)(sizeof(path) / sizeof(path[0])));
                }

                Print(L"\r\nModels (.bin/.gguf):\r\n");
                if (path[0]) {
                    Print(L"Dir: %s\r\n", path);
                    llmk_models_ls_best_effort(path, 200);
                    Print(L"\r\n");
                } else {
                    Print(L"Root:\r\n");
                    llmk_models_ls_best_effort(NULL, 200);
                    Print(L"\r\nmodels\\:\r\n");
                    llmk_models_ls_best_effort(L"models", 200);
                    Print(L"\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/cpu", 4) == 0) {
                CPUFeatures f;
                djiblas_detect_cpu(&f);
                sgemm_kernel_t k = djiblas_get_best_kernel(&f);
                const CHAR16 *name = L"SCALAR";
                if (k == djiblas_sgemm_avx512) name = L"AVX512";
                else if (k == djiblas_sgemm_avx2) name = (f.has_fma ? L"AVX2+FMA" : L"AVX2");
                else if (k == djiblas_sgemm_sse2) name = L"SSE2";
                Print(L"\r\nCPU features:\r\n");
                Print(L"  sse2=%d avx=%d avx2=%d fma=%d\r\n", (int)f.has_sse2, (int)f.has_avx, (int)f.has_avx2, (int)f.has_fma);
                Print(L"  djiblas_sgemm=%s\r\n", name);
                const CHAR16 *attn = g_attn_use_avx2 ? L"AVX2" : L"SSE2";
                if (g_attn_force == 0) attn = L"SSE2 (forced)";
                else if (g_attn_force == 1) attn = L"AVX2 (forced)";
                Print(L"  attn_simd=%s\r\n\r\n", attn);
                continue;
            } else if (my_strncmp(prompt, "/zones", 6) == 0) {
                Print(L"\r\nZones:\r\n");
                if (g_llmk_ready) {
                    llmk_zones_print(&g_zones);
                    llmk_sentinel_print_status(&g_sentinel);
                    Print(L"\r\n");
                } else {
                    Print(L"  (llmk not ready)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/ram", 4) == 0) {
                llmk_print_ram_budget();
                continue;
            } else if (my_strncmp(prompt, "/budget", 7) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }

                // Usage:
                //   /budget                 -> show
                //   /budget <p>             -> set both prefill+decode
                //   /budget <p> <d>         -> set separately
                int i = 7;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    Print(L"\r\nBudgets (cycles):\r\n");
                    Print(L"  prefill_max=%lu\r\n", g_budget_prefill_cycles);
                    Print(L"  decode_max=%lu\r\n\r\n", g_budget_decode_cycles);
                    continue;
                }

                UINT64 pre = 0;
                UINT64 dec = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    pre = pre * 10ULL + (UINT64)(prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 0) {
                    dec = pre;
                } else {
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        dec = dec * 10ULL + (UINT64)(prompt[i] - '0');
                        i++;
                    }
                }

                g_budget_prefill_cycles = pre;
                g_budget_decode_cycles = dec;
                Print(L"\r\nBudgets set (cycles):\r\n");
                Print(L"  prefill_max=%lu\r\n", g_budget_prefill_cycles);
                Print(L"  decode_max=%lu\r\n\r\n", g_budget_decode_cycles);
                continue;
            } else if (my_strncmp(prompt, "/attn", 5) == 0) {
                // Usage:
                //   /attn          -> show
                //   /attn auto     -> runtime default
                //   /attn sse2     -> force SSE2 path
                //   /attn avx2     -> force AVX2 path (only if auto AVX2 is enabled)
                int i = 5;
                while (prompt[i] == ' ') i++;

                if (prompt[i] == 0) {
                    Print(L"\r\nAttention SIMD:\r\n");
                    Print(L"  auto=%s\r\n", g_attn_use_avx2 ? L"AVX2" : L"SSE2");
                    Print(L"  mode=%s\r\n\r\n",
                          (g_attn_force == -1) ? L"auto" : (g_attn_force == 0 ? L"sse2 (forced)" : L"avx2 (forced)"));
                    continue;
                }

                if (prompt[i] == 'a') {
                    g_attn_force = -1;
                    Print(L"\r\nOK: attn mode=auto\r\n\r\n");
                    continue;
                }
                if (prompt[i] == 's') {
                    g_attn_force = 0;
                    Print(L"\r\nOK: attn mode=sse2 (forced)\r\n\r\n");
                    continue;
                }
                if (prompt[i] == 'v') {
                    if (!g_attn_use_avx2) {
                        Print(L"\r\nERROR: AVX2 attention not available (auto is SSE2)\r\n\r\n");
                        continue;
                    }
                    g_attn_force = 1;
                    Print(L"\r\nOK: attn mode=avx2 (forced)\r\n\r\n");
                    continue;
                }

                Print(L"\r\nUsage: /attn [auto|sse2|avx2]\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/test_failsafe", 14) == 0) {
                // One-shot: temporarily enable strict budget and set tiny budgets so the next prompt trips.
                // Usage:
                //   /test_failsafe                -> decode trip with default cycles
                //   /test_failsafe prefill [c]    -> trip during prefill
                //   /test_failsafe decode [c]     -> trip during decode
                //   /test_failsafe both [c]       -> either phase can trip
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }

                UINT64 cycles = 10000ULL;
                int mode = 2; // 1=prefill, 2=decode, 3=both

                int i = 14;
                while (prompt[i] == ' ') i++;
                if (prompt[i] == 'p') mode = 1;
                else if (prompt[i] == 'd') mode = 2;
                else if (prompt[i] == 'b') mode = 3;

                // Skip word
                while (prompt[i] && prompt[i] != ' ') i++;
                while (prompt[i] == ' ') i++;

                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    cycles = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        cycles = cycles * 10ULL + (UINT64)(prompt[i] - '0');
                        i++;
                    }
                }
                if (cycles < 100ULL) cycles = 100ULL;

                if (!g_test_failsafe_active) {
                    g_test_failsafe_prev_strict_budget = g_sentinel.cfg.strict_budget;
                    g_test_failsafe_prev_prefill = g_budget_prefill_cycles;
                    g_test_failsafe_prev_decode = g_budget_decode_cycles;
                }
                g_test_failsafe_active = 1;
                g_sentinel.cfg.strict_budget = TRUE;

                const UINT64 huge = 100000000000ULL;
                if (mode == 1) {
                    g_budget_prefill_cycles = cycles;
                    g_budget_decode_cycles = huge;
                } else if (mode == 2) {
                    g_budget_prefill_cycles = huge;
                    g_budget_decode_cycles = cycles;
                } else {
                    g_budget_prefill_cycles = cycles;
                    g_budget_decode_cycles = cycles;
                }

                Print(L"\r\n[test] fail-safe armed (strict_budget=1)\r\n");
                Print(L"  prefill_max=%lu decode_max=%lu\r\n", g_budget_prefill_cycles, g_budget_decode_cycles);
                Print(L"  Next prompt should trip and auto-dump ctx/zones/sentinel/log.\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/cfg", 4) == 0) {
                llmk_print_cfg(&config, model_filename, &weights,
                               kv_pos,
                               temperature, min_p, top_p, top_k,
                               no_repeat_ngram, repeat_penalty, max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/ctx", 4) == 0) {
                llmk_print_ctx(&config, model_filename, kv_pos, temperature, min_p, top_p, top_k, no_repeat_ngram, repeat_penalty, max_gen_tokens);
                continue;
            } else if (my_strncmp(prompt, "/log", 4) == 0) {
                UINT32 n = 16;
                if (prompt[4] == ' ') {
                    int i = 5;
                    UINT32 val = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val = val * 10u + (UINT32)(prompt[i] - '0');
                        i++;
                    }
                    if (val > 0) n = val;
                    if (n > 128) n = 128;
                }
                llmk_print_log(n);
                continue;
            } else if (my_strncmp(prompt, "/save_log", 9) == 0) {
                if (!g_llmk_ready || !g_llmk_log.capacity) {
                    Print(L"\r\n  (log not available)\r\n\r\n");
                    continue;
                }

                UINT32 n = 64;
                if (prompt[9] == ' ') {
                    int i = 10;
                    UINT32 val = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val = val * 10u + (UINT32)(prompt[i] - '0');
                        i++;
                    }
                    if (val > 0) n = val;
                    if (n > 128) n = 128;
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, L"llmk-log.txt");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to open llmk-log.txt: %r\r\n\r\n", st);
                    continue;
                }
                llmk_dump_log_to_file(f, &g_llmk_log, n);
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote llmk-log.txt (flushed)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/save_dump", 10) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, L"llmk-dump.txt");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to open llmk-dump.txt: %r\r\n\r\n", st);
                    continue;
                }

                // Minimal ctx dump (match /ctx, in UTF-16)
                {
                    CHAR16 line[256];
                    llmk_file_write_u16(f, L"Context:\r\n");
                          SPrint(line, sizeof(line), L"  model=%s\r\n", model_filename ? model_filename : L"(unknown)");
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
                           config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                    llmk_file_write_u16(f, line);
                          SPrint(line, sizeof(line), L"  kv_pos=%d\r\n", kv_pos);
                          llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"Sampling:\r\n");
                    SPrint(line, sizeof(line), L"  temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
                           (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
                           (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
                           (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
                           top_k);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  norepeat=%d repeat_penalty=%d.%02d max_tokens=%d\r\n",
                           no_repeat_ngram,
                           (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
                           max_gen_tokens);
                    llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"Budgets:\r\n");
                    SPrint(line, sizeof(line), L"  prefill_max=%lu decode_max=%lu overruns(p=%d d=%d)\r\n\r\n",
                           g_budget_prefill_cycles, g_budget_decode_cycles,
                           (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
                    llmk_file_write_u16(f, line);
                }

                llmk_dump_zones_to_file(f, &g_zones);
                llmk_dump_sentinel_to_file(f, &g_sentinel);
                if (g_llmk_log.capacity) {
                    llmk_dump_log_to_file(f, &g_llmk_log, 128);
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote llmk-dump.txt (flushed)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/diag_on", 8) == 0) {
                diagnostion_set_mode(&g_diagnostion, DIAGNOSTION_MODE_ON);
                Print(L"\r\nOK: diagnostion=on\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diag_off", 9) == 0) {
                diagnostion_set_mode(&g_diagnostion, DIAGNOSTION_MODE_OFF);
                Print(L"\r\nOK: diagnostion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diag_status", 11) == 0) {
                Print(L"\r\n[Diagnostion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(diagnostion_mode_name_ascii(g_diagnostion.mode));
                Print(L"\r\n");
                Print(L"  reports_written=%d\r\n\r\n", (int)g_diagnostion.reports_written);
                continue;
            } else if (my_strncmp(prompt, "/diag_report", 11) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (g_diagnostion.mode == DIAGNOSTION_MODE_OFF) {
                    Print(L"\r\nERROR: Diagnostion is off (use /diag_on)\r\n\r\n");
                    continue;
                }

                // Optional: /diag_report <file>
                char out_name8[96];
                out_name8[0] = 0;
                {
                    const char *p = prompt + 11;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p) {
                        int n = 0;
                        while (*p && *p != ' ' && *p != '\t' && n + 1 < (int)sizeof(out_name8)) {
                            out_name8[n++] = *p++;
                        }
                        out_name8[n] = 0;
                    }
                }

                CHAR16 out_name16[96];
                if (out_name8[0]) {
                    ascii_to_char16(out_name16, out_name8, (int)(sizeof(out_name16) / sizeof(out_name16[0])));
                } else {
                    StrCpy(out_name16, L"llmk-diag.txt");
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, out_name16);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to open diag file: %r\r\n\r\n", st);
                    continue;
                }

                // Human-friendly report header (UTF-16)
                {
                    CHAR16 line[256];
                    llmk_file_write_u16(f, L"LLMK DIAGNOSTIC REPORT\r\n\r\n");
                    SPrint(line, sizeof(line), L"  model=%s\r\n", model_filename ? model_filename : L"(unknown)");
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
                           config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  kv_pos=%d\r\n", kv_pos);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  budgets: prefill_max=%lu decode_max=%lu overruns(p=%d d=%d)\r\n",
                           g_budget_prefill_cycles, g_budget_decode_cycles,
                           (int)g_budget_overruns_prefill, (int)g_budget_overruns_decode);
                    llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"\r\nEngines:\r\n");
                    SPrint(line, sizeof(line), L"  djibion_mode=%s decisions=%d rejected=%d transformed=%d\r\n",
                           (CHAR16 *)djibion_mode_name(g_djibion.mode),
                           (int)g_djibion.decisions_total,
                           (int)g_djibion.decisions_rejected,
                           (int)g_djibion.decisions_transformed);
                    llmk_file_write_u16(f, line);
                    llmk_file_write_u16(f, L"  diopion_mode=");
                    llmk_file_write_u16(f, L"\"");
                    {
                        // diopion_mode_name_ascii returns ASCII; print it char-by-char into UTF-16 file.
                        CHAR16 m[32];
                        ascii_to_char16(m, diopion_mode_name_ascii(g_diopion.mode), (int)(sizeof(m) / sizeof(m[0])));
                        llmk_file_write_u16(f, m);
                    }
                    llmk_file_write_u16(f, L"\" profile=\"");
                    {
                        CHAR16 p[32];
                        ascii_to_char16(p, diopion_profile_name_ascii(g_diopion.profile), (int)(sizeof(p) / sizeof(p[0])));
                        llmk_file_write_u16(f, p);
                    }
                    llmk_file_write_u16(f, L"\"\r\n\r\n");

                    llmk_file_write_u16(f, L"Sampling:\r\n");
                    SPrint(line, sizeof(line), L"  temp=%d.%02d min_p=%d.%02d top_p=%d.%02d top_k=%d\r\n",
                           (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
                           (int)min_p, (int)((min_p - (int)min_p) * 100.0f),
                           (int)top_p, (int)((top_p - (int)top_p) * 100.0f),
                           top_k);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  norepeat=%d repeat_penalty=%d.%02d max_tokens=%d\r\n\r\n",
                           no_repeat_ngram,
                           (int)repeat_penalty, (int)((repeat_penalty - (int)repeat_penalty) * 100.0f),
                           max_gen_tokens);
                    llmk_file_write_u16(f, line);
                }

                // Deep dumps (same building blocks as /save_dump)
                llmk_dump_zones_to_file(f, &g_zones);
                llmk_dump_sentinel_to_file(f, &g_sentinel);
                if (g_llmk_log.capacity) {
                    llmk_dump_log_to_file(f, &g_llmk_log, 128);
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    g_diagnostion.reports_written++;
                    Print(L"\r\nOK: wrote %s (flushed)\r\n\r\n", out_name16);
                }
                continue;
            } else if (my_strncmp(prompt, "/mem_on", 7) == 0) {
                memorion_set_mode(&g_memorion, MEMORION_MODE_ON);
                Print(L"\r\nOK: memorion=on\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/mem_off", 8) == 0) {
                memorion_set_mode(&g_memorion, MEMORION_MODE_OFF);
                Print(L"\r\nOK: memorion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/mem_status", 10) == 0) {
                Print(L"\r\n[Memorion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(memorion_mode_name_ascii(g_memorion.mode));
                Print(L"\r\n");
                Print(L"  manifests_written=%d\r\n", (int)g_memorion.manifests_written);
                Print(L"  checks_done=%d\r\n\r\n", (int)g_memorion.checks_done);
                continue;
            } else if (my_strncmp(prompt, "/mem_snap_info", 14) == 0 || my_strncmp(prompt, "/mem_snap_check", 15) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (g_memorion.mode == MEMORION_MODE_OFF) {
                    Print(L"\r\nERROR: Memorion is off (use /mem_on)\r\n\r\n");
                    continue;
                }

                int is_check = (my_strncmp(prompt, "/mem_snap_check", 15) == 0);
                int cmd_len = is_check ? 15 : 14;
                const char *p = prompt + cmd_len;
                while (*p == ' ' || *p == '\t') p++;

                char snap8[96];
                snap8[0] = 0;
                if (*p) {
                    int n = 0;
                    while (*p && *p != ' ' && *p != '\t' && n + 1 < (int)sizeof(snap8)) {
                        snap8[n++] = *p++;
                    }
                    snap8[n] = 0;
                }
                if (snap8[0] == 0) {
                    llmk_ascii_copy_cap(snap8, (int)sizeof(snap8), "llmk-snap.bin");
                }

                if (llmk_ascii_has_dotdot(snap8)) {
                    Print(L"\r\nERROR: path contains '..'\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort): treat as a snapshot load-like read.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, snap8, 0, &d);
                    djibion_log_if_observe(&g_djibion, is_check ? "mem_snap_check" : "mem_snap_info", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (%s): %s\r\n\r\n", is_check ? L"/mem_snap_check" : L"/mem_snap_info", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] snap path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        llmk_ascii_copy_cap(snap8, (int)sizeof(snap8), d.transformed_arg0);
                    }
                }

                CHAR16 snap16[96];
                ascii_to_char16(snap16, snap8, (int)(sizeof(snap16) / sizeof(snap16[0])));

                EFI_FILE_HANDLE f = NULL;
                CHAR16 picked[96];
                picked[0] = 0;
                EFI_STATUS st = llmk_open_read_with_fat83_fallback(g_root, snap16, &f, picked,
                                                                  (int)(sizeof(picked) / sizeof(picked[0])),
                                                                  is_check ? L"mem_snap_check" : L"mem_snap_info");
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }

                if (picked[0]) {
                    llmk_char16_copy_cap(snap16, (int)(sizeof(snap16) / sizeof(snap16[0])), picked);
                }

                LlmkSnapHeader hdr;
                st = read_exact(f, &hdr, sizeof(hdr));
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: read failed: %r\r\n\r\n", st);
                    continue;
                }
                if (hdr.magic != LLMK_SNAP_MAGIC || hdr.version != 1) {
                    Print(L"\r\nERROR: invalid snapshot header (magic/version)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[Snapshot]\r\n");
                Print(L"  file=%s\r\n", snap16);
                Print(L"  dim=%d layers=%d heads=%d kv=%d seq=%d\r\n", (int)hdr.dim, (int)hdr.n_layers, (int)hdr.n_heads, (int)hdr.n_kv_heads, (int)hdr.seq_len);
                Print(L"  kv_dim=%d kv_pos=%d\r\n", (int)hdr.kv_dim, (int)hdr.kv_pos);
                {
                    UINTN slice_bytes = (UINTN)hdr.kv_pos * (UINTN)hdr.kv_dim * sizeof(float);
                    UINTN total = sizeof(LlmkSnapHeader) + (UINTN)hdr.n_layers * 2u * slice_bytes;
                    Print(L"  approx_bytes=%lu\r\n", (UINT64)total);
                }

                if (is_check) {
                    int ok = 1;
                    if (hdr.dim != (UINT32)config.dim) ok = 0;
                    if (hdr.n_layers != (UINT32)config.n_layers) ok = 0;
                    if (hdr.n_heads != (UINT32)config.n_heads) ok = 0;
                    if (hdr.n_kv_heads != (UINT32)config.n_kv_heads) ok = 0;
                    if (hdr.seq_len != (UINT32)config.seq_len) ok = 0;
                    if (hdr.kv_pos == 0 || hdr.kv_pos > (UINT32)config.seq_len) ok = 0;
                    Print(L"  compatible=%s\r\n\r\n", ok ? L"yes" : L"NO");
                    g_memorion.checks_done++;
                } else {
                    Print(L"\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/mem_manifest", 13) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (g_memorion.mode == MEMORION_MODE_OFF) {
                    Print(L"\r\nERROR: Memorion is off (use /mem_on)\r\n\r\n");
                    continue;
                }

                // Usage:
                //   /mem_manifest                 -> write current context manifest to llmk-manifest.txt
                //   /mem_manifest <snap>          -> include snapshot header, write llmk-manifest.txt
                //   /mem_manifest <snap> <out>    -> include snapshot header, write <out>
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;

                char snap8[96];
                char out8[96];
                snap8[0] = 0;
                out8[0] = 0;

                if (*p) {
                    int n = 0;
                    while (*p && *p != ' ' && *p != '\t' && n + 1 < (int)sizeof(snap8)) snap8[n++] = *p++;
                    snap8[n] = 0;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p) {
                        int m = 0;
                        while (*p && *p != ' ' && *p != '\t' && m + 1 < (int)sizeof(out8)) out8[m++] = *p++;
                        out8[m] = 0;
                    }
                }

                if (snap8[0] && llmk_ascii_has_dotdot(snap8)) {
                    Print(L"\r\nERROR: snap path contains '..'\r\n\r\n");
                    continue;
                }
                if (out8[0] && llmk_ascii_has_dotdot(out8)) {
                    Print(L"\r\nERROR: out path contains '..'\r\n\r\n");
                    continue;
                }

                CHAR16 out16[96];
                if (out8[0]) {
                    ascii_to_char16(out16, out8, (int)(sizeof(out16) / sizeof(out16[0])));
                } else {
                    StrCpy(out16, L"llmk-manifest.txt");
                }

                // Djibion gate (best-effort): writing a file.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char out_file8[128];
                    llmk_char16_to_ascii_cap(out_file8, (int)sizeof(out_file8), out16);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_WRITE, out_file8, 4096u, &d);
                    djibion_log_if_observe(&g_djibion, "mem_manifest", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/mem_manifest): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] manifest path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(out16, d.transformed_arg0, (int)(sizeof(out16) / sizeof(out16[0])));
                    }
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, out16);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }

                LlmkSnapHeader hdr;
                int have_hdr = 0;
                int compat = 0;
                if (snap8[0]) {
                    // Djibion gate (best-effort): read snapshot.
                    if (g_djibion.mode != DJIBION_MODE_OFF) {
                        DjibionDecision d;
                        djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, snap8, 0, &d);
                        djibion_log_if_observe(&g_djibion, "mem_manifest_snap", &d);
                        if (djibion_should_block(&g_djibion, &d)) {
                            CHAR16 msg[160];
                            ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                            Print(L"\r\nDJIBION: blocked (snap read): %s\r\n\r\n", msg);
                            uefi_call_wrapper(f->Close, 1, f);
                            continue;
                        }
                        if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                            llmk_ascii_copy_cap(snap8, (int)sizeof(snap8), d.transformed_arg0);
                        }
                    }

                    CHAR16 snap16[96];
                    ascii_to_char16(snap16, snap8, (int)(sizeof(snap16) / sizeof(snap16[0])));
                    EFI_FILE_HANDLE rf = NULL;
                    st = llmk_open_read_file(&rf, snap16);
                    if (!EFI_ERROR(st) && rf) {
                        EFI_STATUS st2 = read_exact(rf, &hdr, sizeof(hdr));
                        uefi_call_wrapper(rf->Close, 1, rf);
                        if (!EFI_ERROR(st2) && hdr.magic == LLMK_SNAP_MAGIC && hdr.version == 1) {
                            have_hdr = 1;
                            compat = 1;
                            if (hdr.dim != (UINT32)config.dim) compat = 0;
                            if (hdr.n_layers != (UINT32)config.n_layers) compat = 0;
                            if (hdr.n_heads != (UINT32)config.n_heads) compat = 0;
                            if (hdr.n_kv_heads != (UINT32)config.n_kv_heads) compat = 0;
                            if (hdr.seq_len != (UINT32)config.seq_len) compat = 0;
                            if (hdr.kv_pos == 0 || hdr.kv_pos > (UINT32)config.seq_len) compat = 0;
                        }
                    }
                }

                {
                    CHAR16 line[256];
                    UINT32 h = llmk_memorion_ctx_hash32(&config, model_filename);
                    llmk_file_write_u16(f, L"LLMK MEMORION MANIFEST\r\n\r\n");
                    SPrint(line, sizeof(line), L"  model=%s\r\n", model_filename ? model_filename : L"(unknown)");
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d vocab=%d seq=%d\r\n",
                           config.dim, config.n_layers, config.n_heads, config.n_kv_heads, config.vocab_size, config.seq_len);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  kv_pos=%d\r\n", kv_pos);
                    llmk_file_write_u16(f, line);
                    SPrint(line, sizeof(line), L"  ctx_hash32=0x%08x\r\n\r\n", h);
                    llmk_file_write_u16(f, line);

                    if (snap8[0]) {
                        llmk_file_write_u16(f, L"Snapshot:\r\n");
                        {
                            CHAR16 snap16[96];
                            ascii_to_char16(snap16, snap8, (int)(sizeof(snap16) / sizeof(snap16[0])));
                            SPrint(line, sizeof(line), L"  file=%s\r\n", snap16);
                            llmk_file_write_u16(f, line);
                        }
                        if (have_hdr) {
                            SPrint(line, sizeof(line), L"  dim=%d layers=%d heads=%d kv=%d seq=%d\r\n",
                                   (int)hdr.dim, (int)hdr.n_layers, (int)hdr.n_heads, (int)hdr.n_kv_heads, (int)hdr.seq_len);
                            llmk_file_write_u16(f, line);
                            SPrint(line, sizeof(line), L"  kv_dim=%d kv_pos=%d\r\n",
                                   (int)hdr.kv_dim, (int)hdr.kv_pos);
                            llmk_file_write_u16(f, line);
                            SPrint(line, sizeof(line), L"  compatible=%s\r\n\r\n", compat ? L"yes" : L"NO");
                            llmk_file_write_u16(f, line);
                        } else {
                            llmk_file_write_u16(f, L"  (could not read valid header)\r\n\r\n");
                        }
                    }
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed %r (file may not persist)\r\n\r\n", flush_st);
                } else {
                    g_memorion.manifests_written++;
                    Print(L"\r\nOK: wrote %s (flushed)\r\n\r\n", out16);
                }
                continue;

            // ==============================================================
            // ORCHESTRION commands
            // ==============================================================
            } else if (my_strncmp(prompt, "/orch_on", 8) == 0) {
                orchestrion_set_mode(&g_orchestrion, ORCHESTRION_MODE_OBSERVE);
                Print(L"\r\nOK: orchestrion=observe\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_off", 9) == 0) {
                orchestrion_set_mode(&g_orchestrion, ORCHESTRION_MODE_OFF);
                Print(L"\r\nOK: orchestrion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_enforce", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ') p++;
                int v = 2;
                if (*p >= '0' && *p <= '2') v = *p - '0';
                orchestrion_set_mode(&g_orchestrion, (OrchestrionMode)v);
                Print(L"\r\nOK: orchestrion_mode=%d\r\n\r\n", v);
                continue;
            } else if (my_strncmp(prompt, "/orch_status", 12) == 0) {
                Print(L"\r\n[Orchestrion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(orchestrion_mode_name_ascii(g_orchestrion.mode));
                Print(L"\r\n");
                Print(L"  state=");
                llmk_print_ascii(orchestrion_state_name_ascii(g_orchestrion.pipeline.state));
                Print(L"\r\n");
                Print(L"  steps=%d current=%d loops=%d/%d\r\n",
                      (int)g_orchestrion.pipeline.step_count,
                      (int)g_orchestrion.pipeline.current_step,
                      (int)g_orchestrion.pipeline.loops_done,
                      (int)g_orchestrion.pipeline.loops_max);
                Print(L"  workflows_run=%d steps_executed=%d errors=%d\r\n\r\n",
                      (int)g_orchestrion.workflows_run,
                      (int)g_orchestrion.steps_executed,
                      (int)g_orchestrion.errors);
                continue;
            } else if (my_strncmp(prompt, "/orch_clear", 11) == 0) {
                orchestrion_pipeline_clear(&g_orchestrion);
                Print(L"\r\nOK: pipeline cleared\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_add", 9) == 0) {
                const char *p = prompt + 9;
                while (*p == ' ') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /orch_add <step> [; <step2> ...]\r\n\r\n");
                    continue;
                }
                int added = 0;
                while (*p) {
                    char step[ORCHESTRION_STEP_LEN];
                    int n = 0;
                    while (*p && *p != ';' && n + 1 < ORCHESTRION_STEP_LEN) {
                        step[n++] = *p++;
                    }
                    step[n] = 0;
                    // Trim trailing spaces
                    while (n > 0 && (step[n-1] == ' ' || step[n-1] == '\t')) step[--n] = 0;
                    // Trim leading spaces
                    char *s = step;
                    while (*s == ' ' || *s == '\t') s++;
                    if (*s) {
                        if (orchestrion_pipeline_add_step(&g_orchestrion, s)) added++;
                    }
                    if (*p == ';') p++;
                    while (*p == ' ' || *p == '\t') p++;
                }
                Print(L"\r\nOK: added %d step(s), total=%d\r\n\r\n", added, (int)g_orchestrion.pipeline.step_count);
                continue;
            } else if (my_strncmp(prompt, "/orch_start", 11) == 0) {
                const char *p = prompt + 11;
                while (*p == ' ') p++;
                uint32_t loops = 1;
                if (*p >= '0' && *p <= '9') {
                    loops = 0;
                    while (*p >= '0' && *p <= '9') loops = loops * 10 + (*p++ - '0');
                }
                if (orchestrion_pipeline_start(&g_orchestrion, loops)) {
                    Print(L"\r\nOK: pipeline started (loops=%d)\r\n\r\n", (int)loops);
                } else {
                    Print(L"\r\nERROR: cannot start (no steps?)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/orch_pause", 11) == 0) {
                orchestrion_pipeline_pause(&g_orchestrion);
                Print(L"\r\nOK: pipeline paused\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_resume", 12) == 0) {
                orchestrion_pipeline_resume(&g_orchestrion);
                Print(L"\r\nOK: pipeline resumed\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/orch_stop", 10) == 0) {
                orchestrion_pipeline_stop(&g_orchestrion);
                Print(L"\r\nOK: pipeline stopped\r\n\r\n");
                continue;

            // ==============================================================
            // CALIBRION commands
            // ==============================================================
            } else if (my_strncmp(prompt, "/calib_on", 9) == 0) {
                calibrion_set_mode(&g_calibrion, CALIBRION_MODE_OBSERVE);
                Print(L"\r\nOK: calibrion=observe\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_off", 10) == 0) {
                calibrion_set_mode(&g_calibrion, CALIBRION_MODE_OFF);
                Print(L"\r\nOK: calibrion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_enforce", 14) == 0) {
                const char *p = prompt + 14;
                while (*p == ' ') p++;
                int v = 2;
                if (*p >= '0' && *p <= '2') v = *p - '0';
                calibrion_set_mode(&g_calibrion, (CalibrionMode)v);
                Print(L"\r\nOK: calibrion_mode=%d\r\n\r\n", v);
                continue;
            } else if (my_strncmp(prompt, "/calib_strategy", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ') p++;
                CalibrionStrategy s = CALIBRION_STRATEGY_NONE;
                if (my_strncmp(p, "entropy", 7) == 0) s = CALIBRION_STRATEGY_ENTROPY;
                else if (my_strncmp(p, "length", 6) == 0) s = CALIBRION_STRATEGY_LENGTH;
                else if (my_strncmp(p, "quality", 7) == 0) s = CALIBRION_STRATEGY_QUALITY;
                else if (my_strncmp(p, "hybrid", 6) == 0) s = CALIBRION_STRATEGY_HYBRID;
                calibrion_set_strategy(&g_calibrion, s);
                Print(L"\r\nOK: calibrion_strategy=");
                llmk_print_ascii(calibrion_strategy_name_ascii(s));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_status", 13) == 0) {
                Print(L"\r\n[Calibrion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(calibrion_mode_name_ascii(g_calibrion.mode));
                Print(L"\r\n");
                Print(L"  strategy=");
                llmk_print_ascii(calibrion_strategy_name_ascii(g_calibrion.strategy));
                Print(L"\r\n");
                Print(L"  samples=%d total_tokens=%d repeats=%d\r\n",
                      (int)g_calibrion.stats.samples,
                      (int)g_calibrion.stats.total_tokens,
                      (int)g_calibrion.stats.total_repeats);
                Print(L"  short=%d long=%d avg_entropy_milli=%d\r\n",
                      (int)g_calibrion.stats.short_responses,
                      (int)g_calibrion.stats.long_responses,
                      (int)g_calibrion.stats.avg_entropy_milli);
                Print(L"  rec: temp=%d.%02d top_k=%d top_p=%d.%02d\r\n",
                      (int)(g_calibrion.rec_temp_milli / 1000),
                      (int)((g_calibrion.rec_temp_milli % 1000) / 10),
                      (int)g_calibrion.rec_top_k,
                      (int)(g_calibrion.rec_top_p_milli / 1000),
                      (int)((g_calibrion.rec_top_p_milli % 1000) / 10));
                Print(L"  calibrations_done=%d\r\n\r\n", (int)g_calibrion.calibrations_done);
                continue;
            } else if (my_strncmp(prompt, "/calib_reset", 12) == 0) {
                calibrion_reset_stats(&g_calibrion);
                Print(L"\r\nOK: calibrion stats reset\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/calib_apply", 12) == 0) {
                uint32_t t, k, p;
                calibrion_get_recommendation(&g_calibrion, &t, &k, &p);
                temperature = (float)t / 1000.0f;
                top_k = (int)k;
                top_p = (float)p / 1000.0f;
                Print(L"\r\nOK: applied temp=%d.%02d top_k=%d top_p=%d.%02d\r\n\r\n",
                      (int)temperature, (int)((temperature - (int)temperature) * 100.0f),
                      top_k,
                      (int)top_p, (int)((top_p - (int)top_p) * 100.0f));
                continue;

            // ==============================================================
            // COMPATIBILION commands
            // ==============================================================
            } else if (my_strncmp(prompt, "/compat_on", 10) == 0) {
                compatibilion_set_mode(&g_compatibilion, COMPATIBILION_MODE_ON);
                Print(L"\r\nOK: compatibilion=on\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/compat_off", 11) == 0) {
                compatibilion_set_mode(&g_compatibilion, COMPATIBILION_MODE_OFF);
                Print(L"\r\nOK: compatibilion=off\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/compat_status", 14) == 0) {
                Print(L"\r\n[Compatibilion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(compatibilion_mode_name_ascii(g_compatibilion.mode));
                Print(L"\r\n");
                Print(L"  cpu_vendor=");
                llmk_print_ascii(g_compatibilion.caps.cpu_vendor);
                Print(L"\r\n");
                Print(L"  cpu_brand=");
                llmk_print_ascii(g_compatibilion.caps.cpu_brand);
                Print(L"\r\n");
                Print(L"  cpu_flags=0x%x (SSE2=%d AVX=%d AVX2=%d FMA=%d)\r\n",
                      (unsigned)g_compatibilion.caps.cpu_flags,
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_SSE2),
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_AVX),
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_AVX2),
                      compatibilion_has_cpu(&g_compatibilion, COMPAT_CPU_FMA));
                Print(L"  platform_flags=0x%x (UEFI=%d GOP=%d FAT32=%d)\r\n",
                      (unsigned)g_compatibilion.caps.platform_flags,
                      compatibilion_has_platform(&g_compatibilion, COMPAT_PLAT_UEFI),
                      compatibilion_has_platform(&g_compatibilion, COMPAT_PLAT_GOP),
                      compatibilion_has_platform(&g_compatibilion, COMPAT_PLAT_FAT32));
                Print(L"  mem_tier=");
                llmk_print_ascii(compatibilion_mem_tier_name_ascii(g_compatibilion.caps.mem_tier));
                Print(L" (%lu bytes)\r\n", g_compatibilion.caps.mem_bytes);
                if (g_compatibilion.caps.gop_width > 0) {
                    Print(L"  gop=%dx%d\r\n", (int)g_compatibilion.caps.gop_width, (int)g_compatibilion.caps.gop_height);
                }
                Print(L"  recommend: attn=%s model_mb=%d\r\n",
                      compatibilion_recommend_attn(&g_compatibilion) ? L"AVX2" : L"SSE2",
                      (int)compatibilion_recommend_model_mb(&g_compatibilion));
                Print(L"  probes_done=%d\r\n\r\n", (int)g_compatibilion.probes_done);
                continue;
            } else if (my_strncmp(prompt, "/compat_probe", 13) == 0) {
                compatibilion_probe_cpu(&g_compatibilion);
                Print(L"\r\nOK: CPU probed (flags=0x%x)\r\n\r\n", (unsigned)g_compatibilion.caps.cpu_flags);
                continue;

            } else if (my_strncmp(prompt, "/gop", 4) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\n  GOP: not available\r\n\r\n");
                } else {
                    const CHAR16 *pf = L"unknown";
                    if (g_gop_pf == PixelBlueGreenRedReserved8BitPerColor) pf = L"BGRX8888";
                    else if (g_gop_pf == PixelRedGreenBlueReserved8BitPerColor) pf = L"RGBX8888";
                    else if (g_gop_pf == PixelBitMask) pf = L"BITMASK";
                    Print(L"\r\n  GOP: %dx%d ppsl=%d fmt=%s fb=0x%lx\r\n\r\n",
                          (int)g_gop_w, (int)g_gop_h, (int)g_gop_ppsl, pf, (UINT64)(UINTN)g_gop_fb32);
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_on", 7) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                } else {
                    g_tui_enabled = 1;
                    llmk_tui_set_event("/tui_on");
                    llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: TUI enabled\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_off", 8) == 0) {
                g_tui_enabled = 0;
                llmk_tui_set_event("/tui_off");
                Print(L"\r\nOK: TUI disabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_toggle", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                } else {
                    g_tui_enabled = !g_tui_enabled;
                    llmk_tui_set_event("/tui_toggle");
                    if (g_tui_enabled) llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: TUI %s\r\n\r\n", g_tui_enabled ? L"enabled" : L"disabled");
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_redraw", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                } else {
                    llmk_tui_set_event("/tui_redraw");
                    g_tui_enabled = 1;
                    llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: TUI redrawn\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/tui_mode", 9) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                const char *p = prompt + 9;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /tui_mode <status|log|split|files>\r\n");
                    Print(L"  Current: %d\r\n\r\n", g_ui_mode);
                    continue;
                }
                if (my_strncmp(p, "status", 6) == 0) g_ui_mode = 0;
                else if (my_strncmp(p, "log", 3) == 0) g_ui_mode = 1;
                else if (my_strncmp(p, "split", 5) == 0) g_ui_mode = 2;
                else if (my_strncmp(p, "files", 5) == 0) g_ui_mode = 3;
                else {
                    Print(L"\r\nERROR: unknown mode\r\n\r\n");
                    continue;
                }
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_mode");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: UI mode=%d\r\n\r\n", g_ui_mode);
                continue;
            } else if (my_strncmp(prompt, "/tui_log_on", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                g_ui_mode = 1;
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_on");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log UI enabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_log_off", 12) == 0) {
                g_ui_mode = 0;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_off");
                if (g_tui_enabled) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log UI disabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_log_clear", 13) == 0) {
                llmk_tr_clear();
                llmk_tui_set_event("/tui_log_clear");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: transcript cleared\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/tui_log_up", 11) == 0) {
                const char *p = prompt + 11;
                while (*p == ' ' || *p == '\t') p++;
                int n = 10;
                if (*p) {
                    int v = 0;
                    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                    if (v > 0) n = v;
                }
                g_tr_scroll += n;
                if ((UINT32)g_tr_scroll > g_tr_count) g_tr_scroll = (int)g_tr_count;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_up");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log scroll=%d\r\n\r\n", g_tr_scroll);
                continue;
            } else if (my_strncmp(prompt, "/tui_log_down", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;
                int n = 10;
                if (*p) {
                    int v = 0;
                    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                    if (v > 0) n = v;
                }
                g_tr_scroll -= n;
                if (g_tr_scroll < 0) g_tr_scroll = 0;
                g_tui_dirty = 1;
                llmk_tui_set_event("/tui_log_down");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: log scroll=%d\r\n\r\n", g_tr_scroll);
                continue;
            } else if (my_strncmp(prompt, "/tui_log_dump", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 out_name[96];
                if (*p == 0) {
                    StrCpy(out_name, L"llmk-transcript.txt");
                } else {
                    ascii_to_char16(out_name, p, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                // Flush any partial line so the dump matches what the user saw.
                if (g_tr_cur_len > 0) llmk_tr_flush_cur_line();

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_text_file(&f, out_name);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: cannot open %s (%r)\r\n\r\n", out_name, st);
                    continue;
                }
                for (UINT32 age = g_tr_count; age > 0; age--) {
                    const char *line8 = llmk_tr_get_line_by_age(age - 1);
                    CHAR16 line16[LLMK_TR_COLS + 4];
                    ascii_to_char16(line16, line8, (int)(sizeof(line16) / sizeof(line16[0])));
                    llmk_file_write_u16(f, line16);
                    llmk_file_write_u16(f, L"\r\n");
                }
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed (%r)\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote %s\r\n\r\n", out_name);
                }
                continue;
            } else if (my_strncmp(prompt, "/fb_on", 6) == 0 || my_strcmp(prompt, "/fb") == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                g_ui_mode = 3;
                g_tui_enabled = 1;
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_on");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: file browser enabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fb_off", 7) == 0) {
                g_ui_mode = 0;
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_off");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: file browser disabled\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fb_refresh", 11) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available\r\n\r\n");
                    continue;
                }
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_refresh");
                if (g_tui_enabled) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fb_cd", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /fb_cd <dir>\r\n\r\n");
                    continue;
                }
                llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), p);
                ascii_to_char16(g_fb_path16, g_fb_path8, (int)(sizeof(g_fb_path16) / sizeof(g_fb_path16[0])));
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_ui_mode = 3;
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_cd");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: cd %s\r\n\r\n", g_fb_path16);
                continue;
            } else if (my_strncmp(prompt, "/fb_up", 6) == 0) {
                // Parent directory (ASCII path best-effort)
                int n = 0;
                while (g_fb_path8[n]) n++;
                while (n > 0 && (g_fb_path8[n - 1] == '\\' || g_fb_path8[n - 1] == '/')) n--;
                while (n > 0 && g_fb_path8[n - 1] != '\\') n--;
                if (n <= 0) {
                    llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), "\\");
                } else {
                    g_fb_path8[n] = 0;
                    if (g_fb_path8[0] == 0) llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), "\\");
                }
                ascii_to_char16(g_fb_path16, g_fb_path8, (int)(sizeof(g_fb_path16) / sizeof(g_fb_path16[0])));
                llmk_fb_refresh_best_effort();
                llmk_fb_preview_selected_best_effort();
                g_ui_mode = 3;
                g_tui_enabled = 1;
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_up");
                llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: cd %s\r\n\r\n", g_fb_path16);
                continue;
            } else if (my_strncmp(prompt, "/fb_sel", 7) == 0) {
                const char *p = prompt + 7;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 0) v = 0;
                if (v >= g_fb_count) v = (g_fb_count > 0) ? (g_fb_count - 1) : 0;
                g_fb_sel = v;
                llmk_fb_preview_selected_best_effort();
                g_tui_dirty = 1;
                llmk_tui_set_event("/fb_sel");
                if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                Print(L"\r\nOK: sel=%d\r\n\r\n", g_fb_sel);
                continue;
            } else if (my_strncmp(prompt, "/fb_open", 8) == 0) {
                if (g_fb_count <= 0 || g_fb_sel < 0 || g_fb_sel >= g_fb_count) {
                    Print(L"\r\nERROR: no selection\r\n\r\n");
                    continue;
                }
                if (g_fb_entries[g_fb_sel].is_dir) {
                    // cd into dir
                    char newp[128];
                    newp[0] = 0;
                    llmk_ascii_copy_cap(newp, (int)sizeof(newp), g_fb_path8[0] ? g_fb_path8 : "\\");
                    int np = 0;
                    while (newp[np]) np++;
                    if (np > 0 && newp[np - 1] != '\\') {
                        if (np + 1 < (int)sizeof(newp)) newp[np++] = '\\';
                        newp[np] = 0;
                    }
                    llmk_ascii_append_cap(newp, (int)sizeof(newp), g_fb_entries[g_fb_sel].name8);
                    llmk_ascii_copy_cap(g_fb_path8, (int)sizeof(g_fb_path8), newp);
                    ascii_to_char16(g_fb_path16, g_fb_path8, (int)(sizeof(g_fb_path16) / sizeof(g_fb_path16[0])));
                    llmk_fb_refresh_best_effort();
                    llmk_fb_preview_selected_best_effort();
                    g_tui_dirty = 1;
                    llmk_tui_set_event("/fb_open(dir)");
                    if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: cd %s\r\n\r\n", g_fb_path16);
                } else {
                    llmk_fb_preview_selected_best_effort();
                    g_tui_dirty = 1;
                    llmk_tui_set_event("/fb_open(file)");
                    if (g_tui_enabled && g_gop_fb32) llmk_tui_redraw_best_effort();
                    Print(L"\r\nOK: preview loaded\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/render", 7) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available on this firmware path\r\n\r\n");
                    continue;
                }
                const char *dsl = prompt + 7;
                while (*dsl == ' ' || *dsl == '\t') dsl++;
                if (*dsl == 0) {
                    Print(L"\r\nUsage: /render <dsl>\r\n");
                    Print(L"  DSL ops (separate by ';'):\r\n");
                    Print(L"    clear R G B; rect X Y W H R G B; pixel X Y R G B\r\n\r\n");
                    continue;
                }
                int ok = llmk_render_scene_dsl_ex(dsl, 1);
                if (ok) {
                    llmk_gop_force_update();
                    Print(L"\r\nOK: rendered (check screen above)\r\n\r\n");
                } else {
                    CHAR16 msg[140];
                    ascii_to_char16(msg, g_last_dsl_error, (int)(sizeof(msg) / sizeof(msg[0])));
                    Print(L"\r\nERROR: render failed (%s)\r\n", msg);
                    Print(L"Hint: use 'rect' not 'react'\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/save_img", 9) == 0) {
                if (!g_gop_fb32) {
                    Print(L"\r\nERROR: GOP not available (nothing to save)\r\n\r\n");
                    continue;
                }
                const char *name = prompt + 9;
                while (*name == ' ' || *name == '\t') name++;

                CHAR16 out_name[64];
                if (*name == 0) {
                    StrCpy(out_name, L"llmk-img.ppm");
                } else {
                    ascii_to_char16(out_name, name, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                EFI_STATUS st = llmk_save_ppm(out_name);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: save failed (%r)\r\n\r\n", st);
                } else {
                    Print(L"\r\nOK: wrote %s (PPM, flushed)\r\n\r\n", out_name);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_ls", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 path[160];
                if (*p == 0) {
                    path[0] = 0;
                } else {
                    ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));
                }
                Print(L"\r\n");
                llmk_fs_ls_best_effort(path[0] ? path : NULL, 200);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fs_cat", 7) == 0) {
                const char *p = prompt + 7;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /fs_cat <file>\r\n\r\n");
                    continue;
                }
                CHAR16 path[160];
                ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));
                Print(L"\r\n");
                llmk_fs_cat_best_effort(path, 256U * 1024U);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/fs_write", 9) == 0) {
                const char *p = prompt + 9;
                while (*p == ' ' || *p == '\t') p++;
                // Parse path token
                char tok[160];
                int tp = 0;
                while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) tok[tp++] = *p++;
                tok[tp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                const char *text = p;
                if (tok[0] == 0) {
                    Print(L"\r\nUsage: /fs_write <file> <text...>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_WRITE, tok, (UINT32)my_strlen(text), &d);
                    djibion_log_if_observe(&g_djibion, "fs_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_write): %s\r\n\r\n", msg);
                        continue;
                    }

                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/fs_write) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        djibion_apply_transform_path(tok, (int)sizeof(tok), &d);
                    }
                }

                CHAR16 path[160];
                ascii_to_char16(path, tok, (int)(sizeof(path) / sizeof(path[0])));
                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_binary_file(&f, path);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }
                UINTN n = (UINTN)my_strlen(text);
                st = llmk_file_write_bytes(f, text, n);
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: write failed: %r\r\n\r\n", st);
                } else if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed: %r\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote ");
                    Print(L"%s", path);
                    Print(L" (%d bytes)\r\n\r\n", (int)n);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_append", 10) == 0) {
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                // Parse path token
                char tok[160];
                int tp = 0;
                while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) tok[tp++] = *p++;
                tok[tp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                const char *text = p;
                if (tok[0] == 0) {
                    Print(L"\r\nUsage: /fs_append <file> <text...>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_APPEND, tok, (UINT32)my_strlen(text), &d);
                    djibion_log_if_observe(&g_djibion, "fs_append", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_append): %s\r\n\r\n", msg);
                        continue;
                    }

                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/fs_append) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        djibion_apply_transform_path(tok, (int)sizeof(tok), &d);
                    }
                }

                CHAR16 path[160];
                ascii_to_char16(path, tok, (int)(sizeof(path) / sizeof(path[0])));
                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_binary_file_append(&f, path);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }
                UINTN n = (UINTN)my_strlen(text);
                st = llmk_file_write_bytes(f, text, n);
                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: append failed: %r\r\n\r\n", st);
                } else if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed: %r\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: appended ");
                    Print(L"%s", path);
                    Print(L" (%d bytes)\r\n\r\n", (int)n);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_rm", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /fs_rm <file>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_RM, p, 0, &d);
                    djibion_log_if_observe(&g_djibion, "fs_rm", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_rm): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                CHAR16 path[160];
                ascii_to_char16(path, p, (int)(sizeof(path) / sizeof(path[0])));
                EFI_STATUS st = llmk_delete_file_best_effort(path);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: delete failed: %r\r\n\r\n", st);
                } else {
                    Print(L"\r\nOK: deleted %s\r\n\r\n", path);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_cp", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                char src8[128];
                int sp = 0;
                while (*p && *p != ' ' && *p != '\t' && sp + 1 < (int)sizeof(src8)) src8[sp++] = *p++;
                src8[sp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                char dst8[128];
                int dp = 0;
                while (*p && *p != ' ' && *p != '\t' && dp + 1 < (int)sizeof(dst8)) dst8[dp++] = *p++;
                dst8[dp] = 0;
                if (src8[0] == 0 || dst8[0] == 0) {
                    Print(L"\r\nUsage: /fs_cp <src> <dst>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort): validate src (no '..') and govern dst.
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    if (llmk_ascii_has_dotdot(src8)) {
                        Print(L"\r\nDJIBION: blocked (/fs_cp): src path contains '..'\r\n\r\n");
                        continue;
                    }
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_CP, dst8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "fs_cp", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_cp): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] fs_cp dst transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        llmk_ascii_copy_cap(dst8, (int)sizeof(dst8), d.transformed_arg0);
                    }
                }

                CHAR16 src[160];
                CHAR16 dst[160];
                ascii_to_char16(src, src8, (int)(sizeof(src) / sizeof(src[0])));
                ascii_to_char16(dst, dst8, (int)(sizeof(dst) / sizeof(dst[0])));
                EFI_STATUS st = llmk_copy_file_best_effort(src, dst);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: copy failed: %r\r\n\r\n", st);
                } else {
                    Print(L"\r\nOK: copied %s -> %s\r\n\r\n", src, dst);
                }
                continue;
            } else if (my_strncmp(prompt, "/fs_mv", 6) == 0) {
                const char *p = prompt + 6;
                while (*p == ' ' || *p == '\t') p++;
                char src8[128];
                int sp = 0;
                while (*p && *p != ' ' && *p != '\t' && sp + 1 < (int)sizeof(src8)) src8[sp++] = *p++;
                src8[sp] = 0;
                while (*p == ' ' || *p == '\t') p++;
                char dst8[128];
                int dp = 0;
                while (*p && *p != ' ' && *p != '\t' && dp + 1 < (int)sizeof(dst8)) dst8[dp++] = *p++;
                dst8[dp] = 0;
                if (src8[0] == 0 || dst8[0] == 0) {
                    Print(L"\r\nUsage: /fs_mv <src> <dst>\r\n\r\n");
                    continue;
                }

                // Djibion gate (best-effort): validate src (no '..') and govern dst (move implies delete).
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    if (llmk_ascii_has_dotdot(src8)) {
                        Print(L"\r\nDJIBION: blocked (/fs_mv): src path contains '..'\r\n\r\n");
                        continue;
                    }
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_FS_MV, dst8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "fs_mv", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/fs_mv): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] fs_mv dst transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        llmk_ascii_copy_cap(dst8, (int)sizeof(dst8), d.transformed_arg0);
                    }
                }

                CHAR16 src[160];
                CHAR16 dst[160];
                ascii_to_char16(src, src8, (int)(sizeof(src) / sizeof(src[0])));
                ascii_to_char16(dst, dst8, (int)(sizeof(dst) / sizeof(dst[0])));
                EFI_STATUS st = llmk_copy_file_best_effort(src, dst);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: move copy failed: %r\r\n\r\n", st);
                    continue;
                }
                EFI_STATUS st2 = llmk_delete_file_best_effort(src);
                if (EFI_ERROR(st2)) {
                    Print(L"\r\nWARNING: move delete failed: %r\r\n\r\n", st2);
                } else {
                    Print(L"\r\nOK: moved %s -> %s\r\n\r\n", src, dst);
                }
                continue;
            } else if (my_strncmp(prompt, "/snap_save", 10) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 out_name[96];
                if (*p == 0) {
                    StrCpy(out_name, L"llmk-snap.bin");
                } else {
                    ascii_to_char16(out_name, p, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                if (kv_pos <= 0) {
                    Print(L"\r\nERROR: nothing to snapshot (kv_pos=0)\r\n\r\n");
                    continue;
                }
                if (kv_pos > config.seq_len) {
                    Print(L"\r\nERROR: kv_pos out of range\r\n\r\n");
                    continue;
                }

                int kv_dim = (config.dim * config.n_kv_heads) / config.n_heads;
                UINTN slice_floats = (UINTN)kv_pos * (UINTN)kv_dim;
                UINTN slice_bytes = slice_floats * sizeof(float);

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), out_name);
                    UINTN total_bytes = sizeof(LlmkSnapHeader) + (UINTN)config.n_layers * (UINTN)2 * slice_bytes;
                    UINT32 total32 = (total_bytes > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (UINT32)total_bytes;
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_SNAP_SAVE, file8, total32, &d);
                    djibion_log_if_observe(&g_djibion, "snap_save", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_save): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] snap_save path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(out_name, d.transformed_arg0, (int)(sizeof(out_name) / sizeof(out_name[0])));
                    }
                }

                EFI_FILE_HANDLE f = NULL;
                EFI_STATUS st = llmk_open_binary_file(&f, out_name);
                if (EFI_ERROR(st) || !f) {
                    Print(L"\r\nERROR: open failed: %r\r\n\r\n", st);
                    continue;
                }

                LlmkSnapHeader hdr;
                hdr.magic = LLMK_SNAP_MAGIC;
                hdr.version = 1;
                hdr.dim = (UINT32)config.dim;
                hdr.n_layers = (UINT32)config.n_layers;
                hdr.n_heads = (UINT32)config.n_heads;
                hdr.n_kv_heads = (UINT32)config.n_kv_heads;
                hdr.seq_len = (UINT32)config.seq_len;
                hdr.kv_dim = (UINT32)kv_dim;
                hdr.kv_pos = (UINT32)kv_pos;

                st = llmk_write_exact(f, &hdr, sizeof(hdr));
                if (!EFI_ERROR(st)) {
                    for (int l = 0; l < config.n_layers && !EFI_ERROR(st); l++) {
                        float *base = state.key_cache + (UINTN)l * (UINTN)config.seq_len * (UINTN)kv_dim;
                        st = llmk_write_exact(f, base, slice_bytes);
                    }
                    for (int l = 0; l < config.n_layers && !EFI_ERROR(st); l++) {
                        float *base = state.value_cache + (UINTN)l * (UINTN)config.seq_len * (UINTN)kv_dim;
                        st = llmk_write_exact(f, base, slice_bytes);
                    }
                }

                EFI_STATUS flush_st = uefi_call_wrapper(f->Flush, 1, f);
                uefi_call_wrapper(f->Close, 1, f);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: snapshot write failed: %r\r\n\r\n", st);
                } else if (EFI_ERROR(flush_st)) {
                    Print(L"\r\nWARNING: flush failed: %r\r\n\r\n", flush_st);
                } else {
                    Print(L"\r\nOK: wrote snapshot %s (kv_pos=%d)\r\n\r\n", out_name, kv_pos);
                }
                continue;
            } else if (my_strncmp(prompt, "/snap_load", 10) == 0) {
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                const char *p = prompt + 10;
                while (*p == ' ' || *p == '\t') p++;
                CHAR16 in_name[96];
                if (*p == 0) {
                    StrCpy(in_name, L"llmk-snap.bin");
                } else {
                    ascii_to_char16(in_name, p, (int)(sizeof(in_name) / sizeof(in_name[0])));
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), in_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_SNAP_LOAD, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "snap_load", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_load): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"[djibion] snap_load path transformed -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(in_name, d.transformed_arg0, (int)(sizeof(in_name) / sizeof(in_name[0])));
                    }
                }

                EFI_STATUS st = llmk_snap_load_into_state_best_effort(&state, &config, &kv_pos, in_name);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: snapshot load failed: %r\r\n\r\n", st);
                    continue;
                }
                Print(L"\r\nOK: loaded snapshot %s (kv_pos=%d)\r\n\r\n", in_name, kv_pos);
                continue;
            } else if (my_strncmp(prompt, "/snap_autoload_on", 16) == 0) {
                const char *p = prompt + 16;
                while (*p == ' ' || *p == '\t') p++;

                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "snap_autoload", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_autoload_on): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("snap_autoload", "1");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                    continue;
                }
                if (*p) {
                    // Optional file override

                    if (g_djibion.mode != DJIBION_MODE_OFF) {
                        DjibionDecision d;
                        djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "snap_file", (UINT32)my_strlen(p), &d);
                        djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                        if (djibion_should_block(&g_djibion, &d)) {
                            CHAR16 msg[160];
                            ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                            Print(L"\r\nDJIBION: blocked (snap_file update): %s\r\n\r\n", msg);
                            Print(L"\r\nOK: snap_autoload=1 (reboot to apply)\r\n\r\n");
                            llmk_tr_note("SNAP: snap_autoload_on");
                            continue;
                        }
                    }

                    EFI_STATUS st2 = llmk_repl_cfg_set_kv_best_effort("snap_file", p);
                    if (EFI_ERROR(st2)) {
                        Print(L"\r\nWARNING: snap_file update failed: %r\r\n\r\n", st2);
                    }
                }
                Print(L"\r\nOK: snap_autoload=1 (reboot to apply)\r\n\r\n");
                llmk_tr_note("SNAP: snap_autoload_on");
                continue;
            } else if (my_strncmp(prompt, "/snap_autoload_off", 17) == 0) {

                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_CFG_WRITE, "snap_autoload", 1, &d);
                    djibion_log_if_observe(&g_djibion, "cfg_write", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/snap_autoload_off): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                EFI_STATUS st = llmk_repl_cfg_set_kv_best_effort("snap_autoload", "0");
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: repl.cfg update failed: %r\r\n\r\n", st);
                    continue;
                }
                Print(L"\r\nOK: snap_autoload=0 (reboot to apply)\r\n\r\n");
                llmk_tr_note("SNAP: snap_autoload_off");
                continue;
            } else if (my_strncmp(prompt, "/oo_new", 7) == 0) {
                const char *goal = prompt + 7;
                while (*goal == ' ' || *goal == '\t') goal++;
                if (*goal == 0) {
                    Print(L"\r\nUsage: /oo_new <goal>\r\n\r\n");
                    continue;
                }
                int id = llmk_oo_new(goal);
                if (id < 0) {
                    Print(L"\r\nERROR: cannot create entity (full?)\r\n\r\n");
                } else {
                    Print(L"\r\nOK: created entity id=%d\r\n\r\n", id);
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_list", 8) == 0) {
                llmk_oo_list_print();
                continue;
            } else if (my_strncmp(prompt, "/oo_kill", 8) == 0) {
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_kill <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_kill(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: killed entity id=%d\r\n\r\n", id);
                continue;
            } else if (my_strncmp(prompt, "/oo_step", 8) == 0) {
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_step <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_step(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: stepped entity id=%d\r\n\r\n", id);
                continue;
            } else if (my_strncmp(prompt, "/oo_run", 7) == 0) {
                int steps = 1;
                if (prompt[7] == ' ') {
                    int i = 8;
                    int val = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        val = val * 10 + (prompt[i] - '0');
                        i++;
                    }
                    if (val > 0) steps = val;
                }
                int ran = llmk_oo_run(steps);

                Print(L"\r\nOK: ran %d step(s)\r\n\r\n", ran);
                continue;
            } else if (my_strncmp(prompt, "/oo_note", 8) == 0) {
                // Usage: /oo_note <id> <text...>
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                const char *text = prompt + i;
                if (id <= 0 || !text || !text[0]) {
                    Print(L"\r\nUsage: /oo_note <id> <text>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_note(id, text)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: noted entity id=%d\r\n\r\n", id);
                continue;
            } else if (my_strncmp(prompt, "/oo_show", 8) == 0) {
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_show <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_show_print(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_digest", 10) == 0) {
                int i = 10;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_digest <id>\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_digest(id)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOK: digested entity id=%d\r\n\r\n", id);
                continue;
            } else if (my_strncmp(prompt, "/oo_plan", 8) == 0) {
                // Usage: /oo_plan <id> [prio] <action...>  (optionally: a1; a2; a3)
                // prio forms: +3, -1, p=2
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;

                int prio = 0;
                // Optional priority token
                if ((prompt[i] == '+' || prompt[i] == '-') && (prompt[i + 1] >= '0' && prompt[i + 1] <= '9')) {
                    int sign = (prompt[i] == '-') ? -1 : 1;
                    i++;
                    int v = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        v = v * 10 + (prompt[i] - '0');
                        i++;
                    }
                    prio = v * sign;
                    while (prompt[i] == ' ') i++;
                } else if (prompt[i] == 'p' && prompt[i + 1] == '=' && (prompt[i + 2] >= '0' && prompt[i + 2] <= '9')) {
                    i += 2;
                    int v = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        v = v * 10 + (prompt[i] - '0');
                        i++;
                    }
                    prio = v;
                    while (prompt[i] == ' ') i++;
                }

                const char *text = prompt + i;
                if (id <= 0 || !text || !text[0]) {
                    Print(L"\r\nUsage: /oo_plan <id> <action>\r\n");
                    Print(L"  Example: /oo_plan 1 do X; do Y\r\n");
                    Print(L"  Priority: /oo_plan 1 +2 urgent thing\r\n");
                    Print(L"  Tip: you can also write: /oo_plan <1> ...\r\n\r\n");
                    continue;
                }

                // Split on ';' (simple)
                int added = 0;
                char tmp[128];
                int tp = 0;
                for (const char *s = text; ; s++) {
                    char c = *s;
                    if (c == 0 || c == ';') {
                        tmp[tp] = 0;
                        // trim
                        const char *t = tmp;
                        while (*t == ' ' || *t == '\t') t++;
                        int end = 0;
                        while (t[end]) end++;
                        while (end > 0 && (t[end - 1] == ' ' || t[end - 1] == '\t')) end--;
                        char one[128];
                        int op = 0;
                        for (int k = 0; k < end && op + 1 < (int)sizeof(one); k++) one[op++] = t[k];
                        one[op] = 0;

                        if (one[0]) {
                            if (llmk_oo_agenda_add_ex(id, one, prio)) added++;
                        }
                        tp = 0;
                        if (c == 0) break;
                    } else {
                        if (tp + 1 < (int)sizeof(tmp)) tmp[tp++] = c;
                    }
                }

                if (added <= 0) {
                    Print(L"\r\nERROR: failed to add action(s) (unknown id or agenda full)\r\n\r\n");
                } else {
                    Print(L"\r\nOK: added %d action(s) to id=%d\r\n\r\n", added, id);
                    llmk_oo_digest(id);
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_agenda", 10) == 0) {
                int i = 10;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_agenda <id>\r\n");
                    Print(L"  Example: /oo_agenda 1\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_get_brief(id, NULL, 0, NULL, 0)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }
                Print(L"\r\nOO agenda for id=%d:\r\n", id);
                llmk_oo_agenda_print(id);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/oo_next", 8) == 0) {
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_next <id>\r\n");
                    Print(L"  Example: /oo_next 1\r\n\r\n");
                    continue;
                }
                char act[96];
                act[0] = 0;
                int k = 0;
                if (!llmk_oo_agenda_next_ex(id, &k, act, (int)sizeof(act))) {
                    Print(L"\r\nOK: agenda empty (or unknown id=%d)\r\n\r\n", id);
                    continue;
                }
                CHAR16 a16[110];
                ascii_to_char16(a16, act, (int)(sizeof(a16) / sizeof(a16[0])));
                Print(L"\r\nOK: next action for id=%d (#%d, marked doing):\r\n  %s\r\n\r\n", id, k, a16);
                llmk_oo_digest(id);
                continue;
            } else if (my_strncmp(prompt, "/oo_done", 8) == 0) {
                // Usage: /oo_done <id> <k>
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;
                int k = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    k = k * 10 + (prompt[i] - '0');
                    i++;
                }
                if (id <= 0 || k <= 0) {
                    Print(L"\r\nUsage: /oo_done <id> <k>\r\n");
                    Print(L"  Example: /oo_done 1 2\r\n\r\n");
                    continue;
                }
                char txt[96];
                txt[0] = 0;
                if (!llmk_oo_action_get(id, k, txt, (int)sizeof(txt), NULL, NULL)) {
                    Print(L"\r\nERROR: unknown action #%d for id=%d\r\n\r\n", k, id);
                    continue;
                }
                if (!llmk_oo_action_set_state(id, k, 2)) {
                    Print(L"\r\nERROR: failed to mark done (#%d)\r\n\r\n", k);
                    continue;
                }
                {
                    char dn[196];
                    int dp = 0;
                    const char *h = "done: ";
                    for (int j = 0; h[j] && dp + 1 < (int)sizeof(dn); j++) dn[dp++] = h[j];
                    for (int j = 0; txt[j] && dp + 1 < (int)sizeof(dn); j++) dn[dp++] = txt[j];
                    dn[dp] = 0;
                    llmk_oo_note(id, dn);
                }
                Print(L"\r\nOK: marked done id=%d #%d\r\n\r\n", id, k);
                llmk_oo_digest(id);
                continue;
            } else if (my_strncmp(prompt, "/oo_prio", 8) == 0) {
                // Usage: /oo_prio <id> <k> <prio>
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;
                int k = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    k = k * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                int sign = 1;
                if (prompt[i] == '-') { sign = -1; i++; }
                else if (prompt[i] == '+') { i++; }
                int pr = 0;
                int any = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    pr = pr * 10 + (prompt[i] - '0');
                    i++;
                    any = 1;
                }
                pr *= sign;
                if (id <= 0 || k <= 0 || !any) {
                    Print(L"\r\nUsage: /oo_prio <id> <k> <prio>\r\n");
                    Print(L"  Example: /oo_prio 1 2 +3\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_action_set_prio(id, k, pr)) {
                    Print(L"\r\nERROR: failed to set prio id=%d #%d\r\n\r\n", id, k);
                    continue;
                }
                Print(L"\r\nOK: set prio id=%d #%d -> %d\r\n\r\n", id, k, pr);
                llmk_oo_digest(id);
                continue;
            } else if (my_strncmp(prompt, "/oo_edit", 7) == 0) {
                // Usage: /oo_edit <id> <k> <new text...>
                int i = 7;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;
                int k = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    k = k * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;
                const char *text = prompt + i;
                if (id <= 0 || k <= 0 || !text || !text[0]) {
                    Print(L"\r\nUsage: /oo_edit <id> <k> <text>\r\n");
                    Print(L"  Example: /oo_edit 1 2 rewrite this action\r\n\r\n");
                    continue;
                }
                if (!llmk_oo_action_edit(id, k, text)) {
                    Print(L"\r\nERROR: failed to edit id=%d #%d\r\n\r\n", id, k);
                    continue;
                }
                Print(L"\r\nOK: edited id=%d #%d\r\n\r\n", id, k);
                llmk_oo_digest(id);
                continue;
            } else if (my_strncmp(prompt, "/oo_save", 8) == 0) {
                const char *name = prompt + 8;
                while (*name == ' ' || *name == '\t') name++;
                CHAR16 out_name[96];
                if (*name == 0) {
                    StrCpy(out_name, oo_state_file);
                } else {
                    ascii_to_char16(out_name, name, (int)(sizeof(out_name) / sizeof(out_name[0])));
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), out_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_SAVE, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "oo_save", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_save): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/oo_save) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(out_name, d.transformed_arg0, (int)(sizeof(out_name) / sizeof(out_name[0])));
                    }
                }

                // Best-effort backup (copy previous target -> .bak) before overwriting.
                {
                    CHAR16 bak[120];
                    llmk_make_bak_name(out_name, bak, (int)(sizeof(bak) / sizeof(bak[0])));
                    llmk_copy_file_best_effort(out_name, bak);
                }

                int n = 0;
                EFI_STATUS st = llmk_oo_save_to_file_best_effort(out_name, &n);
                if (EFI_ERROR(st)) {
                    Print(L"\r\nERROR: failed to write %s: %r\r\n\r\n", out_name, st);
                } else {
                    Print(L"\r\nOK: wrote %s (%d bytes)\r\n\r\n", out_name, n);
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_load", 8) == 0) {
                const char *name = prompt + 8;
                while (*name == ' ' || *name == '\t') name++;
                CHAR16 in_name[96];
                if (*name == 0) {
                    StrCpy(in_name, oo_state_file);
                } else {
                    ascii_to_char16(in_name, name, (int)(sizeof(in_name) / sizeof(in_name[0])));
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), in_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_LOAD, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "oo_load", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_load): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/oo_load) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(in_name, d.transformed_arg0, (int)(sizeof(in_name) / sizeof(in_name[0])));
                    }
                }

                // Stop auto/exec mode (loading changes entity IDs/state)
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;

                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;

                void *buf = NULL;
                UINTN len = 0;
                EFI_STATUS st = llmk_read_entire_file_best_effort(in_name, &buf, &len);
                CHAR16 bak[120];
                llmk_make_bak_name(in_name, bak, (int)(sizeof(bak) / sizeof(bak[0])));

                if (EFI_ERROR(st)) {
                    // Fallback: try .bak
                    EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
                    if (EFI_ERROR(st2)) {
                        Print(L"\r\nERROR: failed to read %s: %r\r\n\r\n", in_name, st);
                        continue;
                    }
                    int imported = llmk_oo_import((const char *)buf, (int)len);
                    uefi_call_wrapper(BS->FreePool, 1, buf);
                    if (imported < 0) {
                        Print(L"\r\nERROR: parse failed\r\n\r\n");
                    } else {
                        Print(L"\r\nOK: loaded %d entity(s) from %s\r\n\r\n", imported, bak);
                    }
                    continue;
                }

                int imported = llmk_oo_import((const char *)buf, (int)len);
                uefi_call_wrapper(BS->FreePool, 1, buf);

                if (imported < 0) {
                    // Fallback: try .bak
                    EFI_STATUS st2 = llmk_read_entire_file_best_effort(bak, &buf, &len);
                    if (EFI_ERROR(st2)) {
                        Print(L"\r\nERROR: parse failed\r\n\r\n");
                    } else {
                        imported = llmk_oo_import((const char *)buf, (int)len);
                        uefi_call_wrapper(BS->FreePool, 1, buf);
                        if (imported < 0) {
                            Print(L"\r\nERROR: parse failed\r\n\r\n");
                        } else {
                            Print(L"\r\nOK: loaded %d entity(s) from %s\r\n\r\n", imported, bak);
                        }
                    }
                } else {
                    Print(L"\r\nOK: loaded %d entity(s) from %s\r\n\r\n", imported, in_name);
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_think", 9) == 0) {
                // Usage: /oo_think <id> <prompt...>
                int i = 9;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                const char *q = prompt + i;
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_think <id> [prompt]\r\n");
                    Print(L"  Example: /oo_think 1\r\n");
                    Print(L"           /oo_think 1 how should I proceed?\r\n\r\n");
                    continue;
                }

                // Save the user's raw prompt (for logging into entity notes).
                // If empty, use the default question.
                const char *user_q = (q && q[0]) ? q : "next concrete action";
                {
                    int up = 0;
                    for (const char *s = user_q; *s && up + 1 < (int)sizeof(oo_think_user); s++) oo_think_user[up++] = *s;
                    oo_think_user[up] = 0;
                }

                // Build a compact prompt; includes agenda context.
                char new_prompt[512];
                if (!llmk_oo_build_think_prompt(id, oo_think_user, new_prompt, (int)sizeof(new_prompt))) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }

                // Swap in synthesized prompt for this turn.
                for (int k = 0; k < (int)sizeof(prompt); k++) {
                    prompt[k] = new_prompt[k];
                    if (new_prompt[k] == 0) break;
                }

                Print(L"\r\n[oo] thinking...\r\n");

                // Configure capture mode for model output.
                g_capture_mode = 1;
                capture_kind = 2;
                oo_think_id = id;
                llmk_capture_reset();

                // Keep it short and avoid stopping on the "You:" needle.
                stop_on_you = 0;
                stop_on_double_nl = 1;
                if (max_gen_tokens > 96) max_gen_tokens = 96;
                continue;
            } else if (my_strncmp(prompt, "/oo_auto", 8) == 0) {
                // Usage: /oo_auto <id> [n] [prompt...]
                // Runs n cycles of: think (LLM capture) -> store notes -> step -> digest
                int i = 8;
                while (prompt[i] == ' ') i++;
                int id = 0;
                while (prompt[i] >= '0' && prompt[i] <= '9') {
                    id = id * 10 + (prompt[i] - '0');
                    i++;
                }
                while (prompt[i] == ' ') i++;

                int n = 3;
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    n = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        n = n * 10 + (prompt[i] - '0');
                        i++;
                    }
                    while (prompt[i] == ' ') i++;
                }

                const char *q = prompt + i;
                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_auto <id> [n] [prompt]\r\n\r\n");
                    continue;
                }

                // Ensure entity exists
                if (!llmk_oo_get_brief(id, NULL, 0, NULL, 0)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }

                if (n < 1) n = 1;
                if (n > 16) n = 16;

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_AUTO, "oo_auto", (UINT32)n, &d);
                    djibion_log_if_observe(&g_djibion, "oo_auto", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_auto): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                // Store the user prompt (optional)
                g_oo_auto_user[0] = 0;
                if (q && q[0]) {
                    int up = 0;
                    for (const char *s = q; *s && up + 1 < (int)sizeof(g_oo_auto_user); s++) g_oo_auto_user[up++] = *s;
                    g_oo_auto_user[up] = 0;
                } else {
                    const char *def = "next concrete action";
                    int up = 0;
                    for (const char *s = def; *s && up + 1 < (int)sizeof(g_oo_auto_user); s++) g_oo_auto_user[up++] = *s;
                    g_oo_auto_user[up] = 0;
                }

                g_oo_auto_active = 1;
                g_oo_auto_id = id;
                g_oo_auto_remaining = n;
                g_oo_auto_total = n;

                // /oo_auto takes over; ensure /oo_exec is off.
                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;

                Print(L"\r\n[oo_auto] started: id=%d cycles=%d\r\n", id, n);
                {
                    CHAR16 p16[260];
                    ascii_to_char16(p16, g_oo_auto_user, (int)(sizeof(p16) / sizeof(p16[0])));
                    Print(L"[oo_auto] prompt: %s\r\n\r\n", p16);
                }

                // The actual cycles will run automatically at the top of the loop.
                continue;
            } else if (my_strncmp(prompt, "/oo_exec", 8) == 0) {
                // Usage: /oo_exec <id> [n] [--plan] [hint...]
                // Runs n cycles consuming agenda actions (marks done). Stops when agenda empty unless --plan.
                int i = 8;
                int id = llmk_parse_entity_id_allow_brackets(prompt, &i);
                while (prompt[i] == ' ') i++;

                int n = 3;
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    n = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') {
                        n = n * 10 + (prompt[i] - '0');
                        i++;
                    }
                    while (prompt[i] == ' ') i++;
                }

                int plan_if_empty = 0;
                // Optional flag "--plan" (must appear before hint text)
                if (prompt[i] == '-' && prompt[i + 1] == '-' && prompt[i + 2] == 'p' && prompt[i + 3] == 'l' && prompt[i + 4] == 'a' && prompt[i + 5] == 'n') {
                    plan_if_empty = 1;
                    i += 6;
                    while (prompt[i] == ' ') i++;
                }

                const char *hint = prompt + i;

                if (id <= 0) {
                    Print(L"\r\nUsage: /oo_exec <id> [n] [--plan] [hint]\r\n");
                    Print(L"  Example: /oo_exec 1 5\r\n");
                    Print(L"           /oo_exec <1> 8 --plan\r\n");
                    Print(L"           /oo_exec 1 4 be strict and concise\r\n\r\n");
                    continue;
                }

                // Ensure entity exists
                if (!llmk_oo_get_brief(id, NULL, 0, NULL, 0)) {
                    Print(L"\r\nERROR: unknown entity id=%d\r\n\r\n", id);
                    continue;
                }

                if (n < 1) n = 1;
                if (n > 16) n = 16;

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_OO_EXEC, "oo_exec", (UINT32)n, &d);
                    djibion_log_if_observe(&g_djibion, "oo_exec", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/oo_exec): %s\r\n\r\n", msg);
                        continue;
                    }
                }

                // Store hint (optional)
                g_oo_exec_hint[0] = 0;
                if (hint && hint[0]) {
                    int hp = 0;
                    for (const char *s = hint; *s && hp + 1 < (int)sizeof(g_oo_exec_hint); s++) g_oo_exec_hint[hp++] = *s;
                    g_oo_exec_hint[hp] = 0;
                } else {
                    const char *def = "Execute the action concisely; give concrete steps.";
                    int hp = 0;
                    for (const char *s = def; *s && hp + 1 < (int)sizeof(g_oo_exec_hint); s++) g_oo_exec_hint[hp++] = *s;
                    g_oo_exec_hint[hp] = 0;
                }

                g_oo_exec_active = 1;
                g_oo_exec_id = id;
                g_oo_exec_remaining = n;
                g_oo_exec_total = n;
                g_oo_exec_plan_if_empty = plan_if_empty;

                // /oo_exec takes over; ensure /oo_auto is off.
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;

                Print(L"\r\n[oo_exec] started: id=%d cycles=%d plan_if_empty=%d\r\n", id, n, plan_if_empty);
                {
                    Print(L"[oo_exec] hint: ");
                    llmk_print_ascii(g_oo_exec_hint);
                    Print(L"\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/oo_exec_stop", 13) == 0) {
                if (g_oo_exec_active) {
                    Print(L"\r\n[oo_exec] stopping (id=%d remaining=%d)\r\n\r\n", g_oo_exec_id, g_oo_exec_remaining);
                } else {
                    Print(L"\r\n[oo_exec] not active\r\n\r\n");
                }
                g_oo_exec_active = 0;
                g_oo_exec_id = 0;
                g_oo_exec_remaining = 0;
                g_oo_exec_total = 0;
                g_oo_exec_plan_if_empty = 0;
                g_oo_exec_hint[0] = 0;
                continue;
            } else if (my_strncmp(prompt, "/oo_auto_stop", 13) == 0) {
                if (g_oo_auto_active) {
                    Print(L"\r\n[oo_auto] stopping (id=%d remaining=%d)\r\n\r\n", g_oo_auto_id, g_oo_auto_remaining);
                } else {
                    Print(L"\r\n[oo_auto] not active\r\n\r\n");
                }
                g_oo_auto_active = 0;
                g_oo_auto_id = 0;
                g_oo_auto_remaining = 0;
                g_oo_auto_total = 0;
                g_oo_auto_user[0] = 0;
                continue;
            } else if (my_strncmp(prompt, "/oo_consult_mock", 15) == 0) {
                // OO M5 (test/CI): deterministic consult without LLM generation.
                // Usage:
                //   /oo_consult_mock <suggestion>
                int consult_enabled = g_cfg_oo_llm_consult;
                if (consult_enabled < 0) {
                    consult_enabled = g_cfg_oo_enable; // default: follow oo_enable
                }
                if (!consult_enabled) {
                    Print(L"\r\nERROR: OO LLM consult is disabled (oo_llm_consult=0)\r\n\r\n");
                    continue;
                }
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                if (!p || !p[0]) {
                    Print(L"\r\nUsage: /oo_consult_mock <suggestion>\r\n\r\n");
                    continue;
                }

                // 1) Collect system state (best-effort)
                UINT64 ram_mb = llmk_get_conventional_ram_bytes_best_effort() / (1024ULL * 1024ULL);
                UINT32 mode = g_oo_last_mode_valid ? g_oo_last_mode : LLMK_OO_MODE_SAFE;
                UINT64 boots = 0;
                {
                    LlmkOoState s;
                    if (llmk_oo_load_state_best_effort(&s)) {
                        boots = s.boot_count;
                        mode = s.mode;
                    }
                }

                // 2) Sanitize suggestion to ASCII and run the same policy pipeline.
                char sugg[128];
                int sp = 0;
                while (*p && sp + 1 < (int)sizeof(sugg)) {
                    char c = *p++;
                    if (c < 0x20 || c > 0x7E) c = '_';
                    sugg[sp++] = c;
                }
                sugg[sp] = 0;

                Print(L"\r\n[oo_consult_mock] using mock suggestion\r\n\r\n");
                llmk_oo_consult_process_suggestion(ram_mb, mode, boots, config.seq_len, config.seq_len, sugg);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/oo_consult", 11) == 0) {
                // OO M5: LLM consult (suggest system adaptation action).
                // Check prerequisites.
                int consult_enabled = g_cfg_oo_llm_consult;
                if (consult_enabled < 0) {
                    consult_enabled = g_cfg_oo_enable; // default: follow oo_enable
                }
                if (!consult_enabled) {
                    Print(L"\r\nERROR: OO LLM consult is disabled (oo_llm_consult=0)\r\n\r\n");
                    continue;
                }
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }
                if (!g_llmk_ready) {
                    Print(L"\r\nERROR: llmk not ready (no model loaded)\r\n\r\n");
                    continue;
                }
                if (g_loaded_model_format == LLMK_MODEL_FMT_UNKNOWN) {
                    Print(L"\r\nERROR: no model loaded\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_consult] Consulting LLM for system status adaptation...\r\n\r\n");
                llmk_oo_consult_execute(&config, &weights, &state, &tokenizer,
                                       temperature, min_p, top_p, top_k);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/oo_log", 7) == 0) {
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_log] OOCONSULT.LOG tail:\r\n");
                llmk_oo_print_ooconsult_tail_best_effort(10);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/oo_jour", 8) == 0 || my_strncmp(prompt, "/oo_journal", 11) == 0) {
                if (!g_cfg_oo_enable) {
                    Print(L"\r\nERROR: OO is not enabled (oo_enable=0)\r\n\r\n");
                    continue;
                }

                Print(L"\r\n[oo_jour] OOJOUR.LOG tail:\r\n");
                llmk_oo_print_oojour_tail_best_effort(10);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/autorun_stop", 13) == 0) {
                if (g_autorun_active) {
                    Print(L"\r\n[autorun] stopping\r\n\r\n");
                    llmk_autorun_stop();
                } else {
                    Print(L"\r\n[autorun] not active\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/autorun", 8) == 0) {
                // Usage:
                //   /autorun [--print] [--shutdown|--no-shutdown] [file]
                // Defaults come from repl.cfg (autorun_file, autorun_shutdown_when_done).
                int do_print = 0;
                int shutdown = g_cfg_autorun_shutdown_when_done;
                CHAR16 in_name[96];
                StrCpy(in_name, g_cfg_autorun_file);

                const char *p = prompt + 8;
                while (*p == ' ' || *p == '\t') p++;

                while (*p) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == 0) break;

                    char tok[96];
                    int tp = 0;
                    while (*p && *p != ' ' && *p != '\t' && tp + 1 < (int)sizeof(tok)) {
                        tok[tp++] = *p++;
                    }
                    tok[tp] = 0;
                    if (tok[0] == 0) break;

                    if (llmk_cfg_streq_ci(tok, "--print") || llmk_cfg_streq_ci(tok, "--dry") || llmk_cfg_streq_ci(tok, "--dry-run")) {
                        do_print = 1;
                        continue;
                    }
                    if (llmk_cfg_streq_ci(tok, "--shutdown")) {
                        shutdown = 1;
                        continue;
                    }
                    if (llmk_cfg_streq_ci(tok, "--no-shutdown")) {
                        shutdown = 0;
                        continue;
                    }

                    // First non-flag token is treated as file name.
                    if (tok[0] != '-') {
                        ascii_to_char16(in_name, tok, (int)(sizeof(in_name) / sizeof(in_name[0])));
                        continue;
                    }

                    Print(L"\r\nUsage: /autorun [--print] [--shutdown|--no-shutdown] [file]\r\n\r\n");
                    do_print = -1;
                    break;
                }

                if (do_print == -1) continue;
                if (do_print) {
                    llmk_autorun_print_file_best_effort(in_name, 200);
                    continue;
                }

                // Djibion gate (best-effort)
                if (g_djibion.mode != DJIBION_MODE_OFF) {
                    char file8[128];
                    llmk_char16_to_ascii_cap(file8, (int)sizeof(file8), in_name);
                    DjibionDecision d;
                    djibion_decide(&g_djibion, DJIBION_ACT_AUTORUN, file8, 0, &d);
                    djibion_log_if_observe(&g_djibion, "autorun", &d);
                    if (djibion_should_block(&g_djibion, &d)) {
                        CHAR16 msg[160];
                        ascii_to_char16(msg, d.reason, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"\r\nDJIBION: blocked (/autorun): %s\r\n\r\n", msg);
                        continue;
                    }
                    if (d.verdict == DJIBION_VERDICT_TRANSFORM && d.transformed_arg0[0]) {
                        Print(L"\r\nDJIBION: transform (/autorun) -> ");
                        llmk_print_ascii(d.transformed_arg0);
                        Print(L"\r\n");
                        ascii_to_char16(in_name, d.transformed_arg0, (int)(sizeof(in_name) / sizeof(in_name[0])));
                    }
                }

                if (!llmk_autorun_start(in_name, shutdown)) {
                    Print(L"\r\nERROR: failed to start autorun from %s\r\n\r\n", in_name);
                } else {
                    Print(L"\r\nOK: autorun started from %s (shutdown_when_done=%d)\r\n\r\n", in_name, shutdown);
                }
                continue;
            } else if (my_strncmp(prompt, "/reset", 6) == 0) {
                Print(L"\r\nResetting runtime state...\r\n");
                if (g_llmk_ready) {
                    llmk_reset_runtime_state();
                    Print(L"OK\r\n\r\n");
                } else {
                    Print(L"  (llmk not ready)\r\n\r\n");
                }
                continue;
            } else if (my_strncmp(prompt, "/clear", 6) == 0) {
                Print(L"\r\nClearing KV cache...\r\n");
                reset_kv_cache(&state, &config);
                kv_pos = 0;
                g_llmk_kv_pos = kv_pos;
                Print(L"OK: KV cache cleared, context reset\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/version", 8) == 0) {
                Print(L"\r\nllm-baremetal REPL v3\r\n");
                Print(L"  build=%s\r\n", LLMB_BUILD_ID);
                const CHAR16 *shown_model = NULL;
                if (g_loaded_model_path16[0]) shown_model = g_loaded_model_path16;
                else shown_model = model_filename;
                Print(L"  model=%s seq_len=%d kv_pos=%d\r\n", shown_model ? shown_model : L"(unknown)", config.seq_len, kv_pos);
                Print(L"  features=zones+sentinel+log djibmark utf8 multiline persist\r\n");
                Print(L"  hint: /cpu for SIMD, /ctx for config\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diag", 5) == 0) {
                llmk_print_diag();
                continue;
            } else if (my_strncmp(prompt, "/djibmarks", 10) == 0) {
                DJIBMARK_REPL();
                Print(L"\r\nDjibMark Trace (last %d marks):\r\n", (int)djibmark_count());
                Print(L"  Magic: 0x%08X (DJIB2026)\r\n", DJIBMARK_MAGIC);
                Print(L"  Total recorded: %u\r\n", g_djibmark_state.total_marks);
                Print(L"  Enabled: %s\r\n\r\n", g_djibmark_state.enabled ? L"yes" : L"no");
                
                UINT32 count = djibmark_count();
                if (count > 32) count = 32;  // Limit to 32 most recent
                
                Print(L"  Seq      TSC          Phase    Location\r\n");
                Print(L"  -------- ------------ -------- ------------------\r\n");
                for (UINT32 i = 0; i < count; i++) {
                    DjibMark* m = djibmark_get(i);
                    if (!m || m->magic != DJIBMARK_MAGIC) continue;
                    
                    // Convert CHAR8* to print char by char
                    Print(L"  %08u %012lu %-8s ", m->sequence, m->timestamp_tsc, djibmark_phase_name(m->phase));
                    if (m->location) {
                        for (const CHAR8* p = m->location; *p; p++) {
                            Print(L"%c", (CHAR16)*p);
                        }
                    }
                    Print(L":%u\r\n", m->line);
                }
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/djibperf", 9) == 0) {
                DJIBMARK_REPL();
                Print(L"\r\nDjibMark Performance Analysis:\r\n\r\n");
                
                UINT32 count = djibmark_count();
                if (count < 2) {
                    Print(L"  Need at least 2 marks for analysis\r\n\r\n");
                    continue;
                }
                
                // Analyze phase transitions
                UINT64 prefill_cycles = 0, decode_cycles = 0;
                UINT32 prefill_count = 0, decode_count = 0;
                
                for (UINT32 i = 1; i < count && i < 128; i++) {
                    DjibMark* curr = djibmark_get(i-1);
                    DjibMark* prev = djibmark_get(i);
                    if (!curr || !prev) continue;
                    if (curr->magic != DJIBMARK_MAGIC || prev->magic != DJIBMARK_MAGIC) continue;
                    
                    UINT64 delta = (curr->timestamp_tsc > prev->timestamp_tsc) 
                                   ? (curr->timestamp_tsc - prev->timestamp_tsc) : 0;
                    
                    if (curr->phase == DJIBMARK_PHASE_PREFILL) {
                        prefill_cycles += delta;
                        prefill_count++;
                    } else if (curr->phase == DJIBMARK_PHASE_DECODE) {
                        decode_cycles += delta;
                        decode_count++;
                    }
                }
                
                Print(L"  Prefill phase:\r\n");
                Print(L"    Count: %u marks\r\n", prefill_count);
                Print(L"    Total cycles: %lu\r\n", prefill_cycles);
                if (prefill_count > 0) {
                    Print(L"    Avg cycles/mark: %lu\r\n", prefill_cycles / prefill_count);
                }
                
                Print(L"\r\n  Decode phase:\r\n");
                Print(L"    Count: %u marks\r\n", decode_count);
                Print(L"    Total cycles: %lu\r\n", decode_cycles);
                if (decode_count > 0) {
                    Print(L"    Avg cycles/mark: %lu\r\n", decode_cycles / decode_count);
                }
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/djibion_on", 10) == 0) {
                djibion_set_mode(&g_djibion, DJIBION_MODE_OBSERVE);
                Print(L"\r\nOK: Djibion mode=%s\r\n\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                continue;
            } else if (my_strncmp(prompt, "/djibion_off", 11) == 0) {
                djibion_set_mode(&g_djibion, DJIBION_MODE_OFF);
                Print(L"\r\nOK: Djibion mode=%s\r\n\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                continue;
            } else if (my_strncmp(prompt, "/djibion_enforce", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                djibion_set_mode(&g_djibion, (DjibionMode)v);
                Print(L"\r\nOK: Djibion mode=%s\r\n\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                continue;
            } else if (my_strncmp(prompt, "/djibion_status", 14) == 0) {
                Print(L"\r\n[Djibion]\r\n");
                Print(L"  mode=%s\r\n", (CHAR16 *)djibion_mode_name(g_djibion.mode));
                Print(L"  laws: max_fs_write_bytes=%d allow_fs_write=%d allow_fs_delete=%d\r\n",
                      (int)g_djibion.laws.max_fs_write_bytes,
                      (int)g_djibion.laws.allow_fs_write,
                      (int)g_djibion.laws.allow_fs_delete);
                Print(L"  laws: max_snap_bytes=%d allow_snap_load=%d allow_snap_save=%d\r\n",
                    (int)g_djibion.laws.max_snap_bytes,
                    (int)g_djibion.laws.allow_snap_load,
                    (int)g_djibion.laws.allow_snap_save);
                Print(L"  laws: allow_cfg_write=%d\r\n", (int)g_djibion.laws.allow_cfg_write);
                Print(L"  laws: max_oo_cycles=%d allow_oo_exec=%d allow_oo_auto=%d allow_autorun=%d\r\n",
                      (int)g_djibion.laws.max_oo_cycles,
                      (int)g_djibion.laws.allow_oo_exec,
                      (int)g_djibion.laws.allow_oo_auto,
                      (int)g_djibion.laws.allow_autorun);
                    Print(L"  laws: allow_oo_persist=%d\r\n", (int)g_djibion.laws.allow_oo_persist);
                {
                    CHAR16 pfx[80];
                    ascii_to_char16(pfx, g_djibion.laws.fs_mut_prefix, (int)(sizeof(pfx) / sizeof(pfx[0])));
                    Print(L"  laws: fs_mut_prefix=%s\r\n", pfx[0] ? pfx : L"(none)");
                }
                Print(L"  decisions: total=%d rejected=%d transformed=%d\r\n\r\n",
                      (int)g_djibion.decisions_total,
                      (int)g_djibion.decisions_rejected,
                      (int)g_djibion.decisions_transformed);
                continue;
            } else if (my_strncmp(prompt, "/djibion_prefix", 14) == 0) {
                const char *p = prompt + 14;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /djibion_prefix <prefix>\r\n");
                    Print(L"  Example: /djibion_prefix \\test_dir\\\r\n\r\n");
                    continue;
                }
                llmk_ascii_copy_cap(g_djibion.laws.fs_mut_prefix, (int)sizeof(g_djibion.laws.fs_mut_prefix), p);
                Print(L"\r\nOK: fs_mut_prefix=");
                llmk_print_ascii(g_djibion.laws.fs_mut_prefix);
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_delete", 20) == 0) {
                const char *p = prompt + 20;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_fs_delete = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_fs_delete=%d\r\n\r\n", (int)g_djibion.laws.allow_fs_delete);
                continue;
            } else if (my_strncmp(prompt, "/djibion_max_write", 16) == 0) {
                const char *p = prompt + 16;
                while (*p == ' ' || *p == '\t') p++;
                UINT32 v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (UINT32)(*p - '0'); p++; }
                if (v < 256) v = 256;
                g_djibion.laws.max_fs_write_bytes = v;
                Print(L"\r\nOK: max_fs_write_bytes=%d\r\n\r\n", (int)g_djibion.laws.max_fs_write_bytes);
                continue;
            } else if (my_strncmp(prompt, "/djibion_max_oo", 13) == 0) {
                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;
                UINT32 v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (UINT32)(*p - '0'); p++; }
                if (v < 1) v = 1;
                if (v > 64) v = 64;
                g_djibion.laws.max_oo_cycles = v;
                Print(L"\r\nOK: max_oo_cycles=%d\r\n\r\n", (int)g_djibion.laws.max_oo_cycles);
                continue;
            } else if (my_strncmp(prompt, "/djibion_max_snap", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                UINT32 v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (UINT32)(*p - '0'); p++; }
                if (v < (1024 * 1024)) v = (1024 * 1024);
                g_djibion.laws.max_snap_bytes = v;
                Print(L"\r\nOK: max_snap_bytes=%d\r\n\r\n", (int)g_djibion.laws.max_snap_bytes);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_snap_load", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_snap_load = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_snap_load=%d\r\n\r\n", (int)g_djibion.laws.allow_snap_load);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_snap_save", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_snap_save = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_snap_save=%d\r\n\r\n", (int)g_djibion.laws.allow_snap_save);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_cfg_write", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_cfg_write = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_cfg_write=%d\r\n\r\n", (int)g_djibion.laws.allow_cfg_write);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_autorun", 21) == 0) {
                const char *p = prompt + 21;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_autorun = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_autorun=%d\r\n\r\n", (int)g_djibion.laws.allow_autorun);
                continue;
            } else if (my_strncmp(prompt, "/djibion_allow_oo_persist", 23) == 0) {
                const char *p = prompt + 23;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                g_djibion.laws.allow_oo_persist = (v != 0) ? 1 : 0;
                Print(L"\r\nOK: allow_oo_persist=%d\r\n\r\n", (int)g_djibion.laws.allow_oo_persist);
                continue;
            } else if (my_strncmp(prompt, "/diopion_on", 10) == 0) {
                diopion_set_mode(&g_diopion, DIOPION_MODE_OBSERVE);
                Print(L"\r\nOK: Diopion mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_off", 11) == 0) {
                // Stop any active burst and restore knobs immediately.
                if (g_diopion_burst_active) {
                    g_diopion_burst_remaining = 0;
                    llmk_diopion_burst_finish_one(&max_gen_tokens, &top_k, &temperature);
                }
                diopion_set_mode(&g_diopion, DIOPION_MODE_OFF);
                Print(L"\r\nOK: Diopion mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_enforce", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                diopion_set_mode(&g_diopion, (DiopionMode)v);
                Print(L"\r\nOK: Diopion mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_profile", 15) == 0) {
                const char *p = prompt + 15;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0) {
                    Print(L"\r\nUsage: /diopion_profile <none|animal|vegetal|geom|bio>\r\n\r\n");
                    continue;
                }
                if (llmk_cfg_streq_ci(p, "animal")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_ANIMAL);
                else if (llmk_cfg_streq_ci(p, "vegetal")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_VEGETAL);
                else if (llmk_cfg_streq_ci(p, "geom") || llmk_cfg_streq_ci(p, "geometric")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_GEOM);
                else if (llmk_cfg_streq_ci(p, "bio") || llmk_cfg_streq_ci(p, "biological")) diopion_set_profile(&g_diopion, DIOPION_PROFILE_BIO);
                else diopion_set_profile(&g_diopion, DIOPION_PROFILE_NONE);
                Print(L"\r\nOK: Diopion profile=");
                llmk_print_ascii(diopion_profile_name_ascii(g_diopion.profile));
                Print(L"\r\n\r\n");
                continue;
            } else if (my_strncmp(prompt, "/diopion_status", 14) == 0) {
                Print(L"\r\n[Diopion]\r\n");
                Print(L"  mode=");
                llmk_print_ascii(diopion_mode_name_ascii(g_diopion.mode));
                Print(L" profile=");
                llmk_print_ascii(diopion_profile_name_ascii(g_diopion.profile));
                Print(L"\r\n");
                Print(L"  burst_defaults: turns=%d max_tokens=%d top_k=%d temp=%d.%03d\r\n",
                      (int)g_diopion.params.burst_turns_default,
                      (int)g_diopion.params.burst_max_gen_tokens,
                      (int)g_diopion.params.burst_top_k,
                      (int)(g_diopion.params.burst_temp_milli / 1000u),
                      (int)(g_diopion.params.burst_temp_milli % 1000u));
                Print(L"  bursts_started=%d\r\n", (int)g_diopion.bursts_started);
                Print(L"  burst_active=%d remaining=%d\r\n\r\n", g_diopion_burst_active, g_diopion_burst_remaining);
                continue;
            } else if (my_strncmp(prompt, "/diopion_burst", 13) == 0) {
                if (g_diopion.mode == DIOPION_MODE_OFF) {
                    Print(L"\r\nERROR: Diopion is off (use /diopion_on)\r\n\r\n");
                    continue;
                }

                const char *p = prompt + 13;
                while (*p == ' ' || *p == '\t') p++;

                // Args: [turns] [temp_milli] [top_k] [max_tokens]
                UINT32 turns = g_diopion.params.burst_turns_default;
                UINT32 temp_milli = g_diopion.params.burst_temp_milli;
                UINT32 topk = g_diopion.params.burst_top_k;
                UINT32 max_tokens = g_diopion.params.burst_max_gen_tokens;

                int argc = 0;
                while (*p && argc < 4) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == 0) break;
                    UINT32 v = 0;
                    int any = 0;
                    while (*p >= '0' && *p <= '9') { v = v * 10u + (UINT32)(*p - '0'); p++; any = 1; }
                    if (!any) break;
                    if (argc == 0) turns = v;
                    else if (argc == 1) temp_milli = v;
                    else if (argc == 2) topk = v;
                    else if (argc == 3) max_tokens = v;
                    argc++;
                }

                if (turns < 1) turns = 1;
                if (turns > 16) turns = 16;
                if (temp_milli < 50) temp_milli = 50;
                if (temp_milli > 2000) temp_milli = 2000;
                if (topk < 1) topk = 1;
                if (topk > 200) topk = 200;
                if (max_tokens < 16) max_tokens = 16;
                if (max_tokens > 1024) max_tokens = 1024;

                llmk_diopion_burst_apply(turns, max_tokens, topk, temp_milli, &max_gen_tokens, &top_k, &temperature);
                g_diopion.bursts_started++;

                Print(L"\r\nOK: burst turns=%d temp=%d.%03d top_k=%d max_tokens=%d\r\n\r\n",
                      (int)turns,
                      (int)(temp_milli / 1000u),
                      (int)(temp_milli % 1000u),
                      (int)topk,
                      (int)max_tokens);
                continue;
            } else if (my_strncmp(prompt, "/commands", 9) == 0) {
                char pref[64];
                pref[0] = 0;
                llmk_parse_optional_prefix(prompt, 9, pref, (int)sizeof(pref));

                Print(L"\r\nCommands:\r\n");
                if (pref[0]) {
                    Print(L"  (filter: ");
                    llmk_print_ascii(pref);
                    Print(L")\r\n");
                }
                llmk_print_commands_filtered(pref[0] ? pref : NULL);
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/cls", 4) == 0) {
                uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
                continue;
            } else if (my_strncmp(prompt, "/logo", 5) == 0) {
                llmk_print_logo();
                continue;
            } else if (my_strncmp(prompt, "/blas_bench", 11) == 0) {
                Print(L"\r\nRunning DjibLAS Benchmark (256x256)...\r\n");
                int M=256, N=256, K=256;
                // Use simple_alloc (monotonic) - suitable for a test command if memory allows
                float *A = (float*)simple_alloc(M*K*sizeof(float));
                float *B = (float*)simple_alloc(K*N*sizeof(float));
                float *C_sc = (float*)simple_alloc(M*N*sizeof(float));
                float *C_avx = (float*)simple_alloc(M*N*sizeof(float));

                if (!A || !B || !C_sc || !C_avx) {
                    Print(L"Benchmark aborted: Alloc failed\r\n");
                    continue;
                }

                // Init with deterministic values
                for(int i=0; i<M*K; i++) A[i] = (float)((i % 17) - 8) * 0.1f;
                for(int i=0; i<K*N; i++) B[i] = (float)((i % 19) - 9) * 0.1f;

                // 1. Scalar Baseline
                unsigned long long t0 = rdtsc();
                djiblas_sgemm_scalar(M, N, K, A, K, B, N, C_sc, N);
                unsigned long long t_scalar = rdtsc() - t0;
                Print(L"Scalar: %lu cycles\r\n", t_scalar);

                // 2. AVX2
                CPUFeatures f;
                djiblas_detect_cpu(&f);
                if (f.has_avx2 && f.has_fma) {
                    t0 = rdtsc();
                    djiblas_sgemm_avx2(M, N, K, A, K, B, N, C_avx, N);
                    unsigned long long t_avx = rdtsc() - t0;
                    
                    int speedup = (int)(t_scalar / t_avx);
                    int dec = (int)(((t_scalar * 10) / t_avx) % 10);
                    Print(L"AVX2:   %lu cycles (Speedup: %d.%dx)\r\n", t_avx, speedup, dec);

                    // Verify
                    float max_err = 0.0f;
                    for(int i=0; i<M*N; i++) {
                        float d = C_sc[i] - C_avx[i];
                        if (d < 0) d = -d;
                        if (d > max_err) max_err = d;
                    }
                    Print(L"Max Error: %d.%06d\r\n", (int)max_err, (int)((max_err - (int)max_err)*1000000));
                } else {
                    Print(L"AVX2:   Skipped (Not Supported)\r\n");
                }
                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/q8_bench", 9) == 0) {
                // Usage:
                //   /q8_bench           -> default n=d=256, reps=10
                //   /q8_bench <n> <d>   -> custom sizes (n must be multiple of 32)
                //   /q8_bench <n> <d> <reps>
                int n = 256;
                int d = 256;
                int reps = 10;

                int i = 9;
                while (prompt[i] == ' ') i++;
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    n = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { n = n * 10 + (prompt[i] - '0'); i++; }
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    d = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { d = d * 10 + (prompt[i] - '0'); i++; }
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    reps = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { reps = reps * 10 + (prompt[i] - '0'); i++; }
                }
                if (reps < 1) reps = 1;
                if (reps > 100) reps = 100;

                if ((n % 32) != 0 || n <= 0 || d <= 0) {
                    Print(L"\r\nUsage: /q8_bench [n multiple-of-32] [d] [reps]\r\n\r\n");
                    continue;
                }

                Print(L"\r\nRunning Q8_0 matmul benchmark (n=%d d=%d reps=%d)...\r\n", n, d, reps);

                UINT64 row_bytes = llmk_q8_0_row_bytes(n);
                if (row_bytes == 0) {
                    Print(L"ERROR: invalid Q8 row_bytes\r\n\r\n");
                    continue;
                }

                float *x = (float*)simple_alloc((UINTN)n * sizeof(float));
                UINT8 *wq8 = (UINT8*)simple_alloc((UINTN)d * (UINTN)row_bytes);
                float *y_sc = (float*)simple_alloc((UINTN)d * sizeof(float));
                float *y_avx = (float*)simple_alloc((UINTN)d * sizeof(float));
                if (!x || !wq8 || !y_sc || !y_avx) {
                    Print(L"Benchmark aborted: Alloc failed\r\n\r\n");
                    continue;
                }

                // Init deterministic input vector
                for (int j = 0; j < n; j++) {
                    x[j] = (float)(((j * 13) % 97) - 48) * 0.01f;
                }

                // Init deterministic Q8_0 weights: fp16 scale = 1.0 (0x3C00) and int8 values.
                // Layout per block: [u16 d][32 i8 qs] = 34 bytes.
                for (int r = 0; r < d; r++) {
                    UINT8 *row = wq8 + (UINTN)r * (UINTN)row_bytes;
                    UINT8 *p = row;
                    int nb = n / 32;
                    for (int b = 0; b < nb; b++) {
                        // d = 1.0f in fp16
                        p[0] = 0x00;
                        p[1] = 0x3C;
                        INT8 *qs = (INT8 *)(p + 2);
                        for (int k = 0; k < 32; k++) {
                            int v = (r * 31 + b * 17 + k * 7) & 255;
                            v -= 128;
                            if (v < -127) v = -127;
                            if (v > 127) v = 127;
                            qs[k] = (INT8)v;
                        }
                        p += 34;
                    }
                }

                // Scalar baseline
                unsigned long long best_sc = ~0ULL;
                for (int it = 0; it < reps; it++) {
                    unsigned long long t0 = rdtsc();
                    matmul_q8_0_scalar(y_sc, x, wq8, n, d);
                    unsigned long long dt = rdtsc() - t0;
                    if (dt < best_sc) best_sc = dt;
                }
                Print(L"Q8 scalar: %lu cycles (best of %d)\r\n", best_sc, reps);

                // AVX2 (if supported)
                CPUFeatures f;
                djiblas_detect_cpu(&f);
                if (f.has_avx2) {
                    unsigned long long best_avx = ~0ULL;
                    for (int it = 0; it < reps; it++) {
                        unsigned long long t0 = rdtsc();
                        if (g_cfg_q8_act_quant != 0) {
                            matmul_q8_0_avx2_i8(y_avx, x, wq8, n, d);
                        } else {
                            matmul_q8_0_avx2(y_avx, x, wq8, n, d);
                        }
                        unsigned long long dt = rdtsc() - t0;
                        if (dt < best_avx) best_avx = dt;
                    }

                    int speedup = (best_avx > 0) ? (int)(best_sc / best_avx) : 0;
                    int dec = (best_avx > 0) ? (int)(((best_sc * 10ULL) / best_avx) % 10ULL) : 0;
                    Print(L"Q8 AVX2%s:   %lu cycles (Speedup: %d.%dx)\r\n", (g_cfg_q8_act_quant != 0) ? L"(i8)" : L"", best_avx, speedup, dec);

                    // Verify (loose tolerance, since AVX path accumulates in float too).
                    float max_err = 0.0f;
                    for (int t = 0; t < d; t++) {
                        float diff = y_sc[t] - y_avx[t];
                        if (diff < 0) diff = -diff;
                        if (diff > max_err) max_err = diff;
                    }
                    Print(L"Max Error: %d.%06d\r\n", (int)max_err, (int)((max_err - (int)max_err) * 1000000));
                } else {
                    Print(L"Q8 AVX2:   Skipped (Not Supported)\r\n");
                }

                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/q8_matvec", 10) == 0) {
                // Bench a *real* model matrix-vector multiply when running in Q8_0 blob mode.
                // Usage:
                //   /q8_matvec                 -> wq layer0, reps=20
                //   /q8_matvec <name>          -> e.g. wq|wk|wv|wo|w1|w2|w3|cls
                //   /q8_matvec <name> <layer>  -> selects layer for per-layer matrices
                //   /q8_matvec <name> <layer> <reps>
                if (!g_llmk_ready) {
                    Print(L"\r\n  (llmk not ready)\r\n\r\n");
                    continue;
                }
                if (weights.kind != 1) {
                    Print(L"\r\nERROR: /q8_matvec requires GGUF Q8_0 blob mode (weights_kind=q8_0_blob).\r\n");
                    Print(L"Tip: set gguf_q8_blob=1 in repl.cfg and load a Q8_0 GGUF.\r\n\r\n");
                    continue;
                }

                char name[8];
                name[0] = 'w'; name[1] = 'q'; name[2] = 0;
                int layer = 0;
                int reps = 20;

                int i = 10;
                while (prompt[i] == ' ') i++;
                if (prompt[i] != 0) {
                    int n = 0;
                    while (prompt[i] && prompt[i] != ' ' && n + 1 < (int)sizeof(name)) {
                        name[n++] = prompt[i++];
                    }
                    name[n] = 0;
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    layer = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { layer = layer * 10 + (prompt[i] - '0'); i++; }
                    while (prompt[i] == ' ') i++;
                }
                if (prompt[i] >= '0' && prompt[i] <= '9') {
                    reps = 0;
                    while (prompt[i] >= '0' && prompt[i] <= '9') { reps = reps * 10 + (prompt[i] - '0'); i++; }
                }
                if (reps < 1) reps = 1;
                if (reps > 100) reps = 100;
                if (layer < 0) layer = 0;
                if (layer >= config.n_layers) layer = config.n_layers - 1;

                // Select matrix pointer + shape.
                const UINT8 *W = NULL;
                int n_in = 0;
                int d_out = 0;
                const char *kind = "";

                const int dim = config.dim;
                const int hidden_dim = config.hidden_dim;
                const int kv_dim = (dim * config.n_kv_heads) / config.n_heads;

                if (my_strncmp(name, "wq", 2) == 0) {
                    W = weights.wq_q8 + (UINTN)layer * (UINTN)weights.wq_layer_bytes;
                    n_in = dim; d_out = dim; kind = "wq";
                } else if (my_strncmp(name, "wk", 2) == 0) {
                    W = weights.wk_q8 + (UINTN)layer * (UINTN)weights.wk_layer_bytes;
                    n_in = dim; d_out = kv_dim; kind = "wk";
                } else if (my_strncmp(name, "wv", 2) == 0) {
                    W = weights.wv_q8 + (UINTN)layer * (UINTN)weights.wv_layer_bytes;
                    n_in = dim; d_out = kv_dim; kind = "wv";
                } else if (my_strncmp(name, "wo", 2) == 0) {
                    W = weights.wo_q8 + (UINTN)layer * (UINTN)weights.wo_layer_bytes;
                    n_in = dim; d_out = dim; kind = "wo";
                } else if (my_strncmp(name, "w1", 2) == 0) {
                    W = weights.w1_q8 + (UINTN)layer * (UINTN)weights.w1_layer_bytes;
                    n_in = dim; d_out = hidden_dim; kind = "w1";
                } else if (my_strncmp(name, "w2", 2) == 0) {
                    W = weights.w2_q8 + (UINTN)layer * (UINTN)weights.w2_layer_bytes;
                    n_in = hidden_dim; d_out = dim; kind = "w2";
                } else if (my_strncmp(name, "w3", 2) == 0) {
                    W = weights.w3_q8 + (UINTN)layer * (UINTN)weights.w3_layer_bytes;
                    n_in = dim; d_out = hidden_dim; kind = "w3";
                } else if (my_strncmp(name, "cls", 3) == 0) {
                    W = weights.wcls_q8;
                    n_in = dim; d_out = config.vocab_size; kind = "cls";
                } else {
                    Print(L"\r\nUsage: /q8_matvec [wq|wk|wv|wo|w1|w2|w3|cls] [layer] [reps]\r\n\r\n");
                    continue;
                }

                if (!W || n_in <= 0 || d_out <= 0) {
                    Print(L"\r\nERROR: matrix not available for %a\r\n\r\n", kind);
                    continue;
                }
                if ((n_in % 32) != 0) {
                    Print(L"\r\nERROR: Q8_0 matvec requires n multiple of 32 (n=%d)\r\n\r\n", n_in);
                    continue;
                }

                // Allocate input/output
                float *x = (float*)simple_alloc((UINTN)n_in * sizeof(float));
                float *y_sc = (float*)simple_alloc((UINTN)d_out * sizeof(float));
                float *y_avx = (float*)simple_alloc((UINTN)d_out * sizeof(float));
                if (!x || !y_sc || !y_avx) {
                    Print(L"\r\nERROR: alloc failed\r\n\r\n");
                    continue;
                }

                for (int t = 0; t < n_in; t++) {
                    x[t] = (float)(((t * 29) % 101) - 50) * 0.01f;
                }

                Print(L"\r\nQ8 matvec (%a", kind);
                if (kind[0] == 'w') Print(L" layer=%d", layer);
                Print(L") n=%d d=%d reps=%d\r\n", n_in, d_out, reps);

                unsigned long long best_sc = ~0ULL;
                for (int it = 0; it < reps; it++) {
                    unsigned long long t0 = rdtsc();
                    matmul_q8_0_scalar(y_sc, x, W, n_in, d_out);
                    unsigned long long dt = rdtsc() - t0;
                    if (dt < best_sc) best_sc = dt;
                }
                Print(L"Scalar: %lu cycles (%.2f cyc/out)\r\n", best_sc, (double)best_sc / (double)d_out);

                CPUFeatures f;
                djiblas_detect_cpu(&f);
                if (f.has_avx2) {
                    int allow_i8 = 0;
                    if (g_cfg_q8_act_quant == 1) {
                        allow_i8 = 1;
                    } else if (g_cfg_q8_act_quant == 2) {
                        // Hybrid mode: enable i8 dot only for FFN matrices.
                        if (kind[0] == 'w' && kind[2] == 0 && (kind[1] == '1' || kind[1] == '2' || kind[1] == '3')) {
                            allow_i8 = 1;
                        }
                    }
                    unsigned long long best_avx = ~0ULL;
                    for (int it = 0; it < reps; it++) {
                        unsigned long long t0 = rdtsc();
                        if (allow_i8) {
                            matmul_q8_0_avx2_i8(y_avx, x, W, n_in, d_out);
                        } else {
                            matmul_q8_0_avx2(y_avx, x, W, n_in, d_out);
                        }
                        unsigned long long dt = rdtsc() - t0;
                        if (dt < best_avx) best_avx = dt;
                    }
                    int speedup = (best_avx > 0) ? (int)(best_sc / best_avx) : 0;
                    int dec = (best_avx > 0) ? (int)(((best_sc * 10ULL) / best_avx) % 10ULL) : 0;
                    Print(L"AVX2%s:   %lu cycles (%.2f cyc/out, %d.%dx)\r\n", allow_i8 ? L"(i8)" : L"", best_avx, (double)best_avx / (double)d_out, speedup, dec);

                    float max_err = 0.0f;
                    for (int t = 0; t < d_out; t++) {
                        float diff = y_sc[t] - y_avx[t];
                        if (diff < 0) diff = -diff;
                        if (diff > max_err) max_err = diff;
                    }
                    Print(L"Max Error: %d.%06d\r\n", (int)max_err, (int)((max_err - (int)max_err) * 1000000));
                } else {
                    Print(L"AVX2:   Skipped (Not Supported)\r\n");
                }

                Print(L"\r\n");
                continue;
            } else if (my_strncmp(prompt, "/help", 5) == 0) {
                char pref[64];
                pref[0] = 0;
                llmk_parse_optional_prefix(prompt, 5, pref, (int)sizeof(pref));

                llmk_print_help_filtered(
                    pref[0] ? pref : NULL,
                    temperature, min_p, top_p,
                    top_k, no_repeat_ngram, max_gen_tokens,
                    stats_enabled, stop_on_you, stop_on_double_nl,
                    repeat_penalty
                );
                continue;
            } else if (my_strncmp(prompt, "/metrics", 8) == 0) {
                // Export runtime metrics to LLMK_METRICS.LOG (JSON format)
                EFI_FILE_HANDLE metrics_file = NULL;
                EFI_STATUS metrics_st = llmk_open_binary_file(&metrics_file, L"LLMK_METRICS.LOG");

                if (!EFI_ERROR(metrics_st) && metrics_file) {
                    // Build JSON string manually (no sprintf in UEFI)
                    char json_buf[2048];
                    int jpos = 0;

                    // Helper macro to append string
                    #define JAPPEND(s) do { const char *_s = (s); while (*_s && jpos < (int)sizeof(json_buf)-1) json_buf[jpos++] = *_s++; } while(0)
                    #define JAPPEND_U64(label, val) do { \
                        JAPPEND("  \""); JAPPEND(label); JAPPEND("\": "); \
                        char _tmp[32]; llmk_u64_to_str(val, _tmp, sizeof(_tmp)); JAPPEND(_tmp); JAPPEND(",\n"); \
                    } while(0)

                    JAPPEND("{\n");
                    JAPPEND_U64("session_start_cycles", g_metrics.session_start_cycles);
                    JAPPEND_U64("total_prefill_cycles", g_metrics.total_prefill_cycles);
                    JAPPEND_U64("total_decode_cycles", g_metrics.total_decode_cycles);
                    JAPPEND_U64("total_prefill_tokens", g_metrics.total_prefill_tokens);
                    JAPPEND_U64("total_decode_tokens", g_metrics.total_decode_tokens);
                    JAPPEND_U64("total_prefill_calls", g_metrics.total_prefill_calls);
                    JAPPEND_U64("total_decode_calls", g_metrics.total_decode_calls);
                    JAPPEND_U64("last_prefill_cycles", g_metrics.last_prefill_cycles);
                    JAPPEND_U64("last_decode_cycles", g_metrics.last_decode_cycles);
                    JAPPEND_U64("last_prefill_tokens", g_metrics.last_prefill_tokens);
                    JAPPEND_U64("last_decode_tokens", g_metrics.last_decode_tokens);
                    JAPPEND_U64("sentinel_violations_total", g_metrics.sentinel_violations_total);
                    JAPPEND_U64("kv_cache_resets", g_metrics.kv_cache_resets);
                    JAPPEND_U64("generation_count", g_metrics.generation_count);

                    // Remove trailing comma + newline before closing brace
                    if (jpos >= 2 && json_buf[jpos-2] == ',' && json_buf[jpos-1] == '\n') {
                        jpos -= 2;
                    }
                    JAPPEND("\n}\n");
                    json_buf[jpos] = 0;

                    #undef JAPPEND
                    #undef JAPPEND_U64

                    metrics_st = llmk_file_write_bytes(metrics_file, json_buf, (UINTN)jpos);
                    uefi_call_wrapper(metrics_file->Close, 1, metrics_file);

                    if (!EFI_ERROR(metrics_st)) {
                        Print(L"✅ Metrics exported to LLMK_METRICS.LOG (%d bytes)\r\n", jpos);
                    } else {
                        Print(L"⚠️  Metrics file write failed (status=%lx)\r\n", metrics_st);
                    }
                } else {
                    Print(L"⚠️  Cannot open LLMK_METRICS.LOG for writing (status=%lx)\r\n", metrics_st);
                }
                continue;
            }
        }
        
        // Encode prompt
        // For normal chat turns, wrap input so the model sees explicit roles.
        // Keep /commands and capture-mode prompts untouched.
        const char *encode_text = prompt;
        char model_prompt[1024];
        model_prompt[0] = 0;
        if (!g_capture_mode && !draw_mode && prompt[0] != '/' && prompt[0] != 0) {
            encode_text = llmk_build_chat_prompt(model_prompt, (int)sizeof(model_prompt), prompt, kv_pos);
        }

        int prompt_tokens[384];
        int n_prompt_tokens = 0;
        encode((char *)encode_text, prompt_tokens, &n_prompt_tokens, (int)(sizeof(prompt_tokens) / sizeof(prompt_tokens[0])), &tokenizer);

        // Avoid injecting BOS into the middle of an ongoing conversation.
        if (kv_pos > 0 && n_prompt_tokens > 0 && prompt_tokens[0] == TOKEN_BOS) {
            for (int i = 1; i < n_prompt_tokens; i++) prompt_tokens[i - 1] = prompt_tokens[i];
            n_prompt_tokens--;
        }
        
        // Check if KV cache will overflow
        if (kv_pos + n_prompt_tokens + max_gen_tokens > config.seq_len) {
            Print(L"\r\nWARNING: context too long (%d + %d tokens), clearing KV cache\r\n", 
                  kv_pos, n_prompt_tokens + max_gen_tokens);
            reset_kv_cache(&state, &config);
            kv_pos = 0;
            g_llmk_kv_pos = kv_pos;
        }
        
        if (!g_capture_mode) {
            Print(L"AI: ");
        }

        if (g_llmk_ready) {
            // Reset per-generation overrun counters and print current budget state.
            g_budget_overruns_prefill = 0;
            g_budget_overruns_decode = 0;
            if (!g_capture_mode) {
                Print(L"\r\n[llmk][budget] prefill_max=%lu decode_max=%lu\r\n",
                      g_budget_prefill_cycles, g_budget_decode_cycles);
            }
        }
        
        // Process prompt tokens through model first (prefill)
        for (int i = 0; i < n_prompt_tokens; i++) {
            int pos = kv_pos + i;  // Use persistent KV position
            if (g_llmk_ready) {
                // Per-token prefill budgeting (pos-dependent): set budget before each forward.
                if (g_budget_prefill_cycles == 0) {
                    // Start huge to ensure we get a first measurement without tripping.
                    // llmk_budget_update() will snap down quickly after the first dt sample.
                    g_budget_prefill_cycles = 100000000000ULL;
                }
                g_sentinel.cfg.max_cycles_prefill = g_budget_prefill_cycles;
                llmk_sentinel_phase_start(&g_sentinel, LLMK_PHASE_PREFILL);
                transformer_forward(&state, &weights, &config, prompt_tokens[i], pos);
                BOOLEAN ok = llmk_sentinel_phase_end(&g_sentinel);
                if (g_sentinel.tripped) {
                    Print(L"\r\n[llmk] prefill stopped (fail-safe) at i=%d\r\n", i);
                    llmk_print_ctx(&config, model_filename, kv_pos, temperature, min_p, top_p, top_k, no_repeat_ngram, repeat_penalty, max_gen_tokens);
                    llmk_zones_print(&g_zones);
                    llmk_sentinel_print_status(&g_sentinel);
                    llmk_print_log(32);

                    // Best-effort: persist dump to file for offline diagnosis.
                    {
                        EFI_FILE_HANDLE f = NULL;
                        EFI_STATUS st = llmk_open_text_file(&f, L"llmk-failsafe.txt");
                        if (!EFI_ERROR(st)) {
                            llmk_file_write_u16(f, L"FAIL-SAFE: prefill\r\n\r\n");
                            llmk_dump_zones_to_file(f, &g_zones);
                            llmk_dump_sentinel_to_file(f, &g_sentinel);
                            if (g_llmk_log.capacity) llmk_dump_log_to_file(f, &g_llmk_log, 128);
                            uefi_call_wrapper(f->Flush, 1, f);
                            uefi_call_wrapper(f->Close, 1, f);
                            Print(L"[llmk] wrote llmk-failsafe.txt\r\n");
                        }
                    }
                    if (g_test_failsafe_active) {
                        g_sentinel.cfg.strict_budget = g_test_failsafe_prev_strict_budget;
                        g_budget_prefill_cycles = g_test_failsafe_prev_prefill;
                        g_budget_decode_cycles = g_test_failsafe_prev_decode;
                        g_test_failsafe_active = 0;
                        Print(L"[test] fail-safe test complete (restored)\r\n");
                    }
                    break;
                }
                if (!ok) {
                    // Non-fatal budget overrun: adapt budget upward and continue.
                    g_budget_overruns_prefill++;
                    if (g_budget_overruns_prefill <= 3) {
                        Print(L"\r\n[llmk][budget] prefill overrun i=%d cycles=%lu max=%lu (auto-raise)\r\n",
                              i, g_sentinel.last_dt_cycles, g_sentinel.last_budget_cycles);
                    }
                }
                llmk_budget_update(&g_budget_prefill_cycles, g_sentinel.last_dt_cycles);
            } else {
                transformer_forward(&state, &weights, &config, prompt_tokens[i], i);
            }
        }
        
        // Start generation from the last prompt token.
        // After prefill, state.logits already corresponds to the last prompt token at position (n_prompt_tokens-1).
        int next;
        int token = prompt_tokens[n_prompt_tokens - 1];
        int pos = kv_pos + n_prompt_tokens - 1;  // Use persistent KV position
        
        int generated_count = 0;
        int repeat_count = 0;
        int last_token = -1;
        int immediate_repeat_count = 0;
        int loop_escape_used = 0; // count (budgeted) rather than boolean
        int repeat_escape_used = 0; // count (budgeted)

        // Record why generation stopped early (useful for GGUF vs BIN debugging).
        const CHAR16 *stop_reason = NULL;
        int stop_token = -1;
        int stop_step = -1;
        int stop_pos = -1;
        
        // Track context for repetition penalty and loop detection.
        int context_tokens[384 + MAX_TOKENS];
        int n_context_tokens = 0;
        for (int i = 0; i < n_prompt_tokens && n_context_tokens < (int)(sizeof(context_tokens) / sizeof(context_tokens[0])); i++) {
            context_tokens[n_context_tokens++] = prompt_tokens[i];
        }

        // Simple stop detection on the last bytes printed.
        char out_tail[64];
        int out_tail_len = 0;
        for (int i = 0; i < 64; i++) out_tail[i] = 0;

        unsigned long long gen_t0 = 0;
        unsigned long long gen_wall0_us = 0;
        int gen_have_wall = 0;
        if (stats_enabled) {
            calibrate_tsc_once();
            gen_t0 = rdtsc();
            gen_have_wall = uefi_wall_us(&gen_wall0_us);
        }

        // TUI: show live generation progress (skip /draw to avoid scribbling over images).
        if (!draw_mode) {
            g_tui_gen_active = 1;
            g_tui_gen_tokens = 0;
            if (g_tui_enabled && g_gop_fb32) {
                g_tui_dirty = 1;
                llmk_tui_redraw_best_effort();
            }
        }

        for (int step = 0; step < max_gen_tokens; step++) {
            // We sample from the logits produced by the previous forward pass.
            // For step==0, logits come from the final prompt token (prefill).

            // Apply no-repeat ngram blocking (works on pre-softmax logits).
            if (no_repeat_ngram > 1) {
                apply_no_repeat_ngram(state.logits, config.vocab_size, context_tokens, n_context_tokens, no_repeat_ngram);
            }

            // Sample next token (temperature/top_p/top_k + repetition penalty)
            int n_recent = n_context_tokens;
            if (n_recent > 64) n_recent = 64;
            int* recent = (n_recent > 0) ? &context_tokens[n_context_tokens - n_recent] : (int*)0;

            // Loop escapes:
            // - if we detect a short repeating suffix, ban the sampled token and resample (budgeted).
            // - if we are stuck repeating the same token too many times, ban it once and resample.
            for (int attempt = 0; attempt < 3; attempt++) {
                next = sample_advanced(state.logits, config.vocab_size, temperature, min_p, top_p, top_k, recent, n_recent, repeat_penalty);
                if (next == TOKEN_EOS || next == TOKEN_BOS) break;

                // Prevent premature termination on small models that briefly get stuck repeating one token.
                // If we've already repeated the last token 5 times and would do it again, ban it once and resample.
                if (repeat_escape_used < 8 && next == last_token && repeat_count >= 5) {
                    repeat_escape_used++;
                    state.logits[next] = -1.0e9f;
                    continue;
                }

                if (loop_escape_used < 8 && n_context_tokens + 1 < (int)(sizeof(context_tokens) / sizeof(context_tokens[0]))) {
                    context_tokens[n_context_tokens] = next;
                    int would_repeat = has_suffix_repeat(context_tokens, n_context_tokens + 1, 8) ||
                                      has_suffix_repeat(context_tokens, n_context_tokens + 1, 12) ||
                                      has_suffix_repeat(context_tokens, n_context_tokens + 1, 16);
                    if (would_repeat) {
                        loop_escape_used++;
                        state.logits[next] = -1.0e9f;
                        continue;
                    }
                }
                break;
            }
            
            // Check for EOS (some exports may still emit BOS; treat both as stop)
            if (next == TOKEN_EOS || next == TOKEN_BOS) {
                if (!stop_reason) {
                    stop_reason = L"eos/bos";
                    stop_token = next;
                    stop_step = step;
                    stop_pos = pos;
                }
                break;
            }

            // Track immediate repeats (useful as a cheap repetition signal).
            if (next == token) immediate_repeat_count++;
            
            // Check if stuck on same token (per conversation)
            if (next == last_token) {
                repeat_count++;
            } else {
                repeat_count = 0;
                last_token = next;
            }
            
            // Print token (or capture token output for /draw)
            if (next >= 0 && next < config.vocab_size && tokenizer.vocab[next]) {
                char* piece = tokenizer.vocab[next];
                int len = my_strlen(piece);
                if (len > 0) {
                    if (g_capture_mode) {
                        llmk_capture_append_ascii(piece, len);
                    } else {
                        uefi_print_utf8_bytes(piece, len);
                    }
                    generated_count++;
                    
                    // Update UI (simple overlay if enabled)
                    if ((step % 2) == 0) {
                         InterfaceFx_Tick(); 
                    }

                    if (!draw_mode) {
                        g_tui_gen_tokens = generated_count;
                        int mask = (g_ui_mode == 0) ? 15 : 63;
                        if (g_tui_enabled && g_gop_fb32 && ((generated_count & mask) == 0)) {
                            // Throttle redraws to keep overhead low.
                            g_tui_dirty = 1;
                            llmk_tui_redraw_best_effort();
                        }
                    }

                    // Update ASCII tail buffer for stop detection.
                    for (int k = 0; k < len; k++) {
                        char ch = piece[k];
                        if (out_tail_len < (int)sizeof(out_tail) - 1) {
                            out_tail[out_tail_len++] = ch;
                            out_tail[out_tail_len] = 0;
                        } else {
                            // shift left by 1
                            for (int s = 0; s < (int)sizeof(out_tail) - 2; s++) out_tail[s] = out_tail[s + 1];
                            out_tail[(int)sizeof(out_tail) - 2] = ch;
                            out_tail[(int)sizeof(out_tail) - 1] = 0;
                        }
                    }

                    // Stop conditions
                    if (stop_on_double_nl) {
                        // Look for "\n\n" in tail.
                        for (int i = 0; i + 1 < out_tail_len; i++) {
                            if (out_tail[i] == '\n' && out_tail[i + 1] == '\n') {
                                if (!stop_reason) {
                                    stop_reason = L"stop_double_nl";
                                    stop_token = next;
                                    stop_step = step;
                                    stop_pos = pos;
                                }
                                step = max_gen_tokens; // force exit
                                break;
                            }
                        }
                    }
                    if (stop_on_you) {
                        // Look for "\nYou:" in tail.
                        for (int i = 0; i + 4 < out_tail_len; i++) {
                            if (out_tail[i] == '\n' && out_tail[i + 1] == 'Y' && out_tail[i + 2] == 'o' && out_tail[i + 3] == 'u' && out_tail[i + 4] == ':') {
                                if (!stop_reason) {
                                    stop_reason = L"stop_you";
                                    stop_token = next;
                                    stop_step = step;
                                    stop_pos = pos;
                                }
                                step = max_gen_tokens; // force exit
                                break;
                            }
                        }
                    }
                }
            }

            // Append to context and apply a simple loop-stop heuristic.
            if (n_context_tokens < (int)(sizeof(context_tokens) / sizeof(context_tokens[0]))) {
                context_tokens[n_context_tokens++] = next;
            }
            // Loop heuristic (suffix repeat): do not hard-stop.
            // Under small GGUF models this can trigger very early and mask real generation.
            // The decode budget is already bounded by max_gen_tokens, and we also have
            // loop-escape resampling + repetition penalty.
            // (no-op)
            
            // Advance position and compute next logits
            token = next;
            pos++;
            if (pos >= config.seq_len) {
                if (!stop_reason) {
                    stop_reason = L"seq_len";
                    stop_token = next;
                    stop_step = step;
                    stop_pos = pos;
                }
                break;
            }

            if (g_llmk_ready) {
                if (g_budget_decode_cycles == 0) {
                    g_budget_decode_cycles = 100000000000ULL;
                }
                g_sentinel.cfg.max_cycles_decode = g_budget_decode_cycles;
                llmk_sentinel_phase_start(&g_sentinel, LLMK_PHASE_DECODE);
                transformer_forward(&state, &weights, &config, token, pos);
                BOOLEAN ok = llmk_sentinel_phase_end(&g_sentinel);
                if (g_sentinel.tripped) {
                    Print(L"\r\n[llmk] decode stopped (fail-safe) at step=%d pos=%d\r\n", step, pos);
                    if (!stop_reason) {
                        stop_reason = L"sentinel_decode";
                        stop_token = token;
                        stop_step = step;
                        stop_pos = pos;
                    }
                    llmk_print_ctx(&config, model_filename, kv_pos, temperature, min_p, top_p, top_k, no_repeat_ngram, repeat_penalty, max_gen_tokens);
                    llmk_zones_print(&g_zones);
                    llmk_sentinel_print_status(&g_sentinel);
                    llmk_print_log(32);

                    // Best-effort: persist dump to file for offline diagnosis.
                    {
                        EFI_FILE_HANDLE f = NULL;
                        EFI_STATUS st = llmk_open_text_file(&f, L"llmk-failsafe.txt");
                        if (!EFI_ERROR(st)) {
                            llmk_file_write_u16(f, L"FAIL-SAFE: decode\r\n\r\n");
                            llmk_dump_zones_to_file(f, &g_zones);
                            llmk_dump_sentinel_to_file(f, &g_sentinel);
                            if (g_llmk_log.capacity) llmk_dump_log_to_file(f, &g_llmk_log, 128);
                            uefi_call_wrapper(f->Flush, 1, f);
                            uefi_call_wrapper(f->Close, 1, f);
                            Print(L"[llmk] wrote llmk-failsafe.txt\r\n");
                        }
                    }
                    if (g_test_failsafe_active) {
                        g_sentinel.cfg.strict_budget = g_test_failsafe_prev_strict_budget;
                        g_budget_prefill_cycles = g_test_failsafe_prev_prefill;
                        g_budget_decode_cycles = g_test_failsafe_prev_decode;
                        g_test_failsafe_active = 0;
                        Print(L"[test] fail-safe test complete (restored)\r\n");
                    }
                    break;
                }
                if (!ok) {
                    g_budget_overruns_decode++;
                    if (g_budget_overruns_decode <= 3) {
                        Print(L"\r\n[llmk][budget] decode overrun step=%d pos=%d cycles=%lu max=%lu (auto-raise)\r\n",
                              step, pos, g_sentinel.last_dt_cycles, g_sentinel.last_budget_cycles);
                    }
                }
                llmk_budget_update(&g_budget_decode_cycles, g_sentinel.last_dt_cycles);
            } else {
                transformer_forward(&state, &weights, &config, token, pos);
            }
        }

        // Emit early-stop reason to serial for automated diagnosis.
        // Only when we stopped before using the full token budget.
        if (!g_capture_mode && stop_reason && generated_count < max_gen_tokens) {
            CHAR16 smsg[160];
            SPrint(smsg, sizeof(smsg), L"[stop] reason=%s tok=%d step=%d pos=%d\r\n",
                   (CHAR16 *)stop_reason, stop_token, stop_step, stop_pos);
            llmk_serial_write_char16(smsg);
        }

        // Flush any pending bytes held for mojibake repair across token boundaries.
        if (!g_capture_mode) {
            uefi_print_utf8_flush();
        }

        if (!draw_mode) {
            g_tui_gen_active = 0;
            if (g_tui_enabled && g_gop_fb32) {
                g_tui_dirty = 1;
                llmk_tui_redraw_best_effort();
            }
        }

        if (g_test_failsafe_active) {
            g_sentinel.cfg.strict_budget = g_test_failsafe_prev_strict_budget;
            g_budget_prefill_cycles = g_test_failsafe_prev_prefill;
            g_budget_decode_cycles = g_test_failsafe_prev_decode;
            g_test_failsafe_active = 0;
            Print(L"\r\n[test] fail-safe test cancelled (no trip; restored)\r\n");
        }

        if (g_llmk_ready && !g_capture_mode) {
            Print(L"\r\n[llmk][budget] final prefill_max=%lu decode_max=%lu overruns(p=%d d=%d)\r\n",
                  g_budget_prefill_cycles,
                  g_budget_decode_cycles,
                  (int)g_budget_overruns_prefill,
                  (int)g_budget_overruns_decode);
        }

        // Emit a serial-visible marker so automated QEMU tests can prove generation happened,
        // even when token text is not routed to the serial console.
        if (!g_capture_mode) {
            CHAR16 msg[96];
            SPrint(msg, sizeof(msg), L"[gen] tokens=%d\r\n", generated_count);
            llmk_serial_write_char16(msg);

            {
                const CHAR16 *obs_reason = stop_reason ? stop_reason : L"max_tokens";
                CHAR16 omsg[224];
                SPrint(omsg, sizeof(omsg),
                       L"[obs] gen_end tokens=%d reason=%s step=%d pos=%d repeat_escape=%d loop_escape=%d overrun_d=%d\r\n",
                       generated_count,
                       (CHAR16 *)obs_reason,
                       stop_step,
                       stop_pos,
                       repeat_escape_used,
                       loop_escape_used,
                       (int)g_budget_overruns_decode);
                llmk_serial_write_char16(omsg);
            }
        }

        if (stats_enabled && !g_capture_mode) {
            unsigned long long gen_t1 = rdtsc();
            unsigned long long dt = (gen_t1 > gen_t0) ? (gen_t1 - gen_t0) : 0;

            // Prefer wall-clock timing when available (more stable under emulation).
            if (gen_have_wall) {
                unsigned long long gen_wall1_us = 0;
                if (uefi_wall_us(&gen_wall1_us)) {
                    unsigned long long wall_dt_us = (gen_wall1_us >= gen_wall0_us) ? (gen_wall1_us - gen_wall0_us)
                                                                                   : (gen_wall1_us + 86400ULL * 1000000ULL - gen_wall0_us);
                    unsigned long long ms = wall_dt_us / 1000ULL;
                    if (wall_dt_us == 0) {
                        Print(L"\r\n[stats] tokens=%d time_ms=%d tok_s=inf\r\n", generated_count, (int)ms);
                    } else {
                        unsigned long long tps_milli = ((unsigned long long)generated_count * 1000000ULL * 1000ULL) / wall_dt_us;
                        unsigned long long tps_int = tps_milli / 1000ULL;
                        unsigned long long tps_frac = tps_milli % 1000ULL;
                        Print(L"\r\n[stats] tokens=%d time_ms=%d tok_s=%d.%03d\r\n",
                              generated_count, (int)ms, (int)tps_int, (int)tps_frac);
                    }
                    goto stats_done;
                }
            }

            // Fallback to TSC-based estimate.
            if (tsc_per_sec == 0 || dt == 0) {
                Print(L"\r\n[stats] tokens=%d cycles=%d\r\n", generated_count, (int)dt);
            } else {
                unsigned long long ms = (dt * 1000ULL) / tsc_per_sec;
                // milli tok/s for visibility even when < 1 tok/s
                unsigned long long tps_milli = ((unsigned long long)generated_count * tsc_per_sec * 1000ULL) / dt;
                unsigned long long tps_int = tps_milli / 1000ULL;
                unsigned long long tps_frac = tps_milli % 1000ULL;
                Print(L"\r\n[stats] tokens=%d time_ms=%d tok_s=%d.%03d\r\n",
                      generated_count, (int)ms, (int)tps_int, (int)tps_frac);
            }
stats_done:
            ;
        }
        
        // M16.1: Track completed generation
        g_metrics.generation_count++;

        // Diopion burst: decrement remaining and restore knobs when done.
        llmk_diopion_burst_finish_one(&max_gen_tokens, &top_k, &temperature);

        // Calibrion: feed basic stats after each non-capture generation.
        // Keep it simple and cheap: tokens_generated + immediate repeats, entropy is a neutral placeholder.
        if (!g_capture_mode && !draw_mode) {
            calibrion_feed(&g_calibrion,
                           (uint32_t)generated_count,
                           (uint32_t)immediate_repeat_count,
                           1000 /* entropy_milli (neutral) */);

            if (g_calibrion.mode == CALIBRION_MODE_ENFORCE) {
                uint32_t t, k, p;
                calibrion_get_recommendation(&g_calibrion, &t, &k, &p);
                temperature = (float)t / 1000.0f;
                top_k = (int)k;
                top_p = (float)p / 1000.0f;
            }
        }

        // If capture mode was active, handle it now.
        if (g_capture_mode) {
            llmk_capture_sanitize_inplace();

            if (capture_kind == 1) {
                llmk_apply_simple_autocorrect(g_capture_buf);
                Print(L"\r\n[draw] captured %d chars%s\r\n", g_capture_len, g_capture_truncated ? L" (truncated)" : L"");
                if (g_capture_len == 0) {
                    Print(L"[draw] ERROR: empty output\r\n\r\n");
                } else {
                    int ok = llmk_render_scene_dsl_ex(g_capture_buf, 0);
                    if (ok) {
                        llmk_gop_force_update();
                        Print(L"[draw] OK: rendered (check screen above, use /save_img to export)\r\n\r\n");
                    } else {
                        // The stories model often outputs prose. Render a fallback so the user sees something.
                        llmk_draw_fallback_center_square(1);
                        llmk_gop_force_update();

                        CHAR16 msg[140];
                        ascii_to_char16(msg, g_last_dsl_error, (int)(sizeof(msg) / sizeof(msg[0])));
                        Print(L"[draw] WARNING: model output was not valid DSL (%s)\r\n", msg);
                        Print(L"[draw] Rendered fallback: black background + centered white square\r\n\r\n");
                    }
                }
            } else if (capture_kind == 2) {
                if (oo_think_id > 0) {
                    char n1[320];
                    int p1 = 0;
                    const char *h1 = "think: ";
                    for (int k = 0; h1[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = h1[k];
                    for (int k = 0; oo_think_user[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = oo_think_user[k];
                    n1[p1] = 0;
                    llmk_oo_note(oo_think_id, n1);

                    char n2[640];
                    int p2 = 0;
                    const char *h2 = "answer: ";
                    for (int k = 0; h2[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = h2[k];
                    for (int k = 0; g_capture_buf[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = g_capture_buf[k];
                    n2[p2] = 0;
                    llmk_oo_note(oo_think_id, n2);
                    llmk_oo_digest(oo_think_id);

                    Print(L"\r\n[oo] stored thought for entity id=%d (%d chars%s)\r\n\r\n",
                          oo_think_id, g_capture_len, g_capture_truncated ? L"; truncated" : L"");
                } else {
                    Print(L"\r\n[oo] ERROR: internal think state\r\n\r\n");
                }
            } else if (capture_kind == 3) {
                if (oo_think_id > 0) {
                    // Store the cycle's prompt + answer.
                    char n1[320];
                    int p1 = 0;
                    const char *h1 = "auto: ";
                    for (int k = 0; h1[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = h1[k];
                    for (int k = 0; oo_think_user[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = oo_think_user[k];
                    n1[p1] = 0;
                    llmk_oo_note(oo_think_id, n1);

                    char n2[640];
                    int p2 = 0;
                    const char *h2 = "answer: ";
                    for (int k = 0; h2[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = h2[k];
                    for (int k = 0; g_capture_buf[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = g_capture_buf[k];
                    n2[p2] = 0;
                    llmk_oo_note(oo_think_id, n2);

                    if (oo_auto_planning) {
                        // Planning cycle: extract first line as an action and push to agenda.
                        char act[96];
                        int ap = 0;
                        int si = 0;
                        while (g_capture_buf[si] == ' ' || g_capture_buf[si] == '\t' || g_capture_buf[si] == '\n') si++;
                        for (; g_capture_buf[si] && g_capture_buf[si] != '\n' && ap + 1 < (int)sizeof(act); si++) {
                            act[ap++] = g_capture_buf[si];
                        }
                        while (ap > 0 && (act[ap - 1] == ' ' || act[ap - 1] == '\t')) ap--;
                        act[ap] = 0;

                        if (act[0] && llmk_oo_agenda_add(oo_think_id, act)) {
                            CHAR16 a16[120];
                            ascii_to_char16(a16, act, (int)(sizeof(a16) / sizeof(a16[0])));
                            Print(L"\r\n[oo_auto] planned: %s\r\n\r\n", a16);
                            llmk_oo_digest(oo_think_id);
                            // Do NOT decrement remaining; next cycle will execute.
                        } else {
                            Print(L"\r\n[oo_auto] planning failed; stopping\r\n\r\n");
                            g_oo_auto_active = 0;
                            g_oo_auto_id = 0;
                            g_oo_auto_remaining = 0;
                            g_oo_auto_total = 0;
                            g_oo_auto_user[0] = 0;
                        }
                    } else {
                        // Execute cycle: advance entity and refresh digest.
                        llmk_oo_step(oo_think_id);
                        llmk_oo_digest(oo_think_id);

                        // If this cycle executed an agenda action, mark it DONE and log it.
                        if (oo_auto_action_k > 0) {
                            char done_note[196];
                            int dp = 0;
                            const char *h = "done: ";
                            for (int k = 0; h[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = h[k];
                            for (int k = 0; oo_think_user[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = oo_think_user[k];
                            done_note[dp] = 0;
                            llmk_oo_note(oo_think_id, done_note);
                            llmk_oo_action_set_state(oo_think_id, oo_auto_action_k, 2);
                        }

                        if (g_oo_auto_active && g_oo_auto_id == oo_think_id && g_oo_auto_remaining > 0) {
                            g_oo_auto_remaining--;
                            Print(L"\r\n[oo_auto] stored + stepped id=%d (%d chars%s); remaining=%d\r\n\r\n",
                                  oo_think_id, g_capture_len, g_capture_truncated ? L"; truncated" : L"", g_oo_auto_remaining);
                            if (g_oo_auto_remaining <= 0) {
                                Print(L"[oo_auto] done\r\n\r\n");
                                g_oo_auto_active = 0;
                                g_oo_auto_id = 0;
                                g_oo_auto_remaining = 0;
                                g_oo_auto_total = 0;
                                g_oo_auto_user[0] = 0;
                            }

                            // Optional autosave (repl.cfg: oo_autosave_every=N). Best-effort.
                            if (oo_autosave_every > 0 && oo_state_file[0]) {
                                int completed = 0;
                                if (g_oo_auto_total > 0) {
                                    completed = g_oo_auto_total - g_oo_auto_remaining;
                                }
                                if (completed > 0 && (completed % oo_autosave_every) == 0) {
                                    int nb = 0;
                                    EFI_STATUS st = llmk_oo_save_to_file_best_effort(oo_state_file, &nb);
                                    if (!EFI_ERROR(st)) {
                                        Print(L"[oo_autosave] saved %s (%d bytes)\r\n", oo_state_file, nb);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    Print(L"\r\n[oo_auto] ERROR: internal state\r\n\r\n");
                    g_oo_auto_active = 0;
                    g_oo_auto_id = 0;
                    g_oo_auto_remaining = 0;
                    g_oo_auto_total = 0;
                    g_oo_auto_user[0] = 0;
                }
            } else if (capture_kind == 4) {
                if (oo_think_id > 0) {
                    // Store the cycle's prompt + answer.
                    char n1[320];
                    int p1 = 0;
                    const char *h1 = "exec: ";
                    for (int k = 0; h1[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = h1[k];
                    for (int k = 0; oo_think_user[k] && p1 + 1 < (int)sizeof(n1); k++) n1[p1++] = oo_think_user[k];
                    n1[p1] = 0;
                    llmk_oo_note(oo_think_id, n1);

                    char n2[640];
                    int p2 = 0;
                    const char *h2 = "answer: ";
                    for (int k = 0; h2[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = h2[k];
                    for (int k = 0; g_capture_buf[k] && p2 + 1 < (int)sizeof(n2); k++) n2[p2++] = g_capture_buf[k];
                    n2[p2] = 0;
                    llmk_oo_note(oo_think_id, n2);

                    if (oo_exec_planning) {
                        // Planning cycle: extract first line as an action and push to agenda.
                        char act[96];
                        int ap = 0;
                        int si = 0;
                        while (g_capture_buf[si] == ' ' || g_capture_buf[si] == '\t' || g_capture_buf[si] == '\n') si++;
                        for (; g_capture_buf[si] && g_capture_buf[si] != '\n' && ap + 1 < (int)sizeof(act); si++) {
                            act[ap++] = g_capture_buf[si];
                        }
                        while (ap > 0 && (act[ap - 1] == ' ' || act[ap - 1] == '\t')) ap--;
                        act[ap] = 0;

                        if (act[0] && llmk_oo_agenda_add(oo_think_id, act)) {
                            CHAR16 a16[120];
                            ascii_to_char16(a16, act, (int)(sizeof(a16) / sizeof(a16[0])));
                            Print(L"\r\n[oo_exec] planned: %s\r\n\r\n", a16);
                            llmk_oo_digest(oo_think_id);
                            // Do NOT decrement remaining; next cycle will execute.
                        } else {
                            Print(L"\r\n[oo_exec] planning failed; stopping\r\n\r\n");
                            g_oo_exec_active = 0;
                            g_oo_exec_id = 0;
                            g_oo_exec_remaining = 0;
                            g_oo_exec_total = 0;
                            g_oo_exec_plan_if_empty = 0;
                            g_oo_exec_hint[0] = 0;
                        }
                    } else {
                        llmk_oo_step(oo_think_id);
                        llmk_oo_digest(oo_think_id);

                        if (oo_exec_action_k > 0) {
                            char done_note[196];
                            int dp = 0;
                            const char *h = "done: ";
                            for (int k = 0; h[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = h[k];
                            for (int k = 0; oo_think_user[k] && dp + 1 < (int)sizeof(done_note); k++) done_note[dp++] = oo_think_user[k];
                            done_note[dp] = 0;
                            llmk_oo_note(oo_think_id, done_note);
                            llmk_oo_action_set_state(oo_think_id, oo_exec_action_k, 2);
                        }

                        if (g_oo_exec_active && g_oo_exec_id == oo_think_id && g_oo_exec_remaining > 0) {
                            g_oo_exec_remaining--;
                            Print(L"\r\n[oo_exec] stored + stepped id=%d (%d chars%s); remaining=%d\r\n\r\n",
                                  oo_think_id, g_capture_len, g_capture_truncated ? L"; truncated" : L"", g_oo_exec_remaining);
                            if (g_oo_exec_remaining <= 0) {
                                Print(L"[oo_exec] done\r\n\r\n");
                                g_oo_exec_active = 0;
                                g_oo_exec_id = 0;
                                g_oo_exec_remaining = 0;
                                g_oo_exec_total = 0;
                                g_oo_exec_plan_if_empty = 0;
                                g_oo_exec_hint[0] = 0;
                            }

                            // Optional autosave (repl.cfg: oo_autosave_every=N). Best-effort.
                            if (oo_autosave_every > 0 && oo_state_file[0]) {
                                int completed = 0;
                                if (g_oo_exec_total > 0) {
                                    completed = g_oo_exec_total - g_oo_exec_remaining;
                                }
                                if (completed > 0 && (completed % oo_autosave_every) == 0) {
                                    int nb = 0;
                                    EFI_STATUS st = llmk_oo_save_to_file_best_effort(oo_state_file, &nb);
                                    if (!EFI_ERROR(st)) {
                                        Print(L"[oo_autosave] saved %s (%d bytes)\r\n", oo_state_file, nb);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    Print(L"\r\n[oo_exec] ERROR: internal state\r\n\r\n");
                    g_oo_exec_active = 0;
                    g_oo_exec_id = 0;
                    g_oo_exec_remaining = 0;
                    g_oo_exec_total = 0;
                    g_oo_exec_plan_if_empty = 0;
                    g_oo_exec_hint[0] = 0;
                }
            }

            // Disable capture mode and restore sampling flags.
            g_capture_mode = 0;
            llmk_capture_reset();
            stop_on_you = saved_stop_on_you;
            stop_on_double_nl = saved_stop_on_double_nl;
            max_gen_tokens = saved_max_gen_tokens;

            if (draw_saved_sampling) {
                temperature = saved_temperature;
                min_p = saved_min_p;
                top_p = saved_top_p;
                top_k = saved_top_k;
                repeat_penalty = saved_repeat_penalty;
            }
        }
        
        // Update persistent KV cache position for next generation
        kv_pos += n_prompt_tokens + generated_count;
        g_llmk_kv_pos = kv_pos;
        
        if (!g_capture_mode) {
            Print(L"\r\n\r\n");
        }
    }
    
    Print(L"Press any key to exit...\r\n");
    EFI_INPUT_KEY Key;
    UINTN index;
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
    uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
    
    return EFI_SUCCESS;
}
