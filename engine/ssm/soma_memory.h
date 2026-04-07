// soma_memory.h — SomaMind Phase H: Session Memory & Journal Reflex
//
// Maintains a rolling ring buffer of recent interactions (prompt + response
// summary) and injects relevant historical context before inference.
//
// Responsibilities:
//   1. Record each (prompt, response) pair after inference
//   2. Before next inference, scan prompt for similarity to past entries
//   3. Track system-level events: boot count, model name, last error, turn #
//   4. Inject a [MEM: ...] prefix when relevant history is found
//
// Injection format:
//   [MEM: turn=5 seen=1 last="<truncated response>" boot=2 model=oo_v3]
//
// Features:
//   - Freestanding C11 — no libc, no malloc, stack + static ring buffer
//   - Ring buffer: SOMA_MEM_MAX_ENTRIES slots (circular overwrite)
//   - Similarity: simple hash + substring prefix match (first 24 chars)
//   - Boot & model tracking: updated on /ssm_load and at init
//   - Injection only fires when a match is found (triggered=1)
//
// Freestanding C11 — no libc, no UEFI dependency in this header.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================
#define SOMA_MEM_MAX_ENTRIES     16    // Rolling window size
#define SOMA_MEM_PROMPT_LEN      80    // Max chars stored per prompt
#define SOMA_MEM_RESPONSE_LEN    120   // Max chars stored per response summary
#define SOMA_MEM_MODEL_LEN       32    // Max model name length
#define SOMA_MEM_INJECT_MAX      256   // Max injection string length
#define SOMA_MEM_SIM_PREFIX      24    // Prefix length used for similarity

// ============================================================
// One memory entry
// ============================================================
typedef struct {
    char  prompt[SOMA_MEM_PROMPT_LEN];       // Stored prompt (truncated)
    char  response[SOMA_MEM_RESPONSE_LEN];   // Stored response summary
    int   turn;                              // Turn index when recorded
    int   valid;                             // 1 = slot contains real data
    unsigned int prompt_hash;               // Fast hash for similarity
} SomaMemEntry;

// ============================================================
// Scan result
// ============================================================
typedef struct {
    // Best matching past entry (if any)
    int  match_found;
    int  match_turn;
    char match_prompt[SOMA_MEM_PROMPT_LEN];
    char match_response[SOMA_MEM_RESPONSE_LEN];
    int  match_similarity;   // 0-100 rough score

    // Current session state at scan time
    int  current_turn;
    int  boot_count;
    char model_name[SOMA_MEM_MODEL_LEN];

    // Injection string: "[MEM: ...]"
    char injection[SOMA_MEM_INJECT_MAX];
    int  injection_len;
    int  triggered;
} SomaMemResult;

// ============================================================
// Context
// ============================================================
typedef struct {
    int          enabled;
    int          total_turns;          // Total (prompt,response) pairs recorded
    int          total_triggers;       // Scans that produced an injection
    int          boot_count;           // Incremented on each init call
    char         model_name[SOMA_MEM_MODEL_LEN]; // Current loaded model

    // Ring buffer
    SomaMemEntry entries[SOMA_MEM_MAX_ENTRIES];
    int          head;                 // Next slot to write (wraps)
    int          count;                // How many valid entries
} SomaMemCtx;

// ============================================================
// API
// ============================================================

// Initialize context. Increments boot_count each call.
void soma_memory_init(SomaMemCtx *ctx);

// Notify memory of a model load event (updates model_name).
void soma_memory_set_model(SomaMemCtx *ctx, const char *model_name);

// Scan a prompt before inference — looks for past matches.
// Returns a result with injection string if ctx->enabled and a match found.
SomaMemResult soma_memory_scan(SomaMemCtx *ctx, const char *prompt);

// Record a (prompt, response_summary) pair after inference.
// Call after generate() completes.
void soma_memory_record(SomaMemCtx *ctx, const char *prompt, const char *response_summary);

// Print memory stats to serial (for /soma_memory_stats).
void soma_memory_print_stats(const SomaMemCtx *ctx);

#ifdef __cplusplus
}
#endif
