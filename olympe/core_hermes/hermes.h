#pragma once

#include <stdint.h>
#include <stddef.h>

// CORE_HERMES: minimal cross-pillar message contract.
// This is intentionally tiny and standalone; it does not impose a transport.

#define HERMES_VERSION_MAJOR 0u
#define HERMES_VERSION_MINOR 1u

typedef enum hermes_kind {
    HERMES_KIND_COMMAND = 1,
    HERMES_KIND_EVENT = 2,
    HERMES_KIND_RESPONSE = 3,
} hermes_kind_t;

typedef enum hermes_status {
    HERMES_OK = 0,
    HERMES_ERR_INVALID_ARG = 1,
    HERMES_ERR_UNSUPPORTED_VERSION = 2,
    HERMES_ERR_BAD_LENGTH = 3,
} hermes_status_t;

typedef struct hermes_header {
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t kind;            // hermes_kind_t
    uint16_t flags;           // reserved
    uint32_t payload_len;     // bytes
    uint64_t correlation_id;  // request/response pairing
    uint64_t source;          // pillar-defined
    uint64_t dest;            // pillar-defined
} hermes_header_t;

typedef struct hermes_msg {
    hermes_header_t header;
    const void* payload;
} hermes_msg_t;

typedef hermes_status_t (*hermes_handler_fn)(const hermes_msg_t* msg, hermes_msg_t* out_response);

static inline hermes_status_t hermes_validate_header(const hermes_header_t* h) {
    if (!h) return HERMES_ERR_INVALID_ARG;
    if (h->version_major != HERMES_VERSION_MAJOR) return HERMES_ERR_UNSUPPORTED_VERSION;

    switch ((hermes_kind_t)h->kind) {
        case HERMES_KIND_COMMAND:
        case HERMES_KIND_EVENT:
        case HERMES_KIND_RESPONSE:
            break;
        default:
            return HERMES_ERR_INVALID_ARG;
    }

    // payload_len is trusted only when combined with a transport buffer length.
    return HERMES_OK;
}

