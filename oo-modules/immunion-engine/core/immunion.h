#pragma once

/*
 * Immunion: Immunological Kernel
 *
 * Immune memory of past events. When a known threat pattern reappears,
 * auto-reaction: quarantine, redirect, rollback. Antibodies vs malware.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMMUNION_MODE_OFF    = 0,
    IMMUNION_MODE_RECORD = 1,  /* record patterns, no auto-react */
    IMMUNION_MODE_ACT    = 2,  /* auto-react on known threat */
} ImmunionMode;

typedef enum {
    IMMUNION_THREAT_CRASH    = 0,
    IMMUNION_THREAT_CORRUPT  = 1,
    IMMUNION_THREAT_OOBCheck = 2,
} ImmunionThreatType;

typedef struct {
    ImmunionThreatType type;
    uint32_t pattern_hash;
    uint8_t severity;
} ImmunionPattern;

typedef struct {
    ImmunionMode mode;
    uint32_t patterns_recorded;
    uint32_t reactions_triggered;
} ImmunionEngine;

void immunion_init(ImmunionEngine *e);
void immunion_set_mode(ImmunionEngine *e, ImmunionMode mode);

void immunion_record(ImmunionEngine *e, ImmunionThreatType type, uint32_t pattern_hash, uint8_t severity);
uint8_t immunion_check(ImmunionEngine *e, ImmunionThreatType type, uint32_t pattern_hash);

const char *immunion_mode_name_ascii(ImmunionMode mode);

#ifdef __cplusplus
}
#endif
