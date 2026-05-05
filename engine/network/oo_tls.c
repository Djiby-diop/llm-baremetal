/* oo_tls.c — OO TLS Abstraction Layer  Phase 3
 * ================================================
 * Phase 3 stub implementation.
 * - OO_TLS_MODE_PROXY: builds HTTP URL from proxy_host:proxy_port + real host/path,
 *   routes through oo_netboot HTTP client (already implemented in Phase 2).
 * - OO_TLS_MODE_DIRECT: reserved for mbedTLS port (Phase 4).
 *
 * Freestanding C11. No libc. No dynamic allocation.
 */
#include "oo_tls.h"
#include "oo_netboot.h"
#include <efi.h>
#include <efilib.h>

/* Global singleton */
OoTlsCtx g_oo_tls;

/* ── String helpers ─────────────────────────────────────────────────────── */
static UINTN _tls_strlen(const CHAR8 *s) {
    UINTN n=0; if(!s)return 0; while(s[n])n++; return n;
}
static void _tls_strlcpy(CHAR8 *d, const CHAR8 *s, UINTN cap) {
    UINTN i=0; while(i+1<cap&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static void _tls_memcpy(void *d, const void *s, UINTN n) {
    for(UINTN i=0;i<n;i++)((CHAR8*)d)[i]=((const CHAR8*)s)[i];
}
static int _tls_strncmp(const char *a, const char *b, int n) {
    for(int i=0;i<n;i++){
        if(!a[i]&&!b[i])return 0;
        if(a[i]!=b[i])return (int)(unsigned char)a[i]-(int)(unsigned char)b[i];
    }
    return 0;
}
static UINT32 _tls_atou(const CHAR8 *s) {
    UINT32 v=0; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} return v;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
void oo_tls_init(OoTlsCtx *ctx, OoTlsMode mode) {
    if (!ctx) return;
    for (UINTN i=0;i<sizeof(*ctx);i++) ((CHAR8*)ctx)[i]=0;
    ctx->mode  = mode;
    ctx->state = OO_TLS_STATE_READY;
    /* Default proxy: localhost:8080 */
    _tls_strlcpy(ctx->proxy_host, (const CHAR8*)"127.0.0.1", 64);
    ctx->proxy_port = 8080;
    Print(L"[tls] OO TLS layer ready (mode=%s)\r\n",
          mode == OO_TLS_MODE_DIRECT ? L"DIRECT(stub)" : L"PROXY");
}

void oo_tls_set_proxy(OoTlsCtx *ctx, const CHAR8 *host, UINT16 port) {
    if (!ctx || !host) return;
    _tls_strlcpy(ctx->proxy_host, host, 64);
    ctx->proxy_port = port ? port : 8080;
    Print(L"[tls] Proxy set: %a:%d\r\n", ctx->proxy_host, (int)ctx->proxy_port);
}

void oo_tls_set_token(OoTlsCtx *ctx, const CHAR8 *token) {
    if (!ctx || !token) return;
    _tls_strlcpy(ctx->bearer_token, token, OO_TLS_TOKEN_MAX);
    Print(L"[tls] Bearer token stored (%u chars)\r\n", (UINT32)_tls_strlen(token));
}

void oo_tls_print_status(const OoTlsCtx *ctx) {
    if (!ctx) return;
    Print(L"\r\n  [OO TLS Status]\r\n");
    Print(L"  Mode    : %s\r\n",
          ctx->mode == OO_TLS_MODE_DIRECT ? L"DIRECT(mbedTLS stub)" : L"PROXY");
    Print(L"  State   : %s\r\n",
          ctx->state == OO_TLS_STATE_READY ? L"READY" :
          ctx->state == OO_TLS_STATE_ERROR ? L"ERROR" : L"UNINIT");
    Print(L"  Proxy   : %a:%d\r\n", ctx->proxy_host, (int)ctx->proxy_port);
    Print(L"  Token   : %s\r\n", ctx->bearer_token[0] ? L"[set]" : L"[none]");
    Print(L"  Sent    : %d  OK: %d  Failed: %d\r\n",
          ctx->requests_sent, ctx->requests_ok, ctx->requests_failed);
    Print(L"\r\n");
}

/* ── Proxy URL builder ───────────────────────────────────────────────────── */
/*
 * Proxy mode: OO cannot do TLS natively.
 * Instead, the request is rewritten as:
 *   http://<proxy_host>:<proxy_port>/tls_relay?host=<host>&port=<port>&path=<path>
 * The proxy (oo-oracle-proxy.py) handles the TLS upstream connection.
 *
 * For oracle calls (already handled by oo_netboot_oracle_query), this layer
 * is complementary — it handles arbitrary HTTPS GET/POST not tied to oracle IDs.
 */
static EFI_STATUS _tls_proxy_request(OoTlsCtx *ctx,
                                      int is_post,
                                      const CHAR8 *host, UINT16 port,
                                      const CHAR8 *path,
                                      const CHAR8 *json_body,
                                      CHAR8 *resp_buf, UINTN *resp_len) {
    /* Build relay URL */
    static CHAR8 relay_url[512];
    UINTN up = 0;

#define _URL(lit) do { \
    const CHAR8 *_s=(const CHAR8*)(lit); \
    UINTN _l=_tls_strlen(_s); \
    if(up+_l<510){_tls_memcpy(relay_url+up,_s,_l);up+=_l;} \
} while(0)

    _URL("http://");
    UINTN pl=_tls_strlen(ctx->proxy_host);
    if(up+pl<510){_tls_memcpy(relay_url+up,ctx->proxy_host,pl);up+=pl;}
    relay_url[up++]=':';
    /* port decimal */
    UINT16 pp=ctx->proxy_port;
    CHAR8 pbuf[8]; int pi=6; pbuf[7]=0;
    do{pbuf[pi--]='0'+(pp%10);pp/=10;}while(pp&&pi>=0);
    UINTN plen=_tls_strlen(pbuf+pi+1);
    _tls_memcpy(relay_url+up,pbuf+pi+1,plen); up+=plen;
    _URL("/tls_relay?host=");
    UINTN hl=_tls_strlen(host);
    if(up+hl<510){_tls_memcpy(relay_url+up,host,hl);up+=hl;}
    _URL("&port=");
    UINT16 rp=port?port:443;
    CHAR8 rpbuf[8]; int ri=6; rpbuf[7]=0;
    do{rpbuf[ri--]='0'+(rp%10);rp/=10;}while(rp&&ri>=0);
    UINTN rlen=_tls_strlen(rpbuf+ri+1);
    _tls_memcpy(relay_url+up,rpbuf+ri+1,rlen); up+=rlen;
    _URL("&path=");
    UINTN pathl=_tls_strlen(path);
    if(up+pathl<510){_tls_memcpy(relay_url+up,path,pathl);up+=pathl;}
    relay_url[up]=0;

#undef _URL

    ctx->requests_sent++;

    EFI_STATUS st;
    if (is_post && json_body) {
        st = oo_netboot_oracle_query(&g_netboot, OO_ORACLE_CUSTOM,
                                     json_body, resp_buf,
                                     resp_len ? *resp_len : OO_TLS_RESP_MAX);
    } else {
        /* GET: use oracle_query with empty body — proxy routes by URL */
        st = oo_netboot_oracle_query(&g_netboot, OO_ORACLE_CUSTOM,
                                     relay_url, resp_buf,
                                     resp_len ? *resp_len : OO_TLS_RESP_MAX);
    }

    if (!EFI_ERROR(st)) {
        ctx->requests_ok++;
        if (resp_len) *resp_len = _tls_strlen(resp_buf);
    } else {
        ctx->requests_failed++;
    }
    return st;
}

/* ── Public API ─────────────────────────────────────────────────────────── */
EFI_STATUS oo_tls_https_get(OoTlsCtx *ctx,
                             const CHAR8 *host, UINT16 port,
                             const CHAR8 *path,
                             CHAR8 *resp_buf, UINTN *resp_len) {
    if (!ctx || !host || !path || !resp_buf) return EFI_INVALID_PARAMETER;
    if (ctx->mode == OO_TLS_MODE_DIRECT) {
        /* Phase 4 placeholder — mbedTLS not yet ported */
        Print(L"[tls] DIRECT mode: mbedTLS not yet implemented — use PROXY mode\r\n");
        return EFI_UNSUPPORTED;
    }
    return _tls_proxy_request(ctx, 0, host, port, path, NULL, resp_buf, resp_len);
}

EFI_STATUS oo_tls_https_post_json(OoTlsCtx *ctx,
                                   const CHAR8 *host, UINT16 port,
                                   const CHAR8 *path,
                                   const CHAR8 *json_body,
                                   CHAR8 *resp_buf, UINTN *resp_len) {
    if (!ctx || !host || !path || !json_body || !resp_buf) return EFI_INVALID_PARAMETER;
    if (ctx->mode == OO_TLS_MODE_DIRECT) {
        Print(L"[tls] DIRECT mode: mbedTLS not yet implemented — use PROXY mode\r\n");
        return EFI_UNSUPPORTED;
    }
    return _tls_proxy_request(ctx, 1, host, port, path, json_body, resp_buf, resp_len);
}

/* ── REPL commands ───────────────────────────────────────────────────────── */
int oo_tls_repl_cmd(OoTlsCtx *ctx, const char *cmd) {
    if (!cmd) return 0;

    if (_tls_strncmp(cmd, "/tls_status", 11) == 0) {
        oo_tls_print_status(ctx); return 1;
    }
    if (_tls_strncmp(cmd, "/tls_mode proxy", 15) == 0) {
        ctx->mode = OO_TLS_MODE_PROXY;
        Print(L"[tls] Mode set: PROXY\r\n"); return 1;
    }
    if (_tls_strncmp(cmd, "/tls_mode direct", 16) == 0) {
        ctx->mode = OO_TLS_MODE_DIRECT;
        Print(L"[tls] Mode set: DIRECT (stub — Phase 4)\r\n"); return 1;
    }
    if (_tls_strncmp(cmd, "/tls_proxy ", 11) == 0) {
        const char *rest = cmd + 11;
        while (*rest == ' ') rest++;
        CHAR8 host[64]={0}; int hi=0;
        while (*rest && *rest != ' ' && hi < 63) host[hi++]=(CHAR8)*rest++;
        host[hi]=0;
        while (*rest == ' ') rest++;
        UINT16 port = 8080;
        if (*rest >= '0' && *rest <= '9') port=(UINT16)_tls_atou((const CHAR8*)rest);
        oo_tls_set_proxy(ctx, host, port);
        return 1;
    }
    if (_tls_strncmp(cmd, "/tls_token ", 11) == 0) {
        oo_tls_set_token(ctx, (const CHAR8*)(cmd + 11)); return 1;
    }
    /* /tls_get <host> <path> — test HTTPS GET */
    if (_tls_strncmp(cmd, "/tls_get ", 9) == 0) {
        const char *rest = cmd + 9;
        CHAR8 host[128]={0}; int hi=0;
        while (*rest && *rest != ' ' && hi < 127) host[hi++]=(CHAR8)*rest++;
        host[hi]=0;
        while (*rest == ' ') rest++;
        static CHAR8 resp[4096]; resp[0]=0;
        UINTN rlen = sizeof(resp)-1;
        EFI_STATUS st = oo_tls_https_get(ctx, host, 443,
                                          (const CHAR8*)(*rest ? rest : "/"),
                                          resp, &rlen);
        if (!EFI_ERROR(st)) {
            Print(L"[tls] GET response (%u bytes):\r\n", (UINT32)rlen);
            UINTN show = rlen > 512 ? 512 : rlen;
            for (UINTN i=0;i<show;i++) Print(L"%c",(CHAR16)resp[i]);
            Print(L"\r\n");
        } else {
            Print(L"[tls] GET failed: %r\r\n", st);
        }
        return 1;
    }
    return 0;
}
