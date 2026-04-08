#!/usr/bin/env python3
"""
make_boot_gif.py — OO Boot Splash GIF Generator
================================================
Creates an animated GIF from llm2.png for:
  1. README / documentation showcase
  2. Embedded C array (oo_boot_splash.h) for bare-metal framebuffer

Animation sequence (14 frames, 80ms each ~1.1s loop):
  Phase 1 (f0-f3):  Black → Logo fade-in (opacity ramp)
  Phase 2 (f4-f7):  Glow pulse (luminosity oscillation)
  Phase 3 (f8-f10): DNA hash overlay + "OO" text
  Phase 4 (f11-f13): Hold → soft fade out → loop

Output:
  oo_boot_splash.gif       — animated GIF (for README embed)
  tools/oo_boot_splash.h   — C header with frames as byte arrays (bare-metal use)
"""

import os
import sys
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

try:
    from PIL import Image, ImageDraw, ImageFont, ImageEnhance, ImageFilter
except ImportError:
    print("[ERROR] Pillow not installed. Run: python3 -m pip install Pillow")
    sys.exit(1)

# ── Config ─────────────────────────────────────────────────────────────
LOGO_PATH  = os.path.join(ROOT, "llm2.png")
OUT_GIF    = os.path.join(ROOT, "oo_boot_splash.gif")
OUT_HEADER = os.path.join(ROOT, "tools", "oo_boot_splash.h")

TARGET_W = 400
TARGET_H = 300
BG_COLOR = (0, 0, 0, 255)       # pure black background
GLOW_COLOR = (0, 180, 255)      # cyan-blue glow (OO brand)
TEXT_COLOR = (0, 255, 180)      # green-cyan for OOSI text

FRAME_DURATION_MS = 80          # ms per frame
DNA_HASH = "0x932DF0EA"         # first HW boot DNA hash

def load_logo():
    img = Image.open(LOGO_PATH).convert("RGBA")
    # Scale to fit within TARGET_W x TARGET_H, keeping aspect ratio
    ratio = min(TARGET_W * 0.8 / img.width, TARGET_H * 0.7 / img.height)
    nw, nh = int(img.width * ratio), int(img.height * ratio)
    return img.resize((nw, nh), Image.LANCZOS)

def make_base(logo, alpha=255, brightness=1.0):
    """Create one frame: black bg + logo centered at given alpha/brightness."""
    frame = Image.new("RGBA", (TARGET_W, TARGET_H), BG_COLOR)
    logo_frame = logo.copy()

    # Adjust brightness
    if brightness != 1.0:
        r, g, b, a = logo_frame.split()
        enhancer = ImageEnhance.Brightness(Image.merge("RGB", (r, g, b)))
        rgb = enhancer.enhance(brightness)
        logo_frame = Image.merge("RGBA", (*rgb.split(), a))

    # Adjust alpha
    r, g, b, a = logo_frame.split()
    a = a.point(lambda x: int(x * alpha / 255))
    logo_frame = Image.merge("RGBA", (r, g, b, a))

    # Center logo
    x = (TARGET_W - logo.width) // 2
    y = (TARGET_H - logo.height) // 2 - 20
    frame.paste(logo_frame, (x, y), logo_frame)
    return frame

def add_text(frame, line1, line2="", line3=""):
    draw = ImageDraw.Draw(frame)
    y = TARGET_H - 70
    for line, color in [(line1, TEXT_COLOR), (line2, (150, 150, 150)), (line3, (80, 80, 80))]:
        if not line:
            continue
        # Estimate text width (default font ~6px per char)
        tw = len(line) * 6
        x = (TARGET_W - tw) // 2
        draw.text((x, y), line, fill=color)
        y += 16
    return frame

def add_glow_border(frame, intensity=0.3):
    draw = ImageDraw.Draw(frame)
    r = int(TARGET_W * 0.42)
    cx, cy = TARGET_W // 2, TARGET_H // 2 - 20
    alpha = int(255 * intensity)
    draw.ellipse(
        [cx - r, cy - r, cx + r, cy + r],
        outline=(*GLOW_COLOR, alpha),
        width=2
    )
    return frame

