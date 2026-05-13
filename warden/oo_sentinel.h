/* oo_sentinel.h — Warden / Sentinel Safety Layer (Public Prototype)
 *
 * The sentinel monitors all subsystem invariants and can halt execution
 * if any safety condition is violated. It operates below the D+ policy
 * engine — even policy violations are caught here as a last resort.
 */
#ifndef OO_SENTINEL_H
#define OO_SENTINEL_H

#include <stdint.h>

typedef enum {
    SENTINEL_OK        = 0,
    SENTINEL_WARN      = 1,
    SENTINEL_HALT      = 2,   /* Non-recoverable: halt the system */
    SENTINEL_QUARANTINE= 3,   /* Isolate the violating module */
} SentinelStatus;

typedef struct {
    uint32_t       violation_count;
    SentinelStatus last_status;
    char           last_reason[128];
    int            halt_armed;    /* 1 = sentinel will halt on next violation */
} OoSentinel;

/* Initialize sentinel with default thresholds */
void oo_sentinel_init(OoSentinel *s);

/* Check a zone boundary — called before every memory access in hot path */
SentinelStatus oo_sentinel_check_zone(OoSentinel *s, int zone, void *ptr, size_t len);

/* Report a policy violation from D+ engine */
SentinelStatus oo_sentinel_report_violation(OoSentinel *s, const char *reason);

/* Print sentinel status to console */
void oo_sentinel_print(const OoSentinel *s);

#endif /* OO_SENTINEL_H */
