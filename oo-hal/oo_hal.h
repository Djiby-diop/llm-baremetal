#pragma once
/*
 * oo-hal — Hardware Abstraction Layer for OO Bare-Metal
 * ======================================================
 * Thin layer over UEFI GOP/SimpleInput/Timer.
 * Philosophy: wrap UEFI protocols minimally, expose clean C API.
 * Each driver has a "bypass mode" for when UEFI is unavailable
 * (post-ExitBootServices), using direct hardware access.
 *
 * Driver tree:
 *   keyboard/  — HID input via UEFI SimpleInput + 8042 PS/2 bypass
 *   timer/     — UEFI timer + HPET/APIC bypass
 *   display/   — GOP framebuffer wrapper + direct pixel write
 */

#ifndef OO_HAL_H
#define OO_HAL_H

#include <stdint.h>

/* ── Display ─────────────────────────────────────────────────────── */
typedef struct {
    uint32_t *framebuffer;    /* linear framebuffer base */
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;         /* pixels per scanline */
    int       initialized;
} OoHalDisplay;

int  oo_hal_display_init(OoHalDisplay *d, void *gop_proto);
void oo_hal_display_clear(OoHalDisplay *d, uint32_t color);
void oo_hal_display_pixel(OoHalDisplay *d, int x, int y, uint32_t color);
void oo_hal_display_rect(OoHalDisplay *d, int x, int y, int w, int h, uint32_t color);
void oo_hal_display_char(OoHalDisplay *d, int x, int y, char c,
                          uint32_t fg, uint32_t bg);
void oo_hal_display_string(OoHalDisplay *d, int x, int y, const char *s,
                            uint32_t fg, uint32_t bg);

/* ── Keyboard ────────────────────────────────────────────────────── */
typedef struct {
    void *simple_input_proto;  /* EFI_SIMPLE_TEXT_INPUT_PROTOCOL* */
    int   bypass_8042;         /* 1 = read 8042 directly, UEFI unavailable */
    uint8_t last_scancode;
    char    last_char;
} OoHalKeyboard;

int  oo_hal_keyboard_init(OoHalKeyboard *k, void *input_proto);
int  oo_hal_keyboard_poll(OoHalKeyboard *k);   /* returns 1 if key available */
char oo_hal_keyboard_read(OoHalKeyboard *k);   /* blocks until key */
uint8_t oo_hal_keyboard_scancode(OoHalKeyboard *k);

/* ── Timer ───────────────────────────────────────────────────────── */
typedef struct {
    uint64_t tsc_freq_hz;     /* measured at init */
    uint64_t boot_tsc;        /* TSC at boot */
    int      hpet_available;
    uint64_t hpet_base;       /* HPET MMIO base if available */
} OoHalTimer;

int      oo_hal_timer_init(OoHalTimer *t);
uint64_t oo_hal_timer_tsc(void);              /* raw RDTSC */
uint64_t oo_hal_timer_us(OoHalTimer *t);      /* microseconds since boot */
uint64_t oo_hal_timer_ms(OoHalTimer *t);      /* milliseconds since boot */
void     oo_hal_timer_sleep_us(OoHalTimer *t, uint64_t us);
void     oo_hal_timer_sleep_ms(OoHalTimer *t, uint64_t ms);

/* ── HAL bundle ──────────────────────────────────────────────────── */
typedef struct {
    OoHalDisplay  display;
    OoHalKeyboard keyboard;
    OoHalTimer    timer;
    int           initialized;
} OoHal;

int  oo_hal_init(OoHal *hal, void *gop, void *input);
void oo_hal_print(const OoHal *hal);

#endif /* OO_HAL_H */
