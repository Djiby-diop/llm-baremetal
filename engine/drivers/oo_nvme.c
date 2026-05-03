#include "oo_nvme.h"

/*
 * OO NVMe 1.3 PCIe SSD Driver — bare-metal implementation.
 * Admin queue setup, Identify controller, I/O read/write via polling.
 * No libc, no malloc — all queues in the OoNvmeCtrl struct.
 */

/* ── MMIO helpers ─────────────────────────────────────────────────── */

static inline uint32_t nvme_read32(uint64_t base, uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}

static inline uint64_t nvme_read64(uint64_t base, uint32_t off) {
    /* Read as two 32-bit accesses for portability on strict MMIO buses */
    uint64_t lo = *(volatile uint32_t *)(uintptr_t)(base + off);
    uint64_t hi = *(volatile uint32_t *)(uintptr_t)(base + off + 4);
    return lo | (hi << 32);
}

static inline void nvme_write32(uint64_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(base + off) = v;
}

static inline void nvme_write64(uint64_t base, uint32_t off, uint64_t v) {
    *(volatile uint32_t *)(uintptr_t)(base + off)     = (uint32_t)(v & 0xFFFFFFFF);
    *(volatile uint32_t *)(uintptr_t)(base + off + 4) = (uint32_t)(v >> 32);
}

/* ── Doorbell register address ───────────────────────────────────── */

/* SQ tail doorbell for queue qid */
static inline uint32_t nvme_sq_db_off(const OoNvmeCtrl *c, uint16_t qid) {
    return 0x1000 + (2 * qid) * c->db_stride;
}

/* CQ head doorbell for queue qid */
static inline uint32_t nvme_cq_db_off(const OoNvmeCtrl *c, uint16_t qid) {
    return 0x1000 + (2 * qid + 1) * c->db_stride;
}

/* ── Spin-delay ───────────────────────────────────────────────────── */

static void nvme_delay(uint32_t iters) {
    for (volatile uint32_t i = 0; i < iters; i++)
        __asm__ volatile ("pause");
}

/* ── Zero a memory region ────────────────────────────────────────── */

static void nvme_memzero(void *ptr, uint32_t len) {
    uint8_t *p = (uint8_t *)ptr;
    for (uint32_t i = 0; i < len; i++) p[i] = 0;
}

/* ── Submit one command and poll for completion ──────────────────── */

static int nvme_submit_and_poll(OoNvmeCtrl *c, OoNvmeSqEntry *cmd, uint32_t *dw0_out) {
    uint16_t cid = ++c->next_cid;
    cmd->cid = cid;

    /* Place command in SQ */
    c->sq[c->sq_tail] = *cmd;
    c->sq_tail = (c->sq_tail + 1) % NVME_QUEUE_DEPTH;

    /* Ring SQ tail doorbell (admin queue = 0) */
    nvme_write32(c->bar0, nvme_sq_db_off(c, 0), c->sq_tail);

    /* Poll CQ for completion */
    for (int i = 0; i < 1000000; i++) {
        OoNvmeCqEntry *cqe = &c->cq[c->cq_head];
        uint16_t status = cqe->status;
        /* Phase bit must match our expected phase */
        if ((status & 0x01) == c->cq_phase) {
            uint16_t sc = (status >> 1) & 0x7FF;
            uint16_t head = c->cq_head;

            c->cq_head = (c->cq_head + 1) % NVME_QUEUE_DEPTH;
            if (c->cq_head == 0) c->cq_phase ^= 1; /* phase toggle on wrap */

            /* Ring CQ head doorbell */
            nvme_write32(c->bar0, nvme_cq_db_off(c, 0), c->cq_head);

            if (dw0_out) *dw0_out = cqe->dw0;
            (void)head;
            return (sc == 0) ? 0 : (int)sc;
        }
        nvme_delay(10);
    }
    return -1; /* timeout */
}

/* ── Public API ──────────────────────────────────────────────────── */

