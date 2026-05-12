// oo_tts_phoneme.c — Bare-Metal TTS Phoneme Synthesizer (Implementation)
//
// Synthesizes intelligible speech directly into a PCM ring buffer.
// Designed for UEFI Ring 0 — zero libc, zero malloc.
//
// Voice character: "SOMA" — slightly robotic, clear, androgynous.
//   Base pitch: 130 Hz (mid-range)
//   Formant synthesis: 2-pole IIR filters emulating vocal tract resonances
//   Unvoiced: XorShift noise shaped by phoneme-specific envelope

#include "oo_tts_phoneme.h"

// ── Constants ─────────────────────────────────────────────────────────────────

#define SR      OO_TTS_SAMPLE_RATE   // 16000
#define VOL_MAX 32767

// ── Fixed-point math helpers ──────────────────────────────────────────────────

// sin(x) approximation via 5th-order polynomial (Bhaskara-like)
// x in [0, 65535] representing [0, 2π)
// Returns [-32767, 32767]
static int32_t _fp_sin(int32_t x) {
    // Normalize x to [0, 65535]
    x &= 0xFFFF;
    // Bhaskara I approximation: sin(πt) ≈ 16t(π-t)/(5π²-4t(π-t)) mapped to fixed
    // Simplified LUT-free: map to [0..32768] quarter, mirror
    int32_t sign = 1;
    if (x > 32767) { x = 65535 - x; sign = -1; }
    if (x > 16383) { x = 32767 - x; }
    int32_t s = (int32_t)(x * (32767 - x) / 4096);
    return sign * (s > 32767 ? 32767 : s);
}

