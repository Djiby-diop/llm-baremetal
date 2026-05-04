/* oo_hud_final.c — SOMA Desktop UEFI Application
 * Bare-metal EFI display for the Operating Organism project.
 * Build: gcc -ffreestanding -fno-stack-protector -fpic -fshort-wchar
 *             -mno-red-zone -I/usr/include/efi -I/usr/include/efi/x86_64
 *             -DEFI_FUNCTION_WRAPPER -O2 -c oo_hud_final.c -o oo_hud_final.o
 */
#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "oo_soma_bridge.h"

/* ── Display globals ────────────────────────────────────────────── */
// These will be mapped to the actual GOP variables in llmk_oo_on_step_gop
static volatile UINT32 *g_fb;
static UINT32  g_sw, g_sh, g_stride;

/* ── Coordinate helpers (per-mille 0-1000 → pixels) ─────────────── */
#define W(pm) ((INT32)((UINT32)(g_sw) * (UINT32)(pm) / 1000u))
#define H(pm) ((INT32)((UINT32)(g_sh) * (UINT32)(pm) / 1000u))

/* ── BGRX colour helpers (EFI stores B@byte0, G@byte1, R@byte2) ── */
#define SOMA_RGB(r,g,b) ((UINT32)(((UINT32)(r)<<16)|((UINT32)(g)<<8)|(UINT32)(b)))
#define SOMA_DEEPSPACE  SOMA_RGB(2,5,12)
#define SOMA_CYAN       SOMA_RGB(0,255,255)
#define SOMA_CYAN_DIM   SOMA_RGB(0,80,100)
#define SOMA_AMBER      SOMA_RGB(255,160,0)
#define SOMA_RED        SOMA_RGB(220,20,20)
#define SOMA_WHITE      SOMA_RGB(240,240,255)
#define SOMA_GREEN      SOMA_RGB(0,255,80)
#define SOMA_BLUE       SOMA_RGB(0,80,255)
#define SOMA_MAGENTA    SOMA_RGB(200,0,200)
#define SOMA_GRID       SOMA_RGB(5,20,35)
#define SOMA_PANEL_BG   SOMA_RGB(3,8,18)
#define SOMA_YELLOW     SOMA_RGB(220,220,0)

static UINT32 soma_state_color(OoNodeState s) {
    switch (s) {
        case OO_ACTIVE:    return SOMA_CYAN;
        case OO_DEGRADED:  return SOMA_AMBER;
        case OO_ISOLATED:  return SOMA_RED;
        case OO_EMERGENCY: return SOMA_WHITE;
        case OO_SLEEPING:  return SOMA_BLUE;
        default:           return SOMA_CYAN;
    }
}

/* ── Fixed-point sin/cos LUT (64 entries, amplitude 127) ─────────── */
static const INT8 k_sin64[64] = {
    0,12,25,37,49,60,71,81,90,99,106,113,118,123,126,127,
    127,127,126,123,118,113,106,99,90,81,71,60,49,37,25,12,
    0,-12,-25,-37,-49,-60,-71,-81,-90,-99,-106,-113,-118,-123,-126,-127,
    -127,-127,-126,-123,-118,-113,-106,-99,-90,-81,-71,-60,-49,-37,-25,-12
};
#define isin(i) ((INT32)k_sin64[((UINT32)(i))&63u])
#define icos(i) ((INT32)k_sin64[(((UINT32)(i))+16u)&63u])

