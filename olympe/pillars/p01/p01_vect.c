#include "p01_vect.h"

hermes_status_t p01_vect_handle(const hermes_msg_t* msg, hermes_msg_t* out_response) {
    if (!msg) return HERMES_ERR_INVALID_ARG;
    hermes_status_t st = hermes_validate_header(&msg->header);
    if (st != HERMES_OK) return st;

    if ((hermes_kind_t)msg->header.kind != HERMES_KIND_COMMAND) {
        return HERMES_ERR_INVALID_ARG;
    }

    // Scaffold: no commands implemented yet.
    (void)out_response;
    return HERMES_OK;
}