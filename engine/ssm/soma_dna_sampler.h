// soma_dna_sampler.h — SomaMind Phase Q: DNA→Sampler Feedback
//
// Applies SomaDNA cognition parameters (temperature_solar/lunar, top_p_solar/lunar,
// cognition_bias) to the Mamba/LLaMA2 main inference sampling parameters.
//
// Uses a soft blend: 80% from config/profile, 20% from DNA.
// This preserves user and metabion profile settings while allowing the DNA
// to nudge sampling toward its learned optimum over many generations.
//
// Domain-aware: logic-heavy domains use Solar params, creative uses Lunar,
// general uses a bias-weighted blend.
//
// Freestanding C11 — no libc, no malloc. Header-only (inline functions).

#pragma once

#include "soma_dna.h"
#include "soma_router.h"   // SOMA_DOMAIN_*

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Inline clamp helpers
// ============================================================

static inline float sdna_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ============================================================
// soma_dna_blend_temperature
//
// Returns a DNA-blended temperature for the given SomaRouter domain.
// base_temp: temperature from config/profile (preserved at 80%).
// ============================================================
static inline float soma_dna_blend_temperature(const SomaDNA *dna,
                                                int domain,
                                                float base_temp) {
    if (!dna) return base_temp;

    float bias  = sdna_clampf(dna->cognition_bias, 0.0f, 1.0f);
    float solar = sdna_clampf(dna->temperature_solar, 0.05f, 1.5f);
    float lunar = sdna_clampf(dna->temperature_lunar, 0.05f, 1.5f);
    float dna_temp;

    switch (domain) {
        case SOMA_DOMAIN_SYSTEM:
        case SOMA_DOMAIN_CODE:
        case SOMA_DOMAIN_MATH:
        case SOMA_DOMAIN_POLICY:
            // Logic-heavy → Solar-biased (small lunar leakage via bias)
            dna_temp = solar + bias * 0.15f * (lunar - solar);
            break;
        case SOMA_DOMAIN_CREATIVE:
            // Creative → Lunar-biased
            dna_temp = lunar - (1.0f - bias) * 0.15f * (lunar - solar);
            break;
        default:
            // CHAT / GENERAL: full bias blend
            dna_temp = solar + bias * (lunar - solar);
            break;
    }

    // Soft blend: 80% config, 20% DNA
    float result = base_temp * 0.8f + dna_temp * 0.2f;
    return sdna_clampf(result, 0.05f, 1.5f);
}

// ============================================================
// soma_dna_blend_top_p
//
// Returns a DNA-blended top_p for the given domain.
// ============================================================
static inline float soma_dna_blend_top_p(const SomaDNA *dna,
                                          int domain,
                                          float base_top_p) {
    if (!dna) return base_top_p;

    float bias  = sdna_clampf(dna->cognition_bias, 0.0f, 1.0f);
    float solar = sdna_clampf(dna->top_p_solar, 0.5f, 1.0f);
    float lunar = sdna_clampf(dna->top_p_lunar, 0.5f, 1.0f);
    float dna_tp;

    switch (domain) {
        case SOMA_DOMAIN_SYSTEM:
        case SOMA_DOMAIN_CODE:
        case SOMA_DOMAIN_MATH:
        case SOMA_DOMAIN_POLICY:
            dna_tp = solar + bias * 0.15f * (lunar - solar);
            break;
        case SOMA_DOMAIN_CREATIVE:
            dna_tp = lunar - (1.0f - bias) * 0.15f * (lunar - solar);
            break;
        default:
            dna_tp = solar + bias * (lunar - solar);
            break;
    }

    float result = base_top_p * 0.8f + dna_tp * 0.2f;
    return sdna_clampf(result, 0.5f, 1.0f);
}

// ============================================================
// soma_dna_apply_warden_pressure
//
// Reduces temperature under HIGH/CRITICAL warden pressure.
// Under HIGH:     temperature *= 0.90  (more focused outputs)
// Under CRITICAL: temperature *= 0.75  (near-greedy)
// ============================================================
static inline float soma_dna_pressure_temperature(float temperature,
                                                   int pressure_level) {
    switch (pressure_level) {
        case 2: return sdna_clampf(temperature * 0.90f, 0.05f, 1.5f);  // HIGH
        case 3: return sdna_clampf(temperature * 0.75f, 0.05f, 1.5f);  // CRITICAL
        default: return temperature;
    }
}

#ifdef __cplusplus
}
#endif
