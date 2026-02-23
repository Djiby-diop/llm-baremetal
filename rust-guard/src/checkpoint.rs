//! Checkpoint and recovery mechanism for OO entities
//!
//! Provides:
//! - Snapshot creation (minimal state capture)
//! - Recovery hints (which fields are corrupted)
//! - Sanitization helpers (fix minor corruption)
//!
//! Philosophy: Preserve what can be preserved, reset what must be reset

use crate::ffi::{OoEntity, OoStatus, ActionState};

/// Checkpoint snapshot (minimal state for recovery)
#[repr(C)]
pub struct Checkpoint {
    pub id: i32,
    pub status: i32,
    pub energy: i32,
    pub ticks: i32,
    pub goal: [u8; 160],
    pub agenda_count: i32,
}

/// Create checkpoint from entity (before risky operation)
pub fn create_checkpoint(entity: &OoEntity) -> Option<Checkpoint> {
    if entity.used == 0 {
        return None;
    }

    Some(Checkpoint {
        id: entity.id,
        status: entity.status,
        energy: entity.energy,
        ticks: entity.ticks,
        goal: entity.goal,
        agenda_count: entity.agenda_count,
    })
}

/// Restore entity from checkpoint (after corruption detected)
///
/// Strategy:
/// - Preserve: id, status, energy, ticks, goal
/// - Reset: notes, digest (can be regenerated)
/// - Sanitize: agenda (remove corrupted items)
pub fn restore_from_checkpoint(entity: &mut OoEntity, checkpoint: &Checkpoint) {
    entity.used = 1;
    entity.id = checkpoint.id;
    entity.status = checkpoint.status;
    entity.energy = checkpoint.energy;
    entity.ticks = checkpoint.ticks;
    entity.goal = checkpoint.goal;

    // Reset notes (can be regenerated)
    entity.notes = [0u8; 1024];
    entity.notes_len = 0;
    entity.notes_truncated = 0;

    // Reset digest (can be regenerated)
    entity.digest = [0u8; 256];

    // Sanitize agenda count
    if checkpoint.agenda_count >= 0 && checkpoint.agenda_count <= 8 {
        entity.agenda_count = checkpoint.agenda_count;
    } else {
        entity.agenda_count = 0;
    }

    // Sanitize agenda items
    for i in 0..8 {
        if i >= entity.agenda_count as usize {
            entity.agenda[i].text = [0u8; 96];
            entity.agenda[i].state = ActionState::Todo as i32;
            entity.agenda[i].prio = 0;
        }
    }
}

/// Attempt to sanitize entity in place (fix minor corruption)
///
/// Returns true if sanitization succeeded, false if unrecoverable
pub fn sanitize_entity(entity: &mut OoEntity) -> bool {
    // If unused, nothing to sanitize
    if entity.used == 0 {
        return true;
    }

    // Fix ID if negative
    if entity.id <= 0 {
        entity.id = 1; // Default ID
    }

    // Fix status if out of range
    if entity.status < OoStatus::Idle as i32 || entity.status > OoStatus::Killed as i32 {
        entity.status = OoStatus::Idle as i32;
    }

    // Clamp energy to reasonable bounds
    if entity.energy < -1000 {
        entity.energy = -1000;
    }
    if entity.energy > 10000 {
        entity.energy = 10000;
    }

    // Fix ticks if negative
    if entity.ticks < 0 {
        entity.ticks = 0;
    }

    // Fix notes_len if out of bounds
    if entity.notes_len < 0 || entity.notes_len > 1024 {
        entity.notes_len = 0;
    }

    // Fix notes_truncated flag
    if entity.notes_truncated != 0 && entity.notes_truncated != 1 {
        entity.notes_truncated = 0;
    }

    // Fix agenda_count if out of bounds
    if entity.agenda_count < 0 {
        entity.agenda_count = 0;
    }
    if entity.agenda_count > 8 {
        entity.agenda_count = 8;
    }

    // Sanitize agenda items
    for i in 0..entity.agenda_count as usize {
        let item = &mut entity.agenda[i];

        // Fix invalid state
        if item.state < ActionState::Todo as i32 || item.state > ActionState::Done as i32 {
            item.state = ActionState::Todo as i32;
        }

        // Clamp priority
        if item.prio < -100 {
            item.prio = -100;
        }
        if item.prio > 100 {
            item.prio = 100;
        }

        // Ensure null termination
        item.text[95] = 0;
    }

    // Ensure string buffers are null-terminated
    entity.goal[159] = 0;
    entity.notes[1023] = 0;
    entity.digest[255] = 0;

    true
}

