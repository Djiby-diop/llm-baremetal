// oo_usb_msc.h — USB Mass Storage Class (BBB = Bulk-Only Transport)
//
// Enables reading files from USB flash drives at runtime (post-boot).
// Used for: hot-swapping LLM model files without rebooting.
//
// Protocol: USB MSC Bulk-Only Transport (BOT) — the standard used by
// virtually all USB flash drives and external hard drives.
//
// UEFI context: uses EFI_USB_IO_PROTOCOL for low-level USB transactions
// before ExitBootServices(), then falls back to EHCI/XHCI register I/O.
//
// SCSI commands implemented:
//   - INQUIRY (device identification)
//   - READ_CAPACITY_10 (get sector count + size)
//   - READ_10 (read sectors)
//   - TEST_UNIT_READY (check media present)
//
// Filesystem: reads raw FAT32 sectors (compatible with boot FAT32 partition)
// No full FAT32 implementation — just enough to find files by name.
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Limits ────────────────────────────────────────────────────────────────────
#define OO_USB_MSC_MAX_SECTOR  512   // bytes per sector (standard)
#define OO_USB_MSC_MAX_FILE  (64*1024*1024) // max file size: 64MB

// ── USB MSC device descriptor ─────────────────────────────────────────────────
typedef struct {
    // Device identity
    char     vendor[9];    // SCSI INQUIRY vendor ID (8 chars + \0)
    char     product[17];  // product ID (16 chars + \0)
    char     revision[5];  // firmware revision (4 chars + \0)

    // Geometry
    uint32_t sector_count;
    uint32_t sector_size;  // usually 512
    uint64_t total_bytes;

    // UEFI USB I/O protocol pointer (opaque)
    void    *usb_io;

    // Status
    int      initialized;
    int      media_present;

    // EHCI/XHCI MMIO base (fallback if no UEFI)
    uint64_t hci_base;
    int      use_uefi;     // 1 = use EFI_USB_IO, 0 = raw MMIO
} OoUsbMsc;

// ── CBW (Command Block Wrapper) — USB MSC BBB protocol ────────────────────────
typedef struct __attribute__((packed)) {
    uint32_t signature;     // 0x43425355 "USBC"
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t  flags;         // 0x80 = data in (device→host), 0x00 = data out
    uint8_t  lun;
    uint8_t  cb_length;     // SCSI command length (6-16)
    uint8_t  cb[16];        // SCSI Command Block
} OoUsbCbw;

// ── CSW (Command Status Wrapper) ──────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint32_t signature;     // 0x53425355 "USBS"
    uint32_t tag;
    uint32_t data_residue;
    uint8_t  status;        // 0=pass, 1=fail, 2=phase error
} OoUsbCsw;

// ── SCSI commands ─────────────────────────────────────────────────────────────
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_INQUIRY          0x12
#define SCSI_READ_CAPACITY    0x25
#define SCSI_READ_10          0x28

// ── FAT32 minimal structures ──────────────────────────────────────────────────

typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;   // 0 for FAT32
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;        // 0 for FAT32
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // FAT32 extended BPB
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  _reserved[12];
    uint8_t  drive_number;
    uint8_t  _reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];         // "FAT32   "
} OoFat32Bpb;

typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  _reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} OoFat32DirEntry;

#define FAT32_ATTR_DIR   0x10
#define FAT32_ATTR_ARCH  0x20

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize USB MSC device via UEFI
// usb_io_proto: EFI_USB_IO_PROTOCOL* (pass from UEFI handle database)
// Returns 0 on success, -1 on failure
int oo_usb_msc_init_uefi(OoUsbMsc *dev, void *usb_io_proto);

// Test unit ready (returns 1 if media present)
int oo_usb_msc_ready(OoUsbMsc *dev);

// Read sectors from device
// lba: logical block address, count: number of sectors, buf: output
// Returns bytes read, or -1 on error
int oo_usb_msc_read_sectors(OoUsbMsc *dev, uint32_t lba,
                             uint32_t count, uint8_t *buf);

// High-level: find a file on FAT32 volume and read it into buf
// path: "MODEL.BIN" or "MODELS/OO40M.GGUF" (8.3 uppercase)
// Returns bytes read, 0 if not found, -1 on error
int oo_usb_msc_read_file(OoUsbMsc *dev, const char *path,
                          uint8_t *buf, int buf_cap);

// Dump device info (UART debug)
void oo_usb_msc_dump(const OoUsbMsc *dev);

#ifdef __cplusplus
}
#endif
