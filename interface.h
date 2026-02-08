#ifndef INTERFACE_H
#define INTERFACE_H

#include <efi.h>
#include <efilib.h>

/*
 * Cyberpunk UI for LLM-BAREMETAL
 * Goal: avoid blurry images and use a lightweight animated background during
 * long loading (scanlines + particles + progress bar).
 *
 * Notes:
 * - This is intentionally very cheap to render in UEFI.
 * - We do NOT clear the entire screen every frame; we draw a subtle overlay.
 */

// Cyberpunk Color Palette (B, G, R, Reserved)
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorBlack       = {0, 0, 0, 0};
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorNeonCyan    = {255, 255, 0, 0};    // 00FFFF
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorNeonMagenta = {255, 0, 255, 0};    // FF00FF
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorDarkBlue    = {30, 20, 10, 0};     // dark navy
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorScanDark    = {10, 8, 5, 0};
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorGreen       = {0, 255, 0, 0};      // Matrix green
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorWhite       = {255, 255, 255, 0};
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorNeonOrange  = {0, 127, 255, 0};    // FF7F00 (Orange)
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorHudTeal     = {200, 240, 0, 0};    // Teal/Cyan HUD color
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColorDeepSpace   = {20, 10, 5, 0};      // Very dark blue background

// Static helper data for BMP loading
#pragma pack(1)
typedef struct {
    UINT16 Type;
    UINT32 Size;
    UINT16 Reserved1;
    UINT16 Reserved2;
    UINT32 Offset;
} BMP_FILE_HEADER;

typedef struct {
    UINT32 Size;
    INT32  Width;
    INT32  Height;
    UINT16 Planes;
    UINT16 BitCount;
    UINT32 Compression;
    UINT32 SizeImage;
    INT32  XPelsPerMeter;
    INT32  YPelsPerMeter;
    UINT32 ClrUsed;
    UINT32 ClrImportant;
} BMP_INFO_HEADER;
#pragma pack()

// Helper: Open file (SimpleFileSystem)
static EFI_STATUS Interface_OpenFile(EFI_HANDLE ImageHandle, CHAR16 *Path, EFI_FILE_HANDLE *FileHandle) {
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_STATUS Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &LoadedImageProtocol, (void **)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &FileSystemProtocol, (void **)&FileSystem);
    if (EFI_ERROR(Status)) return Status;

    EFI_FILE_HANDLE Root;
    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(Root->Open, 5, Root, FileHandle, Path, EFI_FILE_MODE_READ, 0);
    return Status;
}

