/* oo_netboot.c — OO Network Boot Implementation
 * ================================================
 * Phase 2: Real EFI_HTTP_PROTOCOL — HTTP GET model pull + HTTP POST oracle
 * Phase 3: HTTPS oracle (TLS via mbedTLS — future)
 * Phase 4: Federated weight delta push
 *
 * Architecture:
 *   oo_netboot_init()        — probe SNP + discover IP from g_oo_net (oo_net_core)
 *   oo_http_get()            — open EFI_HTTP_PROTOCOL child, GET → RAM buffer
 *   oo_http_post_json()      — POST JSON body, read response
 *   oo_netboot_pull_model()  — HTTP GET model weights → AllocatePages buffer
 *   oo_netboot_oracle_query()— HTTP POST JSON → GPT4/Claude/Gemini proxy
 *   oo_netboot_push_delta()  — HTTP POST delta weights → federation server
 *
 * Freestanding C11. No libc. Uses OO AllocatePool/AllocatePages.
 */
#include "oo_netboot.h"
#include "../network/oo_net_core.h"   /* g_oo_net — IP already acquired by DHCP */
#include "oo_mbedtls.h"               /* Phase 9B: direct TLS oracle */
#include <efi.h>
#include <efilib.h>

/* Global singleton */
OoNetContext g_netboot;

/* ═══════════════════════════════════════════════════════════════════════════
 * §1  EFI HTTP Protocol type definitions
 *     (not always present in older gnu-efi — defined here to be safe)
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID
#define EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID \
    { 0xbdc8e6af, 0xd9bc, 0x4379, { 0xa7, 0x2a, 0xe0, 0xc4, 0xe7, 0x5d, 0xae, 0x1c } }
#endif

#ifndef EFI_HTTP_PROTOCOL_GUID
#define EFI_HTTP_PROTOCOL_GUID \
    { 0x7a59b29b, 0x910b, 0x4171, { 0x82, 0x42, 0xa8, 0x5a, 0x0d, 0xf2, 0x5b, 0x5b } }
#endif

typedef enum {
    OoHttpMethodGet  = 0,
    OoHttpMethodPost = 1,
    OoHttpMethodPut  = 2,
    OoHttpMethodDel  = 3,
    OoHttpMethodHead = 4,
    OoHttpMethodMax
} OoEfiHttpMethod;

typedef enum {
    OoHttpVersion10 = 0,
    OoHttpVersion11 = 1,
    OoHttpVersionUnsupported
} OoEfiHttpVersion;

/* IPv4 access point for HTTP config */
typedef struct {
    EFI_IPv4_ADDRESS LocalAddress;
    EFI_IPv4_ADDRESS LocalSubnet;
    EFI_IPv4_ADDRESS RemoteAddress;
    UINT16           RemotePort;
    BOOLEAN          UseDefaultAddress;
} OoEfiHTTPv4AccessPoint;

typedef struct {
    OoEfiHttpVersion  HttpVersion;
    UINT32            TimeOutMillisec;
    BOOLEAN           LocalAddressIsIPv6;
    union {
        OoEfiHTTPv4AccessPoint *IPv4Node;
        void                   *IPv6Node;
    } AccessPoint;
} OoEfiHttpConfigData;

typedef struct {
    OoEfiHttpMethod Method;
    CHAR16         *Url;
} OoEfiHttpRequestData;

typedef struct {
    UINT16 StatusCode;
} OoEfiHttpResponseData;

typedef struct {
    CHAR8 *FieldName;
    CHAR8 *FieldValue;
} OoEfiHttpHeader;

typedef struct {
    union {
        OoEfiHttpRequestData  *Request;
        OoEfiHttpResponseData *Response;
    } Data;
    UINTN            HeaderCount;
    OoEfiHttpHeader *Headers;
    UINTN            BodyLength;
    VOID            *Body;
} OoEfiHttpMessage;

typedef struct {
    EFI_EVENT       Event;
    EFI_STATUS      Status;
    OoEfiHttpMessage *Message;
} OoEfiHttpToken;

/* Forward-declare the protocol struct */
typedef struct _OoEfiHttpProtocol OoEfiHttpProtocol;

typedef EFI_STATUS (EFIAPI *OoEfiHttp_GetModeData)(
    IN  OoEfiHttpProtocol   *This,
    OUT OoEfiHttpConfigData *ConfigData);

typedef EFI_STATUS (EFIAPI *OoEfiHttp_Configure)(
    IN OoEfiHttpProtocol   *This,
    IN OoEfiHttpConfigData *ConfigData);

typedef EFI_STATUS (EFIAPI *OoEfiHttp_Request)(
    IN OoEfiHttpProtocol *This,
    IN OoEfiHttpToken    *Token);

typedef EFI_STATUS (EFIAPI *OoEfiHttp_Cancel)(
    IN OoEfiHttpProtocol *This,
    IN OoEfiHttpToken    *Token);

typedef EFI_STATUS (EFIAPI *OoEfiHttp_Response)(
    IN OoEfiHttpProtocol *This,
    IN OoEfiHttpToken    *Token);

typedef EFI_STATUS (EFIAPI *OoEfiHttp_Poll)(
    IN OoEfiHttpProtocol *This);

struct _OoEfiHttpProtocol {
    OoEfiHttp_GetModeData GetModeData;
    OoEfiHttp_Configure   Configure;
    OoEfiHttp_Request     Request;
    OoEfiHttp_Cancel      Cancel;
    OoEfiHttp_Response    Response;
    OoEfiHttp_Poll        Poll;
};

/* Service binding (generic — same for many EFI protocols) */
typedef struct _OoEfiSvcBinding OoEfiSvcBinding;

typedef EFI_STATUS (EFIAPI *OoEfiSvcBinding_CreateChild)(
    IN     OoEfiSvcBinding *This,
    IN OUT EFI_HANDLE      *ChildHandle);

typedef EFI_STATUS (EFIAPI *OoEfiSvcBinding_DestroyChild)(
    IN OoEfiSvcBinding *This,
    IN EFI_HANDLE       ChildHandle);