/// FFI: Create checkpoint (returns 1 on success, 0 if entity invalid)
#[no_mangle]
pub extern "C" fn rust_checkpoint_create(
    entity_ptr: *const OoEntity,
    checkpoint_ptr: *mut Checkpoint,
) -> i32 {
    if entity_ptr.is_null() || checkpoint_ptr.is_null() {
        return 0;
    }

    let entity = unsafe { &*entity_ptr };
    let checkpoint_out = unsafe { &mut *checkpoint_ptr };

    match create_checkpoint(entity) {
        Some(checkpoint) => {
            *checkpoint_out = checkpoint;
            1
        }
        None => 0,
    }
}

/// FFI: Restore from checkpoint
#[no_mangle]
pub extern "C" fn rust_checkpoint_restore(
    entity_ptr: *mut OoEntity,
    checkpoint_ptr: *const Checkpoint,
) -> i32 {
    if entity_ptr.is_null() || checkpoint_ptr.is_null() {
        return 0;
    }

    let entity = unsafe { &mut *entity_ptr };
    let checkpoint = unsafe { &*checkpoint_ptr };

    restore_from_checkpoint(entity, checkpoint);
    1
}

/// FFI: Sanitize entity in place (returns 1 on success)
#[no_mangle]
pub extern "C" fn rust_sanitize_entity(entity_ptr: *mut OoEntity) -> i32 {
    if entity_ptr.is_null() {
        return 0;
    }

    let entity = unsafe { &mut *entity_ptr };

    if sanitize_entity(entity) {
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn create_test_entity() -> OoEntity {
        let mut entity = OoEntity {
            used: 1,
            id: 123,
            status: OoStatus::Running as i32,
            energy: 100,
            ticks: 50,
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
            agenda_count: 2,
        };

        let goal_text = b"Test Goal";
        entity.goal[..goal_text.len()].copy_from_slice(goal_text);

        entity
    }

    #[test]
    fn test_checkpoint_create() {
        let entity = create_test_entity();
        let checkpoint = create_checkpoint(&entity).unwrap();

        assert_eq!(checkpoint.id, 123);
        assert_eq!(checkpoint.energy, 100);
        assert_eq!(checkpoint.ticks, 50);
        assert_eq!(checkpoint.agenda_count, 2);
    }

    #[test]
    fn test_checkpoint_restore() {
        let entity = create_test_entity();
        let checkpoint = create_checkpoint(&entity).unwrap();

        let mut corrupted = entity.clone();
        corrupted.notes_len = 999999; // Corrupt
        corrupted.energy = 999999; // Corrupt

        restore_from_checkpoint(&mut corrupted, &checkpoint);

        assert_eq!(corrupted.id, 123);
        assert_eq!(corrupted.energy, 100); // Restored
        assert_eq!(corrupted.notes_len, 0); // Reset
    }

    #[test]
    fn test_sanitize_negative_id() {
        let mut entity = create_test_entity();
        entity.id = -42;

        sanitize_entity(&mut entity);
        assert!(entity.id > 0);
    }

    #[test]
    fn test_sanitize_invalid_status() {
        let mut entity = create_test_entity();
        entity.status = 999;

        sanitize_entity(&mut entity);
        assert!(entity.status >= OoStatus::Idle as i32);
        assert!(entity.status <= OoStatus::Killed as i32);
    }

    #[test]
    fn test_sanitize_out_of_bounds_energy() {
        let mut entity = create_test_entity();
        entity.energy = 999999;

        sanitize_entity(&mut entity);
        assert!(entity.energy <= 10000);
    }
}
