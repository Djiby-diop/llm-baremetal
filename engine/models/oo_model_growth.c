/* oo_model_growth.c — OO Model Self-Expansion Engine  Phase 5F
 * ==============================================================
 * Autonomous model growth: monitor → analyze → acquire → approve → apply.
 * Freestanding C11. No libc.
 */
#include "oo_model_growth.h"
#include "../network/oo_netboot.h"
#include <efi.h>
#include <efilib.h>

OoModelGrowth g_growth;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static UINTN _g_strlen(const CHAR8 *s){UINTN n=0;if(!s)return 0;while(s[n])n++;return n;}
static void  _g_strlcpy(CHAR8*d,const CHAR8*s,UINTN c){
    UINTN i=0;if(!d||!s||c==0)return;while(i+1<c&&s[i]){d[i]=s[i];i++;}d[i]=0;}
static void  _g_memset(void*d,UINT8 v,UINTN n){for(UINTN i=0;i<n;i++)((UINT8*)d)[i]=v;}
static int   _g_strncmp8(const CHAR8*a,const CHAR8*b,UINTN n){
    for(UINTN i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}
static int   _g_cstrcmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}
static void  _g_a2u(CHAR16*dst,const CHAR8*src,UINTN c){
    UINTN i=0;while(i+1<c&&src[i]){dst[i]=(CHAR16)src[i];i++;}dst[i]=0;}

