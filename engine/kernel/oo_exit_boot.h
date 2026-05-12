/* oo_exit_boot.h — ExitBootServices + Full Hardware Control  Phase 5A
 * =====================================================================
 * After ExitBootServices():
 *   - OO owns ALL memory (no more UEFI runtime paging)
 *   - All interrupts must be handled by OO
 *   - No more UEFI protocol calls (except runtime services)
 *   - Physical CPU in control — real OS mode
 *
 * Sequence:
 *   1. oo_ebs_prepare()    — snapshot memory map, save ACPI/GOP state
 *   2. oo_ebs_call()       — call ExitBootServices(), point of no return
 *   3. oo_ebs_post_init()  — setup IDT, switch to OO GDT, enable NX
 *   4. oo_ebs_enable_mmu() — (Phase 5B) setup paging
 *
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include <efilib.h>

#define OO_MMAP_MAX_ENTRIES   256
#define OO_GDT_ENTRIES         8

#define OO_SEG_NULL    0x00
#define OO_SEG_KCODE   0x08
#define OO_SEG_KDATA   0x10
#define OO_SEG_UCODE   0x18
#define OO_SEG_UDATA   0x20
#define OO_SEG_TSS     0x28

#define OO_INT_DE      0
#define OO_INT_DB      1
#define OO_INT_NMI     2
#define OO_INT_BP      3
#define OO_INT_GP      13
#define OO_INT_PF      14
#define OO_INT_LAPIC   32
#define OO_INT_SPURIOUS 255

typedef enum {
    OO_MEM_RESERVED   = 0,
    OO_MEM_AVAILABLE  = 1,
    OO_MEM_ACPI_DATA  = 2,
    OO_MEM_ACPI_NVS   = 3,
    OO_MEM_MMIO       = 4,
    OO_MEM_EFI_RUNTIME= 5,
    OO_MEM_OO_KERNEL  = 6,
    OO_MEM_OO_HEAP    = 7,
    OO_MEM_OO_MODEL   = 8,
    OO_MEM_OO_STACK   = 9,
} OoMemType;

typedef struct {
    OoMemType  type;
    UINT64     phys_start;
    UINT64     num_pages;
    UINT64     attr;
} OoMemRegion;

typedef struct {
    OoMemRegion  regions[OO_MMAP_MAX_ENTRIES];
    UINTN        n_regions;
    UINT64       total_ram_bytes;
    UINT64       available_ram_bytes;
    /* GOP framebuffer — stays valid post-EBS */
    UINT64       fb_base;
    UINT32       fb_width;
    UINT32       fb_height;
    UINT32       fb_stride;
    UINT32       fb_pixel_fmt;
    /* ACPI RSDP */
    UINT64       rsdp_addr;
    /* State flags */
    int          ebs_called;
    int          gdt_loaded;
    int          idt_loaded;
    int          nx_enabled;
} OoBootState;

typedef struct __attribute__((packed)) {
    UINT16 limit_low;
    UINT16 base_low;
    UINT8  base_mid;
    UINT8  access;
    UINT8  granularity;
    UINT8  base_high;
} OoGdtEntry;

typedef struct __attribute__((packed)) {
    UINT16 limit;
    UINT64 base;
} OoGdtPtr;

typedef struct __attribute__((packed)) {
    UINT16 offset_low;
    UINT16 selector;
    UINT8  ist;
    UINT8  type_attr;
    UINT16 offset_mid;
    UINT32 offset_high;
    UINT32 reserved;
} OoIdtEntry;

typedef struct __attribute__((packed)) {
    UINT16 limit;
    UINT64 base;
} OoIdtPtr;

EFI_STATUS oo_ebs_prepare(OoBootState *bs, EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *ST);
EFI_STATUS oo_ebs_call(OoBootState *bs, EFI_HANDLE ImageHandle,
                        EFI_SYSTEM_TABLE *ST);
void       oo_ebs_post_init(OoBootState *bs);
void       oo_idt_install(void);
void       oo_idt_set_gate(int vec, UINT64 handler, UINT8 type_attr); /* Add/update one IDT entry */
void       oo_ebs_print_mmap(const OoBootState *bs);
UINT64     oo_ebs_find_heap(const OoBootState *bs, UINT64 *out_size);
int        oo_ebs_repl_cmd(OoBootState *bs, const char *cmd,
                            EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);

extern OoBootState g_oo_boot;

/* Phase 7A: LAPIC timer tick counter — incremented every 10ms by ISR */
extern volatile uint64_t g_lapic_tick_count;
