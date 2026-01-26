#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compatibilion: platform detection + capability reporting engine
// Made in Senegal 🇸🇳

typedef enum {
    COMPATIBILION_MODE_OFF = 0,
    COMPATIBILION_MODE_ON  = 1,
} CompatibilionMode;

// CPU feature flags
#define COMPAT_CPU_SSE2    (1u << 0)
#define COMPAT_CPU_SSE41   (1u << 1)
#define COMPAT_CPU_AVX     (1u << 2)
#define COMPAT_CPU_AVX2    (1u << 3)
#define COMPAT_CPU_FMA     (1u << 4)
#define COMPAT_CPU_AVX512F (1u << 5)

// Platform flags
#define COMPAT_PLAT_UEFI   (1u << 0)
#define COMPAT_PLAT_BIOS   (1u << 1)
#define COMPAT_PLAT_QEMU   (1u << 2)
#define COMPAT_PLAT_HW     (1u << 3)
#define COMPAT_PLAT_GOP    (1u << 4)
#define COMPAT_PLAT_FAT32  (1u << 5)

// Memory tier
typedef enum {
    COMPAT_MEM_UNKNOWN = 0,
    COMPAT_MEM_LOW     = 1,   // < 256 MB
    COMPAT_MEM_MEDIUM  = 2,   // 256 MB - 1 GB
    COMPAT_MEM_HIGH    = 3,   // 1 GB - 4 GB
    COMPAT_MEM_ULTRA   = 4,   // > 4 GB
} CompatibilionMemTier;

typedef struct {
    uint32_t cpu_flags;
    uint32_t platform_flags;
    CompatibilionMemTier mem_tier;
    uint64_t mem_bytes;
    uint32_t gop_width;
    uint32_t gop_height;
    char cpu_vendor[16];
    char cpu_brand[64];
} CompatibilionCaps;

typedef struct {
    CompatibilionMode mode;
    CompatibilionCaps caps;
    uint32_t probes_done;
} CompatibilionEngine;

void compatibilion_init(CompatibilionEngine *e);
void compatibilion_set_mode(CompatibilionEngine *e, CompatibilionMode mode);
const char *compatibilion_mode_name_ascii(CompatibilionMode mode);
const char *compatibilion_mem_tier_name_ascii(CompatibilionMemTier tier);

// Probe CPU features (call once at boot)
void compatibilion_probe_cpu(CompatibilionEngine *e);

// Set platform caps (called by REPL after init)
void compatibilion_set_platform(CompatibilionEngine *e, uint32_t flags);
void compatibilion_set_memory(CompatibilionEngine *e, uint64_t bytes);
void compatibilion_set_gop(CompatibilionEngine *e, uint32_t w, uint32_t h);

// Check if a feature is available
int compatibilion_has_cpu(CompatibilionEngine *e, uint32_t flag);
int compatibilion_has_platform(CompatibilionEngine *e, uint32_t flag);

// Get recommended attention path (0=SSE2, 1=AVX2)
int compatibilion_recommend_attn(CompatibilionEngine *e);

// Get recommended max model size in MB
uint32_t compatibilion_recommend_model_mb(CompatibilionEngine *e);

#ifdef __cplusplus
}
#endif
