#include "morphion.h"

void morphion_init(MorphionEngine *e) {
    if (!e) return;
    e->mode = MORPHION_MODE_OFF;
    e->probe.vendor_ebx = 0;
    e->probe.vendor_ecx = 0;
    e->probe.vendor_edx = 0;
    e->probe.features_ebx = 0;
    e->module_count = 0;
}

void morphion_set_mode(MorphionEngine *e, MorphionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void morphion_probe(MorphionEngine *e) {
    if (!e || e->mode == MORPHION_MODE_OFF) return;
    /* CPUID leaf 0: vendor string (EBX/EDX/ECX) */
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );
    e->probe.vendor_ebx = ebx;
    e->probe.vendor_ecx = ecx;
    e->probe.vendor_edx = edx;
    /* CPUID leaf 7 sub-leaf 0: extended features (EBX) */
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0)
    );
    e->probe.features_ebx = ebx;  /* bit 5=AVX2, bit 16=AVX512F, etc. */
}

uint32_t morphion_suggest_load_order(MorphionEngine *e, uint32_t *out, uint32_t cap) {
    if (!e || !out || cap == 0) return 0;
    if (e->mode != MORPHION_MODE_MORPH) return 0;
    return 0;
}

const char *morphion_mode_name_ascii(MorphionMode mode) {
    switch (mode) {
        case MORPHION_MODE_OFF:   return "off";
        case MORPHION_MODE_PROBE: return "probe";
        case MORPHION_MODE_MORPH: return "morph";
        default:                  return "?";
    }
}