/* ── 5x7 bitmap font (96 chars, ASCII 32-127) ───────────────────── */
/* Each char: 7 row-bytes; each byte = 5 bits (bit4=leftmost pixel) */
static const UINT8 g_font[96][7] = {
 {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 20 ' ' */
 {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* 21 '!' */
 {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* 22 '"' */
 {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, /* 23 '#' */
 {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* 24 '$' */
 {0x18,0x19,0x02,0x04,0x09,0x03,0x00}, /* 25 '%' */
 {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, /* 26 '&' */
 {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* 27 ''' */
 {0x02,0x04,0x04,0x04,0x04,0x04,0x02}, /* 28 '(' */
 {0x08,0x04,0x04,0x04,0x04,0x04,0x08}, /* 29 ')' */
 {0x00,0x11,0x0A,0x1F,0x0A,0x11,0x00}, /* 2A '*' */
 {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* 2B '+' */
 {0x00,0x00,0x00,0x00,0x06,0x04,0x08}, /* 2C ',' */
 {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* 2D '-' */
 {0x00,0x00,0x00,0x00,0x00,0x06,0x06}, /* 2E '.' */
 {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* 2F '/' */
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 30 '0' */
 {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 31 '1' */
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 32 '2' */
 {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 33 '3' */
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 34 '4' */
 {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 35 '5' */
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 36 '6' */
 {0x1F,0x01,0x02,0x04,0x04,0x04,0x04}, /* 37 '7' */
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 38 '8' */
 {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 39 '9' */
 {0x00,0x06,0x06,0x00,0x06,0x06,0x00}, /* 3A ':' */
 {0x00,0x06,0x06,0x00,0x06,0x04,0x08}, /* 3B ';' */
 {0x01,0x02,0x04,0x08,0x04,0x02,0x01}, /* 3C '<' */
 {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* 3D '=' */
 {0x10,0x08,0x04,0x02,0x04,0x08,0x10}, /* 3E '>' */
 {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* 3F '?' */
 {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* 40 '@' */
 {0x04,0x0A,0x11,0x1F,0x11,0x11,0x11}, /* 41 'A' */
 {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* 42 'B' */
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* 43 'C' */
 {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* 44 'D' */
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* 45 'E' */
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* 46 'F' */
 {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* 47 'G' */
 {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 48 'H' */
 {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* 49 'I' */
 {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* 4A 'J' */
 {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* 4B 'K' */
 {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* 4C 'L' */
 {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}, /* 4D 'M' */
 {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* 4E 'N' */
 {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 4F 'O' */
 {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* 50 'P' */
 {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* 51 'Q' */
 {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* 52 'R' */
 {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* 53 'S' */
 {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* 54 'T' */
 {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 55 'U' */
 {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* 56 'V' */
 {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* 57 'W' */
 {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* 58 'X' */
 {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* 59 'Y' */
 {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* 5A 'Z' */
 {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* 5B '[' */
 {0x10,0x08,0x08,0x04,0x02,0x02,0x01}, /* 5C '\' */
 {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* 5D ']' */
 {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* 5E '^' */
 {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* 5F '_' */
 {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* 60 '`' */
 {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* 61 'a' */
 {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* 62 'b' */
 {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, /* 63 'c' */
 {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* 64 'd' */
 {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* 65 'e' */
 {0x03,0x04,0x1E,0x04,0x04,0x04,0x04}, /* 66 'f' */
 {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* 67 'g' */
 {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, /* 68 'h' */
 {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* 69 'i' */
 {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* 6A 'j' */
 {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* 6B 'k' */
 {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* 6C 'l' */
 {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, /* 6D 'm' */
 {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, /* 6E 'n' */
 {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* 6F 'o' */
 {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* 70 'p' */
 {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* 71 'q' */
 {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* 72 'r' */
 {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* 73 's' */
 {0x04,0x04,0x1F,0x04,0x04,0x04,0x03}, /* 74 't' */
 {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, /* 75 'u' */
 {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* 76 'v' */
 {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* 77 'w' */
 {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* 78 'x' */
 {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* 79 'y' */
 {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* 7A 'z' */
 {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* 7B '{' */
 {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* 7C '|' */
 {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* 7D '}' */
 {0x08,0x15,0x02,0x00,0x00,0x00,0x00}, /* 7E '~' */
 {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}, /* 7F DEL */
};

/* ── Pseudo-RNG ─────────────────────────────────────────────────── */
static UINT32 g_rng = 0xDEADBEEFu;
static UINT32 rng_next(void) {
    g_rng ^= g_rng << 13u;
    g_rng ^= g_rng >> 17u;
    g_rng ^= g_rng << 5u;
    return g_rng;
}

/* ── Pixel primitives ───────────────────────────────────────────── */
static inline void px(INT32 x, INT32 y, UINT32 col) {
    if ((UINT32)x < g_sw && (UINT32)y < g_sh)
        g_fb[(UINT32)y * g_stride + (UINT32)x] = col;
}

static void px_blend(INT32 x, INT32 y, UINT32 col, UINT32 alpha) {
    if ((UINT32)x >= g_sw || (UINT32)y >= g_sh) return;
    UINT32 bg = g_fb[(UINT32)y * g_stride + (UINT32)x];
    UINT32 inv = 255u - alpha;
    UINT32 r = (((col>>16)&0xFFu)*alpha + ((bg>>16)&0xFFu)*inv) / 255u;
    UINT32 g = (((col>>8 )&0xFFu)*alpha + ((bg>>8 )&0xFFu)*inv) / 255u;
    UINT32 b = (((col    )&0xFFu)*alpha + ((bg    )&0xFFu)*inv) / 255u;
    g_fb[(UINT32)y * g_stride + (UINT32)x] = SOMA_RGB(r, g, b);
}

static void hline(INT32 x, INT32 y, INT32 w, UINT32 col) {
    if ((UINT32)y >= g_sh) return;
    INT32 x1 = x + w;
    if (x < 0) x = 0;
    if (x1 > (INT32)g_sw) x1 = (INT32)g_sw;
    for (INT32 i = x; i < x1; i++)
        g_fb[(UINT32)y * g_stride + (UINT32)i] = col;
}

static void vline(INT32 x, INT32 y, INT32 h, UINT32 col) {
    if ((UINT32)x >= g_sw) return;
    INT32 y1 = y + h;
    if (y < 0) y = 0;
    if (y1 > (INT32)g_sh) y1 = (INT32)g_sh;
    for (INT32 i = y; i < y1; i++)
        g_fb[(UINT32)i * g_stride + (UINT32)x] = col;
}

static void fill_rect(INT32 x, INT32 y, INT32 w, INT32 h, UINT32 col) {
    INT32 x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > (INT32)g_sw) x1 = (INT32)g_sw;
    if (y1 > (INT32)g_sh) y1 = (INT32)g_sh;
    for (INT32 j = y; j < y1; j++)
        for (INT32 i = x; i < x1; i++)
            g_fb[(UINT32)j * g_stride + (UINT32)i] = col;
}

static void tint_rect(INT32 x, INT32 y, INT32 w, INT32 h, UINT32 tint_col, UINT32 alpha) {
    INT32 x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > (INT32)g_sw) x1 = (INT32)g_sw;
    if (y1 > (INT32)g_sh) y1 = (INT32)g_sh;
    UINT32 inv = 255u - alpha;
    UINT32 tr = (tint_col >> 16) & 0xFFu;
    UINT32 tg = (tint_col >> 8) & 0xFFu;
    UINT32 tb = tint_col & 0xFFu;
    for (INT32 j = y; j < y1; j++) {
        for (INT32 i = x; i < x1; i++) {
            UINT32 bg = g_fb[(UINT32)j * g_stride + (UINT32)i];
            UINT32 br = (bg >> 16) & 0xFFu;
            UINT32 bg_g = (bg >> 8) & 0xFFu;
            UINT32 bb = bg & 0xFFu;
            UINT32 nr = (tr * alpha + br * inv) / 255u;
            UINT32 ng = (tg * alpha + bg_g * inv) / 255u;
            UINT32 nb = (tb * alpha + bb * inv) / 255u;
            g_fb[(UINT32)j * g_stride + (UINT32)i] = SOMA_RGB(nr, ng, nb);
        }
    }
}

static void border_rect(INT32 x, INT32 y, INT32 w, INT32 h, INT32 thick, UINT32 col) {
    for (INT32 t = 0; t < thick; t++) {
        hline(x, y+t, w, col);
        hline(x, y+h-1-t, w, col);
        vline(x+t, y, h, col);
        vline(x+w-1-t, y, h, col);
    }
}

static void draw_line(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 col) {
    INT32 dx = x1-x0, dy = y1-y0;
    INT32 sx = dx>0?1:-1, sy = dy>0?1:-1;
    if (dx<0) dx=-dx; if (dy<0) dy=-dy;
    INT32 err = dx - dy;
    for (;;) {
        px(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        INT32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static void draw_circle(INT32 cx, INT32 cy, INT32 r, UINT32 col) {
    for (int i = 0; i < 64; i++) {
        INT32 x = cx + (r * icos(i)) / 127;
        INT32 y = cy + (r * isin(i)) / 127;
        px(x, y, col);
        px(x+1, y, col);
    }
}

static void draw_ellipse(INT32 cx, INT32 cy, INT32 rx, INT32 ry, UINT32 col) {
    for (int i = 0; i < 64; i++) {
        INT32 x = cx + (rx * icos(i)) / 127;
        INT32 y = cy + (ry * isin(i)) / 127;
        px(x, y, col);
        px(x+1, y, col);
    }
}

/* ── String utilities ───────────────────────────────────────────── */
static int soma_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static void soma_uint_to_dec(UINT32 v, char *buf) {
    char tmp[12]; int i = 0;
    if (v == 0) { buf[0]='0'; buf[1]=0; return; }
    while (v) { tmp[i++] = (char)('0' + v%10); v /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void soma_fmt_time(UINT32 sec, char *buf) {
    UINT32 h = sec / 3600u, m = (sec % 3600u) / 60u, s = sec % 60u;
    buf[0]=(char)('0'+h/10); buf[1]=(char)('0'+h%10); buf[2]=':';
    buf[3]=(char)('0'+m/10); buf[4]=(char)('0'+m%10); buf[5]=':';
    buf[6]=(char)('0'+s/10); buf[7]=(char)('0'+s%10); buf[8]=0;
}

/* string concat helpers */
static void soma_str_cat(char *dst, int *pos, const char *src) {
    while (*src) dst[(*pos)++] = *src++;
    dst[*pos] = 0;
}
static void soma_num_cat(char *dst, int *pos, UINT32 v) {
    char tmp[12]; soma_uint_to_dec(v, tmp);
    soma_str_cat(dst, pos, tmp);
}

/* ── Text rendering ─────────────────────────────────────────────── */
static void draw_char(INT32 x, INT32 y, char c, UINT32 color, int scale) {
    if ((unsigned char)c < 32 || (unsigned char)c > 127) c = 32;
    const UINT8 *bmp = g_font[(unsigned char)c - 32u];
    for (int row = 0; row < 7; row++) {
        for (int bit = 0; bit < 5; bit++) {
            if ((bmp[row] >> (4 - bit)) & 1u) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        px(x + bit*scale + sx, y + row*scale + sy, color);
            }
        }
    }
}

static void draw_str(INT32 x, INT32 y, const char *s, UINT32 color, int scale) {
    INT32 cx = x;
    for (; *s; s++, cx += (5+1)*scale) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        draw_char(cx, y, c, color, scale);
    }
}

static void draw_str_center(INT32 cx, INT32 y, const char *s, UINT32 color, int scale) {
    INT32 tw = (INT32)(soma_strlen(s) * (5+1) * scale);
    draw_str(cx - tw/2, y, s, color, scale);
}

/* ── Animation state ─────────────────────────────────────────────── */
static UINT32 g_tick = 0;

static struct { INT32 x, y; UINT32 speed; UINT32 col; } g_stars[80];
static struct { INT32 y; UINT32 col_idx; } g_rain[24];
static SomaSystemState g_soma;

/* Global Parallax Offsets */
static INT32 g_plx_x = 0;
static INT32 g_plx_y = 0;

static void init_stars(void) {
    for (int i = 0; i < 80; i++) {
        g_stars[i].x     = (INT32)(rng_next() % g_sw);
        g_stars[i].y     = (INT32)(rng_next() % g_sh);
        g_stars[i].speed = 1 + rng_next() % 3u;
        UINT32 bright    = 120u + rng_next() % 136u;
        g_stars[i].col   = SOMA_RGB(bright, bright, bright);
    }
}

static void init_rain(void) {
    for (int i = 0; i < 24; i++) {
        g_rain[i].y       = -(INT32)(rng_next() % 200u);
        g_rain[i].col_idx = rng_next() % 2u;
    }
}

/* ── LAYER 0: Background ─────────────────────────────────────────── */
static void render_background(UINT32 tick) {
    /* clear */
    for (UINT32 i = 0; i < g_sh * g_stride; i++) g_fb[i] = SOMA_DEEPSPACE;

    /* starfield — stars drift left */
    for (int i = 0; i < 80; i++) {
        g_stars[i].x -= (INT32)g_stars[i].speed;
        if (g_stars[i].x < 0) g_stars[i].x = (INT32)g_sw - 1;
        px(g_stars[i].x, g_stars[i].y, g_stars[i].col);
    }

    /* hex grid — dim perspective hexagons */
    for (INT32 gx = 0; gx < (INT32)g_sw; gx += 60) {
        for (INT32 gy = 0; gy < (INT32)g_sh; gy += 52) {
            INT32 r_hex = 24 + gy * 8 / (INT32)g_sh; /* larger near bottom */
            INT32 ox = (gy/52 & 1) ? 30 : 0;
            INT32 hcx = gx + ox - g_plx_x/2;
            INT32 hcy = gy - g_plx_y/2;
            for (int k = 0; k < 6; k++) {
                int a0 = k * 64 / 6, a1 = (k+1) * 64 / 6;
                INT32 x0 = hcx + r_hex * icos(a0) / 127;
                INT32 y0 = hcy + r_hex * isin(a0) / 127;
                INT32 x1 = hcx + r_hex * icos(a1) / 127;
                INT32 y1 = hcy + r_hex * isin(a1) / 127;
                draw_line(x0, y0, x1, y1, SOMA_GRID);
            }
        }
    }

    /* CRT scanlines every 3 rows */
    for (UINT32 y = 0; y < g_sh; y += 3) {
        for (UINT32 x = 0; x < g_sw; x++) {
            UINT32 p = g_fb[y * g_stride + x];
            UINT32 r = (p>>16)&0xFFu, gg = (p>>8)&0xFFu, b = p&0xFFu;
            r  = r  > 6u ? r  - 6u : 0u;
            gg = gg > 6u ? gg - 6u : 0u;
            b  = b  > 6u ? b  - 6u : 0u;
            g_fb[y * g_stride + x] = SOMA_RGB(r, gg, b);
        }
    }
    (void)tick;
}

/* ── LAYER 1: OO Sphere ──────────────────────────────────────────── */
static void render_sphere(UINT32 tick) {
    INT32 cx = W(500) - g_plx_x;
    INT32 cy = H(380) - g_plx_y;
    INT32 base_r = H(120);
    INT32 pulse   = isin(tick * 4u / 64u % 64u) * H(5) / 127;
    INT32 r       = base_r + pulse;
    UINT32 scol   = soma_state_color(g_soma.node_state);

    /* dim colour variants */
    UINT32 scol_dim = SOMA_RGB(
        (((scol>>16)&0xFFu) * 18u) / 255u,
        (((scol>>8 )&0xFFu) * 18u) / 255u,
        (((scol    )&0xFFu) * 18u) / 255u);
    UINT32 scol_med = SOMA_RGB(
        (((scol>>16)&0xFFu) * 60u) / 255u,
        (((scol>>8 )&0xFFu) * 60u) / 255u,
        (((scol    )&0xFFu) * 60u) / 255u);

    /* outer glow rings */
    draw_circle(cx, cy, r + H(40), scol_dim);
    draw_circle(cx, cy, r + H(20), scol_dim);
    draw_circle(cx, cy, r + H(8),  scol_med);

    /* equator */
    draw_circle(cx, cy, r, scol);

    /* latitude lines at +-30 deg: ry = r*cos(30) = r*87/100, ry2=r*cos(60)=r/2 */
    draw_ellipse(cx, cy - r*26/100, r, r*87/100, scol_med);
    draw_ellipse(cx, cy + r*26/100, r, r*87/100, scol_med);
    draw_ellipse(cx, cy - r*50/100, r, r*50/100, scol_dim);
    draw_ellipse(cx, cy + r*50/100, r, r*50/100, scol_dim);

    /* longitude arcs: two vertical great-circle halves, animated tilt */
    INT32 tilt = icos((int)(tick / 3u) & 63) * r / 127;
    for (int i = 0; i < 64; i++) {
        INT32 sx = cx + tilt * isin(i) / 127;
        INT32 sy = cy + r   * isin(i) / 127;
        px(sx, sy, scol_med);
        INT32 sx2 = cx - tilt * isin(i) / 127 + r * icos(i) / 127;
        INT32 sy2 = cy + r * isin(i) / 127;
        px(sx2, sy2, scol_dim);
    }

    /* orbital rings for swarm peers */
    UINT32 n_rings = g_soma.swarm_peer_count;
    if (n_rings > 4u) n_rings = 4u;
    for (UINT32 n = 0; n < n_rings; n++) {
        INT32 ring_rx = r + H(30) + (INT32)n * H(18);
        INT32 ring_ry = H(9) + (INT32)n * H(3);
        INT32 ring_off = (INT32)(tick / 2u + n * 16u) & 63;
        INT32 ring_cy  = cy - (INT32)n * H(8);
        UINT32 ring_col = (n < (UINT32)g_soma.peer_count && g_soma.peers[n].active) ?
            soma_state_color(g_soma.peers[n].state) : SOMA_CYAN_DIM;
        /* draw tilted orbit ellipse, shifted by ring_off phase */
        for (int i = 0; i < 64; i++) {
            int ai = (i + ring_off) & 63;
            INT32 rx = cx + ring_rx * icos(ai) / 127;
            INT32 ry = ring_cy + ring_ry * isin(ai) / 127;
            px_blend(rx, ry, ring_col, 140u);
        }
    }

    /* warden pressure arc around sphere */
    INT32 arc_r = r + H(16);
    INT32 arc_steps = (INT32)(64 * g_soma.warden_pressure / 255u);
    UINT32 arc_col = g_soma.warden_pressure > 180u ? SOMA_RED :
                     g_soma.warden_pressure > 120u ? SOMA_AMBER : SOMA_GREEN;
    for (INT32 i = -16; i < arc_steps - 16; i++) {
        INT32 ax = cx + arc_r * icos(i & 63) / 127;
        INT32 ay = cy + arc_r * isin(i & 63) / 127;
        px(ax, ay, arc_col);
        px(ax+1, ay, arc_col);
    }

    /* central core dot */
    INT32 core_r = g_soma.voice_active ? H(12) + isin((int)(tick*4u)&63)*H(4)/127 : H(6);
    UINT32 core_col = g_soma.voice_active ? SOMA_GREEN : scol;
    draw_circle(cx, cy, core_r, core_col);
    fill_rect(cx - H(3), cy - H(3), H(6), H(6), core_col);
}

/* ── LAYER 2: Data rain ──────────────────────────────────────────── */
static const char k_hex[] = "0123456789ABCDEF";

static void render_data_rain(UINT32 tick) {
    for (int i = 0; i < 24; i++) {
        UINT32 speed = 1u + (UINT32)i % 3u;
        g_rain[i].y += (INT32)speed;
        if (g_rain[i].y > (INT32)g_sh + 120) {
            g_rain[i].y = -(INT32)(rng_next() % 80u + 20u);
        }
        INT32 col_x = (INT32)g_sw * i / 24 + W(2);
        /* draw tail of 8 characters */
        for (int j = 0; j < 8; j++) {
            INT32 py = g_rain[i].y - j * 12;
            if (py < 0 || py >= (INT32)g_sh) continue;
            char ch = k_hex[(tick + (UINT32)(j + i)) % 16u];
            UINT32 fade = (UINT32)(8 - j) * 14u;
            UINT32 c = (j == 0) ? SOMA_CYAN :
                       SOMA_RGB(0u, fade < 80u ? fade : 80u, fade / 2u);
            draw_char(col_x, py, ch, c, 1);
        }
    }
    (void)tick;
}

/* ── LAYER 3: Code fragments ─────────────────────────────────────── */
static const struct { INT32 x_pm, y_pm; const char *text; UINT32 col; } k_frags[] = {
    {  2,  8, "FN WARDEN_CHECK(P: U8)", SOMA_CYAN_DIM },
    {  2, 16, "IF PRESSURE > THRESH {", SOMA_CYAN_DIM },
    {  2, 23, "SOMA_EVAL_DPLUS(ACT);",  SOMA_CYAN_DIM },
    { 70,  8, "#[REPR(C)]",             SOMA_CYAN_DIM },
    { 70, 14, "OO_ARENA_WEIGHTS",       SOMA_CYAN_DIM },
    { 70, 20, "PUB FN NEURAL_TICK()",   SOMA_CYAN_DIM },
};

static void render_code_frags(UINT32 tick) {
    for (int i = 0; i < 6; i++) {
        INT32 drift = isin((int)(tick / 4u + (UINT32)i * 10u) & 63) * 3 / 127;
        INT32 x = W((UINT32)k_frags[i].x_pm) + drift;
        INT32 y = H((UINT32)k_frags[i].y_pm);
        draw_str(x, y, k_frags[i].text, k_frags[i].col, 1);
    }
}

/* ── Panel helper ────────────────────────────────────────────────── */
static void draw_panel(INT32 x, INT32 y, INT32 w, INT32 h,
                       const char *title, UINT32 border_col) {
    /* Transparent glassmorphism tint instead of solid fill */
    tint_rect(x, y, w, h, SOMA_PANEL_BG, 170u);
    border_rect(x, y, w, h, 2, border_col);
    fill_rect(x+2, y+2, w-4, 12, border_col);
    /* title in dark colour over bright header bar */
    UINT32 dark = SOMA_RGB(2,3,6);
    draw_str(x+4, y+3, title, dark, 1);
}

static void draw_bar_horiz_particles(INT32 x, INT32 y, INT32 w, INT32 h,
                                     UINT32 used, UINT32 total, UINT32 col, UINT32 tick) {
    fill_rect(x, y, w, h, SOMA_RGB(8,8,16));
    border_rect(x, y, w, h, 1, SOMA_CYAN_DIM);
    if (total > 0u) {
        INT32 filled = (INT32)((UINT32)w * used / total);
        if (filled > w-2) filled = w-2;
        UINT32 r_base = (col >> 16) & 0xFFu;
        UINT32 g_base = (col >> 8) & 0xFFu;
        UINT32 b_base = col & 0xFFu;
        for (INT32 i = 0; i < filled; i++) {
            INT32 bright = isin((i * 8 - (int)tick * 4) & 63) * 60 / 127 + 60;
            if (bright > 90 || (i % 3 == 0)) {
                UINT32 cr = r_base * bright / 100u; if (cr > 255) cr = 255;
                UINT32 cg = g_base * bright / 100u; if (cg > 255) cg = 255;
                UINT32 cb = b_base * bright / 100u; if (cb > 255) cb = 255;
                vline(x + 1 + i, y + 1, h - 2, SOMA_RGB(cr, cg, cb));
            }
        }
    }
}

/* ── LAYER 4: Panels ─────────────────────────────────────────────── */
static void render_panels(UINT32 tick) {
    char buf[64]; int pos;

    /* Panel 1: SYNAPTIC BLUEPRINT (top-left) */
    INT32 p1x = W(5) + g_plx_x, p1y = H(5) + g_plx_y, p1w = W(270), p1h = H(250);
    draw_panel(p1x, p1y, p1w, p1h, "SYNAPTIC BLUEPRINT", SOMA_CYAN);
    INT32 ry = p1y + 18;

    /* Node state */
    const char *state_names[] = { "ACTIVE", "DEGRADED", "ISOLATED", "EMERGENCY", "SLEEPING" };
    UINT32 snames_n = 5;
    UINT32 sn = (UINT32)g_soma.node_state;
    if (sn >= snames_n) sn = 0;
    draw_str(p1x+4, ry, "STATE:", SOMA_CYAN_DIM, 1);
    draw_str(p1x+46, ry, state_names[sn], soma_state_color(g_soma.node_state), 1);
    ry += 12;

    /* Warden pressure bar */
    draw_str(p1x+4, ry, "WARDEN:", SOMA_CYAN_DIM, 1);
    UINT32 wp = g_soma.warden_pressure;
    UINT32 bar_col = wp > 180u ? SOMA_RED : wp > 120u ? SOMA_AMBER : SOMA_GREEN;
    draw_bar_horiz_particles(p1x+52, ry, p1w-58, 8, wp, 255u, bar_col, tick);
    ry += 14;

    /* Uptime */
    draw_str(p1x+4, ry, "UP:", SOMA_CYAN_DIM, 1);
    soma_fmt_time(g_soma.uptime_sec, buf);
    draw_str(p1x+22, ry, buf, SOMA_WHITE, 1);
    ry += 12;

    /* TPS */
    draw_str(p1x+4, ry, "TPS:", SOMA_CYAN_DIM, 1);
    pos = 0; soma_num_cat(buf, &pos, g_soma.tokens_per_sec);
    draw_str(p1x+28, ry, buf, SOMA_GREEN, 1);
    ry += 12;

    /* D+ mode */
    const char *dplus_names[] = { "SOLAR", "LUNAR", "SAFE" };
    draw_str(p1x+4, ry, "DPLUS:", SOMA_CYAN_DIM, 1);
    UINT32 dm = (UINT32)g_soma.dplus_mode;
    if (dm > 2u) dm = 2u;
    draw_str(p1x+44, ry, dplus_names[dm], SOMA_YELLOW, 1);
    ry += 12;

    /* Tokens generated */
    draw_str(p1x+4, ry, "TOKENS:", SOMA_CYAN_DIM, 1);
    pos = 0; soma_num_cat(buf, &pos, (UINT32)(g_soma.tokens_generated & 0xFFFFFFFFu));
    draw_str(p1x+50, ry, buf, SOMA_CYAN, 1);

    /* Panel 2: ANALYZE (top-right) */
    INT32 p2x = W(680) + g_plx_x, p2y = H(5) + g_plx_y, p2w = W(270), p2h = H(250);
    draw_panel(p2x, p2y, p2w, p2h, "ANALYZE", SOMA_MAGENTA);
    ry = p2y + 18;

    for (int i = 0; i < g_soma.arena_count && i < 5; i++) {
        const SomaArenaInfo *a = &g_soma.arenas[i];
        draw_str(p2x+4, ry, a->name, SOMA_CYAN_DIM, 1);
        UINT32 pct = a->total_mb > 0u ? (a->used_mb * 100u / a->total_mb) : 0u;
        UINT32 mcol = pct > 85u ? SOMA_RED : pct > 60u ? SOMA_AMBER : SOMA_MAGENTA;
        draw_bar_horiz_particles(p2x+4, ry+9, p2w-12, 7, a->used_mb, a->total_mb, mcol, tick);
        pos = 0; soma_num_cat(buf, &pos, pct); soma_str_cat(buf, &pos, "%");
        draw_str(p2x + p2w - 30, ry, buf, mcol, 1);
        ry += 22;
    }

    /* Panel 3: SYS_LOG (mid-left) */
    INT32 p3x = W(2) + g_plx_x, p3y = H(350) + g_plx_y, p3w = W(290), p3h = H(330);
    draw_panel(p3x, p3y, p3w, p3h, "SYS_LOG [SECTOR SCAN]", SOMA_GREEN);
    ry = p3y + 18;

    int ec = g_soma.event_count; if (ec > 6) ec = 6;
    for (int i = 0; i < ec; i++) {
        int idx = (g_soma.event_head - ec + i + 8) % 8;
        const SomaBusEvent *ev = &g_soma.events[idx];
        draw_str(p3x+4, ry, ev->kind, SOMA_GREEN, 1);
        draw_str(p3x+4, ry+10, ev->desc, SOMA_CYAN_DIM, 1);
        ry += 24;
    }

    /* Panel 4: NEURAL NODES (mid-right) */
    INT32 p4x = W(700) + g_plx_x, p4y = H(350) + g_plx_y, p4w = W(270), p4h = H(330);
    draw_panel(p4x, p4y, p4w, p4h, "NEURAL NODES", SOMA_AMBER);
    ry = p4y + 18;

    for (int i = 0; i < g_soma.peer_count && i < 6; i++) {
        const SomaPeer *peer = &g_soma.peers[i];
        UINT32 dot_col = peer->active ? soma_state_color(peer->state) : SOMA_CYAN_DIM;
        fill_rect(p4x+4, ry+1, 6, 6, dot_col);
        draw_str(p4x+14, ry, peer->peer_id, peer->active ? SOMA_WHITE : SOMA_CYAN_DIM, 1);
        pos = 0; soma_num_cat(buf, &pos, peer->latency_ms); soma_str_cat(buf, &pos, "MS");
        draw_str(p4x + p4w - 36, ry, buf, SOMA_CYAN_DIM, 1);
        ry += 14;
    }
    pos = 0;
    soma_str_cat(buf, &pos, "PEERS: ");
    soma_num_cat(buf, &pos, (UINT32)g_soma.peer_count);
    soma_str_cat(buf, &pos, "/6");
    draw_str(p4x+4, p4y + p4h - 14, buf, SOMA_AMBER, 1);

    (void)tick;
}

/* ── LAYER 5: Chat panel ─────────────────────────────────────────── */
static void render_chat(UINT32 tick) {
    INT32 cx = W(50) + g_plx_x, cy = H(755) + g_plx_y, cw = W(900), ch = H(195);
    tint_rect(cx, cy, cw, ch, SOMA_PANEL_BG, 170u);
    border_rect(cx, cy, cw, ch, 2, SOMA_CYAN);

    draw_str_center(cx + cw/2, cy + 4, "[ STREAM OF CONSCIOUSNESS ]", SOMA_CYAN, 1);

    /* Left: Voice waveform (Cymatics Ring) */
    INT32 wave_cx = cx + cw * 2 / 10;
    INT32 wave_cy = cy + ch / 2;
    INT32 base_r = H(30);

    UINT32 wlabel_col = g_soma.voice_active ? SOMA_GREEN : SOMA_CYAN_DIM;
    draw_str_center(wave_cx, wave_cy + base_r + H(20), "VOICE CYMATICS", wlabel_col, 1);

    for (int i = 0; i < 64; i++) {
        UINT32 amp = g_soma.voice_waveform[i];
        INT32 bump = (INT32)(amp * 25u / 255u);
        INT32 r_out = base_r + bump;
        INT32 x_out = wave_cx + r_out * icos(i) / 127;
        INT32 y_out = wave_cy + r_out * isin(i) / 127;
        INT32 x_in  = wave_cx + base_r * icos(i) / 127;
        INT32 y_in  = wave_cy + base_r * isin(i) / 127;
        
        UINT32 bc = g_soma.voice_active ? SOMA_GREEN : SOMA_CYAN_DIM;
        draw_line(x_in, y_in, x_out, y_out, bc);
        
        if (i > 0) {
            INT32 prev_amp = g_soma.voice_waveform[i-1];
            INT32 prev_r = base_r + (INT32)(prev_amp * 25u / 255u);
            INT32 px_out = wave_cx + prev_r * icos(i-1) / 127;
            INT32 py_out = wave_cy + prev_r * isin(i-1) / 127;
            draw_line(px_out, py_out, x_out, y_out, bc);
        } else if (i == 0) { 
            INT32 prev_amp = g_soma.voice_waveform[63];
            INT32 prev_r = base_r + (INT32)(prev_amp * 25u / 255u);
            INT32 px_out = wave_cx + prev_r * icos(63) / 127;
            INT32 py_out = wave_cy + prev_r * isin(63) / 127;
            draw_line(px_out, py_out, x_out, y_out, bc);
        }
    }

    /* Right: Text response + input */
    INT32 txt_x = cx + cw * 45 / 100;
    INT32 txt_y = cy + 18;
    INT32 txt_w = cw - (txt_x - cx) - 4;
    INT32 txt_h = ch - 30;

    border_rect(txt_x, txt_y, txt_w, txt_h, 1, SOMA_CYAN_DIM);

    /* Show last ~3 lines of response_buf */
    const char *resp = g_soma.response_buf;
    int rlen = soma_strlen(resp);
    /* find start 3 lines back */
    int line_start = 0, newlines = 0;
    for (int i = rlen-1; i >= 0; i--) {
        if (resp[i] == '\n') { newlines++; if (newlines >= 3) { line_start = i+1; break; } }
    }
    INT32 line_y = txt_y + 4;
    int lx = 0;
    char line_buf[64];
    for (int i = line_start; i <= rlen && line_y < txt_y + txt_h - 22; i++) {
        char c = (i < rlen) ? resp[i] : '\n';
        if (c == '\n' || lx >= 50) {
            line_buf[lx] = 0;
            if (lx > 0) { draw_str(txt_x+4, line_y, line_buf, SOMA_WHITE, 1); line_y += 10; }
            lx = 0;
        } else {
            if (lx < 63) line_buf[lx++] = c;
        }
    }

    /* Input prompt + blinking cursor */
    INT32 inp_y = txt_y + txt_h - 18;
    fill_rect(txt_x+2, inp_y-2, txt_w-4, 16, SOMA_RGB(5,10,20));
    char inp_line[140];
    int ip = 0;
    soma_str_cat(inp_line, &ip, "OO> ");
    for (int i = 0; i < g_soma.input_len && i < 50; i++) inp_line[ip++] = g_soma.input_buf[i];
    if ((tick / 30u) & 1u) inp_line[ip++] = '_';
    inp_line[ip] = 0;
    draw_str(txt_x+4, inp_y, inp_line, SOMA_CYAN, 1);
}

/* ── LAYER 6: HUD corners ────────────────────────────────────────── */
static void render_hud_corners(UINT32 tick) {
    char buf[64]; int pos;

    /* Top-left */
    draw_str(W(3), H(3), "STREAM OF CONSCIOUSNESS", SOMA_CYAN, 1);
    draw_str(W(3), H(3)+10, g_soma.organism_id, SOMA_CYAN_DIM, 1);

    const char *dplus_names2[] = { "SOLAR", "LUNAR", "SAFE" };
    UINT32 dm = (UINT32)g_soma.dplus_mode; if (dm > 2u) dm = 2u;
    pos = 0; soma_str_cat(buf, &pos, "D+:"); soma_str_cat(buf, &pos, dplus_names2[dm]);
    draw_str(W(3), H(3)+20, buf, SOMA_YELLOW, 1);

    /* Top-right: clock + peers */
    char tstr[12];
    if (g_soma.tsc_hz > 0u) {
        UINT32 sec = (UINT32)(g_soma.tsc_now / g_soma.tsc_hz);
        soma_fmt_time(sec, tstr);
    } else {
        soma_fmt_time(g_soma.uptime_sec, tstr);
    }
    draw_str(W(850), H(3), tstr, SOMA_CYAN, 1);
    pos = 0; soma_str_cat(buf, &pos, "PEERS:");
    soma_num_cat(buf, &pos, g_soma.swarm_peer_count);
    draw_str(W(850), H(3)+10, buf, SOMA_CYAN_DIM, 1);

    /* Bottom-left: warden value */
    pos = 0; soma_str_cat(buf, &pos, "WARDEN:");
    soma_num_cat(buf, &pos, g_soma.warden_pressure);
    UINT32 wp = g_soma.warden_pressure;
    UINT32 wc = wp > 180u ? SOMA_RED : wp > 120u ? SOMA_AMBER : SOMA_GREEN;
    draw_str(W(3), H(960), buf, wc, 1);
    draw_bar_horiz_particles(W(3), H(960)+10, W(80), 6, wp, 255u, wc, tick);

    /* Bottom-right: TPS + version */
    pos = 0; soma_str_cat(buf, &pos, "TPS:");
    soma_num_cat(buf, &pos, g_soma.tokens_per_sec);
    draw_str(W(880), H(960), buf, SOMA_GREEN, 1);
    draw_str(W(880), H(960)+10, "SOMA V1.0", SOMA_CYAN_DIM, 1);

    (void)tick;
}

/* ── LAYER 7: Glitch ─────────────────────────────────────────────── */
static void render_glitch(UINT32 tick) {
    if (g_soma.warden_pressure > 180u) {
        if (g_soma.glitch_frames <= 0) g_soma.glitch_frames = 8;
    }
    if (g_soma.glitch_frames <= 0) { (void)tick; return; }

    g_soma.glitch_frames--;
    UINT32 n_bands = 3u + rng_next() % 3u;
    for (UINT32 b = 0; b < n_bands; b++) {
        UINT32 band_y = rng_next() % g_sh;
        UINT32 band_h = 2u + rng_next() % 12u;
        UINT32 xor_val = rng_next();
        UINT32 shift   = rng_next() % 20u;
        for (UINT32 y = band_y; y < band_y + band_h && y < g_sh; y++) {
            for (UINT32 x = 0; x < g_sw; x++) {
                UINT32 src_x = (x + shift) % g_sw;
                g_fb[y * g_stride + x] ^= (g_fb[y * g_stride + src_x] & xor_val);
            }
        }
    }
    (void)tick;
}

/* ── Master render ───────────────────────────────────────────────── */
static void soma_render_frame(void) {
    /* Auto-Parallax "Breathing" Simulation */
    g_plx_x = isin((int)(g_tick / 8u) & 63) * 25 / 127;
    g_plx_y = icos((int)(g_tick / 11u) & 63) * 15 / 127;

    render_background(g_tick);
    render_data_rain(g_tick);
    render_sphere(g_tick);
    render_code_frags(g_tick);
    render_panels(g_tick);
    render_chat(g_tick);
    render_hud_corners(g_tick);
    render_glitch(g_tick);
    g_tick++;
}

/* ── Demo state fill (no hardware required) ──────────────────────── */
void soma_state_demo_fill(SomaSystemState *st, uint32_t tick) {
    /* organism id */
    const char *oid = "OO-SOMA-001";
    int k = 0;
    while (oid[k]) { st->organism_id[k] = oid[k]; k++; }
    st->organism_id[k] = 0;

    /* cycle node state every 300 ticks */
    st->node_state = (OoNodeState)((tick / 300u) % 5u);

    /* warden pressure: sine wave */
    UINT32 wp_raw = (UINT32)(isin((int)(tick / 30u) & 63));
    st->warden_pressure = (uint8_t)((wp_raw + 127u) & 0xFFu);

    /* swarm peer oscillation */
    st->swarm_peer_count = (tick / 120u) % 5u;

    /* uptime */
    st->uptime_sec = tick / 60u;

    /* tokens per second: 45 + variation */
    UINT32 tvar = (UINT32)(isin((int)(tick / 20u) & 63) + 127);
    st->tokens_per_sec = 45u + tvar / 8u;
    st->tokens_generated += st->tokens_per_sec / 60u;

    /* D+ mode cycles */
    st->dplus_mode = (DplusMode)((tick / 400u) % 3u);

    /* arenas */
    st->arena_count = 5;
    const char *anames[] = { "WEIGHTS", "KV-CACHE", "SCRATCH", "ACTIVAT", "ZONE-C" };
    UINT32 atot[] = { 4096, 2048, 512, 1024, 256 };
    for (int i = 0; i < 5; i++) {
        int j = 0;
        while (anames[i][j]) { st->arenas[i].name[j] = anames[i][j]; j++; }
        st->arenas[i].name[j] = 0;
        st->arenas[i].total_mb = atot[i];
        UINT32 used_pct = 30u + (UINT32)(isin((int)(tick/40u + (UINT32)i*10u) & 63) + 127u)
                          * 55u / 255u;
        st->arenas[i].used_mb = atot[i] * used_pct / 100u;
    }

    /* bus events */
    if ((tick % 90u) == 0u) {
        const char *kinds[] = { "TOKEN", "WARDEN", "DPLUS", "ARENA", "PEER", "EVAL" };
        const char *descs[] = {
            "INFERENCE STEP DONE",
            "PRESSURE THRESHOLD HIT",
            "MODE SWITCH SOLAR->LUNAR",
            "REALLOC SCRATCH 512MB",
            "PEER-003 JOINED SWARM",
            "SOMA_EVAL_DPLUS CALLED"
        };
        int idx = tick / 90u % 6;
        int eh = st->event_head;
        const char *kk = kinds[idx];
        int ki = 0; while (kk[ki] && ki < 15) { st->events[eh].kind[ki] = kk[ki]; ki++; }
        st->events[eh].kind[ki] = 0;
        const char *dd = descs[idx];
        int di = 0; while (dd[di] && di < 47) { st->events[eh].desc[di] = dd[di]; di++; }
        st->events[eh].desc[di] = 0;
        st->events[eh].tsc = tick;
        st->event_head = (eh + 1) % 8;
        if (st->event_count < 8) st->event_count++;
    }

    /* peers */
    st->peer_count = (int)st->swarm_peer_count;
    const char *pnames[] = { "PEER-001","PEER-002","PEER-003","PEER-004","PEER-005","PEER-006" };
    for (int i = 0; i < 6; i++) {
        int j = 0; while (pnames[i][j]) { st->peers[i].peer_id[j] = pnames[i][j]; j++; }
        st->peers[i].peer_id[j] = 0;
        st->peers[i].state     = (OoNodeState)(((UINT32)i + tick/200u) % 5u);
        st->peers[i].latency_ms = 10u + (UINT32)i * 15u + (UINT32)(isin((int)(tick/25u+(UINT32)i*7u)&63)+127u)/10u;
        st->peers[i].active    = (i < st->peer_count) ? 1 : 0;
    }

    /* voice waveform: sine at varying frequencies */
    for (int i = 0; i < 64; i++) {
        UINT32 freq = 2u + (UINT32)i % 4u;
        INT32 v = isin((int)(tick * freq / 4u + (UINT32)i * 3u) & 63);
        st->voice_waveform[i] = (uint8_t)((v + 127) & 0xFF);
    }
    st->voice_active = (tick / 150u) % 2u;

    /* response_buf: static demo */
    if (tick == 1u) {
        const char *rdemo =
            "SOMA BOOT OK. NEURAL ENGINE READY.\n"
            "WARDEN INIT: PRESSURE NOMINAL.\n"
            "D+ MODE: SOLAR — INFERENCE ACTIVE.";
        int ri = 0;
        while (rdemo[ri] && ri < 383) { st->response_buf[ri] = rdemo[ri]; ri++; }
        st->response_buf[ri] = 0;
    }
}

/* ── UART bridge (COM1 @ 0x3F8, 38400 baud) ──────────────────────── */
#ifdef OO_HUD_STANDALONE
static inline void soma_outb(UINT16 port, UINT8 val) {
    __asm__ volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}
static inline UINT8 soma_inb(UINT16 port) {
    UINT8 v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

void soma_uart_init(void) {
    soma_outb(0x3F9u, 0x00u); /* disable interrupts */
    soma_outb(0x3FBu, 0x80u); /* DLAB on */
    soma_outb(0x3F8u, 0x03u); /* divisor lo (38400 baud) */
    soma_outb(0x3F9u, 0x00u); /* divisor hi */
    soma_outb(0x3FBu, 0x03u); /* 8N1 */
    soma_outb(0x3FAu, 0xC7u); /* FIFO */
    soma_outb(0x3FCu, 0x0Bu); /* modem ctrl */
}

/* parse "OO:<KEY>=<VALUE>\n" lines */
static char g_uart_buf[128];
static int  g_uart_pos = 0;

static void soma_uart_parse(SomaSystemState *st, const char *line, int len) {
    /* check prefix "OO:" */
    if (len < 5) return;
    if (line[0]!='O'||line[1]!='O'||line[2]!=':') return;
    const char *kv = line + 3;
    /* find '=' */
    int eq = 0;
    while (kv[eq] && kv[eq] != '=' && eq < len-3) eq++;
    if (!kv[eq]) return;
    /* compare key */
    if (eq==5&&kv[0]=='S'&&kv[1]=='T'&&kv[2]=='A'&&kv[3]=='T'&&kv[4]=='E') {
        const char *v = kv+eq+1;
        if (v[0]=='A') st->node_state = OO_ACTIVE;
        else if (v[0]=='D') st->node_state = OO_DEGRADED;
        else if (v[0]=='I') st->node_state = OO_ISOLATED;
        else if (v[0]=='E') st->node_state = OO_EMERGENCY;
        else if (v[0]=='S') st->node_state = OO_SLEEPING;
    } else if (eq==8&&kv[0]=='P'&&kv[1]=='R'&&kv[2]=='E'&&kv[3]=='S') {
        UINT32 val=0; const char *v=kv+eq+1;
        while (*v>='0'&&*v<='9') { val=val*10u+(UINT32)(*v-'0'); v++; }
        if (val>255u) val=255u;
        st->warden_pressure=(uint8_t)val;
    } else if (eq==5&&kv[0]=='P'&&kv[1]=='E'&&kv[2]=='E'&&kv[3]=='R'&&kv[4]=='S') {
        UINT32 val=0; const char *v=kv+eq+1;
        while (*v>='0'&&*v<='9') { val=val*10u+(UINT32)(*v-'0'); v++; }
        if (val>6u) val=6u;
        st->swarm_peer_count=val;
    } else if (eq==5&&kv[0]=='D'&&kv[1]=='P'&&kv[2]=='L'&&kv[3]=='U'&&kv[4]=='S') {
        const char *v=kv+eq+1;
        if (v[0]=='S'&&v[1]=='O') st->dplus_mode=DPLUS_SOLAR;
        else if (v[0]=='L') st->dplus_mode=DPLUS_LUNAR;
        else if (v[0]=='S'&&v[1]=='A') st->dplus_mode=DPLUS_SAFE;
    } else if (eq==3&&kv[0]=='T'&&kv[1]=='P'&&kv[2]=='K') {
        UINT32 val=0; const char *v=kv+eq+1;
        while (*v>='0'&&*v<='9') { val=val*10u+(UINT32)(*v-'0'); v++; }
        st->tokens_per_sec=val;
    }
}
#endif /* OO_HUD_STANDALONE */

#ifdef OO_HUD_STANDALONE
void soma_uart_poll(SomaSystemState *st) {
    /* read available bytes from UART FIFO */
    while (soma_inb(0x3FDu) & 0x01u) {
        UINT8 ch = soma_inb(0x3F8u);
        if (ch == '\n' || ch == '\r') {
            if (g_uart_pos > 0) {
                g_uart_buf[g_uart_pos] = 0;
                soma_uart_parse(st, g_uart_buf, g_uart_pos);
                g_uart_pos = 0;
            }
        } else if (g_uart_pos < 127) {
            g_uart_buf[g_uart_pos++] = (char)ch;
        }
    }
}

/* ── EFI entry point ─────────────────────────────────────────────── */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    /* Locate GOP */
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS st = uefi_call_wrapper(
        SystemTable->BootServices->LocateProtocol, 3,
        &gopGuid, NULL, (VOID **)&gop);

    if (EFI_ERROR(st) || gop == NULL) {
        Print(L"SOMA: GOP not found\r\n");
        return EFI_UNSUPPORTED;
    }

    /* Grab framebuffer info */
    g_fb     = (UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
    g_sw     = gop->Mode->Info->HorizontalResolution;
    g_sh     = gop->Mode->Info->VerticalResolution;
    g_stride = gop->Mode->Info->PixelsPerScanLine;

    if (!g_fb || g_sw == 0 || g_sh == 0) {
        Print(L"SOMA: Bad framebuffer\r\n");
        return EFI_UNSUPPORTED;
    }

    /* Hide text cursor */
    uefi_call_wrapper(SystemTable->ConOut->EnableCursor, 2,
                      SystemTable->ConOut, FALSE);

    /* Seed RNG with framebuffer base */
    g_rng ^= (UINT32)(UINTN)g_fb;

    /* Init subsystems */
    init_stars();
    init_rain();
    soma_uart_init();

    /* Bootstrap demo state */
    soma_state_demo_fill(&g_soma, 1);

    /* Main render loop */
    while (1) {
        soma_uart_poll(&g_soma);
        soma_state_demo_fill(&g_soma, g_tick);
        soma_render_frame();

        /* Keyboard input */
        EFI_INPUT_KEY key;
        if (!EFI_ERROR(uefi_call_wrapper(
                SystemTable->ConIn->ReadKeyStroke, 2,
                SystemTable->ConIn, &key))) {
            if (key.ScanCode == 0x0C) break; /* F2 = exit */
            if (key.UnicodeChar != 0) {
                if (key.UnicodeChar == L'\r') {
                    /* Enter: echo to response_buf */
                    g_soma.input_buf[g_soma.input_len] = 0;
                    int rpos = soma_strlen(g_soma.response_buf);
                    if (rpos < 370) {
                        g_soma.response_buf[rpos++] = '\n';
                        for (int ci = 0; ci < g_soma.input_len && rpos < 382; ci++)
                            g_soma.response_buf[rpos++] = g_soma.input_buf[ci];
                        g_soma.response_buf[rpos] = 0;
                    }
                    g_soma.input_len = 0;
                } else if (key.UnicodeChar == 0x0008 && g_soma.input_len > 0) {
                    g_soma.input_len--;
                } else if (g_soma.input_len < 127) {
                    g_soma.input_buf[g_soma.input_len++] = (char)key.UnicodeChar;
                }
            }
        }

        /* ~60 fps frame timing */
        uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 16000u);
    }

    return EFI_SUCCESS;
}
#endif /* OO_HUD_STANDALONE */
