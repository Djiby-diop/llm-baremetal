// oo_voice_loop.h — Full Voice Pipeline Orchestrator
//
// Connects:
//   HDA capture (16kHz) → OwwEngine (wakeword) → keyboard/UART text input
//   → OvrEngine (NLP routing) → REPL command → TTS 16kHz → upsample 48kHz
//   → HDA playback + SOMA desktop bridge (emotion/waveform)
//
// Design principles:
//   - Zero dynamic memory (all state static inside .c)
//   - Cooperative loop — call oo_voice_loop_tick() from kernel main loop
//   - Text injection via oo_voice_loop_inject() for keyboard/UART fallback
//   - UART emission at key events for HUD voice bridge
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>
#include "../drivers/oo_audio_hda.h"
#include "oo_voice_desktop_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Forward declarations (avoid pulling heavy headers here) ──────────────────
// (defined fully in oo_audio_hda.h and oo_voice_desktop_bridge.h)

// ── Pipeline states ───────────────────────────────────────────────────────────
typedef enum {
    OVL_IDLE       = 0,  // Always-on capture, feeding wakeword engine
    OVL_WAKE       = 1,  // Wake word just detected — brief animation pulse
    OVL_LISTEN     = 2,  // Collecting text (keyboard / UART) or silence timeout
    OVL_PROCESS    = 3,  // NLP routing + LLM inference
    OVL_SPEAK      = 4,  // TTS playback in progress
    OVL_COOLDOWN   = 5,  // Post-TTS cooldown (prevent echo triggering wakeword)
} OvlState;

// ── Configuration ─────────────────────────────────────────────────────────────
typedef struct {
    OoAudioHda           *hda;           // HDA driver instance (required)
    OoVoiceDesktopBridge *bridge;        // Desktop bridge (may be NULL)
    int                   lang_fr;       // 1 = French TTS, 0 = English
    uint32_t              lapic_ticks_per_ms; // From oo_lapic_calibrate_ms()
    int                   uart_emit;     // 1 = emit OO_VOICE: on UART
} OvlConfig;

// ── Init / Tick ───────────────────────────────────────────────────────────────

// Initialize voice loop (call once after HDA init).
// Returns 0 on success, negative on error.
int oo_voice_loop_init(const OvlConfig *cfg);

// Main tick — call at ~60Hz from kernel main loop (or timer ISR).
// Returns current pipeline state.
OvlState oo_voice_loop_tick(void);

// Inject text for processing (called from keyboard / UART IRQ handler).
// len = -1: null-terminated string.
// Accepted in OVL_LISTEN or OVL_IDLE (triggers immediate PROCESS).
void oo_voice_loop_inject(const char *text, int len);

// Get current state (non-blocking query).
OvlState oo_voice_loop_state(void);

// Get last spoken response (null-terminated, static buffer).
const char *oo_voice_loop_last_response(void);

// Get last REPL command (null-terminated, may be empty).
const char *oo_voice_loop_last_cmd(void);

#ifdef __cplusplus
}
#endif
