/*
 * OO Driver System: PCI bus probe (bare-metal, no OS).
 *
 * Uses PCI configuration mechanism #1:
 *   - Write 32-bit address to 0xCF8 (config address port)
 *   - Read 32-bit data from 0xCFC (config data port)
 *
 * Address format: bit31=enable | bus[23:16] | dev[15:11] | fn[10:8] | reg[7:2]
 *
 * On QEMU/real HW this always works. USB-only or locked-down HW may
 * return 0xFFFFFFFF — those entries are silently skipped.
 */

#include "oo_driver_probe.h"

/* ---------- I/O port helpers (inline asm, freestanding) ---------- */

static inline void pci_outl(unsigned short port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint32_t pci_inl(unsigned short port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)fn  <<  8)
                  | (reg & 0xFCu);
    pci_outl(0xCF8, addr);
    return pci_inl(0xCFC);
}

/* ---------- Public API ------------------------------------------ */

void oo_driver_probe_init(OoDriverProbe *p) {
    if (!p) return;
    for (int i = 0; i < OO_PCI_MAX_DEVICES; i++)
        p->devices[i].vendor_id = 0xFFFF;
    p->device_count  = 0;
    p->unknown_count = 0;
    p->probed        = 0;
}

void oo_driver_probe_pci(OoDriverProbe *p) {
    if (!p) return;
    p->device_count  = 0;
    p->unknown_count = 0;

    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0x00);
            /* 0xFFFF vendor = absent slot */
            if ((id & 0xFFFFu) == 0xFFFFu || id == 0) continue;

            uint16_t vendor_id = (uint16_t)(id & 0xFFFFu);
            uint16_t device_id = (uint16_t)((id >> 16) & 0xFFFFu);

            uint32_t class_reg = pci_read32(bus, dev, 0, 0x08);
            uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
            uint8_t subclass   = (uint8_t)((class_reg >> 16) & 0xFFu);

            if (p->device_count >= OO_PCI_MAX_DEVICES) goto done;

            OoPciDevice *d     = &p->devices[p->device_count++];
            d->bus       = bus;
            d->dev       = dev;
            d->fn        = 0;
            d->vendor_id = vendor_id;
            d->device_id = device_id;
            d->class_code = class_code;
            d->subclass   = subclass;
            /* known = class string doesn't start with '?' */
            d->known = (oo_pci_class_name(class_code, subclass)[0] != '?') ? 1 : 0;
            if (!d->known) p->unknown_count++;
        }
    }
done:
    p->probed = 1;
}

/* ---------- Class / vendor name tables -------------------------- */

const char *oo_pci_vendor_name(uint16_t vid) {
    switch (vid) {
        case 0x8086: return "Intel";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1234: return "QEMU/Bochs";
        case 0x1AF4: return "VirtIO";
        case 0x1B36: return "QEMU PCI";
        case 0x10EC: return "Realtek";
        case 0x168C: return "Qualcomm/Atheros";
        case 0x14E4: return "Broadcom";
        case 0x1000: return "LSI Logic";
        case 0x15AD: return "VMware";
        default:     return "Unknown";
    }
}

const char *oo_pci_class_name(uint8_t cc, uint8_t sc) {
    switch (cc) {
        case 0x00: return sc == 0x01 ? "VGA-compat"  : "Legacy";
        case 0x01:
            switch (sc) {
                case 0x00: return "SCSI";
                case 0x01: return "IDE";
                case 0x05: return "ATA";
                case 0x06: return "SATA/AHCI";
                case 0x08: return "NVMe";
                default:   return "Storage";
            }
        case 0x02:
            return sc == 0x00 ? "Ethernet" : "Network";
        case 0x03:
            switch (sc) {
                case 0x00: return "VGA";
                case 0x01: return "XGA";
                case 0x02: return "3D/GPU";
                default:   return "Display";
            }
        case 0x04: return "Multimedia";
        case 0x05: return "RAM/Memory";
        case 0x06:
            switch (sc) {
                case 0x00: return "Host Bridge";
                case 0x01: return "ISA Bridge";
                case 0x04: return "PCI-PCI Bridge";
                case 0x80: return "Other Bridge";
                default:   return "Bridge";
            }
        case 0x07:
            return sc == 0x00 ? "Serial" : "Comm";
        case 0x08:
            switch (sc) {
                case 0x00: return "PIC";
                case 0x01: return "DMA";
                case 0x02: return "Timer";
                case 0x03: return "RTC";
                default:   return "System";
            }
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C:
            switch (sc) {
                case 0x00: return "FireWire";
                case 0x03: return "USB";
                case 0x05: return "SMBus";
                case 0x0A: return "InfiniBand";
                default:   return "Serial Bus";
            }
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Crypto";
        case 0x11: return "Signal Processing";
        case 0x12: return "Processing Accel";
        case 0x13: return "Non-essential";
        default:   return "?Unknown";
    }
}
