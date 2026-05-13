/* mbedtls_config.h — OO Bare-Metal EFI TLS 1.2 Client Config
 * =============================================================
 * Minimal TLS 1.2 client for bare-metal UEFI.
 * No OS, no libc (freestanding). Uses:
 *   - RDRAND for entropy (hardware RNG)
 *   - EFI_TCP4_PROTOCOL for network transport
 *   - Static 384KB memory pool (no malloc)
 *   - ECDHE_RSA_AES_128_GCM_SHA256 cipher suite only
 *   - No certificate verification (research mode)
 */

#ifndef OO_MBEDTLS_CONFIG_H
#define OO_MBEDTLS_CONFIG_H

/* ── Platform / OS ─────────────────────────────────────────────────────── */
/* No OS, no filesystem, no threading */
#undef  MBEDTLS_HAVE_TIME
#undef  MBEDTLS_HAVE_TIME_DATE
#undef  MBEDTLS_THREADING_C
#undef  MBEDTLS_NET_C           /* We provide TCP4 bio callbacks instead */
#undef  MBEDTLS_TIMING_C
#undef  MBEDTLS_FS_IO           /* No filesystem */

/* Platform printf/calloc redirect to our stubs */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_PRINTF_MACRO   oo_mbedtls_printf
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO oo_mbedtls_snprintf
#define MBEDTLS_PLATFORM_CALLOC_MACRO   oo_mbedtls_calloc_stub
#define MBEDTLS_PLATFORM_FREE_MACRO     oo_mbedtls_free

/* No debug output (saves space) */
#undef  MBEDTLS_DEBUG_C

/* ── Entropy ────────────────────────────────────────────────────────────── */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT    /* Use our RDRAND poll */
#define MBEDTLS_NO_PLATFORM_ENTROPY     /* Skip OS-level entropy */
#define MBEDTLS_ENTROPY_MAX_SOURCES 4

/* ── RNG ─────────────────────────────────────────────────────────────────── */
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_HMAC_DRBG_C

/* ── Hash functions ─────────────────────────────────────────────────────── */
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C

/* ── Symmetric crypto ───────────────────────────────────────────────────── */
#define MBEDTLS_AES_C
#define MBEDTLS_AESNI_C             /* Use hardware AES-NI (x86_64) */
#define MBEDTLS_GCM_C
#define MBEDTLS_CCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC

/* ── Asymmetric crypto ──────────────────────────────────────────────────── */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

/* ── Key exchange / cipher suites ───────────────────────────────────────── */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED

/* ── TLS ─────────────────────────────────────────────────────────────────── */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384  /* 16KB record */
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 16384

/* Disable server-side (we are client-only) */
#undef  MBEDTLS_SSL_SRV_C

/* ── X.509 / ASN.1 (minimal — needed by TLS even with verify=none) ──────── */
#define MBEDTLS_X509_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_ASN1_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_DER_TO_PEM
#define MBEDTLS_PK_WRITE_C

/* ── Error strings (saves ~10KB vs full) ────────────────────────────────── */
#define MBEDTLS_ERROR_C

/* ── Version ────────────────────────────────────────────────────────────── */
#define MBEDTLS_VERSION_C
#define MBEDTLS_VERSION_FEATURES

/* Forward declarations for platform macros */
#ifdef __cplusplus
extern "C" {
#endif
int   oo_mbedtls_printf(const char *fmt, ...);
int   oo_mbedtls_snprintf(char *buf, unsigned long long size, const char *fmt, ...);
void *oo_mbedtls_calloc_stub(unsigned long n, unsigned long size);
void  oo_mbedtls_free(void *ptr);
#ifdef __cplusplus
}
#endif

/* Pull in validation checks */
#include "check_config.h"

#endif /* OO_MBEDTLS_CONFIG_H */
