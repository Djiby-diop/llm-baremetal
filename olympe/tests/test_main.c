#include <stdio.h>

void test_hermes_json_roundtrip(void);
void test_hermes_json_reject_invalid(void);
void test_hermes_json_shadow_keys(void);
void test_hermes_json_sentinel_limits(void);
void test_bus_dispatch_public(void);
void test_bus_flow_enforcer(void);

int main(void) {
    test_hermes_json_roundtrip();
    test_hermes_json_reject_invalid();
    test_hermes_json_shadow_keys();
    test_hermes_json_sentinel_limits();
    test_bus_dispatch_public();
    test_bus_flow_enforcer();
    printf("olympe tests: OK\n");
    return 0;
}