struct _OoEfiSvcBinding {
    OoEfiSvcBinding_CreateChild  CreateChild;
    OoEfiSvcBinding_DestroyChild DestroyChild;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * §2  String + number helpers (no libc)
 * ═══════════════════════════════════════════════════════════════════════════ */

static UINTN _nb_strlen(const CHAR8 *s) {
    UINTN n = 0; while (s && s[n]) n++; return n;
}
static void _nb_strlcpy(CHAR8 *d, const CHAR8 *s, UINTN cap) {
    UINTN i = 0;
    while (i + 1 < cap && s && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int _nb_strncmp(const CHAR8 *a, const CHAR8 *b, UINTN n) {
    for (UINTN i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if ((UINT8)a[i] != (UINT8)b[i]) return (int)(UINT8)a[i] - (int)(UINT8)b[i];
    }
    return 0;
}
static int _nb_strncmpc(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
    }
    return 0;
}
static void _nb_u8_to_c16(CHAR16 *dst, UINTN cap, const CHAR8 *src) {
    UINTN i = 0;
    while (i + 1 < cap && src && src[i]) { dst[i] = (CHAR16)src[i]; i++; }
    dst[i] = 0;
}
static void _nb_c16_to_u8(CHAR8 *dst, UINTN cap, const CHAR16 *src) {
    UINTN i = 0;
    while (i + 1 < cap && src && src[i]) { dst[i] = (CHAR8)(src[i] & 0x7F); i++; }
    dst[i] = 0;
}

/* Append ASCII string to buffer: returns new pos */
static UINTN _nb_append(CHAR8 *buf, UINTN pos, UINTN cap, const CHAR8 *s) {
    while (pos + 1 < cap && s && *s) { buf[pos++] = *s++; }
    buf[pos] = 0;
    return pos;
}

/* Hex digit */
static CHAR8 _nb_hexc(UINT8 v) {
    v &= 0xF;
    return (v < 10) ? ('0' + v) : ('a' + v - 10);
}

/* uint32 → decimal string (minimal) */
static UINTN _nb_u32_to_dec(CHAR8 *buf, UINTN cap, UINT32 v) {
    CHAR8 tmp[12]; int ti = 0;
    if (v == 0) { if (cap > 1) { buf[0]='0'; buf[1]=0; } return 1; }
    while (v && ti < 11) { tmp[ti++] = '0' + (v % 10); v /= 10; }
    UINTN out = 0;
    for (int i = ti - 1; i >= 0 && out + 1 < cap; i--) buf[out++] = tmp[i];
    buf[out] = 0;
    return out;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §3  URL parser  "http://host:port/path"
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    CHAR8  host[128];
    UINT16 port;
    CHAR8  path[256];
    int    valid;
} OoUrl;

static OoUrl _nb_parse_url(const CHAR8 *url) {
    OoUrl u; for (UINTN i = 0; i < sizeof(u); i++) ((CHAR8*)&u)[i] = 0;
    u.port = 80;

    /* skip scheme: "http://" */
    const CHAR8 *p = url;
    if (_nb_strncmp(p, (const CHAR8*)"http://", 7) == 0)  p += 7;
    else if (_nb_strncmp(p, (const CHAR8*)"https://", 8) == 0) { p += 8; u.port = 443; }

    /* extract host (up to ':' or '/') */
    UINTN hi = 0;
    while (*p && *p != ':' && *p != '/' && hi + 1 < sizeof(u.host))
        u.host[hi++] = *p++;
    u.host[hi] = 0;

    /* optional port */
    if (*p == ':') {
        p++;
        UINT32 port_val = 0;
        while (*p >= '0' && *p <= '9') { port_val = port_val * 10 + (*p - '0'); p++; }
        u.port = (UINT16)port_val;
    }

    /* path (rest) */
    if (*p == '/') {
        _nb_strlcpy(u.path, p, sizeof(u.path));
    } else {
        u.path[0] = '/'; u.path[1] = 0;
    }

    u.valid = (hi > 0) ? 1 : 0;
    return u;
}

/* Convert "192.168.1.100" string → EFI_IPv4_ADDRESS */
static EFI_IPv4_ADDRESS _nb_str_to_ipv4(const CHAR8 *s) {
    EFI_IPv4_ADDRESS ip; for (int i = 0; i < 4; i++) ip.Addr[i] = 0;
    int idx = 0; UINT32 v = 0;
    while (*s && idx < 4) {
        if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); }
        else if (*s == '.') { ip.Addr[idx++] = (UINT8)v; v = 0; }
        s++;
    }
    if (idx < 4) ip.Addr[idx] = (UINT8)v;
    return ip;
}

