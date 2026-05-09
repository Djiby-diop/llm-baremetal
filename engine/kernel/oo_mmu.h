/* oo_mmu.h — x86_64 4-level paging  Phase 5B
 * =============================================
 * Sets up page tables after ExitBootServices.
 * Memory layout:
 *
 *  Virtual address space:
 *   0x0000_0000_0000_0000 — 0x0000_7FFF_FFFF_FFFF  User space (128 TB)
 *   0xFFFF_8000_0000_0000 — 0xFFFF_FFFF_FFFF_FFFF  Kernel space (128 TB)
 *
 *  OO kernel mapping:
 *   Physical 0x0000 — 0x00FF_FFFF (16 MB) → identity mapped (kernel image)
 *   Physical RAM → mapped at OO_KBASE (0xFFFF_8000_0000_0000 + phys)
 *   Framebuffer  → mapped at OO_FB_VIRT
 *   LAPIC MMIO   → mapped at OO_LAPIC_VIRT
 *
 * Page table sizes (4K pages):
 *   PML4: 512 × 8B = 4K (1 page)
 *   PDPT: 512 × 8B = 4K per PML4 entry used
 *   PD:   512 × 8B = 4K per PDPT entry used
 *   PT:   512 × 8B = 4K per PD entry used
 *
 * Entry flags:
 *   PRESENT   bit 0
 *   WRITABLE  bit 1
 *   USER      bit 2
 *   PWT       bit 3  (write-through)
 *   PCD       bit 4  (cache disable — use for MMIO)
 *   ACCESSED  bit 5
 *   DIRTY     bit 6
 *   HUGEPAGE  bit 7  (2MB pages in PD entries)
 *   NX        bit 63 (no-execute)
 *
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include "oo_exit_boot.h"

/* Virtual base for kernel physical mapping */
#define OO_KBASE         0xFFFF800000000000ULL
#define OO_FB_VIRT       0xFFFF900000000000ULL
#define OO_LAPIC_VIRT    0xFFFFFFFFFEE00000ULL

/* Page entry flags */
#define OO_PTE_P         (1ULL << 0)   /* Present */
#define OO_PTE_W         (1ULL << 1)   /* Writable */
#define OO_PTE_U         (1ULL << 2)   /* User accessible */
#define OO_PTE_PWT       (1ULL << 3)   /* Write-through */
#define OO_PTE_PCD       (1ULL << 4)   /* Cache disable */
#define OO_PTE_HUGE      (1ULL << 7)   /* 2MB huge page (in PD) */
#define OO_PTE_G         (1ULL << 8)   /* Global */
#define OO_PTE_NX        (1ULL << 63)  /* No-execute */

#define OO_PAGE_SIZE     4096ULL
#define OO_HUGE_SIZE     (2ULL * 1024 * 1024)   /* 2MB */
#define OO_PAGE_MASK     (~(OO_PAGE_SIZE - 1))

/* Max page table pages we pre-allocate statically */
#define OO_PT_POOL_PAGES 64

typedef UINT64 OoPte;   /* page table entry */

typedef struct {
    int    initialized;
    /* PML4 root — must be 4K aligned */
    OoPte *pml4;
    /* Pool of pre-allocated page table pages */
    UINT64 pt_pool_base;
    UINT32 pt_pool_used;   /* pages used from pool */
    UINT32 pt_pool_total;  /* = OO_PT_POOL_PAGES */
    /* Stats */
    UINT64 pages_mapped;
    UINT64 huge_pages_mapped;
} OoMmuCtx;

/* API */
EFI_STATUS oo_mmu_init(OoMmuCtx *ctx);

/* Map physical region → virtual (4K pages) */
EFI_STATUS oo_mmu_map(OoMmuCtx *ctx, UINT64 virt, UINT64 phys,
                       UINT64 size, UINT64 flags);

/* Map physical region → virtual (2MB huge pages, faster) */
EFI_STATUS oo_mmu_map_huge(OoMmuCtx *ctx, UINT64 virt, UINT64 phys,
                             UINT64 size, UINT64 flags);

/* Identity-map the first <size> bytes of physical RAM */
EFI_STATUS oo_mmu_identity_map(OoMmuCtx *ctx, UINT64 size);

/* Map framebuffer MMIO */
EFI_STATUS oo_mmu_map_fb(OoMmuCtx *ctx, UINT64 fb_phys,
                           UINT64 fb_size);

/* Build full OO kernel map from boot state */
EFI_STATUS oo_mmu_build(OoMmuCtx *ctx, const OoBootState *bs);

/* Activate: load PML4 into CR3 */
void oo_mmu_activate(OoMmuCtx *ctx);

/* Translate virtual → physical (walk page tables) */
UINT64 oo_mmu_v2p(const OoMmuCtx *ctx, UINT64 virt);

/* Print stats */
void oo_mmu_print(const OoMmuCtx *ctx);

/* REPL */
int oo_mmu_repl_cmd(OoMmuCtx *ctx, const OoBootState *bs,
                     const char *cmd);

extern OoMmuCtx g_mmu;
