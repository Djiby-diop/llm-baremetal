#pragma once
#include <stdint.h>

/**
 * OO USB HID - Keyboard & Mouse Driver
 * 
 * Decodes USB packets from XHCI for Human Interface Devices.
 */

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keycodes[6]; // Up to 6 simultaneous key presses
} UsbKeyboardReport;

// Initialize USB HID stack (called after XHCI probe)
void oo_usb_hid_init(void);

// Poll for keyboard input. Returns ASCII character or 0.
char oo_usb_get_char(void);

// Handle incoming HID report from XHCI interrupt
void oo_usb_handle_report(const UsbKeyboardReport *report);
