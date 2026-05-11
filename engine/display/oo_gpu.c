/* oo_gpu.c — OO GPU Double-Buffer Engine  Phase 5H
 * ==================================================
 * Provides a back buffer in RAM → blit to GOP framebuffer on flip().
 * VirtIO-GPU detection for QEMU testing.
 * Freestanding C11. No libc.
 */
#include "oo_gpu.h"
#include <efilib.h>

OoGpu g_gpu;

/* Static back buffer — max 1920×1080 RGBA */
#define OO_FB_BACK_MAX (1920 * 1080 * 4)
static UINT32 _back_buf[1920 * 1080] __attribute__((aligned(64)));

/* ── VirtIO-GPU PCI detection ────────────────────────────────────────── */
#define PCI_CONF_ADDR2  0xCF8
#define PCI_CONF_DATA2  0xCFC

/* PCI config read — forward-declare if oo_nvme or oo_exit_boot already defined it */
#ifndef OO_PCI_READ32_DEFINED
#define OO_PCI_READ32_DEFINED
static UINT32 _pci_read32_gpu(UINT8 bus, UINT8 dev, UINT8 fn, UINT8 off) {
    UINT32 addr = 0x80000000U | ((UINT32)bus<<16) | ((UINT32)dev<<11)
                              | ((UINT32)fn<<8)   | (off & 0xFC);
    /* Write address then read data */
    __asm__ volatile("outl %%eax, %%dx" :: "a"(addr), "d"((UINT16)0xCF8));
    UINT32 v;
    __asm__ volatile("inl %%dx, %%eax" : "=a"(v) : "d"((UINT16)0xCFC));
    return v;
}
#endif /* OO_PCI_READ32_DEFINED */

