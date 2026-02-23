//! Core validation logic for OO Entity structures
//!
//! Validation strategy:
//! 1. Header integrity (used, id, status, energy bounds)
//! 2. String buffers (ASCII-only, null-terminated, no overflow)
//! 3. Counter consistency (notes_len, agenda_count)
//! 4. Agenda integrity (valid states, priority bounds)
//!
//! Philosophy: Fail fast, explicit errors, zero allocations

use crate::ffi::{OoEntity, OoStatus, ActionState};

#[derive(Debug, PartialEq)]
pub enum ValidationResult {
    Ok,
    CorruptedHeader,
    CorruptedStrings,
    CorruptedAgenda,
    CorruptedCounters,
}

/// Validate complete OO entity structure
pub fn validate_oo_entity_internal(entity: &OoEntity) -> ValidationResult {
    // Phase 1: Header validation
    if let Err(e) = validate_header(entity) {
        return e;
    }

    // Phase 2: String buffer validation
    if let Err(e) = validate_strings(entity) {
        return e;
    }

    // Phase 3: Counter consistency
    if let Err(e) = validate_counters(entity) {
        return e;
    }

    // Phase 4: Agenda integrity
    if let Err(e) = validate_agenda(entity) {
        return e;
    }

    ValidationResult::Ok
}

/// Validate entity header fields
fn validate_header(entity: &OoEntity) -> Result<(), ValidationResult> {
    // Used flag must be 0 or 1
    if entity.used != 0 && entity.used != 1 {
        return Err(ValidationResult::CorruptedHeader);
    }

    // If not used, skip other checks
    if entity.used == 0 {
        return Ok(());
    }

    // ID must be positive
    if entity.id <= 0 {
        return Err(ValidationResult::CorruptedHeader);
    }

    // Status must be valid enum value
    let status = entity.status;
    if status < OoStatus::Idle as i32 || status > OoStatus::Killed as i32 {
        return Err(ValidationResult::CorruptedHeader);
    }

    // Energy bounds check (-1000 to 10000 reasonable range)
    if entity.energy < -1000 || entity.energy > 10000 {
        return Err(ValidationResult::CorruptedHeader);
    }

    // Ticks must be non-negative
    if entity.ticks < 0 {
        return Err(ValidationResult::CorruptedHeader);
    }

    Ok(())
}

/// Validate string buffers are ASCII and properly terminated
fn validate_strings(entity: &OoEntity) -> Result<(), ValidationResult> {
    if entity.used == 0 {
        return Ok(());
    }

    // Goal buffer
    if !is_valid_ascii_buffer(&entity.goal) {
        return Err(ValidationResult::CorruptedStrings);
    }

    // Notes buffer
    if !is_valid_ascii_buffer(&entity.notes) {
        return Err(ValidationResult::CorruptedStrings);
    }

    // Digest buffer
    if !is_valid_ascii_buffer(&entity.digest) {
        return Err(ValidationResult::CorruptedStrings);
    }

    Ok(())
}

/// Validate counter consistency
fn validate_counters(entity: &OoEntity) -> Result<(), ValidationResult> {
    if entity.used == 0 {
        return Ok(());
    }

    // notes_len must be within buffer bounds
    if entity.notes_len < 0 || entity.notes_len > 1024 {
        return Err(ValidationResult::CorruptedCounters);
    }

    // notes_truncated flag must be 0 or 1
    if entity.notes_truncated != 0 && entity.notes_truncated != 1 {
        return Err(ValidationResult::CorruptedCounters);
    }

    // agenda_count must be within array bounds
    if entity.agenda_count < 0 || entity.agenda_count > 8 {
        return Err(ValidationResult::CorruptedCounters);
    }

    Ok(())
}

