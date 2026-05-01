#include "ps2.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void oo_ps2_init(void) {
    /* Basic initialization, assuming PS/2 controller exists */
    // Disable devices
    outb(PS2_COMMAND_PORT, 0xAD); // Disable port 1
    outb(PS2_COMMAND_PORT, 0xA7); // Disable port 2
    
    // Flush output buffer
    while (inb(PS2_STATUS_PORT) & 1) {
        inb(PS2_DATA_PORT);
    }
    
    // Enable port 1
    outb(PS2_COMMAND_PORT, 0xAE);
}

int oo_ps2_has_data(void) {
    return (inb(PS2_STATUS_PORT) & 1);
}

uint8_t oo_ps2_read_scancode(void) {
    while (!oo_ps2_has_data());
    return inb(PS2_DATA_PORT);
}

/* Very basic US QWERTY mapping for set 1 scancodes */
static const char scancode_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', /* 0x0E */
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', /* 0x1C */
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',  0,  /* 0x2A */
  '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',   0,  /* 0x38 */
  ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  /* 0x46 */
};

char oo_ps2_scancode_to_ascii(uint8_t scancode, int shift_pressed) {
    if (scancode > 0x46) return 0;
    char c = scancode_ascii[scancode];
    if (shift_pressed && c >= 'a' && c <= 'z') {
        c -= 32; /* Uppercase */
    }
    return c;
}
