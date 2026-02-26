#pragma once

#include "../../core_hermes/hermes.h"
#include "../pillar_ids.h"

// Pillar p08 — NEURAL-G (learning + heuristics).

hermes_status_t p08_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
