/* oo_self_improve.c — OO Self-Improvement Engine Implementation
 * ===============================================================
 * Freestanding C11. No libc. Static pool only.
 */
#include "oo_self_improve.h"
#include <efi.h>
#include <efilib.h>

/* Global singleton — accessible from soma_boot.c via extern */
OoSelfImprove g_self_improve;

/* ── String helpers (no libc) ───────────────────────────────────────────── */
static UINTN _si_strlen(const CHAR8 *s) {
    UINTN n = 0; if (!s) return 0; while (s[n]) n++; return n;
}
static void _si_memcpy(void *d, const void *s, UINTN n) {
    for (UINTN i = 0; i < n; i++) ((CHAR8*)d)[i] = ((const CHAR8*)s)[i];
}
static void _si_memset(void *d, CHAR8 v, UINTN n) {
    for (UINTN i = 0; i < n; i++) ((CHAR8*)d)[i] = v;
}
static int _si_strncmp(const CHAR8 *a, const CHAR8 *b, UINTN n) {
    for (UINTN i=0;i<n;i++){
        if (!a[i]&&!b[i]) return 0;
        if (a[i]!=b[i]) return (int)(UINT8)a[i]-(int)(UINT8)b[i];
    }
    return 0;
}
static void _si_strlcpy(CHAR8 *d, const CHAR8 *s, UINTN cap) {
    UINTN i=0;
    while (i+1<cap && s[i]) { d[i]=s[i]; i++; }
    d[i]=0;
}

/* Format tiny int → string, returns ptr to static buf */
static CHAR8 g_numbuf[32];
static CHAR8 *_si_itoa(int v) {
    if (v < 0) { g_numbuf[0]='-'; /* skip sign */ v=-v; }
    int i = 30; g_numbuf[31]=0;
    do { g_numbuf[i--]='0'+(v%10); v/=10; } while (v&&i>0);
    if (g_numbuf[0]=='-') { g_numbuf[i--]='-'; }
    return &g_numbuf[i+1];
}

/* ── Category & Status names ─────────────────────────────────────────────── */
static const CHAR16 *cat_name(PatchCategory c) {
    switch(c) {
    case PATCH_CAT_PERF:    return L"PERF";
    case PATCH_CAT_SAFETY:  return L"SAFETY";
    case PATCH_CAT_FEATURE: return L"FEATURE";
    case PATCH_CAT_BUGFIX:  return L"BUGFIX";
    case PATCH_CAT_MODEL:   return L"MODEL";
    case PATCH_CAT_CONFIG:  return L"CONFIG";
    case PATCH_CAT_ARCH:    return L"ARCH";
    default:                return L"?";
    }
}
static const CHAR16 *st_name(PatchStatus s) {
    switch(s) {
    case PATCH_ST_PENDING:     return L"PENDING";
    case PATCH_ST_APPROVED:    return L"APPROVED";
    case PATCH_ST_APPLIED:     return L"APPLIED";
    case PATCH_ST_REJECTED:    return L"REJECTED";
    case PATCH_ST_FAILED:      return L"FAILED";
    case PATCH_ST_ROLLED_BACK: return L"ROLLBACK";
    default:                   return L"?";
    }
}
static const CHAR16 *src_name(PatchSource s) {
    switch(s) {
    case PATCH_SRC_LOCAL_LLM: return L"LOCAL-LLM";
    case PATCH_SRC_ORACLE:    return L"ORACLE";
    case PATCH_SRC_HUMAN:     return L"HUMAN";
    case PATCH_SRC_CRASH:     return L"CRASH";
    case PATCH_SRC_EVOLUTION: return L"EVOLVION";
    default:                  return L"?";
    }
}

