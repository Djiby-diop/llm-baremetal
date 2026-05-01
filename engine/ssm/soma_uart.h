// soma_uart.h — OO Bare-Metal UART Output (Phase H.1)
//
// Writes structured text events to COM1 (I/O port 0x3F8, 115200 8N1).
// QEMU captures this with: -serial file:OO_UART.log
// oo-host reads and parses it in real-time via serial.rs
//
// Event format (one line per event, \r\n terminated):
//   [oo-event] kind=<kind> ts=<rdtsc_lo> <key>=<val> ...
//
// Kinds:
//   boot      — system started
//   gen       — inference generation complete (turn=N tokens=N tok_s=N)
//   halt      — generation stopped (reason=eos/max/warden)
//   warden    — pressure change (level=N)
//   dna       — DNA evolution (gen=N)
//   entropy   — RNG re-seed (rdrand=hw/sw seed=0xXXX)
//   repl      — REPL command entered (cmd=<first_word>)
//   journal   — journal save (entries=N)
//   error     — error event (msg=<truncated>)
//   heartbeat — periodic alive signal (tok_total=N mem_mb=N)
//
// Freestanding C11 — no libc, no malloc. Header-only (inline/static).

#pragma once

// ============================================================
// COM1 port constants (16550 UART)
// ============================================================

#define SOMA_UART_PORT        0x3F8U   // COM1
#define SOMA_UART_THR         (SOMA_UART_PORT + 0)  // Transmit Holding Register
#define SOMA_UART_IER         (SOMA_UART_PORT + 1)  // Interrupt Enable Register
#define SOMA_UART_FCR         (SOMA_UART_PORT + 2)  // FIFO Control Register
#define SOMA_UART_LCR         (SOMA_UART_PORT + 3)  // Line Control Register
#define SOMA_UART_MCR         (SOMA_UART_PORT + 4)  // Modem Control Register
#define SOMA_UART_LSR         (SOMA_UART_PORT + 5)  // Line Status Register
#define SOMA_UART_DLL         (SOMA_UART_PORT + 0)  // Divisor Latch Low  (DLAB=1)
#define SOMA_UART_DLH         (SOMA_UART_PORT + 1)  // Divisor Latch High (DLAB=1)

#define SOMA_UART_LSR_DR      0x01U    // Data Ready
#define SOMA_UART_LSR_THRE    0x20U    // Transmit Holding Register Empty
#define SOMA_UART_BAUD_115200 1U       // divisor = 115200 / 115200

// ============================================================
// Global state
// ============================================================

static int  g_soma_uart_ready  = 0;
static int  g_soma_uart_enable = 1;  // set to 0 to silence

// ============================================================
// Low-level I/O port access (inline asm)
// ============================================================

static inline void _uart_outb(unsigned short port, unsigned char val) {
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
#else
    (void)port; (void)val;
#endif
}

static inline unsigned char _uart_inb(unsigned short port) {
#if defined(__i386__) || defined(__x86_64__)
    unsigned char val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
#else
    (void)port; return 0;
#endif
}

// ============================================================
// soma_uart_init — initialize COM1 at 115200 8N1
//
// Safe to call multiple times. Detects presence via loopback test.
// Returns 1 if UART is available, 0 otherwise (QEMU always OK).
// ============================================================

static inline int soma_uart_init(void) {
    if (g_soma_uart_ready) return 1;

    // Disable interrupts
    _uart_outb(SOMA_UART_IER, 0x00);
    // Enable DLAB (baud rate divisor access)
    _uart_outb(SOMA_UART_LCR, 0x80);
    // Set 115200 baud (divisor=1)
    _uart_outb(SOMA_UART_DLL, SOMA_UART_BAUD_115200);
    _uart_outb(SOMA_UART_DLH, 0x00);
    // 8 bits, no parity, 1 stop bit
    _uart_outb(SOMA_UART_LCR, 0x03);
    // Enable FIFO, clear TX/RX, 14-byte threshold
    _uart_outb(SOMA_UART_FCR, 0xC7);
    // RTS + DTR
    _uart_outb(SOMA_UART_MCR, 0x0B);

    // Loopback test: write 0xAE, read it back
    _uart_outb(SOMA_UART_MCR, 0x1E);  // Enable loopback
    _uart_outb(SOMA_UART_THR, 0xAE);
    // Brief busy-wait
    volatile int w = 500;
    while (w-- > 0) { __asm__ __volatile__("" ::: "memory"); }
    unsigned char rb = _uart_inb(SOMA_UART_PORT);

    _uart_outb(SOMA_UART_MCR, 0x0F);  // Normal operation

    if (rb != 0xAE) {
        // UART not available (might be virtual/absent)
        // Don't block, just disable
        g_soma_uart_enable = 0;
        return 0;
    }

    g_soma_uart_ready = 1;
    return 1;
}

// ============================================================
// soma_uart_putc — send one byte (busy-wait on THRE)
// ============================================================

static inline void soma_uart_putc(char c) {
    if (!g_soma_uart_enable) return;
    // Wait for Transmit Holding Register Empty
    int retries = 10000;
    while (retries-- > 0 && !(_uart_inb(SOMA_UART_LSR) & SOMA_UART_LSR_THRE)) {
        __asm__ __volatile__("" ::: "memory");
    }
    _uart_outb(SOMA_UART_THR, (unsigned char)c);
}

// ============================================================
// soma_uart_has_data — check if data is available to read
// ============================================================
static inline int soma_uart_has_data(void) {
    if (!g_soma_uart_enable) return 0;
    return (_uart_inb(SOMA_UART_LSR) & SOMA_UART_LSR_DR);
}