/* Write UTF-8 line to EFI file (append) */
static void _g_log_write(EFI_FILE_HANDLE Root, const CHAR8 *path8,
                          const CHAR8 *line) {
    if (!Root || !path8 || !line) return;
    CHAR16 path16[128]; _g_a2u(path16, path8, 128);
    EFI_FILE_HANDLE fh = NULL;
    uefi_call_wrapper(Root->Open, 5, Root, &fh, path16,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (!fh) return;
    /* Seek to end */
    uefi_call_wrapper(fh->SetPosition, 2, fh, (UINT64)-1);
    UINTN len = _g_strlen(line);
    uefi_call_wrapper(fh->Write, 3, fh, &len, (void*)line);
    CHAR8 nl = '\n'; UINTN nls = 1;
    uefi_call_wrapper(fh->Write, 3, fh, &nls, &nl);
    uefi_call_wrapper(fh->Close, 1, fh);
}

/* ── Known DIOP model catalog (HuggingFace djibydiop) ───────────────────── */
/* These are the upgrade paths OO can propose automatically.               */
typedef struct {
    const CHAR8 *name;
    const CHAR8 *hf_repo;   /* repo/filename */
    UINT32       dim;
    UINT64       size_mb;
    OoGrowthAction action;
    float        improvement; /* expected quality improvement */
} _GrowthCatalogEntry;

static const _GrowthCatalogEntry _catalog[] = {
    /* Base tiny models (already bundled) */
    { (const CHAR8*)"stories15M",   (const CHAR8*)"karpathy/tinyllamas/stories15M.bin",
      288, 60,  OO_GROWTH_ACT_DOWNLOAD, 0.10f },
    { (const CHAR8*)"stories42M",   (const CHAR8*)"karpathy/tinyllamas/stories42M.bin",
      512, 167, OO_GROWTH_ACT_DOWNLOAD, 0.20f },
    { (const CHAR8*)"stories110M",  (const CHAR8*)"karpathy/tinyllamas/stories110M.bin",
      768, 440, OO_GROWTH_ACT_DOWNLOAD, 0.35f },

    /* DIOP specialized models */
    { (const CHAR8*)"diop-code-v1", (const CHAR8*)"djibydiop/llm-baremetal/diop-code-v1.bin",
      512, 200, OO_GROWTH_ACT_DOWNLOAD, 0.50f },
    { (const CHAR8*)"diop-sys-v1",  (const CHAR8*)"djibydiop/llm-baremetal/diop-sys-v1.bin",
      768, 300, OO_GROWTH_ACT_DOWNLOAD, 0.55f },

    /* LoRA deltas (small, fast to apply) */
    { (const CHAR8*)"diop-code-lora", (const CHAR8*)"djibydiop/llm-baremetal/code-lora.bin",
      0, 4, OO_GROWTH_ACT_LORA, 0.15f },
    { (const CHAR8*)"diop-sys-lora",  (const CHAR8*)"djibydiop/llm-baremetal/sys-lora.bin",
      0, 4, OO_GROWTH_ACT_LORA, 0.12f },

    { NULL, NULL, 0, 0, OO_GROWTH_ACT_NONE, 0.0f }
};

/* ── Init ────────────────────────────────────────────────────────────────── */
void oo_growth_init(OoModelGrowth *g) {
    _g_memset(g, 0, sizeof(*g));
    g->ram_limit_pct  = 0.75f;
    g->disk_limit_pct = 0.50f;
    g->min_confidence_threshold = 0.60f;
    g->fail_count_trigger = 3;
    g->auto_approve = 0; /* always require human approval */
    g->initialized = 1;
    Print(L"[growth] Model growth engine ready (auto_approve=OFF)\r\n");
}

/* ── Record query result ─────────────────────────────────────────────────── */
void oo_growth_record(OoModelGrowth *g, const CHAR8 *topic,
                       float confidence, int failed) {
    if (!g || !topic) return;

    /* Find or create topic slot */
    int slot = -1;
    for (UINT32 i = 0; i < g->n_topics; i++) {
        if (_g_strncmp8(g->topics[i].topic, topic, 63) == 0) {
            slot = (int)i; break;
        }
    }
    if (slot < 0 && g->n_topics < OO_GROWTH_TOPIC_SLOTS) {
        slot = (int)g->n_topics++;
        _g_strlcpy(g->topics[slot].topic, topic, 64);
        g->topics[slot].avg_confidence = confidence;
    }
    if (slot < 0) return;

    g->topics[slot].hit_count++;
    if (failed) g->topics[slot].fail_count++;
    /* Exponential moving average for confidence */
    g->topics[slot].avg_confidence =
        g->topics[slot].avg_confidence * 0.8f + confidence * 0.2f;
}

/* ── Should we grow? ─────────────────────────────────────────────────────── */
OoGrowthReason oo_growth_should_grow(OoModelGrowth *g,
                                      const OoDiopModel *model) {
    if (!g || !model) return OO_GROWTH_REASON_NONE;

    /* Check repeat failures per topic */
    for (UINT32 i = 0; i < g->n_topics; i++) {
        if (g->topics[i].fail_count >= g->fail_count_trigger) {
            Print(L"[growth] Topic '%a' failed %u times → growth trigger\r\n",
                  g->topics[i].topic, g->topics[i].fail_count);
            return OO_GROWTH_REASON_REPEAT_FAIL;
        }
    }
    /* Check average confidence */
    if (g->n_topics > 0) {
        float total_conf = 0.0f;
        for (UINT32 i = 0; i < g->n_topics; i++)
            total_conf += g->topics[i].avg_confidence;
        float avg = total_conf / (float)g->n_topics;
        if (avg < g->min_confidence_threshold) {
            Print(L"[growth] Average confidence %.1f%% < %.1f%% → growth\r\n",
                  avg * 100.0f, g->min_confidence_threshold * 100.0f);
            return OO_GROWTH_REASON_LOW_CONF;
        }
    }
    /* Check if current model is tiny (< 10MB → obviously room to grow) */
    if (model->loaded && model->weights_len < 10 * 1024 * 1024) {
        Print(L"[growth] Model is tiny (%u MB) — growth available\r\n",
              (UINT32)(model->weights_len / (1024*1024)));
        return OO_GROWTH_REASON_SCHEDULED;
    }
    return OO_GROWTH_REASON_NONE;
}

/* ── Find candidates ─────────────────────────────────────────────────────── */
EFI_STATUS oo_growth_find_candidates(OoModelGrowth *g,
                                      OoGrowthReason reason) {
    if (!g) return EFI_INVALID_PARAMETER;
    g->n_candidates = 0;

    Print(L"[growth] Finding candidates (reason=%d)...\r\n", (int)reason);

    /* Build from static catalog */
    for (int i = 0; _catalog[i].name && g->n_candidates < OO_GROWTH_MAX_CANDIDATES; i++) {
        OoGrowthCandidate *c = &g->candidates[g->n_candidates];
        _g_memset(c, 0, sizeof(*c));
        _g_strlcpy(c->name, _catalog[i].name, 128);
        /* Build URL: http://<proxy>/huggingface.co/<repo> */
        _g_strlcpy(c->url, (const CHAR8*)"http://10.0.2.2:8080/hf/", 256);
        UINTN ul = _g_strlen(c->url);
        UINTN rl = _g_strlen(_catalog[i].hf_repo);
        if (ul + rl < 255) {
            for (UINTN j = 0; j < rl; j++) c->url[ul+j] = _catalog[i].hf_repo[j];
            c->url[ul+rl] = 0;
        }
        c->size_bytes = _catalog[i].size_mb * 1024 * 1024;
        c->dim = _catalog[i].dim;
        c->action = _catalog[i].action;
        c->expected_improvement = _catalog[i].improvement;
        /* D+ score: improvement * 80 (max 80), LoRA gets bonus for small size */
        c->dplus_score = c->expected_improvement * 80.0f;
        if (c->action == OO_GROWTH_ACT_LORA) c->dplus_score += 10.0f;
        _g_strlcpy(c->format, (const CHAR8*)"bin", 8);
        g->n_candidates++;
    }

    /* Also ask oracle for recommendations */
    if (g_netboot.state != OO_NB_UNINIT && g_netboot.state != OO_NB_ERROR) {
        static CHAR8 prompt[256];
        UINTN pp = 0;
        const CHAR8 *pre = (const CHAR8*)
            "OO system needs model upgrade. Current: tiny 260K params. "
            "Suggest ONE specific HuggingFace model URL for bare-metal x86_64 LLM inference. "
            "Format: SUGGEST: <url> SIZE: <MB> DIM: <dim>";
        UINTN prl = _g_strlen(pre);
        for (UINTN i = 0; i < prl && pp < 255; i++) prompt[pp++] = pre[i];
        prompt[pp] = 0;
        static CHAR8 resp[512];
        uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
        EFI_STATUS st = oo_netboot_oracle_query(&g_netboot,
                                                 OO_ORACLE_GPT4, prompt, resp, 511);
        if (!EFI_ERROR(st) && _g_strlen(resp) > 0) {
            Print(L"[growth] Oracle suggests: %a\r\n", resp);
            /* Parse SUGGEST: <url> */
            for (UINTN i = 0; resp[i]; i++) {
                if (resp[i]=='S' && resp[i+1]=='U' && resp[i+2]=='G' &&
                    resp[i+3]=='G' && resp[i+4]=='E' && resp[i+5]=='S' &&
                    resp[i+6]=='T' && resp[i+7]==':') {
                    UINTN j = i + 8;
                    while (resp[j] == ' ') j++;
                    if (g->n_candidates < OO_GROWTH_MAX_CANDIDATES) {
                        OoGrowthCandidate *c = &g->candidates[g->n_candidates];
                        _g_memset(c, 0, sizeof(*c));
                        _g_strlcpy(c->name, (const CHAR8*)"oracle-rec", 128);
                        UINTN k = 0;
                        while (resp[j] && resp[j] != ' ' && k < 255)
                            c->url[k++] = resp[j++];
                        c->url[k] = 0;
                        c->action = OO_GROWTH_ACT_DOWNLOAD;
                        c->expected_improvement = 0.30f;
                        c->dplus_score = 50.0f; /* oracle recommendation */
                        _g_strlcpy(c->format, (const CHAR8*)"gguf", 8);
                        g->n_candidates++;
                    }
                    break;
                }
            }
        }
    }

    Print(L"[growth] Found %d candidates\r\n", g->n_candidates);
    return g->n_candidates > 0 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/* ── Request human approval ─────────────────────────────────────────────── */
void oo_growth_request_approval(OoModelGrowth *g, int cand_idx) {
    if (!g || cand_idx < 0 || cand_idx >= g->n_candidates) return;
    g->pending = g->candidates[cand_idx];
    g->pending_approval = 1;

    Print(L"\r\n  ╔══════════════════════════════════════════╗\r\n");
    Print(L"  ║   [OO MODEL GROWTH REQUEST]              ║\r\n");
    Print(L"  ╠══════════════════════════════════════════╣\r\n");
    Print(L"  ║ Name    : %-32a║\r\n", g->pending.name);
    Print(L"  ║ Action  : %-4s                               ║\r\n",
          g->pending.action == OO_GROWTH_ACT_LORA ?
          L"LoRA" : L"FULL");
    Print(L"  ║ Size    : %-6lu MB                          ║\r\n",
          (UINT64)(g->pending.size_bytes / (1024*1024)));
    Print(L"  ║ D+ Score: %-5.0f                             ║\r\n",
          (double)g->pending.dplus_score);
    Print(L"  ║ Improve : +%-3.0f%%                            ║\r\n",
          (double)(g->pending.expected_improvement * 100.0f));
    Print(L"  ╠══════════════════════════════════════════╣\r\n");
    Print(L"  ║ Type /growth_approve to accept           ║\r\n");
    Print(L"  ║ Type /growth_reject  to decline          ║\r\n");
    Print(L"  ╚══════════════════════════════════════════╝\r\n\r\n");
}

/* ── Apply approved growth ───────────────────────────────────────────────── */
EFI_STATUS oo_growth_apply(OoModelGrowth *g, OoDiopModel *model,
                             EFI_FILE_HANDLE Root) {
    if (!g || !g->pending_approval || !model) return EFI_NOT_READY;

    OoGrowthCandidate *c = &g->pending;
    Print(L"[growth] Applying: %a\r\n", c->name);
    Print(L"[growth] URL: %a\r\n", c->url);

    if (c->action == OO_GROWTH_ACT_LORA) {
        /* LoRA: download delta and apply to current weights */
        Print(L"[growth] LoRA delta apply (stub — full in Phase 5G)\r\n");
        Print(L"[growth] LoRA requires: base model loaded + delta file\r\n");
        oo_growth_log(g, Root, (const CHAR8*)"LORA_STUB applied");
        g->total_growths++;
        g->pending_approval = 0;
        return EFI_SUCCESS;
    }

    /* Full model download */
    Print(L"[growth] Downloading %lu MB model...\r\n",
          (UINT64)(c->size_bytes / (1024*1024)));

    /* Download to cache first */
    static CHAR8 cache_path[256];
    UINTN cp = 0;
    const CHAR8 *cpfx = (const CHAR8*)OO_GROWTH_CACHE_PATH;
    UINTN cpfl = _g_strlen(cpfx);
    for (UINTN i = 0; i < cpfl && cp < 255; i++) cache_path[cp++] = cpfx[i];
    UINTN cnl = _g_strlen(c->name);
    for (UINTN i = 0; i < cnl && cp < 250; i++) cache_path[cp++] = c->name[i];
    cache_path[cp++] = '.';
    cache_path[cp++] = c->format[0];
    cache_path[cp++] = c->format[1];
    cache_path[cp++] = c->format[2];
    cache_path[cp]   = 0;

    /* Pull via netboot HTTP */
    void *weights_buf = NULL; UINTN weights_len = 0;
    EFI_STATUS st = oo_netboot_pull_model(&g_netboot, c->url,
                                           &weights_buf, &weights_len);
    if (EFI_ERROR(st) || !weights_buf) {
        Print(L"[growth] Download failed: %r\r\n", st);
        g->pending_approval = 0;
        return st;
    }

    Print(L"[growth] Downloaded %u MB\r\n", (UINT32)(weights_len/(1024*1024)));
    g->total_downloads++;
    g->total_bytes_downloaded += weights_len;

    /* Save to ESP cache */
    if (Root) {
        CHAR16 cpath16[256]; _g_a2u(cpath16, cache_path, 256);
        EFI_FILE_HANDLE fh = NULL;
        st = uefi_call_wrapper(Root->Open, 5, Root, &fh, cpath16,
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
        if (!EFI_ERROR(st) && fh) {
            UINTN wsz = weights_len;
            uefi_call_wrapper(fh->Write, 3, fh, &wsz, weights_buf);
            uefi_call_wrapper(fh->Close, 1, fh);
            Print(L"[growth] Saved to cache: %s\r\n", cpath16);
        }
    }

    /* Hot-swap: unload old model, activate new one */
    if (model->loaded) {
        Print(L"[growth] Unloading previous model: %a\r\n", model->name);
        oo_diop_unload(model);
    }
    model->weights       = weights_buf;
    model->weights_len   = weights_len;
    model->weights_pages = (weights_len + 0xFFF) >> 12;
    _g_strlcpy(model->name, c->name, OO_DIOP_NAME_MAX);
    oo_diop_probe(model);
    model->loaded = 1;

    g->total_growths++;
    g->pending_approval = 0;
    g->last_reason = OO_GROWTH_REASON_NONE;

    oo_growth_log(g, Root, (const CHAR8*)"GROWTH_APPLIED");
    Print(L"[growth] ✓ Model upgraded to: %a\r\n", model->name);
    return EFI_SUCCESS;
}

/* ── Full pipeline ───────────────────────────────────────────────────────── */
EFI_STATUS oo_growth_run_pipeline(OoModelGrowth *g, OoDiopModel *model,
                                   EFI_FILE_HANDLE Root) {
    OoGrowthReason reason = oo_growth_should_grow(g, model);
    if (reason == OO_GROWTH_REASON_NONE) {
        Print(L"[growth] No growth needed at this time\r\n");
        return EFI_SUCCESS;
    }
    g->last_reason = reason;
    oo_growth_find_candidates(g, reason);
    if (g->n_candidates == 0) {
        Print(L"[growth] No candidates available\r\n");
        return EFI_NOT_FOUND;
    }
    /* Pick best by D+ score */
    int best = 0;
    for (int i = 1; i < g->n_candidates; i++)
        if (g->candidates[i].dplus_score > g->candidates[best].dplus_score)
            best = i;
    if (g->auto_approve) {
        g->pending = g->candidates[best];
        g->pending_approval = 1;
        return oo_growth_apply(g, model, Root);
    } else {
        oo_growth_request_approval(g, best);
        return EFI_SUCCESS; /* Resume when user types /growth_approve */
    }
}

/* ── Log ─────────────────────────────────────────────────────────────────── */
void oo_growth_log(OoModelGrowth *g, EFI_FILE_HANDLE Root,
                   const CHAR8 *event) {
    static CHAR8 line[128];
    UINTN lp = 0;
    /* Simple hex timestamp */
    UINT64 tick = 0;
    uefi_call_wrapper(BS->GetNextMonotonicCount, 1, &tick);
    const CHAR8 *hex = (const CHAR8*)"0123456789abcdef";
    line[lp++]='[';
    for (int i = 60; i >= 0; i -= 4)
        line[lp++] = hex[(tick >> i) & 0xF];
    line[lp++]=']'; line[lp++]=' ';
    UINTN el = _g_strlen(event);
    for (UINTN i = 0; i < el && lp < 126; i++) line[lp++] = event[i];
    line[lp] = 0;
    _g_log_write(Root, (const CHAR8*)OO_GROWTH_LOG_PATH, line);
    (void)g;
}

/* ── Print status ────────────────────────────────────────────────────────── */
void oo_growth_print_status(const OoModelGrowth *g) {
    Print(L"\r\n  [Model Growth Engine]\r\n");
    Print(L"  Initialized   : %s\r\n", g->initialized ? L"YES" : L"NO");
    Print(L"  Auto-approve  : %s\r\n", g->auto_approve ? L"YES (DANGER)" : L"NO (safe)");
    Print(L"  Total growths : %u\r\n", g->total_growths);
    Print(L"  Downloads     : %u (%u MB)\r\n",
          g->total_downloads,
          (UINT32)(g->total_bytes_downloaded/(1024*1024)));
    Print(L"  Pending       : %s\r\n", g->pending_approval ? L"YES" : L"NO");
    Print(L"  Topics tracked: %u\r\n", g->n_topics);
    Print(L"  RAM limit     : %u%%\r\n", (UINT32)(g->ram_limit_pct * 100));
    Print(L"  Disk limit    : %u%%\r\n\r\n", (UINT32)(g->disk_limit_pct * 100));
    if (g->n_topics > 0) {
        Print(L"  [Topic Stats]\r\n");
        for (UINT32 i = 0; i < g->n_topics && i < 8; i++) {
            Print(L"  '%a': hits=%u fail=%u conf=~%u%%\r\n",
                  g->topics[i].topic,
                  g->topics[i].hit_count,
                  g->topics[i].fail_count,
                  (UINT32)(g->topics[i].avg_confidence * 100));
        }
        Print(L"\r\n");
    }
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
int oo_growth_repl_cmd(OoModelGrowth *g, OoDiopModel *model,
                        const char *cmd, EFI_FILE_HANDLE Root) {
    if (!cmd) return 0;

    if (_g_cstrcmp(cmd, "/growth_status", 14) == 0) {
        oo_growth_print_status(g); return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_check", 13) == 0) {
        OoGrowthReason r = oo_growth_should_grow(g, model);
        if (r == OO_GROWTH_REASON_NONE)
            Print(L"[growth] No growth needed\r\n");
        return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_run", 11) == 0) {
        oo_growth_run_pipeline(g, model, Root); return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_approve", 15) == 0) {
        if (!g->pending_approval) {
            Print(L"[growth] Nothing pending approval\r\n"); return 1;
        }
        oo_growth_apply(g, model, Root); return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_reject", 14) == 0) {
        g->pending_approval = 0;
        Print(L"[growth] Growth rejected\r\n"); return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_candidates", 18) == 0) {
        oo_growth_find_candidates(g, OO_GROWTH_REASON_USER_REQ);
        for (int i = 0; i < g->n_candidates; i++) {
            Print(L"  [%d] %a D+=%.0f +%.0f%%\r\n",
                  i, g->candidates[i].name,
                  (double)g->candidates[i].dplus_score,
                  (double)(g->candidates[i].expected_improvement * 100));
        }
        return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_auto_on", 15) == 0) {
        g->auto_approve = 1;
        Print(L"[growth] Auto-approve ON — growth will apply without asking\r\n");
        Print(L"[growth] Use /growth_auto_off to restore safety gate\r\n");
        return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_auto_off", 16) == 0) {
        g->auto_approve = 0;
        Print(L"[growth] Auto-approve OFF — human approval required\r\n");
        return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_record ", 15) == 0) {
        /* /growth_record <topic> <confidence 0-100> [fail] */
        const char *p = cmd + 15;
        static CHAR8 topic[64]; UINTN ti = 0;
        while (*p && *p != ' ' && ti < 63) topic[ti++] = (CHAR8)*p++;
        topic[ti] = 0;
        while (*p == ' ') p++;
        float conf = 0.0f;
        while (*p >= '0' && *p <= '9') conf = conf*10 + (*p++ - '0');
        conf = conf / 100.0f;
        int failed = 0;
        while (*p == ' ') p++;
        if (*p == 'f' || *p == 'F') failed = 1;
        oo_growth_record(g, topic, conf, failed);
        Print(L"[growth] Recorded: %a conf=%.0f%% failed=%d\r\n",
              topic, (double)(conf*100), failed);
        return 1;
    }
    if (_g_cstrcmp(cmd, "/growth_log", 11) == 0) {
        oo_growth_log(g, Root, (const CHAR8*)"MANUAL_LOG");
        Print(L"[growth] Logged to %a\r\n",
              (const CHAR8*)OO_GROWTH_LOG_PATH);
        return 1;
    }
    return 0;
}
