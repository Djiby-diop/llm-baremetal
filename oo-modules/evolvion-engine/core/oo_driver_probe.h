#pragma once

/*
 * OO Driver System: oo_driver_probe
 *
 * PCI bus enumeration at bare-metal boot (I/O ports 0xCF8/0xCFC).
 * Discovers all devices, classifies known ones, flags unknowns for
 * evolvion codegen — the LLM will generate a stub driver on demand.
 *
 * No OS. No BIOS tables. Just the PCI configuration mechanism #1.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OO_PCI_MAX_DEVICES 32

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  fn;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  known;       /* 1 = class recognized, 0 = needs driver */
} OoPciDevice;

typedef struct {
    OoPciDevice devices[OO_PCI_MAX_DEVICES];
    uint8_t     device_count;
    uint8_t     unknown_count;
    uint8_t     probed;       /* 1 after first probe */
} OoDriverProbe;

void oo_driver_probe_init(OoDriverProbe *p);

/* Scan PCI bus 0..7 via config mechanism #1. Fills devices[]. */
void oo_driver_probe_pci(OoDriverProbe *p);

/* Human-readable names for REPL/log display */
const char *oo_pci_class_name(uint8_t class_code, uint8_t subclass);
const char *oo_pci_vendor_name(uint16_t vendor_id);

#ifdef __cplusplus
}
#endif
