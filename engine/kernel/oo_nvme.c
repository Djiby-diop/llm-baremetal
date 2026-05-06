/* oo_nvme.c — Bare-metal NVMe PCI driver  Phase 5C
 * ===================================================
 * PCIe config space scan + NVMe controller init + read/write.
 * Uses UEFI PCI I/O or direct MMIO (post-EBS).
 * Freestanding C11. No libc.
 */
#include "oo_nvme.h"
#include <efi.h>
#include <efilib.h>

OoNvmeCtx g_nvme;

/* ── MMIO helpers ────────────────────────────────────────────────────────── */
static inline UINT32 _nvme_readl(UINT64 base, UINT32 off){
    return *(volatile UINT32*)(base + off);
}
static inline void _nvme_writel(UINT64 base, UINT32 off, UINT32 val){
    *(volatile UINT32*)(base + off) = val;
}
static inline UINT64 _nvme_readq(UINT64 base, UINT32 off){
    return *(volatile UINT64*)(base + off);
}
static inline void _nvme_writeq(UINT64 base, UINT32 off, UINT64 val){
    *(volatile UINT64*)(base + off) = val;
}

/* Doorbell offset: base + 0x1000 + (2 * qid + type) * (4 << dstrd) */
static inline UINT32 _doorbell_off(UINT32 qid, int is_cq, UINT32 dstrd){
    return 0x1000 + ((2 * qid + (UINT32)is_cq) * (4u << dstrd));
}

static void _memset0(void *p, UINTN n){
    for(UINTN i=0;i<n;i++) ((UINT8*)p)[i]=0;
}
static int _cmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}

/* ── PCI config space (MMIO via ECAM at 0xE0000000 or UEFI PciRootBridge) ── */
/* For pre-EBS: use UEFI PCI I/O Protocol */
/* For post-EBS: direct ECAM (0xE0000000 + bus<<20 + dev<<15 + fn<<12 + reg) */

#define PCI_ECAM_BASE        0xE0000000ULL  /* standard QEMU ECAM base */
#define PCI_CLASS_NVME       0x010802       /* storage / NVMe */
#define PCI_VENDOR_OFFSET    0x00
#define PCI_CLASS_OFFSET     0x08
#define PCI_BAR0_OFFSET      0x10
#define PCI_CMD_OFFSET       0x04
#define PCI_CMD_MEM_EN       0x0002
#define PCI_CMD_BUS_MASTER   0x0004

static UINT32 _pci_read32(UINT32 bus, UINT32 dev, UINT32 fn, UINT32 off) {
    UINT64 addr = PCI_ECAM_BASE |
                  ((UINT64)bus << 20) | ((UINT64)dev << 15) |
                  ((UINT64)fn  << 12) | (off & 0xFFC);
    return *(volatile UINT32*)addr;
}

