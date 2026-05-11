#pragma once
/* oo_irq.h — PS/2 keyboard IRQ + 8259A PIC wiring for OO bare-metal
 * Installs keyboard ISR at IDT[33] (IRQ1) using existing IDT from oo_exit_boot.c.
 * Provides a ring buffer for keyboard chars accessible from the REPL.
 */

#include <efi.h>
#include <efilib.h>

/* PIC port addresses */
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

/* Commands */
#define PIC_EOI    0x20
#define ICW1_INIT  0x11
#define ICW4_8086  0x01

/* Keyboard ring buffer (ISR → REPL) */
#define KBD_RING_SIZE 128

typedef struct {
    volatile UINT8  buf[KBD_RING_SIZE];
    volatile UINT32 head;
    volatile UINT32 tail;
} oo_kbd_ring_t;

/* Public API */
void  oo_irq_init(void);          /* reprogram PIC + hook IDT[33] + sti */
void  oo_irq_mask(UINT8 irq);     /* mask one IRQ line */
void  oo_irq_unmask(UINT8 irq);   /* unmask one IRQ line */
int   oo_kbd_getchar(void);       /* pop char from ring (-1 if empty) */
void  oo_kbd_push(UINT8 ch);      /* called by ISR */
void  oo_kbd_isr_handler(void);   /* C ISR body (called from naked stub) */
