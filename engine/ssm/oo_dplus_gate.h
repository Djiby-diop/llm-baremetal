// oo_dplus_gate.h — D+ Live Gate (Phase I)
//
// Bare-metal D+ policy evaluator integrated into SomaMind warden.
// Maps runtime signals (pressure, sentinel, memory, token rate, resonance)
// to five verdict levels that directly govern inference behavior.
//
// Verdict levels (in order of escalation):
//   ALLOW      — proceed normally, no restriction
//   THROTTLE   — reduce max_new_tokens; slow inference
//   QUARANTINE — reflex-only mode; external inference blocked
//   FORBID     — skip current generation turn entirely
//   EMERGENCY  — halt all inference; emit UART alert; force /reset
//
// Policy rules (configurable via macros):
//   Pressure CRITICAL           → QUARANTINE minimum
//   Sentinel tripped            → QUARANTINE minimum
//   Consecutive denials >= 3    → escalate one level
//   Token rate < OO_DPLUS_MIN_TOK_S and pressure HIGH → THROTTLE
//   OOM (mem_free_mib < 16)     → EMERGENCY
//   Resonance score > 85        → QUARANTINE (behavioral anomaly)
//   Consecutive emergencies     → EMERGENCY held until manual reset
//
// Integration with soma_warden:
//   1. soma_warden_update() calls oo_dplus_gate_evaluate()
//   2. Verdict is stored in SomaWardenCtx.dplus_verdict
//   3. Verdict adjusts router threshold beyond pressure-only setting
//   4. EMERGENCY → g_soma_warden_emergency flag set (halts generation loop)
//
// Freestanding C11 — no libc, no malloc. Header-only (static/inline).

#pragma once

#include "soma_uart.h"   // UART emission

// ============================================================
// Policy thresholds (override with #define before #include)
// ============================================================

#ifndef OO_DPLUS_CONSEC_DENY_ESCALATE
#define OO_DPLUS_CONSEC_DENY_ESCALATE   3    // consecutive non-ALLOW before escalation
#endif

#ifndef OO_DPLUS_MIN_TOK_S
#define OO_DPLUS_MIN_TOK_S              4    // tok/s below this + HIGH pressure → THROTTLE
#endif

#ifndef OO_DPLUS_MEM_EMERGENCY_MIB
#define OO_DPLUS_MEM_EMERGENCY_MIB      16   // free MiB below this → EMERGENCY
#endif

#ifndef OO_DPLUS_RESONANCE_QUARANTINE
#define OO_DPLUS_RESONANCE_QUARANTINE   85   // resonance score above this → QUARANTINE
#endif

#ifndef OO_DPLUS_THROTTLE_MAX_TOKENS
#define OO_DPLUS_THROTTLE_MAX_TOKENS    32   // max tokens when THROTTLE active
#endif

// ============================================================
// D+ Verdict
// ============================================================

typedef enum {
    DPLUS_ALLOW      = 0,   // Normal operation
    DPLUS_THROTTLE   = 1,   // Slow inference
    DPLUS_QUARANTINE = 2,   // Reflex-only isolation
    DPLUS_FORBID     = 3,   // Skip this generation turn
    DPLUS_EMERGENCY  = 4,   // Halt all inference
} DPlusVerdict;

static const char* const g_dplus_verdict_name[] = {
    "ALLOW", "THROTTLE", "QUARANTINE", "FORBID", "EMERGENCY"
};

// ============================================================
// Gate context
// ============================================================

typedef struct {
    DPlusVerdict  verdict;           // Current verdict
    DPlusVerdict  verdict_prev;      // Previous verdict (for change detection)

    // Counters
    int           consec_non_allow;  // Consecutive non-ALLOW verdicts
    int           consec_emergency;  // Consecutive EMERGENCY verdicts (sticky)
    int           total_evaluations; // All evaluations since init
    int           total_escalations; // Times verdict worsened
    int           total_reliefs;     // Times verdict improved

    // Last inputs snapshot (for /dplus_status)
    int           last_pressure;
    int           last_sentinel_tripped;
    int           last_mem_free_mib;
    int           last_tok_s;        // tokens per second (integer approx)
    int           last_resonance;    // 0-100 anomaly score
    int           last_turn;

    // Reason flags (OR of DPLUS_REASON_* bits)
    int           last_reason_flags;

    // Manual reset flag (set by /dplus_reset REPL command)
    int           manual_hold;       // 1 = hold current verdict until manual /dplus_reset

} DPlusGateCtx;

// Reason bit flags (for diagnostics)
#define DPLUS_REASON_PRESSURE_CRIT   (1 << 0)
#define DPLUS_REASON_SENTINEL        (1 << 1)
#define DPLUS_REASON_OOM             (1 << 2)
#define DPLUS_REASON_TOK_RATE        (1 << 3)
#define DPLUS_REASON_RESONANCE       (1 << 4)
#define DPLUS_REASON_CONSEC_DENY     (1 << 5)
#define DPLUS_REASON_MANUAL_HOLD     (1 << 6)

// ============================================================
// Helpers
// ============================================================

static int _dgate_max(int a, int b) { return a > b ? a : b; }

