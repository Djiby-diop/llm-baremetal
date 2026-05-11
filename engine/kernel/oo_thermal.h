#pragma once
/* oo_thermal.h — CPU thermal monitoring via Intel MSR 0x19C
 * Works post-ExitBootServices on real Intel hardware.
 * Reads IA32_THERM_STATUS (MSR 0x19C) + IA32_TEMPERATURE_TARGET (0x1A2).
 * No dependency on oo_acpi — pairs with drivers/oo_acpi.h for shutdown.
 */

#include <efi.h>
#include <efilib.h>

#define OO_THERMAL_WARN      75   /* log warning */
#define OO_THERMAL_CRITICAL  90   /* throttle inference */
#define OO_THERMAL_SHUTDOWN  98   /* emergency halt via ACPI S5 */

typedef struct {
    UINT32 temperature_C;   /* 0 = MSR not available */
    UINT8  throttle;        /* 1 = slow inference */
    UINT8  emergency;       /* 1 = must shut down immediately */
} oo_thermal_status_t;

int  oo_thermal_read(oo_thermal_status_t *s);   /* read MSR; returns 0 or -errno */
void oo_thermal_check_and_act(void);            /* auto-shutdown if critical */
