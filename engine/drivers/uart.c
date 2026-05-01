#include "uart.h"

/* Helper pour l'I/O x86 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void oo_uart_init(uint16_t port) {
    outb(port + 1, 0x00);    /* Disable all interrupts */
    outb(port + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(port + 0, 0x03);    /* Set divisor to 3 (lo byte) 38400 baud */
    outb(port + 1, 0x00);    /*                  (hi byte) */
    outb(port + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(port + 2, 0xC7);    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(port + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
}

int oo_uart_is_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}

void oo_uart_write_char(uint16_t port, char c) {
    while (oo_uart_is_transmit_empty(port) == 0);
    outb(port, c);
}

void oo_uart_write_string(uint16_t port, const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        oo_uart_write_char(port, str[i]);
    }
}

int oo_uart_has_data(uint16_t port) {
    return inb(port + 5) & 1;
}

char oo_uart_read_char(uint16_t port) {
    while (oo_uart_has_data(port) == 0);
    return inb(port);
}