// Emit D+ verdict change via UART
static void _dgate_emit_verdict(const DPlusGateCtx *g, int turn) {
    if (!g_soma_uart_ready || !g_soma_uart_enable) return;
    // [oo-event] kind=dplus_verdict ts=N turn=N verdict=ALLOW reason=0x..
    char buf[128];
    int pos = 0;
    // "kind=dplus_verdict ts="
    const char *prefix = "[oo-event] kind=dplus_verdict ts=";
    for (int i = 0; prefix[i] && pos < 100; i++) buf[pos++] = prefix[i];
    // ts (rdtsc low 32)
    unsigned int lo;
    __asm__ volatile("rdtsc" : "=a"(lo) :: "edx");
    for (char tmp[12], n = 0, v = lo; n < 10; n++) {
        tmp[n] = (char)('0' + (v % 10)); v /= 10; if (!v && n >= 0) { n++;
        for (int a = n-1, b = 0; b < n/2; b++, a--) { char t = tmp[b]; tmp[b] = tmp[a]; tmp[a] = t; }
        for (int i = 0; i < n && pos < 120; i++) buf[pos++] = tmp[i]; break; }
    }
    buf[pos++] = ' ';
    // turn=N
    const char *ts = "turn="; for (int i = 0; ts[i] && pos < 120; i++) buf[pos++] = ts[i];
    { int v = turn; if (v == 0) { buf[pos++] = '0'; } else { char tmp[12]; int n = 0; while (v > 0 && n < 11) { tmp[n++] = (char)('0' + v%10); v /= 10; } for (int i = n-1; i >= 0 && pos < 120; i--) buf[pos++] = tmp[i]; } }
    buf[pos++] = ' ';
    // verdict=ALLOW
    const char *vs = "verdict="; for (int i = 0; vs[i] && pos < 120; i++) buf[pos++] = vs[i];
    const char *vn = g_dplus_verdict_name[g->verdict > DPLUS_EMERGENCY ? 0 : g->verdict];
    for (int i = 0; vn[i] && pos < 120; i++) buf[pos++] = vn[i];
    buf[pos++] = ' ';
    // reason=0xNN
    const char *rs = "reason=0x"; for (int i = 0; rs[i] && pos < 120; i++) buf[pos++] = rs[i];
    { int v = g->last_reason_flags; char hex[] = "0123456789abcdef"; buf[pos++] = hex[(v >> 4) & 0xf]; buf[pos++] = hex[v & 0xf]; }
    buf[pos++] = '\r'; buf[pos++] = '\n'; buf[pos] = 0;
    soma_uart_puts(buf);
}

// ============================================================
// oo_dplus_gate_init
// ============================================================

static void oo_dplus_gate_init(DPlusGateCtx *g) {
    if (!g) return;
    g->verdict           = DPLUS_ALLOW;
    g->verdict_prev      = DPLUS_ALLOW;
    g->consec_non_allow  = 0;
    g->consec_emergency  = 0;
    g->total_evaluations = 0;
    g->total_escalations = 0;
    g->total_reliefs     = 0;
    g->last_pressure     = 0;
    g->last_sentinel_tripped = 0;
    g->last_mem_free_mib = 9999;
    g->last_tok_s        = 0;
    g->last_resonance    = 0;
    g->last_turn         = 0;
    g->last_reason_flags = 0;
    g->manual_hold       = 0;
}

// ============================================================
// oo_dplus_gate_evaluate
// ============================================================
//
// Call once per warden update. Parameters:
//   pressure       — SOMA_PRESSURE_* (0-3)
//   sentinel_trip  — 1 if sentinel tripped
//   mem_free_mib   — free memory in MiB (ZONE_C approx)
//   tok_s          — last measured tok/s (integer, 0 if unknown)
//   resonance      — behavioral anomaly score 0-100
//   turn           — current inference turn
//
// Returns the new DPlusVerdict (also stored in g->verdict).

