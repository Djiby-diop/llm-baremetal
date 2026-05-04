/* oo_wifi_fw.h — USB WiFi firmware loader for OO bare-metal
 * Supports: Realtek RTL8188EU, RTL8192EU, Mediatek MT7601U
 * Uses EFI_USB_IO_PROTOCOL for firmware upload (no OS USB stack needed).
 * Freestanding C11. No libc.
 */
#pragma once
#include <efi.h>
#include <efilib.h>
#include <stdint.h>

/* ── USB IO Protocol stubs (not in gnu-efi; minimal for WiFi FW loader) ─── */
#ifndef EFI_USB_IO_PROTOCOL_GUID
#define EFI_USB_IO_PROTOCOL_GUID \
    { 0x2B2F68D6, 0x0CD2, 0x44CF, {0x8E, 0x8B, 0xBB, 0xA2, 0x0B, 0x1B, 0x5B, 0x75} }

typedef struct {
    UINT16 Length;
    UINT8  DescriptorType;
    UINT16 BcdUSB;
    UINT8  DeviceClass;
    UINT8  DeviceSubClass;
    UINT8  DeviceProtocol;
    UINT8  MaxPacketSize0;
    UINT16 IdVendor;
    UINT16 IdProduct;
    UINT16 BcdDevice;
    UINT8  StrManufacturer;
    UINT8  StrProduct;
    UINT8  StrSerialNumber;
    UINT8  NumConfigurations;
} EFI_USB_DEVICE_DESCRIPTOR;

typedef struct _EFI_USB_IO_PROTOCOL EFI_USB_IO_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_USB_IO_CONTROL_TRANSFER)(
    IN     EFI_USB_IO_PROTOCOL *This,
    IN     void                *Request,
    IN     UINT32               Direction,
    IN     UINT32               Timeout,
    IN OUT void                *Data    OPTIONAL,
    IN     UINTN                DataLength OPTIONAL,
       OUT UINT32              *Status);

typedef EFI_STATUS (EFIAPI *EFI_USB_IO_BULK_TRANSFER)(
    IN     EFI_USB_IO_PROTOCOL *This,
    IN     UINT8                DeviceEndpoint,
    IN OUT void                *Data,
    IN OUT UINTN               *DataLength,
    IN     UINTN                Timeout,
       OUT UINT32              *Status);

typedef struct {
    UINT8  Length;
    UINT8  DescriptorType;
    UINT8  EndpointAddress;
    UINT8  Attributes;
    UINT16 MaxPacketSize;
    UINT8  Interval;
} EFI_USB_ENDPOINT_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_USB_IO_GET_DEVICE_DESCRIPTOR)(
    IN  EFI_USB_IO_PROTOCOL      *This,
    OUT EFI_USB_DEVICE_DESCRIPTOR *DeviceDescriptor);

typedef EFI_STATUS (EFIAPI *EFI_USB_IO_GET_ENDPOINT_DESCRIPTOR)(
    IN  EFI_USB_IO_PROTOCOL       *This,
    IN  UINT8                      EndpointIndex,
    OUT EFI_USB_ENDPOINT_DESCRIPTOR *EndpointDescriptor);

struct _EFI_USB_IO_PROTOCOL {
    EFI_USB_IO_CONTROL_TRANSFER          UsbControlTransfer;
    EFI_USB_IO_BULK_TRANSFER             UsbBulkTransfer;
    void                                *UsbAsyncInterruptTransfer;
    void                                *UsbSyncInterruptTransfer;
    void                                *UsbAsyncIsochronousTransfer;
    void                                *UsbSyncIsochronousTransfer;
    EFI_USB_IO_GET_DEVICE_DESCRIPTOR     UsbGetDeviceDescriptor;
    void                                *UsbGetConfigDescriptor;
    void                                *UsbGetInterfaceDescriptor;
    EFI_USB_IO_GET_ENDPOINT_DESCRIPTOR   UsbGetEndpointDescriptor;
    void                                *UsbGetStringDescriptor;
    void                                *UsbGetSupportedLanguages;
    void                                *UsbPortReset;
};
#endif /* EFI_USB_IO_PROTOCOL_GUID */

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
