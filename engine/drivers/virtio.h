#ifndef OO_DRIVERS_VIRTIO_H
#define OO_DRIVERS_VIRTIO_H

#include <stdint.h>
#include "pci.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OO VirtIO Metadriver
 *
 * Provides block storage and network interfaces for VMs (QEMU/oo-sim).
 * VirtIO is simpler than AHCI for a bare-metal kernel.
 */

#define VIRTIO_VENDOR_ID 0x1AF4

// Device IDs
#define VIRTIO_DEV_NET   0x1000
#define VIRTIO_DEV_BLOCK 0x1001

int oo_virtio_probe(OoPciDevice *pci_dev);
int oo_virtio_blk_read(uint64_t sector, uint32_t count, void *buffer);
int oo_virtio_blk_write(uint64_t sector, uint32_t count, const void *buffer);

#ifdef __cplusplus
}
#endif

#endif
