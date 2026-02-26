#pragma once

#include "../../core_hermes/hermes.h"
#include "../pillar_ids.h"

// Pillar p03 — TRUTH-G (integrity + crypto).

hermes_status_t p03_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