/* ── ID generator ───────────────────────────────────────────────────────── */
static void _gen_id(OoSelfImprove *si, CHAR8 *out) {
    /* PA-XXXXXX */
    int n = si->next_id++;
    out[0]='P'; out[1]='A'; out[2]='-';
    out[3]='0'+(n/100000)%10;
    out[4]='0'+(n/10000)%10;
    out[5]='0'+(n/1000)%10;
    out[6]='0'+(n/100)%10;
    out[7]='0'+(n/10)%10;
    out[8]='0'+(n%10);
    out[9]=0;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void oo_si_init(OoSelfImprove *si) {
    if (!si) return;
    _si_memset(si, 0, sizeof(*si));
    si->initialized = 1;
    Print(L"[si] Self-Improve engine ready (max %d patches)\r\n", OO_PATCH_MAX);
}

/* ── Observation ─────────────────────────────────────────────────────────── */
void oo_si_observe_session(OoSelfImprove *si, const CHAR8 *uart_log, UINTN log_len) {
    if (!si || !si->initialized || !uart_log) return;
    /* Phase 1: scan log for known error patterns */
    const CHAR8 *patterns[] = {
        (const CHAR8*)"ERROR:", (const CHAR8*)"PANIC:", (const CHAR8*)"FAULT:",
        (const CHAR8*)"OOM:", (const CHAR8*)"sm_halt"
    };
    for (UINTN p = 0; p < 5; p++) {
        UINTN plen = _si_strlen(patterns[p]);
        for (UINTN i = 0; i + plen <= log_len; i++) {
            if (_si_strncmp(uart_log + i, patterns[p], plen) == 0) {
                CHAR8 desc[OO_PATCH_DESC_LEN];
                _si_strlcpy(desc, (const CHAR8*)"Auto-detected pattern: ", OO_PATCH_DESC_LEN);
                _si_strlcpy(desc + 23, patterns[p], OO_PATCH_DESC_LEN - 23);
                oo_si_add_proposal(si, PATCH_CAT_BUGFIX, PATCH_SRC_CRASH,
                                   desc, (const CHAR8*)"(auto)", (const CHAR8*)"", 40);
                break;
            }
        }
    }
}

void oo_si_observe_crash(OoSelfImprove *si, const CHAR8 *crash_desc, UINT64 fault_addr) {
    if (!si || !si->initialized) return;
    CHAR8 desc[OO_PATCH_DESC_LEN];
    _si_strlcpy(desc, (const CHAR8*)"Crash at 0x", OO_PATCH_DESC_LEN);
    /* append fault_addr as hex */
    UINTN dl = _si_strlen(desc);
    for (int shift = 60; shift >= 0 && dl + 2 < OO_PATCH_DESC_LEN; shift -= 4) {
        UINT8 nibble = (UINT8)((fault_addr >> shift) & 0xF);
        desc[dl++] = nibble < 10 ? '0'+nibble : 'A'+(nibble-10);
    }
    desc[dl++] = ':'; desc[dl++] = ' '; desc[dl] = 0;
    _si_strlcpy(desc + dl, crash_desc, OO_PATCH_DESC_LEN - dl);
    oo_si_add_proposal(si, PATCH_CAT_BUGFIX, PATCH_SRC_CRASH, desc,
                       (const CHAR8*)"(crash)", (const CHAR8*)"", 30);
    Print(L"[si] Crash observed → proposal generated\r\n");
}

/* ── Add proposal ───────────────────────────────────────────────────────── */
int oo_si_add_proposal(OoSelfImprove *si, PatchCategory cat, PatchSource src,
                       const CHAR8 *description, const CHAR8 *target_file,
                       const CHAR8 *code, UINT32 confidence_pct) {
    if (!si || !si->initialized) return 0;
    if (si->count >= OO_PATCH_MAX) {
        Print(L"[si] Patch queue full (%d/%d)\r\n", si->count, OO_PATCH_MAX);
        return 0;
    }
    OoPatch *p = &si->patches[si->count++];
    _si_memset(p, 0, sizeof(*p));
    _gen_id(si, p->id);
    p->category       = cat;
    p->status         = PATCH_ST_PENDING;
    p->source         = src;
    p->confidence_pct = confidence_pct;
    p->dplus_score    = (confidence_pct * 7) / 10; /* simple heuristic */
    _si_strlcpy(p->description, description, OO_PATCH_DESC_LEN);
    _si_strlcpy(p->target_file, target_file ? target_file : (const CHAR8*)"", 128);
    _si_strlcpy(p->code, code ? code : (const CHAR8*)"", OO_PATCH_CODE_LEN);
    Print(L"[si] Proposal %a added [%s|%s|conf=%d%%]\r\n",
          p->id, cat_name(cat), src_name(src), (UINT32)confidence_pct);
    return 1;
}

/* ── Generate proposals via LLM context ─────────────────────────────────── */
int oo_si_generate_proposals(OoSelfImprove *si, PatchSource source, const CHAR8 *context_prompt) {
    if (!si || !si->initialized) return 0;
    Print(L"[si] Generating proposals (source=%s)...\r\n", src_name(source));
    /* Phase 1: seed proposals from known improvement areas */
    int added = 0;
    if (source == PATCH_SRC_LOCAL_LLM || source == PATCH_SRC_ORACLE) {
        added += oo_si_add_proposal(si, PATCH_CAT_PERF, source,
            (const CHAR8*)"Consider batching TTS PCM writes to HDA (reduces REPL latency)",
            (const CHAR8*)"engine/drivers/oo_audio_hda.c",
            (const CHAR8*)"/* TODO: batch 4096-sample blocks instead of per-token writes */",
            55);
        added += oo_si_add_proposal(si, PATCH_CAT_SAFETY, source,
            (const CHAR8*)"Add input sanitization check before /ssm_load to prevent path traversal",
            (const CHAR8*)"engine/llama2/soma_boot.c",
            (const CHAR8*)"/* TODO: validate path chars — reject '..' sequences */",
            80);
        added += oo_si_add_proposal(si, PATCH_CAT_FEATURE, source,
            (const CHAR8*)"Implement /net_pull to download model weights over HTTP at runtime",
            (const CHAR8*)"engine/network/oo_netboot.c",
            (const CHAR8*)"/* Phase 2: wire EFI_HTTP_PROTOCOL for live model pull */",
            70);
    }
    Print(L"[si] %d proposal(s) generated\r\n", added);
    return added;
}

/* ── Approve / Reject / Rollback ─────────────────────────────────────────── */
static OoPatch *_find_patch(OoSelfImprove *si, const CHAR8 *id) {
    for (int i = 0; i < si->count; i++)
        if (_si_strncmp(si->patches[i].id, id, OO_PATCH_ID_LEN) == 0)
            return &si->patches[i];
    return NULL;
}

int oo_si_approve(OoSelfImprove *si, const CHAR8 *patch_id) {
    if (!si || !patch_id) return 0;
    OoPatch *p = _find_patch(si, patch_id);
    if (!p) { Print(L"[si] Patch %a not found\r\n", patch_id); return 0; }
    if (p->status != PATCH_ST_PENDING) {
        Print(L"[si] Patch %a is not pending (status=%s)\r\n", patch_id, st_name(p->status));
        return 0;
    }
    /* D+ policy gate: minimum dplus_score to approve */
    if (p->dplus_score < 20) {
        Print(L"[si] D+ policy BLOCKED: score %d < 20 minimum\r\n", p->dplus_score);
        return 0;
    }
    p->status = PATCH_ST_APPROVED;
    Print(L"[si] ✓ Patch %a APPROVED [D+=%d]\r\n", patch_id, p->dplus_score);
    Print(L"[si]   Use /patch_apply to write to storage\r\n");
    return 1;
}

int oo_si_reject(OoSelfImprove *si, const CHAR8 *patch_id) {
    if (!si || !patch_id) return 0;
    OoPatch *p = _find_patch(si, patch_id);
    if (!p) { Print(L"[si] Patch %a not found\r\n", patch_id); return 0; }
    p->status = PATCH_ST_REJECTED;
    Print(L"[si] ✗ Patch %a REJECTED\r\n", patch_id);
    return 1;
}

int oo_si_rollback(OoSelfImprove *si, const CHAR8 *patch_id) {
    if (!si || !patch_id) return 0;
    OoPatch *p = _find_patch(si, patch_id);
    if (!p) { Print(L"[si] Patch %a not found\r\n", patch_id); return 0; }
    if (p->status != PATCH_ST_APPLIED) {
        Print(L"[si] Patch %a not applied — cannot rollback\r\n", patch_id);
        return 0;
    }
    p->status = PATCH_ST_ROLLED_BACK;
    Print(L"[si] ↺ Patch %a ROLLED BACK\r\n", patch_id);
    return 1;
}

/* ── Apply ───────────────────────────────────────────────────────────────── */
int oo_si_apply_approved(OoSelfImprove *si, EFI_FILE_HANDLE Root) {
    if (!si) return 0;
    int applied = 0;
    for (int i = 0; i < si->count; i++) {
        OoPatch *p = &si->patches[i];
        if (p->status != PATCH_ST_APPROVED) continue;

        Print(L"[si] Applying patch %a → %a\r\n", p->id, p->target_file);

        if (!Root) {
            Print(L"[si] No filesystem handle — writing to journal only\r\n");
            p->status = PATCH_ST_APPLIED;
            applied++;
            continue;
        }

        /* Write patch code to EFI file: oo_patches/<id>.patch */
        CHAR16 path16[64] = L"oo_patches\\";
        for (int j = 0; p->id[j] && j < 12; j++)
            path16[11 + j] = (CHAR16)p->id[j];
        for (int j = 12; j < 19; j++) path16[j] = L".patch"[j-12];
        path16[19] = 0;

        EFI_FILE_HANDLE fh = NULL;
        EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &fh, path16,
            EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
        if (!EFI_ERROR(st) && fh) {
            UINTN sz = _si_strlen(p->code);
            uefi_call_wrapper(fh->Write, 3, fh, &sz, (void*)p->code);
            uefi_call_wrapper(fh->Close, 1, fh);
            Print(L"[si] Written: %s (%u bytes)\r\n", path16, (UINT32)_si_strlen(p->code));
        } else {
            Print(L"[si] Could not write patch file (no oo_patches/ dir?)\r\n");
        }

        p->status = PATCH_ST_APPLIED;
        applied++;
    }
    if (applied == 0)
        Print(L"[si] No approved patches to apply\r\n");
    return applied;
}

/* ── Print ───────────────────────────────────────────────────────────────── */
void oo_si_print_patch(const OoPatch *p) {
    if (!p) return;
    Print(L"\r\n  ID     : %a\r\n", p->id);
    Print(L"  Status : %s\r\n", st_name(p->status));
    Print(L"  Cat    : %s\r\n", cat_name(p->category));
    Print(L"  Source : %s\r\n", src_name(p->source));
    Print(L"  Conf   : %d%%\r\n", p->confidence_pct);
    Print(L"  D+     : %d\r\n",   p->dplus_score);
    Print(L"  Reboot : %s\r\n",   p->requires_reboot ? L"yes" : L"no");
    Print(L"  File   : %a\r\n",   p->target_file);
    Print(L"  Desc   : %a\r\n",   p->description);
    if (p->code[0]) {
        Print(L"  Code   :\r\n    ");
        /* print first 200 chars of code */
        UINTN cl = _si_strlen(p->code);
        if (cl > 200) cl = 200;
        for (UINTN i = 0; i < cl; i++) {
            if (p->code[i] == '\n') Print(L"\r\n    ");
            else Print(L"%c", (CHAR16)p->code[i]);
        }
        Print(L"\r\n");
    }
    Print(L"\r\n");
}

void oo_si_print_list(OoSelfImprove *si) {
    if (!si) return;
    Print(L"\r\n  OO Patch Queue (%d/%d)\r\n", si->count, OO_PATCH_MAX);
    Print(L"  ──────────────────────────────────────────────────\r\n");
    Print(L"  %-10s %-8s %-8s %-6s %s\r\n",
          L"ID", L"STATUS", L"CAT", L"CONF", L"DESCRIPTION");
    Print(L"  ──────────────────────────────────────────────────\r\n");
    for (int i = 0; i < si->count; i++) {
        OoPatch *p = &si->patches[i];
        CHAR16 desc16[48] = {0};
        for (int j=0;j<47&&p->description[j];j++) desc16[j]=(CHAR16)p->description[j];
        Print(L"  %a  %-9s %-8s %3d%%  %.46s\r\n",
              p->id, st_name(p->status), cat_name(p->category),
              p->confidence_pct, desc16);
    }
    if (si->count == 0) Print(L"  (no proposals yet — run /patch_analyze)\r\n");
    Print(L"\r\n");
}

/* ── REPL command handler ───────────────────────────────────────────────── */
static int _cstrcmp(const char *a, const char *b, int n) {
    for (int i=0;i<n;i++){
        if (!a[i]&&!b[i]) return 0;
        if (a[i]!=b[i]) return (int)(unsigned char)a[i]-(int)(unsigned char)b[i];
    }
    return 0;
}

int oo_si_repl_cmd(OoSelfImprove *si, const char *cmd, EFI_FILE_HANDLE Root) {
    if (!cmd) return 0;

    if (_cstrcmp(cmd, "/patch_list", 11) == 0) {
        oo_si_print_list(si); return 1;
    }
    if (_cstrcmp(cmd, "/patch_analyze", 14) == 0) {
        oo_si_generate_proposals(si, PATCH_SRC_LOCAL_LLM, (const CHAR8*)"session"); return 1;
    }
    if (_cstrcmp(cmd, "/patch_show ", 12) == 0) {
        OoPatch *p = _find_patch(si, (const CHAR8*)(cmd+12));
        if (p) oo_si_print_patch(p);
        else Print(L"[si] Patch not found: %a\r\n", (CHAR8*)(cmd+12));
        return 1;
    }
    if (_cstrcmp(cmd, "/patch_approve ", 15) == 0) {
        oo_si_approve(si, (const CHAR8*)(cmd+15)); return 1;
    }
    if (_cstrcmp(cmd, "/patch_reject ", 14) == 0) {
        oo_si_reject(si, (const CHAR8*)(cmd+14)); return 1;
    }
    if (_cstrcmp(cmd, "/patch_rollback ", 16) == 0) {
        oo_si_rollback(si, (const CHAR8*)(cmd+16)); return 1;
    }
    if (_cstrcmp(cmd, "/patch_apply", 12) == 0) {
        oo_si_apply_approved(si, Root); return 1;
    }
    if (_cstrcmp(cmd, "/patch_propose ", 15) == 0) {
        /* /patch_propose [cat] <description> */
        PatchCategory cat = PATCH_CAT_FEATURE;
        const char *rest = cmd + 15;
        if (_cstrcmp(rest, "perf ", 5)==0)    { cat=PATCH_CAT_PERF;    rest+=5; }
        else if (_cstrcmp(rest,"bug ",4)==0)   { cat=PATCH_CAT_BUGFIX;  rest+=4; }
        else if (_cstrcmp(rest,"safety ",7)==0){ cat=PATCH_CAT_SAFETY;  rest+=7; }
        oo_si_add_proposal(si, cat, PATCH_SRC_HUMAN,
            (const CHAR8*)rest, (const CHAR8*)"(human)", (const CHAR8*)"", 90);
        return 1;
    }
    return 0;
}
