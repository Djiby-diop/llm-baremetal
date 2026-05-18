/* REPL surface for the bus-native desktop display HUD. */
#include "oo-kernel/repl/repl.h"
#include <efi.h>
#include <efilib.h>
#include "module_desktop_display.h"
#include "oo-modules/module_registry.h"
#include "oo-modules/module_registry.h"

extern DesktopDisplayEngine g_desktop_display;

static int cmd_display_live(const char *args, ReplInferState *s) {
    const char *node = desktop_display_node_state_name(g_desktop_display.node_state);
    (void)args;
    (void)s;
    Print(L"\r\n[DISPLAY] OO desktop live HUD\r\n");
    Print(L"  node=%a boot_phase=%lu ok=%lu idle=%lu turn=%lu render=%lu\r\n",
          node,
          (unsigned long)g_desktop_display.boot_phase,
          (unsigned long)g_desktop_display.boot_ok,
          (unsigned long)g_desktop_display.idle,
          (unsigned long)g_desktop_display.idle_turn,
          (unsigned long)g_desktop_display.render_generation);
    Print(L"  memory: pressure=%lu%% level=%lu temp_scale=%lu%% deny_allocs=%lu\r\n",
          (unsigned long)g_desktop_display.pressure_pct,
          (unsigned long)g_desktop_display.pressure_level,
          (unsigned long)g_desktop_display.temp_scale,
          (unsigned long)g_desktop_display.deny_allocs);
    Print(L"  thermal: temp=%luC valid=%lu stress=%lu splitbrain=%lu\r\n",
          (unsigned long)g_desktop_display.temp_celsius,
          (unsigned long)g_desktop_display.temp_valid,
          (unsigned long)g_desktop_display.stress,
          (unsigned long)g_desktop_display.splitbrain_phase);
    Print(L"  policy/diop: mode=%lu evals=%lu denies=%lu audits=%lu verdict=%lu pattern=%a\r\n",
          (unsigned long)g_desktop_display.dplus_mode,
          (unsigned long)g_desktop_display.dplus_evals,
          (unsigned long)g_desktop_display.dplus_denies,
          (unsigned long)g_desktop_display.dplus_audits,
          (unsigned long)g_desktop_display.last_dplus_verdict,
          g_desktop_display.last_pattern);
    Print(L"  safety: sentinel=%lu trips=%lu last_trip=%lu last_input=%luB\r\n",
          (unsigned long)g_desktop_display.sentinel_tripped,
          (unsigned long)g_desktop_display.sentinel_trip_count,
          (unsigned long)g_desktop_display.last_trip_code,
          (unsigned long)g_desktop_display.last_input_len);
    Print(L"  swarm: events=%lu winner=%lu divergence=%lu output=%lu\r\n",
          (unsigned long)g_desktop_display.swarm_events,
          (unsigned long)g_desktop_display.consensus_winner,
          (unsigned long)g_desktop_display.divergence_score,
          (unsigned long)g_desktop_display.last_output_len);
    Print(L"  somamind: ready=%lu tools=%lu halt=%lu gen=%lu saved=%lu\r\n\r\n",
          (unsigned long)g_desktop_display.somamind_ready,
          (unsigned long)g_desktop_display.somamind_tools,
          (unsigned long)g_desktop_display.somamind_last_halt,
          (unsigned long)g_desktop_display.somamind_tokens_generated,
          (unsigned long)g_desktop_display.somamind_tokens_saved);
    return 0;
}

static int cmd_display_bridge(const char *args, ReplInferState *s) {
    (void)args;
    (void)s;
    Print(L"[DISPLAY] bridge channel=0x%04lx publishes=%lu policy_events=%lu\r\n",
          (unsigned long)g_desktop_display.last_channel,
          (unsigned long)g_desktop_display.publish_count,
          (unsigned long)g_desktop_display.policy_events);
    Print(L"[DISPLAY] module_channel=0x%04x diop_channel=0x%04x desktop_display alive=%lu\r\n",
          (unsigned)OO_MOD_CHANNEL(OO_MOD_DESKTOP_DISPLAY),
          (unsigned)OO_MOD_CHANNEL(OO_MOD_DIOPION),
          (unsigned long)(g_desktop_display.render_generation != 0));
    return 0;
}

static const ReplCmd CMD_DISPLAY_LIVE   = { "/display_live",   "Show OO desktop live HUD",      cmd_display_live   };
static const ReplCmd CMD_DISPLAY_BRIDGE = { "/display_bridge", "Show desktop_display bus bridge", cmd_display_bridge };

void cmd_desktop_display_register(void) {
    repl_register_cmd(&CMD_DISPLAY_LIVE);
    repl_register_cmd(&CMD_DISPLAY_BRIDGE);
}
