/* oo_exit_boot.c — ExitBootServices + IDT + GDT  Phase 5A
 * ==========================================================
 * Takes full control of the hardware from UEFI.
 * After oo_ebs_call(): OO is the operating system.
 * Freestanding C11. No libc.
 */
#include "oo_exit_boot.h"
#include <efi.h>
#include <efilib.h>

/* Global boot state */
OoBootState g_oo_boot;

/* Static GDT and IDT — aligned to 16 bytes */
static OoGdtEntry _gdt[OO_GDT_ENTRIES] __attribute__((aligned(16)));
static OoGdtPtr   _gdt_ptr             __attribute__((aligned(16)));
static OoIdtEntry _idt[256]            __attribute__((aligned(16)));
static OoIdtPtr   _idt_ptr             __attribute__((aligned(16)));

/* ── EFI memory type → OO type mapping ─────────────────────────────────── */
static OoMemType _efi_to_oo_mem(UINT32 efi_type) {
    switch (efi_type) {
    case EfiConventionalMemory:     return OO_MEM_AVAILABLE;
    case EfiACPIReclaimMemory:      return OO_MEM_ACPI_DATA;
    case EfiACPIMemoryNVS:          return OO_MEM_ACPI_NVS;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:return OO_MEM_MMIO;
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:    return OO_MEM_EFI_RUNTIME;
    default:                        return OO_MEM_RESERVED;
    }
}

/* ── Step 1: Prepare — snapshot everything before EBS ──────────────────── */
EFI_STATUS oo_ebs_prepare(OoBootState *bs, EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *ST) {
    (void)ImageHandle;
    if (!bs || !ST) return EFI_INVALID_PARAMETER;

    /* Snapshot GOP framebuffer */
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    if (!EFI_ERROR(uefi_call_wrapper(ST->BootServices->LocateProtocol, 3,
                                      &gop_guid, NULL, (void**)&gop)) && gop) {
        bs->fb_base    = gop->Mode->FrameBufferBase;
        bs->fb_width   = gop->Mode->Info->HorizontalResolution;
        bs->fb_height  = gop->Mode->Info->VerticalResolution;
        bs->fb_stride  = gop->Mode->Info->PixelsPerScanLine;
        bs->fb_pixel_fmt = (UINT32)gop->Mode->Info->PixelFormat;
        Print(L"[ebs] GOP: %ux%u fb=0x%lx\r\n",
              bs->fb_width, bs->fb_height, bs->fb_base);
    }

    /* Find ACPI RSDP via EFI config table */
    EFI_GUID acpi2_guid = ACPI_20_TABLE_GUID;
    EFI_GUID acpi1_guid = ACPI_TABLE_GUID;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_GUID *g = &ST->ConfigurationTable[i].VendorGuid;
        if (CompareMem(g, &acpi2_guid, sizeof(EFI_GUID)) == 0 ||
            CompareMem(g, &acpi1_guid, sizeof(EFI_GUID)) == 0) {
            bs->rsdp_addr = (UINT64)(UINTN)ST->ConfigurationTable[i].VendorTable;
            Print(L"[ebs] RSDP: 0x%lx\r\n", bs->rsdp_addr);
            break;
        }
    }

    /* Snapshot memory map — this is needed for ExitBootServices key */
    UINTN mmap_sz = 0, mmap_key = 0, desc_sz = 0;
    UINT32 desc_ver = 0;
    /* First call to get size */
    uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5,
                      &mmap_sz, NULL, &mmap_key, &desc_sz, &desc_ver);
    mmap_sz += 4 * desc_sz; /* safety margin */

    EFI_MEMORY_DESCRIPTOR *mmap_buf = NULL;
    uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
                      EfiLoaderData, mmap_sz, (void**)&mmap_buf);
    if (!mmap_buf) return EFI_OUT_OF_RESOURCES;

    EFI_STATUS st = uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5,
                                       &mmap_sz, mmap_buf, &mmap_key,
                                       &desc_sz, &desc_ver);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(ST->BootServices->FreePool, 1, mmap_buf);
        return st;
    }

    /* Convert EFI → OO memory map */
    UINTN n = 0;
    UINT64 total = 0, avail = 0;
    UINT8 *p = (UINT8*)mmap_buf;
    UINT8 *end = p + mmap_sz;
    while (p < end && n < OO_MMAP_MAX_ENTRIES) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)p;
        bs->regions[n].type       = _efi_to_oo_mem(d->Type);
        bs->regions[n].phys_start = d->PhysicalStart;
        bs->regions[n].num_pages  = d->NumberOfPages;
        bs->regions[n].attr       = d->Attribute;
        total += d->NumberOfPages * 4096;
        if (bs->regions[n].type == OO_MEM_AVAILABLE)
            avail += d->NumberOfPages * 4096;
        n++;
        p += desc_sz;
    }
    bs->n_regions          = n;
    bs->total_ram_bytes    = total;
    bs->available_ram_bytes= avail;

    uefi_call_wrapper(ST->BootServices->FreePool, 1, mmap_buf);

    Print(L"[ebs] Memory map: %u regions, total=%u MB, avail=%u MB\r\n",
          (UINT32)n,
          (UINT32)(total / (1024*1024)),
          (UINT32)(avail / (1024*1024)));
    Print(L"[ebs] Ready for ExitBootServices — use /ebs_go to proceed\r\n");
    return EFI_SUCCESS;
}

