/* oo_self_improve.c — OO Self-Improvement Engine  Phase 2
 * =========================================================
 * Phase 2 adds:
 *   - Session log ring buffer (8 KB static)
 *   - Oracle integration via oo_netboot_oracle_query()
 *   - Structured oracle response parser (PROPOSAL: blocks)
 *   - Journal persistence (oo_patches.journal, append-only EFI file)
 *   - Boot-time patch verification (oo_si_boot_verify)
 *   - /patch_oracle, /patch_status, /patch_log, /patch_export REPL commands
 *   - D+ scoring refinement per category + source
 * Freestanding C11. No libc. Static pool only.
 */
#include "oo_self_improve.h"
#include "../network/oo_netboot.h"
#include <efi.h>
#include <efilib.h>

/* ── Global singleton ───────────────────────────────────────────────────── */
OoSelfImprove g_self_improve;

/* ── Session log ring buffer (8 KB) ─────────────────────────────────────── */
#define OO_SI_LOG_CAP  8192
static CHAR8  g_si_log[OO_SI_LOG_CAP];
static UINTN  g_si_log_len = 0;

void oo_si_log_append(const CHAR8 *text, UINTN len) {
    if (!text || len == 0) return;
    UINTN space = OO_SI_LOG_CAP - g_si_log_len;
    if (space == 0) return;          /* ring full — drop (caller can clear) */
    if (len > space) len = space;
    for (UINTN i = 0; i < len; i++) g_si_log[g_si_log_len + i] = text[i];
    g_si_log_len += len;
}

void oo_si_log_clear(void) {
    for (UINTN i = 0; i < g_si_log_len; i++) g_si_log[i] = 0;
    g_si_log_len = 0;
}

