/* oo_mbedtls.c — OO mbedTLS Integration Layer  Phase 4
 * =======================================================
 * EFI_TCP4_PROTOCOL transport glue + mbedTLS stubs.
 * When OO_MBEDTLS_REAL is defined (mbedTLS source present),
 * real TLS symbols replace the stubs.
 *
 * Freestanding C11. No libc. No malloc.
 */
#include "oo_mbedtls.h"
#include "oo_netboot.h"   /* for HTTP fallback (proxy mode) */
#include "oo_tls.h"
#ifdef OO_MBEDTLS_REAL
#include "oo_mbedtls_port.h"
#endif
#include <efi.h>
#include <efilib.h>

/* ── Global state ───────────────────────────────────────────────────────── */
EFI_HANDLE g_tcp4_svc_handle    = NULL;
int        g_mbedtls_initialized = 0;

/* ── EFI TCP4 GUIDs ─────────────────────────────────────────────────────── */
static EFI_GUID _tcp4_svc_guid = {
    0x00720665, 0x67eb, 0x4a99,
    {0xba, 0xf7, 0xd3, 0xc3, 0x3a, 0x1c, 0x7c, 0xc9}
};
static EFI_GUID _tcp4_proto_guid = {
    0x65530bc7, 0xa359, 0x410f,
    {0xb0, 0x10, 0x5a, 0xad, 0xc7, 0xec, 0x2b, 0x62}
};

/* ── EFI TCP4 structures (minimal, for freestanding) ───────────────────── */
typedef struct {
    UINT8  Type;       /* EfiTcp4IoToken type */
    UINT32 FragmentCount;
    struct { void *Buf; UINT32 Len; } Fragments[1];
} _OoTcp4Data;

typedef struct {
    EFI_STATUS  Status;
    EFI_EVENT   Event;
    _OoTcp4Data *Packet;
} _OoTcp4Token;

typedef struct {
    BOOLEAN        UseDefaultAddress;
    EFI_IPv4_ADDRESS StationAddress;
    EFI_IPv4_ADDRESS SubnetMask;
    UINT16         StationPort;
    EFI_IPv4_ADDRESS RemoteAddress;
    UINT16         RemotePort;
    BOOLEAN        ActiveFlag;
} _OoTcp4ConfigData;

typedef struct {
    EFI_STATUS (EFIAPI *GetModeData)(void *This, void *Tcp4State,
                                      _OoTcp4ConfigData *Tcp4ConfigData,
                                      void *Ip4ModeData, void *MnpConfigData,
                                      void *SnpModeData);
    EFI_STATUS (EFIAPI *Configure)(void *This, _OoTcp4ConfigData *Tcp4ConfigData);
    EFI_STATUS (EFIAPI *Routes)(void *This, BOOLEAN DeleteRoute,
                                  EFI_IPv4_ADDRESS *SubnetAddress,
                                  EFI_IPv4_ADDRESS *SubnetMask,
                                  EFI_IPv4_ADDRESS *GatewayAddress);
    EFI_STATUS (EFIAPI *Connect)(void *This, void *ConnectionToken);
    EFI_STATUS (EFIAPI *Accept)(void *This, void *ListenToken);
    EFI_STATUS (EFIAPI *Transmit)(void *This, _OoTcp4Token *Token);
    EFI_STATUS (EFIAPI *Receive)(void *This, _OoTcp4Token *Token);
    EFI_STATUS (EFIAPI *Close)(void *This, void *CloseToken);
    EFI_STATUS (EFIAPI *Cancel)(void *This, void *Token);
    EFI_STATUS (EFIAPI *Poll)(void *This);
} _OoTcp4Proto;

/* ── String helpers ─────────────────────────────────────────────────────── */
static UINTN _mt_strlen(const CHAR8 *s){UINTN n=0;if(!s)return 0;while(s[n])n++;return n;}
static void  _mt_strlcpy(CHAR8*d,const CHAR8*s,UINTN c){UINTN i=0;while(i+1<c&&s[i]){d[i]=s[i];i++;}d[i]=0;}
static void  _mt_memset(void*d,CHAR8 v,UINTN n){for(UINTN i=0;i<n;i++)((CHAR8*)d)[i]=v;}
static void  _mt_memcpy(void*d,const void*s,UINTN n){for(UINTN i=0;i<n;i++)((CHAR8*)d)[i]=((const CHAR8*)s)[i];}
static int   _mt_cstrcmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return (int)(unsigned char)a[i]-(int)(unsigned char)b[i];}return 0;}

