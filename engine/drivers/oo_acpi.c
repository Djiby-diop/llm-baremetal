// oo_acpi.c — ACPI Table Walker (Implementation)
//
// Freestanding C11 — no libc, no malloc, UEFI Ring 0.

#include "oo_acpi.h"

// ── I/O port access ───────────────────────────────────────────────────────────

static inline void _acpi_outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void _acpi_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void _acpi_io_delay(void) { _acpi_outb(0x80, 0); }

// ── Memory helpers (physical = identity-mapped in UEFI) ───────────────────────

static inline void *_phys(uint64_t addr) { return (void *)(uintptr_t)addr; }
static inline void *_phys32(uint32_t addr) { return (void *)(uintptr_t)addr; }

// ── String / memory helpers ───────────────────────────────────────────────────

static int _acpi_memcmp(const void *a, const void *b, int n) {
    const unsigned char *p = a, *q = b;
    for (int i = 0; i < n; i++) {
        if (p[i] < q[i]) return -1;
        if (p[i] > q[i]) return  1;
    }
    return 0;
}

static uint8_t _acpi_checksum(const void *ptr, int len) {
    const uint8_t *p = ptr;
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += p[i];
    return sum;
}

// ── Global parsed state ───────────────────────────────────────────────────────

static OoAcpiInfo _g_acpi;

// ── RSDP discovery ────────────────────────────────────────────────────────────

static AcpiRsdp *_find_rsdp_scan(uint64_t start, uint64_t end) {
    for (uint64_t addr = start; addr < end; addr += 16) {
        AcpiRsdp *r = _phys(addr);
        if (_acpi_memcmp(r->signature, "RSD PTR ", 8) == 0) {
            if (_acpi_checksum(r, 20) == 0) return r;
        }
    }
    return (AcpiRsdp *)0;
}

// ── FADT parsing ──────────────────────────────────────────────────────────────

static void _parse_fadt(const AcpiHeader *hdr) {
    const AcpiFadt *f = (const AcpiFadt *)hdr;
    if (hdr->length < sizeof(AcpiFadt)) return;

    _g_acpi.pm1a_cnt_blk = f->pm1a_cnt_blk;
    _g_acpi.pm1b_cnt_blk = f->pm1b_cnt_blk;
    // SLP_TYP for S5: ACPI spec says it comes from \_S5_ DSDT object.
    // Without AML evaluation, we use the conventional value: 5 (binary 101b << 10)
    _g_acpi.slp_typ_s5 = 5;
    _g_acpi.has_fadt = 1;
}

// ── MADT parsing ──────────────────────────────────────────────────────────────

static void _parse_madt(const AcpiHeader *hdr) {
    const AcpiMadt *m = (const AcpiMadt *)hdr;
    _g_acpi.lapic_base = (uint64_t)m->local_apic_address;
    _g_acpi.has_madt = 1;

    // Walk MADT entries
    uint32_t offset = sizeof(AcpiMadt);
    while (offset < hdr->length) {
        const AcpiMadtEntry *entry =
            (const AcpiMadtEntry *)((const uint8_t *)hdr + offset);
        if (entry->length < 2) break;

        switch (entry->type) {
        case ACPI_MADT_LAPIC: {
            const AcpiMadtLapic *l = (const AcpiMadtLapic *)entry;
            if ((l->flags & 1) && _g_acpi.cpu_count < OO_ACPI_MAX_CPUS) {
                _g_acpi.cpu_apic_ids[_g_acpi.cpu_count++] = l->apic_id;
            }
            break;
        }
        case ACPI_MADT_IOAPIC: {
            const AcpiMadtIoapic *io = (const AcpiMadtIoapic *)entry;
            if (_g_acpi.ioapic_count < OO_ACPI_MAX_IOAPICS) {
                int i = _g_acpi.ioapic_count++;
                _g_acpi.ioapics[i].id       = io->ioapic_id;
                _g_acpi.ioapics[i].base     = (uint64_t)io->ioapic_address;
                _g_acpi.ioapics[i].gsi_base = io->gsi_base;
            }
            break;
        }
        case ACPI_MADT_ISO: {
            const AcpiMadtIso *iso = (const AcpiMadtIso *)entry;
            if (_g_acpi.iso_count < OO_ACPI_MAX_ISOS) {
                int i = _g_acpi.iso_count++;
                _g_acpi.isos[i].isa_irq = iso->source;
                _g_acpi.isos[i].gsi     = iso->gsi;
                _g_acpi.isos[i].flags   = iso->flags;
            }
            break;
        }
        default: break;
        }

        offset += entry->length;
    }
}

