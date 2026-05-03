/* oo_wifi_fw.h — USB WiFi firmware loader for OO bare-metal
 * Supports: Realtek RTL8188EU, RTL8192EU, Mediatek MT7601U
 * Uses EFI_USB_IO_PROTOCOL for firmware upload (no OS USB stack needed).
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include <efilib.h>
#include <stdint.h>

#define OO_WIFI_FW_MAX_HANDLES  4
#define OO_WIFI_FW_MAX_FW_SIZE  (512 * 1024)  /* 512 KB max firmware blob */
#define OO_WIFI_FW_CHUNK_SIZE   4096

/* Vendor/product IDs we recognize */
#define OO_WIFI_VID_REALTEK   0x0BDA
#define OO_WIFI_PID_RTL8188EU 0x8179
#define OO_WIFI_PID_RTL8192EU 0x818B
#define OO_WIFI_VID_MEDIATEK  0x148F
#define OO_WIFI_PID_MT7601U   0x7601

typedef enum {
    OO_WIFI_FW_NONE = 0,
    OO_WIFI_FW_RTL8188EU,
    OO_WIFI_FW_RTL8192EU,
    OO_WIFI_FW_MT7601U,
} OoWifiFwChip;

typedef struct {
    EFI_HANDLE    handle;
    OoWifiFwChip  chip;
    uint16_t      vid;
    uint16_t      pid;
    int           fw_loaded;     /* 1 = firmware successfully uploaded */
    int           bulk_ep_out;   /* endpoint address for bulk OUT      */
    uint32_t      fw_crc32;      /* CRC32 of last uploaded firmware     */
    uint32_t      bytes_sent;    /* total bytes transferred             */
} OoWifiFwDev;

typedef struct {
    OoWifiFwDev devices[OO_WIFI_FW_MAX_HANDLES];
    int         n_devices;
    int         initialized;
} OoWifiFw;

/* Scan USB handles for known WiFi chips. Returns count found. */
int  oo_wifi_fw_init(OoWifiFw *fw);

/* Upload firmware from `fw_blob` (already in RAM) to device `dev_idx`.
 * Sent in OO_WIFI_FW_CHUNK_SIZE chunks via USB bulk OUT transfer.
 * Returns 0 on success, negative on error. */
int  oo_wifi_fw_upload(OoWifiFw *fw, int dev_idx, const uint8_t *fw_blob, uint32_t fw_size);

/* Auto-detect and upload all recognized devices (calls oo_wifi_fw_upload for each).
 * fw_blob must be large enough for the largest firmware (use OO_WIFI_FW_MAX_FW_SIZE).
 * In bare-metal, the caller loads fw_blob from disk via EFI file protocol first.
 * Returns number of successfully initialized devices. */
int  oo_wifi_fw_auto(OoWifiFw *fw, const uint8_t *fw_blob, uint32_t fw_size);

/* Print status via callback. */
void oo_wifi_fw_print_status(const OoWifiFw *fw, void (*fn)(const char *));