static UINT64 _virtio_gpu_scan(void) {
    for (UINT8 bus = 0; bus < 8; bus++) {
        for (UINT8 dev = 0; dev < 32; dev++) {
            UINT32 id = _pci_read32_gpu(bus, dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            UINT16 vendor = id & 0xFFFF;
            UINT16 device = (id >> 16) & 0xFFFF;
            /* VirtIO-GPU: vendor=0x1AF4, device=0x1050 (or 0x1040+16) */
            if (vendor == 0x1AF4 && (device == 0x1050 || device == 0x1058)) {
                UINT32 bar0 = _pci_read32_gpu(bus, dev, 0, 0x10);
                if (bar0 & 1) return 0; /* I/O space, skip */
                UINT32 bar0hi = _pci_read32_gpu(bus, dev, 0, 0x14);
                if ((bar0 & 0x6) == 4) {
                    return ((UINT64)bar0hi << 32) | (bar0 & ~0xFULL);
                }
                return bar0 & ~0xFULL;
            }
        }
    }
    return 0;
}

/* ── Init via UEFI GOP ───────────────────────────────────────────────── */
EFI_STATUS oo_gpu_init(OoGpu *gpu, EFI_SYSTEM_TABLE *st) {
    for (int i = 0; i < sizeof(*gpu)/8; i++) ((UINT64*)gpu)[i] = 0;

    /* Try VirtIO-GPU first */
    gpu->virtio_base = _virtio_gpu_scan();
    if (gpu->virtio_base) {
        Print(L"[gpu] VirtIO-GPU @ 0x%lx\r\n", gpu->virtio_base);
        gpu->type = OO_GPU_VIRTIO;
    }

    /* GOP framebuffer (always use as primary) */
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = LibLocateProtocol(&gop_guid, (VOID**)&gop);
    if (EFI_ERROR(status) || !gop) {
        Print(L"[gpu] ⚠ GOP not found\r\n");
        return status;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    gpu->fb_front = (UINT32*)(UINTN)gop->Mode->FrameBufferBase;
    gpu->fb_phys  = gop->Mode->FrameBufferBase;
    gpu->fb_size  = gop->Mode->FrameBufferSize;
    gpu->width    = info->HorizontalResolution;
    gpu->height   = info->VerticalResolution;
    gpu->stride   = info->PixelsPerScanLine;
    gpu->fb_back  = _back_buf;
    gpu->type     = OO_GPU_GOP;
    gpu->initialized = 1;

    Print(L"[gpu] GOP %ux%u stride=%u fb=0x%lx size=%lu KB\r\n",
          gpu->width, gpu->height, gpu->stride,
          gpu->fb_phys, gpu->fb_size / 1024);
    Print(L"[gpu] Back buffer @ 0x%lx\r\n", (UINT64)(UINTN)_back_buf);

    /* Initial clear to black */
    oo_gpu_clear(gpu, OO_COLOR_BLACK);
    oo_gpu_flip(gpu);

    return EFI_SUCCESS;
}

/* ── Clear back buffer ───────────────────────────────────────────────── */
void oo_gpu_clear(OoGpu *gpu, UINT32 color) {
    if (!gpu->initialized) return;
    UINT32 *p = gpu->fb_back;
    UINT32 n = gpu->stride * gpu->height;
    for (UINT32 i = 0; i < n; i++) p[i] = color;
    gpu->draw_calls++;
}

/* ── Put pixel (back buffer) ─────────────────────────────────────────── */
void oo_gpu_put_pixel(OoGpu *gpu, int x, int y, UINT32 color) {
    if (!gpu->initialized) return;
    if ((UINT32)x >= gpu->width || (UINT32)y >= gpu->height) return;
    gpu->fb_back[y * gpu->stride + x] = color;
    gpu->draw_calls++;
}

/* ── Fill rectangle ──────────────────────────────────────────────────── */
void oo_gpu_fill_rect(OoGpu *gpu, int x, int y, int w, int h, UINT32 color) {
    if (!gpu->initialized) return;
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((UINT32)(x + w) > gpu->width)  w = gpu->width  - x;
    if ((UINT32)(y + h) > gpu->height) h = gpu->height - y;
    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; row++) {
        UINT32 *p = gpu->fb_back + row * gpu->stride + x;
        for (int col = 0; col < w; col++) p[col] = color;
    }
    gpu->draw_calls++;
}

/* ── Horizontal line ─────────────────────────────────────────────────── */
void oo_gpu_draw_hline(OoGpu *gpu, int x, int y, int len, UINT32 color) {
    oo_gpu_fill_rect(gpu, x, y, len, 1, color);
}

/* ── Vertical line ───────────────────────────────────────────────────── */
void oo_gpu_draw_vline(OoGpu *gpu, int x, int y, int len, UINT32 color) {
    oo_gpu_fill_rect(gpu, x, y, 1, len, color);
}

/* ── Flip: blit back buffer → front (GOP framebuffer) ─────────────────── */
void oo_gpu_flip(OoGpu *gpu) {
    if (!gpu->initialized) return;

    UINT32 *src = gpu->fb_back;
    UINT32 *dst = gpu->fb_front;
    UINT32  h   = gpu->height;

    if (gpu->stride == gpu->width) {
        /* Contiguous — single blit */
        UINT32 n = gpu->stride * h;
        for (UINT32 i = 0; i < n; i++) dst[i] = src[i];
    } else {
        /* Non-contiguous stride — row by row */
        for (UINT32 row = 0; row < h; row++) {
            UINT32 *s = src + row * gpu->stride;
            UINT32 *d = dst + row * gpu->stride;
            for (UINT32 col = 0; col < gpu->width; col++)
                d[col] = s[col];
        }
    }
    gpu->flip_count++;
}

/* ── Print ───────────────────────────────────────────────────────────── */
void oo_gpu_print(const OoGpu *gpu) {
    static const char *type_names[] = { "NONE","GOP","VIRTIO","INTEL" };
    Print(L"\r\n  [GPU]\r\n");
    Print(L"  Initialized  : %s\r\n", gpu->initialized ? L"YES" : L"NO");
    Print(L"  Type         : %a\r\n",
          gpu->type <= 3 ? type_names[gpu->type] : "?");
    Print(L"  Resolution   : %ux%u stride=%u\r\n",
          gpu->width, gpu->height, gpu->stride);
    Print(L"  FB physical  : 0x%lx (%lu KB)\r\n",
          gpu->fb_phys, gpu->fb_size / 1024);
    Print(L"  Back buffer  : 0x%lx\r\n", (UINT64)(UINTN)gpu->fb_back);
    Print(L"  Flips        : %lu\r\n", gpu->flip_count);
    Print(L"  Draw calls   : %lu\r\n", gpu->draw_calls);
    if (gpu->virtio_base)
        Print(L"  VirtIO base  : 0x%lx\r\n", gpu->virtio_base);
    Print(L"\r\n");
}

/* ── REPL ────────────────────────────────────────────────────────────── */
static int _gpu_cmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}

