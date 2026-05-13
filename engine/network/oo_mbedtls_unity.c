/* oo_mbedtls_unity.c — mbedTLS 2.28 Unity Build for OO Bare-Metal
 * =================================================================
 * Single translation unit that compiles all required mbedTLS library
 * files for TLS 1.2 client operation (no OS, no libc, UEFI bare-metal).
 *
 * Compile with:
 *   -DOO_MBEDTLS_REAL=1
 *   -DMBEDTLS_CONFIG_FILE=\"mbedtls/config.h\"
 *   -I engine/network/vendor/mbedtls/include
 *   -ffreestanding -fno-stack-protector
 *
 * Files intentionally excluded:
 *   net_sockets.c  — replaced by oo_mbedtls_tcp_send/recv bio callbacks
 *   timing.c       — no OS timer
 *   entropy_poll.c — replaced by oo_mbedtls_entropy_poll (RDRAND)
 *   debug.c        — no debug output in bare-metal release
 *   ssl_srv.c      — server-side not needed (client only)
 */

#ifdef OO_MBEDTLS_REAL

/* Suppress warnings for third-party code */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

/* ── Platform ─────────────────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/platform.c"
#include "engine/network/vendor/mbedtls/platform_util.c"
#include "engine/network/vendor/mbedtls/error.c"
#include "engine/network/vendor/mbedtls/version.c"
#include "engine/network/vendor/mbedtls/version_features.c"

/* ── Hash functions ───────────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/md.c"
#include "engine/network/vendor/mbedtls/md5.c"
#include "engine/network/vendor/mbedtls/sha1.c"
#include "engine/network/vendor/mbedtls/sha256.c"
#include "engine/network/vendor/mbedtls/sha512.c"

/* ── Symmetric crypto ─────────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/aes.c"
#include "engine/network/vendor/mbedtls/aesni.c"
#include "engine/network/vendor/mbedtls/gcm.c"
#include "engine/network/vendor/mbedtls/ccm.c"
#include "engine/network/vendor/mbedtls/cipher.c"
#include "engine/network/vendor/mbedtls/cipher_wrap.c"

/* ── RNG / Entropy ────────────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/entropy.c"
/* NOTE: entropy_poll.c excluded — using MBEDTLS_ENTROPY_HARDWARE_ALT
 * which calls mbedtls_hardware_poll() → our oo_mbedtls_entropy_poll() */
#include "engine/network/vendor/mbedtls/ctr_drbg.c"
#include "engine/network/vendor/mbedtls/hmac_drbg.c"

/* ── Asymmetric crypto ────────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/bignum.c"
#include "engine/network/vendor/mbedtls/rsa.c"
/* rsa_alt_helpers.c is only in mbedTLS 3.x — not present in 2.28 */
#include "engine/network/vendor/mbedtls/ecp.c"
#include "engine/network/vendor/mbedtls/ecp_curves.c"
#include "engine/network/vendor/mbedtls/ecdh.c"
#include "engine/network/vendor/mbedtls/ecdsa.c"
#include "engine/network/vendor/mbedtls/pk.c"
#include "engine/network/vendor/mbedtls/pk_wrap.c"
#include "engine/network/vendor/mbedtls/pkparse.c"
#include "engine/network/vendor/mbedtls/pkwrite.c"

/* ── ASN.1 / X.509 / PEM ──────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/asn1parse.c"
#include "engine/network/vendor/mbedtls/asn1write.c"
#include "engine/network/vendor/mbedtls/oid.c"
#include "engine/network/vendor/mbedtls/base64.c"
#include "engine/network/vendor/mbedtls/pem.c"
#include "engine/network/vendor/mbedtls/x509.c"
#include "engine/network/vendor/mbedtls/x509_crt.c"

/* ── TLS 1.2 Client ───────────────────────────────────────────────────── */
#include "engine/network/vendor/mbedtls/ssl_tls.c"
#include "engine/network/vendor/mbedtls/ssl_msg.c"
#include "engine/network/vendor/mbedtls/ssl_ciphersuites.c"
#include "engine/network/vendor/mbedtls/ssl_client.c"
#include "engine/network/vendor/mbedtls/ssl_cli.c"

#pragma GCC diagnostic pop

#endif /* OO_MBEDTLS_REAL */
