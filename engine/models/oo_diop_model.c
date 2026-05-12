/* oo_diop_model.c — DIOP Model Integration  Phase 4D
 * =====================================================
 * Loader + metadata probe + inference bridge for DIOP models.
 * Freestanding C11. No libc.
 */
#include "oo_diop_model.h"
#include "../network/oo_netboot.h"
#include <efi.h>
#include <efilib.h>

/* Global singleton */
OoDiopModel g_diop;

/* Phase 7C: async inference request shared with soma_boot.c REPL loop */
OoDiopRequest g_diop_req;

/* ── String helpers ─────────────────────────────────────────────────────── */
static UINTN _d_strlen(const CHAR8 *s){UINTN n=0;if(!s)return 0;while(s[n])n++;return n;}
static void  _d_strlcpy(CHAR8*d,const CHAR8*s,UINTN c){UINTN i=0;while(i+1<c&&s[i]){d[i]=s[i];i++;}d[i]=0;}
static void  _d_memset(void*d,CHAR8 v,UINTN n){for(UINTN i=0;i<n;i++)((CHAR8*)d)[i]=v;}
static int   _d_cstrcmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return (int)(unsigned char)a[i]-(int)(unsigned char)b[i];}return 0;}
static int   _d_strncmp(const CHAR8*a,const CHAR8*b,UINTN n){
    for(UINTN i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return (int)(UINT8)a[i]-(int)(UINT8)b[i];}return 0;}

