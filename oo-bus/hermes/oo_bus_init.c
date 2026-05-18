/* oo-bus/hermes/oo_bus_init.c — Concrete Bus Wiring
 *
 * Implements oo_bus_init() declared in oo_bus_channels.h.
 *
 * This file is the SINGLE PLACE where:
 *   1. The Hermes router is initialized
 *   2. All OO modules are registered with their channel subscriptions
 *   3. All cross-subsystem wiring handlers are defined
 *
 * WIRING MAP (channels → handlers):
 *
 *   OO_CH_PMU_SAMPLE       → conscience_handle (thermal → precision)
 *                          → metabion_handle (metabolism tracking)
 *                          → splitbrain_handle (thermal mode gate)
 *
 *   OO_CH_MEM_PRESSURE     → calibrion_handle (adjust sampling)
 *                          → dreamion_handle (trigger consolidation)
 *                          → synaption_handle (promote hot blocks)
 *
 *   OO_CH_HW_IDENTITY      → morphion_handle (boot skeleton decision)
 *                          → compatibilion_handle (caps update)
 *                          → djibion_handle (identity bind)
 *
 *   OO_CH_SENTINEL_TRIP    → immunion_handle (record threat pattern)
 *                          → orchestrion_handle (stop pipeline)
 *                          → diopion_handle (disable burst)
 *
 *   OO_CH_TOKEN_OUT        → pheromion_handle (hot path touch)
 *                          → symbion_handle (hardware feedback)
 *
 *   OO_CH_INFERENCE_STEP   → calibrion_handle (feed stats)
 *                          → metabion_handle (feed performance)
 *                          → memorion_handle (tick manifest)
 *
 *   OO_CH_CONSENSUS        → collectivion_handle (broadcast winner)
 *                          → ghost_handle (optional LED/PCspk emit)
 *
 *   OO_CH_TRAIN_SAMPLE     → evolvion_handle (record training need)
 *
 * HARD BOUNDARIES (enforced by include rules):
 *   - oo-kernel  must NOT include any engine or module header
 *   - oo-warden  must NOT include inference or engine headers
 *   - modules    communicate ONLY via oo_bus_post()
 *
 * NOTE: engine global variables (g_conscience, g_calibrion, etc.) are declared
 *       in the per-module wrapper headers (oo-modules/<cat>/<name>/module_<name>.h).
 *       This file includes them all and registers them.
 */

#include "oo_bus_router.h"
#include "oo_bus_channels.h"
#include "oo-modules/module_api.h"
#include "oo-modules/module_registry.h"

/* ── Module headers (include engine types + declare globals) ───────────── */
#include "oo-modules/sensing/conscience/module_conscience.h"
#include "oo-modules/sensing/compatibilion/module_compatibilion.h"
#include "oo-modules/sensing/diagnostion/module_diagnostion.h"
#include "oo-modules/sensing/metabion/module_metabion.h"
#include "oo-modules/adaptation/calibrion/module_calibrion.h"
#include "oo-modules/adaptation/diopion/module_diopion.h"
#include "oo-modules/adaptation/morphion/module_morphion.h"
#include "oo-modules/adaptation/symbion/module_symbion.h"
#include "oo-modules/adaptation/evolvion/module_evolvion.h"
#include "oo-modules/memory/memorion/module_memorion.h"
#include "oo-modules/memory/synaption/module_synaption.h"
#include "oo-modules/memory/pheromion/module_pheromion.h"
#include "oo-modules/memory/dreamion/module_dreamion.h"
#include "oo-modules/memory/neuralfs/module_neuralfs.h"
#include "oo-modules/identity/djibion/module_djibion.h"
#include "oo-modules/identity/immunion/module_immunion.h"
#include "oo-modules/social/collectivion/module_collectivion.h"
#include "oo-modules/social/ghost/module_ghost.h"
#include "oo-modules/meta/orchestrion/module_orchestrion.h"
#include "oo-modules/meta/cellion/module_cellion.h"
#include "oo-modules/meta/desktop_display/module_desktop_display.h"

/* ── REPL command registrars (forward-declare; defined in cmd_*.c) ───────── */
void cmd_dna_register(void);
void cmd_pressure_register(void);
void cmd_conscience_register(void);
void cmd_immunion_register(void);
void cmd_splitbrain_register(void);
void cmd_hebbian_register(void);
void cmd_kv_persist_register(void);
void cmd_wasm_register(void);
void cmd_pheromion_register(void);
void cmd_evolvion_register(void);
void cmd_neuralfs_register(void);
void cmd_desktop_display_register(void);

/* ============================================================
 * Module descriptors
 * Each entry wires one engine to the bus.
 * ============================================================ */

