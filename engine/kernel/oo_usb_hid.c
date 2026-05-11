/* oo_usb_hid.c — USB HID keyboard  Phase 5D
 * ==========================================
 * Phase 1: PS/2 keyboard fallback via I/O ports (always available)
 * Phase 2: XHCI USB interrupt polling (post-EBS)
 * Freestanding C11. No libc.
 */
#include "oo_usb_hid.h"

OoUsbHid g_usb_hid;

/* ── PS/2 I/O ports (universal fallback) ─────────────────────────────── */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64

static inline UINT8 _inb(UINT16 port) {
    UINT8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Minimal HID scancode → ASCII (US layout, lowercase) */
static const UINT8 _hid2ascii[128] = {
    /*00*/ 0,  0,  0,  0, 'a','b','c','d','e','f','g','h','i','j','k','l',
    /*10*/ 'm','n','o','p','q','r','s','t','u','v','w','x','y','z','1','2',
    /*20*/ '3','4','5','6','7','8','9','0','\n',0x1B,'\b','\t',' ','-','=','[',
    /*30*/ ']','\\','#',';','\'','`',',','.','/', 0,  0,  0,  0,  0,  0,  0,
    /*40*/ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    /*50*/ 0,  0,  0,  0,  0, '/','*','-','+','\n','1','2','3','4','5','6',
    /*60*/ '7','8','9','0','.', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    /*70*/ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static const UINT8 _hid2ascii_shift[128] = {
    /*00*/ 0,  0,  0,  0, 'A','B','C','D','E','F','G','H','I','J','K','L',
    /*10*/ 'M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','!','@',
    /*20*/ '#','$','%','^','&','*','(',')' ,'\n',0x1B,'\b','\t',' ','_','+','{',
    /*30*/ '}','|','~',':','"','~','<','>','?', 0,  0,  0,  0,  0,  0,  0,
    /*40*/ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    /*50*/ 0,  0,  0,  0,  0, '/','*','-','+','\n','1','2','3','4','5','6',
    /*60*/ '7','8','9','0','.', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    /*70*/ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static void _hid_push(OoUsbHid *ctx, UINT8 ch) {
    UINT32 next = (ctx->key_head + 1) % OO_USB_KEY_BUFSZ;
    if (next != ctx->key_tail) {
        ctx->key_buf[ctx->key_head] = ch;
        ctx->key_head = next;
    }
}

/* ── XHCI scan (detect only, no full driver yet) ────────────────────── */
#define PCI_CONF_ADDR 0xCF8
#define PCI_CONF_DATA 0xCFC

static inline UINT32 _pci_read32(UINT8 bus, UINT8 dev, UINT8 fn, UINT8 off) {
    UINT32 addr = 0x80000000 | ((UINT32)bus<<16) | ((UINT32)dev<<11)
                             | ((UINT32)fn<<8)  | (off & 0xFC);
    UINT32 v;
    __asm__ volatile(
        "outl %1, %2\n\t"
        "inl  %3, %0\n\t"
        : "=a"(v) : "a"(addr), "Nd"((UINT16)PCI_CONF_ADDR),
                    "Nd"((UINT16)PCI_CONF_DATA));
    return v;
}

static UINT64 _xhci_scan(void) {
    for (UINT32 bus = 0; bus < 8; bus++) {
        for (UINT32 dev = 0; dev < 32; dev++) {
            for (UINT32 fn = 0; fn < 8; fn++) {
                UINT32 id = _pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                UINT32 cls = _pci_read32(bus, dev, fn, 8);
                UINT8 base_cls  = (cls >> 24) & 0xFF;
                UINT8 sub_cls   = (cls >> 16) & 0xFF;
                UINT8 prog_if   = (cls >>  8) & 0xFF;
                if (base_cls == 0x0C && sub_cls == 0x03 && prog_if == 0x30) {
                    /* XHCI found — read BAR0 (64-bit capable) */
                    UINT32 bar0_lo = _pci_read32(bus, dev, fn, 0x10);
                    UINT32 bar0_hi = _pci_read32(bus, dev, fn, 0x14);
                    if ((bar0_lo & 0x6) == 0x4) {
                        /* 64-bit BAR */
                        return ((UINT64)bar0_hi << 32) | (bar0_lo & ~0xFULL);
                    }
                    return (UINT64)(bar0_lo & ~0xFULL);
                }
            }
        }
    }
    return 0;
}

/* ── Init ────────────────────────────────────────────────────────────── */
EFI_STATUS oo_usb_hid_init(OoUsbHid *ctx) {
    for (int i = 0; i < sizeof(*ctx); i++) ((UINT8*)ctx)[i] = 0;

    /* Try to find XHCI */
    ctx->xhci_base = _xhci_scan();
    if (ctx->xhci_base) {
        Print(L"[usb_hid] XHCI found @ 0x%lx\r\n", ctx->xhci_base);
    } else {
        Print(L"[usb_hid] No XHCI — using PS/2 fallback\r\n");
    }

    /* Always enable PS/2 as fallback */
    ctx->initialized = 1;
    Print(L"[usb_hid] Initialized (PS/2 + XHCI stub)\r\n");
    return EFI_SUCCESS;
}

/* ── Poll PS/2 keyboard ──────────────────────────────────────────────── */
int oo_usb_hid_poll(OoUsbHid *ctx) {
    if (!ctx->initialized) return 0;
    int count = 0;

    /* PS/2: read all available scancodes from port 0x60 */
    while (_inb(PS2_STATUS) & 0x01) {
        UINT8 sc = _inb(PS2_DATA);
        if (sc & 0x80) continue; /* key release — ignore */

        /* Simplified PS/2 Set 1 → ASCII (very minimal) */
        static const UINT8 sc1_ascii[128] = {
            0,0x1B,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
            'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
            'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
            'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
        };
        if (sc < 58 && sc1_ascii[sc]) {
            _hid_push(ctx, sc1_ascii[sc]);
            count++;
        }
    }
    return count;
}

/* ── Getchar ─────────────────────────────────────────────────────────── */
int oo_usb_hid_getchar(OoUsbHid *ctx) {
    if (ctx->key_head == ctx->key_tail) return -1;
    UINT8 ch = ctx->key_buf[ctx->key_tail];
    ctx->key_tail = (ctx->key_tail + 1) % OO_USB_KEY_BUFSZ;
    return ch;
}

/* ── Print ───────────────────────────────────────────────────────────── */
void oo_usb_hid_print(const OoUsbHid *ctx) {
    Print(L"\r\n  [USB HID]\r\n");
    Print(L"  Initialized : %s\r\n", ctx->initialized ? L"YES" : L"NO");
    Print(L"  XHCI base   : 0x%lx\r\n", ctx->xhci_base);
    UINT32 pending = (ctx->key_head - ctx->key_tail + OO_USB_KEY_BUFSZ)
                      % OO_USB_KEY_BUFSZ;
    Print(L"  Keys buffered: %u\r\n\r\n", pending);
}

/* ── REPL ────────────────────────────────────────────────────────────── */
static int _cmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}

int oo_usb_hid_repl_cmd(OoUsbHid *ctx, const char *cmd) {
    if (!cmd) return 0;
    if (_cmp(cmd, "/hid_status", 11) == 0) { oo_usb_hid_print(ctx); return 1; }
    if (_cmp(cmd, "/hid_init",    9) == 0) {
        oo_usb_hid_init(ctx); return 1;
    }
    if (_cmp(cmd, "/hid_poll",    9) == 0) {
        int n = oo_usb_hid_poll(ctx);
        Print(L"[hid] Polled %d chars\r\n", n); return 1;
    }
    return 0;
}
