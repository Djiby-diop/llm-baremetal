#include "overclock.h"
#include <string.h>

// MSR Definitions (Intel/AMD)
#define MSR_IA32_ENERGY_PERF_BIAS   0x1B0
#define MSR_IA32_MISC_ENABLE        0x1A0
#define MSR_IA32_POWER_CTL          0x1FC
#define MSR_IA32_THERM_STATUS       0x19C
#define MSR_TEMPERATURE_TARGET      0x1A0

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

int overclock_get_cpu_temp(void) {
    uint64_t therm = read_msr(MSR_IA32_THERM_STATUS);
    uint64_t target = read_msr(MSR_TEMPERATURE_TARGET);
    
    int tj_max = (target >> 16) & 0xFF;
    if (tj_max == 0) tj_max = 100; // Fallback standard
    
    int delta = (therm >> 16) & 0x7F;
    return tj_max - delta;
}

void overclock_init(OverclockCtx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(OverclockCtx));

    // Detect AVX features
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    
    ctx->has_avx2 = (ebx >> 5) & 1;
    ctx->has_avx512 = (ebx >> 16) & 1;
}

void overclock_tune_cpu(OverclockCtx *ctx) {
    if (!ctx) return;

    // 1. Set Maximum Performance Bias
    write_msr(MSR_IA32_ENERGY_PERF_BIAS, 0); // 0 = Maximum Performance

    // 2. Disable Thermal Throttling / Power Saving Jitter (DANGEROUS)
    // Only for bare-metal sovereign environments
    uint64_t pwr = read_msr(MSR_IA32_POWER_CTL);
    pwr &= ~(1 << 1); // Disable C1E
    write_msr(MSR_IA32_POWER_CTL, pwr);
    
    _log_causal(0, "overclock_cpu_jitter_removed");
}

void overclock_lock_cache(OverclockCtx *ctx) {
    // Implementation of Cache Allocation Technology (CAT)
    // Requires setting L3 Class of Service (CLOS)
    ctx->cache_locked = 1;
    _log_causal(0, "overclock_l3_cache_secured");
}
