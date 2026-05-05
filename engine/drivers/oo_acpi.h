// oo_acpi.h — ACPI Table Walker (RSDP → RSDT/XSDT → FADT → MADT)
//
// Goals:
//   1. Locate RSDP via UEFI SystemTable or legacy scan 0xE0000-0xFFFFF
//   2. Parse RSDT/XSDT → find FADT (shutdown) and MADT (IRQ topology)
//   3. Parse FADT: PM1a_CNT_BLK + SLP_TYP → ACPI S5 shutdown
//   4. Parse MADT: extract LAPIC base + IOAPIC base + all CPU APIC IDs
//
// NOT included: AML interpreter, DSDT evaluation, power states beyond S5
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── ACPI generic table header ─────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} AcpiHeader;

// ── RSDP (Root System Description Pointer) ───────────────────────────────────
typedef struct __attribute__((packed)) {
    char     signature[8];  // "RSD PTR "
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;  // physical address (32-bit)
    // ACPI 2.0+ only:
    uint32_t length;
    uint64_t xsdt_address;  // physical address (64-bit)
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} AcpiRsdp;

// ── FADT (Fixed ACPI Description Table) — key fields only ────────────────────
typedef struct __attribute__((packed)) {
    AcpiHeader header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  _reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;   // ← S5 shutdown: write SLP_TYPa | SLP_EN here
    uint32_t pm1b_cnt_blk;
} AcpiFadt;

// ── MADT (Multiple APIC Description Table) ───────────────────────────────────
typedef struct __attribute__((packed)) {
    AcpiHeader header;
    uint32_t local_apic_address;  // default LAPIC base (usually 0xFEE00000)
    uint32_t flags;               // bit 0: 8259 PICs installed
} AcpiMadt;

// MADT entry header
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} AcpiMadtEntry;

// MADT type 0: Local APIC (one per CPU core)
#define ACPI_MADT_LAPIC    0
typedef struct __attribute__((packed)) {
    AcpiMadtEntry hdr;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;  // bit 0: enabled
} AcpiMadtLapic;

// MADT type 1: I/O APIC
#define ACPI_MADT_IOAPIC   1
typedef struct __attribute__((packed)) {
    AcpiMadtEntry hdr;
    uint8_t  ioapic_id;
    uint8_t  _reserved;
    uint32_t ioapic_address;  // MMIO base (usually 0xFEC00000)
    uint32_t gsi_base;        // Global System Interrupt base
} AcpiMadtIoapic;

// MADT type 2: Interrupt Source Override
#define ACPI_MADT_ISO      2
typedef struct __attribute__((packed)) {
    AcpiMadtEntry hdr;
    uint8_t  bus;
    uint8_t  source;       // ISA IRQ number
    uint32_t gsi;          // mapped GSI
    uint16_t flags;        // polarity + trigger mode
} AcpiMadtIso;

// ── Parsed ACPI topology ──────────────────────────────────────────────────────
#define OO_ACPI_MAX_CPUS     16
#define OO_ACPI_MAX_IOAPICS   4
#define OO_ACPI_MAX_ISOS     16

typedef struct {
    // FADT power management
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint8_t  slp_typ_s5;   // SLP_TYP value for S5 (from \_S5_ object — hardcoded 5)

    // LAPIC
    uint64_t lapic_base;   // MMIO base of Local APIC (identity-mapped in UEFI)

    // I/O APICs
    int      ioapic_count;
    struct {
        uint8_t  id;
        uint64_t base;
        uint32_t gsi_base;
    } ioapics[OO_ACPI_MAX_IOAPICS];

    // CPU APIC IDs
    int      cpu_count;
    uint8_t  cpu_apic_ids[OO_ACPI_MAX_CPUS];

    // Interrupt overrides (ISA IRQ → GSI remapping)
    int      iso_count;
    struct {
        uint8_t  isa_irq;
        uint32_t gsi;
        uint16_t flags;
    } isos[OO_ACPI_MAX_ISOS];

    // Status
    int      initialized;
    int      has_fadt;
    int      has_madt;
} OoAcpiInfo;

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize ACPI — call once at UEFI boot phase
// uefi_rsdp: RSDP pointer from UEFI SystemTable (EFI_ACPI_20_TABLE_GUID)
//            Pass NULL to trigger legacy memory scan
// Returns 0 on success, -1 on failure
int oo_acpi_init(void *uefi_rsdp);

// Get parsed ACPI topology (valid after oo_acpi_init returns 0)
const OoAcpiInfo *oo_acpi_get(void);

// Perform ACPI S5 shutdown (writes PM1a_CNT with SLP_TYP_S5 | SLP_EN)
// Does not return on success
void oo_acpi_shutdown(void);

// Perform ACPI reboot (via Reset Register or port 0xCF9)
void oo_acpi_reboot(void);

// GSI lookup: given an ISA IRQ, return the real GSI (accounts for overrides)
uint32_t oo_acpi_isa_gsi(int isa_irq);

// Dump ACPI info to debug output (UART / early print)
void oo_acpi_dump(void);

#ifdef __cplusplus
}
#endif
