#include <assert.h>
#include <string.h>

#include "../core_hermes/hermes_bus.h"

static hermes_status_t dummy_handler(const hermes_msg_t* msg, hermes_msg_t* out_response) {
    (void)out_response;
    hermes_status_t st = hermes_validate_header(&msg->header);
    return st;
}

void test_bus_dispatch_public(void) {
    hermes_bus_t bus;
    hermes_bus_init(&bus);
    assert(hermes_bus_register(&bus, 1, dummy_handler) == HERMES_OK);

    hermes_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.version_major = HERMES_VERSION_MAJOR;
    msg.header.version_minor = HERMES_VERSION_MINOR;
    msg.header.kind = HERMES_KIND_COMMAND;
    msg.header.dest = 1;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) == HERMES_OK);

    hermes_bus_stats_t st;
    assert(hermes_bus_get_stats(&bus, &st) == HERMES_OK);
    assert(st.dispatch_total == 1);
    assert(st.dispatch_ok == 1);
    assert(st.dispatch_no_route == 0);

    msg.header.dest = 999;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) != HERMES_OK);

    assert(hermes_bus_get_stats(&bus, &st) == HERMES_OK);
    assert(st.dispatch_total == 2);
    assert(st.dispatch_ok == 1);
    assert(st.dispatch_no_route == 1);
}

void test_bus_flow_enforcer(void) {
    hermes_bus_t bus;
    hermes_bus_init(&bus);
    assert(hermes_bus_register(&bus, 2, dummy_handler) == HERMES_OK);

    // Enable flow enforcement and allow only 1 -> 2.
    hermes_bus_set_flow_enforcement(&bus, 1);
    assert(hermes_bus_allow_flow(&bus, 1, 2) == HERMES_OK);

    hermes_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.version_major = HERMES_VERSION_MAJOR;
    msg.header.version_minor = HERMES_VERSION_MINOR;
    msg.header.kind = HERMES_KIND_COMMAND;
    msg.header.source = 1;
    msg.header.dest = 2;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) == HERMES_OK);

    msg.header.source = 3;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) == HERMES_ERR_FORBIDDEN);

    hermes_bus_stats_t st;
    assert(hermes_bus_get_stats(&bus, &st) == HERMES_OK);
    assert(st.dispatch_total == 2);
    assert(st.dispatch_ok == 1);
    assert(st.dispatch_forbidden == 1);
}