// Best-effort: read an integer value from repl.cfg (ASCII) by key.
// Format expected: key=value (one per line). Returns 1 if found.
static int Interface_ReadCfgU32(EFI_HANDLE ImageHandle, const char *key, UINT32 *out_value) {
    if (!key || !out_value) return 0;
    *out_value = 0;

    EFI_FILE_HANDLE File;
    EFI_STATUS Status = Interface_OpenFile(ImageHandle, L"repl.cfg", &File);
    if (EFI_ERROR(Status)) return 0;

    // Read a small prefix of the file (config is expected to be tiny).
    // 4KB cap keeps it safe and fast.
    UINTN cap = 4096;
    CHAR8 *buf = AllocatePool(cap + 1);
    if (!buf) {
        uefi_call_wrapper(File->Close, 1, File);
        return 0;
    }
    UINTN sz = cap;
    Status = uefi_call_wrapper(File->Read, 3, File, &sz, buf);
    uefi_call_wrapper(File->Close, 1, File);
    if (EFI_ERROR(Status) || sz == 0) {
        FreePool(buf);
        return 0;
    }
    buf[sz] = 0;

    // Parse lines.
    UINTN key_len = 0;
    while (key[key_len]) key_len++;

    for (UINTN i = 0; i < sz; ) {
        // Skip whitespace/newlines
        while (i < sz && (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ' || buf[i] == '\t')) i++;
        if (i >= sz) break;

        // Skip comments
        if (buf[i] == '#') {
            while (i < sz && buf[i] != '\n') i++;
            continue;
        }

        // Match key
        UINTN j = 0;
        while (j < key_len && (i + j) < sz && buf[i + j] == (CHAR8)key[j]) j++;
        if (j == key_len && (i + j) < sz && buf[i + j] == '=') {
            i = i + j + 1;
            // Parse unsigned integer
            UINT32 v = 0;
            int any = 0;
            while (i < sz && buf[i] >= '0' && buf[i] <= '9') {
                any = 1;
                UINT32 d = (UINT32)(buf[i] - '0');
                v = v * 10u + d;
                i++;
            }
            FreePool(buf);
            if (!any) return 0;
            *out_value = v;
            return 1;
        }

        // Skip rest of line
        while (i < sz && buf[i] != '\n') i++;
    }

    FreePool(buf);
    return 0;
}

// Helper: Draw Rect
static void Interface_DrawRect(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color) {
    if (!Gop || !Gop->Mode || !Gop->Mode->Info) return;
    if (W == 0 || H == 0) return;

    UINT32 ScreenW = Gop->Mode->Info->HorizontalResolution;
    UINT32 ScreenH = Gop->Mode->Info->VerticalResolution;

    // Robust clipping: avoid underflow/overflow and out-of-range blits.
    if (X >= ScreenW || Y >= ScreenH) return;

    UINT64 x2 = (UINT64)X + (UINT64)W;
    UINT64 y2 = (UINT64)Y + (UINT64)H;
    if (x2 > (UINT64)ScreenW) W = ScreenW - X;
    if (y2 > (UINT64)ScreenH) H = ScreenH - Y;
    if (W == 0 || H == 0) return;

    uefi_call_wrapper(Gop->Blt, 10, Gop, &Color, EfiBltVideoFill, 0, 0, X, Y, W, H, 0);
}

// Helper: Draw Border
static void Interface_DrawBorder(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 Width, UINT32 Height, UINT32 Thickness, EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color) {
    // Top
    Interface_DrawRect(Gop, 0, 0, Width, Thickness, Color);
    // Bottom
    Interface_DrawRect(Gop, 0, Height - Thickness, Width, Thickness, Color);
    // Left
    Interface_DrawRect(Gop, 0, 0, Thickness, Height, Color);
    // Right
    Interface_DrawRect(Gop, Width - Thickness, 0, Thickness, Height, Color);
}

// Load and Draw BMP
static EFI_STATUS Interface_DrawBMP(EFI_HANDLE ImageHandle, EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, CHAR16 *Path) {
    EFI_FILE_HANDLE File;
    EFI_STATUS Status = Interface_OpenFile(ImageHandle, Path, &File);
    if (EFI_ERROR(Status)) return Status;

    // Read Header
    BMP_FILE_HEADER FileHeader;
    UINTN Size = sizeof(BMP_FILE_HEADER);
    Status = uefi_call_wrapper(File->Read, 3, File, &Size, &FileHeader);
    if (EFI_ERROR(Status) || FileHeader.Type != 0x4D42) {
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_UNSUPPORTED;
    }

    BMP_INFO_HEADER InfoHeader;
    Size = sizeof(BMP_INFO_HEADER);
    Status = uefi_call_wrapper(File->Read, 3, File, &Size, &InfoHeader);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    // Validate format: 24-bit uncompressed BMP only (simple fast path)
    if (InfoHeader.BitCount != 24 || InfoHeader.Compression != 0 || InfoHeader.Width <= 0 || InfoHeader.Height == 0) {
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_UNSUPPORTED;
    }

    INT32 bmp_w = InfoHeader.Width;
    INT32 bmp_h = InfoHeader.Height;
    int top_down = 0;
    if (bmp_h < 0) {
        top_down = 1;
        bmp_h = -bmp_h;
    }

    // Move to pixel data
    uefi_call_wrapper(File->SetPosition, 2, File, FileHeader.Offset);

    // Allocation for one row (assuming 24-bit BGR)
    // NOTE: This simple parser assumes 24-bit uncompressed BMP, standard bottom-up
    UINT32 RowSize = ((UINT32)bmp_w * 3u + 3u) & ~3u; // Padding to 4 bytes
    UINT8 *RowBuffer = AllocatePool(RowSize);
    if (!RowBuffer) {
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_OUT_OF_RESOURCES;
    }

    // Calculate Center
    UINT32 ScreenW = Gop->Mode->Info->HorizontalResolution;
    UINT32 ScreenH = Gop->Mode->Info->VerticalResolution;
    UINT32 StartX = (ScreenW > (UINT32)bmp_w) ? (ScreenW - (UINT32)bmp_w) / 2 : 0;
    UINT32 StartY = (ScreenH > (UINT32)bmp_h) ? (ScreenH - (UINT32)bmp_h) / 2 : 0;

    // Draw lines (Bottom-Up)
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *LinePixels = AllocatePool((UINTN)bmp_w * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    if (!LinePixels) {
        FreePool(RowBuffer);
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_OUT_OF_RESOURCES;
    }

    for (INT32 y = 0; y < bmp_h; y++) {
        UINTN ReadSize = RowSize;
        Status = uefi_call_wrapper(File->Read, 3, File, &ReadSize, RowBuffer);
        if (EFI_ERROR(Status) || ReadSize != RowSize) {
            FreePool(RowBuffer);
            FreePool(LinePixels);
            uefi_call_wrapper(File->Close, 1, File);
            return EFI_DEVICE_ERROR;
        }
        
        // Convert BGR to BltPixel
        for (INT32 x = 0; x < bmp_w; x++) {
            UINT32 Offset = x * 3;
            LinePixels[x].Blue = RowBuffer[Offset];
            LinePixels[x].Green = RowBuffer[Offset + 1];
            LinePixels[x].Red = RowBuffer[Offset + 2];
            LinePixels[x].Reserved = 0;
        }

        // Blt line
        UINT32 dst_y = 0;
        if (top_down) {
            dst_y = StartY + (UINT32)y;
        } else {
            // BMP is stored bottom-up
            dst_y = StartY + (UINT32)(bmp_h - 1 - y);
        }
        uefi_call_wrapper(Gop->Blt, 10, Gop, LinePixels, EfiBltBufferToVideo, 
            0, 0, 
            StartX, dst_y,
            (UINT32)bmp_w, 1,
            0);
    }

    FreePool(RowBuffer);
    FreePool(LinePixels);
    uefi_call_wrapper(File->Close, 1, File);
    return EFI_SUCCESS;
}

// --------------------------------------------------------------------------
// Minimal Splash Image (Static Image Only)
// --------------------------------------------------------------------------

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    UINT32 ScreenW;
    UINT32 ScreenH;
    int Active;

    // Loading overlay state
    UINT32 OverlayEnabled;
    UINT32 OverlayPosTop;
    UINT32 OverlayMaxW;
    UINT32 OverlayDigits;
    UINT32 OverlayTime;
    UINT32 OverlayTimeMode;
    UINT32 OverlayX;
    UINT32 OverlayY;
    UINT32 OverlayW;
    UINT32 OverlayH;
    UINT32 StageIndex1;
    UINT32 StageCount;
    UINT32 Permille;
    UINT32 LastDrawPermille;
    UINT32 LastDrawStageIndex1;
    UINT32 TimeDeltaMs;
    UINT32 TimeTotalMs;
    UINT32 LastDrawTimeDeltaMs;
    UINT32 LastDrawTimeTotalMs;

    // ETA mode (seconds resolution; derived from progress + wallclock)
    UINT32 StageStartSec;
    UINT32 ElapsedSec;
    UINT32 EtaSec;
    UINT32 LastDrawElapsedSec;
    UINT32 LastDrawEtaSec;
    UINT32 Anim;
    
    // Warp Effect State
    struct {
        INT32 X, Y;     // Position relative to center (scaled by 100)
        INT32 VelX, VelY; // Velocity
    } Stars[64];
} InterfaceFxState;

static InterfaceFxState g_ifx = {0};

// Simple LCG PRNG for effects
static UINT32 g_seed = 123456789;
static UINT32 Interface_Rand(void) {
    g_seed = g_seed * 1103515245 + 12345;
    return (g_seed / 65536) % 32768;
}

static int InterfaceFx_NowSeconds(UINT32 *out_sec) {
    if (!out_sec) return 0;
    *out_sec = 0;
    if (!RT || !RT->GetTime) return 0;
    EFI_TIME t;
    EFI_STATUS st = uefi_call_wrapper(RT->GetTime, 2, &t, NULL);
    if (EFI_ERROR(st)) return 0;
    *out_sec = (UINT32)t.Hour * 3600u + (UINT32)t.Minute * 60u + (UINT32)t.Second;
    return 1;
}

static EFI_STATUS InterfaceFx_Begin(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    if (!SystemTable) return EFI_INVALID_PARAMETER;
    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS Status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &GopGuid, NULL, (void **)&g_ifx.Gop);
    if (EFI_ERROR(Status) || !g_ifx.Gop || !g_ifx.Gop->Mode || !g_ifx.Gop->Mode->Info) {
        g_ifx.Active = 0;
        return Status;
    }

    g_ifx.ScreenW = g_ifx.Gop->Mode->Info->HorizontalResolution;
    g_ifx.ScreenH = g_ifx.Gop->Mode->Info->VerticalResolution;
    g_ifx.Active = 1;

    // Overlay config (repl.cfg):
    //   overlay=0/1           (default 0; off for simplicity)
    //   overlay_top=0/1       (default 0; bottom)
    //   overlay_max_w=<px>    (default 900)
    //   overlay_h=<px>        (default 26)
    //   overlay_digits=0/1    (default 1; show stage x/y)
    //   overlay_time=0/1      (default 1; show timing digits)
    //   overlay_time_mode=0/1/2  (default 1; 0=off 1=ms delta/total 2=ETA/elapsed seconds)
    g_ifx.OverlayEnabled = 0;
    g_ifx.OverlayPosTop = 0;
    g_ifx.OverlayMaxW = 900;
    g_ifx.OverlayDigits = 1;
    g_ifx.OverlayTime = 1;
    g_ifx.OverlayTimeMode = 1;
    {
        UINT32 v = 0;
        if (Interface_ReadCfgU32(ImageHandle, "overlay", &v)) g_ifx.OverlayEnabled = (v != 0);
        if (Interface_ReadCfgU32(ImageHandle, "overlay_top", &v)) g_ifx.OverlayPosTop = (v != 0);
        if (Interface_ReadCfgU32(ImageHandle, "overlay_max_w", &v) && v >= 120) g_ifx.OverlayMaxW = v;
        if (Interface_ReadCfgU32(ImageHandle, "overlay_h", &v) && v >= 18 && v <= 80) g_ifx.OverlayH = v;
        if (Interface_ReadCfgU32(ImageHandle, "overlay_digits", &v)) g_ifx.OverlayDigits = (v != 0);
        if (Interface_ReadCfgU32(ImageHandle, "overlay_time", &v)) g_ifx.OverlayTime = (v != 0);
        if (Interface_ReadCfgU32(ImageHandle, "overlay_time_mode", &v)) {
            if (v > 2) v = 2;
            g_ifx.OverlayTimeMode = v;
        } else {
            // Backward compat: overlay_time acts as a boolean for the ms mode.
            g_ifx.OverlayTimeMode = g_ifx.OverlayTime ? 1u : 0u;
        }
    }

    // Precompute a small overlay region (bottom center) to avoid full-screen redraw.
    if (g_ifx.OverlayH == 0) g_ifx.OverlayH = 26;
    if (!g_ifx.OverlayEnabled) {
        g_ifx.OverlayW = 0;
        g_ifx.OverlayH = 0;
        g_ifx.OverlayX = 0;
        g_ifx.OverlayY = 0;
    } else {
        g_ifx.OverlayW = (g_ifx.ScreenW > 220) ? (g_ifx.ScreenW - 40) : g_ifx.ScreenW;
        if (g_ifx.OverlayW > g_ifx.OverlayMaxW) g_ifx.OverlayW = g_ifx.OverlayMaxW;
        g_ifx.OverlayX = (g_ifx.ScreenW > g_ifx.OverlayW) ? ((g_ifx.ScreenW - g_ifx.OverlayW) / 2) : 0;
        if (g_ifx.OverlayPosTop) {
            g_ifx.OverlayY = 6;
        } else {
            g_ifx.OverlayY = (g_ifx.ScreenH > (g_ifx.OverlayH + 6)) ? (g_ifx.ScreenH - g_ifx.OverlayH - 6) : 0;
        }
    }
    g_ifx.StageIndex1 = 0;
    g_ifx.StageCount = 0;
    g_ifx.Permille = 0;
    g_ifx.LastDrawPermille = 0xFFFFFFFFu;
    g_ifx.LastDrawStageIndex1 = 0xFFFFFFFFu;
    g_ifx.TimeDeltaMs = 0;
    g_ifx.TimeTotalMs = 0;
    g_ifx.LastDrawTimeDeltaMs = 0xFFFFFFFFu;
    g_ifx.LastDrawTimeTotalMs = 0xFFFFFFFFu;
    g_ifx.StageStartSec = 0;
    g_ifx.ElapsedSec = 0;
    g_ifx.EtaSec = 0;
    g_ifx.LastDrawElapsedSec = 0xFFFFFFFFu;
    g_ifx.LastDrawEtaSec = 0xFFFFFFFFu;
    g_ifx.Anim = 0;

    // Init Warp Stars
    for (int i = 0; i < 64; i++) {
        g_ifx.Stars[i].X = (INT32)(Interface_Rand() % 2000) - 1000;
        g_ifx.Stars[i].Y = (INT32)(Interface_Rand() % 2000) - 1000;
        // Avoid center stagnation
        if (g_ifx.Stars[i].X == 0) g_ifx.Stars[i].X = 1;
        if (g_ifx.Stars[i].Y == 0) g_ifx.Stars[i].Y = 1;
    }

    // Initialize stage start time (best-effort).
    {
        UINT32 now = 0;
        if (InterfaceFx_NowSeconds(&now)) {
            g_ifx.StageStartSec = now;
        }
    }

    // 1. Clear Screen to Black
    Interface_DrawRect(g_ifx.Gop, 0, 0, g_ifx.ScreenW, g_ifx.ScreenH, ColorBlack);

    // 2. Load and draw static splash image
    Status = Interface_DrawBMP(ImageHandle, g_ifx.Gop, L"splash.bmp");
    
    // 3. Pause for visibility (configurable via repl.cfg: splash_ms=NNNN)
    // Default: 2500ms. Clamp: 0..10000ms.
    if (!EFI_ERROR(Status)) {
        UINT32 splash_ms = 2500;
        UINT32 cfg_ms = 0;
        if (Interface_ReadCfgU32(ImageHandle, "splash_ms", &cfg_ms)) {
            splash_ms = cfg_ms;
        }
        if (splash_ms > 10000) splash_ms = 10000;
        // UEFI Stall takes microseconds.
        uefi_call_wrapper(SystemTable->BootServices->Stall, 1, (UINTN)splash_ms * 1000u);
    }

    // Clear back to black so subsequent UI (banner/REPL) starts clean.
    if (!EFI_ERROR(Status)) {
        Interface_DrawRect(g_ifx.Gop, 0, 0, g_ifx.ScreenW, g_ifx.ScreenH, ColorBlack);
    }

    return Status;
}

static void InterfaceFx_DrawSeg(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 x, UINT32 y, UINT32 w, UINT32 h, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c) {
    Interface_DrawRect(Gop, x, y, w, h, c);
}

static EFI_GRAPHICS_OUTPUT_BLT_PIXEL InterfaceFx_LerpColor(EFI_GRAPHICS_OUTPUT_BLT_PIXEL c1, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c2, UINT32 t, UINT32 max) {
    if (t > max) t = max;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL r;
    r.Blue = (UINT8)((c1.Blue * (max - t) + c2.Blue * t) / max);
    r.Green = (UINT8)((c1.Green * (max - t) + c2.Green * t) / max);
    r.Red = (UINT8)((c1.Red * (max - t) + c2.Red * t) / max);
    r.Reserved = 0;
    return r;
}

static void InterfaceFx_DrawDigit7(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 x, UINT32 y, UINT32 scale, UINT32 digit, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c) {
    // 7 segments: a b c d e f g
    static const UINT8 m[10] = {
        0x3F, // 0
        0x06, // 1
        0x5B, // 2
        0x4F, // 3
        0x66, // 4
        0x6D, // 5
        0x7D, // 6
        0x07, // 7
        0x7F, // 8
        0x6F  // 9
    };
    if (!Gop) return;
    if (digit > 9) digit = 9;
    UINT8 mask = m[digit];
    UINT32 t = (scale < 1) ? 1 : scale;
    UINT32 seg = 4 * t;
    // a
    if (mask & 0x01) InterfaceFx_DrawSeg(Gop, x + t, y, seg, t, c);
    // b
    if (mask & 0x02) InterfaceFx_DrawSeg(Gop, x + t + seg, y + t, t, seg, c);
    // c
    if (mask & 0x04) InterfaceFx_DrawSeg(Gop, x + t + seg, y + (2 * t) + seg, t, seg, c);
    // d
    if (mask & 0x08) InterfaceFx_DrawSeg(Gop, x + t, y + (2 * (t + seg)) + t, seg, t, c);
    // e
    if (mask & 0x10) InterfaceFx_DrawSeg(Gop, x, y + (2 * t) + seg, t, seg, c);
    // f
    if (mask & 0x20) InterfaceFx_DrawSeg(Gop, x, y + t, t, seg, c);
    // g
    if (mask & 0x40) InterfaceFx_DrawSeg(Gop, x + t, y + t + seg, seg, t, c);
}

static void InterfaceFx_DrawSlash(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 x, UINT32 y, UINT32 scale, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c) {
    if (!Gop) return;
    UINT32 t = (scale < 1) ? 1 : scale;
    // simple diagonal: 6 steps
    for (UINT32 i = 0; i < 6; i++) {
        UINT32 px = x + (5 - i) * t;
        UINT32 py = y + (i + 1) * t;
        InterfaceFx_DrawSeg(Gop, px, py, t, t, c);
    }
}

static void InterfaceFx_DrawOverlay(void) {
    if (!g_ifx.Active || !g_ifx.Gop) return;
    if (g_ifx.OverlayW == 0 || g_ifx.OverlayH == 0) return;

    // 1. Warp Speed Background (Clear with Deep Space color)
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY, g_ifx.OverlayW, g_ifx.OverlayH, ColorDeepSpace);

    INT32 cx = g_ifx.OverlayX + (g_ifx.OverlayW / 2);
    INT32 cy = g_ifx.OverlayY + (g_ifx.OverlayH / 2);

    // 2. Render Stars (Warp Effect)
    for (int i = 0; i < 64; i++) {
        INT32 dx = g_ifx.Stars[i].X / 10;
        INT32 dy = g_ifx.Stars[i].Y / 10;
        if (dx == 0) dx = (g_ifx.Stars[i].X > 0) ? 1 : -1;
        if (dy == 0) dy = (g_ifx.Stars[i].Y > 0) ? 1 : -1;
        
        g_ifx.Stars[i].X += dx;
        g_ifx.Stars[i].Y += dy;

        if (g_ifx.Stars[i].X < -4000 || g_ifx.Stars[i].X > 4000 || 
            g_ifx.Stars[i].Y < -1000 || g_ifx.Stars[i].Y > 1000) {
            g_ifx.Stars[i].X = (INT32)(Interface_Rand() % 200) - 100;
            g_ifx.Stars[i].Y = (INT32)(Interface_Rand() % 100) - 50;
            if (g_ifx.Stars[i].X == 0) g_ifx.Stars[i].X = 2;
            if (g_ifx.Stars[i].Y == 0) g_ifx.Stars[i].Y = 2;
        }

        INT32 sx = cx + (g_ifx.Stars[i].X / 4);
        INT32 sy = cy + (g_ifx.Stars[i].Y / 4);

        if (sx >= (INT32)g_ifx.OverlayX && sx < (INT32)(g_ifx.OverlayX + g_ifx.OverlayW) &&
            sy >= (INT32)g_ifx.OverlayY && sy < (INT32)(g_ifx.OverlayY + g_ifx.OverlayH)) {
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL color = (Interface_Rand() % 5 == 0) ? ColorNeonCyan : ColorWhite;
            UINT32 len = (UINT32)((dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy)) / 2;
            if (len < 1) len = 1; if (len > 10) len = 10;
            Interface_DrawRect(g_ifx.Gop, (UINT32)sx, (UINT32)sy, len, 1, color);
        }
    }

    // Layout
    const UINT32 pad = 6;
    const UINT32 bar_h = 8;
    const UINT32 stage_h = 4;
    const UINT32 inner_w = (g_ifx.OverlayW > (pad * 2)) ? (g_ifx.OverlayW - (pad * 2)) : g_ifx.OverlayW;
    const UINT32 inner_x = g_ifx.OverlayX + pad;
    const UINT32 bar_y = g_ifx.OverlayY + g_ifx.OverlayH - pad - bar_h;
    const UINT32 stage_y = (bar_y > (stage_h + 4)) ? (bar_y - stage_h - 4) : g_ifx.OverlayY;

    // Reserve a small left gutter for the stage counter (2/7), if enabled.
    UINT32 label_w = 0;
    if (g_ifx.OverlayDigits && g_ifx.StageCount > 0) {
        label_w = 44; // pixels (fits "9/9" at scale=2)
        if (label_w + 24 > inner_w) label_w = 0;
    }

    // Optional right gutter for timing digits:
    //   mode=1: DDDD/TTTTT (delta/total ms)
    //   mode=2: EEEE/LLLL  (eta/elapsed seconds)
    UINT32 time_w = 0;
    if (g_ifx.OverlayTimeMode != 0) {
        time_w = (g_ifx.OverlayTimeMode == 2) ? 74u : 82u;
        if (label_w + time_w + 40 > inner_w) time_w = 0;
    }

    const UINT32 content_x = inner_x + label_w;
    const UINT32 content_w = (inner_w > (label_w + time_w)) ? (inner_w - label_w - time_w) : 0;

    // HUD Frame (Sci-Fi Look)
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY, g_ifx.OverlayW, 2, ColorHudTeal); // Top
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY + g_ifx.OverlayH - 2, g_ifx.OverlayW, 2, ColorHudTeal); // Bottom
    
    // Angled Corners (Simulated with rects)
    UINT32 cLen = 15;
    UINT32 cThick = 4;
    // Top-Left
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY, cLen, cThick, ColorNeonCyan);
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY, cThick, cLen, ColorNeonCyan);
    // Top-Right
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX + g_ifx.OverlayW - cLen, g_ifx.OverlayY, cLen, cThick, ColorNeonCyan);
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX + g_ifx.OverlayW - cThick, g_ifx.OverlayY, cThick, cLen, ColorNeonCyan);
    // Bottom-Left
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY + g_ifx.OverlayH - cThick, cLen, cThick, ColorNeonCyan);
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY + g_ifx.OverlayH - cLen, cThick, cLen, ColorNeonCyan);
    // Bottom-Right
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX + g_ifx.OverlayW - cLen, g_ifx.OverlayY + g_ifx.OverlayH - cThick, cLen, cThick, ColorNeonCyan);
    Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX + g_ifx.OverlayW - cThick, g_ifx.OverlayY + g_ifx.OverlayH - cLen, cThick, cLen, ColorNeonCyan);

    // Decorative "Data" blocks on sides
    if (g_ifx.OverlayH > 20) {
        UINT32 blocks = (g_ifx.OverlayH - 20) / 4;
        for (UINT32 k = 0; k < blocks; k++) {
             if (Interface_Rand() % 2 == 0) // Left
                Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX + 6, g_ifx.OverlayY + 10 + k*4, 4, 2, ColorScanDark);
             if (Interface_Rand() % 2 == 0) // Right
                Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX + g_ifx.OverlayW - 10, g_ifx.OverlayY + 10 + k*4, 4, 2, ColorScanDark);
        }
    }

    // Stage counter (e.g., 2/7)
    if (label_w) {
        UINT32 sx = inner_x;
        UINT32 sy = stage_y - 2;
        UINT32 scale = 2;
        UINT32 a = g_ifx.StageIndex1;
        UINT32 b = g_ifx.StageCount;
        if (a > 9) a = 9;
        if (b > 9) b = 9;
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL c = ColorNeonMagenta;
        InterfaceFx_DrawDigit7(g_ifx.Gop, sx, sy, scale, a, c);
        sx += 12 * scale;
        InterfaceFx_DrawSlash(g_ifx.Gop, sx, sy, scale, c);
        sx += 6 * scale;
        InterfaceFx_DrawDigit7(g_ifx.Gop, sx, sy, scale, b, c);
    }

    // Timing display (delta/total ms)
    if (time_w) {
        UINT32 sx = inner_x + inner_w - time_w;
        UINT32 sy = stage_y - 2;
        UINT32 scale = 1;

        EFI_GRAPHICS_OUTPUT_BLT_PIXEL c = ColorGreen;

        if (g_ifx.OverlayTimeMode == 2) {
            UINT32 e = g_ifx.EtaSec;
            UINT32 l = g_ifx.ElapsedSec;
            if (e > 9999u) e = 9999u;
            if (l > 9999u) l = 9999u;

            UINT32 ee[4];
            ee[0] = (e / 1000u) % 10u;
            ee[1] = (e / 100u) % 10u;
            ee[2] = (e / 10u) % 10u;
            ee[3] = e % 10u;
            for (int i = 0; i < 4; i++) {
                InterfaceFx_DrawDigit7(g_ifx.Gop, sx, sy, scale, ee[i], c);
                sx += 9 * scale;
            }
            InterfaceFx_DrawSlash(g_ifx.Gop, sx, sy, scale, c);
            sx += 5 * scale;

            UINT32 ll[4];
            ll[0] = (l / 1000u) % 10u;
            ll[1] = (l / 100u) % 10u;
            ll[2] = (l / 10u) % 10u;
            ll[3] = l % 10u;
            for (int i = 0; i < 4; i++) {
                InterfaceFx_DrawDigit7(g_ifx.Gop, sx, sy, scale, ll[i], c);
                sx += 9 * scale;
            }
        } else {
            UINT32 d = g_ifx.TimeDeltaMs;
            UINT32 t = g_ifx.TimeTotalMs;
            if (d > 9999u) d = 9999u;
            if (t > 99999u) t = 99999u;

            // delta: 4 digits
            UINT32 dd[4];
            dd[0] = (d / 1000u) % 10u;
            dd[1] = (d / 100u) % 10u;
            dd[2] = (d / 10u) % 10u;
            dd[3] = d % 10u;
            for (int i = 0; i < 4; i++) {
                InterfaceFx_DrawDigit7(g_ifx.Gop, sx, sy, scale, dd[i], c);
                sx += 9 * scale;
            }
            InterfaceFx_DrawSlash(g_ifx.Gop, sx, sy, scale, c);
            sx += 5 * scale;

            // total: 5 digits
            UINT32 tt[5];
            tt[0] = (t / 10000u) % 10u;
            tt[1] = (t / 1000u) % 10u;
            tt[2] = (t / 100u) % 10u;
            tt[3] = (t / 10u) % 10u;
            tt[4] = t % 10u;
            for (int i = 0; i < 5; i++) {
                InterfaceFx_DrawDigit7(g_ifx.Gop, sx, sy, scale, tt[i], c);
                sx += 9 * scale;
            }
        }
    }

    // Stage segments
    if (g_ifx.StageCount >= 1 && g_ifx.StageCount <= 32) {
        UINT32 count = g_ifx.StageCount;
        UINT32 seg_gap = 2;
        UINT32 seg_w = (content_w - (seg_gap * (count - 1))) / count;
        if (seg_w < 2) seg_w = 2;
        UINT32 total_w = (seg_w * count) + (seg_gap * (count - 1));
        UINT32 sx = content_x + ((content_w > total_w) ? ((content_w - total_w) / 2) : 0);
        for (UINT32 i = 0; i < count; i++) {
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL c = (i + 1 == g_ifx.StageIndex1) ? ColorNeonOrange : 
                                             ((i + 1 < g_ifx.StageIndex1) ? ColorNeonCyan : ColorDarkBlue);
            Interface_DrawRect(g_ifx.Gop, sx, stage_y, seg_w, stage_h, c);
            sx += seg_w + seg_gap;
        }
    }

    // Progress bar
    Interface_DrawRect(g_ifx.Gop, content_x, bar_y, content_w, bar_h, ColorDarkBlue);
    UINT32 p = g_ifx.Permille;
    if (p > 1000) p = 1000;
    UINT32 fill_w = (content_w * p) / 1000;
    if (fill_w > 0) {
        Interface_DrawRect(g_ifx.Gop, content_x, bar_y, fill_w, bar_h, ColorNeonCyan);
        // Bright head of progress bar
        if (fill_w > 2) {
            Interface_DrawRect(g_ifx.Gop, content_x + fill_w - 2, bar_y, 2, bar_h, ColorWhite);
        }
    }

    // Subtle animated highlight
    if (content_w >= 8) {
        UINT32 hx = content_x + (g_ifx.Anim % content_w);
        if (hx < content_x + fill_w) {
            Interface_DrawRect(g_ifx.Gop, hx, bar_y, 2, bar_h, ColorNeonMagenta);
        }
    }
}

