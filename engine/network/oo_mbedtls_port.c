/* oo_mbedtls_port.c — mbedTLS Platform Port for OO Bare-Metal EFI
 * ================================================================
 * Provides everything mbedTLS needs that it can't get from libc:
 *   - Static malloc pool (384 KB)
 *   - RDRAND entropy source
 *   - printf / snprintf stubs (UEFI Print)
 *   - TCP4 bio callbacks (send/recv over EFI_TCP4_PROTOCOL)
 *   - TLS 1.2 handshake wiring
 *
 * Compile only when OO_MBEDTLS_REAL=1 (mbedTLS source present).
 * Freestanding C11. No libc.
 */
#ifdef OO_MBEDTLS_REAL

#include "oo_mbedtls.h"
#include "oo_mbedtls_port.h"
#include <efi.h>
#include <efilib.h>

/* mbedTLS headers */
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/platform.h>

/* ── Static heap pool (384 KB) ──────────────────────────────────────────── */
#define OO_POOL_SIZE (384 * 1024)
static unsigned char _pool[OO_POOL_SIZE] __attribute__((aligned(16)));
static volatile int  _pool_lock = 0;

typedef struct _Block { struct _Block *next; uint32_t size; uint32_t free; } _Block;

static _Block *_pool_head = NULL;

static void _pool_init(void) {
    _Block *b   = (_Block *)_pool;
    b->size     = OO_POOL_SIZE - sizeof(_Block);
    b->free     = 1;
    b->next     = NULL;
    _pool_head  = b;
}

void *oo_mbedtls_malloc(uint32_t size) {
    if (!_pool_head) _pool_init();
    if (size == 0) return NULL;
    /* Align to 8 bytes */
    size = (size + 7u) & ~7u;
    for (_Block *b = _pool_head; b; b = b->next) {
        if (b->free && b->size >= size) {
            /* Split if enough room for a new block */
            if (b->size >= size + sizeof(_Block) + 8u) {
                _Block *nb = (_Block *)((unsigned char *)(b+1) + size);
                nb->size   = b->size - size - (uint32_t)sizeof(_Block);
                nb->free   = 1;
                nb->next   = b->next;
                b->next    = nb;
                b->size    = size;
            }
            b->free = 0;
            return (void *)(b + 1);
        }
    }
    return NULL; /* OOM */
}

void oo_mbedtls_free(void *ptr) {
    if (!ptr || !_pool_head) return;
    _Block *b = (_Block *)ptr - 1;
    b->free = 1;
    /* Merge adjacent free blocks */
    for (_Block *c = _pool_head; c && c->next; c = c->next) {
        if (c->free && c->next->free) {
            c->size += (uint32_t)sizeof(_Block) + c->next->size;
            c->next  = c->next->next;
        }
    }
}

/* ── printf/snprintf stubs (required by MBEDTLS_PLATFORM_NO_STD_FUNCTIONS) */
int oo_mbedtls_printf(const char *fmt, ...) {
    /* Route to UEFI Print — only used for debug; stripped in release */
    (void)fmt;
    return 0;
}

int oo_mbedtls_snprintf(char *buf, uint64_t size, const char *fmt, ...) {
    (void)buf; (void)size; (void)fmt;
    if (buf && size) buf[0] = '\0';
    return 0;
}

/* ── RDRAND entropy (hardware RNG) ──────────────────────────────────────── */
static int _rdrand64(uint64_t *out) {
    unsigned char ok;
    __asm__ volatile(
        "rdrand %0\n"
        "setc   %1\n"
        : "=r"(*out), "=qm"(ok)
        :
        : "cc"
    );
    return ok ? 0 : -1;
}

int oo_mbedtls_entropy_poll(void *data, unsigned char *output,
                             uint32_t len, uint32_t *olen) {
    (void)data;
    *olen = 0;
    for (uint32_t i = 0; i + 8 <= len; i += 8) {
        uint64_t r = 0;
        /* Retry up to 10 times (RDRAND can fail transiently) */
        int ok = 0;
        for (int t = 0; t < 10; t++) {
            if (_rdrand64(&r) == 0) { ok = 1; break; }
        }
        if (!ok) break;
        for (int j = 0; j < 8; j++) output[i+j] = (unsigned char)(r >> (j*8));
        *olen += 8;
    }
    return (*olen > 0) ? 0 : -1;
}

/* ── mbedTLS TCP4 bio callbacks ─────────────────────────────────────────── */
/*
 * These are registered via mbedtls_ssl_set_bio() so mbedTLS calls them
 * instead of POSIX send()/recv(). They route through EFI_TCP4_PROTOCOL.
 */
