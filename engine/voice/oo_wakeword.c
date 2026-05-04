// oo_wakeword.c — Always-On Wake Word Detector (Implementation)
//
// Freestanding C11 — no libc, no malloc, no external deps.

#include "oo_wakeword.h"

// ── Abs/sqrt helpers (no libm) ────────────────────────────────────────────────

static int32_t _ww_abs(int32_t x) { return x < 0 ? -x : x; }

// Integer sqrt (Newton's method, sufficient for STE normalization)
static uint32_t _ww_isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n, x1 = (n+1)/2;
    while (x1 < x) { x = x1; x1 = (x + n/x) / 2; }
    return x;
}

// ── Short-time energy of a frame ──────────────────────────────────────────────

static uint16_t _ww_ste(const int16_t *samples, int n) {
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) {
        int32_t v = samples[i];
        sum += (uint64_t)(v * v);
    }
    sum /= (uint32_t)n;
    // Normalize to 0-65535: sqrt(mean_sq) / 32768 * 65535
    uint32_t rms = _ww_isqrt((uint32_t)(sum > 0xFFFFFFFF ? 0xFFFFFFFF : sum));
    return (uint16_t)(rms > 32767 ? 65535 : rms * 2);
}

// ── Zero-crossing rate ────────────────────────────────────────────────────────

static uint8_t _ww_zcr(const int16_t *samples, int n) {
    int zc = 0;
    for (int i = 1; i < n; i++) {
        if ((samples[i] >= 0) != (samples[i-1] >= 0)) zc++;
    }
    // Normalize to 0-255
    return (uint8_t)(zc * 255 / n);
}

// ── Built-in wake word energy profiles ───────────────────────────────────────
//
// Pre-measured approximate STE profiles for each wake word at 16kHz.
// Each entry = average STE across OWW_FRAME_SIZE samples (2ms frame).
// Profile = 16 frames = 32ms window.
//
// "OO" (short, two syllables, low-high-low energy shape):
static const uint16_t _ww_profile_oo[OWW_PATTERN_FRAMES] = {
    100, 800, 3200, 6400, 9800, 14000, 12000, 8000,
    10000, 15000, 12000, 7000, 3000, 1200, 400, 100
};
// "HEY OO" (three syllables: HEY strong, OO double):
static const uint16_t _ww_profile_hey_oo[OWW_PATTERN_FRAMES] = {
    300, 4000, 12000, 18000, 16000, 8000, 2000, 800,
    1500, 9000, 16000, 13000, 8000, 3500, 900, 200
};
// "SALUT OO" (French greeting + OO):
static const uint16_t _ww_profile_salut[OWW_PATTERN_FRAMES] = {
    200, 3000, 10000, 16000, 14000, 9000, 4000, 1500,
    500, 800, 9000, 14000, 11000, 6000, 2000, 400
};
// "ACTIVATE" (4 syllables, sustained energy):
static const uint16_t _ww_profile_activate[OWW_PATTERN_FRAMES] = {
    400, 5000, 14000, 18000, 16000, 14000, 12000, 10000,
    10000, 12000, 14000, 12000, 8000, 4000, 1500, 300
};
// "WAKE UP" (two words, gap between):
static const uint16_t _ww_profile_wakeup[OWW_PATTERN_FRAMES] = {
    300, 3500, 11000, 16000, 12000, 6000, 1500, 300,
    200, 1200, 8000, 13000, 10000, 5000, 1800, 400
};

// ── Init ─────────────────────────────────────────────────────────────────────

void oww_init(OwwEngine *e) {
    if (!e) return;
    // Zero ring buffer
    for (int i = 0; i < OWW_RING_SIZE; i++) e->ring[i] = 0;
    e->ring_write = 0;
    // Zero frames
    for (int i = 0; i < OWW_PATTERN_FRAMES * 2; i++) {
        e->frames[i].ste      = 0;
        e->frames[i].zcr      = 0;
        e->frames[i].is_voiced= 0;
    }
    e->frame_count    = 0;
    e->detected       = 0;
    e->cooldown       = 0;
    e->detected_word[0] = '\0';
    e->pattern_count  = 0;
    e->noise_floor    = 200;   // conservative default (quiet room)
    e->vad_active     = 0;
    e->frames_analyzed= 0;
    e->detections     = 0;
    e->false_positives= 0;

    // Register built-in patterns
    oww_add_pattern(e, "OO",       _ww_profile_oo,       480);
    oww_add_pattern(e, "HEY_OO",   _ww_profile_hey_oo,   450);
    oww_add_pattern(e, "SALUT_OO", _ww_profile_salut,    440);
    oww_add_pattern(e, "ACTIVATE", _ww_profile_activate, 420);
    oww_add_pattern(e, "WAKE_UP",  _ww_profile_wakeup,   430);
}

