/* oo_gpu.h — OO GPU / Framebuffer Acceleration  Phase 5H
 * ========================================================
 * Phase 1: Double-buffered GOP framebuffer (vsync flag)
 * Phase 2: VirtIO-GPU for QEMU
 * Phase 3: Intel HD / AMD stub (2D MMIO)
 * Freestanding C11.
 */
#pragma once
#include <efi.h>

#define OO_GPU_MAX_WIDTH   3840
#define OO_GPU_MAX_HEIGHT  2160

typedef enum {
    OO_GPU_NONE    = 0,
    OO_GPU_GOP     = 1,   /* UEFI GOP double-buffer */
    OO_GPU_VIRTIO  = 2,   /* VirtIO-GPU (QEMU) */
    OO_GPU_INTEL   = 3,   /* Intel HD stub */
} OoGpuType;

typedef struct {
    int        initialized;
    OoGpuType  type;
    UINT32    *fb_front;   /* physical framebuffer (GOP) */
    UINT32    *fb_back;    /* back buffer (in RAM) */
    UINT32     width;
    UINT32     height;
    UINT32     stride;     /* pixels per row */
    UINT64     fb_phys;    /* physical base for MMU mapping */
    UINT64     fb_size;    /* bytes */
    UINT64     flip_count;
    UINT64     virtio_base;
    UINT64     draw_calls;
} OoGpu;

#define OO_COLOR_BLACK    0x00000000
#define OO_COLOR_WHITE    0xFFFFFFFF
#define OO_COLOR_RED      0xFFFF0000
#define OO_COLOR_GREEN    0xFF00FF00
#define OO_COLOR_BLUE     0xFF0000FF
#define OO_COLOR_YELLOW   0xFFFFFF00
#define OO_COLOR_CYAN     0xFF00FFFF
#define OO_COLOR_MAGENTA  0xFFFF00FF
#define OO_COLOR_ORANGE   0xFFFF8000
#define OO_COLOR_GRAY     0xFF808080
#define OO_COLOR_DARKBLUE 0xFF000080

EFI_STATUS oo_gpu_init(OoGpu *gpu, EFI_SYSTEM_TABLE *st);
void       oo_gpu_clear(OoGpu *gpu, UINT32 color);
void       oo_gpu_put_pixel(OoGpu *gpu, int x, int y, UINT32 color);
void       oo_gpu_fill_rect(OoGpu *gpu, int x, int y, int w, int h, UINT32 color);
void       oo_gpu_draw_hline(OoGpu *gpu, int x, int y, int len, UINT32 color);
void       oo_gpu_draw_vline(OoGpu *gpu, int x, int y, int len, UINT32 color);
void       oo_gpu_flip(OoGpu *gpu);
void       oo_gpu_print(const OoGpu *gpu);
int        oo_gpu_repl_cmd(OoGpu *gpu, const char *cmd);

extern OoGpu g_gpu;
