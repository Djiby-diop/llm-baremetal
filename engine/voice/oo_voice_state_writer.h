// oo_voice_state_writer.h — Voice State JSON Emitter (UART Bridge)
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Emit descriptor ──────────────────────────────────────────────────────────

typedef struct {
    const char *emotion;            // Emotion string (e.g. "FOCUSED", "ALERT")
    uint32_t    vortex_speed_mul_x10; // Speed multiplier × 10 (10 = 1.0×, 25 = 2.5×)
    uint8_t     wake_pulse;         // 1 = wakeword just detected
    uint8_t     glitch;             // Glitch intensity 0–255
    uint8_t     inference;          // 1 = LLM inference running
    uint8_t     waveform[64];       // PCM waveform (16kHz, 64 samples, 0–255)
    const char *response;           // LLM response text (may be NULL)
} OoVoiceStateEmit;

// Emit full state as JSON prefixed with "OO_VOICE:" on UART COM1.
// Host-side uart_bridge.py writes the body to oo_voice_state.json.
void oo_voice_state_emit(const OoVoiceStateEmit *e);

// Fast path: emit emotion + inference flag only (waveform set to silence).
void oo_voice_state_emit_simple(const char *emotion, int inference, int wake);

#ifdef __cplusplus
}
#endif