static void InterfaceFx_SetProgressPermille(UINT32 permille) {
    if (!g_ifx.Active) return;
    if (permille > 1000) permille = 1000;
    // Monotone: never decrease (avoids flicker and regressions between phases).
    if (permille < g_ifx.Permille) permille = g_ifx.Permille;
    g_ifx.Permille = permille;

    // If ETA mode is enabled, update it from stage progress (best-effort).
    if (g_ifx.OverlayTimeMode == 2 && g_ifx.StageCount > 0 && g_ifx.StageIndex1 > 0) {
        UINT32 now = 0;
        if (InterfaceFx_NowSeconds(&now) && g_ifx.StageStartSec != 0) {
            UINT32 elapsed = (now >= g_ifx.StageStartSec) ? (now - g_ifx.StageStartSec) : (now + 86400u - g_ifx.StageStartSec);
            g_ifx.ElapsedSec = elapsed;

            UINT32 s = g_ifx.StageIndex1;
            UINT32 n = g_ifx.StageCount;
            if (s > n) s = n;
            UINT32 base = ((s - 1u) * 1000u) / n;
            UINT32 next = (s * 1000u) / n;
            UINT32 span = (next > base) ? (next - base) : 0u;
            UINT32 stage_prog = 0;
            if (span > 0 && g_ifx.Permille > base) {
                stage_prog = (UINT32)(((UINT64)(g_ifx.Permille - base) * 1000ull) / (UINT64)span);
            }
            if (stage_prog > 999u) stage_prog = 999u;
            if (stage_prog >= 10u) {
                UINT64 eta = ((UINT64)elapsed * (UINT64)(1000u - stage_prog)) / (UINT64)stage_prog;
                if (eta > 9999ull) eta = 9999ull;
                g_ifx.EtaSec = (UINT32)eta;
            } else {
                g_ifx.EtaSec = 0;
            }
        }
    }

    // Avoid excessive redraws; 1 permille is already coarse.
    if (g_ifx.Permille != g_ifx.LastDrawPermille ||
        g_ifx.StageIndex1 != g_ifx.LastDrawStageIndex1 ||
        g_ifx.TimeDeltaMs != g_ifx.LastDrawTimeDeltaMs ||
        g_ifx.TimeTotalMs != g_ifx.LastDrawTimeTotalMs ||
        g_ifx.ElapsedSec != g_ifx.LastDrawElapsedSec ||
        g_ifx.EtaSec != g_ifx.LastDrawEtaSec) {
        g_ifx.Anim++;
        InterfaceFx_DrawOverlay();
        g_ifx.LastDrawPermille = g_ifx.Permille;
        g_ifx.LastDrawStageIndex1 = g_ifx.StageIndex1;
        g_ifx.LastDrawTimeDeltaMs = g_ifx.TimeDeltaMs;
        g_ifx.LastDrawTimeTotalMs = g_ifx.TimeTotalMs;
        g_ifx.LastDrawElapsedSec = g_ifx.ElapsedSec;
        g_ifx.LastDrawEtaSec = g_ifx.EtaSec;
    }
}

