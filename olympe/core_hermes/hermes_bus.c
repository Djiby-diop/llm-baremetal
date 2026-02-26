#include "hermes_bus.h"

void hermes_bus_init(hermes_bus_t* bus) {
    if (!bus) return;
    bus->route_count = 0;
    for (uint32_t i = 0; i < HERMES_BUS_MAX_ROUTES; i++) {
        bus->routes[i].pillar_id = 0;
        bus->routes[i].handler = 0;
    }
}

hermes_status_t hermes_bus_register(hermes_bus_t* bus, uint64_t pillar_id, hermes_handler_fn handler) {
    if (!bus || !handler || pillar_id == 0) return HERMES_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < bus->route_count; i++) {
        if (bus->routes[i].pillar_id == pillar_id) {
            bus->routes[i].handler = handler;
            return HERMES_OK;
        }
    }
    if (bus->route_count >= HERMES_BUS_MAX_ROUTES) return HERMES_ERR_BAD_LENGTH;
    bus->routes[bus->route_count].pillar_id = pillar_id;
    bus->routes[bus->route_count].handler = handler;
    bus->route_count++;
    return HERMES_OK;
}

hermes_status_t hermes_bus_dispatch(const hermes_bus_t* bus, const hermes_msg_t* msg, hermes_msg_t* out_response) {
    if (!bus || !msg) return HERMES_ERR_INVALID_ARG;
    hermes_status_t st = hermes_validate_header(&msg->header);
    if (st != HERMES_OK) return st;

    uint64_t dest = msg->header.dest;
    for (uint32_t i = 0; i < bus->route_count; i++) {
        if (bus->routes[i].pillar_id == dest) {
            return bus->routes[i].handler(msg, out_response);
        }
    }
    return HERMES_ERR_INVALID_ARG;
}
