/* oo_mmu.c — x86_64 4-level paging  Phase 5B
 * =============================================
 * Builds page tables and activates paging after ExitBootServices.
 * Uses 2MB huge pages for RAM (fast), 4K pages for MMIO.
 * Freestanding C11. No libc.
 */
#include "oo_mmu.h"
#include <efi.h>
#include <efilib.h>

OoMmuCtx g_mmu;

/* Static pool for page table pages — must be 4K aligned */
static UINT8 _pt_pool[OO_PT_POOL_PAGES * 4096] __attribute__((aligned(4096)));

/* ── Allocate one 4K page from pool ────────────────────────────────────── */
static OoPte *_alloc_pt(OoMmuCtx *ctx) {
    if (ctx->pt_pool_used >= ctx->pt_pool_total) return NULL;
    OoPte *p = (OoPte*)(_pt_pool + ctx->pt_pool_used * 4096);
    ctx->pt_pool_used++;
    /* Zero the page */
    for (int i = 0; i < 512; i++) p[i] = 0;
    return p;
}

/* ── Index helpers ──────────────────────────────────────────────────────── */
#define PML4_IDX(va) (((va) >> 39) & 0x1FF)
#define PDPT_IDX(va) (((va) >> 30) & 0x1FF)
#define PD_IDX(va)   (((va) >> 21) & 0x1FF)
#define PT_IDX(va)   (((va) >> 12) & 0x1FF)

