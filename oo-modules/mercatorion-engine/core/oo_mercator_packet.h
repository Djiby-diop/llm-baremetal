#ifndef OO_MERCATOR_PACKET_H
#define OO_MERCATOR_PACKET_H

#include <stdint.h>

#define ETHER_TYPE_OO_MERCATOR 0xAA01

typedef struct __attribute__((packed)) {
    uint32_t node_id;
    float    top_bid;
    float    top_ask;
    uint64_t timestamp;
} MercatorNetPacket;

// Initialize the mercator packet system
void oo_mercator_packet_init(void);

// Broadcast local top prices to the network
int oo_mercator_packet_broadcast(const MercatorNetPacket *pkt);

// Poll for incoming price packets from other nodes
int oo_mercator_packet_poll(MercatorNetPacket *pkt);

#endif // OO_MERCATOR_PACKET_H
