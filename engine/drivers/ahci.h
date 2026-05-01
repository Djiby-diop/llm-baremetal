#ifndef OO_DRIVERS_AHCI_H
#define OO_DRIVERS_AHCI_H

#include <stdint.h>
#include "pci.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OO AHCI Metadriver (SATA Disk)
 * Permet au système NeuralFS d'accéder aux disques durs et SSD SATA en bare-metal.
 */

#define AHCI_PCI_CLASS 0x01
#define AHCI_PCI_SUBCLASS 0x06

int oo_ahci_probe(OoPciDevice *pci_dev);
int oo_ahci_read(uint32_t port, uint32_t start_lba, uint32_t count, void *buf);
int oo_ahci_write(uint32_t port, uint32_t start_lba, uint32_t count, const void *buf);

#ifdef __cplusplus
}
#endif

#endif
