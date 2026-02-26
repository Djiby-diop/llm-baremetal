#include "p01.h"

hermes_status_t p01_handle(const hermes_msg_t* msg, hermes_msg_t* out_response) {
    (void)out_response;
    if (!msg) return HERMES_ERR_INVALID_ARG;
    hermes_status_t st = hermes_validate_header(&msg->header);
    if (st != HERMES_OK) return st;
    if ((hermes_kind_t)msg->header.kind != HERMES_KIND_COMMAND) return HERMES_ERR_INVALID_ARG;
    if (msg->header.dest != OLY_P01) return HERMES_ERR_INVALID_ARG;
    return HERMES_OK;
}
