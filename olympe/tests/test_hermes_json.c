#include <assert.h>
#include <string.h>

#include "../core_hermes/hermes_json.h"

static void test_roundtrip(void) {
    const char* payload = "{\"cmd\":\"NOP\"}";
    hermes_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.version_major = HERMES_VERSION_MAJOR;
    msg.header.version_minor = HERMES_VERSION_MINOR;
    msg.header.kind = HERMES_KIND_COMMAND;
    msg.header.flags = 0;
    msg.header.payload_len = (uint32_t)strlen(payload);
    msg.header.correlation_id = 42;
    msg.header.source = 1;
    msg.header.dest = 2;
    msg.payload = payload;

    char json[512];
    size_t json_len = 0;
    assert(hermes_json_encode_msg(&msg, json, sizeof(json), &json_len) == HERMES_OK);
    assert(json_len > 10);
    assert(json[json_len] == '\0');

    hermes_msg_t out;
    char out_payload[256];
    assert(hermes_json_decode_msg(json, json_len, &out, out_payload, sizeof(out_payload)) == HERMES_OK);
    assert(out.header.kind == HERMES_KIND_COMMAND);
    assert(out.header.correlation_id == 42);
    assert(out.header.source == 1);
    assert(out.header.dest == 2);
    assert(out.payload != NULL);
    assert(out.header.payload_len == strlen(payload));
    assert(strcmp((const char*)out.payload, payload) == 0);
}

static void test_reject_invalid(void) {
    const char* bad = "{\"kind\":\"COMMAND\"}";
    hermes_msg_t out;
    char payload[64];
    assert(hermes_json_decode_msg(bad, strlen(bad), &out, payload, sizeof(payload)) != HERMES_OK);
}

void test_hermes_json_roundtrip(void) { test_roundtrip(); }
void test_hermes_json_reject_invalid(void) { test_reject_invalid(); }
