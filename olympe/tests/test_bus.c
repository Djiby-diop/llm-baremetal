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

    msg.header.dest = 999;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) != HERMES_OK);
}