// ── Walk RSDT / XSDT ──────────────────────────────────────────────────────────

static void _walk_rsdt(uint32_t rsdt_phys) {
    const AcpiHeader *rsdt = _phys32(rsdt_phys);
    if (_acpi_memcmp(rsdt->signature, "RSDT", 4) != 0) return;
    if (_acpi_checksum(rsdt, rsdt->length) != 0) return;

    int entries = (rsdt->length - sizeof(AcpiHeader)) / 4;
    const uint32_t *ptrs = (const uint32_t *)((const uint8_t *)rsdt + sizeof(AcpiHeader));

    for (int i = 0; i < entries; i++) {
        const AcpiHeader *tbl = _phys32(ptrs[i]);
        if (!tbl) continue;
        if (_acpi_checksum(tbl, tbl->length) != 0) continue;

        if (_acpi_memcmp(tbl->signature, "FACP", 4) == 0) _parse_fadt(tbl);
        if (_acpi_memcmp(tbl->signature, "APIC", 4) == 0) _parse_madt(tbl);
    }
}

static void _walk_xsdt(uint64_t xsdt_phys) {
    const AcpiHeader *xsdt = _phys(xsdt_phys);
    if (_acpi_memcmp(xsdt->signature, "XSDT", 4) != 0) return;
    if (_acpi_checksum(xsdt, xsdt->length) != 0) return;

    int entries = (xsdt->length - sizeof(AcpiHeader)) / 8;
    const uint64_t *ptrs = (const uint64_t *)((const uint8_t *)xsdt + sizeof(AcpiHeader));

    for (int i = 0; i < entries; i++) {
        const AcpiHeader *tbl = _phys(ptrs[i]);
        if (!tbl) continue;
        if (_acpi_checksum(tbl, tbl->length) != 0) continue;

        if (_acpi_memcmp(tbl->signature, "FACP", 4) == 0) _parse_fadt(tbl);
        if (_acpi_memcmp(tbl->signature, "APIC", 4) == 0) _parse_madt(tbl);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

int oo_acpi_init(void *uefi_rsdp) {
    // Zero state
    for (int i = 0; i < (int)sizeof(_g_acpi); i++) ((uint8_t *)&_g_acpi)[i] = 0;
    // Default LAPIC address (ACPI spec default)
    _g_acpi.lapic_base = 0xFEE00000ULL;

    AcpiRsdp *rsdp = (AcpiRsdp *)uefi_rsdp;

    // Fallback: legacy memory scan if UEFI didn't give us the pointer
    if (!rsdp) {
        rsdp = _find_rsdp_scan(0xE0000, 0x100000);
    }
    if (!rsdp) return -1;

    // Validate RSDP
    if (_acpi_memcmp(rsdp->signature, "RSD PTR ", 8) != 0) return -1;
    if (_acpi_checksum(rsdp, 20) != 0) return -1;

    // Prefer XSDT (ACPI 2.0+) over RSDT
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        _walk_xsdt(rsdp->xsdt_address);
    } else if (rsdp->rsdt_address != 0) {
        _walk_rsdt(rsdp->rsdt_address);
    } else {
        return -1;
    }

    _g_acpi.initialized = 1;
    return 0;
}

const OoAcpiInfo *oo_acpi_get(void) {
    return &_g_acpi;
}

// ── ACPI S5 Shutdown ──────────────────────────────────────────────────────────

void oo_acpi_shutdown(void) {
    if (!_g_acpi.has_fadt || _g_acpi.pm1a_cnt_blk == 0) {
        // Fallback: QEMU/bochs magic port
        _acpi_outw(0x604, 0x2000);  // QEMU
        _acpi_outw(0xB004, 0x2000); // Bochs/VirtualBox
        for(;;) __asm__ __volatile__("hlt");
    }

    // SLP_TYPa = slp_typ_s5 << 10, SLP_EN = bit 13
    uint16_t slp_val = (uint16_t)((_g_acpi.slp_typ_s5 << 10) | (1 << 13));
    _acpi_outw((uint16_t)_g_acpi.pm1a_cnt_blk, slp_val);
    if (_g_acpi.pm1b_cnt_blk)
        _acpi_outw((uint16_t)_g_acpi.pm1b_cnt_blk, slp_val);

    // If we're still here, CPU halt
    for(;;) __asm__ __volatile__("hlt");
}

// ── ACPI Reboot ───────────────────────────────────────────────────────────────

void oo_acpi_reboot(void) {
    // Keyboard controller reset (most reliable)
    for (int i = 0; i < 1000; i++) {
        uint8_t status;
        __asm__ __volatile__("inb $0x64, %0" : "=a"(status));
        if (!(status & 0x02)) break;
        _acpi_io_delay();
    }
    _acpi_outb(0x64, 0xFE);  // pulse reset line
    _acpi_io_delay();
    // Fallback: CF9 full reset
    _acpi_outb(0xCF9, 0x06);
    for(;;) __asm__ __volatile__("hlt");
}

// ── ISA → GSI mapping ────────────────────────────────────────────────────────

uint32_t oo_acpi_isa_gsi(int isa_irq) {
    // Check interrupt source overrides
    for (int i = 0; i < _g_acpi.iso_count; i++) {
        if (_g_acpi.isos[i].isa_irq == (uint8_t)isa_irq)
            return _g_acpi.isos[i].gsi;
    }
    // Default: ISA IRQ = GSI (identity)
    return (uint32_t)isa_irq;
}

// ── Debug dump ───────────────────────────────────────────────────────────────

// Minimal print via UART (COM1)
static void _acpi_putc(char c) {
    for (int i = 0; i < 10000; i++) {
        uint8_t s; __asm__ __volatile__("inb $0x3F8+5, %0" : "=a"(s));
        if (s & 0x20) break;
    }
    _acpi_outb(0x3F8, (uint8_t)c);
}

static void _acpi_print(const char *s) {
    while (*s) _acpi_putc(*s++);
}

static void _acpi_print_hex(uint64_t v) {
    _acpi_print("0x");
    char buf[17]; buf[16] = 0;
    for (int i = 15; i >= 0; i--) {
        int d = (int)(v & 0xF);
        buf[i] = d < 10 ? (char)('0' + d) : (char)('a' + d - 10);
        v >>= 4;
    }
    // Skip leading zeros
    int s = 0; while (s < 15 && buf[s] == '0') s++;
    _acpi_print(buf + s);
}

static void _acpi_print_dec(uint32_t v) {
    if (v == 0) { _acpi_putc('0'); return; }
    char buf[10]; int i = 0;
    while (v > 0) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) _acpi_putc(buf[j]);
}

