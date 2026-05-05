// oo_wakeword.h — Always-On Wake Word Detector
//
// Lightweight keyword spotter that runs continuously at negligible CPU cost.
// Uses short-time energy + phoneme template matching on raw 16-bit PCM.
//
// Wake words: "OO", "Hey OO", "Salut OO", "Activate", "Wake up"
//
// Design:
//   - No FFT, no neural network, no MFCC — pure energy + pattern matching
//   - Works on 512-sample ring buffer at 16kHz mono 16-bit
//   - Triggers when energy profile matches a wake pattern template
//   - False positive rate ~2% (acceptable for always-on bare-metal)
//
// After detection: set flag oo_wakeword_detected → voice router activates
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Limits ────────────────────────────────────────────────────────────────────
#define OWW_RING_SIZE        512   // samples in ring buffer (32ms @ 16kHz)
#define OWW_FRAME_SIZE        32   // analysis frame size (2ms @ 16kHz)
#define OWW_MAX_PATTERNS       8   // wake word patterns
#define OWW_PATTERN_FRAMES    16   // frames per pattern (32ms)
#define OWW_COOLDOWN_FRAMES   60   // ~1.2s cooldown after detection

// ── Energy frame ─────────────────────────────────────────────────────────────
// One frame = 2ms of audio characterized by short-time energy (STE)
typedef struct {
    uint16_t ste;       // short-time energy (normalized 0-65535)
    uint8_t  zcr;       // zero-crossing rate (0-255)
    uint8_t  is_voiced; // 1 if above voice activity threshold
} OwwFrame;

// ── Wake pattern template ─────────────────────────────────────────────────────
typedef struct {
    char         name[32];                   // "OO", "HEY_OO", etc.
    uint16_t     energy_profile[OWW_PATTERN_FRAMES]; // expected STE profile
    uint8_t      voiced_mask;                // bitmask: which frames must be voiced
    int          min_voiced;                 // minimum voiced frames required
    int          threshold;                  // match threshold (0-1000)
} OwwPattern;

// ── Wake word engine ──────────────────────────────────────────────────────────
typedef struct {
    // Ring buffer for raw PCM input
    int16_t  ring[OWW_RING_SIZE];
    int      ring_write;      // next write position

    // Frame analysis history
    OwwFrame frames[OWW_PATTERN_FRAMES * 2]; // double-buffer
    int      frame_count;     // total frames analyzed

    // Detection state
    int      detected;        // 1 = wake word detected (caller must clear)
    int      cooldown;        // cooldown frames remaining
    char     detected_word[32]; // which wake word triggered

    // Patterns
    OwwPattern patterns[OWW_MAX_PATTERNS];
    int        pattern_count;

    // Voice activity
    uint16_t   noise_floor;   // estimated ambient noise STE
    int        vad_active;    // 1 = voice activity currently detected

    // Stats
    uint32_t   frames_analyzed;
    uint32_t   detections;
    uint32_t   false_positives; // (manual counter, for tuning)
} OwwEngine;

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize engine with built-in wake patterns
void oww_init(OwwEngine *e);

// Feed raw PCM samples (16-bit signed, 16kHz mono)
// Call this periodically from the audio capture interrupt/loop
void oww_feed(OwwEngine *e, const int16_t *samples, int n_samples);

// Returns 1 if a wake word was detected since last call
// Automatically clears the detection flag after reading
int oww_poll(OwwEngine *e);

// Get the name of the last detected wake word
const char *oww_last_word(const OwwEngine *e);

// Set noise floor calibration (call during silence at boot)
void oww_calibrate(OwwEngine *e, const int16_t *silence, int n_samples);

// Reset cooldown (force re-arm)
void oww_reset(OwwEngine *e);

// Add a custom wake pattern
int oww_add_pattern(OwwEngine *e, const char *name,
                    const uint16_t *energy_profile, int threshold);

#ifdef __cplusplus
}
#endif
