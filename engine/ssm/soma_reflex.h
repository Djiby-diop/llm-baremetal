// soma_reflex.h — SomaMind Phase F: Symbolic Reflex Engine
//
// Intercepts arithmetic patterns in the prompt BEFORE LLM inference.
// Resolves variable assignments and direct math questions symbolically
// (exact integer/float arithmetic), then injects correct results as
// context prefix tokens so the LLM sees pre-computed facts.
//
// This directly addresses Mamba-2.8B's arithmetic hallucination:
//   "100/4 = 25, then 25+10 = 30"  → reflex injects "C=35" before generation.
//
// Supported patterns:
//   1. Variable chains:   A=100 B=A/4 C=B+10 D=A*C
//   2. Direct questions:  "combien font 25+10?" / "what is 2^8?"
//   3. Mixed:             "Si A=50 et B=A*2, combien vaut B?"
//
// Features:
//   - Stack-only, no malloc — freestanding C11
//   - Max 32 variables per scan
//   - Supports: + - * / % ^ (integer power) ( ) parentheses
//   - Dependency ordering: resolves in topo order, detects cycles
//   - Output: null-terminated ASCII string injected before prompt
//
// Freestanding C11 — no libc, no UEFI dependency in this header.

#pragma once

#include "ssm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================
#define SOMA_REFLEX_MAX_VARS     32   // Max variables per scan
#define SOMA_REFLEX_VAR_NAME_LEN  8   // Max variable name length
#define SOMA_REFLEX_INJECT_MAX  256   // Max injection string length
#define SOMA_REFLEX_EXPR_LEN     64   // Max expression string length

// ============================================================
// Variable entry
// ============================================================
typedef struct {
    char    name[SOMA_REFLEX_VAR_NAME_LEN]; // Variable name (uppercase)
    float   value;                           // Resolved numeric value
    int     resolved;                        // 1 = value is known
    char    expr[SOMA_REFLEX_EXPR_LEN];      // Raw expression string
    int     dep_mask;                        // Bitmask of vars this depends on
} SomaReflexVar;

// ============================================================
// Scan result
// ============================================================
typedef struct {
    SomaReflexVar vars[SOMA_REFLEX_MAX_VARS];
    int           var_count;

    // Direct math question result (e.g., "what is 2+3?")
    int           has_direct_result;
    float         direct_result;
    char          direct_expr[SOMA_REFLEX_EXPR_LEN];

    // Injection string: prepended to prompt before inference
    // Format: "[MATH: A=100 B=25 C=35]\n"
    char          injection[SOMA_REFLEX_INJECT_MAX];
    int           injection_len;

    // Stats
    int           vars_resolved;
    int           vars_failed;   // Circular or unknown deps
    int           triggered;     // 1 = reflex fired (found something)
} SomaReflexResult;

// ============================================================
// Context (lightweight — just enabled flag + session stats)
// ============================================================
typedef struct {
    int     enabled;
    int     total_scans;
    int     total_triggers;  // Scans where reflex fired
    int     total_resolved;  // Total variables resolved across all scans
    float   last_inject_len; // Chars injected in last trigger
} SomaReflexCtx;

// ============================================================
// API
// ============================================================

// Initialize context
void soma_reflex_init(SomaReflexCtx *ctx);

// Scan a prompt string for math patterns.
// Fills result. Call before inference when ctx->enabled.
SomaReflexResult soma_reflex_scan(SomaReflexCtx *ctx, const char *prompt);

// Evaluate a single arithmetic expression string (no variables).
// Returns 1 on success, 0 on parse error. Result in *out.
int soma_reflex_eval(const char *expr, float *out);

#ifdef __cplusplus
}
#endif