static void InterfaceFx_SetTimingMs(UINT32 delta_ms, UINT32 total_ms) {
    if (!g_ifx.Active) return;
    g_ifx.TimeDeltaMs = delta_ms;
    g_ifx.TimeTotalMs = total_ms;

    if (g_ifx.TimeDeltaMs != g_ifx.LastDrawTimeDeltaMs ||
        g_ifx.TimeTotalMs != g_ifx.LastDrawTimeTotalMs) {
        g_ifx.Anim++;
        InterfaceFx_DrawOverlay();
        g_ifx.LastDrawPermille = g_ifx.Permille;
        g_ifx.LastDrawStageIndex1 = g_ifx.StageIndex1;
        g_ifx.LastDrawTimeDeltaMs = g_ifx.TimeDeltaMs;
        g_ifx.LastDrawTimeTotalMs = g_ifx.TimeTotalMs;
        g_ifx.LastDrawElapsedSec = g_ifx.ElapsedSec;
        g_ifx.LastDrawEtaSec = g_ifx.EtaSec;
    }
}

static void InterfaceFx_Tick(void) {
    if (!g_ifx.Active) return;
    g_ifx.Anim++;
    // Redraw only occasionally to keep it cheap.
    if ((g_ifx.Anim & 3u) == 0u) {
        InterfaceFx_DrawOverlay();
    }
}

