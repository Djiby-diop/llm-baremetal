/* oo_thermal.c — CPU thermal monitoring via Intel IA32_THERM_STATUS MSR
 * Uses oo_acpi_shutdown() from drivers/oo_acpi.h for emergency halt.
 */

#include "oo_thermal.h"

/* Forward declaration — defined in drivers/oo_acpi.c (unity-included later) */
extern void oo_acpi_shutdown(void);

static inline UINT64 _rdmsr_thermal(UINT32 msr) {
    UINT32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((UINT64)hi << 32) | lo;
}

int oo_thermal_read(oo_thermal_status_t *s) {
    if (!s) return -1;
    s->temperature_C = 0;
    s->throttle      = 0;
    s->emergency     = 0;

    /* IA32_TEMPERATURE_TARGET (0x1A2): TjMax in bits [23:16] */
    UINT64 target = _rdmsr_thermal(0x1A2);
    UINT32 tj_max = (UINT32)((target >> 16) & 0xFF);
    if (tj_max == 0) tj_max = 100;

    /* IA32_THERM_STATUS (0x19C): bit31=valid, bits[22:16]=digital readout */
    UINT64 therm = _rdmsr_thermal(0x19C);
    if (!(therm & (1ULL << 31))) return -2;   /* reading not valid */

    UINT32 readout = (UINT32)((therm >> 16) & 0x7F);
    UINT32 temp_c  = tj_max - readout;
    s->temperature_C = temp_c;

    if (temp_c >= OO_THERMAL_SHUTDOWN)      { s->emergency = 1; s->throttle = 1; }
    else if (temp_c >= OO_THERMAL_CRITICAL) { s->throttle = 1; }
    return 0;
}

void oo_thermal_check_and_act(void) {
    oo_thermal_status_t s;
    if (oo_thermal_read(&s) == 0 && s.emergency)
        oo_acpi_shutdown();   /* from drivers/oo_acpi.h — already included */
}

/* ─── Print + REPL ───────────────────────────────────────────────────────── */
void oo_thermal_print(const oo_thermal_status_t *s) {
    if (!s) return;
    Print(L"\r\n  [Thermal Status]\r\n");
    if (s->temperature_C == 0) {
        Print(L"  MSR not available (virtual machine or unsupported CPU)\r\n\r\n");
        return;
    }
    Print(L"  Temperature : %u C\r\n", s->temperature_C);
    Print(L"  Throttle    : %s\r\n", s->throttle  ? L"YES" : L"no");
    Print(L"  Emergency   : %s\r\n", s->emergency ? L"YES - CRITICAL" : L"no");
    Print(L"  Warn threshold  : %d C\r\n", OO_THERMAL_WARN);
    Print(L"  Critical        : %d C\r\n", OO_THERMAL_CRITICAL);
    Print(L"  Shutdown        : %d C\r\n", OO_THERMAL_SHUTDOWN);
    Print(L"\r\n");
}

static int _therm_cmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

int oo_thermal_repl_cmd(const char *cmd) {
    if (!cmd) return 0;
    if (_therm_cmp(cmd, "/thermal_status", 15) == 0) {
        oo_thermal_status_t s;
        int r = oo_thermal_read(&s);
        if (r < 0) Print(L"[thermal] MSR read failed (%d)\r\n", r);
        else oo_thermal_print(&s);
        return 1;
    }
    if (_therm_cmp(cmd, "/thermal_check", 14) == 0) {
        oo_thermal_check_and_act();
        Print(L"[thermal] Check done (no shutdown = safe)\r\n");
        return 1;
    }
    return 0;
}
