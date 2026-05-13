/* oo_mbedtls.h — OO mbedTLS Integration Layer  Phase 4
 * =======================================================
 * Provides direct TLS 1.2/1.3 over EFI_TCP4_PROTOCOL.
 * No libc. Freestanding. Static buffers only.
 *
 * Architecture:
 *   OO application
 *     └─ oo_mbedtls API (this file)
 *          └─ mbedTLS library (ported to EFI freestanding)
 *               └─ EFI_TCP4_PROTOCOL (transport)
 *
 * Phase 4A status:
 *   - EFI TCP4 transport glue:  IMPLEMENTED
 *   - mbedTLS source integration: STUB (requires mbedTLS source tree)
 *   - TLS handshake with SNI:   STUB → calls proxy fallback
 *   - Certificate verification:  STUB → skip (research use)
 *
 * To enable real TLS:
 *   1. cd tools && sh fetch-mbedtls.sh
 *   2. Set OO_MBEDTLS_REAL=1 in Makefile
 *   3. Rebuild — oo_mbedtls.c will use real mbedTLS symbols
 *
 * Freestanding C11. No libc. No malloc (static pool).
 */
#pragma once
#include <efi.h>
#include <efilib.h>

/* ── TCP4 connection state ───────────────────────────────────────────────── */
#define OO_TCP_TX_BUF   8192
#define OO_TCP_RX_BUF   32768

typedef enum {
    OO_TLS_CON_CLOSED  = 0,
    OO_TLS_CON_OPEN    = 1,
    OO_TLS_CON_TLS_OK  = 2,
    OO_TLS_CON_ERROR   = 3,
} OoTlsConState;

typedef struct {
    OoTlsConState  state;
    void          *tcp4;        /* EFI_TCP4_PROTOCOL* */
    EFI_HANDLE     tcp4_child;
    CHAR8          host[128];
    UINT16         port;
    /* Scratch buffers for TCP I/O */
    CHAR8          tx_buf[OO_TCP_TX_BUF];
    CHAR8          rx_buf[OO_TCP_RX_BUF];
    UINTN          rx_len;
    /* TLS context (opaque — set to NULL in stub mode) */
    void          *tls_ctx;
    void          *ssl_ctx;  /* mbedtls_ssl_context* when OO_MBEDTLS_REAL=1 */
    /* Skip cert verification (research/dev mode) */
    int            insecure;
} OoTlsCon;

/* ── Public API ─────────────────────────────────────────────────────────── */

/* Init global TLS system (locate EFI_TCP4_SERVICE_BINDING) */
EFI_STATUS oo_mbedtls_init(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);

/* Open a TLS connection to host:port with SNI */
EFI_STATUS oo_mbedtls_connect(OoTlsCon *con,
                               const CHAR8 *host_ip,  /* dotted-decimal */
                               const CHAR8 *sni_host, /* for SNI + cert */
                               UINT16 port,
                               int insecure);

/* Send HTTP request over TLS + receive response */
EFI_STATUS oo_mbedtls_https_get(OoTlsCon *con,
                                  const CHAR8 *sni_host,
                                  const CHAR8 *path,
                                  const CHAR8 *bearer_token,  /* NULL = no auth */
                                  CHAR8 *resp_buf, UINTN *resp_len);

/* Send HTTP/POST over TLS — Phase 9C: extra_headers = "Key: Val\r\nKey2: Val2" block (NULL=none).
 * If bearer_token != NULL → injects Authorization: Bearer.
 * Claude: pass bearer_token=NULL + extra_headers="x-api-key: ...\r\nanthropic-version: 2023-06-01"
 * Gemini: path includes ?key=<api_key>, bearer_token=NULL, extra_headers=NULL
 * GPT-4:  bearer_token=api_key, extra_headers=NULL */
EFI_STATUS oo_mbedtls_https_post_json(OoTlsCon *con,
                                        const CHAR8 *sni_host,
                                        const CHAR8 *path,
                                        const CHAR8 *bearer_token,  /* NULL = skip Bearer header */
                                        const CHAR8 *extra_headers, /* NULL or "Key: Val\r\n..." */
                                        const CHAR8 *json_body,
                                        CHAR8 *resp_buf, UINTN *resp_len);

/* Close TLS connection + release TCP4 child */
void oo_mbedtls_close(OoTlsCon *con);

/* Print TLS + TCP4 status */
void oo_mbedtls_print_status(void);

/* REPL: /mbedtls_status, /mbedtls_connect <ip> <host> [port], /mbedtls_get <path> */
int oo_mbedtls_repl_cmd(const char *cmd);

/* True if mbedTLS source is compiled in (not stub) */
int oo_mbedtls_is_real(void);

/* Phase 9B: Direct TLS oracle — DNS resolve → TLS connect → POST → extract
 * oracle_id: 1=GPT4, 2=Claude, 3=Gemini
 * api_key: bearer token (in-memory, from /net_oracle_key)
 * Returns EFI_SUCCESS + extracted content in resp_buf */
EFI_STATUS oo_mbedtls_oracle_query(int oracle_id,
                                    const CHAR8 *api_key,
                                    const CHAR8 *prompt,
                                    CHAR8 *resp_buf, UINTN resp_max);

/* Singleton TCP4 service binding handle */
extern EFI_HANDLE g_tcp4_svc_handle;
extern int        g_mbedtls_initialized;
