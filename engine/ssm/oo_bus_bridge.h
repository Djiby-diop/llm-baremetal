// oo_bus_bridge.h — OO Bus Bridge (Phase J)
//
// Bridges bare-metal SomaMind events to the oo-host file bus via UART.
// Each significant engine event is serialized as a JSON BusMessage line
// that oo-host's `bus uart-relay` command can ingest directly.
//
// Message format (JSON, one line):
//   {"msg_id":"<uuid-lite>","from":"oo-kernel","to":null,
//    "kind":"<kind>","payload":"<kv>","ts_epoch_s":0,"reply_to":null}
//
// Kinds emitted:
//   heartbeat    — periodic alive signal (mode=sovereign tok_total=N mem_mb=N)
//   warden_alert — D+ verdict change (verdict=QUARANTINE reason=0x03)
//   dplus_event  — D+ gate evaluation (verdict=ALLOW pressure=N)
//   uart_event   — raw [oo-event] forwarded (kind=<k> ts=N ...)
//   goal_sync    — warden EMERGENCY: goal=halt_inference
//
// Freestanding C11 — no libc, no malloc. Header-only (static/inline).
// Depends on: soma_uart.h (must be included first)

#pragma once

#include "soma_uart.h"
#include "oo_dplus_gate.h"

// ============================================================
// Tiny UUID-lite (32 hex chars from RDTSC + counter, no dashes)
// ============================================================

static unsigned int _bus_bridge_counter = 0;

static void _bridge_uuid(char *out32) {
    // 8 hex from RDTSC lo + 8 from RDTSC hi + 8 from counter + 8 zeros
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    _bus_bridge_counter++;
    unsigned int vals[4] = { lo, hi, _bus_bridge_counter, 0x00B51D };
    const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 4; i++) {
        unsigned int v = vals[i];
        for (int j = 7; j >= 0; j--) {
            out32[i * 8 + j] = hex[v & 0xf];
            v >>= 4;
        }
    }
}

// ============================================================
// JSON string escaper (minimal — replaces " and \ only)
// ============================================================

static int _bridge_json_puts(char *buf, int pos, int cap, const char *s) {
    for (int i = 0; s[i] && pos < cap - 2; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            if (pos < cap - 2) buf[pos++] = '\\';
        }
        buf[pos++] = s[i];
    }
    return pos;
}

static int _bridge_itoa(int v, char *buf, int pos, int cap) {
    if (pos >= cap - 2) return pos;
    if (v < 0) { buf[pos++] = '-'; v = -v; }
    if (v == 0) { buf[pos++] = '0'; return pos; }
    char tmp[12]; int n = 0;
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = n - 1; i >= 0 && pos < cap - 1; i--) buf[pos++] = tmp[i];
    return pos;
}

// ============================================================
// Core emitter: build and send one BusMessage JSON line via UART
// kind_str: "heartbeat" / "warden_alert" / "dplus_event" / "goal_sync"
// payload:  KV string e.g. "verdict=ALLOW pressure=0"
// ============================================================

static void oo_bus_bridge_emit(const char *kind_str, const char *payload) {
    if (!g_soma_uart_ready || !g_soma_uart_enable) return;

    char buf[384];
    int pos = 0;
    char uuid[33]; uuid[32] = 0;
    _bridge_uuid(uuid);

    // {"msg_id":"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX","from":"oo-kernel","to":null,
    const char *p1 = "{\"msg_id\":\"";
    for (int i = 0; p1[i] && pos < 380; i++) buf[pos++] = p1[i];
    for (int i = 0; uuid[i] && pos < 380; i++) buf[pos++] = uuid[i];
    const char *p2 = "\",\"from\":\"oo-kernel\",\"to\":null,\"kind\":\"";
    for (int i = 0; p2[i] && pos < 380; i++) buf[pos++] = p2[i];
    pos = _bridge_json_puts(buf, pos, 380, kind_str);
    const char *p3 = "\",\"payload\":\"";
    for (int i = 0; p3[i] && pos < 380; i++) buf[pos++] = p3[i];
    pos = _bridge_json_puts(buf, pos, 380, payload);
    const char *p4 = "\",\"ts_epoch_s\":0,\"reply_to\":null}";
    for (int i = 0; p4[i] && pos < 380; i++) buf[pos++] = p4[i];
    if (pos < 382) { buf[pos++] = '\r'; buf[pos++] = '\n'; }
    buf[pos] = '\0';
    soma_uart_puts(buf);
}

