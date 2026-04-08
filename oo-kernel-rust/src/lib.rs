/*! oo-kernel-rust — OO Rust Kernel
 *
 * This is the RUST GOD FILE — a parallel to `llama2_efi_final.c`,
 * written in Rust for superior memory safety without sacrificing
 * bare-metal control.
 *
 * Architecture mirrors the C phases A-Z but with:
 *  - No unsafe pointer arithmetic in phase logic
 *  - Compile-time memory zone bounds via const generics
 *  - D+ policy as type-level constraints (Rust type system enforces rules)
 *  - Zero-cost abstractions for inference hooks
 *
 * Module map:
 *  boot/     — UEFI entry, memory init, phase sequencer
 *  memory/   — Zone allocator, arena, KV cache
 *  inference/ — SSM inference loop (Mamba), phases A-Z
 *  policy/   — D+ policy VM, warden, sentinel
 *  hal/      — Hardware abstraction (display, keyboard, timer)
 *
 * Language: Rust (no_std, no_main for UEFI target)
 * Target: x86_64-unknown-uefi
 * Build: cargo build --target x86_64-unknown-uefi --features uefi
 */

#![no_std]
#![allow(dead_code)]

pub mod boot;
pub mod memory;
pub mod inference;
pub mod policy;
pub mod hal;