int oo_mbedtls_tcp_send(void *ctx, const unsigned char *buf, uint32_t len) {
    OoTlsCon *con = (OoTlsCon *)ctx;
    if (!con || !con->tcp4 || len == 0) return MBEDTLS_ERR_NET_SEND_FAILED;

    typedef struct {
        int    Status;
        void  *Event;
        void  *Packet;
    } _Tok;
    typedef struct {
        uint8_t  Type;
        uint32_t FragCount;
        struct { void *Buf; uint32_t Len; } Frags[1];
    } _Pkt;
    typedef struct {
        int (EFIAPI *GetModeData)(void*,void*,void*,void*,void*,void*);
        int (EFIAPI *Configure)(void*,void*);
        int (EFIAPI *Routes)(void*,int,void*,void*,void*);
        int (EFIAPI *Connect)(void*,void*);
        int (EFIAPI *Accept)(void*,void*);
        int (EFIAPI *Transmit)(void*,_Tok*);
        int (EFIAPI *Receive)(void*,_Tok*);
        int (EFIAPI *Close)(void*,void*);
        int (EFIAPI *Cancel)(void*,void*);
        int (EFIAPI *Poll)(void*);
    } _Tcp4;

    _Tcp4 *tcp4 = (_Tcp4 *)con->tcp4;
    uint32_t send = (len > OO_TCP_TX_BUF) ? OO_TCP_TX_BUF : len;
    for (uint32_t i = 0; i < send; i++) con->tx_buf[i] = buf[i];

    _Pkt pkt = {0};
    pkt.FragCount      = 1;
    pkt.Frags[0].Buf   = con->tx_buf;
    pkt.Frags[0].Len   = send;

    _Tok tok = {0};
    tok.Status = 0x8000000000000006LL; /* EFI_NOT_READY */
    tok.Packet = &pkt;

    if (uefi_call_wrapper(tcp4->Transmit, 2, tcp4, &tok) != 0)
        return MBEDTLS_ERR_NET_SEND_FAILED;

    for (int i = 0; i < 200000 && tok.Status == (int)0x80000000; i++)
        uefi_call_wrapper(tcp4->Poll, 1, tcp4);

    return (tok.Status == 0) ? (int)send : MBEDTLS_ERR_NET_SEND_FAILED;
}

int oo_mbedtls_tcp_recv(void *ctx, unsigned char *buf, uint32_t len) {
    OoTlsCon *con = (OoTlsCon *)ctx;
    if (!con || !con->tcp4) return MBEDTLS_ERR_NET_RECV_FAILED;

    typedef struct {
        int    Status;
        void  *Event;
        void  *Packet;
    } _Tok;
    typedef struct {
        uint8_t  Type;
        uint32_t FragCount;
        struct { void *Buf; uint32_t Len; } Frags[1];
    } _Pkt;
    typedef struct {
        int (EFIAPI *GetModeData)(void*,void*,void*,void*,void*,void*);
        int (EFIAPI *Configure)(void*,void*);
        int (EFIAPI *Routes)(void*,int,void*,void*,void*);
        int (EFIAPI *Connect)(void*,void*);
        int (EFIAPI *Accept)(void*,void*);
        int (EFIAPI *Transmit)(void*,_Tok*);
        int (EFIAPI *Receive)(void*,_Tok*);
        int (EFIAPI *Close)(void*,void*);
        int (EFIAPI *Cancel)(void*,void*);
        int (EFIAPI *Poll)(void*);
    } _Tcp4;

    _Tcp4 *tcp4 = (_Tcp4 *)con->tcp4;
    uint32_t want = (len > OO_TCP_RX_BUF-1) ? OO_TCP_RX_BUF-1 : len;

    _Pkt pkt = {0};
    pkt.FragCount      = 1;
    pkt.Frags[0].Buf   = con->rx_buf;
    pkt.Frags[0].Len   = want;

    _Tok tok = {0};
    tok.Status = 0x8000000000000006LL;
    tok.Packet = &pkt;

    if (uefi_call_wrapper(tcp4->Receive, 2, tcp4, &tok) != 0)
        return MBEDTLS_ERR_NET_RECV_FAILED;

    for (int i = 0; i < 400000 && tok.Status == (int)0x80000000; i++)
        uefi_call_wrapper(tcp4->Poll, 1, tcp4);

    if (tok.Status != 0) return MBEDTLS_ERR_NET_RECV_FAILED;

    uint32_t got = pkt.Frags[0].Len;
    con->rx_len = got;
    for (uint32_t i = 0; i < got && i < len; i++) buf[i] = con->rx_buf[i];
    return (int)got;
}

/* ── Real TLS handshake ─────────────────────────────────────────────────── */
/*
 * Static mbedTLS contexts — one global session at a time.
 * For multi-session support, extend OoTlsCon to carry these inline.
 */
static mbedtls_ssl_context     _ssl;
static mbedtls_ssl_config      _ssl_cfg;
static mbedtls_entropy_context _entropy;
static mbedtls_ctr_drbg_context _ctr_drbg;
static int                     _tls_ctx_initialized = 0;