/* ── Get or create a child page table ──────────────────────────────────── */
static OoPte *_get_or_create(OoMmuCtx *ctx, OoPte *table, UINTN idx, UINT64 flags) {
    if (table[idx] & OO_PTE_P) {
        /* Already present — return pointer to child table */
        return (OoPte*)(UINTN)(table[idx] & OO_PAGE_MASK);
    }
    OoPte *child = _alloc_pt(ctx);
    if (!child) return NULL;
    table[idx] = (UINT64)(UINTN)child | flags | OO_PTE_P;
    return child;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
EFI_STATUS oo_mmu_init(OoMmuCtx *ctx) {
    for (int i = 0; i < sizeof(*ctx)/8; i++) ((UINT64*)ctx)[i] = 0;
    ctx->pt_pool_base  = (UINT64)(UINTN)_pt_pool;
    ctx->pt_pool_total = OO_PT_POOL_PAGES;

    /* Allocate PML4 from pool */
    ctx->pml4 = _alloc_pt(ctx);
    if (!ctx->pml4) return EFI_OUT_OF_RESOURCES;

    ctx->initialized = 1;
    Print(L"[mmu] Page table pool: %u pages @ 0x%lx\r\n",
          OO_PT_POOL_PAGES, ctx->pt_pool_base);
    return EFI_SUCCESS;
}

/* ── Map 4K pages ────────────────────────────────────────────────────────── */
EFI_STATUS oo_mmu_map(OoMmuCtx *ctx, UINT64 virt, UINT64 phys,
                       UINT64 size, UINT64 flags) {
    if (!ctx || !ctx->pml4) return EFI_NOT_READY;
    flags |= OO_PTE_P;

    UINT64 va = virt & OO_PAGE_MASK;
    UINT64 pa = phys & OO_PAGE_MASK;
    UINT64 end = virt + size;

    while (va < end) {
        OoPte *pdpt = _get_or_create(ctx, ctx->pml4, PML4_IDX(va),
                                      OO_PTE_P | OO_PTE_W);
        if (!pdpt) return EFI_OUT_OF_RESOURCES;
        OoPte *pd   = _get_or_create(ctx, pdpt, PDPT_IDX(va),
                                      OO_PTE_P | OO_PTE_W);
        if (!pd)   return EFI_OUT_OF_RESOURCES;
        OoPte *pt   = _get_or_create(ctx, pd, PD_IDX(va),
                                      OO_PTE_P | OO_PTE_W);
        if (!pt)   return EFI_OUT_OF_RESOURCES;

        pt[PT_IDX(va)] = pa | flags;
        ctx->pages_mapped++;
        va += OO_PAGE_SIZE;
        pa += OO_PAGE_SIZE;
    }
    return EFI_SUCCESS;
}

/* ── Map 2MB huge pages (faster for large RAM regions) ──────────────────── */
EFI_STATUS oo_mmu_map_huge(OoMmuCtx *ctx, UINT64 virt, UINT64 phys,
                             UINT64 size, UINT64 flags) {
    if (!ctx || !ctx->pml4) return EFI_NOT_READY;
    flags |= OO_PTE_P | OO_PTE_HUGE;

    /* Align to 2MB */
    UINT64 va  = virt & ~(OO_HUGE_SIZE - 1);
    UINT64 pa  = phys & ~(OO_HUGE_SIZE - 1);
    UINT64 end = virt + size;

    while (va < end) {
        OoPte *pdpt = _get_or_create(ctx, ctx->pml4, PML4_IDX(va),
                                      OO_PTE_P | OO_PTE_W);
        if (!pdpt) return EFI_OUT_OF_RESOURCES;
        OoPte *pd   = _get_or_create(ctx, pdpt, PDPT_IDX(va),
                                      OO_PTE_P | OO_PTE_W);
        if (!pd)   return EFI_OUT_OF_RESOURCES;

        /* PD entry with HUGE bit = 2MB page */
        pd[PD_IDX(va)] = pa | flags;
        ctx->huge_pages_mapped++;
        va += OO_HUGE_SIZE;
        pa += OO_HUGE_SIZE;
    }
    return EFI_SUCCESS;
}

/* ── Identity map first N bytes ─────────────────────────────────────────── */
EFI_STATUS oo_mmu_identity_map(OoMmuCtx *ctx, UINT64 size) {
    /* Use huge pages for speed — align size up to 2MB */
    UINT64 aligned = (size + OO_HUGE_SIZE - 1) & ~(OO_HUGE_SIZE - 1);
    return oo_mmu_map_huge(ctx, 0, 0, aligned, OO_PTE_W);
}

/* ── Map framebuffer MMIO (uncached) ────────────────────────────────────── */
EFI_STATUS oo_mmu_map_fb(OoMmuCtx *ctx, UINT64 fb_phys, UINT64 fb_size) {
    /* MMIO: write-combining, no cache (PWT+PCD), no-execute */
    UINT64 flags = OO_PTE_W | OO_PTE_PWT | OO_PTE_PCD | OO_PTE_NX;
    return oo_mmu_map(ctx, OO_FB_VIRT, fb_phys, fb_size, flags);
}

/* ── Build full OO kernel map ───────────────────────────────────────────── */
EFI_STATUS oo_mmu_build(OoMmuCtx *ctx, const OoBootState *bs) {
    if (!ctx || !bs) return EFI_INVALID_PARAMETER;
    Print(L"[mmu] Building page tables...\r\n");

    /* 1. Identity map first 16MB (kernel image + legacy area) */
    oo_mmu_identity_map(ctx, 16 * 1024 * 1024);

    /* 2. Map all available RAM at OO_KBASE (higher half) using huge pages */
    for (UINTN i = 0; i < bs->n_regions; i++) {
        const OoMemRegion *r = &bs->regions[i];
        if (r->type == OO_MEM_AVAILABLE || r->type == OO_MEM_OO_HEAP ||
            r->type == OO_MEM_OO_MODEL  || r->type == OO_MEM_OO_STACK) {
            UINT64 phys = r->phys_start;
            UINT64 size = r->num_pages * 4096;
            oo_mmu_map_huge(ctx, OO_KBASE + phys, phys, size, OO_PTE_W | OO_PTE_NX);
        }
    }

    /* 3. Map framebuffer */
    if (bs->fb_base && bs->fb_width && bs->fb_height) {
        UINT64 fb_sz = (UINT64)bs->fb_stride * bs->fb_height * 4;
        fb_sz = (fb_sz + 0xFFF) & ~0xFFFULL;
        oo_mmu_map_fb(ctx, bs->fb_base, fb_sz);
        Print(L"[mmu] FB mapped: 0x%lx → virt 0x%lx (%lu KB)\r\n",
              bs->fb_base, OO_FB_VIRT, fb_sz / 1024);
    }

    /* 4. Map LAPIC MMIO (0xFEE00000) at OO_LAPIC_VIRT */
    oo_mmu_map(ctx, OO_LAPIC_VIRT, 0xFEE00000ULL, 4096,
               OO_PTE_W | OO_PTE_PWT | OO_PTE_PCD | OO_PTE_NX);

    Print(L"[mmu] Map complete: %lu 4K + %lu 2M pages\r\n",
          ctx->pages_mapped, ctx->huge_pages_mapped);
    return EFI_SUCCESS;
}

/* ── Activate: load CR3 ─────────────────────────────────────────────────── */
void oo_mmu_activate(OoMmuCtx *ctx) {
    if (!ctx || !ctx->pml4) return;
    UINT64 cr3 = (UINT64)(UINTN)ctx->pml4;
    __asm__ volatile(
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%rax\n\t"
        "or  $0x80000000, %%eax\n\t"   /* Set PG bit */
        "mov %%rax, %%cr0\n\t"
        :: "r"(cr3) : "rax", "memory"
    );
    Print(L"[mmu] Paging ACTIVATED — CR3=0x%lx\r\n", cr3);
}

/* ── Walk page tables: virtual → physical ───────────────────────────────── */
UINT64 oo_mmu_v2p(const OoMmuCtx *ctx, UINT64 virt) {
    if (!ctx || !ctx->pml4) return (UINT64)-1;
    OoPte pml4e = ctx->pml4[PML4_IDX(virt)];
    if (!(pml4e & OO_PTE_P)) return (UINT64)-1;
    OoPte *pdpt = (OoPte*)(UINTN)(pml4e & OO_PAGE_MASK);
    OoPte pdpte = pdpt[PDPT_IDX(virt)];
    if (!(pdpte & OO_PTE_P)) return (UINT64)-1;
    /* 1GB page? */
    if (pdpte & OO_PTE_HUGE)
        return (pdpte & ~((1ULL<<30)-1)) | (virt & ((1ULL<<30)-1));
    OoPte *pd = (OoPte*)(UINTN)(pdpte & OO_PAGE_MASK);
    OoPte pde = pd[PD_IDX(virt)];
    if (!(pde & OO_PTE_P)) return (UINT64)-1;
    /* 2MB page? */
    if (pde & OO_PTE_HUGE)
        return (pde & ~(OO_HUGE_SIZE-1)) | (virt & (OO_HUGE_SIZE-1));
    OoPte *pt = (OoPte*)(UINTN)(pde & OO_PAGE_MASK);
    OoPte pte = pt[PT_IDX(virt)];
    if (!(pte & OO_PTE_P)) return (UINT64)-1;
    return (pte & OO_PAGE_MASK) | (virt & 0xFFF);
}

/* ── Print ───────────────────────────────────────────────────────────────── */
void oo_mmu_print(const OoMmuCtx *ctx) {
    Print(L"\r\n  [MMU Status]\r\n");
    Print(L"  Initialized : %s\r\n", ctx->initialized ? L"YES" : L"NO");
    Print(L"  PML4 base   : 0x%lx\r\n", (UINT64)(UINTN)ctx->pml4);
    Print(L"  PT pool     : %u/%u pages used\r\n",
          ctx->pt_pool_used, ctx->pt_pool_total);
    Print(L"  4K pages    : %lu\r\n", ctx->pages_mapped);
    Print(L"  2M pages    : %lu\r\n", ctx->huge_pages_mapped);
    Print(L"  Kernel base : 0x%lx\r\n", OO_KBASE);
    Print(L"  FB virt     : 0x%lx\r\n", OO_FB_VIRT);
    Print(L"  LAPIC virt  : 0x%lx\r\n\r\n", OO_LAPIC_VIRT);
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
static int _mmu_cmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}

int oo_mmu_repl_cmd(OoMmuCtx *ctx, const OoBootState *bs, const char *cmd) {
    if (!cmd) return 0;

    if (_mmu_cmp(cmd, "/mmu_status", 11) == 0) {
        oo_mmu_print(ctx); return 1;
    }
    if (_mmu_cmp(cmd, "/mmu_init", 9) == 0) {
        EFI_STATUS st = oo_mmu_init(ctx);
        Print(L"[mmu] Init: %r\r\n", st); return 1;
    }
    if (_mmu_cmp(cmd, "/mmu_build", 10) == 0) {
        if (!ctx->initialized) oo_mmu_init(ctx);
        EFI_STATUS st = oo_mmu_build(ctx, bs);
        Print(L"[mmu] Build: %r\r\n", st); return 1;
    }
    if (_mmu_cmp(cmd, "/mmu_activate", 13) == 0) {
        if (!ctx->pml4) {
            Print(L"[mmu] Run /mmu_build first\r\n"); return 1;
        }
        Print(L"[mmu] ⚠ Activating paging — UEFI calls will break after this\r\n");
        oo_mmu_activate(ctx); return 1;
    }
    if (_mmu_cmp(cmd, "/mmu_v2p ", 9) == 0) {
        UINT64 va = 0;
        const char *p = cmd + 9;
        if (p[0]=='0' && p[1]=='x') p += 2;
        while (*p) {
            UINT8 nibble = 0;
            if (*p >= '0' && *p <= '9') nibble = *p - '0';
            else if (*p >= 'a' && *p <= 'f') nibble = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') nibble = *p - 'A' + 10;
            else break;
            va = (va << 4) | nibble;
            p++;
        }
        UINT64 pa = oo_mmu_v2p(ctx, va);
        Print(L"[mmu] 0x%lx → 0x%lx\r\n", va, pa); return 1;
    }
    return 0;
}