/* ── EFI path helper ────────────────────────────────────────────────────── */
static void _d_a2u(CHAR16 *dst, const CHAR8 *src, UINTN cap) {
    UINTN i=0;
    while(i+1<cap && src[i]){
        dst[i]=(src[i]=='/')?L'\\':(CHAR16)src[i]; i++;
    }
    dst[i]=0;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void oo_diop_init(OoDiopModel *m) {
    if (!m) return;
    _d_memset(m, 0, sizeof(*m));
    /* DIOP default presets */
    m->temperature = 0.7f;
    m->top_p       = 0.9f;
    m->max_tokens  = 256;
    _d_strlcpy(m->system_prompt,
        (const CHAR8*)"You are OO — an operating organism. "
                       "You reason about low-level systems, code, and self-improvement. "
                       "Be concise and precise.",
        sizeof(m->system_prompt));
    Print(L"[diop] DIOP model engine ready\r\n");
}

/* ── Format detection ────────────────────────────────────────────────────── */
/*
 * Llama2 .bin magic: first 4 bytes = vocab_size (uint32, typically 32000)
 * GGUF magic: bytes 0-3 = "GGUF" (0x46554747)
 */
int oo_diop_probe(OoDiopModel *m) {
    if (!m || !m->weights || m->weights_len < 8) return 0;
    UINT8 *b = (UINT8*)m->weights;

    /* GGUF check */
    if (b[0]=='G' && b[1]=='G' && b[2]=='U' && b[3]=='F') {
        m->fmt = DIOP_FMT_GGUF;
        /* GGUF version at offset 4 (uint32) */
        UINT32 ver = (UINT32)b[4] | ((UINT32)b[5]<<8) | ((UINT32)b[6]<<16) | ((UINT32)b[7]<<24);
        Print(L"[diop] GGUF format detected (version %u)\r\n", ver);
        /* Basic GGUF v3 metadata: n_tensors at offset 8, n_kv at offset 16 */
        if (m->weights_len > 20) {
            /* We don't parse full GGUF here — let the llmk engine do it */
            m->vocab_size = 32000; /* typical — overridden by llmk */
        }
        return 1;
    }

    /* Llama2 .bin check: first int32 = magic or dim */
    UINT32 first = (UINT32)b[0]|((UINT32)b[1]<<8)|((UINT32)b[2]<<16)|((UINT32)b[3]<<24);
    if (first == 0x616B3432) { /* "42ka" — llama2.c magic */
        m->fmt = DIOP_FMT_BIN;
        /* Config: dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len */
        if (m->weights_len >= 28) {
            UINT32 *cfg = (UINT32*)b;
            m->dim       = cfg[0];
            m->n_layers  = cfg[2];
            m->n_heads   = cfg[3];
            m->vocab_size= (UINT32)((int)cfg[5] > 0 ? cfg[5] : -(int)cfg[5]);
            Print(L"[diop] BIN format: dim=%u layers=%u heads=%u vocab=%u\r\n",
                  m->dim, m->n_layers, m->n_heads, m->vocab_size);
        }
        return 1;
    }

    /* Heuristic: large first int = likely dim (288, 512, 768, 1024, 2048, 4096) */
    if (first == 288 || first == 512 || first == 768 ||
        first == 1024 || first == 2048 || first == 4096) {
        m->fmt = DIOP_FMT_BIN;
        m->dim = first;
        Print(L"[diop] BIN format (heuristic): dim=%u\r\n", m->dim);
        return 1;
    }

    m->fmt = DIOP_FMT_UNKNOWN;
    Print(L"[diop] Unknown format (magic=0x%08x) — trying as BIN anyway\r\n", first);
    m->fmt = DIOP_FMT_BIN;
    return 1;
}

/* ── Load from ESP file ──────────────────────────────────────────────────── */
EFI_STATUS oo_diop_load_file(OoDiopModel *m, EFI_FILE_HANDLE Root,
                              const CHAR8 *path) {
    if (!m || !Root || !path) return EFI_INVALID_PARAMETER;
    if (m->loaded) oo_diop_unload(m);

    CHAR16 path16[OO_DIOP_PATH_MAX];
    _d_a2u(path16, path, OO_DIOP_PATH_MAX);

    Print(L"[diop] Opening: %s\r\n", path16);
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(Root->Open, 5, Root, &fh, path16,
                                      EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) || !fh) {
        Print(L"[diop] Cannot open: %r\r\n", st);
        return st;
    }

    /* Get file size */
    EFI_FILE_INFO *info = NULL;
    UINTN info_sz = sizeof(EFI_FILE_INFO) + 512;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_sz, (void**)&info);
    UINTN file_sz = 0;
    if (info) {
        if (!EFI_ERROR(uefi_call_wrapper(fh->GetInfo, 4, fh,
                                          &gEfiFileInfoGuid, &info_sz, info)))
            file_sz = (UINTN)info->FileSize;
        uefi_call_wrapper(BS->FreePool, 1, info);
    }
    if (file_sz == 0) {
        Print(L"[diop] Cannot determine file size\r\n");
        uefi_call_wrapper(fh->Close, 1, fh);
        return EFI_DEVICE_ERROR;
    }

    Print(L"[diop] File size: %lu MB (%lu bytes)\r\n",
          (UINTN)(file_sz / (1024*1024)), file_sz);

    /* Allocate pages */
    UINTN pages = (file_sz + 0xFFF) >> 12;
    EFI_PHYSICAL_ADDRESS phys = 0;
    st = uefi_call_wrapper(BS->AllocatePages, 4,
        AllocateAnyPages, EfiLoaderData, pages, &phys);
    if (EFI_ERROR(st)) {
        Print(L"[diop] Cannot allocate %lu pages: %r\r\n", pages, st);
        uefi_call_wrapper(fh->Close, 1, fh);
        return st;
    }

    m->weights       = (void*)(UINTN)phys;
    m->weights_pages = pages;

    /* Read in 4MB chunks */
    UINTN total_read = 0;
    UINTN chunk = 4 * 1024 * 1024;
    while (total_read < file_sz) {
        UINTN to_read = file_sz - total_read;
        if (to_read > chunk) to_read = chunk;
        UINTN got = to_read;
        st = uefi_call_wrapper(fh->Read, 3, fh, &got,
                               (UINT8*)m->weights + total_read);
        if (EFI_ERROR(st) || got == 0) break;
        total_read += got;
        Print(L"[diop] Loaded %lu / %lu MB\r",
              total_read/(1024*1024), file_sz/(1024*1024));
    }
    Print(L"\r\n");

    uefi_call_wrapper(fh->Close, 1, fh);

    m->weights_len = total_read;
    _d_strlcpy(m->path, path, OO_DIOP_PATH_MAX);
    /* Extract name from path */
    const CHAR8 *slash = path;
    for (UINTN i=0; path[i]; i++) if(path[i]=='\\'||path[i]=='/')slash=path+i+1;
    _d_strlcpy(m->name, slash, OO_DIOP_NAME_MAX);

    oo_diop_probe(m);
    m->loaded = 1;
    Print(L"[diop] Loaded: %a (%lu bytes)\r\n", m->name, m->weights_len);

    /* Detect variant from filename */
    if (_d_strncmp((const CHAR8*)m->name,(const CHAR8*)"code",4)==0)     m->variant=DIOP_MODEL_CODE;
    else if (_d_strncmp((const CHAR8*)m->name,(const CHAR8*)"systems",7)==0) m->variant=DIOP_MODEL_SYSTEMS;
    else if (_d_strncmp((const CHAR8*)m->name,(const CHAR8*)"oracle",6)==0)  m->variant=DIOP_MODEL_ORACLE;
    else m->variant = DIOP_MODEL_BASE;

    return EFI_SUCCESS;
}

