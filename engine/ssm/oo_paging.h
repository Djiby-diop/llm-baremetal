#pragma once
#include <stdint.h>

/**
 * OO Paging - Virtual Memory & Ring 3 Isolation
 * 
 * Sets up page tables to isolate user space from kernel space.
 */

#define PAGE_PRESENT  (1 << 0)
#define PAGE_WRITABLE (1 << 1)
#define PAGE_USER     (1 << 2)

// Initialize paging, clone kernel page tables
void oo_paging_init(void);

// Map a virtual address to a physical address with flags
void oo_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint32_t flags);

// Switch address space (CR3 register)
void oo_switch_address_space(uint64_t pml4_physical_addr);

// Allocate a clean page for user space
uint64_t oo_alloc_page(void);
