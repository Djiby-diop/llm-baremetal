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
    /* Build POST request */
    static CHAR8 req[2048]; UINTN rp=0;
    UINTN body_len = _mt_strlen(json_body);
    const CHAR8 *m=(const CHAR8*)"POST "; _mt_memcpy(req+rp,m,5); rp+=5;
    UINTN pl=_mt_strlen(path); _mt_memcpy(req+rp,path,pl); rp+=pl;
    const CHAR8 *h1=(const CHAR8*)" HTTP/1.1\r\nHost: ";
    _mt_memcpy(req+rp,h1,17); rp+=17;
    UINTN hl=_mt_strlen(sni_host); _mt_memcpy(req+rp,sni_host,hl); rp+=hl;
    const CHAR8 *h2=(const CHAR8*)"\r\nContent-Type: application/json\r\nContent-Length: ";
    _mt_memcpy(req+rp,h2,50); rp+=50;
    /* body_len as decimal */
    CHAR8 lenbuf[12]; int li=10; lenbuf[11]=0;
    UINTN tmp=body_len; do{lenbuf[li--]='0'+(tmp%10);tmp/=10;}while(tmp&&li>=0);
    UINTN ll=_mt_strlen(lenbuf+li+1);
    _mt_memcpy(req+rp,lenbuf+li+1,ll); rp+=ll;
    if (bearer_token && bearer_token[0]) {
        const CHAR8 *ah=(const CHAR8*)"\r\nAuthorization: Bearer ";
        _mt_memcpy(req+rp,ah,24); rp+=24;
        UINTN bl=_mt_strlen(bearer_token); _mt_memcpy(req+rp,bearer_token,bl); rp+=bl;
    }
    const CHAR8 *te=(const CHAR8*)"\r\nConnection: close\r\n\r\n";
    _mt_memcpy(req+rp,te,23); rp+=23;
    if (rp + body_len < sizeof(req)-1) {
        _mt_memcpy(req+rp,json_body,body_len); rp+=body_len;
    }
    req[rp]=0;
    _tcp4_send(con, req, rp);
    return _tcp4_recv(con, resp_len);
#else
    (void)con;
    return _https_via_proxy(sni_host, 443, path, bearer_token, json_body, resp_buf, resp_len);
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
