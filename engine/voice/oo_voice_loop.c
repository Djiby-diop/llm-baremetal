// oo_voice_loop.c — Full Voice Pipeline Implementation
//
// Pipeline stages:
//   IDLE     → HDA capture → oww_feed() → oww_poll() → WAKE
//   WAKE     → bridge + UART event → LISTEN
//   LISTEN   → accumulate keyboard/UART text OR timeout → PROCESS
//   PROCESS  → ovr_route_with_persona() → TTS generate → SPEAK
//   SPEAK    → drain TTS PCM buffer → upsample 16→48kHz → HDA play → COOLDOWN
//   COOLDOWN → tick down → IDLE
//
// Freestanding C11 — no libc, no malloc.

#include "oo_voice_loop.h"
#include "oo_wakeword.h"
#include "oo_voice_router.h"
#include "oo_voice_context.h"
#include "oo_persona.h"
#include "oo_tts_phoneme.h"
#include "oo_voice_desktop_bridge.h"
#include "../drivers/oo_audio_hda.h"

// ── UART emit (inline — no separate header needed for Ring 0) ─────────────────

static inline void _uart_outb_vl(uint8_t v) {
    __asm__ __volatile__("outb %0,$0x3F8" : : "a"(v));
}
static inline int _uart_tx_ready(void) {
    uint8_t s;
    __asm__ __volatile__("inb $0x3FD,%0" : "=a"(s));
    return (s & 0x20) != 0;
}
static void _uart_putc(char c) {
    for (int i = 0; i < 8000; i++) if (_uart_tx_ready()) break;
    _uart_outb_vl((uint8_t)c);
}
static void _uart_puts(const char *s) { while (*s) _uart_putc(*s++); }

// Minimal itoa for UART emit
static void _u8_to_str(uint8_t v, char *out) {
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    char tmp[4]; int i = 0;
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0; while (i > 0) out[j++] = tmp[--i]; out[j] = '\0';
}

// ── Static engine instances (zero dynamic allocation) ─────────────────────────

static OwwEngine       s_oww;
static OvrEngine       s_router;
static OvcContext      s_ctx;
static OoPersona       s_persona;
static OoTtsSynth      s_tts;
static OoTtsPcmBuffer  s_tts_buf;

// ── Capture + upsample scratch buffers (static, no malloc) ───────────────────

// 16kHz capture: 682 samples per HDA BDL fill (~42ms)
static int16_t s_cap16[682];

// 48kHz upsample output: 682 × 3 = 2046 samples, stereo = 4092 int16_t
// (L+R interleaved: 682 × 3 × 2 = 4092)
#define UPSAMPLE_BUF_CAP  4096
static int16_t s_up48[UPSAMPLE_BUF_CAP];

// Text injection buffer (keyboard / UART input)
#define TEXT_BUF_CAP  256
static char    s_text_buf[TEXT_BUF_CAP];
static int     s_text_len = 0;
static int     s_text_ready = 0;  // 1 = text is ready for processing

// Last response / last cmd (for external query)
static char s_last_response[256];
static char s_last_cmd[256];

// ── State machine ─────────────────────────────────────────────────────────────

static OvlState  s_state     = OVL_IDLE;
static int       s_state_ctr = 0;   // ticks in current state
static int       s_uart_emit = 0;
static OoAudioHda           *s_hda    = (void *)0;
static OoVoiceDesktopBridge *s_bridge = (void *)0;

// Timeouts (in ticks at ~60Hz)
#define WAKE_PULSE_TICKS    18    // 0.3s visual pulse
#define LISTEN_TIMEOUT_TICKS 300  // 5s max listen window
#define COOLDOWN_TICKS       90   // 1.5s post-TTS cooldown

// ── TTS → HDA upsampler (16kHz mono → 48kHz stereo interleaved) ──────────────
//
// Simple linear interpolation: for each pair (s0, s1), output 3 samples.
// Then duplicate mono→stereo (L=R).
// Returns number of int16_t written into out (always n_16k * 6).