int oww_add_pattern(OwwEngine *e, const char *name,
                    const uint16_t *energy_profile, int threshold) {
    if (!e || !name || !energy_profile) return 0;
    if (e->pattern_count >= OWW_MAX_PATTERNS) return 0;

    OwwPattern *p = &e->patterns[e->pattern_count];
    // Copy name (freestanding)
    int ni = 0;
    while (ni < 31 && name[ni]) { p->name[ni] = name[ni]; ni++; }
    p->name[ni] = '\0';
    // Copy energy profile
    for (int i = 0; i < OWW_PATTERN_FRAMES; i++)
        p->energy_profile[i] = energy_profile[i];
    p->voiced_mask = 0xFF; // all frames considered
    p->min_voiced  = 4;
    p->threshold   = threshold;
    e->pattern_count++;
    return 1;
}

// ── Feed PCM samples ──────────────────────────────────────────────────────────

void oww_feed(OwwEngine *e, const int16_t *samples, int n_samples) {
    if (!e || !samples || n_samples <= 0) return;

    // Write samples into ring buffer
    for (int i = 0; i < n_samples; i++) {
        e->ring[e->ring_write++ % OWW_RING_SIZE] = samples[i];
    }

    // Process new frames
    int write_pos  = e->ring_write;
    int n_new_frames = n_samples / OWW_FRAME_SIZE;

    for (int f = 0; f < n_new_frames; f++) {
        // Extract frame from ring buffer
        int16_t frame_buf[OWW_FRAME_SIZE];
        int base = (write_pos - n_samples + f * OWW_FRAME_SIZE + OWW_RING_SIZE * 2)
                   % OWW_RING_SIZE;
        for (int s = 0; s < OWW_FRAME_SIZE; s++)
            frame_buf[s] = e->ring[(base + s) % OWW_RING_SIZE];

        // Analyze frame
        int fidx = e->frame_count % (OWW_PATTERN_FRAMES * 2);
        e->frames[fidx].ste      = _ww_ste(frame_buf, OWW_FRAME_SIZE);
        e->frames[fidx].zcr      = _ww_zcr(frame_buf, OWW_FRAME_SIZE);
        e->frames[fidx].is_voiced= e->frames[fidx].ste > (uint16_t)(e->noise_floor * 3);
        e->frame_count++;
        e->frames_analyzed++;

        // Update VAD
        e->vad_active = e->frames[fidx].is_voiced;

        // Cooldown countdown
        if (e->cooldown > 0) { e->cooldown--; continue; }

        // Only attempt matching if we have enough voiced frames
        if (e->frame_count < OWW_PATTERN_FRAMES) continue;

        // Try to match each wake pattern
        if (!e->vad_active) continue; // skip if silent

        for (int pi = 0; pi < e->pattern_count; pi++) {
            OwwPattern *pat = &e->patterns[pi];
            int voiced_count = 0;
            int64_t dist_sum = 0;

            // Calculate distance between current frame window and pattern
            for (int k = 0; k < OWW_PATTERN_FRAMES; k++) {
                int idx = (e->frame_count - OWW_PATTERN_FRAMES + k +
                           OWW_PATTERN_FRAMES * 4) % (OWW_PATTERN_FRAMES * 2);
                int32_t diff = (int32_t)e->frames[idx].ste - (int32_t)pat->energy_profile[k];
                dist_sum += _ww_abs(diff);
                if (e->frames[idx].is_voiced) voiced_count++;
            }

            // Normalize distance to similarity score (0-1000)
            // Perfect match = 0 distance, we want score = 1000
            int32_t avg_dist = (int32_t)(dist_sum / OWW_PATTERN_FRAMES);
            int score = 1000 - (int)(avg_dist * 1000 / 32768);
            if (score < 0) score = 0;

            if (voiced_count >= pat->min_voiced && score >= pat->threshold) {
                // Wake word detected!
                e->detected = 1;
                e->cooldown = OWW_COOLDOWN_FRAMES;
                e->detections++;
                // Copy name
                int ni = 0;
                while (ni < 31 && pat->name[ni])
                    { e->detected_word[ni] = pat->name[ni]; ni++; }
                e->detected_word[ni] = '\0';
                break;
            }
        }
    }
}

// ── Poll ─────────────────────────────────────────────────────────────────────

int oww_poll(OwwEngine *e) {
    if (!e || !e->detected) return 0;
    e->detected = 0;
    return 1;
}

const char *oww_last_word(const OwwEngine *e) {
    return e ? e->detected_word : (const char*)0;
}

// ── Calibration ───────────────────────────────────────────────────────────────

void oww_calibrate(OwwEngine *e, const int16_t *silence, int n_samples) {
    if (!e || !silence || n_samples <= 0) return;
    uint32_t sum = 0;
    int frames = n_samples / OWW_FRAME_SIZE;
    for (int f = 0; f < frames; f++) {
        uint16_t ste = _ww_ste(silence + f * OWW_FRAME_SIZE, OWW_FRAME_SIZE);
        sum += ste;
    }
    if (frames > 0) {
        e->noise_floor = (uint16_t)(sum / (uint32_t)frames);
        // Add 20% margin
        e->noise_floor = (uint16_t)(e->noise_floor + e->noise_floor / 5 + 50);
    }
}

void oww_reset(OwwEngine *e) {
    if (!e) return;
    e->detected  = 0;
    e->cooldown  = 0;
    e->vad_active= 0;
}
