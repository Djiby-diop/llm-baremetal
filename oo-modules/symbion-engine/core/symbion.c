#include "symbion.h"
#include <string.h>
#include "../../../drivers/nic_e1000.h"
#include "../../../drivers/oo_nvme.h"
#include "../../../drivers/xhci.h"
#include "../../../drivers/hda.h"
#include "../../../../engine/drivers/virtio_net.h"
#include "../../../../engine/drivers/virtio.h"

void symbion_scan_body(SymbionCtx *ctx) {
    if (!ctx) return;
    ctx->device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t config = oo_pci_read_config((uint8_t)bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(config & 0xFFFF);
                
                if (vendor == 0xFFFF) continue; 

                if (ctx->device_count < 64) {
                    SymbionPCIDevice *dev = &ctx->devices[ctx->device_count++];
                    dev->vendor_id = vendor;
                    dev->device_id = (uint16_t)(config >> 16);
                    dev->bus  = (uint8_t)bus;
                    dev->slot = slot;
                    dev->func = func;
                    
                    uint32_t class_reg = oo_pci_read_config((uint8_t)bus, slot, func, 0x08);
                    dev->class_code = class_reg >> 8;
                    
                    uint8_t class_code = (uint8_t)(dev->class_code >> 16);
                    uint8_t subclass   = (uint8_t)(dev->class_code >> 8);

                    // Auto-Bind Drivers
                    if (dev->vendor_id == 0x8086 && (dev->device_id == 0x100E || dev->device_id == 0x100F)) {
                        static NicE1000Ctx nic;
                        nic_e1000_init(&nic, dev);
                    } else if (class_code == 0x01 && subclass == 0x08) {
                        static NvmeCtx nvme;
                        nvme_init(&nvme, dev);
                    } else if (class_code == 0x0C && subclass == 0x03) {
                        static XhciCtx xhci;
                        xhci_init(&xhci, dev);
                    } else if (class_code == 0x04 && subclass == 0x03) {
                        static HdaCtx hda;
                        hda_init(&hda, dev);
                    } else if (dev->vendor_id == VIRTIO_VENDOR_ID && dev->device_id == VIRTIO_DEV_NET) {
                        OoPciDevice oo_dev;
                        oo_dev.bus = dev->bus;
                        oo_dev.dev = dev->slot;
                        oo_dev.func = dev->func;
                        oo_dev.vendor_id = dev->vendor_id;
                        oo_dev.device_id = dev->device_id;
                        oo_virtio_net_init(&oo_dev);
                    }
                }
                
                if (func == 0) {
                    uint32_t header_type = oo_pci_read_config((uint8_t)bus, slot, 0, 0x0C);
                    if (!(header_type & 0x00800000)) break;
                }
            }
        }
    }
}

SymbionPCIDevice* symbion_find_nic(SymbionCtx *ctx) {
    for (uint32_t i = 0; i < ctx->device_count; i++) {
        // Network controller class code starts with 0x02
        if ((ctx->devices[i].class_code >> 16) == 0x02) {
            return &ctx->devices[i];
        }
    }
    return NULL;
}
