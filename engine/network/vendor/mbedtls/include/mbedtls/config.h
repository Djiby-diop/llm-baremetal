/* config.h — OO Bare-Metal mbedTLS 2.28 — minimal TLS 1.2 client
 * ================================================================
 * No OS, no libc. EFI bare-metal. x86_64.
 * Single cipher suite: TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Platform */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_HAVE_ASM
#undef  MBEDTLS_HAVE_TIME
#undef  MBEDTLS_HAVE_TIME_DATE
#undef  MBEDTLS_NET_C
#undef  MBEDTLS_TIMING_C
#undef  MBEDTLS_FS_IO
#undef  MBEDTLS_DEBUG_C

/* Entropy: RDRAND via MBEDTLS_ENTROPY_HARDWARE_ALT hook */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_MAX_SOURCES 4
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_HMAC_DRBG_C

/* Hash (SHA512 excluded — uint64_t conflicts with EFI freestanding types) */
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C

/* Symmetric crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_AESNI_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC

/* Bignum / RSA */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_OID_C

/* Elliptic curves */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C

/* ASN.1 (mbedTLS 2.x uses MBEDTLS_ASN1_PARSE_C / WRITE_C) */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

/* PEM/DER */
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* PK */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C

/* X.509 (MBEDTLS_X509_USE_C is the internal prerequisite for X509_CRT) */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_C
#define MBEDTLS_X509_CRT_PARSE_C

/* Key exchange */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED

/* TLS 1.2 client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#undef  MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 16384

/* Misc */
#define MBEDTLS_ERROR_C
#define MBEDTLS_VERSION_C

#include "check_config.h"
#endif /* MBEDTLS_CONFIG_H */
