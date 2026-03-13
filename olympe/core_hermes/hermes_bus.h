#pragma once

#include <stdint.h>

#include "hermes.h"

// Minimal in-memory dispatcher between pillars.

#define HERMES_BUS_MAX_ROUTES 16u
#define HERMES_BUS_MAX_FLOW_RULES 32u

typedef struct hermes_route {
    uint64_t pillar_id;
    hermes_handler_fn handler;
} hermes_route_t;

typedef struct hermes_bus_stats {
    uint64_t register_total;
    uint64_t register_overwrite;
    uint64_t register_full;

    uint64_t dispatch_total;
    uint64_t dispatch_ok;
    uint64_t dispatch_invalid_header;
    uint64_t dispatch_forbidden;
    uint64_t dispatch_no_route;
    uint64_t dispatch_handler_error;
} hermes_bus_stats_t;

typedef struct hermes_flow_rule {
    uint64_t source;
    uint64_t dest;
} hermes_flow_rule_t;

typedef struct hermes_bus {
    hermes_route_t routes[HERMES_BUS_MAX_ROUTES];
    uint32_t route_count;

    uint8_t flow_enforcement_enabled;
    uint8_t _pad[7];
    hermes_flow_rule_t flow_rules[HERMES_BUS_MAX_FLOW_RULES];
    uint32_t flow_rule_count;
    hermes_bus_stats_t stats;
} hermes_bus_t;

void hermes_bus_init(hermes_bus_t* bus);
hermes_status_t hermes_bus_register(hermes_bus_t* bus, uint64_t pillar_id, hermes_handler_fn handler);

// Optional access control. Disabled by default (backwards compatible).
void hermes_bus_set_flow_enforcement(hermes_bus_t* bus, uint8_t enabled);
hermes_status_t hermes_bus_allow_flow(hermes_bus_t* bus, uint64_t source, uint64_t dest);

hermes_status_t hermes_bus_dispatch(const hermes_bus_t* bus, const hermes_msg_t* msg, hermes_msg_t* out_response);

// Copies the internal stats to out_stats.
hermes_status_t hermes_bus_get_stats(const hermes_bus_t* bus, hermes_bus_stats_t* out_stats);