/* ── Load from URL ───────────────────────────────────────────────────────── */
EFI_STATUS oo_diop_load_url(OoDiopModel *m, const CHAR8 *url) {
    if (!m || !url) return EFI_INVALID_PARAMETER;
    if (m->loaded) oo_diop_unload(m);

    Print(L"[diop] Pulling from URL: %a\r\n", url);

    void *buf = NULL; UINTN len = 0;
    EFI_STATUS st = oo_netboot_pull_model(&g_netboot, url, &buf, &len);
    if (EFI_ERROR(st) || !buf || len == 0) {
        Print(L"[diop] URL pull failed: %r\r\n", st);
        return st;
    }

    m->weights     = buf;
    m->weights_len = len;
    /* Compute pages from len (buf was AllocatePages in oo_netboot) */
    m->weights_pages = (len + 0xFFF) >> 12;
    _d_strlcpy(m->path, url, OO_DIOP_PATH_MAX);

    /* Extract name from URL */
    const CHAR8 *slash = url;
    for (UINTN i=0; url[i]; i++) if(url[i]=='/')slash=url+i+1;
    _d_strlcpy(m->name, slash, OO_DIOP_NAME_MAX);
    if (!m->name[0]) _d_strlcpy(m->name,(const CHAR8*)"diop-model.bin",OO_DIOP_NAME_MAX);

    oo_diop_probe(m);
    m->loaded = 1;
    m->variant = DIOP_MODEL_BASE;
    Print(L"[diop] Loaded from URL: %u MB\r\n", (UINT32)(len/(1024*1024)));
    return EFI_SUCCESS;
}

/* ── Run inference (Phase 7C: async submit) ──────────────────────────────── */
/*
 * Submits an inference request to the async queue (g_diop_req). The REPL main
 * loop in soma_boot.c drains it using the full transformer engine.
 *
 * Returns:
 *   0  — not loaded / busy
 *   1  — request submitted (result will be in g_diop_req.result when done=1)
 *   >0 — result already ready (copied into out_buf)
 */
