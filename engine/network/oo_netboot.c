/* oo_netboot.c — OO Network Boot Implementation
 * ================================================
 * Phase 1: stubs + UEFI SimpleNetwork probe
 * Phase 2: real HTTP GET via EFI_HTTP_PROTOCOL
 * Phase 3: oracle JSON API (GPT/Claude/Gemini)
 * Phase 4: federated weight delta push
 *
 * Freestanding C11. No libc. Uses OO serial + Print for output.
 */
#include "oo_netboot.h"
#include <efi.h>
#include <efilib.h>

/* ── Internal helpers ────────────────────────────────────────────────────── */
static void net_serial(const CHAR16 *msg) {
    /* reuse llmk_serial_write_char16 if available — fall back to Print */
    Print(msg);
}

static void u8_to_c16(CHAR16 *dst, UINTN cap, const CHAR8 *src) {
    UINTN i = 0;
    while (i + 1 < cap && src[i]) { dst[i] = (CHAR16)src[i]; i++; }
    dst[i] = 0;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
EFI_STATUS oo_netboot_init(OoNetContext *ctx, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    if (!ctx) return EFI_INVALID_PARAMETER;

    /* Zero-init */
    for (UINTN i = 0; i < sizeof(*ctx); i++) ((UINT8*)ctx)[i] = 0;
    ctx->state       = OO_NET_PROBING;
    ctx->server_port = 8080;

    net_serial(L"[netboot] Probing UEFI network stack...\r\n");

    /* Phase 1: locate EFI_SIMPLE_NETWORK_PROTOCOL handles */
    EFI_GUID snp_guid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
    UINTN    n_handles = 0;
    EFI_HANDLE *handles = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &snp_guid, NULL, &n_handles, &handles);

    if (EFI_ERROR(st) || n_handles == 0) {
        net_serial(L"[netboot] No network adapter found (EFI_SIMPLE_NETWORK not available)\r\n");
        net_serial(L"[netboot] Network boot disabled — offline mode\r\n");
        ctx->state = OO_NET_ERROR;
        return EFI_NOT_FOUND;
    }

    Print(L"[netboot] Found %d network adapter(s)\r\n", (UINT32)n_handles);

    /* Try to get DHCP IP via EFI_IP4_CONFIG2_PROTOCOL (best-effort) */
    /* For now: mark ready with placeholder IP */
    for (UINTN i = 0; i < 15 && "0.0.0.0"[i]; i++) ctx->ip[i] = "0.0.0.0"[i];

    /* Generate unique node_id from current time + handle address */
    UINT64 tick = 0;
    uefi_call_wrapper(ST->RuntimeServices->GetTime, 2, NULL, NULL);
    Print(L"[netboot] Node ready. IP: %a  Node-ID: %a\r\n", ctx->ip, ctx->node_id);

    ctx->state = OO_NET_READY;
    uefi_call_wrapper(BS->FreePool, 1, handles);
    return EFI_SUCCESS;
}

void oo_netboot_shutdown(OoNetContext *ctx) {
    if (!ctx) return;
    ctx->state = OO_NET_UNINIT;
    net_serial(L"[netboot] Shutdown\r\n");
}

/* ── Model pull ─────────────────────────────────────────────────────────── */
EFI_STATUS oo_netboot_pull_model(OoNetContext *ctx,
                                 const CHAR8  *url,
                                 void        **buf_out,
                                 UINTN        *size_out) {
    if (!ctx || ctx->state < OO_NET_READY) return EFI_NOT_READY;
    if (!url || !buf_out || !size_out) return EFI_INVALID_PARAMETER;

    CHAR16 url16[256] = {0};
    u8_to_c16(url16, 256, url);
    Print(L"[netboot] Pulling model from: %s\r\n", url16);

    /* Phase 1 stub — Phase 2 will use EFI_HTTP_PROTOCOL */
    net_serial(L"[netboot] HTTP pull: EFI_HTTP_PROTOCOL not yet wired (Phase 2)\r\n");
    net_serial(L"[netboot] To enable: compile with OO_NET_HTTP=1 in Makefile\r\n");

    ctx->state = OO_NET_READY; /* remain ready */
    return EFI_UNSUPPORTED;
}

