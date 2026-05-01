#include "virtio.h"

/*
 * VirtIO stub implementation.
 * In a full baremetal environment, this would set up the virtqueues
 * and handle MMIO/PIO communication with the hypervisor.
 */

static uint32_t virtio_io_base = 0;

int oo_virtio_probe(OoPciDevice *pci_dev) {
    if (!pci_dev) return 0;
    
    if (pci_dev->vendor_id == VIRTIO_VENDOR_ID) {
        if (pci_dev->device_id == VIRTIO_DEV_BLOCK) {
            // Read BAR0 to get I/O base
            uint32_t bar0 = oo_pci_read_config_32(pci_dev->bus, pci_dev->dev, pci_dev->func, 0x10);
            if (bar0 & 1) { // I/O Space
                virtio_io_base = bar0 & 0xFFFFFFFC;
                return 1; // Found block device
            }
        }
    }
    return 0;
}

int oo_virtio_blk_read(uint64_t sector, uint32_t count, void *buffer) {
    if (!virtio_io_base) return -1;
    // Stub: Virtqueue submission goes here
    (void)sector; (void)count; (void)buffer;
    return 0;
}

int oo_virtio_blk_write(uint64_t sector, uint32_t count, const void *buffer) {
    if (!virtio_io_base) return -1;
    // Stub: Virtqueue submission goes here
    (void)sector; (void)count; (void)buffer;
    return 0;
}