/* ── Step 2: ExitBootServices — POINT OF NO RETURN ─────────────────────── */
EFI_STATUS oo_ebs_call(OoBootState *bs, EFI_HANDLE ImageHandle,
                        EFI_SYSTEM_TABLE *ST) {
    if (!bs) return EFI_INVALID_PARAMETER;

    Print(L"[ebs] *** CALLING ExitBootServices — NO MORE UEFI AFTER THIS ***\r\n");

    /* We need a fresh memory map key right before the call */
    UINTN mmap_sz = 0, mmap_key = 0, desc_sz = 0;
    UINT32 desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *mmap_buf = NULL;
    UINTN buf_sz = 0;

    /* Loop until GetMemoryMap + ExitBootServices succeed in one shot */
    for (int attempt = 0; attempt < 3; attempt++) {
        mmap_sz = 0;
        uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5,
                          &mmap_sz, NULL, &mmap_key, &desc_sz, &desc_ver);
        buf_sz = mmap_sz + 8 * desc_sz;
        if (!mmap_buf) {
            uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
                              EfiLoaderData, buf_sz, (void**)&mmap_buf);
        }
        if (!mmap_buf) return EFI_OUT_OF_RESOURCES;

        EFI_STATUS st = uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5,
                                           &mmap_sz, mmap_buf, &mmap_key,
                                           &desc_sz, &desc_ver);
        if (EFI_ERROR(st)) continue;

        /* ━━━ ExitBootServices ━━━ */
        st = uefi_call_wrapper(ST->BootServices->ExitBootServices, 2,
                                ImageHandle, mmap_key);
        if (!EFI_ERROR(st)) {
            /* SUCCESS — no more Print(), no more uefi_call_wrapper for BS */
            bs->ebs_called = 1;
            /* BS pointer is now invalid — zero it for safety */
            ST->BootServices = NULL;
            return EFI_SUCCESS;
        }
        /* Key mismatch — retry with fresh map */
    }

    return EFI_ABORTED;
}

/* ── GDT setup ──────────────────────────────────────────────────────────── */
static void _gdt_entry(OoGdtEntry *e, UINT32 base, UINT32 limit,
                        UINT8 access, UINT8 gran) {
    e->base_low    = (UINT16)(base & 0xFFFF);
    e->base_mid    = (UINT8)((base >> 16) & 0xFF);
    e->base_high   = (UINT8)((base >> 24) & 0xFF);
    e->limit_low   = (UINT16)(limit & 0xFFFF);
    e->granularity = (UINT8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    e->access      = access;
}

static void _load_gdt(void) {
    /* Null descriptor */
    _gdt_entry(&_gdt[0], 0, 0, 0, 0);
    /* Kernel code: 64-bit, DPL0 */
    _gdt_entry(&_gdt[1], 0, 0xFFFFF, 0x9A, 0xAF);
    /* Kernel data: 64-bit, DPL0 */
    _gdt_entry(&_gdt[2], 0, 0xFFFFF, 0x92, 0xCF);
    /* User code: 64-bit, DPL3 */
    _gdt_entry(&_gdt[3], 0, 0xFFFFF, 0xFA, 0xAF);
    /* User data: 64-bit, DPL3 */
    _gdt_entry(&_gdt[4], 0, 0xFFFFF, 0xF2, 0xCF);

    _gdt_ptr.limit = (UINT16)(sizeof(_gdt) - 1);
    _gdt_ptr.base  = (UINT64)(UINTN)_gdt;

    /* lgdt + reload CS/SS/DS */
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq  1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "m"(_gdt_ptr),
          "i"((UINT64)OO_SEG_KCODE),
          "i"((UINT16)OO_SEG_KDATA)
        : "rax", "memory"
    );
}

