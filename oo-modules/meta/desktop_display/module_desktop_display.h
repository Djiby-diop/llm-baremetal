/* oo-modules/meta/desktop_display/module_desktop_display.h
 * OO-native desktop display bridge: bus-fed HUD state for REPL/UEFI surfaces.
 */
#ifndef OO_MOD_DESKTOP_DISPLAY_H
#define OO_MOD_DESKTOP_DISPLAY_H

#include <stdint.h>
#include <stdint.h>
#include "oo-bus/hermes/oo_bus_channels.h"
#include "oo-kernel/boot/oo_boot.h"

typedef enum {
    OO_DISPLAY_NODE_ACTIVE    = 0,
    OO_DISPLAY_NODE_SYNCING   = 1,
    OO_DISPLAY_NODE_DEGRADED  = 2,
    OO_DISPLAY_NODE_ISOLATED  = 3,
    OO_DISPLAY_NODE_EMERGENCY = 4,
} OoDisplayNodeState;

typedef struct {
    uint32_t node_state;
    uint32_t boot_phase;
    uint32_t boot_ok;
    uint32_t idle;
    uint32_t idle_turn;
    uint32_t last_input_len;
    uint32_t pressure_pct;
    uint32_t pressure_level;
    uint32_t temp_scale;
    uint32_t deny_allocs;
    uint32_t temp_celsius;
    uint32_t temp_valid;
    uint32_t stress;
    uint32_t splitbrain_phase;
    uint32_t sentinel_tripped;
    uint32_t sentinel_trip_count;
    uint32_t last_trip_code;
    uint32_t dplus_mode;
    uint32_t dplus_evals;
    uint32_t dplus_denies;
    uint32_t dplus_audits;
    uint32_t last_dplus_verdict;
    uint32_t policy_events;
    char     last_pattern[32];
    uint32_t inference_pos;
    uint32_t tokens_generated;
    uint64_t cycles_elapsed;
    uint32_t swarm_events;
    uint32_t consensus_winner;
    uint32_t divergence_score;
    uint32_t last_output_len;
    uint32_t somamind_ready;
    uint32_t somamind_tools;
    uint32_t somamind_last_halt;
    uint32_t somamind_tokens_generated;
    uint64_t somamind_tokens_saved;
    uint32_t render_generation;
    uint32_t publish_count;
    uint32_t last_channel;
} DesktopDisplayEngine;

typedef struct {
    uint32_t node_state;
    uint32_t pressure_pct;
    uint32_t temp_celsius;
    uint32_t splitbrain_phase;
    uint32_t sentinel_tripped;
    uint32_t dplus_denies;
    uint32_t consensus_winner;
    uint32_t divergence_score;
    uint32_t somamind_ready;
    uint32_t render_generation;
    uint32_t publish_count;
    uint32_t last_channel;
} OoBusDesktopDisplaySnapshot;

extern DesktopDisplayEngine g_desktop_display;

void desktop_display_mod_init(void *e, const OoBootCtx *ctx);
void desktop_display_mod_tick(void *e, OoBootCtx *ctx);
void desktop_display_mod_handle(void *e, uint16_t ch, const void *payload, uint32_t len);
const char *desktop_display_node_state_name(uint32_t st);

#endif
