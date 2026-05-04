// oo_tts_phoneme.h — Bare-Metal TTS Phoneme Synthesizer
//
// Synthesizes speech from text without any OS, libc, or external library.
// Pipeline: text → phonemes → PCM samples → HDA playback ring buffer
//
// Design constraints (bare-metal):
//   - No malloc, no libc
//   - No eSpeak, no Festival, no neural TTS model
//   - Freestanding C11, ring-0 compatible
//   - PCM output: 16kHz mono 16-bit signed (same as wake word capture)
//
// Algorithm:
//   1. Text normalization (lowercase, punctuation → pauses)
//   2. Grapheme-to-phoneme via static lookup tables (EN + FR)
//   3. Phoneme → waveform via simple synthesis:
//      - Voiced phonemes: bandpass-filtered sawtooth (formant F1+F2)
//      - Unvoiced: shaped noise burst
//      - Silence: zero samples (pauses, spaces)
//   4. Prosody: pitch/duration scale per phoneme category
//   5. Output: samples written to OoTtsPcmBuffer (ring buffer for HDA)
//
// Quality target: intelligible, slightly robotic voice ("SOMA speaks")
// NOT target: natural human voice (that requires neural TTS)
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── PCM output buffer ─────────────────────────────────────────────────────────
#define OO_TTS_PCM_CAP  (16000 * 4)   // 4 seconds max at 16kHz
#define OO_TTS_SAMPLE_RATE  16000

typedef struct {
    int16_t  samples[OO_TTS_PCM_CAP];
    int      head;  // write position
    int      tail;  // read position (consumed by HDA)
    int      count; // samples available
} OoTtsPcmBuffer;

// ── Phoneme inventory (IPA-inspired subset, ASCII-safe) ───────────────────────
// 40 phonemes covering EN + FR essential sounds
typedef enum {
    // ── Vowels (voiced) ──
    PH_AH = 0,   // "cup" (EN) / "a" (FR)
    PH_AE,       // "cat" (EN)
    PH_EH,       // "bed" (EN) / "é" (FR)
    PH_EE,       // "see" (EN) / "i" (FR)
    PH_IH,       // "bit" (EN)
    PH_OH,       // "go" (EN) / "o" (FR)
    PH_OO,       // "boot" (EN) / "ou" (FR)
    PH_UH,       // "book" (EN)
    PH_UR,       // "bird" (EN)
    PH_AA,       // "father" (EN) / "â" (FR)
    PH_OE,       // "heureux" (FR) / "ö" (DE)
    PH_UU,       // "lune" (FR) / "ü"
    PH_EU,       // "feu" (FR)
    PH_AN,       // nasal "en/an" (FR)
    PH_IN,       // nasal "in" (FR)
    PH_UN,       // nasal "un" (FR)
    PH_ON,       // nasal "on" (FR)

    // ── Consonants (voiced) ──
    PH_B,        // "bat"
    PH_D,        // "dog"
    PH_G,        // "go"
    PH_V,        // "van"
    PH_Z,        // "zoo"
    PH_ZH,       // "measure" / "j" (FR)
    PH_JH,       // "judge"
    PH_L,        // "let"
    PH_M,        // "man"
    PH_N,        // "no"
    PH_NG,       // "sing"
    PH_R,        // "red" (EN flap) / "r" roulé (FR)
    PH_W,        // "wet"
    PH_Y,        // "yes"

    // ── Consonants (unvoiced) ──
    PH_P,        // "pat"
    PH_T,        // "top"
    PH_K,        // "key"
    PH_F,        // "fan"
    PH_S,        // "sun"
    PH_SH,       // "she"
    PH_TH,       // "thin"
    PH_CH,       // "chat"
    PH_H,        // "hat"

    // ── Meta ──
    PH_SIL,      // silence (pause)
    PH_WORD,     // word boundary (short silence)
    PH_COUNT
} OoPhoneme;

// ── Synth state ───────────────────────────────────────────────────────────────
typedef struct {
    // Sawtooth oscillator for voiced phonemes
    int32_t  osc_phase;         // current phase [0..OO_TTS_SAMPLE_RATE)
    int32_t  osc_pitch_hz;      // fundamental pitch (default: 120 Hz female, 95 Hz male)

    // Formant filter state (simple 2-pole IIR)
    int32_t  f1_x1, f1_x2;     // F1 filter memory
    int32_t  f2_x1, f2_x2;     // F2 filter memory

    // Noise state for unvoiced phonemes (XorShift32)
    uint32_t rng_state;

    // Prosody
    int      speech_rate;       // 1..200 (100=normal, 150=fast, 70=slow)
    int      volume;            // 0..100
    int      pitch_shift;       // semitones from base pitch (+/-12)

    // Language mode
    int      lang_fr;           // 1 = FR grapheme rules, 0 = EN
} OoTtsSynth;

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize synthesizer
void oo_tts_init(OoTtsSynth *s, int lang_fr);

// Initialize PCM buffer
void oo_tts_pcm_init(OoTtsPcmBuffer *buf);

// Convert text to speech, write PCM samples into buf
// Returns number of samples written, -1 on buffer overflow
int  oo_tts_speak(OoTtsSynth *s, OoTtsPcmBuffer *buf,
                  const char *text, int text_len);

// Speak a single phoneme, write samples into buf
// duration_ms: 0 = auto (phoneme default duration)
int  oo_tts_speak_phoneme(OoTtsSynth *s, OoTtsPcmBuffer *buf,
                           OoPhoneme ph, int duration_ms);

// Consume N samples from PCM buffer (called by HDA DMA interrupt)
// Returns actual samples consumed
int  oo_tts_pcm_consume(OoTtsPcmBuffer *buf, int16_t *out, int n);

// Check if synthesis is complete (buffer drained)
int  oo_tts_idle(const OoTtsPcmBuffer *buf);

// Set speech rate (50=very slow, 100=normal, 180=fast)
void oo_tts_set_rate(OoTtsSynth *s, int rate);

// Set voice pitch (semitones from base: -12..+12)
void oo_tts_set_pitch(OoTtsSynth *s, int semitones);

// Text normalization helpers (exposed for testing)
int  oo_tts_normalize(const char *in, int in_len, char *out, int out_cap);
int  oo_tts_g2p_en(const char *word, int word_len, OoPhoneme *out, int out_cap);
int  oo_tts_g2p_fr(const char *word, int word_len, OoPhoneme *out, int out_cap);

#ifdef __cplusplus
}
#endif
