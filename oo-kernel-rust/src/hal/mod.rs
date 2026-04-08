/*! oo-kernel-rust::hal — Hardware Abstraction Layer
 *
 * Rust equivalent of oo-hal/oo_hal.c
 * Provides safe wrappers around UEFI protocols.
 */

#![no_std]

use core::arch::asm;

/* ── TSC timer ────────────────────────────────────────────────────── */

#[inline]
pub fn rdtsc() -> u64 {
    let lo: u32;
    let hi: u32;
    unsafe { asm!("rdtsc", out("eax") lo, out("edx") hi, options(nomem, nostack)); }
    ((hi as u64) << 32) | (lo as u64)
}

pub struct OoTimer {
    pub tsc_freq_hz: u64,
    pub boot_tsc: u64,
}

impl OoTimer {
    pub fn new() -> Self {
        Self { tsc_freq_hz: 3_000_000_000, boot_tsc: rdtsc() }
    }

    pub fn elapsed_us(&self) -> u64 {
        (rdtsc() - self.boot_tsc) / (self.tsc_freq_hz / 1_000_000)
    }

    pub fn elapsed_ms(&self) -> u64 { self.elapsed_us() / 1000 }

    pub fn sleep_us(&self, us: u64) {
        let target = rdtsc() + us * (self.tsc_freq_hz / 1_000_000);
        while rdtsc() < target {
            unsafe { asm!("pause", options(nomem, nostack)); }
        }
    }
}

/* ── Port I/O (x86 keyboard 8042) ────────────────────────────────── */

#[inline]
pub unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    asm!("in al, dx", out("al") val, in("dx") port, options(nomem, nostack));
    val
}

#[inline]
pub unsafe fn outb(port: u16, val: u8) {
    asm!("out dx, al", in("dx") port, in("al") val, options(nomem, nostack));
}

pub fn keyboard_ready() -> bool {
    unsafe { inb(0x64) & 0x01 != 0 }
}

pub fn keyboard_read_scancode() -> u8 {
    while !keyboard_ready() {
        unsafe { asm!("pause", options(nomem, nostack)); }
    }
    unsafe { inb(0x60) }
}

/* ── CSPRNG (RDRAND) ────────────────────────────────────────────── */

pub fn rdrand_u64() -> Option<u64> {
    let mut val: u64;
    let mut ok: u8;
    unsafe {
        asm!("rdrand {0:r}; setc {1}", out(reg) val, out(reg_byte) ok, options(nomem, nostack));
    }
    if ok != 0 { Some(val) } else { None }
}

pub fn rdrand_seed() -> u64 {
    for _ in 0..10 {
        if let Some(v) = rdrand_u64() { return v; }
    }
    rdtsc()  /* fallback: TSC seed */
}
