// oo_rtc.h — Real-Time Clock (CMOS RTC) Driver
//
// Reads/writes the x86 CMOS Real-Time Clock via I/O ports 0x70/0x71.
// Provides accurate timestamps for: journal entries, session names,
// uptime display, sleep scheduling.
//
// Handles:
//   - BCD vs binary mode auto-detection
//   - 12h vs 24h mode auto-detection
//   - Update-in-progress flag (UIP) wait
//   - No leap second handling (out of scope for bare-metal)
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Date/time struct ──────────────────────────────────────────────────────────
typedef struct {
    uint16_t year;    // full year (e.g. 2026)
    uint8_t  month;   // 1-12
    uint8_t  day;     // 1-31
    uint8_t  hour;    // 0-23 (always 24h after normalization)
    uint8_t  minute;  // 0-59
    uint8_t  second;  // 0-59
    uint8_t  weekday; // 1=Sunday ... 7=Saturday
} OoRtcTime;

// ── RTC CMOS register indices ─────────────────────────────────────────────────
#define OO_RTC_REG_SECOND   0x00
#define OO_RTC_REG_MINUTE   0x02
#define OO_RTC_REG_HOUR     0x04
#define OO_RTC_REG_WEEKDAY  0x06
#define OO_RTC_REG_DAY      0x07
#define OO_RTC_REG_MONTH    0x08
#define OO_RTC_REG_YEAR     0x09
#define OO_RTC_REG_CENTURY  0x32  // may not exist on all hardware
#define OO_RTC_REG_STATUS_A 0x0A
#define OO_RTC_REG_STATUS_B 0x0B

#define OO_RTC_PORT_ADDR    0x70  // select register
#define OO_RTC_PORT_DATA    0x71  // read/write data
#define OO_RTC_NMI_DISABLE  0x80  // OR into addr byte to disable NMI

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize RTC (detects BCD/binary and 12/24h modes)
void oo_rtc_init(void);

// Read current date/time from RTC
// Returns 0 on success, -1 if RTC not responding
int oo_rtc_get(OoRtcTime *t);

// Write date/time to RTC (use with care — modifies CMOS)
int oo_rtc_set(const OoRtcTime *t);

// Get current Unix-like timestamp (seconds since 2000-01-01 00:00:00)
// Useful for monotonic comparison without libc time()
uint32_t oo_rtc_timestamp(void);

// Format time as "2026-05-04 23:34:00" into buf (needs 20 bytes)
void oo_rtc_format(const OoRtcTime *t, char *buf, int cap);

// Format as compact "20260504-233400" (15 chars) for file/session names
void oo_rtc_format_compact(const OoRtcTime *t, char *buf, int cap);

// Get just the hour:minute as "HH:MM"
void oo_rtc_format_hhmm(const OoRtcTime *t, char *buf, int cap);

// Check if RTC is available (returns 0 if CMOS not readable)
int oo_rtc_available(void);

#ifdef __cplusplus
}
#endif
