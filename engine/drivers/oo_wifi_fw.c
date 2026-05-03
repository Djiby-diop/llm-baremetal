/* oo_wifi_fw.c — USB WiFi firmware loader for bare-metal OO
 * Detects RTL8188EU/RTL8192EU/MT7601U via EFI_USB_IO_PROTOCOL, uploads
 * firmware in 4KB chunks via UsbBulkTransfer.
 * Freestanding C11. No libc, no malloc.
 */
#include "oo_wifi_fw.h"

/* GUID for EFI_USB_IO_PROTOCOL */
static EFI_GUID oo_usb_io_guid = EFI_USB_IO_PROTOCOL_GUID;

/* ── CRC32 (bit-by-bit, polynomial 0xEDB88320, no lookup table) ─────────── */
static uint32_t oo_wifi_crc32_byte(uint32_t crc, uint8_t byte)
{
    crc ^= (uint32_t)byte;
    for (int bit = 0; bit < 8; bit++) {
        if (crc & 1u)
            crc = (crc >> 1) ^ 0xEDB88320u;
        else
            crc >>= 1;
    }
    return crc;
}

static uint32_t oo_wifi_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++)
        crc = oo_wifi_crc32_byte(crc, data[i]);
    return crc ^ 0xFFFFFFFFu;
}

/* ── VID/PID matching ───────────────────────────────────────────────────── */
static OoWifiFwChip oo_wifi_match(uint16_t vid, uint16_t pid)
{
    if (vid == OO_WIFI_VID_REALTEK) {
        if (pid == OO_WIFI_PID_RTL8188EU) return OO_WIFI_FW_RTL8188EU;
        if (pid == OO_WIFI_PID_RTL8192EU) return OO_WIFI_FW_RTL8192EU;
    }
    if (vid == OO_WIFI_VID_MEDIATEK) {
        if (pid == OO_WIFI_PID_MT7601U)  return OO_WIFI_FW_MT7601U;
    }
    return OO_WIFI_FW_NONE;
}

