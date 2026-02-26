#pragma once

#include "../../core_hermes/hermes.h"
#include "../pillar_ids.h"

// Pillar p10 — OMEGA-G (arbitration).

hermes_status_t p10_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
