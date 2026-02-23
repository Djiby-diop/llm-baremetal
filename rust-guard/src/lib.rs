//! 🛡️ RUST-GUARD — Neural Protector FFI Validator
//!
//! UEFI-compatible Rust validation layer for C Soma structures.
//!
//! Manifesto: Un bug en C ne peut jamais tuer l'OO.
//!
//! Architecture:
//! - No heap allocations (UEFI constraint)
//! - No standard library (no_std for UEFI)
//! - Pure validation logic (no side effects)
//! - FFI-safe boundaries (#[repr(C)])
//!
//! Phase 2.4 objectives:
//! 1. Validate LlmkOoEntity structure integrity
//! 2. Detect corruption patterns
//! 3. Provide recovery hints
//! 4. Zero overhead when validation passes

#![cfg_attr(not(test), no_std)]
#![allow(dead_code)]

#[cfg(all(not(test), not(feature = "std")))]
use core::panic::PanicInfo;

#[cfg(all(not(test), not(feature = "std")))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

pub mod ffi;
pub mod validator;
pub mod checkpoint;

// Re-export main validation function for C FFI
pub use ffi::rust_validate_oo_entity;