static int _upsample_16to48_stereo(const int16_t *in16, int n,
                                    int16_t *out48, int out_cap) {
    int out_needed = n * 6;  // 3 time steps × 2 channels
    if (out_needed > out_cap) n = out_cap / 6;
    int oi = 0;
    for (int i = 0; i < n; i++) {
        int32_t s0 = in16[i];
        int32_t s1 = (i + 1 < n) ? in16[i + 1] : 0;
        int16_t t0 = (int16_t)s0;
        int16_t t1 = (int16_t)((s0 * 2 + s1) / 3);
        int16_t t2 = (int16_t)((s0     + s1 * 2) / 3);
        // Stereo interleaved: L, R, L, R, L, R
        out48[oi++] = t0; out48[oi++] = t0;
        out48[oi++] = t1; out48[oi++] = t1;
        out48[oi++] = t2; out48[oi++] = t2;
    }
    return oi;
}

// ── UART voice state emitter ──────────────────────────────────────────────────

static void _emit_state(const char *emotion, int inference, int wake,
                         const int16_t *wave16, int wave_n) {
    if (!s_uart_emit) return;
    _uart_puts("OO_VOICE:{\"emotion\":\"");
    _uart_puts(emotion);
    char nb[6];
    _uart_puts("\",\"vortex_speed_mul\":");
    _uart_puts(inference ? "2.5" : "1.0");
    _uart_puts(",\"wake_pulse\":");
    _u8_to_str((uint8_t)(wake ? 1 : 0), nb); _uart_puts(nb);
    _uart_puts(",\"glitch\":");
    _u8_to_str((uint8_t)(wake ? 8 : 0), nb); _uart_puts(nb);
    _uart_puts(",\"inference\":");
    _uart_puts(inference ? "true" : "false");
    _uart_puts(",\"waveform\":[");
    // Emit 64 evenly-spaced samples from wave (or silence if NULL)
    for (int i = 0; i < 64; i++) {
        uint8_t v = 128;
        if (wave16 && wave_n > 0) {
            int idx = (int)((uint32_t)i * (uint32_t)wave_n / 64u);
            int32_t raw = wave16[idx];
            raw = (raw + 32768) >> 8;  // int16 → 0-255
            v = (uint8_t)(raw < 0 ? 0 : raw > 255 ? 255 : raw);
        }
        _u8_to_str(v, nb); _uart_puts(nb);
        if (i < 63) _uart_putc(',');
    }
    _uart_puts("],\"response\":\"");
    if (s_last_response[0]) {
        // Minimal JSON escape
        const char *r = s_last_response;
        while (*r) {
            if (*r == '"') { _uart_putc('\\'); _uart_putc('"'); }
            else if (*r == '\\') { _uart_putc('\\'); _uart_putc('\\'); }
            else _uart_putc(*r);
            r++;
        }
    }
    _uart_puts("\"}\r\n");
}

// ── String helpers (no libc) ──────────────────────────────────────────────────

static int _slen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static void _scpy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

// ── Public API ────────────────────────────────────────────────────────────────

int oo_voice_loop_init(const OvlConfig *cfg) {
    if (!cfg || !cfg->hda) return -1;

    s_hda        = cfg->hda;
    s_bridge     = cfg->bridge;
    s_uart_emit  = cfg->uart_emit;

    // Init wakeword engine (patterns: "oo", "hey oo", "salut oo")
    oww_init(&s_oww);

    // Init router + context + persona
    ovr_init(&s_router);
    ovc_init(&s_ctx);
    oo_persona_init(&s_persona);

    // Init TTS
    oo_tts_init(&s_tts, cfg->lang_fr);
    oo_tts_pcm_init(&s_tts_buf);

    // Start HDA capture
    int ret = oo_audio_hda_capture_start(s_hda);

    // Initial UART emit: IDLE state
    _emit_state("DORMANT", 0, 0, (void*)0, 0);

    s_state     = OVL_IDLE;
    s_state_ctr = 0;
    s_text_len  = 0;
    s_text_ready = 0;
    s_last_response[0] = '\0';
    s_last_cmd[0] = '\0';

    return ret > 0 ? 0 : -2;
}

