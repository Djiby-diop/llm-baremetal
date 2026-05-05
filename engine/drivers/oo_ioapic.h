// oo_ioapic.h — IOAPIC + LAPIC Interrupt Routing Driver
//
// Programs the I/O APIC and Local APIC for proper interrupt routing in Ring 0.
//
// Replaces the 8259 PIC (legacy, limited to 15 IRQs).
// Enables:
//   - IRQ 0  → LAPIC vector 0x20 (PIT/HPET timer tick)
//   - IRQ 1  → LAPIC vector 0x21 (PS/2 keyboard)
//   - IRQ 8  → LAPIC vector 0x28 (RTC alarm)
//   - IRQ 9  → LAPIC vector 0x29 (ACPI SCI)
//   - IRQ 11 → LAPIC vector 0x2B (HDA audio / USB)
//
// LAPIC timer used for preemptive scheduling tick (1ms at 1GHz).
//
// Must be called AFTER oo_acpi_init() (needs LAPIC/IOAPIC bases from MADT).
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── LAPIC register offsets (MMIO, base = 0xFEE00000) ─────────────────────────
#define LAPIC_ID            0x020   // Local APIC ID
#define LAPIC_VER           0x030   // Version
#define LAPIC_TPR           0x080   // Task Priority Register
#define LAPIC_EOI           0x0B0   // End Of Interrupt (write 0)
#define LAPIC_LDR           0x0D0   // Logical Destination
#define LAPIC_DFR           0x0E0   // Destination Format
#define LAPIC_SVR           0x0F0   // Spurious Interrupt Vector Register
#define LAPIC_ISR0          0x100   // In-Service Register (8 × 32-bit)
#define LAPIC_TMR0          0x180   // Trigger Mode Register
#define LAPIC_IRR0          0x200   // Interrupt Request Register
#define LAPIC_ESR           0x280   // Error Status Register
#define LAPIC_ICR_LO        0x300   // Interrupt Command Register (low)
#define LAPIC_ICR_HI        0x310   // Interrupt Command Register (high)
#define LAPIC_TIMER_LVT     0x320   // Timer LVT entry
#define LAPIC_THERMAL_LVT   0x330
#define LAPIC_PERF_LVT      0x340
#define LAPIC_LINT0_LVT     0x350
#define LAPIC_LINT1_LVT     0x360
#define LAPIC_ERROR_LVT     0x370
#define LAPIC_TIMER_ICR     0x380   // Timer Initial Count Register
#define LAPIC_TIMER_CCR     0x390   // Timer Current Count Register
#define LAPIC_TIMER_DCR     0x3E0   // Timer Divide Configuration

// SVR flags
#define LAPIC_SVR_ENABLE    (1 << 8)
#define LAPIC_SVR_SPURIOUS  0xFF

// Timer modes
#define LAPIC_TIMER_ONESHOT  0x00000000
#define LAPIC_TIMER_PERIODIC 0x00020000
#define LAPIC_TIMER_MASKED   0x00010000

// ── IOAPIC register offsets (indirect via IOREGSEL + IOWIN) ──────────────────
#define IOAPIC_IOREGSEL     0x00    // Select register
#define IOAPIC_IOWIN        0x10    // Window (data)

#define IOAPIC_REG_ID       0x00
#define IOAPIC_REG_VER      0x01
#define IOAPIC_REG_ARB      0x02
#define IOAPIC_REG_REDTBL   0x10    // Redirection Table (2 × 32-bit per entry)

// Redirection Table Entry flags
#define IOAPIC_RTE_MASKED       (1ULL << 16)
#define IOAPIC_RTE_LEVEL        (1ULL << 15)
#define IOAPIC_RTE_ACTIVELOW    (1ULL << 13)
#define IOAPIC_RTE_LOGDEST      (1ULL << 11)
#define IOAPIC_RTE_MODE_FIXED   (0ULL << 8)
#define IOAPIC_RTE_MODE_NMI     (4ULL << 8)

// ── IRQ vector assignments ────────────────────────────────────────────────────
#define OO_IRQ_TIMER    0x20   // ISA IRQ 0
#define OO_IRQ_KEYBOARD 0x21   // ISA IRQ 1
#define OO_IRQ_CASCADE  0x22   // ISA IRQ 2 (not used in APIC mode)
#define OO_IRQ_UART     0x24   // ISA IRQ 4 (COM1)
#define OO_IRQ_RTC      0x28   // ISA IRQ 8
#define OO_IRQ_ACPI_SCI 0x29   // ISA IRQ 9
#define OO_IRQ_HDA      0x2B   // ISA IRQ 11 (HDA audio)
#define OO_IRQ_SPURIOUS 0xFF   // LAPIC spurious vector

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize LAPIC and IOAPIC
// lapic_base: physical/MMIO base of Local APIC (from ACPI MADT or 0xFEE00000)
// ioapic_base: physical/MMIO base of IOAPIC (from ACPI MADT or 0xFEC00000)
// disable_pic: 1 = mask/disable legacy 8259 PICs (recommended)
void oo_ioapic_init(uint64_t lapic_base, uint64_t ioapic_base, int disable_pic);

// Program a single IOAPIC redirection table entry
// gsi: Global System Interrupt number
// vector: IDT vector to deliver (0x20-0xFE)
// flags: IOAPIC_RTE_* bitmask (or 0 for edge-triggered active-high)
// dest_apic_id: LAPIC ID of destination CPU (usually 0 = BSP)
void oo_ioapic_map(uint64_t ioapic_base, uint32_t gsi, uint8_t vector,
                   uint64_t flags, uint8_t dest_apic_id);

// Mask (disable) a GSI
void oo_ioapic_mask(uint64_t ioapic_base, uint32_t gsi);

// Unmask (enable) a GSI
void oo_ioapic_unmask(uint64_t ioapic_base, uint32_t gsi);

// Signal End Of Interrupt to LAPIC (call from ISR before iretq)
void oo_lapic_eoi(void);

// Read LAPIC register (MMIO)
uint32_t oo_lapic_read(uint32_t reg);

// Write LAPIC register (MMIO)
void oo_lapic_write(uint32_t reg, uint32_t val);

// Start LAPIC timer in periodic mode
// ticks: initial count value (calibrate against HPET/PIT first)
// vector: IDT vector to fire
void oo_lapic_timer_start(uint32_t ticks, uint8_t vector);

// Stop LAPIC timer
void oo_lapic_timer_stop(void);

// Get LAPIC ID of current CPU
uint8_t oo_lapic_id(void);

// Send IPI to a specific LAPIC ID
void oo_lapic_ipi(uint8_t dest_id, uint8_t vector);

// Disable legacy 8259 PIC (masks all IRQs, remaps to 0xA0-0xAF)
void oo_pic_disable(void);

// Get current tick count (incremented by timer ISR)
uint64_t oo_ioapic_ticks(void);

// Increment tick counter (called from timer ISR)
void oo_ioapic_tick(void);

// Calibrate LAPIC timer using PIT 8254 (~10ms window).
// Returns LAPIC ticks per millisecond.
// Must be called AFTER oo_ioapic_init() (LAPIC must be enabled).
uint32_t oo_lapic_calibrate_ms(void);

// Busy-wait for 'us' microseconds using calibrated LAPIC timer.
// ticks_per_ms: return value of oo_lapic_calibrate_ms().
void oo_lapic_sleep_us(uint32_t ticks_per_ms, uint32_t us);

#ifdef __cplusplus
}
#endif
