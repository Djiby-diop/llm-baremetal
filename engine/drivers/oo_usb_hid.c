#include "oo_usb_hid.h"
#include <string.h>

// USB HID Scan Code to ASCII mapping (US QWERTY)
static const char scancode_to_ascii[] = {
    0, 0, 0, 0, 
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 0x1B, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'
};

static char g_key_buffer[64];
static int  g_buf_head = 0;
static int  g_buf_tail = 0;

void oo_usb_hid_init(void) {
    memset(g_key_buffer, 0, sizeof(g_key_buffer));
    g_buf_head = 0;
    g_buf_tail = 0;
}

char oo_usb_get_char(void) {
    if (g_buf_head == g_buf_tail) return 0; // Buffer vide
    
    char c = g_key_buffer[g_buf_tail];
    g_buf_tail = (g_buf_tail + 1) % 64;
    return c;
}

void oo_usb_handle_report(const UsbKeyboardReport *report) {
    if (!report) return;
    
    // Pour simplifier, on ne gère que la première touche pressée du rapport
    uint8_t code = report->keycodes[0];
    
    if (code >= 4 && code < sizeof(scancode_to_ascii)) {
        char c = scancode_to_ascii[code];
        
        // Gestion Majuscules (Shift)
        if (report->modifiers & 0x22) { // Left or Right Shift
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }
        
        // Empile dans le buffer circulaire
        int next = (g_buf_head + 1) % 64;
        if (next != g_buf_tail) {
            g_key_buffer[g_buf_head] = c;
            g_buf_head = next;
        }
    }
}