/* ── IDT / Exception handlers ───────────────────────────────────────────── */

/* Generic exception stub — writes to framebuffer directly */
#define OO_PANIC_COLOR  0x00FF2020   /* red */
#define OO_PANIC_X      32
#define OO_PANIC_Y      32

static void _fb_putpixel(UINT64 fb, UINT32 x, UINT32 y,
                          UINT32 stride, UINT32 color) {
    UINT32 *pixel = (UINT32*)(fb + (y * stride + x) * 4);
    *pixel = color;
}

/* Draw a colored bar in top-left corner of screen on panic */
__attribute__((noreturn))
static void _oo_panic(UINT32 vector, UINT64 error_code) {
    (void)error_code;
    /* Draw RED bar directly to framebuffer — no Print() */
    if (g_oo_boot.fb_base) {
        for (UINT32 y = 0; y < 64; y++)
            for (UINT32 x = 0; x < g_oo_boot.fb_width && x < 800; x++)
                _fb_putpixel(g_oo_boot.fb_base, x, y,
                             g_oo_boot.fb_stride, OO_PANIC_COLOR);
        /* Draw vector number as colored blocks */
        for (UINT32 b = 0; b < 8; b++) {
            UINT32 color = (vector & (1u << b)) ? 0x00FFFF00 : 0x00003300;
            for (UINT32 y = 0; y < 32; y++)
                for (UINT32 x = 0; x < 32; x++)
                    _fb_putpixel(g_oo_boot.fb_base, OO_PANIC_X + b*40 + x,
                                 OO_PANIC_Y + y, g_oo_boot.fb_stride, color);
        }
    }
    /* Halt */
    __asm__ volatile("cli; hlt" ::: "memory");
    while(1) __asm__ volatile("hlt");
}

/* GCC rejects interrupt handlers that may touch SIMD registers. */
#define OO_ISR_ATTR __attribute__((interrupt, target("general-regs-only")))

/* Exception stubs — generated via macro to keep it compact */
#define DEFINE_EXC_NOERR(vec) \
OO_ISR_ATTR \
static void _exc_##vec(struct __attribute__((packed)){UINT64 ip,cs,fl,sp,ss;} *f) { \
    (void)f; _oo_panic(vec, 0); }

#define DEFINE_EXC_ERR(vec) \
OO_ISR_ATTR \
static void _exc_##vec(struct __attribute__((packed)){UINT64 ip,cs,fl,sp,ss;} *f, UINT64 err) { \
    (void)f; _oo_panic(vec, err); }

DEFINE_EXC_NOERR(0)   /* #DE */
DEFINE_EXC_NOERR(1)   /* #DB */
DEFINE_EXC_NOERR(2)   /* NMI */
DEFINE_EXC_NOERR(3)   /* #BP */
DEFINE_EXC_NOERR(4)   /* #OF */
DEFINE_EXC_NOERR(5)   /* #BR */
DEFINE_EXC_NOERR(6)   /* #UD */
DEFINE_EXC_NOERR(7)   /* #NM */
DEFINE_EXC_ERR(8)     /* #DF */
DEFINE_EXC_ERR(10)    /* #TS */
DEFINE_EXC_ERR(11)    /* #NP */
DEFINE_EXC_ERR(12)    /* #SS */
DEFINE_EXC_ERR(13)    /* #GP */
DEFINE_EXC_ERR(14)    /* #PF */
DEFINE_EXC_NOERR(16)  /* #MF */
DEFINE_EXC_ERR(17)    /* #AC */
DEFINE_EXC_NOERR(18)  /* #MC */
DEFINE_EXC_NOERR(19)  /* #XF */

/* LAPIC timer stub (will be replaced by scheduler in Phase 5E) */
OO_ISR_ATTR
static void _lapic_timer_stub(struct __attribute__((packed)){
    UINT64 ip,cs,fl,sp,ss;} *f) {
    (void)f;
    /* EOI to LAPIC (0xFEE000B0) */
    volatile UINT32 *lapic_eoi = (volatile UINT32*)0xFEE000B0ULL;
    *lapic_eoi = 0;
}

