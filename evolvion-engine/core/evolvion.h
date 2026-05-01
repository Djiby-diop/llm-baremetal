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

#define EVOLVION_CODEGEN_BUF      512
#define EVOLVION_DRIVER_NEEDS_MAX   8

typedef struct {
    EvolvionMode mode;
    uint32_t needs_recorded;
    uint32_t codegen_attempts;
    uint32_t jit_successes;
    /* OO Driver System */
    uint32_t drivers_generated;
    uint16_t driver_need_vid[EVOLVION_DRIVER_NEEDS_MAX];
    uint16_t driver_need_did[EVOLVION_DRIVER_NEEDS_MAX];
    uint8_t  driver_need_count;
    char     codegen_buf[EVOLVION_CODEGEN_BUF]; /* last generated prompt/stub */
} EvolvionEngine;

void evolvion_init(EvolvionEngine *e);
void evolvion_set_mode(EvolvionEngine *e, EvolvionMode mode);

/* Record a need (driver/compute/protocol). When LIVE, triggers LLM codegen. */
void evolvion_record_need(EvolvionEngine *e, EvolvionNeedType type, const char *desc);

/* Queue an unknown PCI device for driver codegen. */
void evolvion_queue_driver(EvolvionEngine *e, uint16_t vendor_id, uint16_t device_id);

/* Build an LLM-ready prompt describing the driver need. Writes to out[cap]. */
void evolvion_build_driver_prompt(EvolvionEngine *e,
                                  uint16_t vendor_id, uint16_t device_id,
                                  const char *class_name, const char *vendor_name,
                                  char *out, uint32_t cap);

const char *evolvion_mode_name_ascii(EvolvionMode mode);

#ifdef __cplusplus
}
#endif
