#include "oo_mercator_packet.h"
#include "../../../../engine/drivers/e1000.h"
#include "../../../../engine/drivers/virtio_net.h"
#include <string.h>

struct eth_hdr {
    uint8_t  dest[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

static uint8_t my_mac[6] = {0};
static int active_driver = 0; // 1 = E1000, 2 = VirtIO-Net

void oo_mercator_packet_init(void) {
    // Detect active driver based on valid MAC
    oo_e1000_get_mac(my_mac);
    if (my_mac[0] != 0 || my_mac[1] != 0) {
        active_driver = 1;
        return;
    }

    oo_virtio_net_get_mac(my_mac);
    if (my_mac[0] != 0 || my_mac[1] != 0) {
        active_driver = 2;
        return;
    }
}

int oo_mercator_packet_broadcast(const MercatorNetPacket *pkt) {
    if (!active_driver || !pkt) return -1;

    uint8_t buffer[14 + sizeof(MercatorNetPacket)];
    struct eth_hdr *eth = (struct eth_hdr *)buffer;

    // Broadcast
    memset(eth->dest, 0xFF, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = ((ETHER_TYPE_OO_MERCATOR & 0xFF) << 8) | (ETHER_TYPE_OO_MERCATOR >> 8); // Big Endian

    // Payload
    memcpy(buffer + 14, pkt, sizeof(MercatorNetPacket));

    if (active_driver == 1) {
        return oo_e1000_send(buffer, 14 + sizeof(MercatorNetPacket));
    } else if (active_driver == 2) {
        return oo_virtio_net_send(buffer, 14 + sizeof(MercatorNetPacket));
    }

    return -1;
}

int oo_mercator_packet_poll(MercatorNetPacket *pkt) {
    if (!active_driver || !pkt) return -1;

    uint8_t buffer[1514];
    uint16_t len = 0;
    int ret = -1;

    if (active_driver == 1) {
        ret = oo_e1000_receive(buffer, &len);
    } else if (active_driver == 2) {
        ret = oo_virtio_net_receive(buffer, &len);
    }

    if (ret == 0 && len >= (14 + sizeof(MercatorNetPacket))) {
        struct eth_hdr *eth = (struct eth_hdr *)buffer;
        uint16_t type = (eth->type >> 8) | (eth->type << 8); // Little Endian

        if (type == ETHER_TYPE_OO_MERCATOR) {
            memcpy(pkt, buffer + 14, sizeof(MercatorNetPacket));
            return 0; // Valid price packet received
        }
    }

    return -1; // No valid packet
}
