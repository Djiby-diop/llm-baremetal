#include "pci.h"

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t oo_pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void oo_pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

int oo_pci_enumerate(OoPciDevice *out_devices, int max_devices) {
    int count = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id_reg = oo_pci_read_config_32(bus, dev, 0, 0x00);
            uint16_t vendor = id_reg & 0xFFFF;
            if (vendor != 0xFFFF) {
                // Device exists
                for (uint8_t func = 0; func < 8; func++) {
                    id_reg = oo_pci_read_config_32(bus, dev, func, 0x00);
                    if ((id_reg & 0xFFFF) != 0xFFFF) {
                        uint32_t class_reg = oo_pci_read_config_32(bus, dev, func, 0x08);
                        if (count < max_devices) {
                            out_devices[count].bus = bus;
                            out_devices[count].dev = dev;
                            out_devices[count].func = func;
                            out_devices[count].vendor_id = id_reg & 0xFFFF;
                            out_devices[count].device_id = (id_reg >> 16) & 0xFFFF;
                            out_devices[count].class_code = (class_reg >> 24) & 0xFF;
                            out_devices[count].subclass = (class_reg >> 16) & 0xFF;
                            count++;
                        }
                    }
                }
            }
        }
    }
    return count;
}