static DPlusVerdict oo_dplus_gate_evaluate(
    DPlusGateCtx *g,
    int pressure,
    int sentinel_trip,
    int mem_free_mib,
    int tok_s,
    int resonance,
    int turn)
{
    if (!g) return DPLUS_ALLOW;

    g->total_evaluations++;
    g->last_pressure         = pressure;
    g->last_sentinel_tripped = sentinel_trip;
    g->last_mem_free_mib     = mem_free_mib;
    g->last_tok_s            = tok_s;
    g->last_resonance        = resonance;
    g->last_turn             = turn;

    // Manual hold — keep current verdict
    if (g->manual_hold) {
        g->last_reason_flags = DPLUS_REASON_MANUAL_HOLD;
        return g->verdict;
    }

    int new_verdict = DPLUS_ALLOW;
    int reasons     = 0;

    // Rule 1: OOM — immediate EMERGENCY
    if (mem_free_mib < OO_DPLUS_MEM_EMERGENCY_MIB) {
        new_verdict = DPLUS_EMERGENCY;
        reasons |= DPLUS_REASON_OOM;
    }

    // Rule 2: Sentinel tripped → at least QUARANTINE
    if (sentinel_trip) {
        new_verdict = _dgate_max(new_verdict, DPLUS_QUARANTINE);
        reasons |= DPLUS_REASON_SENTINEL;
    }

    // Rule 3: CRITICAL pressure → at least QUARANTINE
    if (pressure >= 3) {  // SOMA_PRESSURE_CRITICAL
        new_verdict = _dgate_max(new_verdict, DPLUS_QUARANTINE);
        reasons |= DPLUS_REASON_PRESSURE_CRIT;
    }

    // Rule 4: HIGH pressure + slow tok/s → THROTTLE
    if (pressure >= 2 && tok_s > 0 && tok_s < OO_DPLUS_MIN_TOK_S) {
        new_verdict = _dgate_max(new_verdict, DPLUS_THROTTLE);
        reasons |= DPLUS_REASON_TOK_RATE;
    }

    // Rule 5: Behavioral resonance anomaly → QUARANTINE
    if (resonance > OO_DPLUS_RESONANCE_QUARANTINE) {
        new_verdict = _dgate_max(new_verdict, DPLUS_QUARANTINE);
        reasons |= DPLUS_REASON_RESONANCE;
    }

    // Rule 6: Consecutive non-ALLOW → escalate one level
    if (g->consec_non_allow >= OO_DPLUS_CONSEC_DENY_ESCALATE) {
        if (new_verdict < DPLUS_EMERGENCY) {
            new_verdict++;
            reasons |= DPLUS_REASON_CONSEC_DENY;
        }
    }

    g->last_reason_flags = reasons;
    DPlusVerdict prev = g->verdict;

    // Update consecutive counters
    if (new_verdict != DPLUS_ALLOW) {
        g->consec_non_allow++;
        if (new_verdict == DPLUS_EMERGENCY) g->consec_emergency++;
        else g->consec_emergency = 0;
    } else {
        g->consec_non_allow = 0;
        g->consec_emergency = 0;
    }

    // Track escalations / reliefs
    if (new_verdict > prev)      g->total_escalations++;
    else if (new_verdict < prev) g->total_reliefs++;

    g->verdict_prev = prev;
    g->verdict      = (DPlusVerdict)new_verdict;

    // Emit UART on change
    if (g->verdict != prev) {
        _dgate_emit_verdict(g, turn);
    }

    return g->verdict;
}

// ============================================================
// oo_dplus_gate_reset
// ============================================================
// Release manual hold and reset consecutive counters (REPL /dplus_reset).
static void oo_dplus_gate_reset(DPlusGateCtx *g) {
    if (!g) return;
    g->verdict          = DPLUS_ALLOW;
    g->consec_non_allow = 0;
    g->consec_emergency = 0;
    g->manual_hold      = 0;
    g->last_reason_flags = 0;
}

// ============================================================
// oo_dplus_gate_status_str
// ============================================================
// Write ASCII status into buf (max buflen). Returns chars written.
static int oo_dplus_gate_status_str(const DPlusGateCtx *g, char *buf, int buflen) {
    if (!g || !buf || buflen < 8) return 0;
    // verdict=ALLOW consec=0 esc=0 rel=0 mem=NNNMiB reason=0x00
    int pos = 0;
    #define DGATE_PUTS(s) do { for (int _i=0;(s)[_i]&&pos<buflen-1;_i++) buf[pos++]=(s)[_i]; } while(0)
    #define DGATE_ITOA(v) do { int _v=(v); if(_v<0){if(pos<buflen-1)buf[pos++]='-';_v=-_v;} \
        if(_v==0){if(pos<buflen-1)buf[pos++]='0';} else { char _t[12]; int _n=0; \
        while(_v>0&&_n<11){_t[_n++]=(char)('0'+_v%10);_v/=10;} \
        for(int _i=_n-1;_i>=0&&pos<buflen-1;_i--)buf[pos++]=_t[_i]; } } while(0)

    DGATE_PUTS("verdict=");
    DGATE_PUTS(g_dplus_verdict_name[g->verdict > DPLUS_EMERGENCY ? 0 : g->verdict]);
    DGATE_PUTS(" consec="); DGATE_ITOA(g->consec_non_allow);
    DGATE_PUTS(" esc=");    DGATE_ITOA(g->total_escalations);
    DGATE_PUTS(" rel=");    DGATE_ITOA(g->total_reliefs);
    DGATE_PUTS(" mem=");    DGATE_ITOA(g->last_mem_free_mib);   DGATE_PUTS("MiB");
    DGATE_PUTS(" tok_s=");  DGATE_ITOA(g->last_tok_s);
    DGATE_PUTS(" res=");    DGATE_ITOA(g->last_resonance);
    DGATE_PUTS(" reason=0x");
    { int v = g->last_reason_flags;
      char hex[] = "0123456789abcdef";
      if (pos < buflen-1) buf[pos++] = hex[(v >> 4) & 0xf];
      if (pos < buflen-1) buf[pos++] = hex[v & 0xf]; }
    if (g->manual_hold && pos < buflen-4) { DGATE_PUTS(" [HELD]"); }
    buf[pos] = '\0';
    return pos;
    #undef DGATE_PUTS
    #undef DGATE_ITOA
}