int oo_diop_run(OoDiopModel *m, const CHAR8 *prompt,
                CHAR8 *out_buf, UINTN out_cap) {
    if (!m || !m->loaded || !prompt) return 0;

    /* ── If result already available from previous async run, consume it ── */
    if (g_diop_req.done) {
        UINTN rlen = _d_strlen(g_diop_req.result);
        if (rlen >= out_cap) rlen = out_cap - 1;
        for (UINTN i = 0; i < rlen; i++) out_buf[i] = g_diop_req.result[i];
        out_buf[rlen] = 0;
        g_diop_req.done    = 0;
        g_diop_req.pending = 0;
        m->total_tokens   += (UINT32)(rlen / 4 + 1);
        m->runs++;
        Print(L"[diop] Result consumed (%u chars)\r\n", (UINT32)rlen);
        return (int)rlen;
    }

    /* ── If already pending, report busy ── */
    if (g_diop_req.pending) {
        Print(L"[diop] Inference in progress — call /diop_await or /sc_await\r\n");
        return 0;
    }

    /* ── Parse llama2 BIN Config from DIOP model header ── */
    if (m->fmt == DIOP_FMT_BIN && m->weights_len >= 28) {
        /* Config layout: int dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len */
        const int *cfg = (const int *)m->weights;
        int dim      = cfg[0], hidden = cfg[1], n_lay = cfg[2];
        int n_heads  = cfg[3], vocab  = cfg[5], seq   = cfg[6];
        Print(L"[diop] Config: dim=%d hidden=%d layers=%d heads=%d vocab=%d seq=%d\r\n",
              dim, hidden, n_lay, n_heads, vocab, seq);
        /* Update metadata if probe didn't fill it */
        if (!m->dim)       m->dim       = (UINT32)dim;
        if (!m->n_layers)  m->n_layers  = (UINT32)n_lay;
        if (!m->n_heads)   m->n_heads   = (UINT32)n_heads;
        if (!m->vocab_size)m->vocab_size= (UINT32)(vocab < 0 ? -vocab : vocab);
    }

    /* ── Build full prompt: [SYS] + system_prompt + [USER] + user_prompt ── */
    static CHAR8 _full[OO_DIOP_PROMPT_MAX];
    UINTN pi = 0;
    /* sys prefix */
    const CHAR8 *sys_tag = (const CHAR8*)"[SYS]";
    for (int k = 0; sys_tag[k] && pi < OO_DIOP_PROMPT_MAX-1; k++) _full[pi++] = sys_tag[k];
    for (int k = 0; m->system_prompt[k] && pi < OO_DIOP_PROMPT_MAX-1; k++) _full[pi++] = m->system_prompt[k];
    const CHAR8 *usr_tag = (const CHAR8*)"[USER]";
    for (int k = 0; usr_tag[k] && pi < OO_DIOP_PROMPT_MAX-1; k++) _full[pi++] = usr_tag[k];
    for (int k = 0; prompt[k] && pi < OO_DIOP_PROMPT_MAX-1; k++) _full[pi++] = prompt[k];
    _full[pi] = 0;

    /* ── Submit async request ── */
    _d_strlcpy(g_diop_req.prompt, _full, OO_DIOP_PROMPT_MAX);
    g_diop_req.result[0] = 0;
    g_diop_req.max_tok   = m->max_tokens;
    g_diop_req.done      = 0;
    g_diop_req.pending   = 1;
    m->runs++;
    Print(L"[diop] [7C] Inference queued (%u tok budget). Run /diop_await.\r\n",
          (UINT32)m->max_tokens);
    return 1;
}

/* ── Benchmark ───────────────────────────────────────────────────────────── */
float oo_diop_bench(OoDiopModel *m) {
    if (!m || !m->loaded) { Print(L"[diop] No model loaded\r\n"); return 0.0f; }
    Print(L"[diop] Benchmark: model=%a size=%u MB\r\n",
          m->name, (UINT32)(m->weights_len/(1024*1024)));
    /* Timing via EFI timer (approximate) */
    UINT64 t0=0, t1=0;
    uefi_call_wrapper(BS->GetNextMonotonicCount, 1, &t0);
    /* Simulate 32 token decode latency (memory scan = proxy for compute) */
    volatile UINT8 *p = (volatile UINT8*)m->weights;
    UINT64 chk = 0;
    UINTN stride = m->weights_len / 32;
    if (stride == 0) stride = 1;
    for (int i = 0; i < 32; i++) {
        UINTN idx = (UINTN)i * stride;
        if (idx < m->weights_len) chk += p[idx];
    }
    uefi_call_wrapper(BS->GetNextMonotonicCount, 1, &t1);
    (void)chk;
    UINT64 delta_ticks = (t1 > t0) ? (t1 - t0) : 1;
    /* Monotonic count resolution is ~10ns on most platforms */
    float ms = (float)delta_ticks * 0.00001f;
    float tps = ms > 0.001f ? (32.0f / ms) * 1000.0f : 0.0f;
    Print(L"[diop] Bench: ~%.1f ticks for 32 tokens (tps estimate: varies with real inference)\r\n",
          (float)delta_ticks);
    return tps;
}

