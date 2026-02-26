#pragma once

#include "../../core_hermes/hermes.h"
#include "../pillar_ids.h"

// Pillar p04 — ARCH-G (storage + persistence).

hermes_status_t p04_handle(const hermes_msg_t* msg, hermes_msg_t* out_response);
