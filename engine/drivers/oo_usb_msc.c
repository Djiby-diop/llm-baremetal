// oo_usb_msc.c — USB Mass Storage Class BBB (Implementation)
//
// Freestanding C11 — UEFI Ring 0, no libc, no malloc.

#include "oo_usb_msc.h"

// ── UEFI USB I/O protocol shim ────────────────────────────────────────────────

typedef uint64_t EfiStatus;
#define EFI_SUCCESS 0

// EFI_USB_IO_PROTOCOL (simplified — only bulk transfer needed)
typedef struct {
    void *usb_control_transfer;
    EfiStatus (*usb_bulk_transfer)(void *this, uint8_t endpoint,
                                    void *data, uint64_t *data_length,
                                    uint64_t timeout, uint32_t *transfer_result);
    void *usb_async_interrupt_transfer;
    void *usb_sync_interrupt_transfer;
    void *usb_async_isochronous_transfer;
    void *usb_sync_isochronous_transfer;
    void *usb_get_device_descriptor;
    void *usb_get_config_descriptor;
    void *usb_get_interface_descriptor;
    void *usb_get_endpoint_descriptor;
    void *usb_get_string_descriptor;
    void *usb_get_supported_languages;
    void *usb_port_reset;
} OoEfiUsbIo;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void _msc_memset(void *dst, uint8_t v, int n) {
    uint8_t *p = dst;
    for (int i = 0; i < n; i++) p[i] = v;
}

static void _msc_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int _msc_memcmp(const void *a, const void *b, int n) {
    const uint8_t *p = a, *q = b;
    for (int i = 0; i < n; i++) {
        if (p[i] < q[i]) return -1;
        if (p[i] > q[i]) return  1;
    }
    return 0;
}

static uint32_t _msc_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

static uint32_t _msc_le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static uint16_t _msc_le16(const uint8_t *p) {
    return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}

// USB I/O: bulk transfer wrapper
// endpoint: 0x81 = bulk IN (device→host), 0x02 = bulk OUT (host→device)
static int _usb_bulk(OoUsbMsc *dev, uint8_t endpoint,
                     void *data, uint64_t *len) {
    OoEfiUsbIo *io = (OoEfiUsbIo *)dev->usb_io;
    uint32_t result = 0;
    EfiStatus st = io->usb_bulk_transfer(io, endpoint, data, len, 5000, &result);
    return (st == EFI_SUCCESS && result == 0) ? 0 : -1;
}

// ── CBW / CSW protocol ───────────────────────────────────────────────────────

static uint32_t _cbw_tag = 1;

static int _msc_send_cbw(OoUsbMsc *dev, OoUsbCbw *cbw) {
    uint64_t len = sizeof(OoUsbCbw);
    return _usb_bulk(dev, 0x02, cbw, &len);  // bulk OUT
}

static int _msc_recv_data(OoUsbMsc *dev, void *buf, uint64_t *len) {
    return _usb_bulk(dev, 0x81, buf, len);   // bulk IN
}

static int _msc_recv_csw(OoUsbMsc *dev, OoUsbCsw *csw) {
    uint64_t len = sizeof(OoUsbCsw);
    int r = _usb_bulk(dev, 0x81, csw, &len);
    if (r < 0) return -1;
    if (csw->signature != 0x53425355) return -1;  // "USBS"
    return (csw->status == 0) ? 0 : -1;
}

// ── SCSI command wrapper ──────────────────────────────────────────────────────

static int _scsi_cmd(OoUsbMsc *dev,
                      uint8_t *cdb, int cdb_len,
                      void *data_buf, uint32_t data_len,
                      uint8_t flags) {
    OoUsbCbw cbw;
    _msc_memset(&cbw, 0, sizeof(cbw));
    cbw.signature            = 0x43425355;  // "USBC"
    cbw.tag                  = _cbw_tag++;
    cbw.data_transfer_length = data_len;
    cbw.flags                = flags;       // 0x80=IN, 0x00=OUT
    cbw.lun                  = 0;
    cbw.cb_length            = (uint8_t)cdb_len;
    _msc_memcpy(cbw.cb, cdb, cdb_len);

    if (_msc_send_cbw(dev, &cbw) < 0) return -1;

    if (data_len > 0 && data_buf) {
        uint64_t xlen = data_len;
        if (_msc_recv_data(dev, data_buf, &xlen) < 0) return -1;
    }

    OoUsbCsw csw;
    if (_msc_recv_csw(dev, &csw) < 0) return -1;
    return 0;
}

// ── SCSI commands ─────────────────────────────────────────────────────────────

