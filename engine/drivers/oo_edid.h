// oo_edid.h — EDID Monitor Detection + GOP Resolution Auto-Select
//
// Queries EDID data via UEFI protocols to determine the monitor's
// preferred resolution, then selects the best GOP mode.
//
// Fallback chain: preferred_mode → 1920×1080 → 1280×720 → 1024×768 → 800×600
//
// Also exposes a bare-metal EDID parser for use after ExitBootServices()
// (reads raw 128-byte EDID via DDC-over-I2C if hardware exposes it).
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── EDID block (128 bytes standard) ──────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  header[8];         // 0x00 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0x00
    uint16_t manufacturer_id;
    uint16_t product_code;
    uint32_t serial;
    uint8_t  week;
    uint8_t  year;              // year - 1990
    uint8_t  edid_version;
    uint8_t  edid_revision;
    uint8_t  video_input;
    uint8_t  h_size_cm;
    uint8_t  v_size_cm;
    uint8_t  gamma;
    uint8_t  features;
    uint8_t  color_chars[10];
    uint8_t  est_timings[3];
    uint8_t  std_timings[16];
    uint8_t  descriptors[72];   // 4 × 18-byte timing descriptors
    uint8_t  ext_count;
    uint8_t  checksum;
} OoEdidBlock;

// ── Preferred timing (extracted from Descriptor 0) ───────────────────────────
typedef struct {
    uint32_t h_pixels;     // horizontal resolution
    uint32_t v_pixels;     // vertical resolution
    uint32_t refresh_hz;   // refresh rate (approx)
    int      valid;        // 1 = parsed successfully
} OoEdidPreferredTiming;

// ── GOP mode candidate ────────────────────────────────────────────────────────
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mode_number;   // GOP mode index
    int      valid;
} OoGopMode;

// ── Resolution result ─────────────────────────────────────────────────────────
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mode_number;
    int      from_edid;       // 1 = matched EDID preferred, 0 = fallback
    int      initialized;
} OoEdidResult;

// ── UEFI protocol GUIDs (for use before ExitBootServices) ────────────────────
// These match the UEFI spec binary GUIDs

// EFI_EDID_ACTIVE_PROTOCOL_GUID
// {0xbd8c1056-9f36-44ec-92a8-a6337f817986}
#define OO_EFI_EDID_ACTIVE_GUID \
    {0xbd8c1056, 0x9f36, 0x44ec, {0x92,0xa8,0xa6,0x33,0x7f,0x81,0x79,0x86}}

// EFI_EDID_DISCOVERED_PROTOCOL_GUID
// {0x1c0c34f6-d380-41fa-a049-8ad06c1a66aa}
#define OO_EFI_EDID_DISCOVERED_GUID \
    {0x1c0c34f6, 0xd380, 0x41fa, {0xa0,0x49,0x8a,0xd0,0x6c,0x1a,0x66,0xaa}}

// EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID
// {0x9042a9de-23dc-4a38-96fb-7aded080516a}
#define OO_EFI_GOP_GUID \
    {0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}}

// ── Public API ────────────────────────────────────────────────────────────────

// Parse a raw 128-byte EDID block and extract preferred timing
// Returns 1 on success, 0 on checksum failure or invalid block
int oo_edid_parse(const uint8_t *edid_bytes, OoEdidPreferredTiming *timing);

// Find the best GOP mode matching the preferred resolution
// gop: pointer to EFI_GRAPHICS_OUTPUT_PROTOCOL (opaque here, cast inside)
// preferred_w/h: from oo_edid_parse, or 0 to skip and use fallback
// Returns the selected mode number, writes width/height into result
int oo_edid_select_gop_mode(void *gop,
                             uint32_t preferred_w, uint32_t preferred_h,
                             OoGopMode *result);

// Full init: query UEFI EDID protocol + select best GOP mode
// system_table: EFI_SYSTEM_TABLE* (pass from UEFI entry point)
// Returns 0 on success, -1 if GOP not available
int oo_edid_init(void *system_table, OoEdidResult *result);

// After ExitBootServices: try to read EDID via I2C/DDC if registers accessible
// Usually not needed since GOP resolution is already set
// i2c_base: MMIO base of I2C controller exposing DDC (hardware-specific)
// Returns 1 if EDID read successfully, 0 otherwise
int oo_edid_read_i2c(uint64_t i2c_base, uint8_t *out_128_bytes);

// Validate EDID checksum
int oo_edid_checksum_ok(const uint8_t *edid_bytes);

// Format resolution as "WxH" string (8 bytes min)
void oo_edid_format_res(uint32_t w, uint32_t h, char *out, int cap);

#ifdef __cplusplus
}
#endif
