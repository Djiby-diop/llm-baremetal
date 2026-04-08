#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hermes.h"

// Minimal JSON transport for CORE_HERMES v0.1.
// This is not a general-purpose JSON library; it only supports the Hermes schema.
// Payload is treated as a UTF-8 string (NUL-terminated when decoded).

// Sentinel guardrail: maximum payload length accepted by the JSON decoder.
// Can be overridden at compile time (e.g. -DHERMES_JSON_MAX_PAYLOAD_LEN=1024).
#ifndef HERMES_JSON_MAX_PAYLOAD_LEN
#define HERMES_JSON_MAX_PAYLOAD_LEN 4096u
#endif

// Encode Hermes message to JSON into `out`.
// Returns HERMES_OK and sets out_len on success.
hermes_status_t hermes_json_encode_msg(
    const hermes_msg_t* msg,
    char* out,
    size_t out_cap,
    size_t* out_len);

// Decode JSON into Hermes message.
// `payload_buf` receives decoded payload string (NUL-terminated). If payload is absent, payload_len=0 and payload=NULL.
hermes_status_t hermes_json_decode_msg(
    const char* json,
    size_t json_len,
    hermes_msg_t* out_msg,
    char* payload_buf,
    size_t payload_buf_cap);