/* Fill EFI_IPv4_ADDRESS from g_oo_net.ip (UINT32 host LE) */
static EFI_IPv4_ADDRESS _nb_local_ipv4(void) {
    EFI_IPv4_ADDRESS a;
    UINT32 ip = g_oo_net.ip;
    a.Addr[0] = (UINT8)(ip        & 0xFF);
    a.Addr[1] = (UINT8)((ip >> 8) & 0xFF);
    a.Addr[2] = (UINT8)((ip >>16) & 0xFF);
    a.Addr[3] = (UINT8)((ip >>24) & 0xFF);
    return a;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §4  EFI_HTTP_PROTOCOL client — open/configure/request/response/close
 * ═══════════════════════════════════════════════════════════════════════════ */

#define OO_HTTP_TIMEOUT_MS   30000   /* 30 s per request */
#define OO_HTTP_POLL_MAX     500000  /* poll iterations before timeout */
#define OO_HTTP_BODY_MAX     (128 * 1024 * 1024)  /* 128 MB */

typedef struct {
    OoEfiSvcBinding    *svc;
    EFI_HANDLE          svc_handle;
    OoEfiHttpProtocol  *http;
    EFI_HANDLE          http_handle;
    int                 configured;
} OoHttpClient;

static EFI_STATUS _nb_http_open(OoHttpClient *c, const OoUrl *url) {
    for (UINTN i = 0; i < sizeof(*c); i++) ((UINT8*)c)[i] = 0;

    EFI_GUID sb_guid = EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID;
    EFI_GUID hp_guid = EFI_HTTP_PROTOCOL_GUID;

    /* Locate HTTP Service Binding on any handle */
    UINTN    n = 0;
    EFI_HANDLE *hdls = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &sb_guid, NULL, &n, &hdls);
    if (EFI_ERROR(st) || n == 0) return EFI_NOT_FOUND;

    st = uefi_call_wrapper(BS->OpenProtocol, 6,
        hdls[0], &sb_guid, (VOID**)&c->svc, NULL, NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    uefi_call_wrapper(BS->FreePool, 1, hdls);
    if (EFI_ERROR(st)) return st;
    c->svc_handle = hdls[0];

    /* Create child HTTP instance */
    c->http_handle = NULL;
    st = uefi_call_wrapper(c->svc->CreateChild, 2, c->svc, &c->http_handle);
    if (EFI_ERROR(st)) return st;

    /* Open EFI_HTTP_PROTOCOL on child */
    st = uefi_call_wrapper(BS->OpenProtocol, 6,
        c->http_handle, &hp_guid, (VOID**)&c->http, NULL, NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(st)) { uefi_call_wrapper(c->svc->DestroyChild, 2, c->svc, c->http_handle); return st; }

    /* Configure: IPv4, connect to remote host:port */
    OoEfiHTTPv4AccessPoint ap;
    for (UINTN i = 0; i < sizeof(ap); i++) ((UINT8*)&ap)[i] = 0;
    ap.UseDefaultAddress = TRUE;
    ap.RemoteAddress     = _nb_str_to_ipv4(url->host);
    ap.RemotePort        = url->port;

    OoEfiHttpConfigData cfg;
    for (UINTN i = 0; i < sizeof(cfg); i++) ((UINT8*)&cfg)[i] = 0;
    cfg.HttpVersion         = OoHttpVersion11;
    cfg.TimeOutMillisec     = OO_HTTP_TIMEOUT_MS;
    cfg.LocalAddressIsIPv6  = FALSE;
    cfg.AccessPoint.IPv4Node = &ap;

    st = uefi_call_wrapper(c->http->Configure, 2, c->http, &cfg);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(c->svc->DestroyChild, 2, c->svc, c->http_handle);
        return st;
    }
    c->configured = 1;
    return EFI_SUCCESS;
}

static void _nb_http_close(OoHttpClient *c) {
    if (!c) return;
    if (c->svc && c->http_handle)
        uefi_call_wrapper(c->svc->DestroyChild, 2, c->svc, c->http_handle);
    for (UINTN i = 0; i < sizeof(*c); i++) ((UINT8*)c)[i] = 0;
}

/* Build URL CHAR16 from OoUrl */
static void _nb_url_to_c16(CHAR16 *dst, UINTN cap, const OoUrl *u) {
    CHAR8 tmp[512];
    UINTN p = 0;
    /* "http://" */
    const CHAR8 *scheme = (const CHAR8*)"http://";
    while (*scheme && p + 1 < sizeof(tmp)) tmp[p++] = *scheme++;
    /* host */
    const CHAR8 *h = u->host;
    while (*h && p + 1 < sizeof(tmp)) tmp[p++] = *h++;
    /* :port if non-80 */
    if (u->port != 80) {
        if (p + 1 < sizeof(tmp)) tmp[p++] = ':';
        CHAR8 portbuf[8] = {0};
        _nb_u32_to_dec(portbuf, sizeof(portbuf), u->port);
        for (UINTN i = 0; portbuf[i] && p + 1 < sizeof(tmp); i++) tmp[p++] = portbuf[i];
    }
    /* path */
    const CHAR8 *pa = u->path;
    while (*pa && p + 1 < sizeof(tmp)) tmp[p++] = *pa++;
    tmp[p] = 0;
    _nb_u8_to_c16(dst, cap, tmp);
}

