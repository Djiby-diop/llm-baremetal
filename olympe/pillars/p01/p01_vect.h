#pragma once

#include <stdint.h>
#include "../../core_hermes/hermes.h"

// Pillar p01 (VECT-G) — C-first scaffold.
// Goal: define a minimal callable surface; transport is handled by CORE_HERMES.

typedef struct p01_vect_config {
    uint32_t reserved;
} p01_vect_config_t;

typedef enum p01_vect_cmd {
    P01_VECT_CMD_NOP = 0,
} p01_vect_cmd_t;

// Handle a Hermes COMMAND targeted to p01.
// Returns HERMES_OK if the message is well-formed and understood.
hermes_status_t p01_vect_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