/* Helper macro to build a descriptor with channel list */
#define MOD_DESC(mid, cat, _name, state, _init, _tick, _handle, _interval, ...) \
    { .id = (mid), .category = (cat), .flags = OO_MOD_FLAG_NO_HEAP,           \
      .name = (_name), .engine_state = (state),                                \
      .init = (_init), .tick = (_tick), .handle = (_handle),                   \
      .channels = { __VA_ARGS__ },                                              \
      .channel_count = sizeof((uint16_t[]){ __VA_ARGS__ }) / sizeof(uint16_t), \
      .tick_interval = (_interval) }

static const OoModuleDescriptor k_module_descriptors[OO_MOD_COUNT] = {

    /* ── SENSING ─────────────────────────────────────────────── */
    MOD_DESC(OO_MOD_CONSCIENCE,    OO_MOD_CAT_SENSING,
             "conscience", &g_conscience,
             conscience_mod_init, conscience_mod_tick, conscience_mod_handle,
             16,   /* tick every 16 steps */
             OO_CH_PMU_SAMPLE),

    MOD_DESC(OO_MOD_COMPATIBILION, OO_MOD_CAT_SENSING,
             "compatibilion", &g_compatibilion,
             compatibilion_mod_init, NULL, compatibilion_mod_handle,
             0,    /* no periodic tick */
             OO_CH_HW_IDENTITY),

    MOD_DESC(OO_MOD_DIAGNOSTION,   OO_MOD_CAT_SENSING,
             "diagnostion", &g_diagnostion,
             diagnostion_mod_init, NULL, diagnostion_mod_handle,
             0,
             OO_CH_SENTINEL_TRIP, OO_CH_BOOT_DONE),

    MOD_DESC(OO_MOD_METABION,      OO_MOD_CAT_SENSING,
             "metabion", &g_metabion,
             metabion_mod_init, metabion_mod_tick, metabion_mod_handle,
             32,   /* tick every 32 steps */
             OO_CH_PMU_SAMPLE, OO_CH_INFERENCE_STEP),

    /* ── ADAPTATION ───────────────────────────────────────────── */
    MOD_DESC(OO_MOD_CALIBRION,     OO_MOD_CAT_ADAPTATION,
             "calibrion", &g_calibrion,
             calibrion_mod_init, NULL, calibrion_mod_handle,
             0,
             OO_CH_MEM_PRESSURE, OO_CH_INFERENCE_STEP),

    MOD_DESC(OO_MOD_DIOPION,       OO_MOD_CAT_ADAPTATION,
             "diopion", &g_diopion,
             diopion_mod_init, NULL, diopion_mod_handle,
             0,
             OO_CH_SENTINEL_TRIP),

    MOD_DESC(OO_MOD_MORPHION,      OO_MOD_CAT_ADAPTATION,
             "morphion", &g_morphion,
             morphion_mod_init, NULL, morphion_mod_handle,
             0,
             OO_CH_HW_IDENTITY, OO_CH_BOOT_PHASE),

    MOD_DESC(OO_MOD_SYMBION,       OO_MOD_CAT_ADAPTATION,
             "symbion", &g_symbion,
             symbion_mod_init, symbion_mod_tick, symbion_mod_handle,
             64,
             OO_CH_TOKEN_OUT),

    MOD_DESC(OO_MOD_EVOLVION,      OO_MOD_CAT_ADAPTATION,
             "evolvion", &g_evolvion,
             evolvion_mod_init, NULL, evolvion_mod_handle,
             0,
             OO_CH_TRAIN_SAMPLE, OO_CH_DPLUS_VERDICT),

    /* ── MEMORY ───────────────────────────────────────────────── */
    MOD_DESC(OO_MOD_MEMORION,      OO_MOD_CAT_MEMORY,
             "memorion", &g_memorion,
             memorion_mod_init, memorion_mod_tick, memorion_mod_handle,
             128,
             OO_CH_INFERENCE_STEP),

    MOD_DESC(OO_MOD_SYNAPTION,     OO_MOD_CAT_MEMORY,
             "synaption", &g_synaption,
             synaption_mod_init, NULL, synaption_mod_handle,
             0,
             OO_CH_MEM_PRESSURE),

    MOD_DESC(OO_MOD_PHEROMION,     OO_MOD_CAT_MEMORY,
             "pheromion", &g_pheromion,
             pheromion_mod_init, NULL, pheromion_mod_handle,
             0,
             OO_CH_TOKEN_OUT),

    MOD_DESC(OO_MOD_DREAMION,      OO_MOD_CAT_MEMORY,
             "dreamion", &g_dreamion,
             dreamion_mod_init, dreamion_mod_tick, dreamion_mod_handle,
             256,  /* only active in idle */
             OO_CH_KVC_SAVED, OO_CH_IDLE_START, OO_CH_IDLE_END),

    MOD_DESC(OO_MOD_NEURALFS,      OO_MOD_CAT_MEMORY,
             "neuralfs", &g_neuralfs,
             neuralfs_mod_init, neuralfs_mod_tick, neuralfs_mod_handle,
             8,    /* tick every 8 steps */
             OO_CH_TOKEN_OUT, OO_CH_KVC_SAVED, OO_CH_INFERENCE_STEP),

    /* ── IDENTITY ─────────────────────────────────────────────── */
    MOD_DESC(OO_MOD_DJIBION,       OO_MOD_CAT_IDENTITY,
             "djibion", &g_djibion,
             djibion_mod_init, NULL, djibion_mod_handle,
             0,
             OO_CH_HW_IDENTITY, OO_CH_DPLUS_VERDICT),

    MOD_DESC(OO_MOD_IMMUNION,      OO_MOD_CAT_IDENTITY,
             "immunion", &g_immunion,
             immunion_mod_init, NULL, immunion_mod_handle,
             0,
             OO_CH_SENTINEL_TRIP),

    /* ── SOCIAL ───────────────────────────────────────────────── */
    MOD_DESC(OO_MOD_COLLECTIVION,  OO_MOD_CAT_SOCIAL,
             "collectivion", &g_collectivion,
             collectivion_mod_init, collectivion_mod_tick, collectivion_mod_handle,
             64,
             OO_CH_CONSENSUS),

    MOD_DESC(OO_MOD_GHOST,         OO_MOD_CAT_SOCIAL,
             "ghost", &g_ghost,
             ghost_mod_init, NULL, ghost_mod_handle,
             0,
             OO_CH_BROADCAST),

    /* ── META ─────────────────────────────────────────────────── */
    MOD_DESC(OO_MOD_ORCHESTRION,   OO_MOD_CAT_META,
             "orchestrion", &g_orchestrion,
             orchestrion_mod_init, orchestrion_mod_tick, orchestrion_mod_handle,
             1,    /* tick every step */
             OO_CH_BOOT_DONE, OO_CH_MEM_PRESSURE),

    MOD_DESC(OO_MOD_CELLION,       OO_MOD_CAT_META,
             "cellion", &g_cellion,
             cellion_mod_init, NULL, cellion_mod_handle,
             0,
             OO_CH_BOOT_DONE, OO_MOD_CHANNEL(OO_MOD_CELLION)),

    MOD_DESC(OO_MOD_DESKTOP_DISPLAY, OO_MOD_CAT_META,
             "desktop_display", &g_desktop_display,
             desktop_display_mod_init, desktop_display_mod_tick, desktop_display_mod_handle,
             16,
             OO_CH_MEM_PRESSURE, OO_CH_DPLUS_VERDICT, OO_CH_SENTINEL_TRIP, OO_CH_INFERENCE_STEP,
             OO_CH_CONSENSUS, OO_CH_IDLE_START, OO_CH_IDLE_END, OO_MOD_CHANNEL(OO_MOD_DIOPION)),
};

