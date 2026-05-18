#ifndef OO_SWARM_PACKET_H
#define OO_SWARM_PACKET_H

#include "soma_swarm_net.h"

// Initialize the swarm packet system (selects active driver)
void oo_swarm_packet_init(void);

// Broadcast a vote slot to the network
int oo_swarm_packet_broadcast(const SomaSwarmNetSlot *slot);

// Poll for incoming swarm packets
int oo_swarm_packet_poll(SomaSwarmNetSlot *slot);

#endif // OO_SWARM_PACKET_H
