// oo_rtc.c — Real-Time Clock (CMOS RTC) Driver (Implementation)
//
// Freestanding C11 — no libc, no malloc, no external deps.
// Uses raw x86 I/O port instructions via inline asm.

#include "oo_rtc.h"

// ── I/O port access (x86 bare-metal) ─────────────────────────────────────────

static inline void _rtc_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t _rtc_inb(uint16_t port) {
    uint8_t val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Short I/O delay (one PCI bus cycle ≈ 1µs)
static inline void _rtc_io_delay(void) {
    _rtc_outb(0x80, 0);  // unused port — safe to write
}

// ── CMOS register read/write ──────────────────────────────────────────────────

static uint8_t _cmos_read(uint8_t reg) {
    _rtc_outb(OO_RTC_PORT_ADDR, (uint8_t)(OO_RTC_NMI_DISABLE | reg));
    _rtc_io_delay();
    return _rtc_inb(OO_RTC_PORT_DATA);
}

static void _cmos_write(uint8_t reg, uint8_t val) {
    _rtc_outb(OO_RTC_PORT_ADDR, (uint8_t)(OO_RTC_NMI_DISABLE | reg));
    _rtc_io_delay();
    _rtc_outb(OO_RTC_PORT_DATA, val);
}

// ── Wait for RTC Update-In-Progress flag to clear ────────────────────────────

static void _rtc_wait_uip(void) {
    // Spin for up to ~1ms (enough for one RTC tick update cycle)
    for (int i = 0; i < 1000000; i++) {
        if (!(_cmos_read(OO_RTC_REG_STATUS_A) & 0x80)) return;
        _rtc_io_delay();
    }
}

// ── BCD / binary mode state ───────────────────────────────────────────────────

static int _rtc_is_bcd   = 1;  // 1 = RTC returns BCD, 0 = binary
static int _rtc_is_12h   = 0;  // 1 = 12-hour mode
static int _rtc_initialized = 0;

static uint8_t _bcd2bin(uint8_t bcd) {
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

static uint8_t _bin2bcd(uint8_t bin) {
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

static uint8_t _rtc_to_bin(uint8_t raw) {
    return _rtc_is_bcd ? _bcd2bin(raw) : raw;
}

static uint8_t _rtc_from_bin(uint8_t bin) {
    return _rtc_is_bcd ? _bin2bcd(bin) : bin;
}

// ── Init ─────────────────────────────────────────────────────────────────────

void oo_rtc_init(void) {
    uint8_t status_b = _cmos_read(OO_RTC_REG_STATUS_B);
    _rtc_is_bcd  = !(status_b & 0x04);  // bit 2 clear = BCD mode
    _rtc_is_12h  = !(status_b & 0x02);  // bit 1 clear = 12-hour mode
    _rtc_initialized = 1;
}

// ── Read date/time ────────────────────────────────────────────────────────────

int oo_rtc_get(OoRtcTime *t) {
    if (!t) return -1;
    if (!_rtc_initialized) oo_rtc_init();

    // Read twice and compare to handle UIP
    OoRtcTime a, b;

    for (int attempt = 0; attempt < 3; attempt++) {
        _rtc_wait_uip();

        a.second  = _rtc_to_bin(_cmos_read(OO_RTC_REG_SECOND));
        a.minute  = _rtc_to_bin(_cmos_read(OO_RTC_REG_MINUTE));
        uint8_t hr_raw = _cmos_read(OO_RTC_REG_HOUR);
        a.weekday = _rtc_to_bin(_cmos_read(OO_RTC_REG_WEEKDAY));
        a.day     = _rtc_to_bin(_cmos_read(OO_RTC_REG_DAY));
        a.month   = _rtc_to_bin(_cmos_read(OO_RTC_REG_MONTH));

        uint8_t yr = _rtc_to_bin(_cmos_read(OO_RTC_REG_YEAR));
        uint8_t cen = _rtc_to_bin(_cmos_read(OO_RTC_REG_CENTURY));

        // Convert 12h to 24h
        int pm = 0;
        if (_rtc_is_12h) {
            pm = (hr_raw & 0x80) ? 1 : 0;
            hr_raw &= 0x7F;
        }
        a.hour = _rtc_to_bin(hr_raw);
        if (_rtc_is_12h) {
            if (pm && a.hour < 12) a.hour += 12;
            if (!pm && a.hour == 12) a.hour = 0;
        }

        // Build year
        if (cen >= 19 && cen <= 21) {
            a.year = (uint16_t)(cen * 100 + yr);
        } else {
            // Heuristic: if yr >= 70 assume 1970s, else 2000s
            a.year = (uint16_t)(yr >= 70 ? 1900 + yr : 2000 + yr);
        }

        // Read again to confirm (no update in progress)
        _rtc_wait_uip();
        b.second  = _rtc_to_bin(_cmos_read(OO_RTC_REG_SECOND));
        b.minute  = _rtc_to_bin(_cmos_read(OO_RTC_REG_MINUTE));
        b.day     = _rtc_to_bin(_cmos_read(OO_RTC_REG_DAY));

        if (a.second == b.second && a.minute == b.minute && a.day == b.day) {
            *t = a;
            return 0;
        }
    }

    // Last attempt — just return a
    *t = a;
    return 0;
}

// ── Write date/time ───────────────────────────────────────────────────────────

int oo_rtc_set(const OoRtcTime *t) {
    if (!t) return -1;
    if (!_rtc_initialized) oo_rtc_init();

    // Disable RTC update cycle before writing (set SET bit in Status B)
    uint8_t sb = _cmos_read(OO_RTC_REG_STATUS_B);
    _cmos_write(OO_RTC_REG_STATUS_B, (uint8_t)(sb | 0x80));

    _cmos_write(OO_RTC_REG_SECOND,  _rtc_from_bin(t->second));
    _cmos_write(OO_RTC_REG_MINUTE,  _rtc_from_bin(t->minute));
    _cmos_write(OO_RTC_REG_HOUR,    _rtc_from_bin(t->hour));
    _cmos_write(OO_RTC_REG_WEEKDAY, _rtc_from_bin(t->weekday));
    _cmos_write(OO_RTC_REG_DAY,     _rtc_from_bin(t->day));
    _cmos_write(OO_RTC_REG_MONTH,   _rtc_from_bin(t->month));
    _cmos_write(OO_RTC_REG_YEAR,    _rtc_from_bin((uint8_t)(t->year % 100)));
    _cmos_write(OO_RTC_REG_CENTURY, _rtc_from_bin((uint8_t)(t->year / 100)));

    // Re-enable update cycle
    _cmos_write(OO_RTC_REG_STATUS_B, (uint8_t)(sb & ~0x80));
    return 0;
}

// ── Unix-like timestamp (seconds since 2000-01-01) ────────────────────────────

uint32_t oo_rtc_timestamp(void) {
    OoRtcTime t;
    if (oo_rtc_get(&t) != 0) return 0;

    // Days in each month (non-leap year)
    static const uint16_t days_in_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    // Count days from 2000-01-01
    uint32_t days = 0;
    for (int y = 2000; y < t.year; y++) {
        int leap = ((y%4==0 && y%100!=0) || y%400==0);
        days += (uint32_t)(365 + leap);
    }
    int leap_now = ((t.year%4==0 && t.year%100!=0) || t.year%400==0);
    for (int m = 1; m < t.month; m++) {
        days += days_in_month[m-1];
        if (m == 2 && leap_now) days += 1;
    }
    days += (uint32_t)(t.day - 1);

    return days * 86400UL
           + (uint32_t)t.hour   * 3600
           + (uint32_t)t.minute * 60
           + (uint32_t)t.second;
}

// ── Formatting ────────────────────────────────────────────────────────────────

static void _u8_2dig(char *out, uint8_t v) {
    out[0] = (char)('0' + v / 10);
    out[1] = (char)('0' + v % 10);
}

static void _u16_4dig(char *out, uint16_t v) {
    out[0] = (char)('0' + (v / 1000) % 10);
    out[1] = (char)('0' + (v / 100)  % 10);
    out[2] = (char)('0' + (v / 10)   % 10);
    out[3] = (char)('0' + v % 10);
}

void oo_rtc_format(const OoRtcTime *t, char *buf, int cap) {
    if (!t || !buf || cap < 20) return;
    // "2026-05-04 23:34:00"
    _u16_4dig(buf, t->year);     buf[4]='-';
    _u8_2dig(buf+5, t->month);   buf[7]='-';
    _u8_2dig(buf+8, t->day);     buf[10]=' ';
    _u8_2dig(buf+11, t->hour);   buf[13]=':';
    _u8_2dig(buf+14, t->minute); buf[16]=':';
    _u8_2dig(buf+17, t->second); buf[19]='\0';
}

void oo_rtc_format_compact(const OoRtcTime *t, char *buf, int cap) {
    if (!t || !buf || cap < 16) return;
    // "20260504-233400"
    _u16_4dig(buf, t->year);
    _u8_2dig(buf+4, t->month);
    _u8_2dig(buf+6, t->day);
    buf[8] = '-';
    _u8_2dig(buf+9, t->hour);
    _u8_2dig(buf+11, t->minute);
    _u8_2dig(buf+13, t->second);
    buf[15] = '\0';
}

void oo_rtc_format_hhmm(const OoRtcTime *t, char *buf, int cap) {
    if (!t || !buf || cap < 6) return;
    // "23:34"
    _u8_2dig(buf, t->hour);
    buf[2] = ':';
    _u8_2dig(buf+3, t->minute);
    buf[5] = '\0';
}

// ── Availability check ────────────────────────────────────────────────────────

int oo_rtc_available(void) {
    // Read seconds register twice, 1ms apart
    uint8_t s1 = _cmos_read(OO_RTC_REG_SECOND);
    for (volatile int i = 0; i < 50000; i++) _rtc_io_delay();
    uint8_t s2 = _cmos_read(OO_RTC_REG_SECOND);
    // If stuck at 0xFF, CMOS not present
    if (s1 == 0xFF && s2 == 0xFF) return 0;
    // If seconds changed by more than 59 in BCD, suspicious
    return 1;
}