const CHAR8 *oo_si_log_get(UINTN *out_len) {
    if (out_len) *out_len = g_si_log_len;
    return g_si_log;
}

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
static int _si_isdigit(CHAR8 c) { return c>='0' && c<='9'; }
static UINT32 _si_atou(const CHAR8 *s) {
    UINT32 v=0; while (_si_isdigit(*s)) { v=v*10+(*s-'0'); s++; } return v;
}
/* skip whitespace */
static const CHAR8 *_si_skip_ws(const CHAR8 *s) {
    while (*s==' '||*s=='\t'||*s=='\r'||*s=='\n') s++; return s;
}
/* find needle in haystack (CHAR8) */
static const CHAR8 *_si_memmem(const CHAR8 *hay, UINTN hlen,
                                const CHAR8 *needle, UINTN nlen) {
    if (!nlen || hlen < nlen) return NULL;
    for (UINTN i = 0; i <= hlen - nlen; i++) {
        UINTN j = 0;
        while (j < nlen && hay[i+j] == needle[j]) j++;
        if (j == nlen) return hay + i;
    }
    return NULL;
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

/* Map CHAR8 category token → PatchCategory */
static PatchCategory _cat_from_str(const CHAR8 *s) {
    if (_si_strncmp(s,(const CHAR8*)"PERF",4)==0)    return PATCH_CAT_PERF;
    if (_si_strncmp(s,(const CHAR8*)"SAFETY",6)==0)  return PATCH_CAT_SAFETY;
    if (_si_strncmp(s,(const CHAR8*)"BUGFIX",6)==0)  return PATCH_CAT_BUGFIX;
    if (_si_strncmp(s,(const CHAR8*)"MODEL",5)==0)   return PATCH_CAT_MODEL;
    if (_si_strncmp(s,(const CHAR8*)"CONFIG",6)==0)  return PATCH_CAT_CONFIG;
    if (_si_strncmp(s,(const CHAR8*)"ARCH",4)==0)    return PATCH_CAT_ARCH;
    return PATCH_CAT_FEATURE;
}

/* ── ID generator ───────────────────────────────────────────────────────── */
static void _gen_id(OoSelfImprove *si, CHAR8 *out) {
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

/* ── D+ scoring (Phase 2 refined) ───────────────────────────────────────── */
static UINT32 _dplus_score(PatchCategory cat, PatchSource src, UINT32 conf) {
    /* Base = 60% of confidence */
    UINT32 score = (conf * 6) / 10;
    /* Category weights: SAFETY & BUGFIX get bonus, ARCH penalised (high risk) */
    switch (cat) {
    case PATCH_CAT_SAFETY:  score = (score * 120) / 100; break;
    case PATCH_CAT_BUGFIX:  score = (score * 110) / 100; break;
    case PATCH_CAT_PERF:    score = (score * 100) / 100; break;
    case PATCH_CAT_CONFIG:  score = (score *  95) / 100; break;
    case PATCH_CAT_FEATURE: score = (score *  90) / 100; break;
    case PATCH_CAT_MODEL:   score = (score *  85) / 100; break;
    case PATCH_CAT_ARCH:    score = (score *  70) / 100; break;
    default: break;
    }
    /* Source trust: human > oracle > local LLM > crash */
    switch (src) {
    case PATCH_SRC_HUMAN:     score = (score * 130) / 100; break;
    case PATCH_SRC_ORACLE:    score = (score * 110) / 100; break;
    case PATCH_SRC_LOCAL_LLM: score = (score * 100) / 100; break;
    case PATCH_SRC_EVOLUTION: score = (score *  95) / 100; break;
    case PATCH_SRC_CRASH:     score = (score *  80) / 100; break;
    default: break;
    }
    if (score > 100) score = 100;
    return score;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void oo_si_init(OoSelfImprove *si) {
    if (!si) return;
    _si_memset(si, 0, sizeof(*si));
    si->initialized = 1;
    Print(L"[si] Self-Improve engine ready (max %d patches)\r\n", OO_PATCH_MAX);
}

/* ── Boot verification ───────────────────────────────────────────────────── */
/*
 * Called early at boot (after filesystem is mounted).
 * Scans oo_patches/ for .patch files that match APPLIED patches in queue.
 * Marks verified ones as PATCH_ST_APPLIED (confirmed on next boot).
 */
void oo_si_boot_verify(OoSelfImprove *si, EFI_FILE_HANDLE Root) {
    if (!si || !si->initialized || !Root) return;
    Print(L"[si] Boot verify — checking %d patch(es)...\r\n", si->count);
    int verified = 0;
    for (int i = 0; i < si->count; i++) {
        OoPatch *p = &si->patches[i];
        if (p->status != PATCH_ST_APPLIED) continue;

        CHAR16 path16[32];
        path16[0]='o'; path16[1]='o'; path16[2]='_';
        path16[3]='p'; path16[4]='a'; path16[5]='t';
        path16[6]='c'; path16[7]='h'; path16[8]='e';
        path16[9]='s'; path16[10]='\\';
        int k = 11;
        for (int j = 0; p->id[j] && j < 12; j++)
            path16[k++] = (CHAR16)p->id[j];
        /* append ".patch" */
        const CHAR16 *suf = L".patch";
        for (int j = 0; suf[j] && k < 30; j++) path16[k++] = suf[j];
        path16[k] = 0;

        EFI_FILE_HANDLE fh = NULL;
        EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &fh, path16,
                                          EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(st) && fh) {
            uefi_call_wrapper(fh->Close, 1, fh);
            Print(L"[si]   VERIFIED: %a patch file present\r\n", p->id);
            verified++;
        } else {
            Print(L"[si]   MISSING: %a patch file not found on disk\r\n", p->id);
            p->status = PATCH_ST_FAILED;
        }
    }
    Print(L"[si] Boot verify done: %d/%d patches confirmed\r\n",
          verified, si->count);
}

/* ── Observation ─────────────────────────────────────────────────────────── */
void oo_si_observe_session(OoSelfImprove *si, const CHAR8 *uart_log, UINTN log_len) {
    if (!si || !si->initialized || !uart_log) return;
    const CHAR8 *patterns[] = {
        (const CHAR8*)"ERROR:", (const CHAR8*)"PANIC:", (const CHAR8*)"FAULT:",
        (const CHAR8*)"OOM:",   (const CHAR8*)"sm_halt"
    };
    const UINTN plen[] = { 6, 6, 6, 4, 7 };
    for (UINTN p = 0; p < 5; p++) {
        const CHAR8 *hit = _si_memmem(uart_log, log_len, patterns[p], plen[p]);
        if (hit) {
            CHAR8 desc[OO_PATCH_DESC_LEN];
            _si_strlcpy(desc, (const CHAR8*)"Auto-detected pattern: ", OO_PATCH_DESC_LEN);
            _si_strlcpy(desc + 23, patterns[p], OO_PATCH_DESC_LEN - 23);
            oo_si_add_proposal(si, PATCH_CAT_BUGFIX, PATCH_SRC_CRASH,
                               desc, (const CHAR8*)"(auto)", (const CHAR8*)"", 40);
        }
    }
    /* Also feed to session log for future oracle analysis */
    if (log_len > 0 && g_si_log_len == 0)
        oo_si_log_append(uart_log, log_len < OO_SI_LOG_CAP ? log_len : OO_SI_LOG_CAP - 1);
}

void oo_si_observe_crash(OoSelfImprove *si, const CHAR8 *crash_desc, UINT64 fault_addr) {
    if (!si || !si->initialized) return;
    CHAR8 desc[OO_PATCH_DESC_LEN];
    _si_strlcpy(desc, (const CHAR8*)"Crash at 0x", OO_PATCH_DESC_LEN);
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
    p->dplus_score    = _dplus_score(cat, src, confidence_pct);
    _si_strlcpy(p->description, description, OO_PATCH_DESC_LEN);
    _si_strlcpy(p->target_file, target_file ? target_file : (const CHAR8*)"", 128);
    _si_strlcpy(p->code, code ? code : (const CHAR8*)"", OO_PATCH_CODE_LEN);
    Print(L"[si] Proposal %a added [%s|%s|conf=%d%%|D+=%d]\r\n",
          p->id, cat_name(cat), src_name(src),
          (UINT32)confidence_pct, p->dplus_score);
    return 1;
}

/* ── Generate proposals (seeded) ────────────────────────────────────────── */
int oo_si_generate_proposals(OoSelfImprove *si, PatchSource source, const CHAR8 *context_prompt) {
    if (!si || !si->initialized) return 0;
    (void)context_prompt;
    Print(L"[si] Generating seeded proposals (source=%s)...\r\n", src_name(source));
    int added = 0;
    if (source == PATCH_SRC_LOCAL_LLM || source == PATCH_SRC_ORACLE) {
        added += oo_si_add_proposal(si, PATCH_CAT_PERF, source,
            (const CHAR8*)"Batch TTS PCM writes to HDA (reduce REPL latency)",
            (const CHAR8*)"engine/drivers/oo_audio_hda.c",
            (const CHAR8*)"/* TODO: batch 4096-sample blocks instead of per-token writes */",
            55);
        added += oo_si_add_proposal(si, PATCH_CAT_SAFETY, source,
            (const CHAR8*)"Sanitize /ssm_load path: reject '..' sequences to prevent traversal",
            (const CHAR8*)"engine/llama2/soma_boot.c",
            (const CHAR8*)"/* TODO: validate path chars — reject '..' sequences */",
            80);
        added += oo_si_add_proposal(si, PATCH_CAT_FEATURE, source,
            (const CHAR8*)"Wire EFI_DNS4_PROTOCOL for hostname resolution at runtime",
            (const CHAR8*)"engine/network/oo_netboot.c",
            (const CHAR8*)"/* Phase 3: EFI_DNS4_PROTOCOL to resolve api.openai.com */",
            65);
    }
    Print(L"[si] %d seeded proposal(s) added\r\n", added);
    return added;
}

/* ── Oracle proposal parser ──────────────────────────────────────────────── */
/*
 * Expected oracle response format (one or more blocks):
 *
 *   PROPOSAL: category=BUGFIX target=engine/foo.c confidence=85
 *   DESC: Fix null pointer before buffer allocation
 *   CODE: if (!buf) return EFI_OUT_OF_RESOURCES;
 *   END
 *
 * Parser is tolerant: stops at "END" or next "PROPOSAL:".
 * Returns number of proposals successfully parsed + added.
 */
static int _si_parse_oracle_response(OoSelfImprove *si, PatchSource src,
                                     const CHAR8 *resp, UINTN resp_len) {
    if (!resp || resp_len == 0) return 0;
    int added = 0;
    const CHAR8 *p   = resp;
    const CHAR8 *end = resp + resp_len;

    while (p < end) {
        /* find next "PROPOSAL:" marker */
        const CHAR8 *mark = _si_memmem(p, (UINTN)(end - p),
                                        (const CHAR8*)"PROPOSAL:", 9);
        if (!mark) break;
        p = mark + 9;
        p = _si_skip_ws(p);

        /* defaults */
        PatchCategory cat = PATCH_CAT_FEATURE;
        CHAR8 target[128] = {0};
        UINT32 conf = 70;

        /* parse key=value pairs on same line until \n */
        while (p < end && *p != '\n' && *p != '\r') {
            p = _si_skip_ws(p);
            if (_si_strncmp(p,(const CHAR8*)"category=",9)==0) {
                p += 9;
                CHAR8 tok[16]={0};
                int k=0;
                while (p<end && *p!=' '&&*p!='\n'&&*p!='\r'&&k<15)
                    tok[k++]=*p++;
                cat = _cat_from_str(tok);
            } else if (_si_strncmp(p,(const CHAR8*)"target=",7)==0) {
                p += 7;
                int k=0;
                while (p<end && *p!=' '&&*p!='\n'&&*p!='\r'&&k<127)
                    target[k++]=*p++;
                target[k]=0;
            } else if (_si_strncmp(p,(const CHAR8*)"confidence=",11)==0) {
                p += 11;
                conf = _si_atou(p);
                while (p<end && _si_isdigit(*p)) p++;
            } else {
                /* skip unknown token */
                while (p<end && *p!=' '&&*p!='\n'&&*p!='\r') p++;
            }
        }
        /* skip to next line */
        while (p < end && (*p=='\n'||*p=='\r')) p++;

        /* DESC: */
        CHAR8 desc[OO_PATCH_DESC_LEN] = {0};
        if (_si_strncmp(p,(const CHAR8*)"DESC:",5)==0) {
            p += 5;
            p = _si_skip_ws(p);
            int k=0;
            while (p<end && *p!='\n'&&*p!='\r'&&k<OO_PATCH_DESC_LEN-1)
                desc[k++]=*p++;
            desc[k]=0;
            while (p<end&&(*p=='\n'||*p=='\r')) p++;
        }

        /* CODE: (may span multiple lines until END) */
        CHAR8 code[OO_PATCH_CODE_LEN] = {0};
        if (_si_strncmp(p,(const CHAR8*)"CODE:",5)==0) {
            p += 5;
            if (*p=='\n'||*p=='\r') { p++; if (*p=='\n') p++; }
            int k=0;
            while (p<end && k<OO_PATCH_CODE_LEN-1) {
                if (_si_strncmp(p,(const CHAR8*)"END",3)==0 &&
                    (p[3]=='\n'||p[3]=='\r'||p[3]==0)) break;
                if (_si_strncmp(p,(const CHAR8*)"PROPOSAL:",9)==0) break;
                code[k++]=*p++;
            }
            code[k]=0;
        }

        /* skip past END marker */
        const CHAR8 *end_mark = _si_memmem(p, (UINTN)(end - p),
                                            (const CHAR8*)"END", 3);
        if (end_mark) p = end_mark + 3;

        if (desc[0] == 0)
            _si_strlcpy(desc, (const CHAR8*)"Oracle-generated proposal", OO_PATCH_DESC_LEN);
        if (target[0] == 0)
            _si_strlcpy(target, (const CHAR8*)"(unspecified)", 128);

        added += oo_si_add_proposal(si, cat, src, desc, target, code, conf);
    }
    return added;
}

/* ── Oracle integration (Phase 2) ───────────────────────────────────────── */
/*
 * Builds a self-improvement prompt from session log, sends to oracle,
 * parses PROPOSAL: blocks from response.
 *
 * oracle_id: 0=GPT4, 1=Claude, 2=Gemini (matches OoOracleId enum).
 * Returns number of proposals created from oracle response.
 */
int oo_si_ask_oracle(OoSelfImprove *si, int oracle_id, const CHAR8 *extra_prompt) {
    if (!si || !si->initialized) return 0;

    /* Build prompt */
    static CHAR8 prompt_buf[OO_SI_LOG_CAP + 1024];
    UINTN pp = 0;

    /* System preamble */
    const CHAR8 *preamble =
        (const CHAR8*)
        "You are a bare-metal OS systems engineer reviewing the OO (Operating Organism) "
        "project — a UEFI x86_64 kernel with integrated LLM inference.\n"
        "Analyze the session log below and output improvement proposals in EXACTLY this format "
        "(one or more blocks, each terminated by END on its own line):\n\n"
        "PROPOSAL: category=<PERF|BUGFIX|SAFETY|FEATURE|MODEL|CONFIG|ARCH> "
        "target=<file/path.c> confidence=<0-100>\n"
        "DESC: <one-line description of the improvement>\n"
        "CODE: <code snippet or pseudo-code>\n"
        "END\n\n"
        "Session log follows:\n---\n";
    UINTN plen = _si_strlen(preamble);
    if (pp + plen < sizeof(prompt_buf) - 1) {
        _si_memcpy(prompt_buf + pp, preamble, plen); pp += plen;
    }

    /* Append session log (up to 4KB to fit in prompt) */
    UINTN loglen = 0;
    const CHAR8 *log = oo_si_log_get(&loglen);
    if (loglen > 4096) loglen = 4096;
    if (log && loglen > 0 && pp + loglen < sizeof(prompt_buf) - 1) {
        _si_memcpy(prompt_buf + pp, log, loglen); pp += loglen;
    }

    /* Extra prompt from user */
    if (extra_prompt && extra_prompt[0]) {
        const CHAR8 *extra_hdr = (const CHAR8*)"\n---\nAdditional context: ";
        UINTN ehlen = _si_strlen(extra_hdr);
        if (pp + ehlen < sizeof(prompt_buf) - 1) {
            _si_memcpy(prompt_buf + pp, extra_hdr, ehlen); pp += ehlen;
        }
        UINTN elen = _si_strlen(extra_prompt);
        if (pp + elen < sizeof(prompt_buf) - 1) {
            _si_memcpy(prompt_buf + pp, extra_prompt, elen); pp += elen;
        }
    }
    prompt_buf[pp] = 0;

    /* Oracle response buffer (8KB) */
    static CHAR8 resp_buf[8192];
    _si_memset(resp_buf, 0, sizeof(resp_buf));

    Print(L"[si] Asking oracle (id=%d) for improvement proposals...\r\n", oracle_id);

    EFI_STATUS st = oo_netboot_oracle_query(
        &g_netboot,
        (OoOracleId)oracle_id,
        prompt_buf,
        resp_buf,
        sizeof(resp_buf) - 1);

    if (EFI_ERROR(st)) {
        Print(L"[si] Oracle query failed: %r\r\n", st);
        Print(L"[si] Tip: set server with /net_server <ip> and run oracle proxy\r\n");
        return 0;
    }

    UINTN rlen = _si_strlen(resp_buf);
    Print(L"[si] Oracle response: %u bytes\r\n", (UINT32)rlen);

    int n = _si_parse_oracle_response(si, PATCH_SRC_ORACLE, resp_buf, rlen);
    Print(L"[si] Parsed %d proposal(s) from oracle\r\n", n);
    return n;
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
    if (p->dplus_score < 20) {
        Print(L"[si] D+ policy BLOCKED: score %d < 20 minimum\r\n", p->dplus_score);
        return 0;
    }
    p->status = PATCH_ST_APPROVED;
    Print(L"[si] \u2713 Patch %a APPROVED [D+=%d]\r\n", patch_id, p->dplus_score);
    Print(L"[si]   Use /patch_apply to write to storage\r\n");
    return 1;
}

int oo_si_reject(OoSelfImprove *si, const CHAR8 *patch_id) {
    if (!si || !patch_id) return 0;
    OoPatch *p = _find_patch(si, patch_id);
    if (!p) { Print(L"[si] Patch %a not found\r\n", patch_id); return 0; }
    p->status = PATCH_ST_REJECTED;
    Print(L"[si] \u2717 Patch %a REJECTED\r\n", patch_id);
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
    Print(L"[si] \u21ba Patch %a ROLLED BACK\r\n", patch_id);
    return 1;
}

/* ── Journal (Phase 2) ───────────────────────────────────────────────────── */
/*
 * Appends one journal line to oo_patches.journal (EFI file, append-only).
 * Format:  <timestamp> <event> <patch_id> <category> <status> <desc>\n
 */
void oo_si_journal_write(OoSelfImprove *si, EFI_FILE_HANDLE Root,
                         const CHAR8 *event, const OoPatch *p) {
    if (!si || !Root || !event || !p) return;

    static CHAR8 line[512];
    UINTN lp = 0;

    /* timestamp (tick count as hex proxy) */
    UINT64 tick = 0;
    EFI_TIME t = {0};
    if (gRT && !EFI_ERROR(uefi_call_wrapper(gRT->GetTime, 2, &t, NULL))) {
        tick = ((UINT64)t.Year  << 48) | ((UINT64)t.Month << 40) |
               ((UINT64)t.Day  << 32) | ((UINT64)t.Hour  << 24) |
               ((UINT64)t.Minute<<16) | ((UINT64)t.Second<<8);
    }
    /* write hex timestamp */
    line[lp++]='[';
    for (int sh=60;sh>=0;sh-=4) {
        UINT8 n=(UINT8)((tick>>sh)&0xF);
        if (lp<500) line[lp++]= n<10?'0'+n:'A'+(n-10);
    }
    line[lp++]=']'; line[lp++]=' ';

    /* event */
    UINTN elen = _si_strlen(event);
    if (lp + elen < 500) { _si_memcpy(line+lp, event, elen); lp+=elen; }
    line[lp++]=' ';

    /* patch id + category + status + description */
    UINTN il = _si_strlen(p->id);
    if (lp+il<500){ _si_memcpy(line+lp,p->id,il); lp+=il; } line[lp++]=' ';
    /* category as number (simple) */
    line[lp++]='C'; line[lp++]='0'+(int)p->category; line[lp++]=' ';
    line[lp++]='S'; line[lp++]='0'+(int)p->status;   line[lp++]=' ';
    UINTN dl = _si_strlen(p->description);
    if (dl > 80) dl = 80;
    if (lp+dl<500){ _si_memcpy(line+lp,p->description,dl); lp+=dl; }
    line[lp++]='\n'; line[lp]=0;

    /* Open or create journal file (append by seeking to end) */
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &fh,
                                      L"oo_patches.journal",
                                      EFI_FILE_MODE_CREATE|EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,
                                      0);
    if (EFI_ERROR(st) || !fh) return;

    /* Seek to end */
    EFI_FILE_INFO *info = NULL;
    UINTN info_sz = sizeof(EFI_FILE_INFO) + 128;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_sz, (void**)&info);
    if (info) {
        if (!EFI_ERROR(uefi_call_wrapper(fh->GetInfo, 4, fh, &gEfiFileInfoGuid, &info_sz, info)))
            uefi_call_wrapper(fh->SetPosition, 2, fh, info->FileSize);
        uefi_call_wrapper(BS->FreePool, 1, info);
    }

    UINTN write_sz = lp;
    uefi_call_wrapper(fh->Write, 3, fh, &write_sz, (void*)line);
    uefi_call_wrapper(fh->Flush, 1, fh);
    uefi_call_wrapper(fh->Close, 1, fh);
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
            Print(L"[si] No filesystem — journal-only\r\n");
            p->status = PATCH_ST_APPLIED;
            applied++;
            continue;
        }

        /* Ensure oo_patches\ directory exists */
        EFI_FILE_HANDLE dir = NULL;
        uefi_call_wrapper(Root->Open, 5, Root, &dir, L"oo_patches",
                          EFI_FILE_MODE_CREATE|EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,
                          EFI_FILE_DIRECTORY);
        if (dir) uefi_call_wrapper(dir->Close, 1, dir);

        /* Build path: oo_patches\PA-000000.patch */
        CHAR16 path16[32]; int k=0;
        const CHAR16 *prefix = L"oo_patches\\";
        while (prefix[k] && k<11) { path16[k]=prefix[k]; k++; }
        for (int j=0; p->id[j]&&j<12; j++) path16[k++]=(CHAR16)p->id[j];
        const CHAR16 *suf = L".patch";
        for (int j=0; suf[j]&&k<30; j++) path16[k++]=suf[j];
        path16[k]=0;

        EFI_FILE_HANDLE fh = NULL;
        EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &fh, path16,
            EFI_FILE_MODE_CREATE|EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE, 0);
        if (!EFI_ERROR(st) && fh) {
            UINTN sz = _si_strlen(p->code);
            if (sz == 0) {
                /* write description if no code */
                sz = _si_strlen(p->description);
                uefi_call_wrapper(fh->Write, 3, fh, &sz, (void*)p->description);
            } else {
                uefi_call_wrapper(fh->Write, 3, fh, &sz, (void*)p->code);
            }
            uefi_call_wrapper(fh->Flush, 1, fh);
            uefi_call_wrapper(fh->Close, 1, fh);
            Print(L"[si] Written: %s\r\n", path16);
        } else {
            Print(L"[si] Could not write patch file\r\n");
        }

        p->status = PATCH_ST_APPLIED;
        oo_si_journal_write(si, Root, (const CHAR8*)"APPLIED", p);
        applied++;
    }
    if (applied == 0)
        Print(L"[si] No approved patches to apply\r\n");
    return applied;
}

