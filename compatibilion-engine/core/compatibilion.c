#include "compatibilion.h"

// Simple memset for freestanding
static void compat_memset(void *dst, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)c;
}

static void compat_strcpy_cap(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = 0;
}

void compatibilion_init(CompatibilionEngine *e) {
    if (!e) return;
    e->mode = COMPATIBILION_MODE_ON;
    compat_memset(&e->caps, 0, sizeof(e->caps));
    e->probes_done = 0;
}

void compatibilion_set_mode(CompatibilionEngine *e, CompatibilionMode mode) {
    if (!e) return;
    e->mode = mode;
}

const char *compatibilion_mode_name_ascii(CompatibilionMode mode) {
    switch (mode) {
        case COMPATIBILION_MODE_OFF: return "off";
        case COMPATIBILION_MODE_ON:  return "on";
        default:                     return "?";
    }
}

const char *compatibilion_mem_tier_name_ascii(CompatibilionMemTier tier) {
    switch (tier) {
        case COMPAT_MEM_UNKNOWN: return "unknown";
        case COMPAT_MEM_LOW:     return "low (<256MB)";
        case COMPAT_MEM_MEDIUM:  return "medium (256MB-1GB)";
        case COMPAT_MEM_HIGH:    return "high (1GB-4GB)";
        case COMPAT_MEM_ULTRA:   return "ultra (>4GB)";
        default:                 return "?";
    }
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
// CPUID helper (freestanding)
static void compat_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(0)
    );
#endif
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static uint64_t compat_xgetbv(uint32_t xcr) {
    uint32_t lo = 0, hi = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__(
        "xgetbv"
        : "=a"(lo), "=d"(hi)
        : "c"(xcr)
    );
#endif
    return ((uint64_t)hi << 32) | lo;
}
#endif

void compatibilion_probe_cpu(CompatibilionEngine *e) {
    if (!e) return;
    e->caps.cpu_flags = 0;
    e->caps.cpu_vendor[0] = 0;
    e->caps.cpu_brand[0] = 0;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    uint32_t eax, ebx, ecx, edx;

    // Vendor string
    compat_cpuid(0, &eax, &ebx, &ecx, &edx);
    char *v = e->caps.cpu_vendor;
    *((uint32_t *)(v + 0)) = ebx;
    *((uint32_t *)(v + 4)) = edx;
    *((uint32_t *)(v + 8)) = ecx;
    v[12] = 0;

    // Feature flags (leaf 1)
    compat_cpuid(1, &eax, &ebx, &ecx, &edx);

    // SSE2 (edx bit 26)
    if (edx & (1u << 26)) e->caps.cpu_flags |= COMPAT_CPU_SSE2;
    // SSE4.1 (ecx bit 19)
    if (ecx & (1u << 19)) e->caps.cpu_flags |= COMPAT_CPU_SSE41;
    // AVX (ecx bit 28) + OSXSAVE (ecx bit 27)
    int has_osxsave = (ecx & (1u << 27)) != 0;
    int has_avx_bit = (ecx & (1u << 28)) != 0;
    int avx_ok = 0;
    if (has_osxsave && has_avx_bit) {
        uint64_t xcr0 = compat_xgetbv(0);
        if ((xcr0 & 0x6) == 0x6) {
            e->caps.cpu_flags |= COMPAT_CPU_AVX;
            avx_ok = 1;
        }
    }
    // FMA (ecx bit 12)
    if (avx_ok && (ecx & (1u << 12))) e->caps.cpu_flags |= COMPAT_CPU_FMA;

    // Extended features (leaf 7)
    compat_cpuid(7, &eax, &ebx, &ecx, &edx);
    // AVX2 (ebx bit 5)
    if (avx_ok && (ebx & (1u << 5))) e->caps.cpu_flags |= COMPAT_CPU_AVX2;
    // AVX-512F (ebx bit 16)
    if (avx_ok && (ebx & (1u << 16))) e->caps.cpu_flags |= COMPAT_CPU_AVX512F;

    // Brand string (leaves 0x80000002-0x80000004)
    compat_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t *brand = (uint32_t *)e->caps.cpu_brand;
        compat_cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        compat_cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        compat_cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        e->caps.cpu_brand[48] = 0;
    }
#endif

    e->probes_done++;
}

void compatibilion_set_platform(CompatibilionEngine *e, uint32_t flags) {
    if (!e) return;
    e->caps.platform_flags = flags;
}

void compatibilion_set_memory(CompatibilionEngine *e, uint64_t bytes) {
    if (!e) return;
    e->caps.mem_bytes = bytes;
    if (bytes < 256ULL * 1024 * 1024) {
        e->caps.mem_tier = COMPAT_MEM_LOW;
    } else if (bytes < 1024ULL * 1024 * 1024) {
        e->caps.mem_tier = COMPAT_MEM_MEDIUM;
    } else if (bytes < 4ULL * 1024 * 1024 * 1024) {
        e->caps.mem_tier = COMPAT_MEM_HIGH;
    } else {
        e->caps.mem_tier = COMPAT_MEM_ULTRA;
    }
}

void compatibilion_set_gop(CompatibilionEngine *e, uint32_t w, uint32_t h) {
    if (!e) return;
    e->caps.gop_width = w;
    e->caps.gop_height = h;
    if (w > 0 && h > 0) {
        e->caps.platform_flags |= COMPAT_PLAT_GOP;
    }
}

int compatibilion_has_cpu(CompatibilionEngine *e, uint32_t flag) {
    if (!e) return 0;
    return (e->caps.cpu_flags & flag) != 0;
}

int compatibilion_has_platform(CompatibilionEngine *e, uint32_t flag) {
    if (!e) return 0;
    return (e->caps.platform_flags & flag) != 0;
}

int compatibilion_recommend_attn(CompatibilionEngine *e) {
    if (!e) return 0;
    // Prefer AVX2 if available
    if (e->caps.cpu_flags & COMPAT_CPU_AVX2) return 1;
    return 0;
}

uint32_t compatibilion_recommend_model_mb(CompatibilionEngine *e) {
    if (!e) return 64;
    switch (e->caps.mem_tier) {
        case COMPAT_MEM_LOW:    return 64;
        case COMPAT_MEM_MEDIUM: return 256;
        case COMPAT_MEM_HIGH:   return 1024;
        case COMPAT_MEM_ULTRA:  return 4096;
        default:                return 128;
    }
}
