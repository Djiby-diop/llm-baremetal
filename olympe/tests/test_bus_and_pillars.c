#include <assert.h>
#include <string.h>

#include "../core_hermes/hermes_bus.h"

#include "../pillars/pillar_ids.h"
#include "../pillars/p01/p01.h"
#include "../pillars/p02/p02.h"
#include "../pillars/p03/p03.h"
#include "../pillars/p04/p04.h"
#include "../pillars/p05/p05.h"
#include "../pillars/p06/p06.h"
#include "../pillars/p07/p07.h"
#include "../pillars/p08/p08.h"
#include "../pillars/p09/p09.h"
#include "../pillars/p10/p10.h"

static void test_bus_dispatch(void) {
    hermes_bus_t bus;
    hermes_bus_init(&bus);
    assert(hermes_bus_register(&bus, OLY_P01, p01_handle) == HERMES_OK);
    assert(hermes_bus_register(&bus, OLY_P02, p02_handle) == HERMES_OK);

    hermes_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.version_major = HERMES_VERSION_MAJOR;
    msg.header.version_minor = HERMES_VERSION_MINOR;
    msg.header.kind = HERMES_KIND_COMMAND;
    msg.header.dest = OLY_P01;

    assert(hermes_bus_dispatch(&bus, &msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P02;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) == HERMES_OK);
    msg.header.dest = 999;
    assert(hermes_bus_dispatch(&bus, &msg, NULL) != HERMES_OK);
}

static void test_all_pillars_accept_their_dest(void) {
    hermes_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.version_major = HERMES_VERSION_MAJOR;
    msg.header.version_minor = HERMES_VERSION_MINOR;
    msg.header.kind = HERMES_KIND_COMMAND;

    msg.header.dest = OLY_P01; assert(p01_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P02; assert(p02_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P03; assert(p03_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P04; assert(p04_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P05; assert(p05_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P06; assert(p06_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P07; assert(p07_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P08; assert(p08_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P09; assert(p09_handle(&msg, NULL) == HERMES_OK);
    msg.header.dest = OLY_P10; assert(p10_handle(&msg, NULL) == HERMES_OK);
}

void test_bus_dispatch_public(void) { test_bus_dispatch(); }
void test_all_pillars_accept_their_dest_public(void) { test_all_pillars_accept_their_dest(); }
