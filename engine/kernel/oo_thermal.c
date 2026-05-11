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
