#include "oo_swarm_packet.h"
#include "../drivers/e1000.h"
#include "../drivers/virtio_net.h"
#include "uart.h"
#include <string.h>

#define ETHER_TYPE_OO_SWARM 0xAA00

// Raw Ethernet Frame Header
struct eth_hdr {
    uint8_t  dest[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

static uint8_t my_mac[6] = {0};
static int active_driver = 0; // 1 = E1000, 2 = VirtIO-Net

void oo_swarm_packet_init(void) {
    // Try to get MAC from E1000 first
    oo_e1000_get_mac(my_mac);
    if (my_mac[0] != 0 || my_mac[1] != 0) {
        active_driver = 1;
        soma_uart_puts("[Swarm-Packet] E1000 driver selected for transport.\r\n");
        return;
    }

    // Fallback to VirtIO-Net
    oo_virtio_net_get_mac(my_mac);
    if (my_mac[0] != 0 || my_mac[1] != 0) {
        active_driver = 2;
        soma_uart_puts("[Swarm-Packet] VirtIO-Net driver selected for transport.\r\n");
        return;
    }

    soma_uart_puts("[Swarm-Packet] Warning: No active network driver found for swarm.\r\n");
}

int oo_swarm_packet_broadcast(const SomaSwarmNetSlot *slot) {
    if (!active_driver || !slot) return -1;

    uint8_t buffer[14 + 512]; // Ethernet Header + Slot
    struct eth_hdr *eth = (struct eth_hdr *)buffer;

    // Broadcast address
    memset(eth->dest, 0xFF, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = ((ETHER_TYPE_OO_SWARM & 0xFF) << 8) | (ETHER_TYPE_OO_SWARM >> 8); // Big Endian

    // Payload
    memcpy(buffer + 14, slot, 512);

    if (active_driver == 1) {
        return oo_e1000_send(buffer, 14 + 512);
    } else if (active_driver == 2) {
        return oo_virtio_net_send(buffer, 14 + 512);
    }

    return -1;
}

int oo_swarm_packet_poll(SomaSwarmNetSlot *slot) {
    if (!active_driver || !slot) return -1;

    uint8_t buffer[1514];
    uint16_t len = 0;
    int ret = -1;

    if (active_driver == 1) {
        ret = oo_e1000_receive(buffer, &len);
    } else if (active_driver == 2) {
        ret = oo_virtio_net_receive(buffer, &len);
    }

    if (ret == 0 && len >= (14 + 512)) {
        struct eth_hdr *eth = (struct eth_hdr *)buffer;
        uint16_t type = (eth->type >> 8) | (eth->type << 8); // Little Endian

        if (type == ETHER_TYPE_OO_SWARM) {
            memcpy(slot, buffer + 14, 512);
            return 0; // Valid swarm packet received
        }
    }

    return -1; // No valid packet
}