/* ── Print helpers ───────────────────────────────────────────────────────── */
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
    Print(L"  \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n");
    Print(L"  %-10s %-9s %-8s %4s %4s  %s\r\n",
          L"ID", L"STATUS", L"CAT", L"CONF", L"D+", L"DESCRIPTION");
    Print(L"  \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n");
    for (int i = 0; i < si->count; i++) {
        OoPatch *p = &si->patches[i];
        CHAR16 desc16[48] = {0};
        for (int j=0;j<47&&p->description[j];j++) desc16[j]=(CHAR16)p->description[j];
        Print(L"  %a  %-9s %-8s %3d%% %3d  %.46s\r\n",
              p->id, st_name(p->status), cat_name(p->category),
              p->confidence_pct, p->dplus_score, desc16);
    }
    if (si->count == 0) Print(L"  (no proposals yet — run /patch_analyze or /patch_oracle)\r\n");
    Print(L"\r\n");
}

void oo_si_print_status(OoSelfImprove *si) {
    if (!si) return;
    int pending=0, approved=0, applied=0, rejected=0, failed=0, rolledback=0;
    for (int i=0;i<si->count;i++) {
        switch(si->patches[i].status) {
        case PATCH_ST_PENDING:     pending++;    break;
        case PATCH_ST_APPROVED:    approved++;   break;
        case PATCH_ST_APPLIED:     applied++;    break;
        case PATCH_ST_REJECTED:    rejected++;   break;
        case PATCH_ST_FAILED:      failed++;     break;
        case PATCH_ST_ROLLED_BACK: rolledback++; break;
        default: break;
        }
    }
    UINTN loglen = 0;
    oo_si_log_get(&loglen);
    Print(L"\r\n  [OO Self-Improve Status]\r\n");
    Print(L"  Total proposals : %d / %d\r\n", si->count, OO_PATCH_MAX);
    Print(L"  Pending         : %d\r\n", pending);
    Print(L"  Approved        : %d\r\n", approved);
    Print(L"  Applied         : %d\r\n", applied);
    Print(L"  Rejected        : %d\r\n", rejected);
    Print(L"  Failed          : %d\r\n", failed);
    Print(L"  Rolled back     : %d\r\n", rolledback);
    Print(L"  Session log     : %u bytes / %d cap\r\n", (UINT32)loglen, OO_SI_LOG_CAP);
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

    /* /patch_list */
    if (_cstrcmp(cmd, "/patch_list", 11) == 0) {
        oo_si_print_list(si); return 1;
    }
    /* /patch_status */
    if (_cstrcmp(cmd, "/patch_status", 13) == 0) {
        oo_si_print_status(si); return 1;
    }
    /* /patch_analyze — seeded local proposals */
    if (_cstrcmp(cmd, "/patch_analyze", 14) == 0) {
        oo_si_generate_proposals(si, PATCH_SRC_LOCAL_LLM, (const CHAR8*)"session");
        return 1;
    }
    /* /patch_oracle [gpt4|claude|gemini] [extra prompt] */
    if (_cstrcmp(cmd, "/patch_oracle", 13) == 0) {
        const char *rest = cmd + 13;
        while (*rest==' ') rest++;
        int oracle_id = 0; /* default GPT4 */
        if (_cstrcmp(rest,"gpt4",4)==0)   { oracle_id=0; rest+=4; while(*rest==' ')rest++; }
        else if (_cstrcmp(rest,"claude",6)==0){ oracle_id=1; rest+=6; while(*rest==' ')rest++; }
        else if (_cstrcmp(rest,"gemini",6)==0){ oracle_id=2; rest+=6; while(*rest==' ')rest++; }
        oo_si_ask_oracle(si, oracle_id, (const CHAR8*)rest);
        return 1;
    }
    /* /patch_show <id> */
    if (_cstrcmp(cmd, "/patch_show ", 12) == 0) {
        OoPatch *p = _find_patch(si, (const CHAR8*)(cmd+12));
        if (p) oo_si_print_patch(p);
        else Print(L"[si] Patch not found: %a\r\n", (CHAR8*)(cmd+12));
        return 1;
    }
    /* /patch_approve <id> */
    if (_cstrcmp(cmd, "/patch_approve ", 15) == 0) {
        if (oo_si_approve(si, (const CHAR8*)(cmd+15)))
            oo_si_journal_write(si, Root, (const CHAR8*)"APPROVED",
                                _find_patch(si,(const CHAR8*)(cmd+15)));
        return 1;
    }
    /* /patch_reject <id> */
    if (_cstrcmp(cmd, "/patch_reject ", 14) == 0) {
        if (oo_si_reject(si, (const CHAR8*)(cmd+14)))
            oo_si_journal_write(si, Root, (const CHAR8*)"REJECTED",
                                _find_patch(si,(const CHAR8*)(cmd+14)));
        return 1;
    }
    /* /patch_rollback <id> */
    if (_cstrcmp(cmd, "/patch_rollback ", 16) == 0) {
        oo_si_rollback(si, (const CHAR8*)(cmd+16)); return 1;
    }
    /* /patch_apply — apply all approved */
    if (_cstrcmp(cmd, "/patch_apply", 12) == 0) {
        oo_si_apply_approved(si, Root); return 1;
    }
    /* /patch_propose [cat] <description> */
    if (_cstrcmp(cmd, "/patch_propose ", 15) == 0) {
        PatchCategory cat = PATCH_CAT_FEATURE;
        const char *rest = cmd + 15;
        if (_cstrcmp(rest,"perf ",5)==0)    { cat=PATCH_CAT_PERF;    rest+=5; }
        else if (_cstrcmp(rest,"bug ",4)==0) { cat=PATCH_CAT_BUGFIX;  rest+=4; }
        else if (_cstrcmp(rest,"safety ",7)==0){ cat=PATCH_CAT_SAFETY;rest+=7; }
        else if (_cstrcmp(rest,"arch ",5)==0) { cat=PATCH_CAT_ARCH;   rest+=5; }
        oo_si_add_proposal(si, cat, PATCH_SRC_HUMAN,
            (const CHAR8*)rest, (const CHAR8*)"(human)", (const CHAR8*)"", 90);
        return 1;
    }
    /* /patch_log — show session log */
    if (_cstrcmp(cmd, "/patch_log", 10) == 0) {
        UINTN loglen=0;
        const CHAR8 *log = oo_si_log_get(&loglen);
        Print(L"[si] Session log (%u bytes):\r\n", (UINT32)loglen);
        UINTN show = loglen > 512 ? 512 : loglen;
        for (UINTN i=0;i<show;i++) Print(L"%c",(CHAR16)log[i]);
        if (loglen > 512) Print(L"\r\n...[truncated]\r\n");
        Print(L"\r\n");
        return 1;
    }
    /* /patch_log_clear */
    if (_cstrcmp(cmd, "/patch_log_clear", 16) == 0) {
        oo_si_log_clear();
        Print(L"[si] Session log cleared\r\n");
        return 1;
    }
    /* /patch_export — dump all patches to oo_patches_export.txt */
    if (_cstrcmp(cmd, "/patch_export", 13) == 0) {
        if (!Root) { Print(L"[si] No filesystem\r\n"); return 1; }
        EFI_FILE_HANDLE fh=NULL;
        EFI_STATUS st = uefi_call_wrapper(Root->Open,5,Root,&fh,
            L"oo_patches_export.txt",
            EFI_FILE_MODE_CREATE|EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
        if (EFI_ERROR(st)||!fh){ Print(L"[si] Cannot create export file\r\n"); return 1; }
        for (int i=0;i<si->count;i++) {
            OoPatch *p=&si->patches[i];
            static CHAR8 line[512];
            UINTN lp=0;
            /* ID|CAT|STATUS|CONF|D+|DESC */
            UINTN il=_si_strlen(p->id);
            _si_memcpy(line+lp,p->id,il); lp+=il; line[lp++]='|';
            line[lp++]='0'+(int)p->category; line[lp++]='|';
            line[lp++]='0'+(int)p->status;   line[lp++]='|';
            /* confidence */
            UINT32 c=p->confidence_pct;
            line[lp++]='0'+(c/100)%10;
            line[lp++]='0'+(c/10)%10;
            line[lp++]='0'+(c%10);
            line[lp++]='|';
            UINT32 d=p->dplus_score;
            line[lp++]='0'+(d/100)%10;
            line[lp++]='0'+(d/10)%10;
            line[lp++]='0'+(d%10);
            line[lp++]='|';
            UINTN dl=_si_strlen(p->description);
            if(dl>200)dl=200;
            _si_memcpy(line+lp,p->description,dl); lp+=dl;
            line[lp++]='\n';
            UINTN ws=lp;
            uefi_call_wrapper(fh->Write,3,fh,&ws,(void*)line);
        }
        uefi_call_wrapper(fh->Flush,1,fh);
        uefi_call_wrapper(fh->Close,1,fh);
        Print(L"[si] Exported %d patch(es) to oo_patches_export.txt\r\n", si->count);
        return 1;
    }
    return 0;
}