OO_ISR_ATTR
static void _spurious_stub(struct __attribute__((packed)){
    UINT64 ip,cs,fl,sp,ss;} *f) { (void)f; }

static void _idt_set_gate(int vec, void *handler, UINT8 type_attr) {
    UINT64 addr = (UINT64)(UINTN)handler;
    _idt[vec].offset_low  = (UINT16)(addr & 0xFFFF);
    _idt[vec].selector    = OO_SEG_KCODE;
    _idt[vec].ist         = 0;
    _idt[vec].type_attr   = type_attr;
    _idt[vec].offset_mid  = (UINT16)((addr >> 16) & 0xFFFF);
    _idt[vec].offset_high = (UINT32)((addr >> 32) & 0xFFFFFFFF);
    _idt[vec].reserved    = 0;
}

void oo_idt_install(void) {
#define GATE_INT  0x8E   /* present, ring0, 64-bit interrupt gate */
#define GATE_TRAP 0x8F   /* present, ring0, 64-bit trap gate */

    _idt_set_gate(0,  _exc_0,  GATE_TRAP);
    _idt_set_gate(1,  _exc_1,  GATE_TRAP);
    _idt_set_gate(2,  _exc_2,  GATE_INT);
    _idt_set_gate(3,  _exc_3,  GATE_TRAP);
    _idt_set_gate(4,  _exc_4,  GATE_TRAP);
    _idt_set_gate(5,  _exc_5,  GATE_TRAP);
    _idt_set_gate(6,  _exc_6,  GATE_TRAP);
    _idt_set_gate(7,  _exc_7,  GATE_TRAP);
    _idt_set_gate(8,  _exc_8,  GATE_INT);
    _idt_set_gate(10, _exc_10, GATE_INT);
    _idt_set_gate(11, _exc_11, GATE_INT);
    _idt_set_gate(12, _exc_12, GATE_INT);
    _idt_set_gate(13, _exc_13, GATE_INT);
    _idt_set_gate(14, _exc_14, GATE_INT);
    _idt_set_gate(16, _exc_16, GATE_TRAP);
    _idt_set_gate(17, _exc_17, GATE_INT);
    _idt_set_gate(18, _exc_18, GATE_INT);
    _idt_set_gate(19, _exc_19, GATE_TRAP);
    _idt_set_gate(OO_INT_LAPIC,   _lapic_timer_stub, GATE_INT);
    _idt_set_gate(OO_INT_SPURIOUS, _spurious_stub,   GATE_INT);

    _idt_ptr.limit = (UINT16)(sizeof(_idt) - 1);
    _idt_ptr.base  = (UINT64)(UINTN)_idt;
    __asm__ volatile("lidt %0" :: "m"(_idt_ptr) : "memory");
}

/* ── Enable NX (No-Execute) bit in EFER ────────────────────────────────── */
static void _enable_nx(void) {
    /* IA32_EFER MSR = 0xC0000080, NXE = bit 11 */
    UINT32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));
    lo |= (1u << 11);
    __asm__ volatile("wrmsr" :: "c"(0xC0000080U), "a"(lo), "d"(hi));
}

/* ── Step 3: Post-EBS initialization ────────────────────────────────────── */
void oo_ebs_post_init(OoBootState *bs) {
    /* Load our own GDT */
    _load_gdt();
    bs->gdt_loaded = 1;

    /* Install exception handlers */
    oo_idt_install();
    bs->idt_loaded = 1;

    /* Enable NX */
    _enable_nx();
    bs->nx_enabled = 1;

    /* Re-enable interrupts */
    __asm__ volatile("sti");
}

/* ── Find largest available heap region ────────────────────────────────── */
UINT64 oo_ebs_find_heap(const OoBootState *bs, UINT64 *out_size) {
    UINT64 best_start = 0, best_size = 0;
    for (UINTN i = 0; i < bs->n_regions; i++) {
        if (bs->regions[i].type == OO_MEM_AVAILABLE) {
            UINT64 sz = bs->regions[i].num_pages * 4096;
            /* Prefer regions above 1MB (avoid legacy area) */
            if (sz > best_size && bs->regions[i].phys_start >= 0x100000ULL) {
                best_size  = sz;
                best_start = bs->regions[i].phys_start;
            }
        }
    }
    if (out_size) *out_size = best_size;
    return best_start;
}

