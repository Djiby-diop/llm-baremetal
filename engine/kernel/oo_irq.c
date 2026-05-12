/* oo_irq.c — PS/2 keyboard IRQ wiring, 8259A PIC re-init
 * Uses oo_idt_set_gate() from oo_exit_boot.c (already IDT-managing).
 * Must be called AFTER oo_idt_install().
 */

#include "oo_irq.h"
#include "oo_exit_boot.h"   /* for oo_idt_set_gate() */

/* ─── Keyboard ring buffer ────────────────────────────────────────────── */
static oo_kbd_ring_t _kbd_ring;

/* ─── I/O port helpers ────────────────────────────────────────────────── */
static inline void _outb_irq(UINT16 port, UINT8 val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline UINT8 _inb_irq(UINT16 port) {
    UINT8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void _io_wait_irq(void) { _outb_irq(0x80, 0); }

/* ─── Scancode → ASCII (US QWERTY, set 1) ────────────────────────────── */
static const char _sc2ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    9,  'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' ', 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0, '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

/* ─── Ring buffer ─────────────────────────────────────────────────────── */
void oo_kbd_push(UINT8 ch) {
    UINT32 next = (_kbd_ring.head + 1) % KBD_RING_SIZE;
    if (next != _kbd_ring.tail)
        _kbd_ring.buf[_kbd_ring.head] = ch, _kbd_ring.head = next;
}

int oo_kbd_getchar(void) {
    if (_kbd_ring.head == _kbd_ring.tail) return -1;
    UINT8 ch = _kbd_ring.buf[_kbd_ring.tail];
    _kbd_ring.tail = (_kbd_ring.tail + 1) % KBD_RING_SIZE;
    return ch;
}

/* ─── Keyboard ISR C handler ──────────────────────────────────────────── */
void oo_kbd_isr_handler(void) {
    UINT8 sc = _inb_irq(0x60);
    if (!(sc & 0x80) && sc < 128 && _sc2ascii[sc])
        oo_kbd_push(_sc2ascii[sc]);
    _outb_irq(PIC1_CMD, PIC_EOI);
}

/* ─── ISR stub (naked) ────────────────────────────────────────────────── */
__attribute__((naked)) static void _oo_kbd_stub(void) {
    __asm__ volatile(
        "push %rax\n push %rcx\n push %rdx\n push %rsi\n push %rdi\n"
        "push %r8\n  push %r9\n  push %r10\n push %r11\n"
        "call oo_kbd_isr_handler\n"
        "pop  %r11\n pop %r10\n  pop  %r9\n  pop  %r8\n"
        "pop  %rdi\n pop %rsi\n  pop  %rdx\n pop  %rcx\n pop %rax\n"
        "iretq\n"
    );
}

/* ─── 8259A PIC re-init ────────────────────────────────────────────────── */
static void _oo_pic_init(void) {
    UINT8 m1 = _inb_irq(PIC1_DATA), m2 = _inb_irq(PIC2_DATA);

    _outb_irq(PIC1_CMD,  ICW1_INIT); _io_wait_irq();
    _outb_irq(PIC2_CMD,  ICW1_INIT); _io_wait_irq();
    _outb_irq(PIC1_DATA, 0x20); _io_wait_irq();  /* IRQ0-7 → INT 32-39 */
    _outb_irq(PIC2_DATA, 0x28); _io_wait_irq();  /* IRQ8-15 → INT 40-47 */
    _outb_irq(PIC1_DATA, 0x04); _io_wait_irq();
    _outb_irq(PIC2_DATA, 0x02); _io_wait_irq();
    _outb_irq(PIC1_DATA, ICW4_8086); _io_wait_irq();
    _outb_irq(PIC2_DATA, ICW4_8086); _io_wait_irq();

    _outb_irq(PIC1_DATA, m1 & ~(UINT8)(1 << 1)); /* unmask IRQ1 */
    _outb_irq(PIC2_DATA, 0xFF);                   /* mask all slave IRQs */
}

/* ─── Public init — call AFTER oo_idt_install() ──────────────────────── */
void oo_irq_init(void) {
    _oo_pic_init();
    /* Install keyboard ISR at vector 33 (PIC1 base 32 + IRQ1) */
    oo_idt_set_gate(33, (UINT64)_oo_kbd_stub, 0x8E);
    __asm__ volatile("sti");
}

void oo_irq_mask(UINT8 irq) {
    UINT16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    _outb_irq(port, _inb_irq(port) | (UINT8)(1 << irq));
}

void oo_irq_unmask(UINT8 irq) {
    UINT16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    _outb_irq(port, _inb_irq(port) & (UINT8)~(1 << irq));
}

/* ─── Status + REPL ──────────────────────────────────────────────────────── */
void oo_irq_print_status(void) {
    UINT8 m1 = _inb_irq(PIC1_DATA), m2 = _inb_irq(PIC2_DATA);
    UINT32 fill = (_kbd_ring.head >= _kbd_ring.tail)
                ? (_kbd_ring.head - _kbd_ring.tail)
                : (KBD_RING_SIZE - _kbd_ring.tail + _kbd_ring.head);
    Print(L"\r\n  [IRQ/PIC Status]\r\n");
    Print(L"  PIC1 mask : 0x%02x (bit1=IRQ1/kbd=%s)\r\n",
          m1, (m1 & 2) ? L"masked" : L"active");
    Print(L"  PIC2 mask : 0x%02x\r\n", m2);
    Print(L"  KBD ring  : %u chars buffered\r\n", fill);
    Print(L"\r\n");
}

static int _irq_cstrcmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

int oo_irq_repl_cmd(const char *cmd) {
    if (!cmd) return 0;
    if (_irq_cstrcmp(cmd, "/irq_status", 11) == 0) {
        oo_irq_print_status(); return 1;
    }
    if (_irq_cstrcmp(cmd, "/irq_mask ", 10) == 0) {
        int irq = 0;
        const char *p = cmd + 10;
        while (*p >= '0' && *p <= '9') irq = irq * 10 + (*p++ - '0');
        oo_irq_mask((UINT8)irq);
        Print(L"[irq] Masked IRQ %d\r\n", irq); return 1;
    }
    if (_irq_cstrcmp(cmd, "/irq_unmask ", 12) == 0) {
        int irq = 0;
        const char *p = cmd + 12;
        while (*p >= '0' && *p <= '9') irq = irq * 10 + (*p++ - '0');
        oo_irq_unmask((UINT8)irq);
        Print(L"[irq] Unmasked IRQ %d\r\n", irq); return 1;
    }
    return 0;
}

