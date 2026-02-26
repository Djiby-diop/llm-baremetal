#pragma once

#include "../../core_hermes/hermes.h"
#include "../pillar_ids.h"

// Pillar p02 — WILD-G (scheduler + scripting).

hermes_status_t p02_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
