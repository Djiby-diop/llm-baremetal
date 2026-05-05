#!/bin/sh
# tools/fetch-mbedtls.sh — Download and prepare mbedTLS for OO bare-metal
# =========================================================================
# Downloads mbedTLS 3.6.x (LTS) and patches it for EFI freestanding build.
# After running this script:
#   1. Add OO_MBEDTLS_REAL=1 to Makefile CFLAGS
#   2. Rebuild: make clean && make -j4
#
# Requirements: curl or wget, git (optional)
# =========================================================================
set -e

MBEDTLS_VERSION="3.6.2"
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v${MBEDTLS_VERSION}.tar.gz"
DEST="$(dirname "$0")/../engine/network/mbedtls"

echo "[fetch-mbedtls] Target: ${DEST}"
echo "[fetch-mbedtls] Version: mbedTLS ${MBEDTLS_VERSION}"

if [ -d "${DEST}" ]; then
    echo "[fetch-mbedtls] Already exists — delete ${DEST} to re-fetch"
    exit 0
fi

mkdir -p "${DEST}"

# Download
TMP="/tmp/mbedtls-${MBEDTLS_VERSION}.tar.gz"
if command -v curl >/dev/null 2>&1; then
    curl -L -o "${TMP}" "${MBEDTLS_URL}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${TMP}" "${MBEDTLS_URL}"
else
    echo "ERROR: need curl or wget"
    exit 1
fi

# Extract
tar -xzf "${TMP}" -C /tmp
SRCDIR="/tmp/mbedtls-${MBEDTLS_VERSION}"

# Copy only what we need (crypto + ssl + net stubs)
mkdir -p "${DEST}/include/mbedtls"
mkdir -p "${DEST}/library"

cp "${SRCDIR}"/include/mbedtls/*.h "${DEST}/include/mbedtls/"
cp "${SRCDIR}"/library/aes.c       "${DEST}/library/"
cp "${SRCDIR}"/library/aesni.c     "${DEST}/library/"
cp "${SRCDIR}"/library/bignum.c    "${DEST}/library/"
cp "${SRCDIR}"/library/cipher.c    "${DEST}/library/"
cp "${SRCDIR}"/library/cipher_wrap.c "${DEST}/library/"
cp "${SRCDIR}"/library/ctr_drbg.c  "${DEST}/library/"
cp "${SRCDIR}"/library/entropy.c   "${DEST}/library/"
cp "${SRCDIR}"/library/gcm.c       "${DEST}/library/"
cp "${SRCDIR}"/library/hmac_drbg.c "${DEST}/library/"
cp "${SRCDIR}"/library/md.c        "${DEST}/library/"
cp "${SRCDIR}"/library/md5.c       "${DEST}/library/"
cp "${SRCDIR}"/library/net_sockets.c "${DEST}/library/"
cp "${SRCDIR}"/library/pem.c       "${DEST}/library/"
cp "${SRCDIR}"/library/pk.c        "${DEST}/library/"
cp "${SRCDIR}"/library/pk_wrap.c   "${DEST}/library/"
cp "${SRCDIR}"/library/platform.c  "${DEST}/library/"
cp "${SRCDIR}"/library/rsa.c       "${DEST}/library/"
cp "${SRCDIR}"/library/sha256.c    "${DEST}/library/"
cp "${SRCDIR}"/library/sha512.c    "${DEST}/library/"
cp "${SRCDIR}"/library/ssl_ciphersuites.c "${DEST}/library/"
cp "${SRCDIR}"/library/ssl_client.c "${DEST}/library/"
cp "${SRCDIR}"/library/ssl_msg.c   "${DEST}/library/"
cp "${SRCDIR}"/library/ssl_tls.c   "${DEST}/library/"
cp "${SRCDIR}"/library/ssl_tls12_client.c "${DEST}/library/"
cp "${SRCDIR}"/library/x509.c      "${DEST}/library/"
cp "${SRCDIR}"/library/x509_crt.c  "${DEST}/library/"

# Write OO EFI mbedTLS config (no libc, no file I/O, no threads)
cat > "${DEST}/include/mbedtls/mbedtls_config.h" << 'CONFIG'
/* OO EFI mbedTLS config — bare-metal, no libc */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* ── Algorithms needed for TLS 1.2 ── */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_GCM_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_MD_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C

/* ── TLS 1.2 ── */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_CIPHERSUITES \
    MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384, \
    MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256

/* ── No POSIX / no filesystem ── */
#undef  MBEDTLS_NET_C
#undef  MBEDTLS_TIMING_C
#undef  MBEDTLS_FS_IO
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_HMAC_DRBG_C

/* ── No libc — provide stubs ── */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO  oo_mbedtls_snprintf
#define MBEDTLS_PLATFORM_PRINTF_MACRO    oo_mbedtls_printf

/* ── Skip cert verification for development ── */
#define MBEDTLS_SSL_VERIFY_NONE   /* override per-connection */

#include "check_config.h"
#endif /* MBEDTLS_CONFIG_H */
CONFIG

# Write Makefile fragment for mbedTLS
cat > "${DEST}/mbedtls.mk" << 'MK'
# mbedtls.mk — include in main Makefile when OO_MBEDTLS_REAL=1
MBEDTLS_DIR := engine/network/mbedtls
MBEDTLS_INC := -I$(MBEDTLS_DIR)/include
MBEDTLS_SRC := $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJ := $(patsubst %.c,build/%.o,$(MBEDTLS_SRC))
CFLAGS      += $(MBEDTLS_INC) -DOO_MBEDTLS_REAL=1 \
               -DMBEDTLS_USER_CONFIG_FILE='"mbedtls/mbedtls_config.h"'
MK

echo "[fetch-mbedtls] Done."
echo "[fetch-mbedtls] Next: add 'include engine/network/mbedtls/mbedtls.mk' to Makefile"
echo "[fetch-mbedtls] Then: make clean && make -j4"
echo "[fetch-mbedtls] TLS commands: /mbedtls_status /tls_mode direct"