static void _pci_write32(UINT32 bus, UINT32 dev, UINT32 fn,
                          UINT32 off, UINT32 val) {
    UINT64 addr = PCI_ECAM_BASE |
                  ((UINT64)bus << 20) | ((UINT64)dev << 15) |
                  ((UINT64)fn  << 12) | (off & 0xFFC);
    *(volatile UINT32*)addr = val;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void oo_nvme_init(OoNvmeCtx *ctx) {
    _memset0(ctx, sizeof(*ctx));
    Print(L"[nvme] NVMe driver initialized\r\n");
}

/* ── Scan PCI for NVMe controllers ─────────────────────────────────────── */
/*
 * Phase 5C: Two paths:
 *   1. UEFI PCI I/O Protocol (pre-EBS, safe, recommended for testing)
 *   2. Direct ECAM MMIO scan (post-EBS)
 * This implementation uses UEFI PCI I/O Protocol for compatibility.
 */
EFI_STATUS oo_nvme_scan_pci(OoNvmeCtx *ctx) {
    if (!ctx) return EFI_INVALID_PARAMETER;
    Print(L"[nvme] Scanning PCI for NVMe controllers...\r\n");

    /* Use UEFI PciIo protocol */
    EFI_GUID pciio_guid = EFI_PCI_IO_PROTOCOL_GUID;
    UINTN n_handles = 0;
    EFI_HANDLE *handles = NULL;

    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &pciio_guid, NULL, &n_handles, &handles);
    if (EFI_ERROR(st)) {
        Print(L"[nvme] LocateHandleBuffer failed: %r\r\n", st);
        return st;
    }

    Print(L"[nvme] PCI devices: %u\r\n", (UINT32)n_handles);

    for (UINTN i = 0; i < n_handles && ctx->n_drives < OO_NVME_MAX_DRIVES; i++) {
        EFI_PCI_IO_PROTOCOL *pciio = NULL;
        st = uefi_call_wrapper(BS->HandleProtocol, 3,
             handles[i], &pciio_guid, (void**)&pciio);
        if (EFI_ERROR(st) || !pciio) continue;

        /* Read PCI class code (offset 0x09-0x0B) */
        UINT8 cls[4] = {0};
        st = uefi_call_wrapper(pciio->Pci.Read, 5,
             pciio, EfiPciIoWidthUint8, 0x09, 3, cls);
        if (EFI_ERROR(st)) continue;

        UINT32 class_code = ((UINT32)cls[2] << 16) |
                             ((UINT32)cls[1] << 8)  | cls[0];
        /* NVMe = class 0x01, subclass 0x08, prog_if 0x02 */
        if (cls[2] != 0x01 || cls[1] != 0x08 || cls[0] != 0x02) continue;
        (void)class_code;

        /* Found NVMe — read BAR0 */
        UINT64 bar0 = 0;
        st = uefi_call_wrapper(pciio->Pci.Read, 5,
             pciio, EfiPciIoWidthUint32, PCI_BAR0_OFFSET, 1, &bar0);
        if (EFI_ERROR(st)) continue;
        bar0 &= ~0xFULL; /* clear flags */

        /* Enable memory space + bus mastering */
        UINT16 cmd = 0;
        uefi_call_wrapper(pciio->Pci.Read, 5,
             pciio, EfiPciIoWidthUint16, PCI_CMD_OFFSET, 1, &cmd);
        cmd |= PCI_CMD_MEM_EN | PCI_CMD_BUS_MASTER;
        uefi_call_wrapper(pciio->Pci.Write, 5,
             pciio, EfiPciIoWidthUint16, PCI_CMD_OFFSET, 1, &cmd);

        /* Read PCI segment/bus/dev/fn */
        UINTN seg, bus, dev, fn;
        uefi_call_wrapper(pciio->GetLocation, 5, pciio, &seg, &bus, &dev, &fn);

        OoNvmeDrive *d = &ctx->drives[ctx->n_drives];
        _memset0(d, sizeof(*d));
        d->present  = 1;
        d->bar0     = bar0;
        d->pci_bus  = (UINT32)bus;
        d->pci_dev  = (UINT32)dev;
        d->pci_func = (UINT32)fn;
        d->lba_size = 512;

        Print(L"[nvme] Found NVMe: pci %02u:%02u.%u BAR0=0x%lx\r\n",
              (UINT32)bus, (UINT32)dev, (UINT32)fn, bar0);
        ctx->n_drives++;
    }

    if (handles) uefi_call_wrapper(BS->FreePool, 1, handles);
    ctx->initialized = 1;
    Print(L"[nvme] Found %d NVMe drive(s)\r\n", ctx->n_drives);
    return ctx->n_drives > 0 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/* ── Setup drive: allocate queues, configure controller ─────────────────── */
EFI_STATUS oo_nvme_setup_drive(OoNvmeDrive *d) {
    if (!d || !d->present || !d->bar0) return EFI_INVALID_PARAMETER;

    UINT64 base = d->bar0;

    /* Reset: clear CC.EN */
    _nvme_writel(base, NVME_REG_CC, 0);
    /* Wait for CSTS.RDY=0 */
    UINTN timeout = 500000;
    while ((_nvme_readl(base, NVME_REG_CSTS) & NVME_CSTS_RDY) && timeout--)
        ;

    /* Read CAP for doorbell stride and timeout */
    UINT64 cap = _nvme_readq(base, NVME_REG_CAP);
    UINT32 dstrd = (UINT32)((cap >> 32) & 0xF);
    UINT32 mqes  = (UINT32)(cap & 0xFFFF) + 1; /* max queue entries */
    UINT32 qd    = mqes < OO_NVME_QUEUE_DEPTH ? mqes : OO_NVME_QUEUE_DEPTH;
    (void)qd;

    /* Allocate aligned queues (must be physically contiguous + 4K aligned) */
    UINTN sq_sz = OO_NVME_QUEUE_DEPTH * sizeof(OoNvmeSqe);
    UINTN cq_sz = OO_NVME_QUEUE_DEPTH * sizeof(OoNvmeCqe);

    EFI_PHYSICAL_ADDRESS phys;
    /* Admin SQ */
    uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
                      (sq_sz + 0xFFF) >> 12, &phys);
    d->asq = (OoNvmeSqe*)(UINTN)phys;
    _memset0(d->asq, sq_sz);
    /* Admin CQ */
    uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
                      (cq_sz + 0xFFF) >> 12, &phys);
    d->acq = (OoNvmeCqe*)(UINTN)phys;
    _memset0(d->acq, cq_sz);
    /* IO SQ */
    uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
                      (sq_sz + 0xFFF) >> 12, &phys);
    d->iosq = (OoNvmeSqe*)(UINTN)phys;
    _memset0(d->iosq, sq_sz);
    /* IO CQ */
    uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
                      (cq_sz + 0xFFF) >> 12, &phys);
    d->iocq = (OoNvmeCqe*)(UINTN)phys;
    _memset0(d->iocq, cq_sz);

    /* Set Admin Queue Attributes */
    _nvme_writel(base, NVME_REG_AQA,
                 ((OO_NVME_QUEUE_DEPTH - 1) << 16) |
                  (OO_NVME_QUEUE_DEPTH - 1));
    /* Set Admin queue base addresses */
    _nvme_writeq(base, NVME_REG_ASQ, (UINT64)(UINTN)d->asq);
    _nvme_writeq(base, NVME_REG_ACQ, (UINT64)(UINTN)d->acq);

    /* Enable controller */
    UINT32 cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS |
                NVME_CC_AMS | NVME_CC_IOSQES | NVME_CC_IOCQES;
    _nvme_writel(base, NVME_REG_CC, cc);

    /* Wait for CSTS.RDY=1 */
    timeout = 2000000;
    while (!(_nvme_readl(base, NVME_REG_CSTS) & NVME_CSTS_RDY) && timeout--)
        ;
    if (!timeout) {
        Print(L"[nvme] Controller enable timeout\r\n");
        return EFI_TIMEOUT;
    }
    if (_nvme_readl(base, NVME_REG_CSTS) & NVME_CSTS_CFS) {
        Print(L"[nvme] Controller Fatal Status\r\n");
        return EFI_DEVICE_ERROR;
    }

    d->acq_phase = 1;
    d->iocq_phase = 1;

    /* Identify controller */
    /* Allocate 4K identify buffer */
    uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
                      1, &phys);
    UINT8 *id_buf = (UINT8*)(UINTN)phys;
    _memset0(id_buf, 4096);

    /* Submit Identify Controller (Opcode 0x06, CNS=0x01) */
    OoNvmeSqe *sqe = &d->asq[d->asq_tail & (OO_NVME_QUEUE_DEPTH-1)];
    _memset0(sqe, sizeof(*sqe));
    sqe->opc   = 0x06;  /* Identify */
    sqe->cid   = d->next_cid++;
    sqe->nsid  = 0;
    sqe->prp1  = (UINT64)(UINTN)id_buf;
    sqe->cdw10 = 0x01;  /* CNS = Controller */

    /* Ring Admin SQ doorbell */
    d->asq_tail = (d->asq_tail + 1) & (OO_NVME_QUEUE_DEPTH - 1);
    _nvme_writel(base, _doorbell_off(0, 0, dstrd), d->asq_tail);

    /* Poll Admin CQ */
    timeout = 1000000;
    OoNvmeCqe *cqe = &d->acq[d->acq_head & (OO_NVME_QUEUE_DEPTH-1)];
    while (((cqe->status & 1) == d->acq_phase) == 0 && timeout--)
        ;

    UINT16 status_field = (cqe->status >> 1) & 0x7FFF;
    d->acq_head = (d->acq_head + 1) & (OO_NVME_QUEUE_DEPTH - 1);
    if (d->acq_head == 0) d->acq_phase ^= 1;
    _nvme_writel(base, _doorbell_off(0, 1, dstrd), d->acq_head);

    if (status_field != 0) {
        Print(L"[nvme] Identify failed status=0x%x\r\n", status_field);
    } else {
        /* Parse identify data */
        /* Model number at offset 24, 40 bytes */
        for (int i = 0; i < 40; i++) {
            CHAR8 c = id_buf[24 + i];
            d->id.model_number[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
        }
        d->id.model_number[39] = 0;
        /* Serial number at offset 4, 20 bytes */
        for (int i = 0; i < 20; i++) {
            CHAR8 c = id_buf[4 + i];
            d->id.serial[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
        }
        d->id.serial[19] = 0;
        /* MDTS at offset 77 */
        d->id.max_data_transfer = (1u << id_buf[77]) * 4096;
        /* Vendor ID at offset 0 */
        d->id.vendor_id = (UINT16)(id_buf[0] | ((UINT16)id_buf[1] << 8));
        Print(L"[nvme] Model: %a\r\n", d->id.model_number);
    }

    uefi_call_wrapper(BS->FreePages, 2, (EFI_PHYSICAL_ADDRESS)(UINTN)id_buf, 1);
    Print(L"[nvme] Drive setup complete\r\n");
    return EFI_SUCCESS;
}

/* ── Read sectors ────────────────────────────────────────────────────────── */
EFI_STATUS oo_nvme_read(OoNvmeDrive *d, UINT64 lba, UINT32 n_sectors,
                         void *buf) {
    if (!d || !d->present || !buf) return EFI_INVALID_PARAMETER;
    if (!d->iosq) return EFI_NOT_READY;  /* setup not done yet */

    UINT64 base = d->bar0;
    /* Read CAP for dstrd */
    UINT32 dstrd = (UINT32)((_nvme_readq(base, NVME_REG_CAP) >> 32) & 0xF);

    OoNvmeSqe *sqe = &d->iosq[d->iosq_tail & (OO_NVME_QUEUE_DEPTH-1)];
    _memset0(sqe, sizeof(*sqe));
    sqe->opc   = 0x02;  /* Read */
    sqe->nsid  = 1;     /* Namespace 1 */
    sqe->cid   = d->next_cid++;
    sqe->prp1  = (UINT64)(UINTN)buf;
    /* PRP2: if crossing page boundary */
    UINTN end = (UINTN)buf + (UINTN)n_sectors * 512;
    if ((end & ~0xFFFULL) != ((UINTN)buf & ~0xFFFULL))
        sqe->prp2 = ((UINTN)buf + 0x1000) & ~0xFFFULL;
    sqe->cdw10 = (UINT32)(lba & 0xFFFFFFFF);
    sqe->cdw11 = (UINT32)(lba >> 32);
    sqe->cdw12 = n_sectors - 1;  /* 0-based count */

    d->iosq_tail = (d->iosq_tail + 1) & (OO_NVME_QUEUE_DEPTH - 1);
    _nvme_writel(base, _doorbell_off(1, 0, dstrd), d->iosq_tail);

    /* Poll I/O CQ */
    UINTN timeout = 2000000;
    OoNvmeCqe *cqe = &d->iocq[d->iocq_head & (OO_NVME_QUEUE_DEPTH-1)];
    while (((cqe->status & 1) == d->iocq_phase) == 0 && timeout--)
        ;

    UINT16 status_field = (cqe->status >> 1) & 0x7FFF;
    d->iocq_head = (d->iocq_head + 1) & (OO_NVME_QUEUE_DEPTH - 1);
    if (d->iocq_head == 0) d->iocq_phase ^= 1;
    _nvme_writel(base, _doorbell_off(1, 1, dstrd), d->iocq_head);

    return (status_field == 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

/* ── Write sectors ───────────────────────────────────────────────────────── */
EFI_STATUS oo_nvme_write(OoNvmeDrive *d, UINT64 lba, UINT32 n_sectors,
                          const void *buf) {
    if (!d || !d->present || !buf) return EFI_INVALID_PARAMETER;
    if (!d->iosq) return EFI_NOT_READY;

    UINT64 base = d->bar0;
    UINT32 dstrd = (UINT32)((_nvme_readq(base, NVME_REG_CAP) >> 32) & 0xF);

    OoNvmeSqe *sqe = &d->iosq[d->iosq_tail & (OO_NVME_QUEUE_DEPTH-1)];
    _memset0(sqe, sizeof(*sqe));
    sqe->opc   = 0x01;  /* Write */
    sqe->nsid  = 1;
    sqe->cid   = d->next_cid++;
    sqe->prp1  = (UINT64)(UINTN)buf;
    UINTN end = (UINTN)buf + (UINTN)n_sectors * 512;
    if ((end & ~0xFFFULL) != ((UINTN)buf & ~0xFFFULL))
        sqe->prp2 = ((UINTN)buf + 0x1000) & ~0xFFFULL;
    sqe->cdw10 = (UINT32)(lba & 0xFFFFFFFF);
    sqe->cdw11 = (UINT32)(lba >> 32);
    sqe->cdw12 = n_sectors - 1;

    d->iosq_tail = (d->iosq_tail + 1) & (OO_NVME_QUEUE_DEPTH - 1);
    _nvme_writel(base, _doorbell_off(1, 0, dstrd), d->iosq_tail);

    UINTN timeout = 2000000;
    OoNvmeCqe *cqe = &d->iocq[d->iocq_head & (OO_NVME_QUEUE_DEPTH-1)];
    while (((cqe->status & 1) == d->iocq_phase) == 0 && timeout--)
        ;

    UINT16 status_field = (cqe->status >> 1) & 0x7FFF;
    d->iocq_head = (d->iocq_head + 1) & (OO_NVME_QUEUE_DEPTH - 1);
    if (d->iocq_head == 0) d->iocq_phase ^= 1;
    _nvme_writel(base, _doorbell_off(1, 1, dstrd), d->iocq_head);

    return (status_field == 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

/* ── Print info ──────────────────────────────────────────────────────────── */
void oo_nvme_print_info(const OoNvmeCtx *ctx) {
    Print(L"\r\n  [NVMe Drives] n=%d\r\n", ctx->n_drives);
    for (int i = 0; i < ctx->n_drives; i++) {
        const OoNvmeDrive *d = &ctx->drives[i];
        Print(L"  [%d] %a | BAR0=0x%lx | pci=%02u:%02u.%u\r\n",
              i, d->id.model_number[0] ? d->id.model_number : (CHAR8*)"?",
              d->bar0, d->pci_bus, d->pci_dev, d->pci_func);
    }
    Print(L"\r\n");
}

/* ── REPL ────────────────────────────────────────────────────────────────── */
int oo_nvme_repl_cmd(OoNvmeCtx *ctx, const char *cmd) {
    if (!cmd) return 0;

    if (_cmp(cmd, "/nvme_scan", 10) == 0) {
        oo_nvme_scan_pci(ctx); return 1;
    }
    if (_cmp(cmd, "/nvme_info", 10) == 0) {
        oo_nvme_print_info(ctx); return 1;
    }
    if (_cmp(cmd, "/nvme_setup", 11) == 0) {
        if (ctx->n_drives == 0) {
            Print(L"[nvme] No drives — run /nvme_scan first\r\n"); return 1;
        }
        oo_nvme_setup_drive(&ctx->drives[0]);
        return 1;
    }
    if (_cmp(cmd, "/nvme_read ", 11) == 0) {
        UINT64 lba = 0;
        const char *p = cmd + 11;
        while (*p >= '0' && *p <= '9') lba = lba*10 + (*p++ - '0');
        static UINT8 rbuf[512];
        EFI_STATUS st = oo_nvme_read(&ctx->drives[0], lba, 1, rbuf);
        if (!EFI_ERROR(st)) {
            Print(L"[nvme] LBA %lu: %02x %02x %02x %02x ...\r\n",
                  lba, rbuf[0], rbuf[1], rbuf[2], rbuf[3]);
        } else {
            Print(L"[nvme] Read failed: %r\r\n", st);
        }
        return 1;
    }
    return 0;
}
