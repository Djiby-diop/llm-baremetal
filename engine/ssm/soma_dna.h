// soma_dna.h — OO Digital DNA (identity + evolution parameters)
//
// Each OO instance has a unique DNA that defines its behavior.
// DNA mutates via meta-evolution loop and persists across reboots.
//
// Freestanding C11 — no libc.

#pragma once

#include "ssm_infer.h"  // ssm_f32, uint32_t

#ifdef __cplusplus
extern "C" {
#endif

#define SOMA_DNA_MAGIC   0x4F4F444Eu  // "OODN"
#define SOMA_DNA_VERSION 1u

// ============================================================
// Digital DNA Structure (128 bytes, stored on disk)
// ============================================================
typedef struct __attribute__((packed)) {
    // Identity
    uint32_t magic;              // SOMA_DNA_MAGIC
    uint32_t version;            // SOMA_DNA_VERSION
    uint32_t generation;         // Mutation counter (0 = first boot)
    uint32_t parent_hash;        // Hash of parent DNA (lineage)

    // Cognition parameters (tunable by meta-evolution)
    float cognition_bias;        // [0=pure logic, 1=pure creative] (Solar/Lunar balance)
    float confidence_threshold;  // When to escalate to external battery
    float temperature_solar;     // Sampling temp for Solar core (logic)
    float temperature_lunar;     // Sampling temp for Lunar core (creative)
    float top_p_solar;           // Top-p for Solar
    float top_p_lunar;           // Top-p for Lunar
    float reflex_threshold;      // Min pattern score to trigger reflex

    // Homeostasis
    float pressure_sensitivity;  // How reactive to memory strain [0.1-2.0]
    float learning_rate;         // Meta-evolution mutation magnitude
    float halt_threshold;        // HaltingHead threshold for adaptive compute

    // Specialization
    uint32_t domain_mask;        // Bitmask: which domains SomaMind handles
                                 // bit0=system, bit1=policy, bit2=chat,
                                 // bit3=code, bit4=math, bit5=creative

    // Stats (updated at runtime, persisted)
    uint32_t total_interactions;
    uint32_t successful_reflexes;
    uint32_t successful_internals;
    uint32_t escalations;
    float    avg_confidence;     // Running average of confidence scores

    // Reserved for future fields
    uint8_t  _reserved[24];
} SomaDNA;

// ============================================================
// API
// ============================================================

// Initialize DNA with default values (first boot)
void soma_dna_init_default(SomaDNA *dna);

// Validate DNA from loaded bytes (magic + version check)
int soma_dna_validate(const SomaDNA *dna);

// Mutate DNA (meta-evolution step)
// rng: XOR-shift state, magnitude: mutation step size [0.01-0.1]
void soma_dna_mutate(SomaDNA *dna, uint32_t *rng, float magnitude);

// Compute hash of DNA (for lineage tracking)
uint32_t soma_dna_hash(const SomaDNA *dna);

// Create child DNA (copy + mutate + set parent)
void soma_dna_reproduce(const SomaDNA *parent, SomaDNA *child,
                        uint32_t *rng, float magnitude);

#ifdef __cplusplus
}
#endif
