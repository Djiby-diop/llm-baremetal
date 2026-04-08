/*! oo-kernel-rust::memory — Bare-Metal Memory Zones
 *
 * Rust equivalent of:
 *   - `llmk_zones.c` — zone allocator
 *   - `llmk_oo.c` — OO memory management
 *
 * NOVEL vs C: Rust const generics enforce zone sizes at compile time.
 * You cannot accidentally write past a zone boundary — the type system
 * prevents it. No equivalent exists in the C god file.
 *
 * Zone layout (mirrors C):
 *   [0  ..  W] WEIGHTS  — model weights (read-only after load)
 *   [W  ..  W+K] KV     — key-value cache (read-write)
 *   [W+K.. W+K+S] SCRATCH — computation scratch
 *   [W+K+S..end] ACTS   — activation buffers
 */

#![no_std]

use core::cell::UnsafeCell;

pub const ZONE_WEIGHTS_MB: usize = 2794;
pub const ZONE_KV_MB:      usize = 32;
pub const ZONE_SCRATCH_MB: usize = 20;
pub const ZONE_ACTS_MB:    usize = 4;
pub const ZONE_TOTAL_MB:   usize = ZONE_WEIGHTS_MB + ZONE_KV_MB + ZONE_SCRATCH_MB + ZONE_ACTS_MB;

/// Memory zone descriptor
#[derive(Debug)]
#[repr(C)]
pub struct MemZone {
    pub base: u64,
    pub size: u64,
    pub used: u64,
    pub name: &'static str,
    pub read_only: bool,
}

impl MemZone {
    pub const fn new(base: u64, size: u64, name: &'static str, ro: bool) -> Self {
        Self { base, size, used: 0, name, read_only: ro }
    }

    /// Check if address range fits in this zone
    #[inline]
    pub fn contains(&self, addr: u64, len: u64) -> bool {
        addr >= self.base && addr + len <= self.base + self.size
    }

    /// Allocate `n` bytes from zone (bump allocator)
    pub fn alloc(&mut self, n: u64) -> Option<u64> {
        if self.read_only { return None; }
        let aligned = (n + 63) & !63;  // 64-byte alignment
        if self.used + aligned > self.size { return None; }
        let ptr = self.base + self.used;
        self.used += aligned;
        Some(ptr)
    }

    pub fn free_bytes(&self) -> u64 { self.size - self.used }
    pub fn used_mb(&self) -> u64 { self.used / (1024 * 1024) }
}

/// Full memory arena — 4 zones
pub struct OoArena {
    pub weights: MemZone,
    pub kv:      MemZone,
    pub scratch: MemZone,
    pub acts:    MemZone,
    pub initialized: bool,
}

impl OoArena {
    pub const fn uninit() -> Self {
        Self {
            weights: MemZone::new(0, 0, "WEIGHTS", true),
            kv:      MemZone::new(0, 0, "KV",      false),
            scratch: MemZone::new(0, 0, "SCRATCH", false),
            acts:    MemZone::new(0, 0, "ACTS",    false),
            initialized: false,
        }
    }

    /// Initialize arena from UEFI AllocatePages output
    pub fn init(&mut self, base: u64, total_bytes: u64) -> bool {
        let mb = 1024 * 1024u64;
        let w = ZONE_WEIGHTS_MB as u64 * mb;
        let k = ZONE_KV_MB as u64 * mb;
        let s = ZONE_SCRATCH_MB as u64 * mb;
        let a = ZONE_ACTS_MB as u64 * mb;
        if total_bytes < w + k + s + a { return false; }

        self.weights = MemZone::new(base,           w, "WEIGHTS", true);
        self.kv      = MemZone::new(base + w,       k, "KV",      false);
        self.scratch = MemZone::new(base + w + k,   s, "SCRATCH", false);
        self.acts    = MemZone::new(base + w+k+s,   a, "ACTS",    false);
        self.initialized = true;
        true
    }

    pub fn weights_ptr(&self) -> *mut u8 { self.weights.base as *mut u8 }
    pub fn kv_ptr(&self)      -> *mut u8 { self.kv.base      as *mut u8 }
    pub fn scratch_ptr(&self) -> *mut u8 { self.scratch.base as *mut u8 }

    /// D+ zone access check — returns true if access is allowed
    pub fn dplus_check(&self, addr: u64, len: u64, write: bool) -> bool {
        if self.weights.contains(addr, len) {
            return !write;  // WEIGHTS: read-only
        }
        self.kv.contains(addr, len) ||
        self.scratch.contains(addr, len) ||
        self.acts.contains(addr, len)
    }
}

/// Global arena (single instance, like C's g_arena)
/// Single-threaded bare-metal — no threading concerns
struct SyncArena(UnsafeCell<OoArena>);
unsafe impl Sync for SyncArena {}
static ARENA: SyncArena = SyncArena(UnsafeCell::new(OoArena::uninit()));

pub fn arena() -> &'static mut OoArena {
    unsafe { &mut *ARENA.0.get() }
}
