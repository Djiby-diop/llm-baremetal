#pragma once

/*
 * Evolvion: Self-Evolving Kernel
 *
 * LLM generates system functions on demand. JIT compiles and executes
 * without external updates. OS that rewrites itself.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVOLVION_MODE_OFF   = 0,
    EVOLVION_MODE_LEARN = 1,  /* record needs, no codegen */
    EVOLVION_MODE_LIVE  = 2,  /* generate + JIT + execute */
} EvolvionMode;

typedef enum {
    EVOLVION_NEED_UNKNOWN  = 0,
    EVOLVION_NEED_DRIVER   = 1,  /* new hardware */
    EVOLVION_NEED_COMPUTE  = 2,  /* math/kernel function */
    EVOLVION_NEED_PROTOCOL = 3,  /* comms/format */
} EvolvionNeedType;

typedef struct {
    EvolvionNeedType type;
    uint32_t hash;      /* stable id of the need */
    uint8_t recorded;   /* 1 if logged for later */
} EvolvionNeed;

typedef struct {
    EvolvionMode mode;
    uint32_t needs_recorded;
    uint32_t codegen_attempts;
    uint32_t jit_successes;
} EvolvionEngine;

void evolvion_init(EvolvionEngine *e);
void evolvion_set_mode(EvolvionEngine *e, EvolvionMode mode);

/* Record a need (driver/compute/protocol). When LIVE, triggers LLM codegen. */
void evolvion_record_need(EvolvionEngine *e, EvolvionNeedType type, const char *desc);

const char *evolvion_mode_name_ascii(EvolvionMode mode);

#ifdef __cplusplus
}
#endif