static void _tls_ctx_reset(void) {
    if (_tls_ctx_initialized) {
        mbedtls_ssl_free(&_ssl);
        mbedtls_ssl_config_free(&_ssl_cfg);
        mbedtls_ctr_drbg_free(&_ctr_drbg);
        mbedtls_entropy_free(&_entropy);
    }
    mbedtls_ssl_init(&_ssl);
    mbedtls_ssl_config_init(&_ssl_cfg);
    mbedtls_entropy_init(&_entropy);
    mbedtls_ctr_drbg_init(&_ctr_drbg);
    _tls_ctx_initialized = 1;
}

EFI_STATUS oo_mbedtls_do_handshake(OoTlsCon *con) {
    _tls_ctx_reset();

    /* Set up platform (malloc) */
    mbedtls_platform_set_calloc_free(
        (void *(*)(uint32_t, uint32_t))oo_mbedtls_malloc, oo_mbedtls_free);

    /* Seed RNG with hardware entropy */
    static const unsigned char _pers[] = "oo-mbedtls-1.0";
    mbedtls_entropy_add_source(&_entropy, oo_mbedtls_entropy_poll,
                               NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    int ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func,
                                    &_entropy, _pers, sizeof(_pers)-1);
    if (ret != 0) {
        Print(L"[tls] RNG seed failed: -0x%04x\r\n", (unsigned)(-ret));
        return EFI_ABORTED;
    }

    /* SSL config — TLS 1.2 client */
    ret = mbedtls_ssl_config_defaults(&_ssl_cfg,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        Print(L"[tls] ssl_config_defaults: -0x%04x\r\n", (unsigned)(-ret));
        return EFI_ABORTED;
    }

    /* Skip cert verification (no CA bundle in bare-metal yet) */
    mbedtls_ssl_conf_authmode(&_ssl_cfg, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&_ssl_cfg, mbedtls_ctr_drbg_random, &_ctr_drbg);

    /* Set up SSL context */
    ret = mbedtls_ssl_setup(&_ssl, &_ssl_cfg);
    if (ret != 0) {
        Print(L"[tls] ssl_setup failed: -0x%04x\r\n", (unsigned)(-ret));
        return EFI_ABORTED;
    }

    /* SNI */
    if (con->host[0])
        mbedtls_ssl_set_hostname(&_ssl, con->host);

    /* Wire TCP4 bio */
    mbedtls_ssl_set_bio(&_ssl, con, oo_mbedtls_tcp_send, oo_mbedtls_tcp_recv, NULL);

    /* TLS handshake */
    Print(L"[tls] Handshake with %a...\r\n", con->host);
    while ((ret = mbedtls_ssl_handshake(&_ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            Print(L"[tls] Handshake FAILED: -0x%04x\r\n", (unsigned)(-ret));
            con->state = OO_TLS_CON_ERROR;
            return EFI_ABORTED;
        }
    }

    Print(L"[tls] Handshake OK — cipher: %a\r\n",
          mbedtls_ssl_get_ciphersuite(&_ssl));
    con->state    = OO_TLS_CON_TLS_OK;
    con->ssl_ctx  = &_ssl;
    return EFI_SUCCESS;
}

/* ── TLS read/write (post-handshake) ────────────────────────────────────── */
int oo_mbedtls_tls_write(OoTlsCon *con, const unsigned char *buf, uint32_t len) {
    if (!con || !con->ssl_ctx) return -1;
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)con->ssl_ctx;
    int ret;
    while ((ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) return ret;
    }
    return ret;
}

int oo_mbedtls_tls_read(OoTlsCon *con, unsigned char *buf, uint32_t len) {
    if (!con || !con->ssl_ctx) return -1;
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)con->ssl_ctx;
    int ret;
    do { ret = mbedtls_ssl_read(ssl, buf, len); }
    while (ret == MBEDTLS_ERR_SSL_WANT_READ);
    return ret;
}

void oo_mbedtls_tls_close(OoTlsCon *con) {
    if (!con || !con->ssl_ctx) return;
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)con->ssl_ctx;
    mbedtls_ssl_close_notify(ssl);
    con->ssl_ctx = NULL;
    con->state   = OO_TLS_CON_CLOSED;
}

/* ── Pool stats ─────────────────────────────────────────────────────────── */
void oo_mbedtls_pool_stats(uint32_t *used, uint32_t *total) {
    uint32_t u = 0;
    for (_Block *b = _pool_head; b; b = b->next)
        if (!b->free) u += b->size + (uint32_t)sizeof(_Block);
    if (used)  *used  = u;
    if (total) *total = OO_POOL_SIZE;
}

#endif /* OO_MBEDTLS_REAL */
