// oo_voice_desktop_bridge.c — Voice Engine ↔ SOMA Desktop Bridge (Implementation)
//
// Freestanding C11 — no libc, no malloc.

#include "oo_voice_desktop_bridge.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static void _br_cpy(char *dst, const char *src, int cap) {
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int _br_len(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}

// ── Color table per emotion ───────────────────────────────────────────────────

typedef struct { uint8_t r, g, b; uint32_t speed_x100; } EmotionColor;

static const EmotionColor _emotion_colors[] = {
    [OO_EMOTION_FOCUSED]  = {0x40, 0x90, 0xFF, 120},  // blue
    [OO_EMOTION_CURIOUS]  = {0x00, 0xFF, 0xCC, 140},  // cyan-green
    [OO_EMOTION_ALERT]    = {0xFF, 0x30, 0x20, 300},  // red-orange (fast)
    [OO_EMOTION_DORMANT]  = {0x20, 0x30, 0x80,  60},  // dark indigo (slow)
    [OO_EMOTION_PROUD]    = {0xFF, 0xCC, 0x00, 150},  // gold
    [OO_EMOTION_CAUTIOUS] = {0xFF, 0x80, 0x00, 100},  // amber
};
#define N_EMOTIONS 6

// ── Init ─────────────────────────────────────────────────────────────────────

void oo_bridge_init(OoVoiceDesktopBridge *b,
                    OoDesktopState *state,
                    OoPersona *persona,
                    OoTtsPcmBuffer *tts_buf) {
    if (!b || !state) return;
    b->state       = state;
    b->persona     = persona;
    b->tts_buf     = tts_buf;
    b->initialized = 1;

    // Initial state
    state->vortex_state      = OO_VORTEX_IDLE;
    state->vortex_speed_x100 = 100;
    state->vortex_r          = 0x40;
    state->vortex_g          = 0x90;
    state->vortex_b          = 0xFF;
    state->vortex_brightness = 180;
    state->pulse_active      = 0;
    state->pulse_radius      = 0;
    state->ring_speed_mul    = 100;
    state->waveform_active   = 0;
    state->waveform_head     = 0;
    state->response_count    = 0;
    state->response_dirty    = 0;
    state->wake_detected     = 0;
    state->glitch_active     = 0;
    state->glitch_intensity  = 0;
    state->warden_pressure   = 0;
    state->frame_tick        = 0;
    state->uptime_seconds    = 0;
    for (int i = 0; i < 8; i++) state->ring_brightness[i] = 200;
    _br_cpy((char*)state->status_mode, "COLLABORATOR", sizeof(state->status_mode));
    _br_cpy((char*)state->status_emotion, "FOCUSED", sizeof(state->status_emotion));
    _br_cpy((char*)state->status_intent, "---", sizeof(state->status_intent));
    _br_cpy((char*)state->time_str, "00:00", sizeof(state->time_str));

    // Welcome message
    oo_bridge_push_response(b, "OO> ", "System initialized. I am listening.", 37);
}

// ── Wake word ─────────────────────────────────────────────────────────────────

void oo_bridge_on_wake(OoVoiceDesktopBridge *b, const char *wake_word) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;

    // White pulse from center
    s->pulse_active   = 1;
    s->pulse_radius   = 0;
    s->vortex_r       = 0xFF;
    s->vortex_g       = 0xFF;
    s->vortex_b       = 0xFF;
    s->vortex_brightness = 255;
    s->vortex_speed_x100 = 200;
    s->wake_detected  = 1;
    if (wake_word) _br_cpy((char*)s->wake_word, wake_word, sizeof(s->wake_word));
}

// ── Listening state ───────────────────────────────────────────────────────────

void oo_bridge_on_listening_start(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->vortex_state      = OO_VORTEX_LISTENING;
    s->vortex_speed_x100 = 160;
    s->vortex_r          = 0x80;
    s->vortex_g          = 0xFF;
    s->vortex_b          = 0xFF;
    s->vortex_brightness = 220;
    // All rings slightly brighter
    for (int i = 0; i < 8; i++) s->ring_brightness[i] = 230;
    oo_bridge_push_response(b, "   ", "[ listening... ]", 16);
}

void oo_bridge_on_listening_end(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    b->state->vortex_state = OO_VORTEX_THINKING;
}

// ── Inference state ───────────────────────────────────────────────────────────

void oo_bridge_on_inference_start(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->vortex_state      = OO_VORTEX_THINKING;
    s->vortex_speed_x100 = 350;   // fast spin
    s->vortex_r          = 0xFF;
    s->vortex_g          = 0xFF;
    s->vortex_b          = 0xFF;  // white = computing
    s->vortex_brightness = 255;
    s->ring_speed_mul    = 200;   // rings spin 2× faster
    for (int i = 0; i < 8; i++) s->ring_brightness[i] = 255;
    _br_cpy((char*)s->status_intent, "INFERENCE", sizeof(s->status_intent));
}

void oo_bridge_on_inference_end(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->vortex_speed_x100 = 150;
    s->ring_speed_mul    = 100;
}

// ── TTS speaking state ────────────────────────────────────────────────────────

void oo_bridge_on_tts_start(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->vortex_state      = OO_VORTEX_SPEAKING;
    s->vortex_speed_x100 = 130;
    s->vortex_r          = 0x00;
    s->vortex_g          = 0xFF;
    s->vortex_b          = 0xCC;  // cyan = voice output
    s->vortex_brightness = 200;
    s->waveform_active   = 1;
}

void oo_bridge_on_tts_end(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->waveform_active = 0;
    // Return to idle
    oo_bridge_set_emotion(b, b->persona ? b->persona->emotion : OO_EMOTION_FOCUSED);
    s->vortex_state = OO_VORTEX_IDLE;
}

