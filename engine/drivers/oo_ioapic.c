// oo_ioapic.c — IOAPIC + LAPIC Interrupt Routing (Implementation)
//
// Freestanding C11 — UEFI Ring 0.

#include "oo_ioapic.h"

// ── Globals ───────────────────────────────────────────────────────────────────

static uint64_t _lapic_base  = 0xFEE00000ULL;
static uint64_t _ioapic_base = 0xFEC00000ULL;
static volatile uint64_t _tick_count = 0;

// ── LAPIC MMIO ────────────────────────────────────────────────────────────────

uint32_t oo_lapic_read(uint32_t reg) {
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(_lapic_base + reg);
    return *p;
}

void oo_lapic_write(uint32_t reg, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(_lapic_base + reg);
    *p = val;
    // Read back to flush write buffer
    (void)oo_lapic_read(LAPIC_ID);
}

void oo_lapic_eoi(void) {
    oo_lapic_write(LAPIC_EOI, 0);
}

uint8_t oo_lapic_id(void) {
    return (uint8_t)(oo_lapic_read(LAPIC_ID) >> 24);
}

// ── IOAPIC indirect MMIO ──────────────────────────────────────────────────────

static uint32_t _ioapic_read(uint64_t base, uint8_t reg) {
    volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)(base + IOAPIC_IOREGSEL);
    volatile uint32_t *win = (volatile uint32_t *)(uintptr_t)(base + IOAPIC_IOWIN);
    *sel = reg;
    return *win;
}

static void _ioapic_write(uint64_t base, uint8_t reg, uint32_t val) {
    volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)(base + IOAPIC_IOREGSEL);
    volatile uint32_t *win = (volatile uint32_t *)(uintptr_t)(base + IOAPIC_IOWIN);
    *sel = reg;
    *win = val;
}

// Read 64-bit redirection table entry
static uint64_t _ioapic_read_rte(uint64_t base, uint32_t gsi) {
    uint8_t reg_lo = (uint8_t)(IOAPIC_REG_REDTBL + gsi * 2);
    uint8_t reg_hi = reg_lo + 1;
    uint64_t lo = _ioapic_read(base, reg_lo);
    uint64_t hi = _ioapic_read(base, reg_hi);
    return (hi << 32) | lo;
}

static void _ioapic_write_rte(uint64_t base, uint32_t gsi, uint64_t rte) {
    uint8_t reg_lo = (uint8_t)(IOAPIC_REG_REDTBL + gsi * 2);
    uint8_t reg_hi = reg_lo + 1;
    // Write high word first, then low (to avoid spurious delivery during update)
    _ioapic_write(base, reg_hi, (uint32_t)(rte >> 32));
    _ioapic_write(base, reg_lo, (uint32_t)(rte & 0xFFFFFFFF));
}

// ── Public IOAPIC API ─────────────────────────────────────────────────────────

void oo_ioapic_map(uint64_t ioapic_base, uint32_t gsi, uint8_t vector,
                   uint64_t flags, uint8_t dest_apic_id) {
    uint64_t rte = (uint64_t)vector
                 | IOAPIC_RTE_MODE_FIXED
                 | flags
                 | ((uint64_t)dest_apic_id << 56);  // destination field
    _ioapic_write_rte(ioapic_base, gsi, rte);
}

void oo_ioapic_mask(uint64_t ioapic_base, uint32_t gsi) {
    uint64_t rte = _ioapic_read_rte(ioapic_base, gsi);
    rte |= IOAPIC_RTE_MASKED;
    _ioapic_write_rte(ioapic_base, gsi, rte);
}

void oo_ioapic_unmask(uint64_t ioapic_base, uint32_t gsi) {
    uint64_t rte = _ioapic_read_rte(ioapic_base, gsi);
    rte &= ~IOAPIC_RTE_MASKED;
    _ioapic_write_rte(ioapic_base, gsi, rte);
}

// ── Legacy 8259 PIC disable ───────────────────────────────────────────────────

void oo_pic_disable(void) {
    // Remap PIC1 to 0xA0 and PIC2 to 0xA8 (avoid conflict with CPU exceptions)
    // Then mask all IRQs

    // ICW1: init
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0x20));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0xA0));
    // ICW2: vector offset
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xA0), "Nd"((uint16_t)0x21)); // master → 0xA0
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xA8), "Nd"((uint16_t)0xA1)); // slave → 0xA8
    // ICW3
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x04), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x02), "Nd"((uint16_t)0xA1));
    // ICW4: 8086 mode
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0xA1));
    // OCW1: mask ALL IRQs
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0xA1));
}

// ── LAPIC timer ───────────────────────────────────────────────────────────────