static void InterfaceFx_End(void) {
    if (g_ifx.Active && g_ifx.Gop) {
        // Clear overlay region only (do not wipe console output).
        if (g_ifx.OverlayW && g_ifx.OverlayH) {
            Interface_DrawRect(g_ifx.Gop, g_ifx.OverlayX, g_ifx.OverlayY, g_ifx.OverlayW, g_ifx.OverlayH, ColorBlack);
        }
    }
    g_ifx.Active = 0;
    g_ifx.Gop = NULL;
}

// Compatibility wrapper: keep the call site name stable.
static int ShowCyberpunkSplash(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status = InterfaceFx_Begin(ImageHandle, SystemTable);
    return !EFI_ERROR(Status);
}

static void InterfaceFx_Stage(UINT32 stage_index_1based, UINT32 stage_count) {
    if (!g_ifx.Active) return;
    if (stage_count == 0) return;
    if (stage_index_1based < 1) stage_index_1based = 1;
    if (stage_index_1based > stage_count) stage_index_1based = stage_count;
    g_ifx.StageIndex1 = stage_index_1based;
    g_ifx.StageCount = stage_count;

    // Reset per-stage ETA timing (best-effort).
    {
        UINT32 now = 0;
        if (InterfaceFx_NowSeconds(&now)) {
            g_ifx.StageStartSec = now;
            g_ifx.ElapsedSec = 0;
            g_ifx.EtaSec = 0;
        }
    }

    // Map stage to a coarse progress floor (keeps bar moving even when byte progress is sparse).
    UINT32 base = ((stage_index_1based - 1) * 1000u) / stage_count;
    if (base > 1000) base = 1000;
    if (g_ifx.Permille < base) {
        g_ifx.Permille = base;
    }

    g_ifx.Anim++;
    InterfaceFx_DrawOverlay();
    g_ifx.LastDrawPermille = g_ifx.Permille;
    g_ifx.LastDrawStageIndex1 = g_ifx.StageIndex1;
    g_ifx.LastDrawTimeDeltaMs = g_ifx.TimeDeltaMs;
    g_ifx.LastDrawTimeTotalMs = g_ifx.TimeTotalMs;
    g_ifx.LastDrawElapsedSec = g_ifx.ElapsedSec;
    g_ifx.LastDrawEtaSec = g_ifx.EtaSec;
}

static void InterfaceFx_ProgressBytes(UINTN done, UINTN total) {
    if (!g_ifx.Active) return;
    if (total == 0) {
        InterfaceFx_SetProgressPermille(0);
        return;
    }

    // Use 64-bit math to avoid overflow on large files.
    UINT64 local = ((UINT64)done * 1000ull) / (UINT64)total;
    if (local > 1000ull) local = 1000ull;

    // If we have a stage context, map local progress into the current stage segment.
    if (g_ifx.StageCount > 0 && g_ifx.StageIndex1 > 0) {
        UINT32 s = g_ifx.StageIndex1;
        UINT32 n = g_ifx.StageCount;
        if (s > n) s = n;
        UINT32 base = ((s - 1u) * 1000u) / n;
        UINT32 next = (s * 1000u) / n;
        UINT32 span = (next > base) ? (next - base) : 0u;
        UINT32 mapped = base + (UINT32)((local * (UINT64)span) / 1000ull);
        InterfaceFx_SetProgressPermille(mapped);
        return;
    }

    InterfaceFx_SetProgressPermille((UINT32)local);
}

#endif // INTERFACE_H
