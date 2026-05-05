/* oo_dns.c — OO DNS4 Resolver  Phase 3
 * =========================================
 * EFI_DNS4_PROTOCOL wrapper + static cache.
 * Falls back gracefully when DNS4 is unavailable (most OVMF builds).
 * Freestanding C11. No libc. No dynamic allocation.
 */
#include "oo_dns.h"
#include <efi.h>
#include <efilib.h>

/* Global singleton */
OoDnsCtx g_oo_dns;

/* ── EFI DNS4 GUIDs ─────────────────────────────────────────────────────── */
static EFI_GUID _dns4_svc_guid = {
    0xb625b186, 0xe063, 0x44f7,
    {0x89, 0x05, 0x6a, 0x74, 0xdc, 0x6f, 0x52, 0xb4}
};
static EFI_GUID _dns4_proto_guid = {
    0xae3d28cc, 0xe05b, 0x4fa1,
    {0xa0, 0x11, 0x7e, 0xb5, 0x5a, 0x3f, 0x38, 0x4a}
};

/* ── String helpers ─────────────────────────────────────────────────────── */
static UINTN _dns_strlen(const CHAR8 *s) {
    UINTN n=0; if(!s)return 0; while(s[n])n++; return n;
}
static void _dns_strlcpy(CHAR8 *d, const CHAR8 *s, UINTN cap) {
    UINTN i=0; while(i+1<cap&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static void _dns_memset(void *d, CHAR8 v, UINTN n) {
    for(UINTN i=0;i<n;i++)((CHAR8*)d)[i]=v;
}
static int _dns_strncmp(const CHAR8 *a, const CHAR8 *b, UINTN n) {
    for(UINTN i=0;i<n;i++){
        if(!a[i]&&!b[i])return 0;
        if(a[i]!=b[i])return (int)(UINT8)a[i]-(int)(UINT8)b[i];
    }
    return 0;
}
static int _dns_cstrcmp(const char *a, const char *b, int n) {
    for(int i=0;i<n;i++){
        if(!a[i]&&!b[i])return 0;
        if(a[i]!=b[i])return (int)(unsigned char)a[i]-(int)(unsigned char)b[i];
    }
    return 0;
}

/* Build dotted-decimal from 4 bytes */
static void _ipv4_to_str(UINT8 a, UINT8 b, UINT8 c, UINT8 d,
                          CHAR8 *out, UINTN cap) {
    CHAR8 tmp[4];
    UINTN p=0;
    UINT8 bytes[4]={a,b,c,d};
    for(int i=0;i<4;i++){
        UINT8 v=bytes[i]; int k=0;
        if(v>=100){tmp[k++]='0'+v/100; v%=100;}
        if(v>=10 ||k>0){tmp[k++]='0'+v/10; v%=10;}
        tmp[k++]='0'+v;
        for(int j=0;j<k&&p+1<cap;j++) out[p++]=tmp[j];
        if(i<3&&p+1<cap) out[p++]='.';
    }
    if(p<cap) out[p]=0;
}

/* ── EFI DNS4 token + structs (not in all gnu-efi versions) ──────────────── */
/* We define minimally what we need */
#ifndef EFI_DNS4_PROTOCOL_GUID
typedef struct {
    UINT32 RspCode;
    UINT32 ReturnCode;
    UINT32 IpCount;
    EFI_IPv4_ADDRESS *IpList;
} _OoDnsHostInfo;

typedef struct {
    UINT16          *QName;     /* hostname as CHAR16 */
    UINT16           QType;     /* 1 = A record */
    UINT16           QClass;    /* 1 = IN */
    UINT32           Token;
    EFI_STATUS       Status;
    _OoDnsHostInfo  *RspData;
} _OoDns4CompletionToken;

typedef struct {
    EFI_STATUS (EFIAPI *GetModeData)(void *This, void *DnsModeData);
    EFI_STATUS (EFIAPI *Configure)(void *This, void *DnsConfigData);
    EFI_STATUS (EFIAPI *HostNameToIp)(void *This, CHAR16 *HostName,
                                       _OoDns4CompletionToken *Token);
    EFI_STATUS (EFIAPI *IpToHostName)(void *This, EFI_IPv4_ADDRESS *IpAddress,
                                       _OoDns4CompletionToken *Token);
    EFI_STATUS (EFIAPI *GeneralLookUp)(void *This, CHAR8 *QName, UINT16 QType,
                                        UINT16 QClass, _OoDns4CompletionToken *Token);
    EFI_STATUS (EFIAPI *UpdateDnsCache)(void *This, BOOLEAN DeleteFlag,
                                         BOOLEAN Override, void *DnsCacheEntry);
    EFI_STATUS (EFIAPI *Poll)(void *This);
    EFI_STATUS (EFIAPI *Cancel)(void *This, _OoDns4CompletionToken *Token);
} _OoDns4Proto;
#endif

/* ── Cache operations ───────────────────────────────────────────────────── */
static OoDnsCacheEntry *_dns_cache_find(OoDnsCtx *ctx, const CHAR8 *name) {
    for (int i = 0; i < ctx->cache_count; i++) {
        OoDnsCacheEntry *e = &ctx->cache[i];
        if (e->ttl_ticks > 0 &&
            _dns_strncmp(e->name, name, OO_DNS_NAME_MAX) == 0)
            return e;
    }
    return NULL;
}

static void _dns_cache_put(OoDnsCtx *ctx, const CHAR8 *name, const CHAR8 *ip) {
    /* Find existing or LRU slot (overwrite oldest = index 0 shifted down) */
    for (int i = 0; i < ctx->cache_count; i++) {
        if (_dns_strncmp(ctx->cache[i].name, name, OO_DNS_NAME_MAX) == 0) {
            _dns_strlcpy(ctx->cache[i].ip, ip, OO_DNS_IP_LEN);
            ctx->cache[i].ttl_ticks = 300;
            return;
        }
    }
    int slot = ctx->cache_count < OO_DNS_CACHE_MAX ?
               ctx->cache_count++ : OO_DNS_CACHE_MAX - 1;
    _dns_strlcpy(ctx->cache[slot].name, name, OO_DNS_NAME_MAX);
    _dns_strlcpy(ctx->cache[slot].ip,   ip,   OO_DNS_IP_LEN);
    ctx->cache[slot].ttl_ticks = 300;
}

/* ── Known-host fallback table ──────────────────────────────────────────── */
/* Pre-seeded IPs for oracle endpoints when DNS unavailable.
 * These are informational — production would resolve dynamically.
 * The user must set the actual proxy IP via /net_server or /tls_proxy. */
typedef struct { const CHAR8 *name; const CHAR8 *ip; } _OoDnsSeed;
static const _OoDnsSeed g_dns_seeds[] = {
    { (const CHAR8*)"localhost",        (const CHAR8*)"127.0.0.1"  },
    { (const CHAR8*)"api.openai.com",   (const CHAR8*)"104.18.7.192" },  /* CDN - may change */
    { (const CHAR8*)"api.anthropic.com",(const CHAR8*)"104.18.33.45"},   /* CDN - may change */
    { NULL, NULL }
};

/* ── Init ────────────────────────────────────────────────────────────────── */
EFI_STATUS oo_dns_init(OoDnsCtx *ctx, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    if (!ctx) return EFI_INVALID_PARAMETER;
    _dns_memset(ctx, 0, sizeof(*ctx));

    /* Seed known hosts */
    for (int i = 0; g_dns_seeds[i].name; i++)
        _dns_cache_put(ctx, g_dns_seeds[i].name, g_dns_seeds[i].ip);

    /* Try to locate EFI_DNS4_SERVICE_BINDING_PROTOCOL */
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &_dns4_svc_guid, NULL, &count, &handles);

    if (!EFI_ERROR(st) && count > 0 && handles) {
        /* Create child */
        EFI_SERVICE_BINDING *svc = NULL;
        st = uefi_call_wrapper(BS->HandleProtocol, 3,
            handles[0], &_dns4_svc_guid, (void**)&svc);
        if (!EFI_ERROR(st) && svc) {
            EFI_HANDLE child = NULL;
            st = uefi_call_wrapper(svc->CreateChild, 2, svc, &child);
            if (!EFI_ERROR(st) && child) {
                void *proto = NULL;
                st = uefi_call_wrapper(BS->HandleProtocol, 3,
                    child, &_dns4_proto_guid, &proto);
                if (!EFI_ERROR(st)) {
                    ctx->dns4_protocol = proto;
                    Print(L"[dns] EFI_DNS4_PROTOCOL found\r\n");
                }
            }
        }
        uefi_call_wrapper(BS->FreePool, 1, handles);
    }

    if (!ctx->dns4_protocol)
        Print(L"[dns] EFI_DNS4 not available — using cache + manual entries\r\n");

    ctx->initialized = 1;
    return EFI_SUCCESS;
}

/* ── EFI DNS4 resolve ────────────────────────────────────────────────────── */
static EFI_STATUS _dns4_resolve(OoDnsCtx *ctx, const CHAR8 *hostname,
                                 CHAR8 *ip_out, UINTN ip_cap) {
    if (!ctx->dns4_protocol) return EFI_UNSUPPORTED;

    _OoDns4Proto *dns4 = (_OoDns4Proto*)ctx->dns4_protocol;

    /* Convert hostname to CHAR16 */
    CHAR16 name16[OO_DNS_NAME_MAX];
    UINTN hl = _dns_strlen(hostname);
    if (hl >= OO_DNS_NAME_MAX) hl = OO_DNS_NAME_MAX - 1;
    for (UINTN i = 0; i < hl; i++) name16[i] = (CHAR16)hostname[i];
    name16[hl] = 0;

    _OoDnsHostInfo  host_info = {0};
    _OoDns4CompletionToken token = {0};
    token.QName   = name16;
    token.QType   = 1;   /* A */
    token.QClass  = 1;   /* IN */
    token.RspData = &host_info;

    EFI_STATUS st = uefi_call_wrapper(dns4->HostNameToIp, 3, dns4, name16, &token);
    if (EFI_ERROR(st)) return st;

    /* Poll (synchronous) */
    UINTN tries = 0;
    while (token.Status == EFI_NOT_READY && tries < 200000) {
        uefi_call_wrapper(dns4->Poll, 1, dns4);
        tries++;
    }

    if (EFI_ERROR(token.Status)) return token.Status;
    if (!host_info.IpList || host_info.IpCount == 0) return EFI_NOT_FOUND;

    /* Take first IP */
    EFI_IPv4_ADDRESS *ip = &host_info.IpList[0];
    _ipv4_to_str(ip->Addr[0], ip->Addr[1], ip->Addr[2], ip->Addr[3],
                 ip_out, ip_cap);
    return EFI_SUCCESS;
}

/* ── Public resolve ──────────────────────────────────────────────────────── */
EFI_STATUS oo_dns_resolve(OoDnsCtx *ctx, const CHAR8 *hostname,
                           CHAR8 *ip_out, UINTN ip_cap) {
    if (!ctx || !hostname || !ip_out) return EFI_INVALID_PARAMETER;

    /* Cache lookup */
    OoDnsCacheEntry *e = _dns_cache_find(ctx, hostname);
    if (e) {
        _dns_strlcpy(ip_out, e->ip, ip_cap);
        ctx->cache_hits++;
        Print(L"[dns] Cache hit: %a → %a\r\n", hostname, ip_out);
        return EFI_SUCCESS;
    }

    /* EFI DNS4 */
    EFI_STATUS st = _dns4_resolve(ctx, hostname, ip_out, ip_cap);
    if (!EFI_ERROR(st)) {
        _dns_cache_put(ctx, hostname, ip_out);
        ctx->resolve_ok++;
        Print(L"[dns] Resolved: %a → %a\r\n", hostname, ip_out);
        return EFI_SUCCESS;
    }

    ctx->resolve_fail++;
    Print(L"[dns] Resolution failed for %a: %r\r\n", hostname, st);
    Print(L"[dns] Use /dns_add %a <ip> to set manually\r\n", hostname);
    return EFI_NOT_FOUND;
}

/* ── Manual add ─────────────────────────────────────────────────────────── */
void oo_dns_add(OoDnsCtx *ctx, const CHAR8 *name, const CHAR8 *ip) {
    if (!ctx || !name || !ip) return;
    _dns_cache_put(ctx, name, ip);
    Print(L"[dns] Added: %a → %a\r\n", name, ip);
}

/* ── Print cache ─────────────────────────────────────────────────────────── */
void oo_dns_print_cache(const OoDnsCtx *ctx) {
    if (!ctx) return;
    Print(L"\r\n  [OO DNS Cache (%d entries)]\r\n", ctx->cache_count);
    Print(L"  %-40s %s\r\n", L"HOSTNAME", L"IP");
    Print(L"  \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n");
    for (int i = 0; i < ctx->cache_count; i++) {
        const OoDnsCacheEntry *e = &ctx->cache[i];
        if (e->ttl_ticks == 0) continue;
        CHAR16 n16[OO_DNS_NAME_MAX]={0};
        for(int j=0;j<OO_DNS_NAME_MAX-1&&e->name[j];j++) n16[j]=(CHAR16)e->name[j];
        CHAR16 i16[OO_DNS_IP_LEN]={0};
        for(int j=0;j<OO_DNS_IP_LEN-1&&e->ip[j];j++) i16[j]=(CHAR16)e->ip[j];
        Print(L"  %-40s %s\r\n", n16, i16);
    }
    Print(L"  Hits: %d  OK: %d  Fail: %d\r\n",
          ctx->cache_hits, ctx->resolve_ok, ctx->resolve_fail);
    Print(L"\r\n");
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
int oo_dns_repl_cmd(OoDnsCtx *ctx, const char *cmd) {
    if (!cmd) return 0;

    if (_dns_cstrcmp(cmd, "/dns_status", 11) == 0) {
        Print(L"[dns] %s | EFI_DNS4: %s | cache: %d entries\r\n",
              ctx->initialized ? L"ready" : L"uninit",
              ctx->dns4_protocol ? L"YES" : L"NO (manual only)",
              ctx->cache_count);
        return 1;
    }
    if (_dns_cstrcmp(cmd, "/dns_cache", 10) == 0) {
        oo_dns_print_cache(ctx); return 1;
    }
    if (_dns_cstrcmp(cmd, "/dns_resolve ", 13) == 0) {
        const char *host = cmd + 13;
        while (*host == ' ') host++;
        CHAR8 ip[OO_DNS_IP_LEN]; ip[0]=0;
        EFI_STATUS st = oo_dns_resolve(ctx, (const CHAR8*)host, ip, sizeof(ip));
        if (!EFI_ERROR(st))
            Print(L"[dns] %a → %a\r\n", (CHAR8*)host, ip);
        return 1;
    }
    if (_dns_cstrcmp(cmd, "/dns_add ", 9) == 0) {
        const char *rest = cmd + 9;
        CHAR8 name[OO_DNS_NAME_MAX]={0}; int ni=0;
        while (*rest && *rest != ' ' && ni < OO_DNS_NAME_MAX-1)
            name[ni++]=(CHAR8)*rest++;
        name[ni]=0;
        while (*rest == ' ') rest++;
        CHAR8 ip[OO_DNS_IP_LEN]={0}; int ii=0;
        while (*rest && ii < OO_DNS_IP_LEN-1)
            ip[ii++]=(CHAR8)*rest++;
        ip[ii]=0;
        if (name[0] && ip[0]) oo_dns_add(ctx, name, ip);
        else Print(L"[dns] Usage: /dns_add <host> <ip>\r\n");
        return 1;
    }
    return 0;
}