def build_frames(logo):
    frames = []

    # Phase 1: Fade in (4 frames)
    for i, a in enumerate([40, 100, 180, 255]):
        f = make_base(logo, alpha=a, brightness=0.7 + 0.3 * (a/255))
        f = add_text(f, "OOSI v3  BOOTING...", "", "")
        frames.append(f.convert("RGB"))

    # Phase 2: Glow pulse (4 frames)
    for i, (brightness, glow) in enumerate([
        (1.0, 0.2), (1.15, 0.45), (1.0, 0.3), (0.9, 0.15)
    ]):
        f = make_base(logo, alpha=255, brightness=brightness)
        f = add_glow_border(f, glow)
        f = add_text(f, "SOMA MIND  INITIALIZING", "", "")
        frames.append(f.convert("RGB"))

    # Phase 3: DNA + status text (3 frames)
    status_lines = [
        ("MAMBA 2.8B  LOADED", f"DNA  {DNA_HASH}", "WEIGHTS 2794 MB"),
        ("A-Z PHASES  ACTIVE", f"DNA  {DNA_HASH}", "SSM READY"),
        ("INFERENCE  ENGINE  ONLINE", f"DNA  {DNA_HASH}", "SWARM WAITING"),
    ]
    for line1, line2, line3 in status_lines:
        f = make_base(logo, alpha=255, brightness=1.0)
        f = add_glow_border(f, 0.25)
        f = add_text(f, line1, line2, line3)
        frames.append(f.convert("RGB"))

    # Phase 4: Hold + fade out (3 frames)
    for a in [255, 180, 80]:
        f = make_base(logo, alpha=a, brightness=a/255)
        f = add_text(f, "OO  SYSTEM  READY", "", "")
        frames.append(f.convert("RGB"))

    return frames

def frames_to_gif(frames, out_path):
    frames[0].save(
        out_path,
        save_all=True,
        append_images=frames[1:],
        duration=FRAME_DURATION_MS,
        loop=0,
        optimize=False
    )
    size = os.path.getsize(out_path) // 1024
    print(f"[GIF] Saved: {out_path} ({size} KB, {len(frames)} frames)")

def frames_to_c_header(frames, out_path):
    """Export first frame as 16-bit RGB565 array for bare-metal framebuffer."""
    frame0 = frames[0]
    frame0_small = frame0.resize((200, 150), Image.LANCZOS)
    pixels = list(frame0_small.getdata())

    def rgb_to_rgb565(r, g, b):
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    rgb565 = [rgb_to_rgb565(p[0], p[1], p[2]) for p in pixels]

    with open(out_path, "w") as f:
        f.write("/* oo_boot_splash.h — OO Boot Logo (RGB565, 200x150) */\n")
        f.write("/* Auto-generated by tools/make_boot_gif.py */\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define OO_SPLASH_W  200\n")
        f.write("#define OO_SPLASH_H  150\n\n")
        f.write("static const uint16_t oo_boot_splash[200*150] = {\n")
        for i, px in enumerate(rgb565):
            if i % 16 == 0:
                f.write("    ")
            f.write(f"0x{px:04X},")
            if i % 16 == 15:
                f.write("\n")
        f.write("\n};\n\n")
        f.write("/* Usage in bare-metal framebuffer:\n")
        f.write(" *   for(int y=0;y<OO_SPLASH_H;y++) for(int x=0;x<OO_SPLASH_W;x++) {\n")
        f.write(" *     uint16_t px = oo_boot_splash[y*OO_SPLASH_W+x];\n")
        f.write(" *     uint8_t r=(px>>8)&0xF8, g=(px>>3)&0xFC, b=(px<<3);\n")
        f.write(" *     framebuf[(cy+y)*stride+(cx+x)] = (0xFF<<24)|(r<<16)|(g<<8)|b;\n")
        f.write(" *   }\n")
        f.write(" */\n")
    size = os.path.getsize(out_path) // 1024
    print(f"[C]   Saved: {out_path} ({size} KB, {len(rgb565)} pixels RGB565)")

if __name__ == "__main__":
    print("[OO] Generating boot splash GIF...")
    if not os.path.exists(LOGO_PATH):
        print(f"[ERROR] Logo not found: {LOGO_PATH}")
        sys.exit(1)

    logo = load_logo()
    print(f"[OO] Logo loaded: {logo.width}x{logo.height}")

    frames = build_frames(logo)
    print(f"[OO] Built {len(frames)} frames ({TARGET_W}x{TARGET_H})")

    frames_to_gif(frames, OUT_GIF)
    frames_to_c_header(frames, OUT_HEADER)

    print("[OO] Done. Boot splash ready.")
    print(f"     GIF:    {OUT_GIF}")
    print(f"     Header: {OUT_HEADER}")
