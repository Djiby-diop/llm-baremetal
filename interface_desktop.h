#ifndef INTERFACE_DESKTOP_H
#define INTERFACE_DESKTOP_H

#include <efi.h>
#include <efilib.h>

/*
 * DESKTOP UI for LLM-BAREMETAL (SentienceOS)
 * Theme: "Cosmic HUD"
 * - Persistent Desktop Environment
 * - Top/Bottom Status Bars
 * - Side Telemetry Widgets
 * - Holographic Text Frame
 */

// --- Colors ---
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColOsBg       = {10, 15, 20, 0};    // Deep Slate
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColOsText     = {200, 220, 255, 0}; // Pale Cyan
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColOsAccent   = {0, 255, 200, 0};   // Neon Teal
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColOsWarn     = {255, 100, 0, 0};   // Amber
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColOsDim      = {50, 60, 70, 0};    // Dim Grey
static const EFI_GRAPHICS_OUTPUT_BLT_PIXEL ColOsTrans    = {0, 0, 0, 0};       // Black (Transparent-ish)

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_SYSTEM_TABLE *St;
    UINT32 Width;
    UINT32 Height;
    UINT32 FrameCount;
    UINT32 Active;
    
    // Desktop State
    CHAR16 StatusMsg[64];
    UINT32 MemUsagePct;
    UINT32 CpuLoadPct;
    UINT32 TokPerSec;
} DesktopState;

static DesktopState g_desktop = {0};

// --- Graphics Primitives ---

static void Desk_FillRect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c) {
    if (!g_desktop.Gop) return;
    if (x >= g_desktop.Width || y >= g_desktop.Height) return;
    if (x + w > g_desktop.Width) w = g_desktop.Width - x;
    if (y + h > g_desktop.Height) h = g_desktop.Height - y;
    uefi_call_wrapper(g_desktop.Gop->Blt, 10, g_desktop.Gop, &c, EfiBltVideoFill, 0, 0, x, y, w, h, 0);
}

// Draw a "Hollow" Rect (Frame)
static void Desk_DrawRect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c) {
    Desk_FillRect(x, y, w, 1, c);            // Top
    Desk_FillRect(x, y + h - 1, w, 1, c);    // Bottom
    Desk_FillRect(x, y, 1, h, c);            // Left
    Desk_FillRect(x + w - 1, y, 1, h, c);    // Right
}

static void Desk_DrawCharFake(UINT32 x, UINT32 y, EFI_GRAPHICS_OUTPUT_BLT_PIXEL c) {
     Desk_FillRect(x, y, 4, 6, c);
}

// --- UI Components ---

static void Desk_DrawTopBar(void) {
    // Background
    Desk_FillRect(0, 0, g_desktop.Width, 30, ColOsDim);
    Desk_FillRect(0, 29, g_desktop.Width, 1, ColOsAccent);
    
    // "Start" Button
    Desk_FillRect(5, 5, 20, 20, ColOsAccent);
    
    // Status Indicators (Fake Text Blocks)
    UINT32 tx = g_desktop.Width - 100;
    Desk_FillRect(tx, 10, 80, 10, ColOsBg); // Clock area
    
    // Draw Real Time if available
    if (g_desktop.St) {
        EFI_TIME t;
        EFI_STATUS st = uefi_call_wrapper(g_desktop.St->RuntimeServices->GetTime, 2, &t, NULL);
        if (!EFI_ERROR(st)) {
            // Binary Clock simulation (dots) because we have no font
            for(int i=0; i<6; i++) {
                 // Hour
                 if (i<2) {
                     int val = (i==0) ? t.Hour/10 : t.Hour%10;
                     Desk_FillRect(tx + 5 + (i*12), 12, 8, 2 + (val/2), ColOsText);
                 } else if (i<4) { // Min
                     int val = (i==2) ? t.Minute/10 : t.Minute%10;
                     Desk_FillRect(tx + 10 + (i*12), 12, 8, 2 + (val/2), ColOsText);
                 } else { // Sec
                     int val = (i==4) ? t.Second/10 : t.Second%10;
                     Desk_FillRect(tx + 15 + (i*12), 12, 8, 2 + (val/2), ColOsWarn);
                 }
            }
        }
    } else {
        Desk_FillRect(tx + 78, 10, 2, 10, ColOsWarn);
    }
}

static void Desk_DrawBottomBar(void) {
    UINT32 y = g_desktop.Height - 30;
    Desk_FillRect(0, y, g_desktop.Width, 30, ColOsDim);
    Desk_FillRect(0, y, g_desktop.Width, 1, ColOsAccent);
}

