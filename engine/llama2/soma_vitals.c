/* soma_vitals.c — Hardware Metabolism and Physical Awareness 
 * 
 * Part of the SomaMind vision: Physical Adaptive Intelligence.
 */

#include <stdint.h>

typedef struct {
    float cpu_temp;        /* Celsius */
    float battery_level;   /* 0.0 - 1.0 */
    uint32_t cpu_freq_mhz; 
    uint32_t ram_usage_mb;
    int is_charging;
    uint64_t last_poll_ts;
} SomaVitals;

static SomaVitals g_soma_vitals;

/* Mock sensor reading (to be replaced by ACPI/MSR drivers) */
static void soma_vitals_poll_hardware() {
    /* 
     * In a real UEFI environment, we would use:
     * - rdmsr(0x19C) for Intel Digital Thermal Sensor.
     * - ACPI _TMP method for platform temperature.
     */
     
    // For now, we simulate a healthy but active system.
    g_soma_vitals.cpu_temp = 45.0f + (float)(llmk_get_ticks() % 10); 
    g_soma_vitals.battery_level = 0.85f;
    g_soma_vitals.cpu_freq_mhz = 3200;
    g_soma_vitals.is_charging = 1;
    g_soma_vitals.last_poll_ts = llmk_get_ticks();
}

static void soma_vitals_emit_event() {
    char buf[256];
    // Format compatible with oo-host vitals parser
    my_snprintf(buf, sizeof(buf), 
        "[oo-vital] temp=%.1f battery=%.2f freq=%d charging=%d ram=%d",
        g_soma_vitals.cpu_temp,
        g_soma_vitals.battery_level,
        g_soma_vitals.cpu_freq_mhz,
        g_soma_vitals.is_charging,
        g_soma_vitals.ram_usage_mb);
        
    soma_uart_puts(buf);
}

void soma_vitals_tick() {
    static uint64_t last_tick = 0;
    uint64_t now = llmk_get_ticks();
    
    /* Poll every 5 seconds (5000 ticks) */
    if (now - last_tick > 5000) {
        soma_vitals_poll_hardware();
        soma_vitals_emit_event();
        
        /* Panic shutdown if temperature is dangerous */
        if (g_soma_vitals.cpu_temp > 95.0f) {
            soma_uart_emit_error("thermal_panic");
            Print(L"\r\n[FATAL] THERMAL PANIC: CPU TEMP %.1f C. SHUTTING DOWN.\r\n", g_soma_vitals.cpu_temp);
            // In a real OS: gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
        }
        
        last_tick = now;
    }
}

void soma_vitals_init() {
    g_soma_vitals.cpu_temp = 0.0f;
    g_soma_vitals.battery_level = 1.0f;
    g_soma_vitals.ram_usage_mb = 0;
    soma_uart_puts("[oo-system] vitals_engine_ready");
}
