/* oo_organ_bus.c — Biological organ → united_bus wiring
 *
 * Connects organ events to the united_bus IPC ring buffer.
 * Bridges:
 *   keyboard ISR (oo_irq) → SENSORY → CORTEX  (RED globule)
 *   scheduler state change → VITAL → BROADCAST (YELLOW globule)
 *   bot threat detect      → IMMUNE → BROADCAST (WHITE globule)
 *   thermal burn           → VITAL → REFLEX    (YELLOW globule)
 *   evolution mutation     → EVOLUTION → CORTEX (RED globule)
 */

#include "oo_organ_bus.h"
#include "oo_irq.h"
#include "oo_thermal.h"

/* Pull in united_bus types via its header */
#include "../../../united-baremetal/include/united_bus.h"

/* Pull in homeostasis state type from kernel-baremetal */
#include "../../../kernel-baremetal/include/oo_scheduler.h"

/* Forward declarations from organ .o files (linked via OO_ORGAN_OBJS) */

/* Forward from kernel-baremetal scheduler */
extern void oo_scheduler_init(void);
extern oo_homeostasis_state_t oo_scheduler_get_state(void);

/* Forward from sense-baremetal */
extern void sense_init(void);
extern void sense_transduce_keystroke(UINT8 scancode);

/* Forward from reflex-baremetal */
extern void reflex_init(void);
extern void reflex_on_thermal_burn(void);

/* Forward from bot-baremetal */
extern void bot_init(void);
extern UINT8 bot_get_threat_level(void);    /* 0 = none, 255 = critical */

/* ─── Static payload buffers ──────────────────────────────────────────── */
static UINT8 _kbd_buf[4];
static UINT8 _sched_buf[4];
static UINT8 _threat_buf[4];
static UINT8 _thermal_buf[4];

static UINT32 _gid = 1;
static UINT8  _last_sched_state = 0xFF;

/* ─── Pump helper — builds globule_t and calls united_bus_pump ────────── */
static int _pump(globule_type_t type, UINT8 src, UINT8 dst,
                 void *payload, UINT32 size) {
    globule_t g;
    g.globule_id   = _gid++;
    g.type         = type;
    g.source_organ = src;
    g.target_organ = dst;
    g.payload_addr = payload;
    g.payload_size = size;
    return united_bus_pump(g);
}

/* ─── Public: init all organs ─────────────────────────────────────────── */
void oo_organ_bus_init(void) {
    united_bus_init();
    sense_init();
    reflex_init();
}

/* ─── Public: periodic tick ────────────────────────────────────────────── */
void oo_organ_bus_tick(void) {
    /* 1. Keyboard → SENSORY RED → CORTEX */
    int ch = oo_kbd_getchar();
    if (ch > 0) {
        _kbd_buf[0] = (UINT8)ch;
        sense_transduce_keystroke((UINT8)ch);
        _pump(GLOBULE_RED, OO_BUS_ORGAN_SENSORY,
              OO_BUS_ORGAN_CORTEX, _kbd_buf, 1);
    }

    /* 2. Scheduler state → VITAL YELLOW → BROADCAST */
    UINT8 sched_st = (UINT8)oo_scheduler_get_state();
    if (sched_st != _last_sched_state) {
        _last_sched_state = sched_st;
        _sched_buf[0] = sched_st;
        _pump(GLOBULE_YELLOW, OO_BUS_ORGAN_VITAL,
              OO_BUS_BROADCAST, _sched_buf, 1);
    }

    /* 3. Bot threat → IMMUNE WHITE → BROADCAST */
    UINT8 threat = bot_get_threat_level();
    if (threat > 0) {
        _threat_buf[0] = threat;
        _pump(GLOBULE_WHITE, OO_BUS_ORGAN_IMMUNE,
              OO_BUS_BROADCAST, _threat_buf, 1);
    }

    /* 4. Thermal → VITAL YELLOW → REFLEX */
    oo_thermal_status_t ts;
    if (oo_thermal_read(&ts) == 0 && ts.throttle) {
        _thermal_buf[0] = (UINT8)ts.temperature_C;
        _pump(GLOBULE_YELLOW, OO_BUS_ORGAN_VITAL,
              OO_BUS_ORGAN_REFLEX, _thermal_buf, 1);
        if (ts.emergency) reflex_on_thermal_burn();
    }
}

/* ─── Public: emit helpers ────────────────────────────────────────────── */
void oo_bus_emit_keyboard(UINT8 ch) {
    _kbd_buf[0] = ch;
    _pump(GLOBULE_RED, OO_BUS_ORGAN_SENSORY, OO_BUS_ORGAN_CORTEX, _kbd_buf, 1);
}

void oo_bus_emit_scheduler_state(UINT8 state) {
    _sched_buf[0] = state;
    _pump(GLOBULE_YELLOW, OO_BUS_ORGAN_VITAL, OO_BUS_BROADCAST, _sched_buf, 1);
}

void oo_bus_emit_threat(UINT8 threat_level) {
    _threat_buf[0] = threat_level;
    _pump(GLOBULE_WHITE, OO_BUS_ORGAN_IMMUNE, OO_BUS_BROADCAST, _threat_buf, 1);
}

void oo_bus_emit_reflex(UINT8 irq) {
    _thermal_buf[0] = irq;
    _pump(GLOBULE_YELLOW, OO_BUS_ORGAN_VITAL, OO_BUS_ORGAN_REFLEX, _thermal_buf, 1);
}

int oo_bus_poll_cortex(void) {
    globule_t buf[4];
    int n = united_bus_absorb(OO_BUS_ORGAN_CORTEX, buf, 4);
    if (n > 0 && buf[0].payload_addr)
        return (int)*(UINT8 *)buf[0].payload_addr;
    return -1;
}

/* ─── Print + REPL ───────────────────────────────────────────────────────── */
void oo_organ_bus_print(void) {
    Print(L"\r\n  [Organ Bus Status]\r\n");
    Print(L"  Organs: CORTEX(0) IMMUNE(1) SENSORY(2) VITAL(3) REFLEX(4) EVOL(5)\r\n");
    Print(L"  Globule types: RED=data WHITE=immunity YELLOW=energy/sched\r\n");
    Print(L"  Use /organ_tick to run one bus cycle\r\n");
    Print(L"\r\n");
}

static int _organ_cmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

int oo_organ_bus_repl_cmd(const char *cmd) {
    if (!cmd) return 0;
    if (_organ_cmp(cmd, "/organ_status", 13) == 0) {
        oo_organ_bus_print(); return 1;
    }
    if (_organ_cmp(cmd, "/organ_tick", 11) == 0) {
        oo_organ_bus_tick();
        Print(L"[organ] Bus tick done\r\n"); return 1;
    }
    if (_organ_cmp(cmd, "/organ_poll", 11) == 0) {
        int ch = oo_bus_poll_cortex();
        if (ch >= 0) Print(L"[organ] CORTEX poll: 0x%02x\r\n", (UINT32)ch);
        else         Print(L"[organ] CORTEX queue empty\r\n");
        return 1;
    }
    if (_organ_cmp(cmd, "/organ_emit_kbd ", 16) == 0) {
        UINT8 ch = (UINT8)*(cmd + 16);
        oo_bus_emit_keyboard(ch);
        Print(L"[organ] Emitted kbd 0x%02x → SENSORY→CORTEX\r\n", (UINT32)ch);
        return 1;
    }
    return 0;
}

