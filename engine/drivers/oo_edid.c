// oo_edid.c — EDID Monitor Detection + GOP Resolution Auto-Select (Implementation)
//
// Freestanding C11 — UEFI Ring 0, no libc, no malloc.

#include "oo_edid.h"

// ── UEFI minimal type shims ───────────────────────────────────────────────────
// We use void* for UEFI protocol pointers to remain freestanding.
// Real integration casts these to EFI_GRAPHICS_OUTPUT_PROTOCOL* etc.

typedef uint64_t EfiStatus;
typedef void *   EfiHandle;

// EFI_GUID (16-byte)
typedef struct { uint32_t d1; uint16_t d2; uint16_t d3; uint8_t d4[8]; } EfiGuid;

// EFI_EDID_ACTIVE_PROTOCOL (simplified)
typedef struct {
    uint32_t size_of_edid;
    uint8_t  *edid;
} OoEfiEdidProto;

// EFI_GRAPHICS_OUTPUT_MODE_INFORMATION (simplified)
typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    uint32_t pixel_info[4];
    uint32_t pixels_per_scan_line;
} OoGopModeInfo;

// EFI_GRAPHICS_OUTPUT_PROTOCOL (only the fields we use)
typedef struct {
    // query_mode(gop, mode_number, size_of_info, &info)
    EfiStatus (*query_mode)(void *gop, uint32_t mode_num, uint64_t *size, OoGopModeInfo **info);
    // set_mode(gop, mode_number)
    EfiStatus (*set_mode)(void *gop, uint32_t mode_num);
    void     *blt;
    struct {
        uint32_t max_mode;
        uint32_t mode;
        OoGopModeInfo *info;
        uint64_t size_of_info;
        uint64_t frame_buffer_base;
        uint64_t frame_buffer_size;
    } *mode;
} OoGopProto;

// EFI_SYSTEM_TABLE (minimal — enough to call LocateProtocol)
typedef struct {
    uint8_t   hdr[24];
    void     *console_in_handle;
    void     *con_in;
    void     *console_out_handle;
    void     *con_out;
    void     *standard_error_handle;
    void     *std_err;
    void     *runtime_services;
    struct {
        uint8_t hdr[24];
        // ... lots of fields, we only need LocateProtocol
        void *raise_tpl;
        void *restore_tpl;
        void *allocate_pages;
        void *free_pages;
        void *get_memory_map;
        void *allocate_pool;
        void *free_pool;
        void *create_event;
        void *set_timer;
        void *wait_for_event;
        void *signal_event;
        void *close_event;
        void *check_event;
        void *install_protocol_interface;
        void *reinstall_protocol_interface;
        void *uninstall_protocol_interface;
        void *handle_protocol;
        void *_reserved;
        void *register_protocol_notify;
        void *locate_handle;
        void *locate_device_path;
        void *install_configuration_table;
        void *load_image;
        void *start_image;
        void *exit;
        void *unload_image;
        void *exit_boot_services;
        void *get_next_monotonic_count;
        void *stall;
        void *set_watchdog_timer;
        void *connect_controller;
        void *disconnect_controller;
        void *open_protocol;
        void *close_protocol;
        void *open_protocol_information;
        void *protocols_per_handle;
        void *locate_handle_buffer;
        EfiStatus (*locate_protocol)(EfiGuid *proto, void *registration, void **interface);
    } *boot_services;
} OoEfiSystemTable;

// ── EDID parsing ──────────────────────────────────────────────────────────────

int oo_edid_checksum_ok(const uint8_t *edid) {
    uint8_t sum = 0;
    for (int i = 0; i < 128; i++) sum += edid[i];
    return sum == 0;
}

