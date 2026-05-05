/* oo_tls.h — OO TLS Abstraction Layer  Phase 3
 * ================================================
 * Provides a single interface for HTTPS that works in two modes:
 *
 *   OO_TLS_MODE_PROXY  (default/Phase 2):
 *     Plain HTTP → local oo-oracle-proxy.py → HTTPS upstream
 *     No TLS stack required. Works today.
 *
 *   OO_TLS_MODE_DIRECT (Phase 3 / future):
 *     mbedTLS port for bare-metal EFI.
 *     Direct TLS 1.2/1.3 to api.openai.com etc.
 *     Requires mbedTLS + EFI_TCP4_PROTOCOL + entropy source.
 *
 * The layer is a stub now — it auto-detects which mode is viable
 * and falls back gracefully. Real mbedTLS integration is Phase 4.
 *
 * Usage:
 *   OoTlsCtx tls;
 *   oo_tls_init(&tls, OO_TLS_MODE_PROXY);
 *   oo_tls_set_proxy(&tls, "192.168.1.100", 8080);
 *   oo_tls_https_get(&tls, "api.openai.com", 443, "/v1/models", resp, &resp_len);
 *
 * Freestanding C11. No libc. No dynamic allocation.
 */
#pragma once
#include <efi.h>
#include <efilib.h>

#define OO_TLS_HOST_MAX    128
#define OO_TLS_PATH_MAX    256
#define OO_TLS_TOKEN_MAX   128
#define OO_TLS_RESP_MAX    16384

typedef enum {
    OO_TLS_MODE_PROXY  = 0,   /* Plain HTTP via local proxy (Phase 2 default) */
    OO_TLS_MODE_DIRECT = 1,   /* Direct TLS (Phase 4, mbedTLS)                */
} OoTlsMode;

typedef enum {
    OO_TLS_STATE_UNINIT = 0,
    OO_TLS_STATE_READY  = 1,
    OO_TLS_STATE_ERROR  = 2,
} OoTlsState;

typedef struct {
    OoTlsMode  mode;
    OoTlsState state;
    /* Proxy settings (mode = PROXY) */
    CHAR8      proxy_host[64];
    UINT16     proxy_port;
    /* Auth token (stored in RAM only) */
    CHAR8      bearer_token[OO_TLS_TOKEN_MAX];
    /* Stats */
    UINT32     requests_sent;
    UINT32     requests_ok;
    UINT32     requests_failed;
} OoTlsCtx;

/* Lifecycle */
void oo_tls_init(OoTlsCtx *ctx, OoTlsMode mode);
void oo_tls_set_proxy(OoTlsCtx *ctx, const CHAR8 *host, UINT16 port);
void oo_tls_set_token(OoTlsCtx *ctx, const CHAR8 *bearer_token);
void oo_tls_print_status(const OoTlsCtx *ctx);

/* HTTPS GET via chosen mode (proxy or direct TLS) */
EFI_STATUS oo_tls_https_get(OoTlsCtx *ctx,
                             const CHAR8 *host, UINT16 port,
                             const CHAR8 *path,
                             CHAR8 *resp_buf, UINTN *resp_len);

/* HTTPS POST JSON */
EFI_STATUS oo_tls_https_post_json(OoTlsCtx *ctx,
                                   const CHAR8 *host, UINT16 port,
                                   const CHAR8 *path,
                                   const CHAR8 *json_body,
                                   CHAR8 *resp_buf, UINTN *resp_len);

/* REPL commands: /tls_mode [proxy|direct], /tls_proxy <ip> [port],
 *                /tls_token <key>, /tls_status */
int oo_tls_repl_cmd(OoTlsCtx *ctx, const char *cmd);

/* Global singleton */
extern OoTlsCtx g_oo_tls;
