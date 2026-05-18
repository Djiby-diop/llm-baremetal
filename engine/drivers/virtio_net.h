#ifndef OO_DRIVERS_VIRTIO_NET_H
#define OO_DRIVERS_VIRTIO_NET_H

#include <stdint.h>
#include "pci.h"

// Initialize VirtIO Network device
int oo_virtio_net_init(OoPciDevice *pci_dev);

// Send a packet via VirtIO
int oo_virtio_net_send(void *data, uint16_t len);

// Receive a packet via VirtIO
int oo_virtio_net_receive(void *buffer, uint16_t *len);

// Get MAC address
void oo_virtio_net_get_mac(uint8_t *mac);

#endif
