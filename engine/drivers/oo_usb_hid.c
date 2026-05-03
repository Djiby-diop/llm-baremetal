/* oo_usb_hid.c — USB HID keyboard driver for bare-metal OO
 * Polls EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL handles discovered at init.
 * Freestanding C11. No libc, no malloc.
 */
#include "oo_usb_hid.h"

/* GUID for EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL */
static EFI_GUID oo_usb_hid_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

int oo_usb_hid_init(OoUsbHid *h)
{
    EFI_HANDLE  *handle_buf  = NULL;
    UINTN        buf_size     = 0;
    EFI_STATUS   status;

    h->n_handles   = 0;
    h->initialized = 0;
    h->head        = 0;
    h->tail        = 0;
    h->total_keys  = 0;

    status = gBS->LocateHandleBuffer(ByProtocol,
                                     &oo_usb_hid_guid,
                                     NULL,
                                     &buf_size,
                                     &handle_buf);
    if (!EFI_ERROR(status) && buf_size > 0) {
        UINTN i;
        for (i = 0; i < buf_size && h->n_handles < OO_USB_HID_MAX_HANDLES; i++) {
            h->handles[h->n_handles++] = handle_buf[i];
        }
        gBS->FreePool(handle_buf);
    } else {
        /* Fall back to the primary console input handle */
        if (gST->ConsoleInHandle != NULL) {
            h->handles[h->n_handles++] = gST->ConsoleInHandle;
        }
    }

    h->initialized = 1;
    return h->n_handles;
}

int oo_usb_hid_poll(OoUsbHid *h)
{
    int added = 0;

    if (!h->initialized)
        return 0;

    for (int i = 0; i < h->n_handles; i++) {
        EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *proto = NULL;
        EFI_STATUS status = gBS->HandleProtocol(h->handles[i],
                                                &oo_usb_hid_guid,
                                                (VOID **)&proto);
        if (EFI_ERROR(status) || proto == NULL)
            continue;

        /* Drain all available keystrokes from this handle */
        for (;;) {
            EFI_KEY_DATA key_data;
            status = proto->ReadKeyStrokeEx(proto, &key_data);
            if (EFI_ERROR(status))
                break;

            /* Convert Unicode to 7-bit ASCII; discard non-ASCII */
            CHAR16 uc = key_data.Key.UnicodeChar;
            char   ch = (char)(uc & 0x7F);
            if (ch == 0)
                continue;

            /* Push into ring buffer if space available */
            int next_tail = (h->tail + 1) % OO_USB_HID_BUF_SIZE;
            if (next_tail != h->head) {
                h->buf[h->tail] = ch;
                h->tail = next_tail;
                h->total_keys++;
                added = 1;
            }
        }
    }

    return added;
}

char oo_usb_hid_read_char(OoUsbHid *h)
{
    if (h->head == h->tail)
        return 0;

    char c = h->buf[h->head];
    h->head = (h->head + 1) % OO_USB_HID_BUF_SIZE;
    return c;
}

int oo_usb_hid_has_data(const OoUsbHid *h)
{
    return (h->head != h->tail) ? 1 : 0;
}

void oo_usb_hid_print_status(const OoUsbHid *h, void (*fn)(const char *))
{
    fn("[USB-HID] status:");
    fn("  initialized=");
    fn(h->initialized ? "1" : "0");
    fn("  handles=");
    /* Simple decimal print without libc */
    char tmp[24];
    int  v   = h->n_handles;
    int  idx = 23;
    tmp[idx] = '\0';
    if (v == 0) {
        tmp[--idx] = '0';
    } else {
        while (v > 0) {
            tmp[--idx] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    fn(&tmp[idx]);
    fn("  pending=");
    int pending = (h->tail - h->head + OO_USB_HID_BUF_SIZE) % OO_USB_HID_BUF_SIZE;
    v = pending; idx = 23; tmp[idx] = '\0';
    if (v == 0) { tmp[--idx] = '0'; } else { while (v > 0) { tmp[--idx] = (char)('0' + (v % 10)); v /= 10; } }
    fn(&tmp[idx]);
    fn("\n");
}