/* Parse dotted-decimal → EFI_IPv4_ADDRESS */
static int _mt_parse_ip(const CHAR8 *s, EFI_IPv4_ADDRESS *out) {
    int byte_idx=0; UINT8 val=0; int got_digit=0;
    for (UINTN i=0; s[i] && byte_idx<4; i++) {
        if (s[i]>='0' && s[i]<='9') { val=(UINT8)(val*10+(s[i]-'0')); got_digit=1; }
        else if (s[i]=='.') {
            if (!got_digit) return 0;
            out->Addr[byte_idx++]=val; val=0; got_digit=0;
        } else break;
    }
    if (got_digit && byte_idx==3) { out->Addr[3]=val; return 1; }
    return 0;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
EFI_STATUS oo_mbedtls_init(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    (void)ImageHandle; (void)ST;

    /* Locate TCP4 service binding */
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &_tcp4_svc_guid, NULL, &count, &handles);

    if (!EFI_ERROR(st) && count > 0 && handles) {
        g_tcp4_svc_handle = handles[0];
        uefi_call_wrapper(BS->FreePool, 1, handles);
        Print(L"[mbedtls] EFI_TCP4_SERVICE_BINDING found (%u handle(s))\r\n", (UINT32)count);
    } else {
        Print(L"[mbedtls] EFI_TCP4_SERVICE_BINDING not found (st=%r)\r\n", st);
    }

#ifdef OO_MBEDTLS_REAL
    /* Real mbedTLS init would go here */
    Print(L"[mbedtls] Real mbedTLS compiled in\r\n");
#else
    Print(L"[mbedtls] Stub mode — direct TLS not available yet\r\n");
    Print(L"[mbedtls] Run tools/fetch-mbedtls.sh then rebuild with OO_MBEDTLS_REAL=1\r\n");
#endif

    g_mbedtls_initialized = 1;
    return EFI_SUCCESS;
}

/* ── TCP4 open ───────────────────────────────────────────────────────────── */
static EFI_STATUS _tcp4_open(OoTlsCon *con, EFI_IPv4_ADDRESS *remote_ip, UINT16 port) {
    if (!g_tcp4_svc_handle) return EFI_NOT_FOUND;

    EFI_SERVICE_BINDING *svc = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->HandleProtocol, 3,
        g_tcp4_svc_handle, &_tcp4_svc_guid, (void**)&svc);
    if (EFI_ERROR(st) || !svc) return st;

    EFI_HANDLE child = NULL;
    st = uefi_call_wrapper(svc->CreateChild, 2, svc, &child);
    if (EFI_ERROR(st) || !child) return st;

    _OoTcp4Proto *tcp4 = NULL;
    st = uefi_call_wrapper(BS->HandleProtocol, 3,
        child, &_tcp4_proto_guid, (void**)&tcp4);
    if (EFI_ERROR(st) || !tcp4) { return st; }

    /* Configure active connection */
    _OoTcp4ConfigData cfg = {0};
    cfg.UseDefaultAddress = TRUE;
    cfg.RemoteAddress     = *remote_ip;
    cfg.RemotePort        = port;
    cfg.ActiveFlag        = TRUE;

    st = uefi_call_wrapper(tcp4->Configure, 2, tcp4, &cfg);
    if (EFI_ERROR(st)) {
        Print(L"[mbedtls] TCP4 Configure failed: %r\r\n", st);
        return st;
    }

    con->tcp4       = tcp4;
    con->tcp4_child = child;
    con->state      = OO_TLS_CON_OPEN;

    Print(L"[mbedtls] TCP4 connection configured to %d.%d.%d.%d:%d\r\n",
          remote_ip->Addr[0], remote_ip->Addr[1],
          remote_ip->Addr[2], remote_ip->Addr[3], port);
    return EFI_SUCCESS;
}

