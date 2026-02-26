#include <stdio.h>

void test_hermes_json_roundtrip(void);
void test_hermes_json_reject_invalid(void);
void test_bus_dispatch_public(void);
void test_all_pillars_accept_their_dest_public(void);

int main(void) {
    test_hermes_json_roundtrip();
    test_hermes_json_reject_invalid();
    test_bus_dispatch_public();
    test_all_pillars_accept_their_dest_public();
    printf("olympe tests: OK\n");
    return 0;
}
