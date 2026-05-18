#pragma once

#include <stdint.h>

/**
 * SYMBION Engine — Direct Hardware Access Organ
 * 
 * Bypasses UEFI/BIOS to talk directly to the silicon.
 */

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint32_t class_code;
} SymbionPCIDevice;

typedef struct {
    SymbionPCIDevice devices[64];
    uint32_t         device_count;
    
    // Hardware metrics
    uint32_t         cpu_cores;
    uint64_t         total_ram_bytes;
} SymbionCtx;

/* --- Hardware Primitives (ASM) --- */

static inline uint32_t oo_pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    
    // Create configuration address
    address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    
    // Write to CONFIG_ADDRESS (0xCF8)
    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"((uint16_t)0xCF8));
    
    // Read from CONFIG_DATA (0xCFC)
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "Nd"((uint16_t)0xCFC));
    
    return result;
}

/* --- Symbion API --- */

/**
 * Scans the entire PCI bus to map the Organism's physical body.
 */
void symbion_scan_body(SymbionCtx *ctx);

/**
 * Identifies high-speed targets for Mercatorion (NICs).
 */
SymbionPCIDevice* symbion_find_nic(SymbionCtx *ctx);
