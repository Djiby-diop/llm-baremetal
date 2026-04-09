/* oo_bus_oo_modules.c — Wire all OO modules to the Hermes bus
 *
 * This file registers every OO module as a Hermes handler.
 * Channels are defined in oo_bus_channels.h
 * Each module exposes: <module>_init(), <module>_tick(), <module>_handle()
 *
 * Build: included in oo-bus.a via Makefile.oo
 */

#include "hermes_bus.h"

/* Channel IDs (must match oo-modules/module_registry.h) */
#define OO_CH_CALIBRION    0x0101ULL
#define OO_CH_CONSCIENCE   0x0102ULL
#define OO_CH_DJIBION      0x0103ULL
#define OO_CH_MEMORION     0x0201ULL
#define OO_CH_NEURALFS     0x0202ULL
#define OO_CH_METABION     0x0301ULL
#define OO_CH_DREAMION     0x0302ULL
#define OO_CH_COLLECTIVION 0x0401ULL
#define OO_CH_GHOST        0x0402ULL
#define OO_CH_MORPHION     0x0501ULL
#define OO_CH_EVOLVION     0x0502ULL
#define OO_CH_IMMUNION     0x0601ULL
#define OO_CH_ORCHESTRION  0x0701ULL
#define OO_CH_DIAGNOSTION  0x0801ULL
#define OO_CH_SOMAMIND     0x1000ULL  /* SomaMind routing hub */
#define OO_CH_WARDEN       0x2000ULL  /* D+ verdict bus */
#define OO_CH_SENTINEL     0x2001ULL  /* Sentinel violation alert */
#define OO_CH_DPLUS        0x2002ULL  /* D+ policy eval request */

/* Minimal no-op handler for unimplemented modules */
static hermes_status_t oo_noop_handler(const hermes_msg_t *msg, hermes_msg_t *resp) {
    (void)msg; (void)resp;
    return HERMES_OK;
}

/* Register all OO modules on the shared bus */
hermes_status_t oo_bus_register_modules(hermes_bus_t *bus) {
    hermes_status_t s;

    /* Cognition */
    s = hermes_register(bus, OO_CH_CALIBRION,    oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_CONSCIENCE,   oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_DJIBION,      oo_noop_handler); if (s) return s;

    /* Memory */
    s = hermes_register(bus, OO_CH_MEMORION,     oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_NEURALFS,     oo_noop_handler); if (s) return s;

    /* Metabolism */
    s = hermes_register(bus, OO_CH_METABION,     oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_DREAMION,     oo_noop_handler); if (s) return s;

    /* Social */
    s = hermes_register(bus, OO_CH_COLLECTIVION, oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_GHOST,        oo_noop_handler); if (s) return s;

    /* Morphology */
    s = hermes_register(bus, OO_CH_MORPHION,     oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_EVOLVION,     oo_noop_handler); if (s) return s;

    /* Immunity */
    s = hermes_register(bus, OO_CH_IMMUNION,     oo_noop_handler); if (s) return s;

    /* Coordination */
    s = hermes_register(bus, OO_CH_ORCHESTRION,  oo_noop_handler); if (s) return s;

    /* Diagnostics */
    s = hermes_register(bus, OO_CH_DIAGNOSTION,  oo_noop_handler); if (s) return s;

    /* Hub channels */
    s = hermes_register(bus, OO_CH_SOMAMIND,     oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_WARDEN,       oo_noop_handler); if (s) return s;

    /* Security channels (Phase J) */
    s = hermes_register(bus, OO_CH_SENTINEL,     oo_noop_handler); if (s) return s;
    s = hermes_register(bus, OO_CH_DPLUS,        oo_noop_handler); if (s) return s;

    return HERMES_OK;
}