// ============================================================
// Typed emitters
// ============================================================

// Periodic heartbeat — call every N turns (16 recommended)
static void oo_bus_emit_heartbeat(int tok_total, int mem_mb, const char *mode) {
    // "mode=sovereign tok_total=N mem_mb=N"
    char pay[128];
    int pos = 0;
    const char *m1 = "mode="; for (int i=0; m1[i]&&pos<120; i++) pay[pos++]=m1[i];
    for (int i=0; mode[i]&&pos<120; i++) pay[pos++]=mode[i];
    const char *m2 = " tok_total="; for (int i=0; m2[i]&&pos<120; i++) pay[pos++]=m2[i];
    pos = _bridge_itoa(tok_total, pay, pos, 120);
    const char *m3 = " mem_mb="; for (int i=0; m3[i]&&pos<120; i++) pay[pos++]=m3[i];
    pos = _bridge_itoa(mem_mb, pay, pos, 120);
    pay[pos] = '\0';
    oo_bus_bridge_emit("heartbeat", pay);
}

// D+ verdict change alert
static void oo_bus_emit_warden_alert(DPlusVerdict verdict, int reason_flags, int pressure) {
    // "verdict=QUARANTINE reason=0x03 pressure=2"
    char pay[128];
    int pos = 0;
    const char *v1 = "verdict="; for (int i=0; v1[i]&&pos<120; i++) pay[pos++]=v1[i];
    const char *vn = g_dplus_verdict_name[verdict > DPLUS_EMERGENCY ? 0 : verdict];
    for (int i=0; vn[i]&&pos<120; i++) pay[pos++]=vn[i];
    const char *v2 = " reason=0x"; for (int i=0; v2[i]&&pos<120; i++) pay[pos++]=v2[i];
    char hex[] = "0123456789abcdef";
    if (pos < 118) pay[pos++] = hex[(reason_flags >> 4) & 0xf];
    if (pos < 119) pay[pos++] = hex[reason_flags & 0xf];
    const char *v3 = " pressure="; for (int i=0; v3[i]&&pos<120; i++) pay[pos++]=v3[i];
    pos = _bridge_itoa(pressure, pay, pos, 120);
    pay[pos] = '\0';
    oo_bus_bridge_emit("warden_alert", pay);
}

// EMERGENCY: goal_sync halt signal to oo-host
static void oo_bus_emit_goal_halt(const char *reason) {
    // "goal=halt_inference reason=dplus_emergency"
    char pay[128];
    int pos = 0;
    const char *g1 = "goal=halt_inference reason=";
    for (int i=0; g1[i]&&pos<120; i++) pay[pos++]=g1[i];
    for (int i=0; reason[i]&&pos<120; i++) pay[pos++]=reason[i];
    pay[pos] = '\0';
    oo_bus_bridge_emit("goal_sync", pay);
}

// DNA evolution notification
static void oo_bus_emit_dna_update(int generation, int tok_total) {
    char pay[128];
    int pos = 0;
    const char *d1 = "gen="; for (int i=0; d1[i]&&pos<120; i++) pay[pos++]=d1[i];
    pos = _bridge_itoa(generation, pay, pos, 120);
    const char *d2 = " tok_total="; for (int i=0; d2[i]&&pos<120; i++) pay[pos++]=d2[i];
    pos = _bridge_itoa(tok_total, pay, pos, 120);
    pay[pos] = '\0';
    oo_bus_bridge_emit("dplus_event", pay);
}
