#pragma once
#ifndef OO_SOMA_BRIDGE_H
#define OO_SOMA_BRIDGE_H

#include <stdint.h>

/* ── Node state ─────────────────────────────────────────────────── */
#ifndef OO_SWARM_NODE_H   /* avoid conflict with oo_swarm_node.h */
typedef enum {
    OO_ACTIVE    = 0,   /* cyan    */
    OO_DEGRADED  = 1,   /* amber   */
    OO_ISOLATED  = 2,   /* red     */
    OO_EMERGENCY = 3,   /* white   */
    OO_SLEEPING  = 4    /* blue    */
} OoNodeState;
#else
/* oo_swarm_node.h defines OO_NODE_* — provide aliases for HUD code */
#define OO_ACTIVE    OO_NODE_ACTIVE
#define OO_DEGRADED  OO_NODE_DEGRADED
#define OO_ISOLATED  OO_NODE_ISOLATED
#define OO_EMERGENCY 5   /* visual: critical — no swarm equivalent */
#define OO_SLEEPING  6   /* visual: low-power — no swarm equivalent */
#endif

/* ── D+ mode ────────────────────────────────────────────────────── */
/* DplusMode enum is defined in oo-warden/dplus/dplus.h; uint8_t used here to avoid conflict */

/* ── Arena info ─────────────────────────────────────────────────── */
typedef struct {
    char     name[12];
    uint32_t used_mb;
    uint32_t total_mb;
} SomaArenaInfo;

/* ── Bus event ──────────────────────────────────────────────────── */
typedef struct {
    uint64_t tsc;
    char     kind[16];
    char     desc[48];
} SomaBusEvent;

/* ── Peer ───────────────────────────────────────────────────────── */
typedef struct {
    char        peer_id[16];
    OoNodeState state;
    uint32_t    latency_ms;
    int         active;
} SomaPeer;

/* ── Full system state ──────────────────────────────────────────── */
typedef struct {
    char         organism_id[32];
    OoNodeState  node_state;
    uint8_t      dplus_mode;        /* 0=SOLAR 1=LUNAR 2=SAFE — matches DplusMode enum */
    uint8_t      warden_pressure;   /* 0-255 */
    uint32_t     swarm_peer_count;

    SomaArenaInfo arenas[5];
    int           arena_count;

    SomaBusEvent  events[8];
    int           event_head;
    int           event_count;

    SomaPeer      peers[6];
    int           peer_count;

    uint64_t      tsc_now;
    uint64_t      tsc_hz;
    uint32_t      uptime_sec;
    uint32_t      tokens_per_sec;
    uint64_t      tokens_generated;

    char          response_buf[384];
    char          input_buf[128];
    int           input_len;

    int           voice_active;
    uint8_t       voice_waveform[64];
    int           glitch_frames;
} SomaSystemState;

/* ── Declarations ───────────────────────────────────────────────── */
/* soma_uart_init is defined as static inline in ssm/soma_uart.h — do not redeclare */
void soma_uart_poll(SomaSystemState *state);
void soma_state_demo_fill(SomaSystemState *state, uint32_t tick);

#endif /* OO_SOMA_BRIDGE_H */