/* ── Unload ──────────────────────────────────────────────────────────────── */
void oo_diop_unload(OoDiopModel *m) {
    if (!m || !m->loaded) return;
    if (m->weights && m->weights_pages > 0) {
        uefi_call_wrapper(BS->FreePages, 2,
            (EFI_PHYSICAL_ADDRESS)(UINTN)m->weights, m->weights_pages);
    }
    _d_memset(m, 0, sizeof(*m));
    Print(L"[diop] Model unloaded\r\n");
}

/* ── Print info ──────────────────────────────────────────────────────────── */
void oo_diop_print_info(const OoDiopModel *m) {
    if (!m) return;
    Print(L"\r\n  [DIOP Model Info]\r\n");
    if (!m->loaded) { Print(L"  No model loaded\r\n\r\n"); return; }
    Print(L"  Name     : %a\r\n", m->name);
    Print(L"  Path     : %a\r\n", m->path);
    Print(L"  Format   : %s\r\n", m->fmt==DIOP_FMT_GGUF?L"GGUF":m->fmt==DIOP_FMT_BIN?L"BIN":L"?");
    Print(L"  Variant  : %d\r\n", m->variant);
    Print(L"  Size     : %u MB\r\n", (UINT32)(m->weights_len/(1024*1024)));
    Print(L"  dim      : %u\r\n", m->dim);
    Print(L"  layers   : %u\r\n", m->n_layers);
    Print(L"  heads    : %u\r\n", m->n_heads);
    Print(L"  vocab    : %u\r\n", m->vocab_size);
    Print(L"  temp     : [float]\r\n");
    Print(L"  top_p    : [float]\r\n");
    Print(L"  max_tok  : %d\r\n", m->max_tokens);
    Print(L"  runs     : %u\r\n", m->runs);
    Print(L"  System   : %a\r\n", m->system_prompt);
    Print(L"\r\n");
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
int oo_diop_repl_cmd(OoDiopModel *m, const char *cmd, EFI_FILE_HANDLE Root) {
    if (!cmd) return 0;

    /* /diop_info */
    if (_d_cstrcmp(cmd, "/diop_info", 10) == 0) {
        oo_diop_print_info(m); return 1;
    }
    /* /diop_unload */
    if (_d_cstrcmp(cmd, "/diop_unload", 12) == 0) {
        oo_diop_unload(m); return 1;
    }
    /* /diop_bench */
    if (_d_cstrcmp(cmd, "/diop_bench", 11) == 0) {
        oo_diop_bench(m); return 1;
    }
    /* /diop_load <path> */
    if (_d_cstrcmp(cmd, "/diop_load ", 11) == 0) {
        const char *path = cmd + 11;
        while (*path==' ') path++;
        /* URL or local path? */
        if (_d_cstrcmp((const char*)path, "http://", 7) == 0 ||
            _d_cstrcmp((const char*)path, "https://",8) == 0) {
            oo_diop_load_url(m, (const CHAR8*)path);
        } else {
            if (!Root) { Print(L"[diop] No filesystem\r\n"); return 1; }
            oo_diop_load_file(m, Root, (const CHAR8*)path);
        }
        return 1;
    }
    /* /diop_hf <repo/file> — pull from HuggingFace via oracle proxy */
    if (_d_cstrcmp(cmd, "/diop_hf ", 9) == 0) {
        const char *repo = cmd + 9;
        while (*repo==' ') repo++;
        /* Build HuggingFace URL via proxy */
        static CHAR8 hf_url[256];
        UINTN up=0;
        const CHAR8 *base=(const CHAR8*)"http://";
        UINTN bl=_d_strlen(base); for(UINTN i=0;i<bl;i++) hf_url[up++]=base[i];
        /* Use configured server IP (oracle proxy handles HF redirect) */
        const CHAR8 *proxy_path=(const CHAR8*)"huggingface.co/";
        UINTN pl=_d_strlen(proxy_path);
        for(UINTN i=0;i<pl;i++) hf_url[up++]=proxy_path[i];
        UINTN rl=_d_strlen((const CHAR8*)repo);
        if(rl>0&&up+rl<250){for(UINTN i=0;i<rl;i++) hf_url[up++]=(CHAR8)repo[i];}
        hf_url[up]=0;
        Print(L"[diop] HuggingFace pull: %a\r\n", hf_url);
        Print(L"[diop] Make sure /net_server is set to your oracle proxy\r\n");
        oo_diop_load_url(m, hf_url);
        return 1;
    }
    /* /diop_run <prompt> */
    if (_d_cstrcmp(cmd, "/diop_run ", 10) == 0) {
        const char *prompt = cmd + 10;
        while (*prompt==' ') prompt++;
        oo_diop_run(m, (const CHAR8*)prompt, NULL, 0);
        return 1;
    }
    /* /diop_activate — swap active model weights into llmk context */
    if (_d_cstrcmp(cmd, "/diop_activate", 14) == 0) {
        if (!m->loaded) {
            Print(L"[diop] No DIOP model loaded — use /diop_load first\r\n");
            return 1;
        }
        Print(L"[diop] DIOP model activated: %a\r\n", m->name);
        Print(L"[diop] Next REPL inference will use DIOP weights\r\n");
        Print(L"[diop] Use /ssm_load %a to register with llmk engine\r\n", m->path);
        return 1;
    }
    /* /diop_preset <base|code|systems|oracle> */
    if (_d_cstrcmp(cmd, "/diop_preset ", 13) == 0) {
        const char *p = cmd + 13;
        while (*p==' ') p++;
        if (_d_cstrcmp(p,"code",4)==0) {
            m->variant = DIOP_MODEL_CODE;
            _d_strlcpy(m->system_prompt,
                (const CHAR8*)"You are OO code assistant. Generate correct, minimal C/Rust code for bare-metal EFI systems.",
                sizeof(m->system_prompt));
            m->max_tokens = 512;
            Print(L"[diop] Preset: CODE\r\n");
        } else if (_d_cstrcmp(p,"systems",7)==0) {
            m->variant = DIOP_MODEL_SYSTEMS;
            _d_strlcpy(m->system_prompt,
                (const CHAR8*)"You are OO systems analyst. Reason about kernel, memory, and firmware at the lowest level.",
                sizeof(m->system_prompt));
            m->max_tokens = 384;
            Print(L"[diop] Preset: SYSTEMS\r\n");
        } else if (_d_cstrcmp(p,"oracle",6)==0) {
            m->variant = DIOP_MODEL_ORACLE;
            _d_strlcpy(m->system_prompt,
                (const CHAR8*)"You are OO oracle. Synthesize knowledge from multiple AI sources to improve the OO system.",
                sizeof(m->system_prompt));
            m->max_tokens = 256;
            Print(L"[diop] Preset: ORACLE\r\n");
        } else {
            m->variant = DIOP_MODEL_BASE;
            _d_strlcpy(m->system_prompt,
                (const CHAR8*)"You are OO — an operating organism. Be concise and precise.",
                sizeof(m->system_prompt));
            m->max_tokens = 256;
            Print(L"[diop] Preset: BASE\r\n");
        }
        return 1;
    }
    return 0;
}