// Parse preferred timing from Descriptor Block 0 (bytes 54-71)
// Detailed Timing Descriptor format (18 bytes)
int oo_edid_parse(const uint8_t *edid, OoEdidPreferredTiming *timing) {
    if (!edid || !timing) return 0;
    timing->valid = 0;

    // Header check
    static const uint8_t hdr[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
    for (int i = 0; i < 8; i++) if (edid[i] != hdr[i]) return 0;

    if (!oo_edid_checksum_ok(edid)) return 0;

    // Detailed Timing Descriptor 0 starts at byte 54
    const uint8_t *d = edid + 54;

    // Bytes 0-1: pixel clock in 10kHz units (0 = monitor descriptor, skip)
    uint32_t pixel_clk = (uint32_t)d[0] | ((uint32_t)d[1] << 8);
    if (pixel_clk == 0) return 0;  // not a timing descriptor

    // Horizontal active pixels: d[2] low + bits[7:4] of d[4]
    uint32_t h_active = (uint32_t)d[2] | (((uint32_t)d[4] & 0xF0) << 4);
    // Vertical active lines: d[5] low + bits[7:4] of d[7]
    uint32_t v_active = (uint32_t)d[5] | (((uint32_t)d[7] & 0xF0) << 4);

    if (h_active < 640 || v_active < 480) return 0;

    // Horizontal total = h_active + h_blanking
    uint32_t h_blank  = (uint32_t)d[3] | (((uint32_t)d[4] & 0x0F) << 8);
    uint32_t v_blank  = (uint32_t)d[6] | (((uint32_t)d[7] & 0x0F) << 8);
    uint32_t h_total  = h_active + h_blank;
    uint32_t v_total  = v_active + v_blank;

    // Refresh = pixel_clock×10000 / (h_total × v_total)
    uint32_t refresh = 0;
    if (h_total > 0 && v_total > 0) {
        refresh = (pixel_clk * 10000u) / (h_total * v_total);
    }

    timing->h_pixels   = h_active;
    timing->v_pixels   = v_active;
    timing->refresh_hz = refresh > 0 ? refresh : 60;
    timing->valid      = 1;
    return 1;
}

// ── GOP mode selection ────────────────────────────────────────────────────────

// Fallback resolution chain
static const struct { uint32_t w, h; } _fb_chain[] = {
    {1920, 1080}, {1280, 720}, {1024, 768}, {800, 600}, {640, 480}
};
#define FB_CHAIN_LEN 5

int oo_edid_select_gop_mode(void *gop_raw,
                             uint32_t preferred_w, uint32_t preferred_h,
                             OoGopMode *result) {
    if (!gop_raw || !result) return -1;
    OoGopProto *gop = (OoGopProto *)gop_raw;

    result->valid = 0;
    uint32_t max_mode = gop->mode->max_mode;

    // Build candidate list: prefer EDID target, then fallback chain
    uint32_t targets_w[FB_CHAIN_LEN + 1];
    uint32_t targets_h[FB_CHAIN_LEN + 1];
    int n_targets = 0;

    if (preferred_w >= 640 && preferred_h >= 480) {
        targets_w[n_targets] = preferred_w;
        targets_h[n_targets] = preferred_h;
        n_targets++;
    }
    for (int i = 0; i < FB_CHAIN_LEN; i++) {
        targets_w[n_targets] = _fb_chain[i].w;
        targets_h[n_targets] = _fb_chain[i].h;
        n_targets++;
    }

    for (int t = 0; t < n_targets; t++) {
        for (uint32_t m = 0; m < max_mode; m++) {
            OoGopModeInfo *info = (OoGopModeInfo *)0;
            uint64_t info_size = 0;
            EfiStatus st = gop->query_mode(gop, m, &info_size, &info);
            if (st != 0 || !info) continue;

            if (info->horizontal_resolution == targets_w[t] &&
                info->vertical_resolution   == targets_h[t]) {
                result->width       = targets_w[t];
                result->height      = targets_h[t];
                result->mode_number = m;
                result->valid       = 1;
                return (int)m;
            }
        }
    }

    // Last resort: use current mode
    result->width       = gop->mode->info->horizontal_resolution;
    result->height      = gop->mode->info->vertical_resolution;
    result->mode_number = gop->mode->mode;
    result->valid       = 1;
    return (int)gop->mode->mode;
}

// ── Full UEFI init ────────────────────────────────────────────────────────────

int oo_edid_init(void *system_table_raw, OoEdidResult *result) {
    if (!result) return -1;
    result->initialized = 0;

    OoEfiSystemTable *st = (OoEfiSystemTable *)system_table_raw;
    if (!st || !st->boot_services) return -1;

    // 1. Locate GOP
    EfiGuid gop_guid = OO_EFI_GOP_GUID;
    OoGopProto *gop = (OoGopProto *)0;
    EfiStatus s = st->boot_services->locate_protocol(&gop_guid, (void *)0, (void **)&gop);
    if (s != 0 || !gop) return -1;

    // 2. Try to get EDID Active protocol
    EfiGuid edid_guid = OO_EFI_EDID_ACTIVE_GUID;
    OoEfiEdidProto *edid_proto = (OoEfiEdidProto *)0;
    uint32_t preferred_w = 0, preferred_h = 0;

    s = st->boot_services->locate_protocol(&edid_guid, (void *)0, (void **)&edid_proto);
    if (s == 0 && edid_proto && edid_proto->size_of_edid >= 128 && edid_proto->edid) {
        OoEdidPreferredTiming timing;
        if (oo_edid_parse(edid_proto->edid, &timing)) {
            preferred_w = timing.h_pixels;
            preferred_h = timing.v_pixels;
        }
    }

    // 3. Select GOP mode
    OoGopMode gop_mode;
    int mode_num = oo_edid_select_gop_mode(gop, preferred_w, preferred_h, &gop_mode);
    if (!gop_mode.valid) return -1;

    // 4. Set the mode (changes resolution)
    if (mode_num != (int)gop->mode->mode) {
        s = gop->set_mode(gop, (uint32_t)mode_num);
        if (s != 0) {
            // Failed to set — stick with current
            gop_mode.width       = gop->mode->info->horizontal_resolution;
            gop_mode.height      = gop->mode->info->vertical_resolution;
            gop_mode.mode_number = gop->mode->mode;
        }
    }

    result->width       = gop_mode.width;
    result->height      = gop_mode.height;
    result->mode_number = gop_mode.mode_number;
    result->from_edid   = (preferred_w > 0 &&
                           gop_mode.width == preferred_w &&
                           gop_mode.height == preferred_h) ? 1 : 0;
    result->initialized = 1;
    return 0;
}

// ── I2C/DDC read (bare-metal, post-ExitBootServices) ─────────────────────────
// Most systems don't need this — GOP already sets resolution.
// Provided for completeness (e.g., custom display drivers).

int oo_edid_read_i2c(uint64_t i2c_base, uint8_t *out) {
    // Very hardware-specific — this is a stub.
    // Real implementation would: program I2C start, address 0xA0,
    // read 128 bytes via register polling.
    (void)i2c_base; (void)out;
    return 0;
}

// ── Formatting ────────────────────────────────────────────────────────────────

void oo_edid_format_res(uint32_t w, uint32_t h, char *out, int cap) {
    if (!out || cap < 8) return;
    // Format: "1920x1080"
    int i = 0;
    // Write width
    char wbuf[8]; int wn = 0;
    uint32_t tmp = w;
    if (tmp == 0) { wbuf[wn++] = '0'; } else {
        while (tmp > 0) { wbuf[wn++] = (char)('0' + tmp % 10); tmp /= 10; }
    }
    for (int j = wn - 1; j >= 0 && i < cap - 1; j--) out[i++] = wbuf[j];
    if (i < cap - 1) out[i++] = 'x';
    // Write height
    char hbuf[8]; int hn = 0;
    tmp = h;
    if (tmp == 0) { hbuf[hn++] = '0'; } else {
        while (tmp > 0) { hbuf[hn++] = (char)('0' + tmp % 10); tmp /= 10; }
    }
    for (int j = hn - 1; j >= 0 && i < cap - 1; j--) out[i++] = hbuf[j];
    out[i] = '\0';
}