// XorShift32 fast PRNG
static uint32_t _xsr(uint32_t s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

// ── Phoneme property table ────────────────────────────────────────────────────

typedef struct {
    uint8_t  voiced;      // 1 = voiced (oscillator), 0 = unvoiced (noise)
    uint16_t f1_hz;       // first formant frequency Hz
    uint16_t f2_hz;       // second formant frequency Hz
    uint8_t  duration_ms; // default duration in ms (at normal rate)
    int8_t   pitch_mod;   // pitch modifier from base (% × 100 → semitones/10)
} PhProps;

// Table: OoPhoneme index → properties
static const PhProps _ph_props[PH_COUNT] = {
    // Vowels
    [PH_AH]  = {1,  800, 1200, 80,  0},
    [PH_AE]  = {1,  800, 1800, 80,  0},
    [PH_EH]  = {1,  600, 1700, 80,  0},
    [PH_EE]  = {1,  300, 2300, 90,  2},
    [PH_IH]  = {1,  400, 2000, 70,  1},
    [PH_OH]  = {1,  500,  900, 85,  0},
    [PH_OO]  = {1,  300,  800, 90, -1},
    [PH_UH]  = {1,  450, 1100, 70,  0},
    [PH_UR]  = {1,  500, 1300, 80,  0},
    [PH_AA]  = {1,  800, 1200, 90,  0},
    [PH_OE]  = {1,  400, 1500, 80,  0},
    [PH_UU]  = {1,  250,  700, 80, -2},
    [PH_EU]  = {1,  450,  900, 75,  0},
    [PH_AN]  = {1,  700,  900, 85,  0},
    [PH_IN]  = {1,  400, 1800, 80,  1},
    [PH_UN]  = {1,  400,  800, 80,  0},
    [PH_ON]  = {1,  500,  700, 85, -1},
    // Voiced consonants
    [PH_B]   = {1,  200,  800, 50, -3},
    [PH_D]   = {1,  200, 1500, 40, -2},
    [PH_G]   = {1,  200, 1000, 40, -2},
    [PH_V]   = {1,  300,  900, 55, -1},
    [PH_Z]   = {1,  300, 1500, 50,  0},
    [PH_ZH]  = {1,  300, 1800, 55,  0},
    [PH_JH]  = {1,  200, 2000, 55,  0},
    [PH_L]   = {1,  300,  900, 50,  0},
    [PH_M]   = {1,  200,  500, 60, -2},
    [PH_N]   = {1,  200, 1700, 55, -1},
    [PH_NG]  = {1,  200,  700, 60, -2},
    [PH_R]   = {1,  400, 1400, 50,  0},
    [PH_W]   = {1,  300,  700, 45,  0},
    [PH_Y]   = {1,  250, 2200, 40,  2},
    // Unvoiced consonants
    [PH_P]   = {0,    0,    0, 50, 0},
    [PH_T]   = {0,    0,    0, 40, 0},
    [PH_K]   = {0,    0,    0, 45, 0},
    [PH_F]   = {0, 3000, 8000, 55, 0},
    [PH_S]   = {0, 4000,10000, 55, 0},
    [PH_SH]  = {0, 2500, 7000, 60, 0},
    [PH_TH]  = {0, 5000,12000, 50, 0},
    [PH_CH]  = {0, 2000, 8000, 60, 0},
    [PH_H]   = {0,  500, 1500, 45, 0},
    // Meta
    [PH_SIL]  = {0, 0, 0, 100, 0},  // silence 100ms
    [PH_WORD] = {0, 0, 0,  30, 0},  // word gap 30ms
};

// ── 2-pole IIR filter coefficient set ────────────────────────────────────────

typedef struct { int32_t a1, a2, b0; } IirCoef;

// Very rough resonant filter (formant simulation)
// freq_hz: resonance frequency, sr: sample rate
// Returns fixed-point coefficients (Q15)
static IirCoef _formant_coef(int freq_hz) {
    // Simplified: biquad peak filter approximation
    // cos(2π f/sr) precomputed for common formants via table
    // We use a linear approximation: a1 = 2·cos(2π·f/sr) ≈ 2 - (2π·f/sr)²
    // All × 16384 (Q14)
    int32_t f = freq_hz;
    int32_t omega = (f * 512) / SR;              // omega/2π × 16384 scaled
    int32_t c = 16384 - (omega * omega) / 256;   // ≈ cos(omega) × 16384
    IirCoef ic;
    ic.a1 = (2 * c) >> 4;      // resonance ≈ 0.95
    ic.a2 = -28672;            // Q15: -0.875 (decay)
    ic.b0 = 4096;              // gain
    return ic;
}

// Apply IIR: out = b0·in + a1·x1 + a2·x2; x2=x1; x1=out
static int32_t _iir(IirCoef c, int32_t in, int32_t *x1, int32_t *x2) {
    int32_t y = (c.b0 * in + c.a1 * (*x1) + c.a2 * (*x2)) >> 14;
    *x2 = *x1;
    *x1 = y;
    if (y >  32767) y =  32767;
    if (y < -32767) y = -32767;
    return y;
}

// ── PCM buffer operations ─────────────────────────────────────────────────────

static int _pcm_write(OoTtsPcmBuffer *b, int16_t s) {
    if (b->count >= OO_TTS_PCM_CAP) return -1;
    b->samples[b->head] = s;
    b->head = (b->head + 1) % OO_TTS_PCM_CAP;
    b->count++;
    return 0;
}

// ── Init ──────────────────────────────────────────────────────────────────────

void oo_tts_init(OoTtsSynth *s, int lang_fr) {
    s->osc_phase   = 0;
    s->osc_pitch_hz = 130;   // SOMA base pitch (androgynous)
    s->f1_x1 = s->f1_x2 = 0;
    s->f2_x1 = s->f2_x2 = 0;
    s->rng_state   = 0xDEADB007;
    s->speech_rate = 100;
    s->volume      = 85;
    s->pitch_shift = 0;
    s->lang_fr     = lang_fr;
}

void oo_tts_pcm_init(OoTtsPcmBuffer *buf) {
    for (int i = 0; i < OO_TTS_PCM_CAP; i++) buf->samples[i] = 0;
    buf->head = buf->tail = buf->count = 0;
}

// ── Speak a single phoneme ────────────────────────────────────────────────────

int oo_tts_speak_phoneme(OoTtsSynth *s, OoTtsPcmBuffer *buf,
                          OoPhoneme ph, int duration_ms) {
    if ((int)ph >= (int)PH_COUNT) return 0;
    const PhProps *p = &_ph_props[ph];

    int dur_ms = (duration_ms > 0) ? duration_ms : p->duration_ms;
    int _rate  = (s->speech_rate > 0) ? s->speech_rate : 100;  /* guard div/0 */
    dur_ms = (dur_ms * 100) / _rate;  // adjust for rate
    int n_samples = (SR * dur_ms) / 1000;

    // Silence/word boundary
    if (ph == PH_SIL || ph == PH_WORD || !p->voiced && p->f1_hz == 0) {
        for (int i = 0; i < n_samples; i++) {
            if (_pcm_write(buf, 0) < 0) return i;
        }
        return n_samples;
    }

    // Pitch from base + phoneme modifier
    int pitch = s->osc_pitch_hz;
    if (p->pitch_mod != 0) pitch = pitch + (pitch * p->pitch_mod) / 100;

    // Formant filters
    IirCoef c1 = _formant_coef(p->f1_hz > 0 ? p->f1_hz : 1000);
    IirCoef c2 = _formant_coef(p->f2_hz > 0 ? p->f2_hz : 2000);

    // Amplitude envelope: linear attack (10%) + sustain (80%) + decay (10%)
    int attack  = n_samples / 10;
    int decay_s = n_samples - n_samples / 10;
    int vol     = (s->volume * VOL_MAX) / 100;

    for (int i = 0; i < n_samples; i++) {
        // Envelope
        int32_t amp;
        if (i < attack) {
            amp = (int32_t)vol * i / (attack > 0 ? attack : 1);
        } else if (i > decay_s) {
            amp = (int32_t)vol * (n_samples - i) / (n_samples - decay_s + 1);
        } else {
            amp = vol;
        }

        int32_t samp = 0;

        if (p->voiced) {
            // Sawtooth oscillator
            int32_t phase_inc = (pitch * 65536) / SR;
            s->osc_phase = (s->osc_phase + phase_inc) & 0xFFFF;
            // Sawtooth: phase / 65536 * 2 - 1, scaled to 16-bit
            int32_t saw = (s->osc_phase >> 1) - 16384;

            // Apply formant filters
            int32_t f1_out = _iir(c1, saw, &s->f1_x1, &s->f1_x2);
            int32_t f2_out = _iir(c2, saw, &s->f2_x1, &s->f2_x2);

            samp = (f1_out * 6 + f2_out * 4) / 10;
        } else {
            // Noise burst shaped by formant filter
            s->rng_state = _xsr(s->rng_state);
            int32_t noise = (int32_t)(s->rng_state & 0xFFFF) - 32768;

            int32_t f1_out = _iir(c1, noise, &s->f1_x1, &s->f1_x2);
            int32_t f2_out = _iir(c2, noise, &s->f2_x1, &s->f2_x2);

            samp = (f1_out * 5 + f2_out * 5) / 10;
        }

        // Apply amplitude
        samp = (samp * amp) / VOL_MAX;
        if (samp >  32767) samp =  32767;
        if (samp < -32767) samp = -32767;

        if (_pcm_write(buf, (int16_t)samp) < 0) return i;
    }

    return n_samples;
}

// ── Grapheme-to-Phoneme: English ──────────────────────────────────────────────

// Simple letter → phoneme mapping for common words
// Returns phoneme count
int oo_tts_g2p_en(const char *w, int wlen, OoPhoneme *out, int cap) {
    int n = 0;
#define PH(p) if(n < cap) out[n++] = (p)

    for (int i = 0; i < wlen && n < cap - 2; ) {
        char c = w[i];
        char c2 = (i+1 < wlen) ? w[i+1] : 0;
        char c3 = (i+2 < wlen) ? w[i+2] : 0;

        switch (c) {
        case 'a':
            if (c2=='e') { PH(PH_EE); i+=2; }
            else if (c2=='i'||c2=='y') { PH(PH_EE); i+=2; }
            else { PH(PH_AE); i++; }
            break;
        case 'e':
            if (c2=='e'||c2=='a') { PH(PH_EE); i+=2; }
            else if (i==wlen-1) { /* silent e */ i++; }
            else { PH(PH_EH); i++; }
            break;
        case 'i':
            if (c2=='e') { PH(PH_EE); i+=2; }
            else { PH(PH_IH); i++; }
            break;
        case 'o':
            if (c2=='o') { PH(PH_OO); i+=2; }
            else if (c2=='u') { PH(PH_OO); i+=2; }
            else { PH(PH_OH); i++; }
            break;
        case 'u':
            if (c2=='e') { PH(PH_OO); i+=2; }
            else { PH(PH_UH); i++; }
            break;
        case 'b': PH(PH_B); i++; break;
        case 'c':
            if (c2=='h') { PH(PH_CH); i+=2; }
            else if (c2=='e'||c2=='i'||c2=='y') { PH(PH_S); i++; }
            else { PH(PH_K); i++; }
            break;
        case 'd': PH(PH_D); i++; break;
        case 'f': PH(PH_F); i++; break;
        case 'g':
            if (c2=='h') { /* silent or f */ i+=2; }
            else if (c2=='e'||c2=='i') { PH(PH_JH); i++; }
            else { PH(PH_G); i++; }
            break;
        case 'h': PH(PH_H); i++; break;
        case 'j': PH(PH_JH); i++; break;
        case 'k':
            if (c2=='n') { i+=2; }  // silent k
            else { PH(PH_K); i++; }
            break;
        case 'l': PH(PH_L); i++; break;
        case 'm': PH(PH_M); i++; break;
        case 'n':
            if (c2=='g') { PH(PH_NG); i+=2; }
            else { PH(PH_N); i++; }
            break;
        case 'p':
            if (c2=='h') { PH(PH_F); i+=2; }
            else { PH(PH_P); i++; }
            break;
        case 'q': PH(PH_K); i++; (void)c2; break;
        case 'r': PH(PH_R); i++; break;
        case 's':
            if (c2=='h') { PH(PH_SH); i+=2; }
            else { PH(PH_S); i++; }
            break;
        case 't':
            if (c2=='h') { PH(PH_TH); i+=2; }
            else { PH(PH_T); i++; }
            break;
        case 'v': PH(PH_V); i++; break;
        case 'w': PH(PH_W); i++; break;
        case 'x': PH(PH_K); PH(PH_S); i++; break;
        case 'y': PH(PH_Y); i++; break;
        case 'z': PH(PH_Z); i++; break;
        default: i++; break;
        }
        (void)c3;
    }
#undef PH
    return n;
}

// ── Grapheme-to-Phoneme: French ───────────────────────────────────────────────

int oo_tts_g2p_fr(const char *w, int wlen, OoPhoneme *out, int cap) {
    int n = 0;
#define PH(p) if(n < cap) out[n++] = (p)

    for (int i = 0; i < wlen && n < cap - 2; ) {
        unsigned char c = (unsigned char)w[i];
        char c2 = (i+1 < wlen) ? w[i+1] : 0;

        // UTF-8 accented chars (basic subset)
        if (c == 0xC3 && i+1 < wlen) {
            unsigned char c_next = (unsigned char)w[i+1];
            if (c_next == 0xA9 || c_next == 0xA8 || c_next == 0xAA) { // é è ê
                PH(PH_EH); i+=2; continue;
            }
            if (c_next == 0xA0 || c_next == 0xA2) { // à â
                PH(PH_AA); i+=2; continue;
            }
            if (c_next == 0xB9) { // ù
                PH(PH_UU); i+=2; continue;
            }
            if (c_next == 0xB4) { // ô
                PH(PH_OH); i+=2; continue;
            }
        }

        switch ((char)c) {
        case 'a':
            if (c2=='n'||c2=='m') { PH(PH_AN); i+=2; }
            else { PH(PH_AA); i++; }
            break;
        case 'e':
            if (c2=='u') { PH(PH_EU); i+=2; }
            else if (c2=='n'||c2=='m') { PH(PH_AN); i+=2; }
            else if (c2=='r') { PH(PH_EH); i++; }
            else if (i == wlen-1) { /* silent */ i++; }
            else { PH(PH_EH); i++; }
            break;
        case 'i':
            if (c2=='n'||c2=='m') { PH(PH_IN); i+=2; }
            else { PH(PH_EE); i++; }
            break;
        case 'o':
            if (c2=='n'||c2=='m') { PH(PH_ON); i+=2; }
            else if (c2=='u') { PH(PH_OO); i+=2; }
            else { PH(PH_OH); i++; }
            break;
        case 'u':
            if (c2=='n'||c2=='m') { PH(PH_UN); i+=2; }
            else { PH(PH_UU); i++; }
            break;
        case 'b': PH(PH_B); i++; break;
        case 'c':
            if (c2=='h') { PH(PH_SH); i+=2; }  // FR "ch" → /ʃ/
            else if (c2=='e'||c2=='i'||c2=='y') { PH(PH_S); i++; }
            else { PH(PH_K); i++; }
            break;
        case 'd': PH(PH_D); i++; break;
        case 'f': PH(PH_F); i++; break;
        case 'g':
            if (c2=='n') { PH(PH_N); PH(PH_Y); i+=2; }  // gn → /ɲ/
            else if (c2=='e'||c2=='i') { PH(PH_ZH); i++; }
            else { PH(PH_G); i++; }
            break;
        case 'h': /* silent */ i++; break;
        case 'j': PH(PH_ZH); i++; break;
        case 'k': PH(PH_K); i++; break;
        case 'l': PH(PH_L); i++; break;
        case 'm': PH(PH_M); i++; break;
        case 'n': PH(PH_N); i++; break;
        case 'p':
            if (c2=='h') { PH(PH_F); i+=2; }
            else { PH(PH_P); i++; }
            break;
        case 'q': PH(PH_K); i++; break;
        case 'r': PH(PH_R); i++; break;
        case 's':
            if (c2=='s') { PH(PH_S); i+=2; }
            else { PH(PH_Z); i++; }  // liaisons
            break;
        case 't': PH(PH_T); i++; break;
        case 'v': PH(PH_V); i++; break;
        case 'w': PH(PH_V); i++; break;  // FR "w" → /v/
        case 'x': PH(PH_K); PH(PH_S); i++; break;
        case 'y': PH(PH_EE); i++; break;
        case 'z': PH(PH_Z); i++; break;
        default: i++; break;
        }
    }
#undef PH
    return n;
}

// ── Text normalization ────────────────────────────────────────────────────────

int oo_tts_normalize(const char *in, int in_len, char *out, int cap) {
    int o = 0;
    for (int i = 0; i < in_len && o < cap - 1; i++) {
        char c = in[i];
        // Uppercase → lowercase
        if (c >= 'A' && c <= 'Z') c = c + 32;
        // Punctuation → silence marker '$'
        if (c == '.' || c == '!' || c == '?' || c == ';') {
            out[o++] = '$'; continue;
        }
        // Comma/colon → short pause '|'
        if (c == ',' || c == ':') {
            out[o++] = '|'; continue;
        }
        // Keep alphanumeric, spaces, accents
        if ((c >= 'a' && c <= 'z') || c == ' ' || (unsigned char)c >= 0xC0) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return o;
}

// ── Full text-to-speech ───────────────────────────────────────────────────────

int oo_tts_speak(OoTtsSynth *s, OoTtsPcmBuffer *buf,
                 const char *text, int text_len) {
    char norm[512];
    int nlen = oo_tts_normalize(text, text_len, norm, 512);

    int total = 0;
    int word_start = -1;

    for (int i = 0; i <= nlen; i++) {
        char c = (i < nlen) ? norm[i] : ' ';  // force word-end at end

        if (c == ' ' || c == '$' || c == '|' || i == nlen) {
            // End of word — emit phonemes
            if (word_start >= 0) {
                OoPhoneme phs[64];
                int pn;
                int wlen_local = i - word_start;
                if (s->lang_fr) {
                    pn = oo_tts_g2p_fr(norm + word_start, wlen_local, phs, 64);
                } else {
                    pn = oo_tts_g2p_en(norm + word_start, wlen_local, phs, 64);
                }
                for (int p = 0; p < pn; p++) {
                    total += oo_tts_speak_phoneme(s, buf, phs[p], 0);
                }
                word_start = -1;
            }
            // Emit pause
            if (c == '$') total += oo_tts_speak_phoneme(s, buf, PH_SIL, 200);
            else if (c == '|') total += oo_tts_speak_phoneme(s, buf, PH_SIL, 80);
            else if (c == ' ') total += oo_tts_speak_phoneme(s, buf, PH_WORD, 0);
        } else {
            if (word_start < 0) word_start = i;
        }
    }

    return total;
}

// ── PCM buffer consume (called by HDA DMA) ────────────────────────────────────

int oo_tts_pcm_consume(OoTtsPcmBuffer *buf, int16_t *out, int n) {
    int consumed = 0;
    while (consumed < n && buf->count > 0) {
        out[consumed++] = buf->samples[buf->tail];
        buf->tail = (buf->tail + 1) % OO_TTS_PCM_CAP;
        buf->count--;
    }
    return consumed;
}

int oo_tts_idle(const OoTtsPcmBuffer *buf) {
    return buf->count == 0;
}

void oo_tts_set_rate(OoTtsSynth *s, int rate) {
    if (rate < 50) rate = 50;
    if (rate > 200) rate = 200;
    s->speech_rate = rate;
}

void oo_tts_set_pitch(OoTtsSynth *s, int semitones) {
    if (semitones < -12) semitones = -12;
    if (semitones > 12)  semitones = 12;
    s->pitch_shift = semitones;
    // 1 semitone ≈ 6% pitch change
    int base = 130;
    for (int i = 0; i < semitones; i++) base = (base * 106) / 100;
    for (int i = 0; i > semitones; i--) base = (base * 100) / 106;
    s->osc_pitch_hz = base;
}