/// Validate agenda structure
fn validate_agenda(entity: &OoEntity) -> Result<(), ValidationResult> {
    if entity.used == 0 {
        return Ok(());
    }

    // Check each active agenda item
    for i in 0..entity.agenda_count as usize {
        let item = &entity.agenda[i];

        // Text buffer must be valid ASCII
        if !is_valid_ascii_buffer(&item.text) {
            return Err(ValidationResult::CorruptedAgenda);
        }

        // State must be valid enum value
        if item.state < ActionState::Todo as i32 || item.state > ActionState::Done as i32 {
            return Err(ValidationResult::CorruptedAgenda);
        }

        // Priority bounds (-100 to 100 reasonable range)
        if item.prio < -100 || item.prio > 100 {
            return Err(ValidationResult::CorruptedAgenda);
        }
    }

    Ok(())
}

/// Check if buffer contains valid ASCII and is properly null-terminated
fn is_valid_ascii_buffer(buf: &[u8]) -> bool {
    // Find null terminator
    let mut found_null = false;
    let mut null_pos = buf.len();

    for (i, &byte) in buf.iter().enumerate() {
        if byte == 0 {
            found_null = true;
            null_pos = i;
            break;
        }
    }

    // Must have null terminator
    if !found_null {
        return false;
    }

    // All bytes before null must be printable ASCII or whitespace
    for &byte in &buf[..null_pos] {
        if byte < 0x20 && byte != b'\n' && byte != b'\r' && byte != b'\t' {
            return false; // Control characters
        }
        if byte > 0x7E {
            return false; // Non-ASCII
        }
    }

    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn create_valid_entity() -> OoEntity {
        let mut entity = OoEntity {
            used: 1,
            id: 42,
            status: OoStatus::Running as i32,
            energy: 100,
            ticks: 10,
            goal: [0u8; 160],
            notes: [0u8; 1024],
            notes_len: 0,
            notes_truncated: 0,
            digest: [0u8; 256],
            agenda: [
                super::super::ffi::AgendaItem {
                    text: [0u8; 96],
                    state: ActionState::Todo as i32,
                    prio: 0,
                };
                8
            ],
            agenda_count: 0,
        };

        // Add valid goal
        let goal_text = b"Test goal";
        entity.goal[..goal_text.len()].copy_from_slice(goal_text);

        entity
    }

    #[test]
    fn test_valid_entity() {
        let entity = create_valid_entity();
        assert_eq!(validate_oo_entity_internal(&entity), ValidationResult::Ok);
    }

    #[test]
    fn test_invalid_used_flag() {
        let mut entity = create_valid_entity();
        entity.used = 42; // Invalid
        assert_eq!(
            validate_oo_entity_internal(&entity),
            ValidationResult::CorruptedHeader
        );
    }

    #[test]
    fn test_invalid_id() {
        let mut entity = create_valid_entity();
        entity.id = -1; // Invalid
        assert_eq!(
            validate_oo_entity_internal(&entity),
            ValidationResult::CorruptedHeader
        );
    }

    #[test]
    fn test_invalid_status() {
        let mut entity = create_valid_entity();
        entity.status = 99; // Invalid enum
        assert_eq!(
            validate_oo_entity_internal(&entity),
            ValidationResult::CorruptedHeader
        );
    }

    #[test]
    fn test_invalid_energy() {
        let mut entity = create_valid_entity();
        entity.energy = 50000; // Out of bounds
        assert_eq!(
            validate_oo_entity_internal(&entity),
            ValidationResult::CorruptedHeader
        );
    }

    #[test]
    fn test_invalid_agenda_count() {
        let mut entity = create_valid_entity();
        entity.agenda_count = 99; // Out of bounds
        assert_eq!(
            validate_oo_entity_internal(&entity),
            ValidationResult::CorruptedCounters
        );
    }

    #[test]
    fn test_unused_entity_skips_validation() {
        let mut entity = create_valid_entity();
        entity.used = 0;
        entity.id = -999; // Would fail if checked
        assert_eq!(validate_oo_entity_internal(&entity), ValidationResult::Ok);
    }
}
