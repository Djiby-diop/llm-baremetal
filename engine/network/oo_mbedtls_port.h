/* oo_mbedtls_port.h — OO mbedTLS Platform Port API
 * ===================================================
 * Provides freestanding platform services for mbedTLS:
 *   malloc pool, entropy, bio callbacks, handshake wiring.
 */
#pragma once
#include <stdint.h>
#include "oo_mbedtls.h"

/* ── Malloc pool ─────────────────────────────────────────────────────────── */
void *oo_mbedtls_malloc(uint32_t size);
void  oo_mbedtls_free(void *ptr);
void  oo_mbedtls_pool_stats(uint32_t *used, uint32_t *total);

/* ── Printf stubs (MBEDTLS_PLATFORM_NO_STD_FUNCTIONS) ────────────────────── */
int   oo_mbedtls_printf(const char *fmt, ...);
int   oo_mbedtls_snprintf(char *buf, uint64_t size, const char *fmt, ...);

/* ── Entropy ─────────────────────────────────────────────────────────────── */
int   oo_mbedtls_entropy_poll(void *data, unsigned char *output,
                               uint32_t len, uint32_t *olen);

/* ── Bio callbacks (TCP4 send/recv for mbedtls_ssl_set_bio) ─────────────── */
int   oo_mbedtls_tcp_send(void *ctx, const unsigned char *buf, uint32_t len);
int   oo_mbedtls_tcp_recv(void *ctx, unsigned char *buf, uint32_t len);

/* ── TLS handshake + I/O ─────────────────────────────────────────────────── */
/* Returns EFI_SUCCESS on handshake OK; sets con->ssl_ctx */
unsigned long oo_mbedtls_do_handshake(OoTlsCon *con);
int   oo_mbedtls_tls_write(OoTlsCon *con, const unsigned char *buf, uint32_t len);
int   oo_mbedtls_tls_read (OoTlsCon *con,       unsigned char *buf, uint32_t len);
void  oo_mbedtls_tls_close(OoTlsCon *con);