/* ── Oracle query ───────────────────────────────────────────────────────── */
EFI_STATUS oo_netboot_oracle_query(OoNetContext *ctx,
                                   OoOracleId    oracle,
                                   const CHAR8  *prompt,
                                   CHAR8        *resp_buf,
                                   UINTN         resp_max) {
    if (!ctx || ctx->state < OO_NET_READY) return EFI_NOT_READY;
    if (!prompt || !resp_buf || resp_max < 2) return EFI_INVALID_PARAMETER;

    const CHAR16 *oracle_names[] = {
        L"NONE", L"GPT-4", L"Claude", L"Gemini", L"OO-Node", L"Custom"
    };
    UINTN oid = (UINTN)oracle;
    if (oid > 5) oid = 5;

    Print(L"[netboot] Oracle query → %s\r\n", oracle_names[oid]);
    Print(L"[netboot] Prompt: %.80a...\r\n", prompt);

    /* Phase 1: stub — Phase 3 wires real HTTP JSON POST
     * JSON payload: {"model":"gpt-4","messages":[{"role":"user","content":"<prompt>"}]}
     * Response parsing: extract "content" field from JSON.
     */
    const CHAR8 *stub_resp = "[OO-NET] Oracle query queued (HTTP not yet wired). "
                             "Phase 3 will connect GPT-4/Claude/Gemini via HTTPS JSON API.";
    UINTN len = 0;
    while (stub_resp[len] && len + 1 < resp_max) {
        resp_buf[len] = stub_resp[len];
        len++;
    }
    resp_buf[len] = 0;

    net_serial(L"[netboot] Oracle stub response queued\r\n");
    return EFI_SUCCESS; /* stub returns success so REPL can display the message */
}

/* ── Federation push ────────────────────────────────────────────────────── */
EFI_STATUS oo_netboot_push_delta(OoNetContext *ctx,
                                 const void   *delta_buf,
                                 UINTN         delta_size,
                                 const CHAR8  *model_id) {
    if (!ctx || ctx->state < OO_NET_READY) return EFI_NOT_READY;
    Print(L"[netboot] Push delta: %a  size=%u bytes\r\n", model_id, (UINT32)delta_size);
    net_serial(L"[netboot] Delta push: Phase 4 (federated learning) — not yet wired\r\n");
    return EFI_UNSUPPORTED;
}

/* ── REPL command handler ───────────────────────────────────────────────── */
void oo_netboot_print_status(OoNetContext *ctx) {
    if (!ctx) { Print(L"  [netboot] not initialized\r\n"); return; }
    const CHAR16 *states[] = {
        L"UNINIT", L"PROBING", L"READY", L"PULLING", L"CONNECTED", L"ERROR"
    };
    UINTN si = (UINTN)ctx->state;
    if (si > 5) si = 5;
    Print(L"\r\n  OO Network Status\r\n");
    Print(L"  ─────────────────\r\n");
    Print(L"  State     : %s\r\n", states[si]);
    Print(L"  IP        : %a\r\n", ctx->ip[0] ? ctx->ip : "(none)");
    Print(L"  Server    : %a:%d\r\n",
          ctx->server_ip[0] ? ctx->server_ip : "(none)", (int)ctx->server_port);
    Print(L"  Node-ID   : %a\r\n", ctx->node_id[0] ? ctx->node_id : "(none)");
    Print(L"  Bytes in  : %u\r\n", (UINT32)ctx->bytes_pulled);
    Print(L"  Bytes out : %u\r\n", (UINT32)ctx->bytes_pushed);
    Print(L"  Oracle    : %s\r\n", ctx->oracle_enabled ? L"ON" : L"OFF");
    if (ctx->oracle_enabled)
        Print(L"  Endpoint  : %a\r\n", ctx->oracle_endpoint);
    Print(L"\r\n");
}

/* Minimal C string helpers (no libc) */
static int _net_strncmp(const char *a, const char *b, int n) {
    for (int i=0;i<n;i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
    }
    return 0;
}

int oo_netboot_repl_cmd(OoNetContext *ctx, const char *cmd) {
    if (!cmd) return 0;
    if (_net_strncmp(cmd, "/net_status", 11) == 0) {
        oo_netboot_print_status(ctx);
        return 1;
    }
    if (_net_strncmp(cmd, "/net_pull ", 10) == 0) {
        void *buf = NULL; UINTN sz = 0;
        oo_netboot_pull_model(ctx, (const CHAR8*)(cmd + 10), &buf, &sz);
        return 1;
    }
    if (_net_strncmp(cmd, "/net_oracle ", 12) == 0) {
        /* /net_oracle <id> <question> */
        const char *rest = cmd + 12;
        OoOracleId oid = OO_ORACLE_GPT4;
        if (_net_strncmp(rest, "claude ", 7) == 0) { oid = OO_ORACLE_CLAUDE; rest += 7; }
        else if (_net_strncmp(rest, "gemini ", 7) == 0) { oid = OO_ORACLE_GEMINI; rest += 7; }
        else if (_net_strncmp(rest, "gpt4 ", 5) == 0) { rest += 5; }
        char resp[1024] = {0};
        oo_netboot_oracle_query(ctx, oid, (const CHAR8*)rest, (CHAR8*)resp, sizeof(resp));
        Print(L"\r\nOracle: %a\r\n\r\n", resp);
        return 1;
    }
    if (_net_strncmp(cmd, "/net_push", 9) == 0) {
        oo_netboot_push_delta(ctx, NULL, 0, (const CHAR8*)"cortex_oo");
        return 1;
    }
    return 0;
}