// ── Response panel ────────────────────────────────────────────────────────────

void oo_bridge_push_response(OoVoiceDesktopBridge *b,
                              const char *label, const char *text, int text_len) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;

    // Build line: label + text (truncated to LINE_LEN)
    char line[OO_BRIDGE_LINE_LEN];
    int li = 0;
    // Copy label
    if (label) {
        int ll = _br_len(label);
        for (int i = 0; i < ll && li < OO_BRIDGE_LINE_LEN - 1; i++) line[li++] = label[i];
    }
    // Copy text (up to text_len)
    for (int i = 0; i < text_len && li < OO_BRIDGE_LINE_LEN - 1; i++) line[li++] = text[i];
    line[li] = '\0';

    // Scroll up if full
    int n = (int)s->response_count;
    if (n >= OO_BRIDGE_MAX_RESPONSE_LINES) {
        for (int i = 0; i < OO_BRIDGE_MAX_RESPONSE_LINES - 1; i++) {
            _br_cpy((char*)s->response_lines[i], (const char*)s->response_lines[i+1],
                    OO_BRIDGE_LINE_LEN);
        }
        n = OO_BRIDGE_MAX_RESPONSE_LINES - 1;
    }
    _br_cpy((char*)s->response_lines[n], line, OO_BRIDGE_LINE_LEN);
    s->response_count = n + 1;
    s->response_dirty = 1;
}

void oo_bridge_push_user(OoVoiceDesktopBridge *b, const char *text, int len) {
    oo_bridge_push_response(b, "YOU> ", text, len);
}

// ── Emotion → vortex color ────────────────────────────────────────────────────

void oo_bridge_set_emotion(OoVoiceDesktopBridge *b, OoPersonaEmotion emotion) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;

    int e = (int)emotion;
    if (e < 0 || e >= N_EMOTIONS) e = 0;
    const EmotionColor *c = &_emotion_colors[e];

    s->vortex_r          = c->r;
    s->vortex_g          = c->g;
    s->vortex_b          = c->b;
    s->vortex_speed_x100 = c->speed_x100;
    s->vortex_brightness = 200;

    // Update status label
    static const char *emotion_names[] = {
        "FOCUSED","CURIOUS","ALERT","DORMANT","PROUD","CAUTIOUS"
    };
    _br_cpy((char*)s->status_emotion, emotion_names[e], sizeof(s->status_emotion));
}

// ── Warden pressure → glitch ──────────────────────────────────────────────────

void oo_bridge_set_warden(OoVoiceDesktopBridge *b, uint8_t pressure) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->warden_pressure = pressure;
    if (pressure > 200) {
        s->glitch_active    = 1;
        s->glitch_intensity = (uint8_t)(pressure - 200);
        // Switch to alert emotion
        oo_bridge_set_emotion(b, OO_EMOTION_ALERT);
        s->vortex_state = OO_VORTEX_ALERT;
    } else if (pressure < 100 && s->vortex_state == OO_VORTEX_ALERT) {
        s->glitch_active = 0;
        s->vortex_state  = OO_VORTEX_IDLE;
    }
}

// ── PCM waveform feed ─────────────────────────────────────────────────────────

void oo_bridge_feed_pcm(OoVoiceDesktopBridge *b,
                         const int16_t *samples, int count) {
    if (!b || !b->state || !samples) return;
    OoDesktopState *s = b->state;
    for (int i = 0; i < count; i++) {
        int h = s->waveform_head;
        s->waveform[h] = samples[i];
        s->waveform_head = (h + 1) % 64;
    }
}

// ── Time update ───────────────────────────────────────────────────────────────

void oo_bridge_set_time(OoVoiceDesktopBridge *b, const char *hhmm) {
    if (!b || !b->state || !hhmm) return;
    _br_cpy((char*)b->state->time_str, hhmm, sizeof(b->state->time_str));
}

// ── Per-frame tick ────────────────────────────────────────────────────────────

void oo_bridge_tick(OoVoiceDesktopBridge *b) {
    if (!b || !b->state) return;
    OoDesktopState *s = b->state;
    s->frame_tick++;

    // Decay pulse
    if (s->pulse_active) {
        s->pulse_radius = (uint8_t)(s->pulse_radius + 8);
        if (s->pulse_radius > 200) {
            s->pulse_active = 0;
            s->pulse_radius = 0;
            // Restore emotion color after wake pulse
            if (b->persona)
                oo_bridge_set_emotion(b, b->persona->emotion);
        }
    }

    // Decay glitch
    if (s->glitch_active) {
        if (s->glitch_intensity > 10)
            s->glitch_intensity = (uint8_t)(s->glitch_intensity - 5);
        else {
            s->glitch_active = 0;
            s->glitch_intensity = 0;
        }
    }

    // Reset response_dirty after one frame
    if (s->response_dirty) s->response_dirty = 0;
}

// ── Wake poll ────────────────────────────────────────────────────────────────

int oo_bridge_poll_wake(OoVoiceDesktopBridge *b, char *word_out, int cap) {
    if (!b || !b->state) return 0;
    OoDesktopState *s = b->state;
    if (!s->wake_detected) return 0;
    if (word_out) _br_cpy(word_out, (const char*)s->wake_word, cap);
    s->wake_detected = 0;
    return 1;
}

// ── Vortex color accessor ─────────────────────────────────────────────────────

void oo_bridge_vortex_color(const OoVoiceDesktopBridge *b,
                             uint8_t *r, uint8_t *g, uint8_t *b_ch) {
    if (!b || !b->state) { *r = *g = *b_ch = 0; return; }
    *r    = b->state->vortex_r;
    *g    = b->state->vortex_g;
    *b_ch = b->state->vortex_b;
}
