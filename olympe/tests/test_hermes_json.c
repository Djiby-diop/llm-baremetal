#include <assert.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

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

static void test_shadow_keys_do_not_override_top_level(void) {
    // Regression: decoder must NOT pick header fields from nested objects.
    // Old behavior (naive key scan) could read the first occurrence anywhere in the JSON.
    const char* json =
        "{" 
        "\"v\":{\"maj\":0,\"min\":1},"
        "\"kind\":\"COMMAND\","
        "\"shadow\":{\"flags\":999,\"payload_len\":5,\"correlation_id\":77,\"source\":88,\"dest\":99},"
        "\"flags\":0,"
        "\"payload_len\":5,"
        "\"correlation_id\":42,"
        "\"source\":1,"
        "\"dest\":2,"
        "\"payload\":\"hello\""
        "}";

    hermes_msg_t out;
    char payload[64];
    assert(hermes_json_decode_msg(json, strlen(json), &out, payload, sizeof(payload)) == HERMES_OK);
    assert(out.header.kind == HERMES_KIND_COMMAND);
    assert(out.header.flags == 0);
    assert(out.header.payload_len == 5);
    assert(out.header.correlation_id == 42);
    assert(out.header.source == 1);
    assert(out.header.dest == 2);
    assert(out.payload != NULL);
    assert(strcmp((const char*)out.payload, "hello") == 0);
}

void test_hermes_json_roundtrip(void) { test_roundtrip(); }
void test_hermes_json_reject_invalid(void) { test_reject_invalid(); }
void test_hermes_json_shadow_keys(void) { test_shadow_keys_do_not_override_top_level(); }

static void test_payload_len_limit_is_enforced(void) {
    // Build a Hermes JSON with payload_len > HERMES_JSON_MAX_PAYLOAD_LEN.
    // Must fail even if payload_buf is large enough.
    const size_t n = (size_t)HERMES_JSON_MAX_PAYLOAD_LEN + 1u;

    char* payload = (char*)malloc(n + 1);
    assert(payload);
    memset(payload, 'a', n);
    payload[n] = '\0';

    // JSON needs to include the payload string.
    // Keep it simple: only schema fields.
    size_t json_cap = n + 512;
    char* json = (char*)malloc(json_cap);
    assert(json);

    int wrote = snprintf(
        json,
        json_cap,
        "{\"v\":{\"maj\":%u,\"min\":%u},\"kind\":\"COMMAND\",\"flags\":0,\"payload_len\":%zu,\"correlation_id\":1,\"source\":1,\"dest\":2,\"payload\":\"%s\"}",
        (unsigned)HERMES_VERSION_MAJOR,
        (unsigned)HERMES_VERSION_MINOR,
        n,
        payload);
    assert(wrote > 0);
    assert((size_t)wrote < json_cap);

    hermes_msg_t msg;
    // payload_buf must be large enough to not be the limiting factor.
    char* payload_buf = (char*)malloc(n + 8);
    assert(payload_buf);
    hermes_status_t st = hermes_json_decode_msg(json, strlen(json), &msg, payload_buf, n + 8);
    assert(st == HERMES_ERR_BAD_LENGTH);

    free(payload_buf);
    free(json);
    free(payload);
}

static void test_payload_len_u64_overflow_is_rejected(void) {
    const char* json =
        "{\"v\":{\"maj\":0,\"min\":1},\"kind\":\"COMMAND\",\"flags\":0,\"payload_len\":4294967296,\"correlation_id\":1,\"source\":1,\"dest\":2}";
    hermes_msg_t msg;
    char payload_buf[8];
    hermes_status_t st = hermes_json_decode_msg(json, strlen(json), &msg, payload_buf, sizeof(payload_buf));
    assert(st == HERMES_ERR_INVALID_ARG);
}

void test_hermes_json_sentinel_limits(void) {
    test_payload_len_limit_is_enforced();
    test_payload_len_u64_overflow_is_rejected();
}
