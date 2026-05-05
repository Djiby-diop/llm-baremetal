// oo_voice_desktop_bridge.h — Voice Engine ↔ SOMA Desktop Bridge
//
// Wires the voice system to the SOMA HUD display:
//   - Wake word detected → sphere white pulse + "LISTENING..." overlay
//   - OO speaking (TTS) → waveform animation ring in vortex
//   - Inference running → vortex speed ↑, orbital rings brighten
//   - New voice response → update response panel text
//   - Warden pressure spike → glitch effect on HUD
//   - Emotion change → vortex color shift (blue=calm, white=inference,
//                                          red=alert, cyan=curious)
//
// This bridge owns a shared state struct (OoDesktopState) that both
// the voice engine and the HUD renderer read/write.
//
// Thread-safety: bare-metal, single core → no locks needed.
// For SMP: writer = voice thread (core 0), reader = render thread (core 1)
//   → use volatile + write-once semantics for each field.
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>
#include "oo_persona.h"
#include "oo_wakeword.h"
#include "oo_tts_phoneme.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Desktop visual state (shared between voice + HUD renderer) ────────────────

#define OO_BRIDGE_MAX_RESPONSE_LINES  6
#define OO_BRIDGE_LINE_LEN           80

typedef enum {
    OO_VORTEX_IDLE      = 0,   // slow rotation, dim blue
    OO_VORTEX_LISTENING = 1,   // medium, pulse white
    OO_VORTEX_THINKING  = 2,   // fast, bright white (LLM inference)
    OO_VORTEX_SPEAKING  = 3,   // medium, waveform ripple cyan
    OO_VORTEX_ALERT     = 4,   // erratic, red
} OoVortexState;

typedef struct {
    // ── Vortex / core animation ───────────────────────────────────────────────
    volatile OoVortexState  vortex_state;
    volatile uint32_t       vortex_speed_x100;  // rotation speed × 100 (100 = 1.0)
    volatile uint8_t        vortex_r, vortex_g, vortex_b; // core color
    volatile uint8_t        vortex_brightness;  // 0-255
    volatile int            pulse_active;        // 1 = white pulse expanding
    volatile uint8_t        pulse_radius;        // pulse expansion 0-255

    // ── Orbital rings state ───────────────────────────────────────────────────
    volatile uint8_t        ring_brightness[8]; // per-ring brightness override
    volatile uint8_t        ring_speed_mul;     // ring speed multiplier (100=1×)

    // ── Waveform (TTS speaking animation) ────────────────────────────────────
    volatile int            waveform_active;    // 1 = TTS playing
    volatile int16_t        waveform[64];       // last 64 PCM samples (for viz)
    volatile int            waveform_head;

    // ── Response panel text ───────────────────────────────────────────────────
    volatile char           response_lines[OO_BRIDGE_MAX_RESPONSE_LINES][OO_BRIDGE_LINE_LEN];
    volatile int            response_count;
    volatile int            response_dirty;     // 1 = needs redraw

    // ── Status bar ───────────────────────────────────────────────────────────
    volatile char           status_intent[64];  // last matched intent label
    volatile char           status_mode[32];    // persona mode name
    volatile char           status_emotion[32]; // current emotion name
    volatile uint8_t        warden_pressure;    // 0-255

    // ── Wake word ─────────────────────────────────────────────────────────────
    volatile int            wake_detected;      // 1 = just detected (read + clear)
    volatile char           wake_word[32];      // which wake word triggered

    // ── Glitch ────────────────────────────────────────────────────────────────
    volatile int            glitch_active;      // 1 = render glitch frame
    volatile uint8_t        glitch_intensity;   // 0-255

    // ── System clocks ─────────────────────────────────────────────────────────
    volatile uint64_t       frame_tick;         // incremented each render frame
    volatile uint32_t       uptime_seconds;     // OO uptime
    volatile char           time_str[8];        // "HH:MM\0" from RTC
} OoDesktopState;

// ── Bridge engine ─────────────────────────────────────────────────────────────

typedef struct {
    OoDesktopState   *state;    // shared state (must outlive bridge)
    OoPersona        *persona;  // persona engine
    OoTtsPcmBuffer   *tts_buf;  // TTS output buffer
    int               initialized;
} OoVoiceDesktopBridge;

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize bridge (call once, after persona + TTS + HUD init)
void oo_bridge_init(OoVoiceDesktopBridge *b,
                    OoDesktopState *state,
                    OoPersona *persona,
                    OoTtsPcmBuffer *tts_buf);

// Call when wake word is detected
void oo_bridge_on_wake(OoVoiceDesktopBridge *b, const char *wake_word);

// Call when voice input starts (VAD triggered, mic open)
void oo_bridge_on_listening_start(OoVoiceDesktopBridge *b);

// Call when voice input ends (VAD silence, processing begins)
void oo_bridge_on_listening_end(OoVoiceDesktopBridge *b);

// Call when LLM inference starts (query sent to engine)
void oo_bridge_on_inference_start(OoVoiceDesktopBridge *b);

// Call when LLM inference ends (response ready)
void oo_bridge_on_inference_end(OoVoiceDesktopBridge *b);

// Call when OO starts speaking (TTS active)
void oo_bridge_on_tts_start(OoVoiceDesktopBridge *b);

// Call when OO finishes speaking
void oo_bridge_on_tts_end(OoVoiceDesktopBridge *b);

// Push a new response text to the display panel
// Wraps long lines, scrolls up when full
void oo_bridge_push_response(OoVoiceDesktopBridge *b,
                              const char *label,  // e.g., "OO> "
                              const char *text, int text_len);

// Push user input echo to display panel
void oo_bridge_push_user(OoVoiceDesktopBridge *b, const char *text, int len);

// Update emotion state → changes vortex color
void oo_bridge_set_emotion(OoVoiceDesktopBridge *b, OoPersonaEmotion emotion);

// Update warden pressure → triggers glitch effect if > 200
void oo_bridge_set_warden(OoVoiceDesktopBridge *b, uint8_t pressure);

// Feed PCM samples to waveform visualizer (called from HDA DMA handler)
void oo_bridge_feed_pcm(OoVoiceDesktopBridge *b,
                         const int16_t *samples, int count);

// Update time string (call from RTC tick handler)
void oo_bridge_set_time(OoVoiceDesktopBridge *b, const char *hhmm);

// Per-frame update: advance animations, decay pulse, etc. (call from render loop)
void oo_bridge_tick(OoVoiceDesktopBridge *b);

// Poll and clear wake_detected flag (returns 1 if wake just happened)
int oo_bridge_poll_wake(OoVoiceDesktopBridge *b, char *word_out, int cap);

// Get current vortex color as RGB (for renderer)
void oo_bridge_vortex_color(const OoVoiceDesktopBridge *b,
                             uint8_t *r, uint8_t *g, uint8_t *b_ch);

#ifdef __cplusplus
}
#endif
