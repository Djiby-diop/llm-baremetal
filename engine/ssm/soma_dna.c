// soma_dna.c — OO Digital DNA implementation
//
// Freestanding C11 — no libc.

#include "soma_dna.h"
#include "oo_quantum_rng.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Freestanding XOR-shift RNG ────────────────────────────────────────────
static uint32_t _xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// Random float in [-1, 1] from xorshift state
static float _rand_float(uint32_t *rng) {
    uint32_t r = _xorshift32(rng);
    return ((float)(r & 0xFFFF) / 32768.0f) - 1.0f;  // [-1, 1]
}

// Clamp float to [lo, hi]
static float _clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// ============================================================
// soma_genomion_init_default
// ============================================================
void soma_genomion_init_default(Genomion *dna) {
    if (!dna) return;

    // Zero everything first
    unsigned char *p = (unsigned char *)dna;
    for (int i = 0; i < (int)sizeof(Genomion); i++) p[i] = 0;

    dna->magic   = SOMA_GENOMION_MAGIC;
    dna->version = SOMA_GENOMION_VERSION;
    dna->generation = 0;
    dna->parent_hash = 0;

    // Balanced cognition (50/50 Solar/Lunar)
    dna->cognition_bias       = 0.5f;
    dna->confidence_threshold = 0.85f;
    dna->temperature_solar    = 0.3f;   // Low temp for logic
    dna->temperature_lunar    = 1.2f;   // High temp for creativity
    dna->top_p_solar          = 0.5f;   // Tight nucleus for logic
    dna->top_p_lunar          = 0.95f;  // Wide nucleus for creativity
    dna->reflex_threshold     = 0.7f;

    // Homeostasis
    dna->pressure_sensitivity = 1.0f;   // Normal reactivity
    dna->learning_rate        = 0.05f;  // 5% mutation steps
    dna->halt_threshold       = 0.5f;   // HaltingHead default

    // Handle all domains by default
    dna->domain_mask = 0x3F;  // bits 0-5: all domains

    // Stats start at zero
    dna->total_interactions   = 0;
    dna->successful_reflexes  = 0;
    dna->successful_internals = 0;
    dna->escalations          = 0;
    dna->avg_confidence       = 0.0f;

    // Phase 7: Biometric Identity & Personality
    dna->biometric_seed = oo_quantum_seed();
    uint32_t seed_copy = dna->biometric_seed;
    
    // Derived Personality from hardware entropy
    dna->cognition_bias    = _clampf(0.5f + _rand_float(&seed_copy) * 0.2f, 0.0f, 1.0f);
    dna->curiosity_level   = _clampf(1.0f + _rand_float(&seed_copy) * 0.5f, 0.5f, 2.0f);
    dna->risk_tolerance    = _clampf(1.0f + _rand_float(&seed_copy) * 0.5f, 0.5f, 2.0f);
    dna->intuition_power   = _clampf(0.5f + _rand_float(&seed_copy) * 0.3f, 0.1f, 0.9f);
}

// ============================================================
// soma_genomion_validate
// ============================================================
int soma_genomion_validate(const Genomion *dna) {
    if (!dna) return 0;
    if (dna->magic != SOMA_GENOMION_MAGIC) return 0;
    if (dna->version != SOMA_GENOMION_VERSION) return 0;
    if (dna->confidence_threshold < 0.0f || dna->confidence_threshold > 1.0f) return 0;
    if (dna->cognition_bias < 0.0f || dna->cognition_bias > 1.0f) return 0;
    return 1;
}

// ============================================================
// soma_genomion_hash (FNV-1a 32-bit over Genomion bytes)
// ============================================================
uint32_t soma_genomion_hash(const Genomion *dna) {
    if (!dna) return 0;
    const unsigned char *bytes = (const unsigned char *)dna;
    uint32_t hash = 0x811C9DC5u;  // FNV offset basis
    // Skip first 8 bytes (magic + version) for content hash
    for (int i = 8; i < (int)sizeof(Genomion); i++) {
        hash ^= bytes[i];
        hash *= 0x01000193u;  // FNV prime
    }
    return hash;
}

// ============================================================
// soma_genomion_mutate
// ============================================================
void soma_genomion_mutate(Genomion *dna, uint32_t *rng, float magnitude) {
    if (!dna || !rng) return;
    if (magnitude < 0.001f) magnitude = 0.001f;
    if (magnitude > 0.5f)   magnitude = 0.5f;

    dna->generation++;

    // Mutate cognition parameters with bounded perturbation
    dna->cognition_bias = _clampf(
        dna->cognition_bias + _rand_float(rng) * magnitude, 0.0f, 1.0f);

    dna->confidence_threshold = _clampf(
        dna->confidence_threshold + _rand_float(rng) * magnitude * 0.5f, 0.3f, 0.99f);

    dna->temperature_solar = _clampf(
        dna->temperature_solar + _rand_float(rng) * magnitude * 0.3f, 0.05f, 1.0f);

    dna->temperature_lunar = _clampf(
        dna->temperature_lunar + _rand_float(rng) * magnitude * 0.3f, 0.5f, 2.0f);

    dna->top_p_solar = _clampf(
        dna->top_p_solar + _rand_float(rng) * magnitude * 0.2f, 0.1f, 0.9f);

    dna->top_p_lunar = _clampf(
        dna->top_p_lunar + _rand_float(rng) * magnitude * 0.2f, 0.5f, 1.0f);

    dna->pressure_sensitivity = _clampf(
        dna->pressure_sensitivity + _rand_float(rng) * magnitude * 0.3f, 0.1f, 2.0f);

    dna->halt_threshold = _clampf(
        dna->halt_threshold + _rand_float(rng) * magnitude * 0.2f, 0.1f, 0.9f);

    // Personality Mutation
    dna->curiosity_level = _clampf(
        dna->curiosity_level + _rand_float(rng) * magnitude * 0.4f, 0.5f, 2.0f);
    
    dna->risk_tolerance = _clampf(
        dna->risk_tolerance + _rand_float(rng) * magnitude * 0.4f, 0.5f, 2.0f);
        
    dna->intuition_power = _clampf(
        dna->intuition_power + _rand_float(rng) * magnitude * 0.3f, 0.0f, 1.0f);

    // Reflex threshold: careful, don't go too low (false positives)
    dna->reflex_threshold = _clampf(
        dna->reflex_threshold + _rand_float(rng) * magnitude * 0.15f, 0.4f, 0.95f);
}

// ============================================================
// soma_genomion_reproduce
// ============================================================
void soma_genomion_reproduce(const Genomion *parent, Genomion *child,
                        uint32_t *rng, float magnitude) {
    if (!parent || !child || !rng) return;

    // Copy parent Genomion
    const unsigned char *src = (const unsigned char *)parent;
    unsigned char *dst = (unsigned char *)child;
    for (int i = 0; i < (int)sizeof(Genomion); i++) dst[i] = src[i];

    // Set lineage
    child->parent_hash = soma_genomion_hash(parent);

    // Reset child stats
    child->total_interactions   = 0;
    child->successful_reflexes  = 0;
    child->successful_internals = 0;
    child->escalations          = 0;
    child->avg_confidence       = 0.0f;

    // Mutate child
    soma_genomion_mutate(child, rng, magnitude);
}
