/* oo_usb_hid.h — USB HID keyboard input for bare-metal OO
 * Supplements PS/2 (engine/drivers/ps2.h) for USB keyboards.
 * Uses UEFI EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL — no USB stack needed.
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include <efilib.h>
#include <stdint.h>

#define OO_USB_HID_MAX_HANDLES  8   /* max keyboard handles to poll */
#define OO_USB_HID_BUF_SIZE    64   /* internal key ring buffer     */

typedef struct {
    EFI_HANDLE  handles[OO_USB_HID_MAX_HANDLES];
    int         n_handles;
    int         initialized;

    /* Ring buffer for pending chars */
    char   buf[OO_USB_HID_BUF_SIZE];
    int    head;
    int    tail;

    uint64_t total_keys;
} OoUsbHid;

/* Initialize: scan for all EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL handles.
 * Returns number of keyboards found (0 = none, falls back to ConIn). */
int  oo_usb_hid_init(OoUsbHid *h);

/* Poll all keyboards. Returns 1 if a key was added to the ring buffer. */
int  oo_usb_hid_poll(OoUsbHid *h);

/* Read one char from ring buffer. Returns 0 if empty. */
char oo_usb_hid_read_char(OoUsbHid *h);

/* Returns 1 if ring buffer has data. */
int  oo_usb_hid_has_data(const OoUsbHid *h);

/* Print status via callback. */
void oo_usb_hid_print_status(const OoUsbHid *h, void (*fn)(const char *));