void oo_acpi_dump(void) {
    _acpi_print("[ACPI] ");
    if (!_g_acpi.initialized) { _acpi_print("NOT INITIALIZED\n"); return; }
    _acpi_print("OK\n");
    _acpi_print("[ACPI] CPUs: "); _acpi_print_dec(_g_acpi.cpu_count); _acpi_putc('\n');
    _acpi_print("[ACPI] LAPIC base: "); _acpi_print_hex(_g_acpi.lapic_base); _acpi_putc('\n');
    _acpi_print("[ACPI] IOAPICs: "); _acpi_print_dec(_g_acpi.ioapic_count); _acpi_putc('\n');
    for (int i = 0; i < _g_acpi.ioapic_count; i++) {
        _acpi_print("  ["); _acpi_print_dec(i); _acpi_print("] base=");
        _acpi_print_hex(_g_acpi.ioapics[i].base);
        _acpi_print(" gsi_base="); _acpi_print_dec(_g_acpi.ioapics[i].gsi_base);
        _acpi_putc('\n');
    }
    _acpi_print("[ACPI] FADT PM1a="); _acpi_print_hex(_g_acpi.pm1a_cnt_blk); _acpi_putc('\n');
    _acpi_print("[ACPI] IRQ overrides: "); _acpi_print_dec(_g_acpi.iso_count); _acpi_putc('\n');
}
