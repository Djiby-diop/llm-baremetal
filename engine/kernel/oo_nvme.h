/* oo_nvme.h — Bare-metal NVMe PCI driver  Phase 5C
 * ===================================================
 * Direct NVMe over PCIe — no UEFI BlockIO needed after EBS.
 * Enables persistent storage for:
 *   - Model weight caching (self-expansion)
 *   - Journal persistence
 *   - Patch history
 *   - OO filesystem (OO-FS)
 *
 * Protocol: NVMe 1.4 Base Spec
 * Transport: PCIe BAR0 MMIO
 *
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include <efilib.h>

/* NVMe constants */
#define OO_NVME_MAX_DRIVES    4
#define OO_NVME_SECTOR_SIZE   512
#define OO_NVME_QUEUE_DEPTH   64
#define OO_NVME_MAX_NAMESPACES 8

/* NVMe BAR0 register offsets */
#define NVME_REG_CAP    0x00    /* Controller Capabilities (64-bit) */
#define NVME_REG_VS     0x08    /* Version */
#define NVME_REG_INTMS  0x0C    /* Interrupt Mask Set */
#define NVME_REG_INTMC  0x10    /* Interrupt Mask Clear */
#define NVME_REG_CC     0x14    /* Controller Configuration */
#define NVME_REG_CSTS   0x1C    /* Controller Status */
#define NVME_REG_AQA    0x24    /* Admin Queue Attributes */
#define NVME_REG_ASQ    0x28    /* Admin Submission Queue Base */
#define NVME_REG_ACQ    0x30    /* Admin Completion Queue Base */

/* CC fields */
#define NVME_CC_EN      (1u << 0)   /* Enable */
#define NVME_CC_CSS_NVM (0u << 4)   /* NVM command set */
#define NVME_CC_MPS     (0u << 7)   /* 4KB page size */
#define NVME_CC_AMS     (0u << 11)  /* Round Robin */
#define NVME_CC_IOSQES  (6u << 16)  /* 64B submission entry */
#define NVME_CC_IOCQES  (4u << 20)  /* 16B completion entry */

/* CSTS fields */
#define NVME_CSTS_RDY   (1u << 0)
#define NVME_CSTS_CFS   (1u << 1)   /* Controller Fatal Status */

/* NVMe Submission Queue Entry (64 bytes) */
typedef struct __attribute__((packed)) {
    UINT32 opc    : 8;   /* Opcode */
    UINT32 fuse   : 2;
    UINT32 rsvd   : 4;
    UINT32 psdt   : 2;
    UINT32 cid    : 16;  /* Command ID */
    UINT32 nsid;         /* Namespace ID */
    UINT64 rsvd2;
    UINT64 mptr;         /* Metadata Pointer */
    UINT64 prp1;         /* Physical Region Page 1 */
    UINT64 prp2;         /* Physical Region Page 2 */
    UINT32 cdw10;
    UINT32 cdw11;
    UINT32 cdw12;
    UINT32 cdw13;
    UINT32 cdw14;
    UINT32 cdw15;
} OoNvmeSqe;

/* NVMe Completion Queue Entry (16 bytes) */
typedef struct __attribute__((packed)) {
    UINT32 dw0;          /* Command-specific result */
    UINT32 dw1;
    UINT16 sq_head;      /* Submission queue head pointer */
    UINT16 sq_id;        /* Submission queue ID */
    UINT16 cid;          /* Command ID */
    UINT16 status;       /* Status field + phase bit */
} OoNvmeCqe;

/* Identify Controller structure (key fields only) */
typedef struct {
    CHAR8  model_number[40];
    CHAR8  serial[20];
    CHAR8  fw_rev[8];
    UINT64 total_capacity_bytes;
    UINT32 max_data_transfer;
    UINT16 vendor_id;
} OoNvmeIdCtrl;

/* Per-drive state */
typedef struct {
    int      present;
    UINT64   bar0;           /* MMIO base address */
    UINT32   pci_bus;
    UINT32   pci_dev;
    UINT32   pci_func;
    /* Queue pair (Admin: qid=0, IO: qid=1) */
    OoNvmeSqe *asq;          /* Admin Submission Queue */
    OoNvmeCqe *acq;          /* Admin Completion Queue */
    OoNvmeSqe *iosq;         /* I/O Submission Queue */
    OoNvmeCqe *iocq;         /* I/O Completion Queue */
    UINT16   asq_tail;
    UINT16   acq_head;
    UINT16   iosq_tail;
    UINT16   iocq_head;
    UINT8    acq_phase;
    UINT8    iocq_phase;
    UINT16   next_cid;
    OoNvmeIdCtrl id;
    UINT64   lba_count;
    UINT32   lba_size;
} OoNvmeDrive;

typedef struct {
    int          initialized;
    OoNvmeDrive  drives[OO_NVME_MAX_DRIVES];
    int          n_drives;
} OoNvmeCtx;

/* API */
void       oo_nvme_init(OoNvmeCtx *ctx);
EFI_STATUS oo_nvme_scan_pci(OoNvmeCtx *ctx);
EFI_STATUS oo_nvme_setup_drive(OoNvmeDrive *d);
EFI_STATUS oo_nvme_read(OoNvmeDrive *d, UINT64 lba, UINT32 n_sectors,
                         void *buf);
EFI_STATUS oo_nvme_write(OoNvmeDrive *d, UINT64 lba, UINT32 n_sectors,
                          const void *buf);
void       oo_nvme_print_info(const OoNvmeCtx *ctx);
int        oo_nvme_repl_cmd(OoNvmeCtx *ctx, const char *cmd);

extern OoNvmeCtx g_nvme;