/* ── Print memory map ───────────────────────────────────────────────────── */
void oo_ebs_print_mmap(const OoBootState *bs) {
    static const CHAR16 *type_names[] = {
        L"RESERVED", L"AVAILABLE", L"ACPI-DATA", L"ACPI-NVS",
        L"MMIO", L"EFI-RT", L"OO-KERNEL", L"OO-HEAP", L"OO-MODEL", L"OO-STACK"
    };
    Print(L"\r\n  [OO Memory Map] %u regions\r\n", (UINT32)bs->n_regions);
    for (UINTN i = 0; i < bs->n_regions; i++) {
        const OoMemRegion *r = &bs->regions[i];
        UINT64 sz_mb = (r->num_pages * 4096) / (1024*1024);
        UINT32 type = (UINT32)r->type;
        const CHAR16 *tname = type < 10 ? type_names[type] : L"?";
        if (sz_mb > 0) {
            Print(L"  [%02u] 0x%010lx + %4lu MB  %s\r\n",
                  (UINT32)i, r->phys_start, sz_mb, tname);
        }
    }
    Print(L"  Total RAM: %u MB | Available: %u MB\r\n\r\n",
          (UINT32)(bs->total_ram_bytes/(1024*1024)),
          (UINT32)(bs->available_ram_bytes/(1024*1024)));
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
static int _cmp(const char *a, const char *b, int n) {
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;
}

int oo_ebs_repl_cmd(OoBootState *bs, const char *cmd,
                    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    if (!cmd) return 0;

    /* /ebs_status */
    if (_cmp(cmd, "/ebs_status", 11) == 0) {
        Print(L"\r\n  [ExitBootServices Status]\r\n");
        Print(L"  EBS called  : %s\r\n", bs->ebs_called ? L"YES" : L"NO");
        Print(L"  GDT loaded  : %s\r\n", bs->gdt_loaded ? L"YES" : L"NO");
        Print(L"  IDT loaded  : %s\r\n", bs->idt_loaded ? L"YES" : L"NO");
        Print(L"  NX enabled  : %s\r\n", bs->nx_enabled ? L"YES" : L"NO");
        Print(L"  FB base     : 0x%lx\r\n", bs->fb_base);
        Print(L"  RSDP        : 0x%lx\r\n", bs->rsdp_addr);
        Print(L"  RAM total   : %u MB\r\n",
              (UINT32)(bs->total_ram_bytes/(1024*1024)));
        Print(L"  RAM avail   : %u MB\r\n\r\n",
              (UINT32)(bs->available_ram_bytes/(1024*1024)));
        return 1;
    }
    /* /ebs_mmap */
    if (_cmp(cmd, "/ebs_mmap", 9) == 0) {
        oo_ebs_print_mmap(bs);
        return 1;
    }
    /* /ebs_prepare */
    if (_cmp(cmd, "/ebs_prepare", 12) == 0) {
        if (bs->ebs_called) {
            Print(L"[ebs] Already in post-EBS mode\r\n");
            return 1;
        }
        EFI_STATUS st = oo_ebs_prepare(bs, ImageHandle, ST);
        if (EFI_ERROR(st)) Print(L"[ebs] Prepare failed: %r\r\n", st);
        return 1;
    }
    /* /ebs_go — THE POINT OF NO RETURN */
    if (_cmp(cmd, "/ebs_go", 7) == 0) {
        if (bs->ebs_called) {
            Print(L"[ebs] Already done\r\n"); return 1;
        }
        Print(L"[ebs] Preparing memory map...\r\n");
        oo_ebs_prepare(bs, ImageHandle, ST);
        Print(L"[ebs] Calling ExitBootServices...\r\n");
        EFI_STATUS st = oo_ebs_call(bs, ImageHandle, ST);
        if (!EFI_ERROR(st)) {
            /* NO MORE Print() via UEFI here — direct framebuffer only */
            oo_ebs_post_init(bs);
            /* TODO: draw "OO KERNEL ACTIVE" to framebuffer */
        } else {
            Print(L"[ebs] FAILED: %r — system intact\r\n", st);
        }
        return 1;
    }
    /* /ebs_idt */
    if (_cmp(cmd, "/ebs_idt", 8) == 0) {
        oo_idt_install();
        bs->idt_loaded = 1;
        Print(L"[ebs] IDT installed (256 gates)\r\n");
        return 1;
    }
    return 0;
}
