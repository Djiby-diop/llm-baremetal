/* oo_netboot.h — OO Network Boot Protocol
 * =========================================
 * Allows OO to:
 *   1. Pull model weights from an HTTP/TFTP server at boot
 *   2. Register itself to a federation server
 *   3. Query external AI oracles (GPT/Claude/Gemini) via HTTP JSON API
 *   4. Push learned delta weights back to the federation
 *
 * Architecture:
 *   OO UEFI App
 *     └─ oo_netboot_init()          — probe UEFI network stack
 *     └─ oo_netboot_pull_model()    — HTTP GET weights → RAM
 *     └─ oo_netboot_push_delta()    — HTTP POST delta → federation server
 *     └─ oo_netboot_oracle_query()  — HTTP POST prompt → GPT/Claude/Gemini
 *     └─ oo_netboot_oracle_result() — read oracle response → REPL / model ctx
 *
 * UEFI protocols used:
 *   EFI_HTTP_PROTOCOL, EFI_DHCP4_PROTOCOL, EFI_DNS4_PROTOCOL
 *   EFI_SIMPLE_NETWORK_PROTOCOL (fallback)
 *
 * Freestanding C11 — no libc, no malloc (uses OO zone allocator).
 */
#pragma once
#include <efi.h>
#include <efilib.h>

/* ── Network state ───────────────────────────────────────────────────────── */
typedef enum {
    OO_NET_UNINIT    = 0,
    OO_NET_PROBING,        /* searching for NIC + DHCP */
    OO_NET_READY,          /* IP obtained, HTTP stack up */
    OO_NET_PULLING,        /* downloading model weights */
    OO_NET_CONNECTED,      /* registered to federation */
    OO_NET_ERROR
} OoNetState;

/* Oracle identity — external AI model used as knowledge source */
typedef enum {
    OO_ORACLE_NONE   = 0,
    OO_ORACLE_GPT4,
    OO_ORACLE_CLAUDE,
    OO_ORACLE_GEMINI,
    OO_ORACLE_LOCAL_OO,    /* another OO node */
    OO_ORACLE_CUSTOM       /* user-defined endpoint */
} OoOracleId;

typedef struct {
    OoNetState   state;
    CHAR8        ip[16];          /* our IPv4 as string */
    CHAR8        server_ip[64];   /* federation server */
    UINT16       server_port;
    CHAR8        node_id[32];     /* unique node identifier */
    UINT64       bytes_pulled;
    UINT64       bytes_pushed;
    int          oracle_enabled;
    OoOracleId   oracle_id;
    CHAR8        oracle_endpoint[128];
    CHAR8        oracle_api_key[64];  /* stored in-memory only, never persisted */
} OoNetContext;

/* ── Init ────────────────────────────────────────────────────────────────── */
EFI_STATUS oo_netboot_init(OoNetContext *ctx, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);
void       oo_netboot_shutdown(OoNetContext *ctx);

/* ── Model pull (HTTP GET) ───────────────────────────────────────────────── */
/* Pull model from URL → write into zone-allocated buffer.
 * URL example: "http://192.168.1.100:8080/models/cortex_oo_v2.bin"
 * Returns EFI_SUCCESS + fills *buf_out / *size_out on success. */
EFI_STATUS oo_netboot_pull_model(OoNetContext *ctx,
                                 const CHAR8  *url,
                                 void        **buf_out,
                                 UINTN        *size_out);

/* ── Oracle query (HTTP POST JSON) ──────────────────────────────────────── */
/* Send prompt to external AI oracle, receive text response.
 * Response written into resp_buf (caller-owned, max resp_max bytes). */
EFI_STATUS oo_netboot_oracle_query(OoNetContext *ctx,
                                   OoOracleId    oracle,
                                   const CHAR8  *prompt,
                                   CHAR8        *resp_buf,
                                   UINTN         resp_max);

/* ── Federation push ────────────────────────────────────────────────────── */
/* Send learned delta weights to federation server for aggregation. */
EFI_STATUS oo_netboot_push_delta(OoNetContext *ctx,
                                 const void   *delta_buf,
                                 UINTN         delta_size,
                                 const CHAR8  *model_id);

/* ── REPL helpers ───────────────────────────────────────────────────────── */
void oo_netboot_print_status(OoNetContext *ctx);
int  oo_netboot_repl_cmd(OoNetContext *ctx, const char *cmd);
/* Commands handled:
 *   /net_status             — show IP, federation, oracle state
 *   /net_pull <url>         — pull model from URL
 *   /net_oracle <id> <q>    — query oracle (id: gpt4/claude/gemini)
 *   /net_push               — push delta to federation
 *   /net_oracle_key <key>   — set API key (in-memory, not persisted)
 */
