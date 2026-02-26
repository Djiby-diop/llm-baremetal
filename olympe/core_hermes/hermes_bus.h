#pragma once

#include <stdint.h>

#include "hermes.h"

// Minimal in-memory dispatcher between pillars.

#define HERMES_BUS_MAX_ROUTES 16u

typedef struct hermes_route {
    uint64_t pillar_id;
    hermes_handler_fn handler;
} hermes_route_t;

typedef struct hermes_bus {
    hermes_route_t routes[HERMES_BUS_MAX_ROUTES];
    uint32_t route_count;
} hermes_bus_t;

void hermes_bus_init(hermes_bus_t* bus);
hermes_status_t hermes_bus_register(hermes_bus_t* bus, uint64_t pillar_id, hermes_handler_fn handler);
hermes_status_t hermes_bus_dispatch(const hermes_bus_t* bus, const hermes_msg_t* msg, hermes_msg_t* out_response);
