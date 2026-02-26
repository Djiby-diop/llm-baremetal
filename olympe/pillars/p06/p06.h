#pragma once

#include "../../core_hermes/hermes.h"
#include "../pillar_ids.h"

// Pillar p06 — LOGOS-G (compiler + low-level orchestration).

hermes_status_t p06_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