/* ============================================================
 * oo_bus_init — main wiring entry point
 * Declared in oo_bus_channels.h, implemented here.
 * ============================================================ */

void oo_bus_init(void *hermes_ctx, void *oo_boot_ctx) {
    (void)hermes_ctx; /* reserved for future olympe integration */

    /* 1. Initialize the freestanding router */
    oo_bus_router_init();

    /* 2. Zero-fill module table */
    {
        uint8_t *p = (uint8_t *)&g_oo_module_table;
        for (uint32_t i = 0; i < (uint32_t)sizeof(OoModuleTable); i++) p[i] = 0;
    }

    /* 3. Register all modules */
    for (uint32_t i = 0; i < OO_MOD_COUNT; i++) {
        oo_module_table_register(&k_module_descriptors[i]);
    }

    /* 4. Init all modules (calls each module's init_fn with boot context) */
    oo_module_table_init_all((OoBootCtx *)oo_boot_ctx);

    /* 5. Register REPL /commands from modules and subsystems.
     *    These are available before repl_run() via repl_find_cmd().
     *    Called here so Phase 6 (engine init) owns all command registrations.
     */
    cmd_dna_register();
    cmd_pressure_register();
    cmd_conscience_register();
    cmd_immunion_register();
    cmd_splitbrain_register();
    cmd_hebbian_register();
    cmd_kv_persist_register();
    cmd_wasm_register();
    cmd_pheromion_register();
    cmd_evolvion_register();
    cmd_neuralfs_register();
    cmd_desktop_display_register();
}
