/* oo_usb_hid.h — USB HID keyboard (post-EBS, no UEFI)  Phase 5D
 * =============================================================
 * Scans PCI for XHCI controller, polls USB interrupt endpoint,
 * translates HID keycodes → ASCII for the REPL.
 */
#pragma once
#include <efi.h>

#define OO_USB_MAX_PORTS   16
#define OO_USB_KEY_BUFSZ   64

/* USB key state */
typedef struct {
    UINT8  modifier;
    UINT8  reserved;
    UINT8  keycode[6];
} __attribute__((packed)) OoHidReport;

typedef struct {
    int    initialized;
    UINT32 xhci_bar0;          /* XHCI base address */
    UINT64 xhci_base;          /* MMIO base (64-bit capable) */
    UINT8  key_buf[OO_USB_KEY_BUFSZ];
    UINT32 key_head, key_tail;
    UINT8  prev_keycodes[6];   /* dedup held keys */
} OoUsbHid;

EFI_STATUS oo_usb_hid_init(OoUsbHid *ctx);
int        oo_usb_hid_poll(OoUsbHid *ctx);  /* returns # new chars */
int        oo_usb_hid_getchar(OoUsbHid *ctx);  /* -1 if empty */
void       oo_usb_hid_print(const OoUsbHid *ctx);
int        oo_usb_hid_repl_cmd(OoUsbHid *ctx, const char *cmd);

extern OoUsbHid g_usb_hid;