static void Desk_DrawSideWidgets(void) {
    // Left CPU Graph (Simulated Load or Real from arg)
    UINT32 cx = 10;
    UINT32 cy = 100;
    Desk_DrawRect(cx, cy, 30, 200, ColOsDim);
    
    // Animate Graph - Mix simulated pulse with "Real" load if provided
    UINT32 load = g_desktop.CpuLoadPct;
    if (load == 0) load = (g_desktop.FrameCount % 100); 
    
    UINT32 h = load * 2; 
    if (h > 200) h = 200;
    
    // Clear old
    Desk_FillRect(cx + 2, cy + 2, 26, 196, ColOsBg);
    // Draw new
    Desk_FillRect(cx + 2, cy + 200 - h, 26, h, ColOsWarn);
    
    // Right Memory Matrix (Token Speed Visualization)
    UINT32 mx = g_desktop.Width - 40;
    UINT32 my = 100;
    UINT32 tps = g_desktop.TokPerSec;
    if (tps == 0) tps = 10; // Idle speed

    // Avoid modulo by zero when tps is high.
    UINT32 period = (tps >= 200) ? 1 : (200 / tps);
    if (period == 0) period = 1;
    
    for(int i=0; i<10; i++) {
        // Higher speed = faster flashing
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL c = (((g_desktop.FrameCount + (UINT32)i) % period) == 0) ? ColOsAccent : ColOsBg;
        Desk_FillRect(mx, my + (i*22), 30, 20, c);
        Desk_DrawRect(mx, my + (i*22), 30, 20, ColOsDim);
    }
}

static void Desk_UpdateStats(UINT32 cpu, UINT32 tok_speed) {
    g_desktop.CpuLoadPct = cpu;
    g_desktop.TokPerSec = (tok_speed > 0) ? tok_speed : 1;
}

static void Desk_DrawHolographicOverlay(void) {
    // Scanline (one thin line moving down)
    UINT32 y = g_desktop.FrameCount % g_desktop.Height;
    // XOR or Additive simple hack: just draw a thin faint line
    // Since we can't read-back easily for XOR without buffer, just prompt a line
    // But we don't want to destroy text.
    // SAFE TRICK: Don't draw in the middle 80% if we want to preserve text safely without offscreen buffer.
    
    // Draw Corner Brackets (HUD style)
    UINT32 w = g_desktop.Width;
    UINT32 h = g_desktop.Height;
    UINT32 len = 18;
    UINT32 thick = 2;
    
    // TL
    Desk_FillRect(0, 0, len, thick, ColOsAccent);
    Desk_FillRect(0, 0, thick, len, ColOsAccent);
    
    // TR
    Desk_FillRect(w - len, 0, len, thick, ColOsAccent);
    Desk_FillRect(w - thick, 0, thick, len, ColOsAccent);
    
    // BL
    Desk_FillRect(0, h - thick, len, thick, ColOsAccent);
    Desk_FillRect(0, h - len, thick, len, ColOsAccent);
    
    // BR
    Desk_FillRect(w - len, h - thick, len, thick, ColOsAccent);
    Desk_FillRect(w - thick, h - len, thick, len, ColOsAccent);
}

// --- Logic ---

static void Desktop_Tick(void) {
    if (!g_desktop.Active) return;
    g_desktop.FrameCount++;
    
    // Redraw static elements periodically or if dirty?
    // For simple animation, redraw dynamic parts every frame.
    // Note: ConOut Text might overwrite our UI if it scrolls.
    // Ideally we would set text window margins, but EFI doesn't support that easily.
    // We just redraw the UI *on top* of text margins.
    
    // Keep the overlay minimal so it doesn't fight with ConOut text.
    Desk_DrawTopBar();
    Desk_DrawBottomBar();
    Desk_DrawHolographicOverlay();
}

static EFI_STATUS Desktop_Init(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    (void)ImageHandle;
    g_desktop.St = SystemTable;
    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS Status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &GopGuid, NULL, (void **)&g_desktop.Gop);
    if (EFI_ERROR(Status) || !g_desktop.Gop) return Status;

    g_desktop.Width = g_desktop.Gop->Mode->Info->HorizontalResolution;
    g_desktop.Height = g_desktop.Gop->Mode->Info->VerticalResolution;
    g_desktop.Active = 1;

    // Clear Screen to BG
    Desk_FillRect(0, 0, g_desktop.Width, g_desktop.Height, ColOsBg);
    
    return EFI_SUCCESS;
}

// Compat hooks for main code
static void InterfaceFx_DrawOverlay(void) { Desktop_Tick(); }
static EFI_STATUS InterfaceFx_Begin(EFI_HANDLE h, EFI_SYSTEM_TABLE *st) { return Desktop_Init(h, st); }
static void InterfaceFx_Tick(void) { Desktop_Tick(); }
static void InterfaceFx_Stage(UINT32 s, UINT32 c) { (void)s; (void)c; Desktop_Tick(); }
static void InterfaceFx_SetTimingMs(UINT32 d, UINT32 t) { (void)d; (void)t; }
static void InterfaceFx_End(void) { } // Don't end, persistent.
static void InterfaceFx_ProgressBytes(UINTN done, UINTN total) { Desktop_Tick(); }
static int Interface_ReadCfgU32(EFI_HANDLE h, const char *k, UINT32 *o) { return 0; }
static int ShowCyberpunkSplash(EFI_HANDLE h, EFI_SYSTEM_TABLE *st) { return !EFI_ERROR(Desktop_Init(h, st)); }

#endif // INTERFACE_DESKTOP_H
