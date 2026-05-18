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

#define SOMA_GENOMION_MAGIC   0x4F4F444Eu  // "OODN"
#define SOMA_GENOMION_VERSION 1u

// ============================================================
// Digital Genomion Structure (128 bytes, stored on disk)
// ============================================================
typedef struct __attribute__((packed)) {
    // Identity
    uint32_t magic;              // SOMA_GENOMION_MAGIC
    uint32_t version;            // SOMA_GENOMION_VERSION
    uint32_t generation;         // Mutation counter
    uint32_t parent_hash;        // Lineage
    
    // Cognition parameters
    float    cognition_bias;     // [0=logic, 1=creative]
    float    halt_threshold;
    
    // Personality Genes (Phase 7+)
    float    curiosity_level;
    float    risk_tolerance;
    float    intuition_power;
    uint32_t biometric_seed;
    
    // Specialization
    uint32_t domain_mask;
    
    // Stats
    uint32_t total_interactions;
    uint32_t successful_reflexes;
    uint32_t successful_internals;
    uint32_t escalations;
    float    avg_confidence;
    
    uint8_t  dplus_mode;
    uint8_t  _reserved[15];
} Genomion;

// ============================================================
// API
// ============================================================

// Initialize Genomion with default values (first boot)
void soma_genomion_init_default(Genomion *dna);

// Validate Genomion from loaded bytes
int soma_genomion_validate(const Genomion *dna);

// Mutate Genomion (meta-evolution step)
void soma_genomion_mutate(Genomion *dna, uint32_t *rng, float magnitude);

// Compute hash of Genomion
uint32_t soma_genomion_hash(const Genomion *dna);

// Create child Genomion (copy + mutate + set parent)
void soma_genomion_reproduce(const Genomion *parent, Genomion *child,
                             uint32_t *rng, float magnitude);

#ifdef __cplusplus
}
#endif
