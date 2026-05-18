#ifndef OO_DRIVERS_E1000_H
#define OO_DRIVERS_E1000_H

#include "pci.h"

// Initialize the E1000 card
void oo_e1000_init(OoPciDevice *dev);

// Get link status
int oo_e1000_status(void);

// Send a raw network packet
int oo_e1000_send(void *data, uint16_t len);

// Poll for a received packet (Zero-Jitter mode)
int oo_e1000_receive(void *buffer, uint16_t *len);

// Get the MAC address of the card
void oo_e1000_get_mac(uint8_t *mac);

#endif
