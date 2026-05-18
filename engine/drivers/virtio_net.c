#include "virtio_net.h"
#include "virtio.h"
#include "uart.h"
#include <string.h>

// VirtIO Legacy PCI Register Offsets
#define VIRTIO_PCI_HOST_FEATURES  0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN      8
#define VIRTIO_PCI_QUEUE_NUM      12
#define VIRTIO_PCI_QUEUE_SEL      14
#define VIRTIO_PCI_QUEUE_NOTIFY   16
#define VIRTIO_PCI_STATUS         18
#define VIRTIO_PCI_ISR            19

// VirtIO Status Bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FAILED      128

static uint32_t virtio_net_io_base = 0;
static uint8_t  mac_addr[6];

// Helper functions for I/O ports
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

int oo_virtio_net_init(OoPciDevice *pci_dev) {
    if (!pci_dev) return -1;

    // 1. Read BAR0 to get I/O base
    uint32_t bar0 = oo_pci_read_config_32(pci_dev->bus, pci_dev->dev, pci_dev->func, 0x10);
    if (!(bar0 & 1)) {
        soma_uart_puts("[VirtIO-Net] Error: BAR0 is not I/O space\r\n");
        return -1;
    }
    virtio_net_io_base = bar0 & 0xFFFFFFFC;

    soma_uart_puts("[VirtIO-Net] Found device. I/O Base: ");
    _uart_putx8(virtio_net_io_base);
    soma_uart_puts("\r\n");

    // 2. Reset Device
    outb(virtio_net_io_base + VIRTIO_PCI_STATUS, 0);

    // 3. Set Acknowledge & Driver bits
    outb(virtio_net_io_base + VIRTIO_PCI_STATUS, inb(virtio_net_io_base + VIRTIO_PCI_STATUS) | VIRTIO_STATUS_ACKNOWLEDGE);
    outb(virtio_net_io_base + VIRTIO_PCI_STATUS, inb(virtio_net_io_base + VIRTIO_PCI_STATUS) | VIRTIO_STATUS_DRIVER);

    // 4. Read MAC Address (Device specific config starts at offset 20 for legacy)
    uint16_t config_base = virtio_net_io_base + 20;
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = inb(config_base + i);
    }

    soma_uart_puts("  -> MAC: ");
    for(int i=0; i<6; i++) {
        _uart_putx8(mac_addr[i]);
        if(i<5) soma_uart_putc(':');
    }
    soma_uart_puts("\r\n");

    // 5. Set DRIVER_OK
    outb(virtio_net_io_base + VIRTIO_PCI_STATUS, inb(virtio_net_io_base + VIRTIO_PCI_STATUS) | VIRTIO_STATUS_DRIVER_OK);

    soma_uart_puts("[VirtIO-Net] Active and listening for the Swarm.\r\n");
    return 0;
}

int oo_virtio_net_send(void *data, uint16_t len) {
    if (!virtio_net_io_base) return -1;
    // Stub: Virtqueue handling for TX
    (void)data; (void)len;
    return 0;
}

int oo_virtio_net_receive(void *buffer, uint16_t *len) {
    if (!virtio_net_io_base) return -1;
    // Stub: Virtqueue handling for RX
    (void)buffer; (void)len;
    return -1;
}

void oo_virtio_net_get_mac(uint8_t *mac) {
    memcpy(mac, mac_addr, 6);
}