void oo_voice_loop_inject(const char *text, int len) {
    if (!text) return;
    if (len < 0) len = _slen(text);
    if (len <= 0) return;
    if (len >= TEXT_BUF_CAP) len = TEXT_BUF_CAP - 1;
    for (int i = 0; i < len; i++) s_text_buf[i] = text[i];
    s_text_buf[len] = '\0';
    s_text_len   = len;
    s_text_ready = 1;
    // If we were idle, skip straight to LISTEN so next tick processes it
    if (s_state == OVL_IDLE) {
        s_state     = OVL_LISTEN;
        s_state_ctr = 0;
    }
}

OvlState oo_voice_loop_state(void) { return s_state; }

const char *oo_voice_loop_last_response(void) { return s_last_response; }
const char *oo_voice_loop_last_cmd(void)      { return s_last_cmd; }

// ── Main tick (call ~60Hz) ────────────────────────────────────────────────────

OvlState oo_voice_loop_tick(void) {
    s_state_ctr++;

    switch (s_state) {

    // ── IDLE: capture audio, feed wakeword engine ─────────────────────────
    case OVL_IDLE: {
        int n = oo_audio_hda_read_samples(s_hda, s_cap16, 682);
        if (n > 0) {
            oww_feed(&s_oww, s_cap16, n);
            // Forward waveform to desktop bridge
            if (s_bridge) oo_bridge_feed_pcm(s_bridge, s_cap16, n);
        }
        if (oww_poll(&s_oww)) {
            // Wake word detected!
            const char *word = oww_last_word(&s_oww);
            if (s_bridge) {
                oo_bridge_on_wake(s_bridge, word);
                oo_bridge_on_listening_start(s_bridge);
                oo_bridge_set_emotion(s_bridge, EMOTION_CURIOUS);
            }
            _emit_state("WAKE", 0, 1, s_cap16, n);
            s_state     = OVL_WAKE;
            s_state_ctr = 0;
        }
        // Also check for injected text (keyboard in IDLE)
        if (s_text_ready) {
            s_state     = OVL_LISTEN;
            s_state_ctr = 0;
        }
        break;
    }

    // ── WAKE: brief visual pulse (WAKE_PULSE_TICKS ticks) ────────────────
    case OVL_WAKE: {
        if (s_state_ctr >= WAKE_PULSE_TICKS) {
            s_state     = OVL_LISTEN;
            s_state_ctr = 0;
            s_text_ready = 0;
            s_text_len   = 0;
        }
        break;
    }

    // ── LISTEN: wait for text injection or timeout ────────────────────────
    case OVL_LISTEN: {
        // Keep capturing audio for waveform animation
        int n = oo_audio_hda_read_samples(s_hda, s_cap16, 682);
        if (n > 0 && s_bridge) oo_bridge_feed_pcm(s_bridge, s_cap16, n);
        _emit_state("CURIOUS", 0, 0, n > 0 ? s_cap16 : (void*)0, n);

        if (s_text_ready) {
            // Got text — move to process
            if (s_bridge) oo_bridge_on_listening_end(s_bridge);
            s_state     = OVL_PROCESS;
            s_state_ctr = 0;
        } else if (s_state_ctr >= LISTEN_TIMEOUT_TICKS) {
            // Timeout — go back to idle
            if (s_bridge) {
                oo_bridge_on_listening_end(s_bridge);
                oo_bridge_set_emotion(s_bridge, EMOTION_DORMANT);
            }
            _emit_state("DORMANT", 0, 0, (void*)0, 0);
            s_state     = OVL_IDLE;
            s_state_ctr = 0;
        }
        break;
    }

    // ── PROCESS: NLP routing → generate TTS ──────────────────────────────
    case OVL_PROCESS: {
        if (s_bridge) {
            oo_bridge_on_inference_start(s_bridge);
            oo_bridge_set_emotion(s_bridge, EMOTION_FOCUSED);
        }
        _emit_state("FOCUSED", 1, 0, (void*)0, 0);

        // Route through persona + NLP
        OvrContextResult res = ovr_route_with_persona(
            &s_router, &s_ctx, &s_persona,
            s_text_buf, s_text_len
        );

        // Store results
        _scpy(s_last_response, res.reply, sizeof(s_last_response));
        _scpy(s_last_cmd, res.route.cmd, sizeof(s_last_cmd));

        // Push to context log
        ovc_push_human(&s_ctx, s_text_buf, res.route.cmd,
                       res.route.label, res.route.score, (uint32_t)s_state_ctr);
        ovc_push_oo(&s_ctx, res.reply, (uint32_t)s_state_ctr);

        if (s_bridge) {
            oo_bridge_push_user(s_bridge, s_text_buf, s_text_len);
            oo_bridge_push_response(s_bridge, res.route.label,
                                    res.reply, _slen(res.reply));
            oo_bridge_on_inference_end(s_bridge);
        }

        // Generate TTS PCM
        oo_tts_pcm_init(&s_tts_buf);  // clear buffer
        if (res.reply[0]) {
            oo_tts_speak(&s_tts, &s_tts_buf, res.reply, _slen(res.reply));
        }

        // Clear injection state
        s_text_ready = 0;
        s_text_len   = 0;
        s_text_buf[0] = '\0';

        _emit_state("SPEAKING", 0, 0, (void*)0, 0);

        if (s_bridge) {
            oo_bridge_on_tts_start(s_bridge);
            oo_bridge_set_emotion(s_bridge, EMOTION_PROUD);
        }

        s_state     = OVL_SPEAK;
        s_state_ctr = 0;
        break;
    }

    // ── SPEAK: drain TTS PCM buffer → upsample → HDA playback ────────────
    case OVL_SPEAK: {
        // Consume up to 682 samples per tick (42ms @ 16kHz)
        int16_t tmp16[682];
        int got = oo_tts_pcm_consume(&s_tts_buf, tmp16, 682);

        if (got > 0) {
            // Forward waveform to bridge
            if (s_bridge) oo_bridge_feed_pcm(s_bridge, tmp16, got);
            _emit_state("SPEAKING", 0, 0, tmp16, got);

            // Upsample 16kHz mono → 48kHz stereo
            int n48 = _upsample_16to48_stereo(tmp16, got,
                                               s_up48, UPSAMPLE_BUF_CAP);
            // Play via HDA (stereo int16 @ 48kHz)
            oo_audio_hda_play_pcm(s_hda, s_up48, (uint32_t)n48);
        }

        // Check if TTS buffer exhausted
        if (got <= 0 || s_tts_buf.count <= 0) {
            if (s_bridge) {
                oo_bridge_on_tts_end(s_bridge);
                oo_bridge_set_emotion(s_bridge, EMOTION_FOCUSED);
            }
            _emit_state("FOCUSED", 0, 0, (void*)0, 0);
            s_state     = OVL_COOLDOWN;
            s_state_ctr = 0;
        }
        break;
    }

    // ── COOLDOWN: wait for echo to die (avoid re-triggering wakeword) ─────
    case OVL_COOLDOWN: {
        // Flush any captured audio (discard — prevents echo wake)
        int n = oo_audio_hda_read_samples(s_hda, s_cap16, 682);
        (void)n;

        if (s_state_ctr >= COOLDOWN_TICKS) {
            if (s_bridge) oo_bridge_set_emotion(s_bridge, EMOTION_DORMANT);
            _emit_state("DORMANT", 0, 0, (void*)0, 0);
            s_state     = OVL_IDLE;
            s_state_ctr = 0;
        }
        break;
    }

    } // end switch

    if (s_bridge) oo_bridge_tick(s_bridge);
    return s_state;
}
