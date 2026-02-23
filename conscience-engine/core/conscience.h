#pragma once

/*
 * Conscience: Particule de Conscience (Thermal Homeostasis)
 *
 * Background monitor: CPU temp, power. If overheating, Sentinel asks LLM to reduce
 * precision (16bit -> 4bit) to "rest" without stopping.
 * Digital homeostasis: the body sweats to cool.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONSCIENCE_MODE_OFF  = 0,
    CONSCIENCE_MODE_WATCH = 1,  /* observe only */
    CONSCIENCE_MODE_ACT  = 2,   /* trigger precision downgrade on thermal stress */
} ConscienceMode;

typedef enum {
    CONSCIENCE_PREC_F32  = 0,
    CONSCIENCE_PREC_F16  = 1,
    CONSCIENCE_PREC_Q8   = 2,
    CONSCIENCE_PREC_Q4   = 3,
} ConsciencePrecision;

typedef struct {
    uint32_t temp_raw;    /* platform-specific (e.g. MSR) */
    uint32_t power_raw;   /* best-effort */
    uint8_t  stress;      /* 0..100 */
} ConscienceSample;

typedef struct {
    ConscienceMode mode;
    ConsciencePrecision current_precision;
    uint32_t samples_taken;
    uint32_t downgrades_triggered;
} ConscienceEngine;

void conscience_init(ConscienceEngine *e);
void conscience_set_mode(ConscienceEngine *e, ConscienceMode mode);

/* Best-effort sample (UEFI: may use ACPI/EC or stub). */
void conscience_sample(ConscienceEngine *e, ConscienceSample *out);

/* Recommendation: reduce precision if thermal stress. */
ConsciencePrecision conscience_recommend_precision(const ConscienceEngine *e, uint32_t stress);

const char *conscience_mode_name_ascii(ConscienceMode mode);

#ifdef __cplusplus
}
#endif
