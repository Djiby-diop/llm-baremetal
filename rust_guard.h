#pragma once

// 🛡️ RUST-GUARD FFI HEADER
//
// Neural Protector validation layer for C Soma structures.
// Links against librust_guard.a compiled from rust-guard/
//
// Phase 2.4: UEFI Rust FFI + Recovery Orchestrator
//
// Manifesto: Un bug en C ne peut jamais tuer l'OO.

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration (must match llmk_oo.h LlmkOoEntity)
struct LlmkOoEntity;

// Validation error codes (returned by validation functions)
#define RUST_VALIDATE_OK                      0
#define RUST_VALIDATE_ERR_NULL_PTR           -1
#define RUST_VALIDATE_ERR_CORRUPTED_HEADER   -2
#define RUST_VALIDATE_ERR_CORRUPTED_STRINGS  -3
#define RUST_VALIDATE_ERR_CORRUPTED_AGENDA   -4
#define RUST_VALIDATE_ERR_CORRUPTED_COUNTERS -5

// ============================================================================
// PRIMARY VALIDATION API
// ============================================================================

/**
 * Validate OO entity structure integrity
 * 
 * Checks:
 * - Header fields (used, id, status, energy, ticks)
 * - String buffers (ASCII-only, null-terminated)
 * - Counter consistency (notes_len, agenda_count)
 * - Agenda structure (valid states, priorities)
 * 
 * @param entity Pointer to LlmkOoEntity structure
 * @return 0 if valid, negative error code if corrupted
 * 
 * Performance: < 10 microseconds typical case
 * Thread-safe: Yes (read-only operation)
 */
int rust_validate_oo_entity(const struct LlmkOoEntity *entity);

/**
 * Fast check if entity slot is used (no deep validation)
 * 
 * @param entity Pointer to LlmkOoEntity structure
 * @return 1 if used, 0 if unused or invalid
 */
int rust_check_oo_entity_used(const struct LlmkOoEntity *entity);

/**
 * Get entity ID (returns -1 if invalid)
 * 
 * @param entity Pointer to LlmkOoEntity structure
 * @return Entity ID or -1 if invalid
 */
int rust_get_oo_entity_id(const struct LlmkOoEntity *entity);

// ============================================================================
// CHECKPOINT / RECOVERY API
// ============================================================================

/**
 * Checkpoint structure (minimal state for recovery)
 * 
 * Contains only essential fields that can be safely restored
 * after corruption detection. Notes and digest are NOT saved
 * (can be regenerated).
 */
typedef struct {
    int id;
    int status;
    int energy;
    int ticks;
    char goal[160];
    int agenda_count;
} RustCheckpoint;

/**
 * Create checkpoint from entity (before risky operation)
 * 
 * @param entity Source entity (must be valid)
 * @param checkpoint Output checkpoint structure
 * @return 1 on success, 0 if entity is invalid or unused
 * 
 * Usage pattern:
 *   RustCheckpoint cp;
 *   rust_checkpoint_create(&entity, &cp);
 *   // ... risky operation ...
 *   if (rust_validate_oo_entity(&entity) != 0) {
 *       rust_checkpoint_restore(&entity, &cp);
 *   }
 */
int rust_checkpoint_create(const struct LlmkOoEntity *entity, RustCheckpoint *checkpoint);

/**
 * Restore entity from checkpoint (after corruption detected)
 * 
 * Strategy:
 * - Preserve: id, status, energy, ticks, goal
 * - Reset: notes, digest (can be regenerated)
 * - Sanitize: agenda (remove corrupted items)
 * 
 * @param entity Target entity (will be modified)
 * @param checkpoint Source checkpoint
 * @return 1 on success, 0 if pointers are invalid
 */
int rust_checkpoint_restore(struct LlmkOoEntity *entity, const RustCheckpoint *checkpoint);

/**
 * Sanitize entity in place (fix minor corruption)
 * 
 * Attempts to repair:
 * - Negative IDs → reset to 1
 * - Invalid status → reset to IDLE
 * - Out-of-bounds energy → clamp to [-1000, 10000]
 * - Invalid counters → clamp to valid ranges
 * - Missing null terminators → add them
 * 
 * @param entity Entity to sanitize (will be modified)
 * @return 1 on success, 0 if entity is unrecoverable
 * 
 * Note: This is a best-effort operation. Always validate after sanitizing.
 */
int rust_sanitize_entity(struct LlmkOoEntity *entity);

// ============================================================================
// INTEGRATION GUIDELINES
// ============================================================================

/*
 * RECOMMENDED VALIDATION POINTS:
 * 
 * 1. After loading from disk:
 *    if (rust_validate_oo_entity(&entity) != 0) {
 *        // Corruption detected - reset or skip
 *    }
 * 
 * 2. Before writing to disk:
 *    if (rust_validate_oo_entity(&entity) != 0) {
 *        // Don't persist corrupted state
 *    }
 * 
 * 3. After risky operations (malloc, string manipulation):
 *    RustCheckpoint cp;
 *    rust_checkpoint_create(&entity, &cp);
 *    // ... operation ...
 *    if (rust_validate_oo_entity(&entity) != 0) {
 *        rust_checkpoint_restore(&entity, &cp);
 *    }
 * 
 * 4. Periodic health checks (every N ticks):
 *    if (rust_validate_oo_entity(&entity) != 0) {
 *        rust_sanitize_entity(&entity);
 *    }
 * 
 * PERFORMANCE NOTES:
 * - Validation is read-only (no side effects)
 * - Typical validation: < 10 microseconds
 * - Checkpoint creation: < 5 microseconds
 * - Sanitization: < 15 microseconds
 * 
 * LINKING:
 * - GNU-EFI: Add -lrust_guard to link flags
 * - Link with target/release/librust_guard.a
 * - Ensure Rust library is compiled with panic="abort"
 */

#ifdef __cplusplus
}
#endif
