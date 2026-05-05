/* oo_dns.h — OO DNS4 Resolver  Phase 3
 * =========================================
 * Minimal EFI_DNS4_PROTOCOL wrapper for bare-metal hostname resolution.
 *
 * Usage:
 *   OoDnsCtx dns;
 *   oo_dns_init(&dns, ImageHandle, SystemTable);
 *   CHAR8 ip[16];
 *   if (oo_dns_resolve(&dns, "api.openai.com", ip, sizeof(ip)) == EFI_SUCCESS)
 *       Print(L"Resolved: %a\r\n", ip);
 *
 * Fallback: if EFI_DNS4_PROTOCOL is unavailable (OVMF without DnsHydra.efi),
 * returns EFI_UNSUPPORTED and caller can use a preconfigured IP.
 *
 * Freestanding C11. No libc. No dynamic allocation.
 */
#pragma once
#include <efi.h>
#include <efilib.h>

#define OO_DNS_CACHE_MAX    16
#define OO_DNS_NAME_MAX     128
#define OO_DNS_IP_LEN       16    /* "255.255.255.255\0" */

/* DNS cache entry */
typedef struct {
    CHAR8  name[OO_DNS_NAME_MAX];
    CHAR8  ip[OO_DNS_IP_LEN];
    UINT32 ttl_ticks;      /* simple countdown, 0 = invalid */
} OoDnsCacheEntry;

typedef struct {
    int              initialized;
    UINT32           resolve_ok;
    UINT32           resolve_fail;
    UINT32           cache_hits;
    /* DNS cache (static, LRU-evict by age) */
    OoDnsCacheEntry  cache[OO_DNS_CACHE_MAX];
    int              cache_count;
    /* EFI_DNS4 handle (NULL if not available) */
    void            *dns4_protocol;   /* EFI_DNS4_PROTOCOL* */
} OoDnsCtx;

/* Init: locate EFI_DNS4_SERVICE_BINDING_PROTOCOL if present */
EFI_STATUS oo_dns_init(OoDnsCtx *ctx, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);

/* Resolve a hostname → dotted-decimal IP string.
 * Checks cache first. Falls back to stub if DNS4 unavailable.
 * Returns EFI_SUCCESS + fills ip_out (OO_DNS_IP_LEN bytes). */
EFI_STATUS oo_dns_resolve(OoDnsCtx *ctx, const CHAR8 *hostname,
                           CHAR8 *ip_out, UINTN ip_cap);

/* Add a manual mapping to the cache (e.g., from repl.cfg or /dns_add) */
void oo_dns_add(OoDnsCtx *ctx, const CHAR8 *name, const CHAR8 *ip);

/* Print cache */
void oo_dns_print_cache(const OoDnsCtx *ctx);

/* REPL commands: /dns_resolve <host>, /dns_add <host> <ip>, /dns_status */
int oo_dns_repl_cmd(OoDnsCtx *ctx, const char *cmd);

/* Global singleton */
extern OoDnsCtx g_oo_dns;