// ============================================================
// soma_uart_getc — receive one byte (busy-wait)
// ============================================================
static inline char soma_uart_getc(void) {
    if (!g_soma_uart_enable) return 0;
    while (!soma_uart_has_data()) {
        __asm__ __volatile__("pause");
    }
    return (char)_uart_inb(SOMA_UART_PORT);
}

// ============================================================
// soma_uart_puts — send null-terminated string (with \r\n end)
// ============================================================

static inline void soma_uart_puts(const char *s) {
    if (!g_soma_uart_enable || !s) return;
    while (*s) soma_uart_putc(*s++);
    soma_uart_putc('\r');
    soma_uart_putc('\n');
}

// ============================================================
// _uart_putu — emit unsigned int decimal (helper)
// ============================================================

static inline void _uart_putu(unsigned int v) {
    char buf[12]; int n = 0;
    if (v == 0) { soma_uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n-- > 0) soma_uart_putc(buf[n + 1]); // n already decremented once
}

// Proper version (n is correct after loop)
static inline void _uart_putu_v2(unsigned int v) {
    char buf[12]; int n = 0;
    if (v == 0) { soma_uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    for (int i = n - 1; i >= 0; i--) soma_uart_putc(buf[i]);
}

static inline void _uart_putx8(unsigned int v) {
    const char *hex = "0123456789ABCDEF";
    soma_uart_putc('0'); soma_uart_putc('x');
    soma_uart_putc(hex[(v>>28)&0xF]); soma_uart_putc(hex[(v>>24)&0xF]);
    soma_uart_putc(hex[(v>>20)&0xF]); soma_uart_putc(hex[(v>>16)&0xF]);
    soma_uart_putc(hex[(v>>12)&0xF]); soma_uart_putc(hex[(v>>8)&0xF]);
    soma_uart_putc(hex[(v>>4)&0xF]);  soma_uart_putc(hex[v&0xF]);
}

// ============================================================
// Event emitters — one function per event kind
// ============================================================

// Low 32 bits of RDTSC as lightweight timestamp
static inline unsigned int _uart_ts(void) {
    unsigned int lo;
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("lfence\nrdtsc" : "=a"(lo) :: "edx", "memory");
#else
    lo = 0;
#endif
    return lo;
}

static inline void soma_uart_emit_boot(int verbose) {
    if (!g_soma_uart_enable) return;
    const char *pfx = "[oo-event] kind=boot ts=";
    while (*pfx) soma_uart_putc(*pfx++);
    _uart_putu_v2(_uart_ts());
    if (verbose) { const char *v = " verbose=1"; while (*v) soma_uart_putc(*v++); }
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_gen(unsigned int turn, unsigned int tokens,
                                       unsigned int tok_s, unsigned int prompt_hash) {
    if (!g_soma_uart_enable) return;
    const char *p = "[oo-event] kind=gen ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " turn="; while (*p) soma_uart_putc(*p++); _uart_putu_v2(turn);
    p = " tokens="; while (*p) soma_uart_putc(*p++); _uart_putu_v2(tokens);
    p = " tok_s="; while (*p) soma_uart_putc(*p++); _uart_putu_v2(tok_s);
    p = " ph="; while (*p) soma_uart_putc(*p++); _uart_putx8(prompt_hash);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_halt(const char *reason) {
    if (!g_soma_uart_enable || !reason) return;
    const char *p = "[oo-event] kind=halt ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " reason="; while (*p) soma_uart_putc(*p++);
    while (*reason) soma_uart_putc(*reason++);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_warden(int level) {
    if (!g_soma_uart_enable) return;
    const char *p = "[oo-event] kind=warden ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " level="; while (*p) soma_uart_putc(*p++);
    soma_uart_putc('0' + (level & 3));
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_entropy(int rdrand_ok, unsigned int seed) {
    if (!g_soma_uart_enable) return;
    const char *p = "[oo-event] kind=entropy ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " rdrand="; while (*p) soma_uart_putc(*p++);
    p = rdrand_ok ? "hw" : "sw"; while (*p) soma_uart_putc(*p++);
    p = " seed="; while (*p) soma_uart_putc(*p++); _uart_putx8(seed);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_dna(unsigned int generation) {
    if (!g_soma_uart_enable) return;
    const char *p = "[oo-event] kind=dna ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " gen="; while (*p) soma_uart_putc(*p++); _uart_putu_v2(generation);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_heartbeat(unsigned int total_tokens, unsigned int free_mb) {
    if (!g_soma_uart_enable) return;
    const char *p = "[oo-event] kind=heartbeat ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " tok_total="; while (*p) soma_uart_putc(*p++); _uart_putu_v2(total_tokens);
    p = " mem_mb="; while (*p) soma_uart_putc(*p++); _uart_putu_v2(free_mb);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_repl(const char *cmd_word) {
    if (!g_soma_uart_enable || !cmd_word) return;
    const char *p = "[oo-event] kind=repl ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " cmd="; while (*p) soma_uart_putc(*p++);
    // First word only (up to 32 chars)
    int i = 0;
    while (cmd_word[i] && cmd_word[i] != ' ' && i < 32) soma_uart_putc(cmd_word[i++]);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}

static inline void soma_uart_emit_error(const char *msg) {
    if (!g_soma_uart_enable || !msg) return;
    const char *p = "[oo-event] kind=error ts="; while (*p) soma_uart_putc(*p++);
    _uart_putu_v2(_uart_ts());
    p = " msg="; while (*p) soma_uart_putc(*p++);
    int i = 0;
    while (msg[i] && i < 48) soma_uart_putc(msg[i++]);
    soma_uart_putc('\r'); soma_uart_putc('\n');
}
