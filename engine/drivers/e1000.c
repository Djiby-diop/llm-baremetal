#include "e1000.h"
#include "uart.h"
#include <string.h>

/*
 * OO Driver: Intel E1000 (Mutation v2 - Complete Pulse)
 * Evolved for Phase 12 Swarm Sovereignty.
 */

#define E1000_REG_CTRL      0x0000
#define E1000_REG_STATUS    0x0008
#define E1000_REG_RCTL      0x0100
#define E1000_REG_TCTL      0x0400
#define E1000_REG_RDBAL     0x2800
#define E1000_REG_RDBAH     0x2804
#define E1000_REG_RDLEN     0x2808
#define E1000_REG_RDH       0x2810
#define E1000_REG_RDT       0x2818
#define E1000_REG_TDBAL     0x3800
#define E1000_REG_TDBAH     0x3804
#define E1000_REG_TDLEN     0x3808
#define E1000_REG_TDH       0x3810
#define E1000_REG_TDT       0x3818
#define E1000_REG_RAL       0x5400
#define E1000_REG_RAH       0x5404

// Descriptors
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

#define NUM_RX_DESCRIPTORS 32
#define NUM_TX_DESCRIPTORS 32

// Static allocation for rings and buffers (Zero-Jitter Bare-Metal style)
static __attribute__((aligned(16))) struct e1000_rx_desc rx_ring[NUM_RX_DESCRIPTORS];
static __attribute__((aligned(16))) struct e1000_tx_desc tx_ring[NUM_TX_DESCRIPTORS];
static uint8_t rx_buffers[NUM_RX_DESCRIPTORS][2048];

static uint32_t e1000_mmio_base = 0;
static uint32_t rx_cur = 0;
static uint32_t tx_cur = 0;
static uint8_t  mac_addr[6];

static inline void e1000_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(uintptr_t)(e1000_mmio_base + reg) = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t*)(uintptr_t)(e1000_mmio_base + reg);
}

void oo_e1000_init(OoPciDevice *dev) {
    if (!dev) return;
    
    // Read BAR0 (MMIO)
    uint32_t bar0 = oo_pci_read_config_32(dev->bus, dev->dev, dev->func, 0x10);
    e1000_mmio_base = bar0 & 0xFFFFFFF0;
    
    soma_uart_puts("[E1000] Initializing mutation v2 driver...\r\n");
    soma_uart_puts("  -> MMIO Base: ");
    _uart_putx8(e1000_mmio_base);
    soma_uart_puts("\r\n");

    // 1. Read MAC
    uint32_t ral = e1000_read(E1000_REG_RAL);
    uint32_t rah = e1000_read(E1000_REG_RAH);
    mac_addr[0] = ral & 0xFF;
    mac_addr[1] = (ral >> 8) & 0xFF;
    mac_addr[2] = (ral >> 16) & 0xFF;
    mac_addr[3] = (ral >> 24) & 0xFF;
    mac_addr[4] = rah & 0xFF;
    mac_addr[5] = (rah >> 8) & 0xFF;

    soma_uart_puts("  -> MAC: ");
    for(int i=0; i<6; i++) {
        _uart_putx8(mac_addr[i]);
        if(i<5) soma_uart_putc(':');
    }
    soma_uart_puts("\r\n");

    // 2. Setup RX Ring
    for(int i=0; i<NUM_RX_DESCRIPTORS; i++) {
        rx_ring[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_ring[i].status = 0;
    }

    e1000_write(E1000_REG_RDBAL, (uint32_t)(uintptr_t)rx_ring);
    e1000_write(E1000_REG_RDBAH, 0);
    e1000_write(E1000_REG_RDLEN, NUM_RX_DESCRIPTORS * sizeof(struct e1000_rx_desc));
    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, NUM_RX_DESCRIPTORS - 1);

    // Enable RX
    e1000_write(E1000_REG_RCTL, (1 << 1) | (1 << 2) | (1 << 4) | (1 << 15)); // EN + SBP + UPE + BAM

    // 3. Setup TX Ring
    for(int i=0; i<NUM_TX_DESCRIPTORS; i++) {
        tx_ring[i].addr = 0;
        tx_ring[i].status = 0;
    }

    e1000_write(E1000_REG_TDBAL, (uint32_t)(uintptr_t)tx_ring);
    e1000_write(E1000_REG_TDBAH, 0);
    e1000_write(E1000_REG_TDLEN, NUM_TX_DESCRIPTORS * sizeof(struct e1000_tx_desc));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);

    // Enable TX
    e1000_write(E1000_REG_TCTL, (1 << 1) | (1 << 3)); // EN + PSP

    soma_uart_puts("[E1000] Active and hunting.\r\n");
}

int oo_e1000_status(void) {
    if (!e1000_mmio_base) return -1;
    uint32_t status = e1000_read(E1000_REG_STATUS);
    return (status & (1 << 1)) ? 1 : 0; // Link up bit
}

int oo_e1000_send(void *data, uint16_t len) {
    if (!e1000_mmio_base) return -1;

    uint32_t tail = e1000_read(E1000_REG_TDT);
    struct e1000_tx_desc *desc = &tx_ring[tail];

    desc->addr = (uint64_t)(uintptr_t)data;
    desc->length = len;
    desc->cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP + IFCS + RS
    desc->status = 0;

    e1000_write(E1000_REG_TDT, (tail + 1) % NUM_TX_DESCRIPTORS);

    // Wait for transmit to complete
    while (!(desc->status & 0x0F)) { /* Polling */ }

    return 0;
}

int oo_e1000_receive(void *buffer, uint16_t *len) {
    if (!e1000_mmio_base) return -1;

    struct e1000_rx_desc *desc = &rx_ring[rx_cur];

    if (!(desc->status & (1 << 0))) {
        return -1; // No packet received
    }

    *len = desc->length;
    memcpy(buffer, rx_buffers[rx_cur], *len);

    desc->status = 0; // Clear status for reuse
    e1000_write(E1000_REG_RDT, rx_cur);
    rx_cur = (rx_cur + 1) % NUM_RX_DESCRIPTORS;

    return 0;
}

void oo_e1000_get_mac(uint8_t *mac) {
    memcpy(mac, mac_addr, 6);
}