int oo_nvme_init(OoNvmeCtrl *c, uint32_t bus_dev_fn, uint64_t bar0_addr) {
    nvme_memzero(c, sizeof(*c));

    c->bar0          = bar0_addr;
    c->pci_bus_dev_fn = bus_dev_fn;
    c->cq_phase      = 1; /* initial phase bit expected from controller = 1 */

    /* Read capabilities */
    uint64_t cap = nvme_read64(bar0_addr, NVME_REG_CAP);
    uint32_t to  = (uint32_t)((cap >> 24) & 0xFF); /* CSTS.RDY timeout in 500 ms units */
    if (to == 0) to = 10; /* safe fallback */

    /* Doorbell stride = 4 * 2^DSTRD, DSTRD = CAP[35:32] */
    uint32_t dstrd = (uint32_t)((cap >> 32) & 0x0F);
    c->db_stride   = 4u << dstrd;

    /* Disable controller */
    uint32_t cc = nvme_read32(bar0_addr, NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    nvme_write32(bar0_addr, NVME_REG_CC, cc);

    /* Wait for CSTS.RDY = 0 */
    uint32_t loops = to * 5000;
    for (uint32_t i = 0; i < loops; i++) {
        if (!(nvme_read32(bar0_addr, NVME_REG_CSTS) & NVME_CSTS_RDY)) break;
        nvme_delay(100);
    }

    /* Program Admin Queue Attributes: both depth = NVME_QUEUE_DEPTH */
    uint32_t aqa = ((NVME_QUEUE_DEPTH - 1) << 16) | (NVME_QUEUE_DEPTH - 1);
    nvme_write32(bar0_addr, NVME_REG_AQA, aqa);

    /* Program ASQ/ACQ base addresses (physical = virtual in identity-mapped bare-metal) */
    c->sq_phys = (uint64_t)(uintptr_t)c->sq;
    c->cq_phys = (uint64_t)(uintptr_t)c->cq;
    nvme_write64(bar0_addr, NVME_REG_ASQ, c->sq_phys);
    nvme_write64(bar0_addr, NVME_REG_ACQ, c->cq_phys);

    /* Enable controller */
    cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K | NVME_CC_AMS_RR |
         NVME_CC_IOSQES | NVME_CC_IOCQES;
    nvme_write32(bar0_addr, NVME_REG_CC, cc);

    /* Wait for CSTS.RDY = 1 */
    for (uint32_t i = 0; i < loops; i++) {
        uint32_t csts = nvme_read32(bar0_addr, NVME_REG_CSTS);
        if (csts & NVME_CSTS_CFS) return -2; /* fatal */
        if (csts & NVME_CSTS_RDY) break;
        nvme_delay(100);
    }

    /* Identify Controller (CNS = 1) */
    OoNvmeSqEntry cmd;
    nvme_memzero(&cmd, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.prp1   = (uint64_t)(uintptr_t)c->data_buf;
    cmd.cdw10  = 1; /* CNS = 1 = identify controller */

    if (nvme_submit_and_poll(c, &cmd, 0) != 0) return -3;

    /* Extract total capacity from Identify Namespace (CNS=0) */
    nvme_memzero(&cmd, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid   = OO_NVME_NSID;
    cmd.prp1   = (uint64_t)(uintptr_t)c->data_buf;
    cmd.cdw10  = 0; /* CNS = 0 = identify namespace */

    if (nvme_submit_and_poll(c, &cmd, 0) == 0) {
        /* NSZE at offset 0 in Identify Namespace data structure */
        uint64_t nsze;
        uint8_t *d = c->data_buf;
        nsze = (uint64_t)d[0]       | ((uint64_t)d[1] << 8)  |
               ((uint64_t)d[2] << 16) | ((uint64_t)d[3] << 24) |
               ((uint64_t)d[4] << 32) | ((uint64_t)d[5] << 40) |
               ((uint64_t)d[6] << 48) | ((uint64_t)d[7] << 56);
        c->capacity_lba = nsze;
    }

    c->initialized = 1;
    return 0;
}

int oo_nvme_read_blocks(OoNvmeCtrl *c, uint64_t lba, uint32_t count, void *buf) {
    if (!c->initialized) return -1;
    if (count == 0)      return  0;

    /* Transfer one block at a time to stay within static data_buf */
    uint8_t *dst = (uint8_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        nvme_memzero(c->data_buf, OO_NVME_BLOCK_SIZE);

        OoNvmeSqEntry cmd;
        nvme_memzero(&cmd, sizeof(cmd));
        cmd.opcode = NVME_NVM_READ;
        cmd.nsid   = OO_NVME_NSID;
        cmd.prp1   = (uint64_t)(uintptr_t)c->data_buf;
        cmd.cdw10  = (uint32_t)((lba + i) & 0xFFFFFFFF);
        cmd.cdw11  = (uint32_t)((lba + i) >> 32);
        cmd.cdw12  = 0; /* NLB = 0 means 1 block */

        if (nvme_submit_and_poll(c, &cmd, 0) != 0) return -(int)(i + 1);

        /* Copy block to caller's buffer */
        uint8_t *src = c->data_buf;
        for (uint32_t b = 0; b < OO_NVME_BLOCK_SIZE; b++)
            dst[b] = src[b];
        dst += OO_NVME_BLOCK_SIZE;
    }
    return 0;
}

int oo_nvme_write_blocks(OoNvmeCtrl *c, uint64_t lba, uint32_t count, const void *buf) {
    if (!c->initialized) return -1;
    if (count == 0)      return  0;

    const uint8_t *src = (const uint8_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        /* Copy one block into the data buffer */
        uint8_t *dst = c->data_buf;
        for (uint32_t b = 0; b < OO_NVME_BLOCK_SIZE; b++)
            dst[b] = src[b];
        src += OO_NVME_BLOCK_SIZE;

        OoNvmeSqEntry cmd;
        nvme_memzero(&cmd, sizeof(cmd));
        cmd.opcode = NVME_NVM_WRITE;
        cmd.nsid   = OO_NVME_NSID;
        cmd.prp1   = (uint64_t)(uintptr_t)c->data_buf;
        cmd.cdw10  = (uint32_t)((lba + i) & 0xFFFFFFFF);
        cmd.cdw11  = (uint32_t)((lba + i) >> 32);
        cmd.cdw12  = 0; /* NLB = 0 means 1 block */

        if (nvme_submit_and_poll(c, &cmd, 0) != 0) return -(int)(i + 1);
    }
    return 0;
}

/* Minimal hex printer for 64-bit values */
static void nvme_u64_hex(uint64_t v, char *buf, int size) {
    const char *hex = "0123456789ABCDEF";
    int out = 0;
    buf[out++] = '0'; buf[out++] = 'x';
    for (int shift = 60; shift >= 0 && out < size - 1; shift -= 4)
        buf[out++] = hex[(v >> shift) & 0xF];
    buf[out] = '\0';
}

static void nvme_u32_to_str(uint32_t v, char *buf, int size) {
    char tmp[12];
    int pos = 0;
    if (v == 0) { tmp[pos++] = '0'; }
    else { while (v && pos < 11) { tmp[pos++] = '0' + (v % 10); v /= 10; } }
    int out = 0;
    for (int i = pos - 1; i >= 0 && out < size - 1; i--)
        buf[out++] = tmp[i];
    buf[out] = '\0';
}

void oo_nvme_print_status(const OoNvmeCtrl *c, void (*print_fn)(const char *)) {
    if (!print_fn) return;
    print_fn("[NVMe] NVMe 1.3 PCIe SSD Controller\n");
    print_fn("  initialized : "); print_fn(c->initialized ? "yes" : "no"); print_fn("\n");

    char buf[20];
    nvme_u64_hex(c->bar0, buf, sizeof(buf));
    print_fn("  BAR0        : "); print_fn(buf); print_fn("\n");

    nvme_u32_to_str((uint32_t)(c->capacity_lba >> 32), buf, sizeof(buf));
    print_fn("  capacity_lba: 0x");
    nvme_u64_hex(c->capacity_lba, buf, sizeof(buf));
    print_fn(buf); print_fn("\n");

    nvme_u32_to_str(c->db_stride, buf, sizeof(buf));
    print_fn("  db_stride   : "); print_fn(buf); print_fn(" bytes\n");
}