void oo_lapic_timer_start(uint32_t ticks, uint8_t vector) {
    // Divide by 16
    oo_lapic_write(LAPIC_TIMER_DCR, 0x3);
    // Set LVT: periodic mode, vector
    oo_lapic_write(LAPIC_TIMER_LVT, (uint32_t)(LAPIC_TIMER_PERIODIC | vector));
    // Set initial count (starts immediately)
    oo_lapic_write(LAPIC_TIMER_ICR, ticks);
}

void oo_lapic_timer_stop(void) {
    oo_lapic_write(LAPIC_TIMER_LVT, LAPIC_TIMER_MASKED);
    oo_lapic_write(LAPIC_TIMER_ICR, 0);
}

// ── IPI ───────────────────────────────────────────────────────────────────────

void oo_lapic_ipi(uint8_t dest_id, uint8_t vector) {
    // Write destination first
    oo_lapic_write(LAPIC_ICR_HI, (uint32_t)dest_id << 24);
    // Then command (triggers delivery)
    oo_lapic_write(LAPIC_ICR_LO, (uint32_t)vector | 0x00004000);  // fixed, assert
}

// ── Tick counter ──────────────────────────────────────────────────────────────

uint64_t oo_ioapic_ticks(void) {
    return _tick_count;
}

void oo_ioapic_tick(void) {
    _tick_count++;
}

// ── Main init ─────────────────────────────────────────────────────────────────

void oo_ioapic_init(uint64_t lapic_base, uint64_t ioapic_base, int disable_pic) {
    _lapic_base  = lapic_base;
    _ioapic_base = ioapic_base;

    // 1. Disable legacy 8259 PIC
    if (disable_pic) oo_pic_disable();

    // 2. Enable LAPIC:
    //    - Set Spurious Interrupt Vector Register (SVR): enable + vector 0xFF
    oo_lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SVR_SPURIOUS);
    //    - Clear Task Priority Register (accept all interrupts)
    oo_lapic_write(LAPIC_TPR, 0);
    //    - Acknowledge any pending interrupts
    oo_lapic_write(LAPIC_EOI, 0);
    //    - Mask LINT0/LINT1 (handled by IOAPIC)
    oo_lapic_write(LAPIC_LINT0_LVT, LAPIC_TIMER_MASKED);
    oo_lapic_write(LAPIC_LINT1_LVT, LAPIC_TIMER_MASKED);
    //    - Mask error/thermal LVTs
    oo_lapic_write(LAPIC_ERROR_LVT,   LAPIC_TIMER_MASKED | 0xFE);
    oo_lapic_write(LAPIC_THERMAL_LVT, LAPIC_TIMER_MASKED);
    oo_lapic_write(LAPIC_PERF_LVT,    LAPIC_TIMER_MASKED);

    // 3. Program IOAPIC redirection table:
    //    Mask everything first
    uint32_t max_rte = (_ioapic_read(ioapic_base, IOAPIC_REG_VER) >> 16) & 0xFF;
    for (uint32_t i = 0; i <= max_rte; i++) {
        oo_ioapic_mask(ioapic_base, i);
    }

    uint8_t bsp_id = oo_lapic_id();

    // IRQ 0: PIT timer → vector 0x20 (edge, active-high)
    oo_ioapic_map(ioapic_base, 0,  OO_IRQ_TIMER,    0, bsp_id);

    // IRQ 1: PS/2 keyboard → vector 0x21 (edge, active-high)
    oo_ioapic_map(ioapic_base, 1,  OO_IRQ_KEYBOARD, 0, bsp_id);

    // IRQ 4: UART COM1 → vector 0x24
    oo_ioapic_map(ioapic_base, 4,  OO_IRQ_UART, 0, bsp_id);

    // IRQ 8: RTC → vector 0x28 (level, active-high for CMOS RTC alarm)
    oo_ioapic_map(ioapic_base, 8,  OO_IRQ_RTC,
                  IOAPIC_RTE_LEVEL, bsp_id);

    // IRQ 9: ACPI SCI → vector 0x29 (level, active-low per ACPI spec)
    oo_ioapic_map(ioapic_base, 9,  OO_IRQ_ACPI_SCI,
                  IOAPIC_RTE_LEVEL | IOAPIC_RTE_ACTIVELOW, bsp_id);

    // IRQ 11: HDA audio / USB → vector 0x2B (level, active-low PCI)
    oo_ioapic_map(ioapic_base, 11, OO_IRQ_HDA,
                  IOAPIC_RTE_LEVEL | IOAPIC_RTE_ACTIVELOW, bsp_id);

    // Unmask the ones we care about
    oo_ioapic_unmask(ioapic_base, 0);   // timer
    oo_ioapic_unmask(ioapic_base, 1);   // keyboard
    oo_ioapic_unmask(ioapic_base, 8);   // RTC
    oo_ioapic_unmask(ioapic_base, 9);   // ACPI SCI
    oo_ioapic_unmask(ioapic_base, 11);  // HDA

    // 4. Enable hardware interrupts
    __asm__ __volatile__("sti");
}
