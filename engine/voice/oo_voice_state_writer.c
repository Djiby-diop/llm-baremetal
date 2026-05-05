// oo_voice_state_writer.c — Voice State JSON Emitter (UART Bridge)
//
// Emits a JSON blob on UART COM1 (38400 baud) that the Python preview
// (preview_hud.py) or a host-side forwarder can write to oo_voice_state.json.
//
// Format emitted per-event on UART:
//   OO_VOICE:{"emotion":"FOCUSED","vortex_speed_mul":1.0,"wake_pulse":0,
//              "glitch":0,"inference":true,"waveform":[...],"response":"..."}
//
// The host-side forwarder (tools/scripts/uart_bridge.py) strips the prefix
// and writes the JSON body to oo_voice_state.json for the HUD preview.
//
// Freestanding C11 — no libc, no malloc, UEFI Ring 0.

#include "oo_voice_state_writer.h"

// ── UART COM1 output (direct port I/O) ───────────────────────────────────────

static inline void _uart_outb(uint16_t port, uint8_t v) {
    __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(port));
}

static inline uint8_t _uart_inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

#define COM1_BASE       0x3F8
#define COM1_THR        (COM1_BASE + 0)
#define COM1_LSR        (COM1_BASE + 5)
#define COM1_LSR_THRE   0x20

static void _uart_putc(char c) {
    for (int i = 0; i < 10000; i++) {
        if (_uart_inb(COM1_LSR) & COM1_LSR_THRE) break;
    }
    _uart_outb(COM1_THR, (uint8_t)c);
}

static void _uart_puts(const char *s) {
    while (*s) _uart_putc(*s++);
}

static void _itoa_dec(int32_t n, char *buf, int buflen) {
    if (buflen < 2) { buf[0] = '0'; return; }
    if (n < 0) { buf[0] = '-'; _itoa_dec(-n, buf + 1, buflen - 1); return; }
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (n > 0 && i < 11) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (i > 0 && j < buflen - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void _emit_f10(uint32_t v10) {
    char buf[8];
    _itoa_dec((int32_t)(v10 / 10), buf, sizeof(buf));
    _uart_puts(buf);
    _uart_putc('.');
    _uart_putc('0' + (v10 % 10));
}

// ── Public API ────────────────────────────────────────────────────────────────

void oo_voice_state_emit(const OoVoiceStateEmit *e) {
    _uart_puts("OO_VOICE:{\"emotion\":\"");
    _uart_puts(e->emotion ? e->emotion : "FOCUSED");
    _uart_puts("\",\"vortex_speed_mul\":");
    _emit_f10(e->vortex_speed_mul_x10);
    _uart_puts(",\"wake_pulse\":");
    char nb[8];
    _itoa_dec(e->wake_pulse, nb, sizeof(nb));
    _uart_puts(nb);
    _uart_puts(",\"glitch\":");
    _itoa_dec(e->glitch, nb, sizeof(nb));
    _uart_puts(nb);
    _uart_puts(",\"inference\":");
    _uart_puts(e->inference ? "true" : "false");
    _uart_puts(",\"waveform\":[");
    for (int i = 0; i < 64; i++) {
        _itoa_dec(e->waveform[i], nb, sizeof(nb));
        _uart_puts(nb);
        if (i < 63) _uart_putc(',');
    }
    _uart_puts("]");
    if (e->response && e->response[0]) {
        _uart_puts(",\"response\":\"");
        const char *r = e->response;
        while (*r) {
            if (*r == '\\') { _uart_putc('\\'); _uart_putc('\\'); }
            else if (*r == '"') { _uart_putc('\\'); _uart_putc('"'); }
            else if (*r == '\n') { _uart_putc('\\'); _uart_putc('n'); }
            else _uart_putc(*r);
            r++;
        }
        _uart_putc('"');
    }
    _uart_puts("}\r\n");
}

void oo_voice_state_emit_simple(const char *emotion, int inference, int wake) {
    uint8_t silent[64];
    for (int i = 0; i < 64; i++) silent[i] = 128;
    OoVoiceStateEmit e = {
        .emotion              = emotion,
        .vortex_speed_mul_x10 = inference ? 25u : 10u,
        .wake_pulse           = (uint8_t)(wake ? 1 : 0),
        .glitch               = 0,
        .inference            = (uint8_t)(inference ? 1 : 0),
        .response             = (const char *)0,
    };
    for (int i = 0; i < 64; i++) e.waveform[i] = silent[i];
    oo_voice_state_emit(&e);
}
