/*
 * Evolvion: Self-Evolving Kernel - core implementation.
 * Stub: records needs. LLM codegen + JIT wired by main binary.
 */

#include "evolvion.h"

static uint32_t simple_hash(const char *p) {
    uint32_t h = 0;
    while (*p) { h = h * 31 + (uint8_t)*p++; }
    return h;
}

/* Minimal bare-metal strcat into a fixed buffer */
static void evol_strcat(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    while (i < cap - 1 && dst[i]) i++;
    uint32_t j = 0;
    while (i < cap - 1 && src[j]) dst[i++] = src[j++];
    dst[i] = 0;
}

static void evol_append_hex16(char *dst, uint32_t cap, uint16_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[5];
    tmp[0] = hex[(v >> 12) & 0xF];
    tmp[1] = hex[(v >>  8) & 0xF];
    tmp[2] = hex[(v >>  4) & 0xF];
    tmp[3] = hex[ v        & 0xF];
    tmp[4] = 0;
    evol_strcat(dst, cap, tmp);
}

void evolvion_init(EvolvionEngine *e) {
    if (!e) return;
    e->mode              = EVOLVION_MODE_OFF;
    e->needs_recorded    = 0;
    e->codegen_attempts  = 0;
    e->jit_successes     = 0;
    e->drivers_generated = 0;
    e->driver_need_count = 0;
    e->codegen_buf[0]    = 0;
    for (int i = 0; i < EVOLVION_DRIVER_NEEDS_MAX; i++) {
        e->driver_need_vid[i] = 0;
        e->driver_need_did[i] = 0;
    }
}

void evolvion_set_mode(EvolvionEngine *e, EvolvionMode mode) {
    if (!e) return;
    e->mode = mode;
}

void evolvion_record_need(EvolvionEngine *e, EvolvionNeedType type, const char *desc) {
    if (!e || !desc) return;
    if (e->mode == EVOLVION_MODE_OFF) return;
    e->needs_recorded++;
    (void)type;
    (void)simple_hash(desc);
}

void evolvion_queue_driver(EvolvionEngine *e, uint16_t vendor_id, uint16_t device_id) {
    if (!e) return;
    if (e->driver_need_count >= EVOLVION_DRIVER_NEEDS_MAX) return;
    for (uint8_t i = 0; i < e->driver_need_count; i++) {
        if (e->driver_need_vid[i] == vendor_id &&
            e->driver_need_did[i] == device_id) return;
    }
    uint8_t idx = e->driver_need_count++;
    e->driver_need_vid[idx] = vendor_id;
    e->driver_need_did[idx] = device_id;
    e->needs_recorded++;
}

void evolvion_build_driver_prompt(EvolvionEngine *e,
                                  uint16_t vendor_id, uint16_t device_id,
                                  const char *class_name, const char *vendor_name,
                                  char *out, uint32_t cap) {
    if (!e || !out || cap < 2) return;
    out[0] = 0;
    evol_strcat(out, cap, "[OO-Driver] Generate minimal bare-metal C driver stub for PCI device: ");
    evol_strcat(out, cap, "vendor=");
    evol_strcat(out, cap, vendor_name);
    evol_strcat(out, cap, " (0x");
    evol_append_hex16(out, cap, vendor_id);
    evol_strcat(out, cap, ") device=0x");
    evol_append_hex16(out, cap, device_id);
    evol_strcat(out, cap, " class=");
    evol_strcat(out, cap, class_name);
    evol_strcat(out, cap, ". Provide: init(), read(), write() in freestanding C. No stdlib.");
    e->codegen_attempts++;
}

const char *evolvion_mode_name_ascii(EvolvionMode mode) {
    switch (mode) {
        case EVOLVION_MODE_OFF:   return "off";
        case EVOLVION_MODE_LEARN: return "learn";
        case EVOLVION_MODE_LIVE:  return "live";
        default:                  return "?";
    }
}