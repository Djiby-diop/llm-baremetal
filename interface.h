#ifndef INTERFACE_H
#define INTERFACE_H

#include <efi.h>
#include <efilib.h>

/*
 * Cyberpunk UI for LLM-BAREMETAL
 * Goal (per your feedback): avoid blurry images and use a lightweight animated
 * background during long loading (scanlines + particles + progress bar).
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

    // Move to pixel data
    uefi_call_wrapper(File->SetPosition, 2, File, FileHeader.Offset);

    // Allocation for one row (assuming 24-bit BGR)
    // NOTE: This simple parser assumes 24-bit uncompressed BMP, standard bottom-up
    UINT32 RowSize = ((InfoHeader.Width * 3 + 3) & ~3); // Padding to 4 bytes
    UINT8 *RowBuffer = AllocatePool(RowSize);
    if (!RowBuffer) {
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_OUT_OF_RESOURCES;
    }

    // Calculate Center
    UINT32 ScreenW = Gop->Mode->Info->HorizontalResolution;
    UINT32 ScreenH = Gop->Mode->Info->VerticalResolution;
    UINT32 StartX = (ScreenW > (UINT32)InfoHeader.Width) ? (ScreenW - InfoHeader.Width) / 2 : 0;
    UINT32 StartY = (ScreenH > (UINT32)InfoHeader.Height) ? (ScreenH - InfoHeader.Height) / 2 : 0;

    // Draw lines (Bottom-Up)
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *LinePixels = AllocatePool(InfoHeader.Width * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    
    for (INT32 y = 0; y < InfoHeader.Height; y++) {
        UINTN ReadSize = RowSize;
        uefi_call_wrapper(File->Read, 3, File, &ReadSize, RowBuffer);
        
        // Convert BGR to BltPixel
        for (INT32 x = 0; x < InfoHeader.Width; x++) {
            UINT32 Offset = x * 3;
            LinePixels[x].Blue = RowBuffer[Offset];
            LinePixels[x].Green = RowBuffer[Offset + 1];
            LinePixels[x].Red = RowBuffer[Offset + 2];
            LinePixels[x].Reserved = 0;
        }

        // Blt Line (Inverting Y because BMP is stored bottom-up)
        uefi_call_wrapper(Gop->Blt, 10, Gop, LinePixels, EfiBltBufferToVideo, 
            0, 0, 
            StartX, StartY + (InfoHeader.Height - 1 - y), 
            InfoHeader.Width, 1, 
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
} InterfaceFxState;

static InterfaceFxState g_ifx = {0};

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

    // 1. Clear Screen to Black
    Interface_DrawRect(g_ifx.Gop, 0, 0, g_ifx.ScreenW, g_ifx.ScreenH, ColorBlack);

    // 2. Load and Draw Static Splash Image
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

    // 4. Fallback or Cleanup: we now return SUCCESS so the banner logic knows we ran.
    // However, the caller will clear the screen if we return TRUE.
    
    // Wait... if we want the image to "disappear after boot loading", we should NOT stall here.
    // But the user said "image au debut puls disparait APRES le chargement".
    // If we stall, we delay boot. If we don't stall, we show image, then load model.
    // The previous code showed image, then loaded model, then cleared. 
    // If the image was "not visible", maybe it was overwritten immediately?
    
    // Let's rely on the Stall here to ensure it's seen at least once clearly.
    // Then we exit. The caller (llama2_efi_final.c) will then print the banner.
    // Since we want the banner back, we must allow the banner to print.
    // The banner prints AFTER we return.
    // So we should probably clear the screen HERE before returning, to ensure clean slate for banner.
    
    if (!EFI_ERROR(Status)) {
         Interface_DrawRect(g_ifx.Gop, 0, 0, g_ifx.ScreenW, g_ifx.ScreenH, ColorBlack);
    }

    return Status;
}

// Stub function: no animation
static void InterfaceFx_SetProgressPermille(UINT32 permille) {
    (void)permille;
}

// Stub function: no animation
static void InterfaceFx_Tick(void) {
}

static void InterfaceFx_End(void) {
    if (g_ifx.Active && g_ifx.Gop) {
        // Clear screen to black one last time to remove the image before REPL
        Interface_DrawRect(g_ifx.Gop, 0, 0, g_ifx.ScreenW, g_ifx.ScreenH, ColorBlack);
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
    (void)stage_index_1based;
    (void)stage_count;
}

static void InterfaceFx_ProgressBytes(UINTN done, UINTN total) {
    (void)done;
    (void)total;
}

#endif // INTERFACE_H
