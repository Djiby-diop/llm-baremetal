//! FFI boundary layer between C and Rust
//!
//! All functions exposed to C must:
//! - Use #[no_mangle]
//! - Use extern "C"
//! - Accept/return FFI-safe types only
//! - Never panic (return error codes instead)

use crate::validator::{ValidationResult, validate_oo_entity_internal};

/// OO Entity status (must match C enum)
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OoStatus {
    Idle = 0,
    Running = 1,
    Done = 2,
    Killed = 3,
}

/// Agenda action state (must match C enum)
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ActionState {
    Todo = 0,
    Doing = 1,
    Done = 2,
}

/// Agenda item (must match C struct layout)
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct AgendaItem {
    pub text: [u8; 96],
    pub state: i32,
    pub prio: i32,
}

/// OO Entity (must match C struct layout exactly)
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OoEntity {
    pub used: i32,
    pub id: i32,
    pub status: i32, // LlmkOoStatus as i32
    pub energy: i32,
    pub ticks: i32,
    pub goal: [u8; 160],
    pub notes: [u8; 1024],
    pub notes_len: i32,
    pub notes_truncated: i32,
    pub digest: [u8; 256],
    pub agenda: [AgendaItem; 8],
    pub agenda_count: i32,
}

/// Validation error codes (returned to C)
pub const VALIDATE_OK: i32 = 0;
pub const VALIDATE_ERR_NULL_PTR: i32 = -1;
pub const VALIDATE_ERR_CORRUPTED_HEADER: i32 = -2;
pub const VALIDATE_ERR_CORRUPTED_STRINGS: i32 = -3;
pub const VALIDATE_ERR_CORRUPTED_AGENDA: i32 = -4;
pub const VALIDATE_ERR_CORRUPTED_COUNTERS: i32 = -5;

/// Main validation entry point called from C
///
/// # Arguments
/// * `ptr` - Pointer to LlmkOoEntity structure
///
/// # Returns
/// * 0 (VALIDATE_OK) if validation passes
/// * Negative error code if corruption detected
///
/// # Safety
/// Caller must ensure ptr points to valid LlmkOoEntity with proper alignment
#[no_mangle]
pub extern "C" fn rust_validate_oo_entity(ptr: *const OoEntity) -> i32 {
    // Null check
    if ptr.is_null() {
        return VALIDATE_ERR_NULL_PTR;
    }

    // Safety: Caller guarantees valid pointer
    let entity = unsafe { &*ptr };

    // Run validation
    match validate_oo_entity_internal(entity) {
        ValidationResult::Ok => VALIDATE_OK,
        ValidationResult::CorruptedHeader => VALIDATE_ERR_CORRUPTED_HEADER,
        ValidationResult::CorruptedStrings => VALIDATE_ERR_CORRUPTED_STRINGS,
        ValidationResult::CorruptedAgenda => VALIDATE_ERR_CORRUPTED_AGENDA,
        ValidationResult::CorruptedCounters => VALIDATE_ERR_CORRUPTED_COUNTERS,
    }
}

/// Fast check if entity slot is used (no deep validation)
#[no_mangle]
pub extern "C" fn rust_check_oo_entity_used(ptr: *const OoEntity) -> i32 {
    if ptr.is_null() {
        return 0;
    }
    let entity = unsafe { &*ptr };
    if entity.used != 0 { 1 } else { 0 }
}

/// Get entity ID (returns -1 if invalid)
#[no_mangle]
pub extern "C" fn rust_get_oo_entity_id(ptr: *const OoEntity) -> i32 {
    if ptr.is_null() {
        return -1;
    }
    let entity = unsafe { &*ptr };
    entity.id
}