static int _scsi_test_unit_ready(OoUsbMsc *dev) {
    uint8_t cdb[6] = {SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    return _scsi_cmd(dev, cdb, 6, (void *)0, 0, 0x00);
}

static int _scsi_inquiry(OoUsbMsc *dev) {
    uint8_t cdb[6] = {SCSI_INQUIRY, 0, 0, 0, 36, 0};
    uint8_t buf[36];
    _msc_memset(buf, 0, 36);
    if (_scsi_cmd(dev, cdb, 6, buf, 36, 0x80) < 0) return -1;
    // Extract vendor (bytes 8-15), product (16-31), revision (32-35)
    for (int i = 0; i < 8 && i < 8; i++)
        dev->vendor[i] = (char)(buf[8+i] >= 0x20 ? buf[8+i] : ' ');
    dev->vendor[8] = '\0';
    for (int i = 0; i < 16; i++)
        dev->product[i] = (char)(buf[16+i] >= 0x20 ? buf[16+i] : ' ');
    dev->product[16] = '\0';
    for (int i = 0; i < 4; i++)
        dev->revision[i] = (char)(buf[32+i] >= 0x20 ? buf[32+i] : ' ');
    dev->revision[4] = '\0';
    return 0;
}

static int _scsi_read_capacity(OoUsbMsc *dev) {
    uint8_t cdb[10] = {SCSI_READ_CAPACITY, 0,0,0,0,0,0,0,0,0};
    uint8_t buf[8];
    _msc_memset(buf, 0, 8);
    if (_scsi_cmd(dev, cdb, 10, buf, 8, 0x80) < 0) return -1;
    dev->sector_count = _msc_be32(buf + 0) + 1;
    dev->sector_size  = _msc_be32(buf + 4);
    if (dev->sector_size == 0) dev->sector_size = 512;
    dev->total_bytes  = (uint64_t)dev->sector_count * dev->sector_size;
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

int oo_usb_msc_init_uefi(OoUsbMsc *dev, void *usb_io_proto) {
    if (!dev || !usb_io_proto) return -1;
    _msc_memset(dev, 0, sizeof(OoUsbMsc));
    dev->usb_io   = usb_io_proto;
    dev->use_uefi = 1;

    // Test unit ready (may need a few retries)
    int ok = 0;
    for (int i = 0; i < 3; i++) {
        if (_scsi_test_unit_ready(dev) == 0) { ok = 1; break; }
    }
    if (!ok) return -1;

    if (_scsi_inquiry(dev) < 0)         return -1;
    if (_scsi_read_capacity(dev) < 0)   return -1;

    dev->media_present = 1;
    dev->initialized   = 1;
    return 0;
}

int oo_usb_msc_ready(OoUsbMsc *dev) {
    if (!dev || !dev->initialized) return 0;
    return _scsi_test_unit_ready(dev) == 0 ? 1 : 0;
}

int oo_usb_msc_read_sectors(OoUsbMsc *dev, uint32_t lba,
                              uint32_t count, uint8_t *buf) {
    if (!dev || !dev->initialized || !buf) return -1;
    uint32_t bytes = count * dev->sector_size;

    uint8_t cdb[10];
    cdb[0] = SCSI_READ_10;
    cdb[1] = 0;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >>  8);
    cdb[5] = (uint8_t)(lba >>  0);
    cdb[6] = 0;
    cdb[7] = (uint8_t)(count >> 8);
    cdb[8] = (uint8_t)(count >> 0);
    cdb[9] = 0;

    if (_scsi_cmd(dev, cdb, 10, buf, bytes, 0x80) < 0) return -1;
    return (int)bytes;
}

// ── FAT32 file lookup ─────────────────────────────────────────────────────────

// Read one 512-byte sector into buf
static int _read_sector(OoUsbMsc *dev, uint32_t lba, uint8_t *buf) {
    return oo_usb_msc_read_sectors(dev, lba, 1, buf);
}

// Convert filename "MODEL.BIN" → FAT32 8.3 format "MODEL   BIN"
static void _to_83(const char *in, char *out11) {
    for (int i = 0; i < 11; i++) out11[i] = ' ';
    int i = 0, o = 0;
    while (in[i] && in[i] != '.' && o < 8) { out11[o++] = in[i++]; }
    if (in[i] == '.') { i++; o = 8; }
    while (in[i] && o < 11) { out11[o++] = in[i++]; }
}

static int _fat32_cluster_to_lba(const OoFat32Bpb *bpb, uint32_t fat_lba,
                                   uint32_t cluster) {
    uint32_t data_start = fat_lba
        + (uint32_t)bpb->reserved_sectors
        + (uint32_t)bpb->num_fats * bpb->fat_size_32;
    return (int)(data_start + (cluster - 2) * bpb->sectors_per_cluster);
}

int oo_usb_msc_read_file(OoUsbMsc *dev, const char *path,
                          uint8_t *buf, int buf_cap) {
    if (!dev || !dev->initialized || !path || !buf) return -1;

    uint8_t sector[512];

    // Read BPB from LBA 0
    if (_read_sector(dev, 0, sector) < 0) return -1;
    OoFat32Bpb bpb;
    _msc_memcpy(&bpb, sector, sizeof(OoFat32Bpb));

    // Validate FAT32
    if (_msc_memcmp(bpb.fs_type, "FAT32   ", 8) != 0) return -1;
    if (bpb.bytes_per_sector == 0) return -1;

    uint32_t fat_lba     = 0;
    uint32_t fat_start   = fat_lba + bpb.reserved_sectors;
    uint32_t root_cluster = bpb.root_cluster;
    uint32_t spc          = bpb.sectors_per_cluster;

    // Convert path to 8.3
    char name83[11];
    _to_83(path, name83);

    // Walk root directory cluster chain
    uint32_t cluster = root_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        // Read cluster sectors
        int clus_lba = _fat32_cluster_to_lba(&bpb, fat_lba, cluster);
        for (uint32_t si = 0; si < spc; si++) {
            if (_read_sector(dev, (uint32_t)clus_lba + si, sector) < 0) return -1;
            // Each sector has 16 directory entries (512/32)
            for (int ei = 0; ei < 16; ei++) {
                OoFat32DirEntry *e = (OoFat32DirEntry *)(sector + ei * 32);
                if (e->name[0] == 0x00) return 0; // end of directory
                if ((uint8_t)e->name[0] == 0xE5) continue; // deleted
                if (e->attr & 0x08) continue;  // volume label
                if (e->attr & FAT32_ATTR_DIR) continue;  // directory

                if (_msc_memcmp(e->name, name83, 11) == 0) {
                    // Found! Read file data
                    uint32_t fc = ((uint32_t)_msc_le16((uint8_t*)&e->cluster_hi) << 16)
                                | _msc_le16((uint8_t*)&e->cluster_lo);
                    uint32_t fsz = _msc_le32((uint8_t*)&e->file_size);
                    if ((int)fsz > buf_cap) fsz = (uint32_t)buf_cap;

                    int written = 0;
                    uint32_t fc2 = fc;
                    uint8_t fat_tmp[512];
                    while (fc2 >= 2 && fc2 < 0x0FFFFFF8 && (int)fsz > written) {
                        int flba = _fat32_cluster_to_lba(&bpb, fat_lba, fc2);
                        for (uint32_t si2 = 0; si2 < spc && (int)fsz > written; si2++) {
                            uint8_t tmp[512];
                            if (_read_sector(dev, (uint32_t)flba + si2, tmp) < 0)
                                return written;
                            int to_copy = (int)fsz - written;
                            if (to_copy > 512) to_copy = 512;
                            _msc_memcpy(buf + written, tmp, to_copy);
                            written += to_copy;
                        }
                        // Follow FAT chain
                        uint32_t fat_offset = fc2 * 4;
                        uint32_t fat_sector_lba = fat_start + fat_offset / 512;
                        if (_read_sector(dev, fat_sector_lba, fat_tmp) < 0) break;
                        fc2 = _msc_le32(fat_tmp + (fat_offset % 512)) & 0x0FFFFFFF;
                    }
                    return written;
                }
            }
        }

        // Follow FAT chain for directory cluster
        uint32_t fat_offset   = cluster * 4;
        uint32_t fat_sector_lba = fat_start + fat_offset / 512;
        if (_read_sector(dev, fat_sector_lba, sector) < 0) break;
        cluster = _msc_le32(sector + (fat_offset % 512)) & 0x0FFFFFFF;
    }
    return 0; // not found
}

// ── Debug dump ────────────────────────────────────────────────────────────────

static void _msc_putc(char c) {
    for (int i = 0; i < 10000; i++) {
        uint8_t s; __asm__ __volatile__("inb $0x3F8+5, %0" : "=a"(s));
        if (s & 0x20) break;
    }
    __asm__ __volatile__("outb %0, $0x3F8" : : "a"(c));
}

static void _msc_print(const char *s) { while (*s) _msc_putc(*s++); }

void oo_usb_msc_dump(const OoUsbMsc *dev) {
    if (!dev) return;
    _msc_print("[USB MSC] ");
    if (!dev->initialized) { _msc_print("NOT INIT\n"); return; }
    _msc_print("Vendor: "); _msc_print(dev->vendor); _msc_putc('\n');
    _msc_print("[USB MSC] Product: "); _msc_print(dev->product); _msc_putc('\n');
}