int oo_gpu_repl_cmd(OoGpu *gpu, const char *cmd) {
    if (!cmd) return 0;
    if (_gpu_cmp(cmd, "/gpu_status", 11) == 0) { oo_gpu_print(gpu); return 1; }
    if (_gpu_cmp(cmd, "/gpu_clear",  10) == 0) {
        oo_gpu_clear(gpu, OO_COLOR_BLACK);
        oo_gpu_flip(gpu);
        Print(L"[gpu] Screen cleared\r\n"); return 1;
    }
    if (_gpu_cmp(cmd, "/gpu_flip",   9) == 0) {
        oo_gpu_flip(gpu);
        Print(L"[gpu] Flip #%lu\r\n", gpu->flip_count); return 1;
    }
    if (_gpu_cmp(cmd, "/gpu_test",   9) == 0) {
        /* Draw a test pattern */
        oo_gpu_clear(gpu, OO_COLOR_DARKBLUE);
        /* Header bar */
        oo_gpu_fill_rect(gpu, 0, 0, gpu->width, 40, OO_COLOR_ORANGE);
        /* Vertical stripes */
        UINT32 stripe_w = gpu->width / 7;
        UINT32 colors[7] = {
            OO_COLOR_RED, OO_COLOR_GREEN, OO_COLOR_BLUE,
            OO_COLOR_YELLOW, OO_COLOR_CYAN, OO_COLOR_MAGENTA, OO_COLOR_WHITE
        };
        for (int i = 0; i < 7; i++) {
            oo_gpu_fill_rect(gpu, i * stripe_w, 50,
                             stripe_w, gpu->height - 100, colors[i]);
        }
        oo_gpu_flip(gpu);
        Print(L"[gpu] Test pattern rendered\r\n"); return 1;
    }
    if (_gpu_cmp(cmd, "/gpu_rect ", 10) == 0) {
        /* /gpu_rect x y w h color */
        int x=0,y=0,w=100,h=100; UINT32 color=OO_COLOR_WHITE;
        /* minimal parse */
        const char *p = cmd + 10;
        x = 0; while(*p>='0'&&*p<='9'){x=x*10+(*p-'0');p++;} if(*p==' ')p++;
        y = 0; while(*p>='0'&&*p<='9'){y=y*10+(*p-'0');p++;} if(*p==' ')p++;
        w = 0; while(*p>='0'&&*p<='9'){w=w*10+(*p-'0');p++;} if(*p==' ')p++;
        h = 0; while(*p>='0'&&*p<='9'){h=h*10+(*p-'0');p++;} if(*p==' ')p++;
        color = 0;
        if (p[0]=='0'&&p[1]=='x') p+=2;
        while(*p){UINT8 n=0;if(*p>='0'&&*p<='9')n=*p-'0';
                  else if(*p>='a'&&*p<='f')n=*p-'a'+10;
                  else if(*p>='A'&&*p<='F')n=*p-'A'+10;
                  else break;color=(color<<4)|n;p++;}
        oo_gpu_fill_rect(gpu, x, y, w, h, color);
        oo_gpu_flip(gpu);
        Print(L"[gpu] rect (%d,%d) %dx%d col=0x%08x\r\n",x,y,w,h,color);
        return 1;
    }
    return 0;
}
