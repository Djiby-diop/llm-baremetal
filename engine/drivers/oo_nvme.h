#ifndef OO_DRIVERS_NVME_H
#define OO_DRIVERS_NVME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OO NVMe 1.3 PCIe SSD Driver (Bare-Metal, Freestanding)
 * PCIe class 0x010802. Admin queue + I/O queue, polling completion.
 */

#define OO_NVME_BLOCK_SIZE  512
#define OO_NVME_NSID        1

/* NVMe controller register offsets (BAR0) */
#define NVME_REG_CAP        0x0000  /* Controller Capabilities (64-bit) */
#define NVME_REG_VS         0x0008  /* Version */
#define NVME_REG_CC         0x0014  /* Controller Configuration */
#define NVME_REG_CSTS       0x001C  /* Controller Status */
#define NVME_REG_AQA        0x0024  /* Admin Queue Attributes */
#define NVME_REG_ASQ        0x0028  /* Admin Submission Queue Base Address (64-bit) */
#define NVME_REG_ACQ        0x0030  /* Admin Completion Queue Base Address (64-bit) */

/* CC fields */
#define NVME_CC_EN          (1u << 0)   /* Enable */
#define NVME_CC_CSS_NVM     (0u << 4)   /* I/O Command Set: NVM */
#define NVME_CC_MPS_4K      (0u << 7)   /* Memory Page Size: 4 KiB */
#define NVME_CC_AMS_RR      (0u << 11)  /* Arbitration: round-robin */
#define NVME_CC_IOSQES      (6u << 16)  /* I/O SQ entry size: 64 B = 2^6 */
#define NVME_CC_IOCQES      (4u << 20)  /* I/O CQ entry size: 16 B = 2^4 */

/* CSTS fields */
#define NVME_CSTS_RDY       (1u << 0)
#define NVME_CSTS_CFS       (1u << 1)   /* Controller Fatal Status */

/* Queue depth for admin + I/O queues */
#define NVME_QUEUE_DEPTH    4

/* Admin opcodes */
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06

/* NVM opcodes */
#define NVME_NVM_WRITE          0x01
#define NVME_NVM_READ           0x02

/* 64-byte Submission Queue entry */
typedef struct {
    uint8_t  opcode;
    uint8_t  fuse_psdt;
    uint16_t cid;           /* command identifier */
    uint32_t nsid;
    uint64_t reserved0;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) OoNvmeSqEntry;

/* 16-byte Completion Queue entry */
typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;        /* bit 0 = phase, bits 1-15 = status code */
} __attribute__((packed)) OoNvmeCqEntry;

typedef struct {
    uint64_t  bar0;             /* controller registers BAR0 */
    uint64_t  capacity_lba;    /* total LBAs */
    int       initialized;
    uint32_t  pci_bus_dev_fn;
    uint64_t  sq_phys;         /* submission queue physical addr */
    uint64_t  cq_phys;         /* completion queue physical addr */
    uint16_t  sq_tail;
    uint16_t  cq_head;
    uint16_t  cq_phase;

    /* Static queue storage (aligned to 4 KiB by alignment attribute) */
    OoNvmeSqEntry sq[NVME_QUEUE_DEPTH] __attribute__((aligned(4096)));
    OoNvmeCqEntry cq[NVME_QUEUE_DEPTH] __attribute__((aligned(4096)));

    /* Data buffer for identify/transfers */
    uint8_t  data_buf[OO_NVME_BLOCK_SIZE * NVME_QUEUE_DEPTH] __attribute__((aligned(4096)));

    uint16_t  next_cid;
    uint32_t  db_stride;        /* doorbell stride in bytes */
} OoNvmeCtrl;

int  oo_nvme_init(OoNvmeCtrl *c, uint32_t bus_dev_fn, uint64_t bar0_addr);
int  oo_nvme_read_blocks(OoNvmeCtrl *c, uint64_t lba, uint32_t count, void *buf);
int  oo_nvme_write_blocks(OoNvmeCtrl *c, uint64_t lba, uint32_t count, const void *buf);
void oo_nvme_print_status(const OoNvmeCtrl *c, void (*print_fn)(const char *));

#ifdef __cplusplus
}
#endif

#endif /* OO_DRIVERS_NVME_H */
