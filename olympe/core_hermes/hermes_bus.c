#include "hermes_bus.h"

static void hermes_bus_stats_zero(hermes_bus_stats_t* s) {
    if (!s) return;
    s->register_total = 0;
    s->register_overwrite = 0;
    s->register_full = 0;
    s->dispatch_total = 0;
    s->dispatch_ok = 0;
    s->dispatch_invalid_header = 0;
    s->dispatch_forbidden = 0;
    s->dispatch_no_route = 0;
    s->dispatch_handler_error = 0;
}

void hermes_bus_init(hermes_bus_t* bus) {
    if (!bus) return;
    bus->route_count = 0;
    bus->flow_enforcement_enabled = 0;
    bus->flow_rule_count = 0;
    hermes_bus_stats_zero(&bus->stats);
    for (uint32_t i = 0; i < HERMES_BUS_MAX_ROUTES; i++) {
        bus->routes[i].pillar_id = 0;
        bus->routes[i].handler = 0;
    }
    for (uint32_t i = 0; i < HERMES_BUS_MAX_FLOW_RULES; i++) {
        bus->flow_rules[i].source = 0;
        bus->flow_rules[i].dest = 0;
    }
}

void hermes_bus_set_flow_enforcement(hermes_bus_t* bus, uint8_t enabled) {
    if (!bus) return;
    bus->flow_enforcement_enabled = enabled ? 1u : 0u;
}

hermes_status_t hermes_bus_allow_flow(hermes_bus_t* bus, uint64_t source, uint64_t dest) {
    if (!bus || source == 0 || dest == 0) return HERMES_ERR_INVALID_ARG;

    for (uint32_t i = 0; i < bus->flow_rule_count; i++) {
        if (bus->flow_rules[i].source == source && bus->flow_rules[i].dest == dest) {
            return HERMES_OK;
        }
    }

    if (bus->flow_rule_count >= HERMES_BUS_MAX_FLOW_RULES) return HERMES_ERR_BAD_LENGTH;
    bus->flow_rules[bus->flow_rule_count].source = source;
    bus->flow_rules[bus->flow_rule_count].dest = dest;
    bus->flow_rule_count++;
    return HERMES_OK;
}

static int hermes_bus_flow_is_allowed(const hermes_bus_t* bus, uint64_t source, uint64_t dest) {
    if (!bus) return 0;
    if (!bus->flow_enforcement_enabled) return 1;
    for (uint32_t i = 0; i < bus->flow_rule_count; i++) {
        if (bus->flow_rules[i].source == source && bus->flow_rules[i].dest == dest) return 1;
    }
    return 0;
}

hermes_status_t hermes_bus_register(hermes_bus_t* bus, uint64_t pillar_id, hermes_handler_fn handler) {
    if (!bus || !handler || pillar_id == 0) return HERMES_ERR_INVALID_ARG;
    bus->stats.register_total++;
    for (uint32_t i = 0; i < bus->route_count; i++) {
        if (bus->routes[i].pillar_id == pillar_id) {
            bus->routes[i].handler = handler;
            bus->stats.register_overwrite++;
            return HERMES_OK;
        }
    }
    if (bus->route_count >= HERMES_BUS_MAX_ROUTES) {
        bus->stats.register_full++;
        return HERMES_ERR_BAD_LENGTH;
    }
    bus->routes[bus->route_count].pillar_id = pillar_id;
    bus->routes[bus->route_count].handler = handler;
    bus->route_count++;
    return HERMES_OK;
}

hermes_status_t hermes_bus_dispatch(const hermes_bus_t* bus, const hermes_msg_t* msg, hermes_msg_t* out_response) {
    if (!bus || !msg) return HERMES_ERR_INVALID_ARG;
    // const bus but mutable counters: keep API small and avoid global state.
    hermes_bus_stats_t* stats = (hermes_bus_stats_t*)&bus->stats;
    stats->dispatch_total++;

    hermes_status_t st = hermes_validate_header(&msg->header);
    if (st != HERMES_OK) {
        stats->dispatch_invalid_header++;
        return st;
    }

    uint64_t source = msg->header.source;
    uint64_t dest = msg->header.dest;

    if (!hermes_bus_flow_is_allowed(bus, source, dest)) {
        stats->dispatch_forbidden++;
        return HERMES_ERR_FORBIDDEN;
    }

    for (uint32_t i = 0; i < bus->route_count; i++) {
        if (bus->routes[i].pillar_id == dest) {
            hermes_status_t hst = bus->routes[i].handler(msg, out_response);
            if (hst == HERMES_OK) stats->dispatch_ok++;
            else stats->dispatch_handler_error++;
            return hst;
        }
    }
    stats->dispatch_no_route++;
    return HERMES_ERR_INVALID_ARG;
}

hermes_status_t hermes_bus_get_stats(const hermes_bus_t* bus, hermes_bus_stats_t* out_stats) {
    if (!bus || !out_stats) return HERMES_ERR_INVALID_ARG;
    *out_stats = bus->stats;
    return HERMES_OK;
}
