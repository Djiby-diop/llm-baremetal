// soma_logic.h — SomaMind Phase G: Logical Syllogism Reflex
//
// Detects logical reasoning patterns in the prompt and resolves
// them symbolically before inference, injecting derived conclusions.
//
// Supported patterns:
//
//  1. Modus Ponens (MP)
//     "Si A alors B. A est vrai."  → [LOGIC: B est vrai]
//     "If X then Y. X is true."   → [LOGIC: Y is true]
//
//  2. Modus Tollens (MT)
//     "Si A alors B. B est faux." → [LOGIC: A est faux]
//
//  3. Syllogisme universel (Barbara)
//     "Tous les X sont Y. Z est un X." → [LOGIC: Z est un Y]
//
//  4. Chaîne d'implication
//     "A implique B. B implique C. A est vrai." → [LOGIC: B est vrai, C est vrai]
//
//  5. "Donc / Alors / Therefore / Thus / Hence" conclusion extraction
//     "P1. P2. Donc Q." → flag Q as asserted conclusion
//
//  6. Négation simple
//     "A est vrai. A est faux." → [LOGIC: CONTRADICTION détectée]
//
// Output injection format:
//   [LOGIC: conclusion1; conclusion2; ...]
//
// Freestanding C11 — no libc, no malloc, stack-only, max 16 rules.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================
#define SOMA_LOGIC_MAX_FACTS      24   // Max atomic facts per scan
#define SOMA_LOGIC_MAX_RULES      16   // Max IF-THEN rules per scan
#define SOMA_LOGIC_FACT_LEN       48   // Max chars in a fact string
#define SOMA_LOGIC_INJECT_MAX    256   // Max injection string length
#define SOMA_LOGIC_MAX_DERIVED     8   // Max derived conclusions per scan

// ============================================================
// Fact status
// ============================================================
typedef enum {
    SOMA_FACT_UNKNOWN  = 0,
    SOMA_FACT_TRUE     = 1,
    SOMA_FACT_FALSE    = 2,
} SomaFactStatus;

// ============================================================
// Atomic fact
// ============================================================
typedef struct {
    char           text[SOMA_LOGIC_FACT_LEN]; // Normalized fact text
    SomaFactStatus status;                     // TRUE / FALSE / UNKNOWN
    int            derived;                    // 1 = inferred, 0 = stated
} SomaLogicFact;

// ============================================================
// IF-THEN rule
// ============================================================
typedef struct {
    char antecedent[SOMA_LOGIC_FACT_LEN]; // "if A" part
    char consequent[SOMA_LOGIC_FACT_LEN]; // "then B" part
    int  used;                             // 1 = already fired
} SomaLogicRule;

// ============================================================
// Scan result
// ============================================================
typedef struct {
    SomaLogicFact facts[SOMA_LOGIC_MAX_FACTS];
    int           fact_count;
    SomaLogicRule rules[SOMA_LOGIC_MAX_RULES];
    int           rule_count;

    // Derived conclusions (what was newly inferred)
    char derived[SOMA_LOGIC_MAX_DERIVED][SOMA_LOGIC_FACT_LEN];
    int  derived_count;

    // Universal quantifier matches (Barbara syllogism)
    char barbara_derived[SOMA_LOGIC_MAX_DERIVED][SOMA_LOGIC_FACT_LEN];
    int  barbara_count;

    // Contradiction detected?
    int  contradiction;
    char contradiction_fact[SOMA_LOGIC_FACT_LEN];

    // Injection string: "[LOGIC: ...]"
    char injection[SOMA_LOGIC_INJECT_MAX];
    int  injection_len;
    int  triggered;
} SomaLogicResult;

// ============================================================
// Context
// ============================================================
typedef struct {
    int enabled;
    int total_scans;
    int total_triggers;
    int total_derived;
    int total_contradictions;
} SomaLogicCtx;

// ============================================================
// API
// ============================================================

void             soma_logic_init(SomaLogicCtx *ctx);
SomaLogicResult  soma_logic_scan(SomaLogicCtx *ctx, const char *prompt);

#ifdef __cplusplus
}
#endif