/* ── TCP4 send raw ───────────────────────────────────────────────────────── */
static EFI_STATUS _tcp4_send(OoTlsCon *con, const CHAR8 *data, UINTN len) {
    if (!con->tcp4 || len == 0) return EFI_NOT_READY;
    _OoTcp4Proto *tcp4 = (_OoTcp4Proto*)con->tcp4;

    /* Copy to tx_buf */
    UINTN send = len > OO_TCP_TX_BUF ? OO_TCP_TX_BUF : len;
    _mt_memcpy(con->tx_buf, data, send);

    _OoTcp4Data pkt = {0};
    pkt.FragmentCount     = 1;
    pkt.Fragments[0].Buf  = con->tx_buf;
    pkt.Fragments[0].Len  = (UINT32)send;

    _OoTcp4Token tok = {0};
    tok.Status = EFI_NOT_READY;
    tok.Packet = &pkt;

    EFI_STATUS st = uefi_call_wrapper(tcp4->Transmit, 2, tcp4, &tok);
    if (EFI_ERROR(st)) return st;

    /* Poll until done */
    for (UINTN i = 0; i < 100000 && tok.Status == EFI_NOT_READY; i++)
        uefi_call_wrapper(tcp4->Poll, 1, tcp4);

    return tok.Status;
}

/* ── TCP4 recv ───────────────────────────────────────────────────────────── */
static EFI_STATUS _tcp4_recv(OoTlsCon *con, UINTN *out_len) {
    if (!con->tcp4) return EFI_NOT_READY;
    _OoTcp4Proto *tcp4 = (_OoTcp4Proto*)con->tcp4;

    con->rx_len = 0;
    _mt_memset(con->rx_buf, 0, OO_TCP_RX_BUF);

    _OoTcp4Data pkt = {0};
    pkt.FragmentCount     = 1;
    pkt.Fragments[0].Buf  = con->rx_buf;
    pkt.Fragments[0].Len  = OO_TCP_RX_BUF - 1;

    _OoTcp4Token tok = {0};
    tok.Status = EFI_NOT_READY;
    tok.Packet = &pkt;

    EFI_STATUS st = uefi_call_wrapper(tcp4->Receive, 2, tcp4, &tok);
    if (EFI_ERROR(st)) return st;

    for (UINTN i = 0; i < 200000 && tok.Status == EFI_NOT_READY; i++)
        uefi_call_wrapper(tcp4->Poll, 1, tcp4);

    if (!EFI_ERROR(tok.Status)) {
        con->rx_len = pkt.Fragments[0].Len;
        if (out_len) *out_len = con->rx_len;
    }
    return tok.Status;
}