/* ── Bulk OUT endpoint discovery ────────────────────────────────────────── */
static int oo_wifi_find_bulk_out(EFI_USB_IO_PROTOCOL *usb_io)
{
    /* Walk interface descriptors looking for a bulk-OUT endpoint.
     * EFI_USB_IO_PROTOCOL exposes UsbGetEndpointDescriptor indexed 0..N-1.
     * We try up to 16 endpoint indices; stop at error. */
    for (UINT8 idx = 0; idx < 16; idx++) {
        EFI_USB_ENDPOINT_DESCRIPTOR ep;
        EFI_STATUS st = usb_io->UsbGetEndpointDescriptor(usb_io, idx, &ep);
        if (EFI_ERROR(st))
            break;
        /* bmAttributes bits[1:0] == 2 → Bulk; bit7 of bEndpointAddress == 0 → OUT */
        if ((ep.Attributes & 0x03u) == 2u && !(ep.EndpointAddress & 0x80u))
            return (int)(ep.EndpointAddress & 0xFFu);
    }
    return 0x02; /* default bulk-OUT endpoint if discovery fails */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int oo_wifi_fw_init(OoWifiFw *fw)
{
    EFI_HANDLE *handle_buf = NULL;
    UINTN       buf_size   = 0;
    EFI_STATUS  status;

    fw->n_devices   = 0;
    fw->initialized = 0;

    status = gBS->LocateHandleBuffer(ByProtocol,
                                     &oo_usb_io_guid,
                                     NULL,
                                     &buf_size,
                                     &handle_buf);
    if (EFI_ERROR(status) || buf_size == 0)
        goto done;

    for (UINTN i = 0; i < buf_size && fw->n_devices < OO_WIFI_FW_MAX_HANDLES; i++) {
        EFI_USB_IO_PROTOCOL *usb_io = NULL;
        status = gBS->HandleProtocol(handle_buf[i], &oo_usb_io_guid, (VOID **)&usb_io);
        if (EFI_ERROR(status) || usb_io == NULL)
            continue;

        EFI_USB_DEVICE_DESCRIPTOR desc;
        status = usb_io->UsbGetDeviceDescriptor(usb_io, &desc);
        if (EFI_ERROR(status))
            continue;

        OoWifiFwChip chip = oo_wifi_match((uint16_t)desc.IdVendor, (uint16_t)desc.IdProduct);
        if (chip == OO_WIFI_FW_NONE)
            continue;

        OoWifiFwDev *dev = &fw->devices[fw->n_devices++];
        dev->handle      = handle_buf[i];
        dev->chip        = chip;
        dev->vid         = (uint16_t)desc.IdVendor;
        dev->pid         = (uint16_t)desc.IdProduct;
        dev->fw_loaded   = 0;
        dev->fw_crc32    = 0;
        dev->bytes_sent  = 0;
        dev->bulk_ep_out = oo_wifi_find_bulk_out(usb_io);
    }

    gBS->FreePool(handle_buf);

done:
    fw->initialized = 1;
    return fw->n_devices;
}

int oo_wifi_fw_upload(OoWifiFw *fw, int dev_idx, const uint8_t *fw_blob, uint32_t fw_size)
{
    if (!fw || dev_idx < 0 || dev_idx >= fw->n_devices)
        return -1;
    if (!fw_blob || fw_size == 0)
        return -2;

    OoWifiFwDev *dev = &fw->devices[dev_idx];

    EFI_USB_IO_PROTOCOL *usb_io = NULL;
    EFI_STATUS status = gBS->HandleProtocol(dev->handle, &oo_usb_io_guid, (VOID **)&usb_io);
    if (EFI_ERROR(status) || usb_io == NULL)
        return -3;

    uint32_t offset     = 0;
    uint32_t total_sent = 0;

    while (offset < fw_size) {
        uint32_t chunk = fw_size - offset;
        if (chunk > OO_WIFI_FW_CHUNK_SIZE)
            chunk = OO_WIFI_FW_CHUNK_SIZE;

        UINTN     transfer_len = (UINTN)chunk;
        UINT32    usb_status   = 0;

        status = usb_io->UsbBulkTransfer(usb_io,
                                         (UINT8)dev->bulk_ep_out,
                                         (VOID *)(fw_blob + offset),
                                         &transfer_len,
                                         5000,       /* 5-second timeout */
                                         &usb_status);
        if (EFI_ERROR(status))
            return -4;

        offset      += (uint32_t)transfer_len;
        total_sent  += (uint32_t)transfer_len;
    }

    dev->bytes_sent = total_sent;
    dev->fw_crc32   = oo_wifi_crc32(fw_blob, fw_size);
    dev->fw_loaded  = 1;
    return 0;
}

int oo_wifi_fw_auto(OoWifiFw *fw, const uint8_t *fw_blob, uint32_t fw_size)
{
    if (!fw || !fw->initialized)
        return 0;

    int ok = 0;
    for (int i = 0; i < fw->n_devices; i++) {
        if (fw->devices[i].fw_loaded)
            continue;
        if (oo_wifi_fw_upload(fw, i, fw_blob, fw_size) == 0)
            ok++;
    }
    return ok;
}

void oo_wifi_fw_print_status(const OoWifiFw *fw, void (*fn)(const char *))
{
    fn("[WiFi-FW] status:");
    fn("  initialized=");
    fn(fw->initialized ? "1" : "0");
    fn("  devices=");

    char tmp[24];
    int  v   = fw->n_devices;
    int  idx = 23;
    tmp[idx] = '\0';
    if (v == 0) {
        tmp[--idx] = '0';
    } else {
        while (v > 0) { tmp[--idx] = (char)('0' + (v % 10)); v /= 10; }
    }
    fn(&tmp[idx]);
    fn("\n");

    for (int i = 0; i < fw->n_devices; i++) {
        const OoWifiFwDev *d = &fw->devices[i];
        fn("  [dev] fw_loaded=");
        fn(d->fw_loaded ? "1" : "0");
        fn("\n");
    }
}
