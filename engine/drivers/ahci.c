#include "ahci.h"

/*
 * AHCI stub implementation.
 */

static uint32_t abar_base = 0;

int oo_ahci_probe(OoPciDevice *pci_dev) {
    if (!pci_dev) return 0;
    
    if (pci_dev->class_code == AHCI_PCI_CLASS && pci_dev->subclass == AHCI_PCI_SUBCLASS) {
        // Read ABAR (BAR5) to get Memory base
        uint32_t bar5 = oo_pci_read_config_32(pci_dev->bus, pci_dev->dev, pci_dev->func, 0x24);
        if (!(bar5 & 1)) { // Memory Space
            abar_base = bar5 & 0xFFFFFFF0;
            return 1; // Found AHCI controller
        }
    }
    return 0;
}

int oo_ahci_read(uint32_t port, uint32_t start_lba, uint32_t count, void *buf) {
    if (!abar_base) return -1;
    // Stub: send command FIS to AHCI port
    (void)port; (void)start_lba; (void)count; (void)buf;
    return 0;
}

int oo_ahci_write(uint32_t port, uint32_t start_lba, uint32_t count, const void *buf) {
    if (!abar_base) return -1;
    // Stub: send command FIS to AHCI port
    (void)port; (void)start_lba; (void)count; (void)buf;
    return 0;
}