/* ── Connect (TLS or plain TCP) ─────────────────────────────────────────── */
EFI_STATUS oo_mbedtls_connect(OoTlsCon *con,
                               const CHAR8 *host_ip, const CHAR8 *sni_host,
                               UINT16 port, int insecure) {
    if (!con || !host_ip) return EFI_INVALID_PARAMETER;
    _mt_memset(con, 0, sizeof(*con));
    _mt_strlcpy(con->host, sni_host ? sni_host : host_ip, 128);
    con->port     = port ? port : 443;
    con->insecure = insecure;

    EFI_IPv4_ADDRESS remote_ip = {0};
    if (!_mt_parse_ip(host_ip, &remote_ip)) {
        Print(L"[mbedtls] Invalid IP: %a\r\n", host_ip);
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS st = _tcp4_open(con, &remote_ip, con->port);
    if (EFI_ERROR(st)) return st;

#ifdef OO_MBEDTLS_REAL
    /* Real TLS handshake via oo_mbedtls_port.c */
    EFI_STATUS hret = oo_mbedtls_do_handshake(con);
    if (EFI_ERROR(hret)) {
        oo_mbedtls_close(con);
        return hret;
    }
    return EFI_SUCCESS;
#else
    Print(L"[mbedtls] Stub: TCP4 open OK but TLS handshake requires mbedTLS source\r\n");
    Print(L"[mbedtls] Tip: use /tls_proxy mode for now, run tools/fetch-mbedtls.sh\r\n");
    con->state = OO_TLS_CON_OPEN;   /* plain TCP only for now */
    return EFI_SUCCESS;
#endif
}

/* ── HTTPS via proxy fallback ───────────────────────────────────────────── */
/*
 * In stub mode, HTTPS calls route through oo_tls (proxy mode).
 * This ensures the rest of OO works today — oracle queries, model pulls, etc.
 * When real mbedTLS is compiled in, this path is bypassed.
 */
static EFI_STATUS _https_via_proxy(const CHAR8 *host, UINT16 port,
                                    const CHAR8 *path,
                                    const CHAR8 *bearer, const CHAR8 *body,
                                    CHAR8 *resp_buf, UINTN *resp_len) {
    /* Set bearer token on shared TLS ctx if provided */
    if (bearer && bearer[0]) oo_tls_set_token(&g_oo_tls, bearer);

    if (body)
        return oo_tls_https_post_json(&g_oo_tls, host, port, path, body,
                                       resp_buf, resp_len);
    else
        return oo_tls_https_get(&g_oo_tls, host, port, path, resp_buf, resp_len);
}

EFI_STATUS oo_mbedtls_https_get(OoTlsCon *con,
                                  const CHAR8 *sni_host, const CHAR8 *path,
                                  const CHAR8 *bearer_token,
                                  CHAR8 *resp_buf, UINTN *resp_len) {
#ifdef OO_MBEDTLS_REAL
    /* Build raw HTTP/1.1 GET request over TLS */
    static CHAR8 req[1024]; UINTN rp=0;
    const CHAR8 *m=(const CHAR8*)"GET "; _mt_memcpy(req+rp,m,4); rp+=4;
    UINTN pl=_mt_strlen(path); _mt_memcpy(req+rp,path,pl); rp+=pl;
    const CHAR8 *h=(const CHAR8*)" HTTP/1.1\r\nHost: ";
    _mt_memcpy(req+rp,h,17); rp+=17;
    UINTN hl=_mt_strlen(sni_host); _mt_memcpy(req+rp,sni_host,hl); rp+=hl;
    const CHAR8 *te=(const CHAR8*)"\r\nConnection: close\r\n";
    _mt_memcpy(req+rp,te,21); rp+=21;
    if (bearer_token && bearer_token[0]) {
        const CHAR8 *ah=(const CHAR8*)"Authorization: Bearer ";
        _mt_memcpy(req+rp,ah,22); rp+=22;
        UINTN bl=_mt_strlen(bearer_token); _mt_memcpy(req+rp,bearer_token,bl); rp+=bl;
        req[rp++]='\r'; req[rp++]='\n';
    }
    req[rp++]='\r'; req[rp++]='\n'; req[rp]=0;
    _tcp4_send(con, req, rp);
    return _tcp4_recv(con, resp_len);
    /* TODO: decrypt via mbedtls_ssl_read() */
#else
    (void)con;
    return _https_via_proxy(sni_host, 443, path, bearer_token, NULL, resp_buf, resp_len);
#endif
}

EFI_STATUS oo_mbedtls_https_post_json(OoTlsCon *con,
                                        const CHAR8 *sni_host, const CHAR8 *path,
                                        const CHAR8 *bearer_token,
                                        const CHAR8 *json_body,
                                        CHAR8 *resp_buf, UINTN *resp_len) {
#ifdef OO_MBEDTLS_REAL
    /* Build HTTP/1.1 POST request */
    static CHAR8 req[4096]; UINTN rp=0;
    UINTN body_len = _mt_strlen(json_body ? json_body : (const CHAR8*)"");

    /* POST <path> HTTP/1.1\r\nHost: <host>\r\n */
    const CHAR8 *m=(const CHAR8*)"POST "; _mt_memcpy(req+rp,m,5); rp+=5;
    UINTN pl=_mt_strlen(path); _mt_memcpy(req+rp,path,pl); rp+=pl;
    const CHAR8 *h1=(const CHAR8*)" HTTP/1.1\r\nHost: ";
    _mt_memcpy(req+rp,h1,17); rp+=17;
    UINTN hl=_mt_strlen(sni_host); _mt_memcpy(req+rp,sni_host,hl); rp+=hl;
    /* Content-Type */
    const CHAR8 *h2=(const CHAR8*)"\r\nContent-Type: application/json\r\nContent-Length: ";
    _mt_memcpy(req+rp,h2,50); rp+=50;
    /* Content-Length as decimal */
    CHAR8 lenbuf[16]={0}; int li=14; lenbuf[15]=0;
    UINTN tmp=body_len; if(!tmp){lenbuf[li--]='0';}
    else{do{lenbuf[li--]='0'+(CHAR8)(tmp%10);tmp/=10;}while(tmp&&li>=0);}
    UINTN ll=_mt_strlen(lenbuf+li+1);
    _mt_memcpy(req+rp,lenbuf+li+1,ll); rp+=ll;
    /* Authorization Bearer if token present */
    if (bearer_token && bearer_token[0]) {
        const CHAR8 *ah=(const CHAR8*)"\r\nAuthorization: Bearer ";
        _mt_memcpy(req+rp,ah,24); rp+=24;
        UINTN bl=_mt_strlen(bearer_token); _mt_memcpy(req+rp,bearer_token,bl); rp+=bl;
    }
    /* Accept + Connection close + end of headers */
    const CHAR8 *te=(const CHAR8*)"\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
    _mt_memcpy(req+rp,te,49); rp+=49;
    /* Body */
    if (json_body && body_len > 0 && rp + body_len < sizeof(req)-1) {
        _mt_memcpy(req+rp,json_body,body_len); rp+=body_len;
    }
    req[rp]=0;

    /* Send via TLS (mbedtls_ssl_write through oo_mbedtls_tls_write) */
    int wret = oo_mbedtls_tls_write(con, (const unsigned char*)req, (uint32_t)rp);
    if (wret <= 0) {
        Print(L"[mbedtls] TLS write failed: %d\r\n", wret);
        return EFI_ABORTED;
    }

    /* Receive response via TLS */
    static CHAR8 raw_resp[32768]; UINTN raw_pos = 0;
    int rret;
    do {
        rret = oo_mbedtls_tls_read(con,
                (unsigned char*)raw_resp + raw_pos,
                (uint32_t)(sizeof(raw_resp)-1-raw_pos));
        if (rret > 0) raw_pos += (UINTN)rret;
    } while (rret > 0 && raw_pos < sizeof(raw_resp)-1);
    raw_resp[raw_pos] = 0;

    /* Skip HTTP headers — find \r\n\r\n */
    CHAR8 *body_start = NULL;
    for (UINTN i = 0; i + 3 < raw_pos; i++) {
        if (raw_resp[i]=='\r' && raw_resp[i+1]=='\n' &&
            raw_resp[i+2]=='\r' && raw_resp[i+3]=='\n') {
            body_start = raw_resp + i + 4;
            break;
        }
    }
    if (!body_start) {
        /* No headers separator — return raw (might be an error response) */
        body_start = raw_resp;
    }

    /* Copy body into caller buffer */
    UINTN blen = (UINTN)(raw_resp + raw_pos - body_start);
    if (resp_buf && resp_len) {
        UINTN copy = blen < (*resp_len - 1) ? blen : (*resp_len - 1);
        _mt_memcpy(resp_buf, body_start, copy);
        resp_buf[copy] = 0;
        *resp_len = copy;
    }

    return (raw_pos > 0) ? EFI_SUCCESS : EFI_ABORTED;
#else
    (void)con;
    return _https_via_proxy(sni_host, 443, path, bearer_token, json_body, resp_buf, resp_len);
#endif
}

/* ── Phase 9B: All-in-one oracle query via direct TLS ───────────────────── */
/*
 * Maps OoOracleId → HTTPS host + path + model name.
 * Resolves hostname via oo_dns, opens TLS, POSTs JSON, extracts content.
 *
 * oracle_host_out / oracle_path_out: optional, filled if non-NULL.
 * Returns EFI_SUCCESS + response in resp_buf on success.
 */
static void _mbedtls_oracle_host_path(int oracle_id,
                                       const CHAR8 **host_out,
                                       const CHAR8 **path_out,
                                       const CHAR8 **model_out) {
    switch (oracle_id) {
    case 1: /* OO_ORACLE_GPT4 */
        *host_out  = (const CHAR8*)"api.openai.com";
        *path_out  = (const CHAR8*)"/v1/chat/completions";
        *model_out = (const CHAR8*)"gpt-4o";
        break;
    case 2: /* OO_ORACLE_CLAUDE */
        *host_out  = (const CHAR8*)"api.anthropic.com";
        *path_out  = (const CHAR8*)"/v1/messages";
        *model_out = (const CHAR8*)"claude-3-5-sonnet-20241022";
        break;
    case 3: /* OO_ORACLE_GEMINI */
        *host_out  = (const CHAR8*)"generativelanguage.googleapis.com";
        *path_out  = (const CHAR8*)"/v1beta/models/gemini-1.5-pro:generateContent";
        *model_out = (const CHAR8*)"gemini-1.5-pro";
        break;
    default: /* custom / none */
        *host_out  = NULL;
        *path_out  = (const CHAR8*)"/";
        *model_out = (const CHAR8*)"unknown";
        break;
    }
}

/* Minimal JSON escaping for the prompt (only \n, \r, \t, \", \\) */
static UINTN _mbedtls_json_escape(const CHAR8 *in, CHAR8 *out, UINTN out_cap) {
    UINTN i=0, o=0;
    while (in[i] && o+4 < out_cap) {
        CHAR8 c = in[i++];
        if      (c=='"')  { out[o++]='\\'; out[o++]='"';  }
        else if (c=='\\') { out[o++]='\\'; out[o++]='\\'; }
        else if (c=='\n') { out[o++]='\\'; out[o++]='n';  }
        else if (c=='\r') { out[o++]='\\'; out[o++]='r';  }
        else if (c=='\t') { out[o++]='\\'; out[o++]='t';  }
        else              { out[o++]=c; }
    }
    out[o]=0;
    return o;
}

/* Find first occurrence of key in JSON and copy value string */
static int _mbedtls_json_extract(const CHAR8 *json, const CHAR8 *key,
                                  CHAR8 *out, UINTN out_cap) {
    if (!json || !key || !out) return 0;
    /* Search for "key": " */
    UINTN kl = _mt_strlen(key);
    for (UINTN i = 0; json[i]; i++) {
        if (json[i]=='"') {
            /* check if key matches */
            int match=1;
            for (UINTN k=0; k<kl; k++) {
                if (!json[i+1+k] || json[i+1+k]!=key[k]) { match=0; break; }
            }
            if (match && json[i+1+kl]=='"') {
                UINTN j=i+1+kl+1;
                while (json[j]==' '||json[j]=='\t') j++;
                if (json[j]==':') { j++;
                    while (json[j]==' '||json[j]=='\t') j++;
                    if (json[j]=='"') {
                        j++;
                        UINTN o=0;
                        while (json[j] && json[j]!='"' && o+1<out_cap) {
                            if (json[j]=='\\' && json[j+1]) {
                                char c=json[++j];
                                if(c=='n') out[o++]='\n';
                                else if(c=='t') out[o++]='\t';
                                else out[o++]=(CHAR8)c;
                            } else out[o++]=json[j];
                            j++;
                        }
                        out[o]=0;
                        return (int)o;
                    }
                }
            }
        }
    }
    return 0;
}

EFI_STATUS oo_mbedtls_oracle_query(int oracle_id,
                                    const CHAR8 *api_key,
                                    const CHAR8 *prompt,
                                    CHAR8 *resp_buf, UINTN resp_max) {
#ifdef OO_MBEDTLS_REAL
    if (!api_key || !api_key[0]) {
        Print(L"[tls] No API key — use /net_oracle_key <key> first\r\n");
        return EFI_NOT_READY;
    }
    if (!prompt || !resp_buf || resp_max < 2) return EFI_INVALID_PARAMETER;
    if (!g_mbedtls_initialized) {
        Print(L"[tls] mbedTLS not init — call oo_mbedtls_init() first\r\n");
        return EFI_NOT_READY;
    }

    const CHAR8 *host = NULL, *path = NULL, *model = NULL;
    _mbedtls_oracle_host_path(oracle_id, &host, &path, &model);
    if (!host) {
        Print(L"[tls] Unknown oracle id %d\r\n", oracle_id);
        return EFI_INVALID_PARAMETER;
    }

    Print(L"[tls] Oracle %a → %a\r\n", model, host);

    /* DNS resolve hostname → IP */
    extern OoDnsCtx g_oo_dns;
    CHAR8 host_ip[16] = {0};
    EFI_STATUS dns_st = oo_dns_resolve(&g_oo_dns, host, host_ip, sizeof(host_ip));
    if (EFI_ERROR(dns_st) || !host_ip[0]) {
        Print(L"[tls] DNS resolve failed for %a (%r)\r\n", host, dns_st);
        Print(L"[tls] Use /dns_add %a <ip> to set manually\r\n", host);
        return EFI_NOT_FOUND;
    }
    Print(L"[tls] DNS: %a → %a\r\n", host, host_ip);

    /* TLS connect */
    static OoTlsCon _oracle_con;
    EFI_STATUS st = oo_mbedtls_connect(&_oracle_con, host_ip, host, 443, 1 /*insecure*/);
    if (EFI_ERROR(st)) {
        Print(L"[tls] Connect failed: %r\r\n", st);
        return st;
    }

    /* Build JSON body */
    static CHAR8 escaped[4096]; UINTN esc_len;
    esc_len = _mbedtls_json_escape(prompt, escaped, sizeof(escaped));
    (void)esc_len;

    static CHAR8 json_body[6144];
    UINTN jlen;
    if (oracle_id == 2) { /* Claude: different format */
        jlen = 0;
        const CHAR8 *p1 = (const CHAR8*)"{\"model\":\"";
        while (*p1) json_body[jlen++]=*p1++;
        for (UINTN k=0; model[k]; k++) json_body[jlen++]=model[k];
        const CHAR8 *p2 = (const CHAR8*)"\",\"max_tokens\":2048,\"messages\":[{\"role\":\"user\",\"content\":\"";
        while (*p2) json_body[jlen++]=*p2++;
        for (UINTN k=0; escaped[k] && jlen < sizeof(json_body)-20; k++) json_body[jlen++]=escaped[k];
        const CHAR8 *p3 = (const CHAR8*)"\"}]}";
        while (*p3) json_body[jlen++]=*p3++;
        json_body[jlen]=0;
    } else {
        jlen = 0;
        const CHAR8 *p1 = (const CHAR8*)"{\"model\":\"";
        while (*p1) json_body[jlen++]=*p1++;
        for (UINTN k=0; model[k]; k++) json_body[jlen++]=model[k];
        const CHAR8 *p2 = (const CHAR8*)"\",\"max_tokens\":2048,\"messages\":[{\"role\":\"user\",\"content\":\"";
        while (*p2) json_body[jlen++]=*p2++;
        for (UINTN k=0; escaped[k] && jlen < sizeof(json_body)-20; k++) json_body[jlen++]=escaped[k];
        const CHAR8 *p3 = (const CHAR8*)"\"}]}";
        while (*p3) json_body[jlen++]=*p3++;
        json_body[jlen]=0;
    }

    /* POST + receive */
    UINTN rlen = resp_max - 1;
    st = oo_mbedtls_https_post_json(&_oracle_con, host, path,
                                     api_key, json_body, resp_buf, &rlen);
    oo_mbedtls_close(&_oracle_con);

    if (!EFI_ERROR(st) && rlen > 0) {
        /* Try to extract "content" then "text" from JSON response */
        static CHAR8 extracted[4096];
        if (_mbedtls_json_extract(resp_buf, (const CHAR8*)"content", extracted, sizeof(extracted))
         || _mbedtls_json_extract(resp_buf, (const CHAR8*)"text",    extracted, sizeof(extracted))) {
            UINTN elen = _mt_strlen(extracted);
            if (elen >= resp_max) elen = resp_max-1;
            _mt_memcpy(resp_buf, extracted, elen);
            resp_buf[elen] = 0;
        }
        Print(L"[tls] Oracle response: %u bytes\r\n", (UINT32)_mt_strlen(resp_buf));
    } else {
        Print(L"[tls] Oracle POST failed: %r\r\n", st);
    }
    return st;
#else
    /* Stub mode: fall through to netboot proxy */
    (void)oracle_id; (void)api_key; (void)prompt;
    if (resp_buf && resp_max > 0) { resp_buf[0]='['; resp_buf[1]=0; }
    Print(L"[tls] Stub mode — use /net_oracle instead (proxy)\r\n");
    return EFI_UNSUPPORTED;
#endif
}

/* ── Close ───────────────────────────────────────────────────────────────── */
void oo_mbedtls_close(OoTlsCon *con) {
    if (!con || con->state == OO_TLS_CON_CLOSED) return;
    if (g_tcp4_svc_handle && con->tcp4_child) {
        EFI_SERVICE_BINDING *svc = NULL;
        if (!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol, 3,
                g_tcp4_svc_handle, &_tcp4_svc_guid, (void**)&svc)) && svc)
            uefi_call_wrapper(svc->DestroyChild, 2, svc, con->tcp4_child);
    }
    con->state      = OO_TLS_CON_CLOSED;
    con->tcp4       = NULL;
    con->tcp4_child = NULL;
}