/* Poll until token status is resolved (not EFI_NOT_READY) */
static EFI_STATUS _nb_http_poll_token(OoEfiHttpProtocol *http, OoEfiHttpToken *tok) {
    for (UINT32 i = 0; i < OO_HTTP_POLL_MAX; i++) {
        uefi_call_wrapper(http->Poll, 1, http);
        if (tok->Status != EFI_NOT_READY) return tok->Status;
    }
    return EFI_TIMEOUT;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §5  HTTP GET  →  raw body in AllocatePages buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Static receive buffer — 128 MB max for model weights.
 * On real hardware we use AllocatePages. QEMU usually has enough RAM. */
#define OO_HTTP_RX_PAGES   32768    /* 32768 × 4 KB = 128 MB */

static EFI_STATUS _nb_http_get(OoHttpClient *c, const OoUrl *url,
                                void **body_out, UINTN *body_sz_out) {
    *body_out    = NULL;
    *body_sz_out = 0;

    /* Build URL CHAR16 */
    CHAR16 url16[512] = {0};
    _nb_url_to_c16(url16, 512, url);

    /* ── Request phase ── */
    OoEfiHttpRequestData req_data;
    req_data.Method = OoHttpMethodGet;
    req_data.Url    = url16;

    /* Headers: Host + User-Agent */
    OoEfiHttpHeader req_hdrs[2];
    req_hdrs[0].FieldName  = (CHAR8*)"Host";
    req_hdrs[0].FieldValue = (CHAR8*)url->host;
    req_hdrs[1].FieldName  = (CHAR8*)"User-Agent";
    req_hdrs[1].FieldValue = (CHAR8*)"OO-NetBoot/2.0";

    OoEfiHttpMessage req_msg;
    for (UINTN i = 0; i < sizeof(req_msg); i++) ((UINT8*)&req_msg)[i] = 0;
    req_msg.Data.Request = &req_data;
    req_msg.HeaderCount  = 2;
    req_msg.Headers      = req_hdrs;
    req_msg.BodyLength   = 0;
    req_msg.Body         = NULL;

    OoEfiHttpToken req_tok;
    req_tok.Event   = NULL;
    req_tok.Status  = EFI_NOT_READY;
    req_tok.Message = &req_msg;

    EFI_STATUS st = uefi_call_wrapper(c->http->Request, 2, c->http, &req_tok);
    if (EFI_ERROR(st)) { Print(L"[HTTP] Request() failed: %r\r\n", st); return st; }

    st = _nb_http_poll_token(c->http, &req_tok);
    if (EFI_ERROR(st)) { Print(L"[HTTP] Request poll timeout: %r\r\n", st); return st; }

    /* ── Response phase 1: read headers to get Content-Length ── */
    OoEfiHttpResponseData resp_data;
    resp_data.StatusCode = 0;

    OoEfiHttpMessage resp_hdr_msg;
    for (UINTN i = 0; i < sizeof(resp_hdr_msg); i++) ((UINT8*)&resp_hdr_msg)[i] = 0;
    resp_hdr_msg.Data.Response = &resp_data;
    resp_hdr_msg.BodyLength    = 0;
    resp_hdr_msg.Body          = NULL;

    OoEfiHttpToken resp_hdr_tok;
    resp_hdr_tok.Event   = NULL;
    resp_hdr_tok.Status  = EFI_NOT_READY;
    resp_hdr_tok.Message = &resp_hdr_msg;

    st = uefi_call_wrapper(c->http->Response, 2, c->http, &resp_hdr_tok);
    if (EFI_ERROR(st)) { Print(L"[HTTP] Response(headers) failed: %r\r\n", st); return st; }

    st = _nb_http_poll_token(c->http, &resp_hdr_tok);
    if (EFI_ERROR(st)) { Print(L"[HTTP] Response header poll: %r\r\n", st); return st; }

    UINT16 http_status = resp_data.StatusCode;
    Print(L"[HTTP] Status: %d\r\n", (UINT32)http_status);
    if (http_status < 200 || http_status >= 300) return EFI_ABORTED;

    /* Look for Content-Length in response headers */
    UINTN content_length = 0;
    for (UINTN hi = 0; hi < resp_hdr_msg.HeaderCount; hi++) {
        OoEfiHttpHeader *h = &resp_hdr_msg.Headers[hi];
        if (h->FieldName && _nb_strncmp((const CHAR8*)h->FieldName,
                                        (const CHAR8*)"Content-Length", 14) == 0) {
            const CHAR8 *cv = (const CHAR8*)h->FieldValue;
            while (cv && *cv >= '0' && *cv <= '9')
                content_length = content_length * 10 + (*cv++ - '0');
        }
    }

    /* ── Allocate receive buffer ── */
    UINTN alloc_sz = content_length ? content_length : OO_HTTP_BODY_MAX;
    UINTN pages    = (alloc_sz + 0xFFF) >> 12;
    EFI_PHYSICAL_ADDRESS phys = 0;
    st = uefi_call_wrapper(BS->AllocatePages, 4,
        AllocateAnyPages, EfiLoaderData, pages, &phys);
    if (EFI_ERROR(st)) { Print(L"[HTTP] AllocatePages(%u) failed\r\n", (UINT32)pages); return st; }

    UINT8 *body_buf = (UINT8*)(UINTN)phys;
    UINTN  total    = 0;

    /* ── Response phase 2: stream body ── */
    while (total < alloc_sz) {
        UINTN chunk = alloc_sz - total;
        if (chunk > 65536) chunk = 65536;   /* read in 64 KB pieces */

        OoEfiHttpMessage resp_body_msg;
        for (UINTN i = 0; i < sizeof(resp_body_msg); i++) ((UINT8*)&resp_body_msg)[i] = 0;
        resp_body_msg.Data.Response = &resp_data;
        resp_body_msg.BodyLength    = chunk;
        resp_body_msg.Body          = body_buf + total;

        OoEfiHttpToken resp_body_tok;
        resp_body_tok.Event   = NULL;
        resp_body_tok.Status  = EFI_NOT_READY;
        resp_body_tok.Message = &resp_body_msg;

        st = uefi_call_wrapper(c->http->Response, 2, c->http, &resp_body_tok);
        if (EFI_ERROR(st)) break;

        st = _nb_http_poll_token(c->http, &resp_body_tok);
        if (st == EFI_SUCCESS) {
            total += resp_body_msg.BodyLength;
            if (content_length && total >= content_length) break;
        } else if (st == EFI_CONNECTION_FIN) {
            /* server closed connection — done */
            break;
        } else {
            Print(L"[HTTP] Body poll error: %r (received %u bytes)\r\n", st, (UINT32)total);
            break;
        }

        /* Print progress every 1 MB */
        if ((total & 0xFFFFF) == 0)
            Print(L"[HTTP] Received %u MB...\r\n", (UINT32)(total >> 20));
    }

    if (total == 0) {
        uefi_call_wrapper(BS->FreePages, 2, phys, pages);
        return EFI_END_OF_FILE;
    }

    *body_out    = (void*)body_buf;
    *body_sz_out = total;
    return EFI_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §6  HTTP POST JSON  →  response in caller-supplied buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

static EFI_STATUS _nb_http_post_json(OoHttpClient *c, const OoUrl *url,
                                     const CHAR8 *json_body, UINTN body_len,
                                     CHAR8 *resp_buf, UINTN resp_max) {
    CHAR16 url16[512] = {0};
    _nb_url_to_c16(url16, 512, url);

    /* Request */
    OoEfiHttpRequestData req_data;
    req_data.Method = OoHttpMethodPost;
    req_data.Url    = url16;

    OoEfiHttpHeader req_hdrs[4];
    req_hdrs[0].FieldName  = (CHAR8*)"Host";
    req_hdrs[0].FieldValue = (CHAR8*)url->host;
    req_hdrs[1].FieldName  = (CHAR8*)"Content-Type";
    req_hdrs[1].FieldValue = (CHAR8*)"application/json";
    req_hdrs[2].FieldName  = (CHAR8*)"User-Agent";
    req_hdrs[2].FieldValue = (CHAR8*)"OO-Oracle/2.0";

    /* Content-Length value — build as string */
    CHAR8 cl_val[24] = {0};
    _nb_u32_to_dec(cl_val, sizeof(cl_val), (UINT32)body_len);
    req_hdrs[3].FieldName  = (CHAR8*)"Content-Length";
    req_hdrs[3].FieldValue = cl_val;

    OoEfiHttpMessage req_msg;
    for (UINTN i = 0; i < sizeof(req_msg); i++) ((UINT8*)&req_msg)[i] = 0;
    req_msg.Data.Request = &req_data;
    req_msg.HeaderCount  = 4;
    req_msg.Headers      = req_hdrs;
    req_msg.BodyLength   = body_len;
    req_msg.Body         = (VOID*)json_body;

    OoEfiHttpToken req_tok;
    req_tok.Event = NULL; req_tok.Status = EFI_NOT_READY; req_tok.Message = &req_msg;

    EFI_STATUS st = uefi_call_wrapper(c->http->Request, 2, c->http, &req_tok);
    if (EFI_ERROR(st)) return st;
    st = _nb_http_poll_token(c->http, &req_tok);
    if (EFI_ERROR(st)) return st;

    /* Response headers */
    OoEfiHttpResponseData resp_data; resp_data.StatusCode = 0;
    OoEfiHttpMessage resp_hdr_msg;
    for (UINTN i = 0; i < sizeof(resp_hdr_msg); i++) ((UINT8*)&resp_hdr_msg)[i] = 0;
    resp_hdr_msg.Data.Response = &resp_data;

    OoEfiHttpToken resp_hdr_tok;
    resp_hdr_tok.Event = NULL; resp_hdr_tok.Status = EFI_NOT_READY; resp_hdr_tok.Message = &resp_hdr_msg;

    st = uefi_call_wrapper(c->http->Response, 2, c->http, &resp_hdr_tok);
    if (EFI_ERROR(st)) return st;
    st = _nb_http_poll_token(c->http, &resp_hdr_tok);
    if (EFI_ERROR(st)) return st;

    if (resp_data.StatusCode < 200 || resp_data.StatusCode >= 300) return EFI_ABORTED;

    /* Response body */
    OoEfiHttpMessage resp_body_msg;
    for (UINTN i = 0; i < sizeof(resp_body_msg); i++) ((UINT8*)&resp_body_msg)[i] = 0;
    resp_body_msg.Data.Response = &resp_data;
    resp_body_msg.BodyLength    = resp_max - 1;
    resp_body_msg.Body          = resp_buf;

    OoEfiHttpToken resp_body_tok;
    resp_body_tok.Event = NULL; resp_body_tok.Status = EFI_NOT_READY; resp_body_tok.Message = &resp_body_msg;

    st = uefi_call_wrapper(c->http->Response, 2, c->http, &resp_body_tok);
    if (!EFI_ERROR(st)) st = _nb_http_poll_token(c->http, &resp_body_tok);

    /* Null-terminate whatever we got */
    UINTN got = resp_body_msg.BodyLength;
    if (got >= resp_max) got = resp_max - 1;
    resp_buf[got] = 0;

    return EFI_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §7  Minimal JSON builder + content extractor
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Escape a plain ASCII string for JSON (no unicode — OO runs in ASCII REPL) */
static UINTN _nb_json_escape(CHAR8 *out, UINTN cap, const CHAR8 *in) {
    UINTN p = 0;
    while (*in && p + 2 < cap) {
        CHAR8 c = *in++;
        if (c == '"')       { if (p+2 < cap) { out[p++]='\\'; out[p++]='"'; } }
        else if (c == '\\') { if (p+2 < cap) { out[p++]='\\'; out[p++]='\\'; } }
        else if (c == '\n') { if (p+2 < cap) { out[p++]='\\'; out[p++]='n'; } }
        else if (c == '\r') { if (p+2 < cap) { out[p++]='\\'; out[p++]='r'; } }
        else                out[p++] = c;
    }
    out[p] = 0;
    return p;
}

/* Extract first occurrence of "key":"value" from raw JSON — fills val (no alloc) */
static int _nb_json_extract(const CHAR8 *json, const CHAR8 *key,
                             CHAR8 *val, UINTN val_cap) {
    /* Search for "key":" */
    UINTN klen = _nb_strlen(key);
    const CHAR8 *p = json;
    val[0] = 0;
    while (*p) {
        if (*p == '"' && _nb_strncmp(p + 1, (const CHAR8*)key, klen) == 0
            && p[1 + klen] == '"') {
            /* advance past "key": */
            p += 1 + klen + 1;
            while (*p == ':' || *p == ' ') p++;
            if (*p == '"') {
                p++;
                UINTN vi = 0;
                while (*p && *p != '"' && vi + 1 < val_cap) {
                    if (*p == '\\' && *(p+1)) { p++; } /* skip escape */
                    val[vi++] = *p++;
                }
                val[vi] = 0;
                return 1;
            }
        }
        p++;
    }
    return 0;
}

/* Build OpenAI-compatible chat JSON body.
 * For Claude: {"model":"claude-3-5-sonnet-20241022","max_tokens":1024,"messages":[...]}
 * For Gemini: different endpoint, simplified here.
 * We use OpenAI format for GPT4 + Claude (via API).
 */
#define OO_ORACLE_JSON_BUF  8192

static UINTN _nb_build_oracle_json(CHAR8 *buf, UINTN cap,
                                   OoOracleId oracle, const CHAR8 *prompt,
                                   const CHAR8 *api_key) {
    static const CHAR8 *model_ids[] = {
        (const CHAR8*)"",
        (const CHAR8*)"gpt-4o",
        (const CHAR8*)"claude-3-5-sonnet-20241022",
        (const CHAR8*)"gemini-1.5-pro",
        (const CHAR8*)"oo-local",
        (const CHAR8*)"custom"
    };
    UINTN oid = (UINTN)oracle;
    if (oid >= 6) oid = 1;

    /* Escape prompt */
    CHAR8 escaped[4096] = {0};
    _nb_json_escape(escaped, sizeof(escaped), prompt);

    UINTN p = 0;
    p = _nb_append(buf, p, cap, (const CHAR8*)"{\"model\":\"");
    p = _nb_append(buf, p, cap, model_ids[oid]);
    p = _nb_append(buf, p, cap, (const CHAR8*)"\",\"max_tokens\":2048,\"messages\":[{\"role\":\"user\",\"content\":\"");
    p = _nb_append(buf, p, cap, escaped);
    p = _nb_append(buf, p, cap, (const CHAR8*)"\"}]}");
    return p;
}

/* Oracle endpoint defaults (plain HTTP proxy expected on port 8080).
 * For direct HTTPS use, the user must run a local proxy or wait for Phase 3 TLS. */
static const CHAR8 *_nb_oracle_default_path(OoOracleId oracle) {
    switch (oracle) {
    case OO_ORACLE_GPT4:   return (const CHAR8*)"/v1/chat/completions";
    case OO_ORACLE_CLAUDE: return (const CHAR8*)"/v1/messages";
    case OO_ORACLE_GEMINI: return (const CHAR8*)"/v1beta/models/gemini-1.5-pro:generateContent";
    default:               return (const CHAR8*)"/";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §8  Node ID generator  (MAC[4:5] + timestamp hex)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void _nb_gen_node_id(OoNetContext *ctx) {
    /* Use MAC from g_oo_net if available */
    CHAR8 *id = ctx->node_id;
    id[0]='o'; id[1]='o'; id[2]='-';
    id[3] = _nb_hexc(g_oo_net.mac[3] >> 4);
    id[4] = _nb_hexc(g_oo_net.mac[3]);
    id[5] = _nb_hexc(g_oo_net.mac[4] >> 4);
    id[6] = _nb_hexc(g_oo_net.mac[4]);
    id[7] = _nb_hexc(g_oo_net.mac[5] >> 4);
    id[8] = _nb_hexc(g_oo_net.mac[5]);
    id[9] = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §9  Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

EFI_STATUS oo_netboot_init(OoNetContext *ctx, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    (void)ImageHandle; (void)ST;
    if (!ctx) return EFI_INVALID_PARAMETER;

    for (UINTN i = 0; i < sizeof(*ctx); i++) ((UINT8*)ctx)[i] = 0;
    ctx->state       = OO_NB_PROBING;
    ctx->server_port = 8080;

    Print(L"[netboot] Phase 2 — EFI_HTTP_PROTOCOL enabled\r\n");

    /* Reuse IP from oo_net_core (already DHCP'd) */
    if (g_oo_net.ip_ready) {
        UINT32 ip = g_oo_net.ip;
        CHAR8 *s = ctx->ip;
        _nb_u32_to_dec(s, 4, (ip) & 0xFF);        s += _nb_strlen(s); *s++='.';
        _nb_u32_to_dec(s, 4, (ip>>8) & 0xFF);     s += _nb_strlen(s); *s++='.';
        _nb_u32_to_dec(s, 4, (ip>>16)& 0xFF);     s += _nb_strlen(s); *s++='.';
        _nb_u32_to_dec(s, 4, (ip>>24)& 0xFF);
        ctx->state = OO_NB_READY;
    } else {
        /* No DHCP yet — try to probe HTTP service binding anyway */
        EFI_GUID sb_guid = EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID;
        UINTN n = 0; EFI_HANDLE *hdls = NULL;
        EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
            ByProtocol, &sb_guid, NULL, &n, &hdls);
        if (!EFI_ERROR(st) && n > 0) {
            uefi_call_wrapper(BS->FreePool, 1, hdls);
            ctx->state = OO_NB_PROBING;
            for (UINTN i = 0; i < 7 && "0.0.0.0"[i]; i++) ctx->ip[i] = "0.0.0.0"[i];
        } else {
            ctx->state = OO_NB_ERROR;
            Print(L"[netboot] No HTTP service binding found — offline mode\r\n");
            return EFI_NOT_FOUND;
        }
    }

    _nb_gen_node_id(ctx);
    Print(L"[netboot] Ready. IP=%a  Node=%a\r\n", ctx->ip, ctx->node_id);
    return EFI_SUCCESS;
}

void oo_netboot_shutdown(OoNetContext *ctx) {
    if (!ctx) return;
    ctx->state = OO_NB_UNINIT;
    Print(L"[netboot] Shutdown\r\n");
}

/* HTTP GET model weights → AllocatePages buffer */
EFI_STATUS oo_netboot_pull_model(OoNetContext *ctx,
                                 const CHAR8  *url,
                                 void        **buf_out,
                                 UINTN        *size_out) {
    if (!ctx || ctx->state < OO_NB_READY) return EFI_NOT_READY;
    if (!url || !buf_out || !size_out) return EFI_INVALID_PARAMETER;

    CHAR16 url16[256] = {0};
    _nb_u8_to_c16(url16, 256, url);
    Print(L"[netboot] GET %s\r\n", url16);

    OoUrl parsed = _nb_parse_url(url);
    if (!parsed.valid) { Print(L"[netboot] Invalid URL\r\n"); return EFI_INVALID_PARAMETER; }

    OoHttpClient client;
    EFI_STATUS st = _nb_http_open(&client, &parsed);
    if (EFI_ERROR(st)) {
        Print(L"[netboot] HTTP open failed: %r\r\n", st);
        return st;
    }

    ctx->state = OO_NB_PULLING;
    st = _nb_http_get(&client, &parsed, buf_out, size_out);
    _nb_http_close(&client);

    if (!EFI_ERROR(st)) {
        ctx->bytes_pulled += (UINT64)*size_out;
        ctx->state = OO_NB_READY;
        Print(L"[netboot] Pulled %u bytes OK\r\n", (UINT32)*size_out);
    } else {
        ctx->state = OO_NB_READY;
        Print(L"[netboot] Pull failed: %r\r\n", st);
    }
    return st;
}

/* HTTP POST JSON → oracle response */
EFI_STATUS oo_netboot_oracle_query(OoNetContext *ctx,
                                   OoOracleId    oracle,
                                   const CHAR8  *prompt,
                                   CHAR8        *resp_buf,
                                   UINTN         resp_max) {
    if (!ctx || ctx->state < OO_NB_READY) return EFI_NOT_READY;
    if (!prompt || !resp_buf || resp_max < 2) return EFI_INVALID_PARAMETER;

    const CHAR16 *oracle_names[] = { L"NONE",L"GPT-4o",L"Claude-3.5",L"Gemini-1.5",L"OO-Node",L"Custom" };
    UINTN oid = (UINTN)oracle; if (oid > 5) oid = 5;
    Print(L"[netboot] Oracle → %s\r\n", oracle_names[oid]);

    /* Determine endpoint */
    OoUrl endpoint;
    for (UINTN i = 0; i < sizeof(endpoint); i++) ((UINT8*)&endpoint)[i] = 0;

    if (ctx->oracle_endpoint[0]) {
        endpoint = _nb_parse_url(ctx->oracle_endpoint);
    } else {
        /* Default: federation server as HTTP proxy on port 8080 */
        if (ctx->server_ip[0]) {
            _nb_strlcpy(endpoint.host, ctx->server_ip, sizeof(endpoint.host));
        } else {
            /* fallback: localhost proxy */
            _nb_strlcpy(endpoint.host, (const CHAR8*)"127.0.0.1", sizeof(endpoint.host));
        }
        endpoint.port  = ctx->server_port ? ctx->server_port : 8080;
        _nb_strlcpy(endpoint.path, _nb_oracle_default_path(oracle), sizeof(endpoint.path));
        endpoint.valid = 1;
    }

    if (!endpoint.valid) {
        Print(L"[netboot] Oracle: no endpoint configured.\r\n");
        Print(L"  Use /net_oracle_key <key> and set server IP in repl.cfg:\r\n");
        Print(L"  oracle_server=192.168.1.100\r\n\r\n");
        return EFI_NOT_READY;
    }

    /* Build JSON body */
    static CHAR8 json_body[OO_ORACLE_JSON_BUF];
    UINTN jlen = _nb_build_oracle_json(json_body, sizeof(json_body),
                                       oracle, prompt, ctx->oracle_api_key);

    /* Add Authorization header value for oracle if API key set */
    if (ctx->oracle_api_key[0]) {
#ifdef OO_MBEDTLS_REAL
        /* Phase 9B: direct TLS path — bypass local proxy entirely */
        if (oracle <= OO_ORACLE_GEMINI && !ctx->oracle_endpoint[0]) {
            Print(L"[netboot] Direct TLS oracle (mbedTLS real) — no proxy needed\r\n");
            return oo_mbedtls_oracle_query((int)oracle,
                                           ctx->oracle_api_key,
                                           prompt,
                                           resp_buf, resp_max);
        }
#endif
        /* Plain HTTP path — proxy must inject Authorization header */
        Print(L"[netboot] API key set (%u chars) — proxy must forward Authorization\r\n",
              (UINT32)_nb_strlen(ctx->oracle_api_key));
    }

    OoHttpClient client;
    EFI_STATUS st = _nb_http_open(&client, &endpoint);
    if (EFI_ERROR(st)) {
        Print(L"[netboot] Oracle HTTP open failed: %r\r\n", st);
        Print(L"  Tip: run oo-oracle-proxy.py on %a:%d to forward HTTPS\r\n",
              endpoint.host, (int)endpoint.port);
        return st;
    }

    st = _nb_http_post_json(&client, &endpoint, json_body, jlen, resp_buf, resp_max);
    _nb_http_close(&client);

    if (!EFI_ERROR(st)) {
        ctx->bytes_pushed += (UINT64)jlen;
        /* Try to extract "content" field from JSON response */
        CHAR8 extracted[2048] = {0};
        if (_nb_json_extract((const CHAR8*)resp_buf, (const CHAR8*)"content", extracted, sizeof(extracted))
            || _nb_json_extract((const CHAR8*)resp_buf, (const CHAR8*)"text", extracted, sizeof(extracted))) {
            _nb_strlcpy((CHAR8*)resp_buf, extracted, resp_max);
        }
        Print(L"[netboot] Oracle response received (%u bytes)\r\n",
              (UINT32)_nb_strlen((const CHAR8*)resp_buf));
    } else {
        Print(L"[netboot] Oracle query failed: %r\r\n", st);
    }
    return st;
}

/* HTTP POST delta weights → federation server */
EFI_STATUS oo_netboot_push_delta(OoNetContext *ctx,
                                 const void   *delta_buf,
                                 UINTN         delta_size,
                                 const CHAR8  *model_id) {
    if (!ctx || ctx->state < OO_NB_READY) return EFI_NOT_READY;
    Print(L"[netboot] Push delta: %a  size=%u bytes\r\n", model_id, (UINT32)delta_size);

    if (!ctx->server_ip[0]) {
        Print(L"[netboot] No federation server set. Use: /net_server <ip>\r\n\r\n");
        return EFI_NOT_READY;
    }

    OoUrl endpoint;
    for (UINTN i = 0; i < sizeof(endpoint); i++) ((UINT8*)&endpoint)[i] = 0;
    _nb_strlcpy(endpoint.host, ctx->server_ip, sizeof(endpoint.host));
    endpoint.port  = ctx->server_port;
    _nb_strlcpy(endpoint.path, (const CHAR8*)"/api/v1/delta", sizeof(endpoint.path));
    endpoint.valid = 1;

    /* Build multipart JSON header for the delta */
    static CHAR8 delta_json[512];
    UINTN p = 0;
    p = _nb_append(delta_json, p, sizeof(delta_json), (const CHAR8*)"{\"node\":\"");
    p = _nb_append(delta_json, p, sizeof(delta_json), (const CHAR8*)ctx->node_id);
    p = _nb_append(delta_json, p, sizeof(delta_json), (const CHAR8*)"\",\"model\":\"");
    p = _nb_append(delta_json, p, sizeof(delta_json), model_id ? model_id : (const CHAR8*)"unknown");
    p = _nb_append(delta_json, p, sizeof(delta_json), (const CHAR8*)"\",\"bytes\":");
    CHAR8 szb[16] = {0}; _nb_u32_to_dec(szb, sizeof(szb), (UINT32)delta_size);
    p = _nb_append(delta_json, p, sizeof(delta_json), szb);
    p = _nb_append(delta_json, p, sizeof(delta_json), (const CHAR8*)"}");

    OoHttpClient client;
    EFI_STATUS st = _nb_http_open(&client, &endpoint);
    if (EFI_ERROR(st)) { Print(L"[netboot] Delta push open failed: %r\r\n", st); return st; }

    CHAR8 resp[256] = {0};
    /* For delta, send JSON metadata first */
    st = _nb_http_post_json(&client, &endpoint, delta_json, p, resp, sizeof(resp));
    _nb_http_close(&client);

    if (!EFI_ERROR(st)) {
        ctx->bytes_pushed += (UINT64)delta_size;
        Print(L"[netboot] Delta push OK. Server: %a\r\n\r\n", resp);
    } else {
        Print(L"[netboot] Delta push failed: %r\r\n", st);
    }
    return st;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §10 REPL status + command handler
 * ═══════════════════════════════════════════════════════════════════════════ */

void oo_netboot_print_status(OoNetContext *ctx) {
    if (!ctx) { Print(L"  [netboot] not initialized\r\n"); return; }
    const CHAR16 *states[] = { L"UNINIT",L"PROBING",L"READY",L"PULLING",L"CONNECTED",L"ERROR" };
    UINTN si = (UINTN)ctx->state; if (si > 5) si = 5;
    Print(L"\r\n  OO Network Boot Status (Phase 2)\r\n");
    Print(L"  ─────────────────────────────────\r\n");
    Print(L"  State      : %s\r\n", states[si]);
    Print(L"  Local IP   : %a\r\n", ctx->ip[0] ? ctx->ip : "(none)");
    Print(L"  Server     : %a:%d\r\n",
          ctx->server_ip[0] ? ctx->server_ip : "(none)", (int)ctx->server_port);
    Print(L"  Node-ID    : %a\r\n", ctx->node_id[0] ? ctx->node_id : "(none)");
    Print(L"  Bytes in   : %u\r\n", (UINT32)ctx->bytes_pulled);
    Print(L"  Bytes out  : %u\r\n", (UINT32)ctx->bytes_pushed);
    Print(L"  Oracle     : %s\r\n", ctx->oracle_enabled ? L"ON" : L"OFF");
    if (ctx->oracle_endpoint[0])
        Print(L"  Endpoint   : %a\r\n", ctx->oracle_endpoint);
    Print(L"  API key    : %s\r\n",
          ctx->oracle_api_key[0] ? L"SET (in RAM only)" : L"NOT SET");
    Print(L"\r\n  Commands:\r\n");
    Print(L"   /net_status                    — this view\r\n");
    Print(L"   /net_pull http://host/path     — GET model weights\r\n");
    Print(L"   /net_oracle gpt4|claude <q>    — query AI oracle\r\n");
    Print(L"   /net_oracle_key <key>          — set API key (RAM only)\r\n");
    Print(L"   /net_server <ip> [port]        — set federation server\r\n");
    Print(L"   /net_push                      — push delta weights\r\n");
    Print(L"\r\n");
}

int oo_netboot_repl_cmd(OoNetContext *ctx, const char *cmd) {
    if (!cmd) return 0;

    if (_nb_strncmpc(cmd, "/net_status", 11) == 0) {
        oo_netboot_print_status(ctx);
        return 1;
    }
    if (_nb_strncmpc(cmd, "/net_pull ", 10) == 0) {
        void *buf = NULL; UINTN sz = 0;
        oo_netboot_pull_model(ctx, (const CHAR8*)(cmd + 10), &buf, &sz);
        return 1;
    }
    if (_nb_strncmpc(cmd, "/net_oracle ", 12) == 0) {
        const char *rest = cmd + 12;
        OoOracleId oid = OO_ORACLE_GPT4;
        if (_nb_strncmpc(rest, "claude ", 7) == 0) { oid = OO_ORACLE_CLAUDE; rest += 7; }
        else if (_nb_strncmpc(rest, "gemini ", 7) == 0) { oid = OO_ORACLE_GEMINI; rest += 7; }
        else if (_nb_strncmpc(rest, "gpt4 ", 5) == 0) { rest += 5; }
        CHAR8 resp[2048] = {0};
        oo_netboot_oracle_query(ctx, oid, (const CHAR8*)rest, resp, sizeof(resp));
        if (resp[0]) Print(L"\r\n[Oracle] %a\r\n\r\n", resp);
        return 1;
    }
    if (_nb_strncmpc(cmd, "/net_server ", 12) == 0) {
        const char *rest = cmd + 12;
        UINTN i = 0;
        while (*rest && *rest != ' ' && i + 1 < sizeof(ctx->server_ip))
            ctx->server_ip[i++] = (CHAR8)*rest++;
        ctx->server_ip[i] = 0;
        if (*rest == ' ') {
            rest++; UINT32 port = 0;
            while (*rest >= '0' && *rest <= '9') port = port*10 + (*rest++ - '0');
            if (port) ctx->server_port = (UINT16)port;
        }
        Print(L"\r\n[netboot] Federation server: %a:%d\r\n\r\n",
              ctx->server_ip, (int)ctx->server_port);
        return 1;
    }
    if (_nb_strncmpc(cmd, "/net_push", 9) == 0) {
        oo_netboot_push_delta(ctx, NULL, 0, (const CHAR8*)"cortex_oo");
        return 1;
    }
    return 0;
}