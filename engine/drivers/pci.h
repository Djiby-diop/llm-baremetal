#ifndef OO_DRIVERS_PCI_H
#define OO_DRIVERS_PCI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OO PCI Bus Metadriver (Spinal Cord)
 * Permet à l'Organisme de découvrir ses propres organes matériels.
 */

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
} OoPciDevice;

uint32_t oo_pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void oo_pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

/* Scanne le bus PCI et trouve jusqu'à max_devices. Retourne le nombre trouvé. */
int oo_pci_enumerate(OoPciDevice *out_devices, int max_devices);

#ifdef __cplusplus
}
#endif

#endif