/* ── Status + REPL ───────────────────────────────────────────────────────── */
int oo_mbedtls_is_real(void) {
#ifdef OO_MBEDTLS_REAL
    return 1;
#else
    return 0;
#endif
}

void oo_mbedtls_print_status(void) {
    Print(L"\r\n  [OO mbedTLS Status]\r\n");
    Print(L"  Initialized : %s\r\n", g_mbedtls_initialized ? L"yes" : L"no");
    Print(L"  TCP4 SvcBnd : %s\r\n", g_tcp4_svc_handle ? L"FOUND" : L"NOT FOUND");
    Print(L"  Real TLS    : %s\r\n", oo_mbedtls_is_real() ?
          L"YES (mbedTLS compiled in)" : L"NO (stub — proxy fallback)");
#ifdef OO_MBEDTLS_REAL
    UINT32 used = 0, total = 0;
    oo_mbedtls_pool_stats(&used, &total);
    Print(L"  Heap pool   : %u / %u bytes used\r\n", used, total);
#else
    Print(L"  Next step   : run tools/fetch-mbedtls.sh + rebuild with OO_MBEDTLS_REAL=1\r\n");
#endif
    Print(L"\r\n");
}

int oo_mbedtls_repl_cmd(const char *cmd) {
    if (!cmd) return 0;

    if (_mt_cstrcmp(cmd, "/mbedtls_status", 15) == 0) {
        oo_mbedtls_print_status(); return 1;
    }
    /* /mbedtls_connect <ip> <host> [port] */
    if (_mt_cstrcmp(cmd, "/mbedtls_connect ", 17) == 0) {
        const char *r = cmd + 17;
        while (*r==' ') r++;
        CHAR8 ip[64]={0}; int ii=0;
        while(*r&&*r!=' '&&ii<63) ip[ii++]=(CHAR8)*r++;
        ip[ii]=0; while(*r==' ')r++;
        CHAR8 host[128]={0}; int hi=0;
        while(*r&&*r!=' '&&hi<127) host[hi++]=(CHAR8)*r++;
        host[hi]=0; while(*r==' ')r++;
        UINT16 port=443;
        if(*r>='0'&&*r<='9'){UINT32 v=0;while(*r>='0'&&*r<='9'){v=v*10+(*r-'0');r++;}port=(UINT16)v;}
        static OoTlsCon test_con;
        EFI_STATUS st = oo_mbedtls_connect(&test_con, ip, host, port, 1);
        Print(L"[mbedtls] Connect: %r\r\n", st);
        if (!EFI_ERROR(st)) oo_mbedtls_close(&test_con);
        return 1;
    }
    /* /mbedtls_fetch <ip> <host> <path> */
    if (_mt_cstrcmp(cmd, "/mbedtls_fetch ", 15) == 0) {
        const char *r = cmd + 15;
        CHAR8 ip[64]={0};   int ii=0; while(*r&&*r!=' '&&ii<63) ip[ii++]=(CHAR8)*r++; ip[ii]=0; while(*r==' ')r++;
        CHAR8 host[128]={0};int hi=0; while(*r&&*r!=' '&&hi<127)host[hi++]=(CHAR8)*r++;host[hi]=0;while(*r==' ')r++;
        static CHAR8 resp[4096]; UINTN rlen=sizeof(resp)-1;
        EFI_STATUS st = oo_mbedtls_https_get(NULL, host, (const CHAR8*)r, NULL, resp, &rlen);
        if (!EFI_ERROR(st)) {
            Print(L"[mbedtls] Response (%u bytes):\r\n",(UINT32)rlen);
            UINTN show=rlen>512?512:rlen;
            for(UINTN i=0;i<show;i++) Print(L"%c",(CHAR16)resp[i]);
            Print(L"\r\n");
        } else Print(L"[mbedtls] Fetch failed: %r\r\n", st);
        return 1;
    }
    return 0;
}
