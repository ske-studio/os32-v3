#include "libos32gfx.h"
#include "os32api.h"

/* lib/utf8.c equivalents for kcg */
/* (Currently we will just declare what we need or implement lightweight versions) */
extern int kcg_scale;
int kcg_scale = 1;

void kcg_set_scale(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    kcg_scale = scale;
}

static void draw_scaled_pixel(int x, int y, u8 color) {
    int sx, sy;
    if (kcg_scale == 1) {
        gfx_pixel(x, y, color);
        return;
    }
    for (sy = 0; sy < kcg_scale; sy++) {
        for (sx = 0; sx < kcg_scale; sx++) {
            gfx_pixel(x + sx, y + sy, color);
        }
    }
}

void kcg_draw_ank(int x, int y, u8 ch, u8 fg, u8 bg) {
    u8 pat[16];
    int row, col;
    if (bg != 0xFF) gfx_fill_rect(x, y, 8 * kcg_scale, 16 * kcg_scale, bg);
    gfx_api->kcg_read_ank(ch, pat);
    if (kcg_scale == 1) {
        extern void gfx_draw_font(int x, int y, const u8 *pat, int w_bytes, int h_lines, u8 fg);
        gfx_draw_font(x, y, pat, 1, 16, fg);
    } else {
        for (row = 0; row < 16; row++) {
            u8 bits = pat[row];
            for (col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) draw_scaled_pixel(x + col * kcg_scale, y + row * kcg_scale, fg);
            }
        }
    }
}

void kcg_draw_kanji(int x, int y, u16 jis_code, u8 fg, u8 bg) {
    u8 pat[32];
    int row, col;
    if (bg != 0xFF) gfx_fill_rect(x, y, 16 * kcg_scale, 16 * kcg_scale, bg);
    gfx_api->kcg_read_kanji(jis_code, pat);
    if (kcg_scale == 1) {
        extern void gfx_draw_font(int x, int y, const u8 *pat, int w_bytes, int h_lines, u8 fg);
        gfx_draw_font(x, y, pat, 2, 16, fg);
    } else {
        for (row = 0; row < 16; row++) {
            u8 left  = pat[row * 2];
            u8 right = pat[row * 2 + 1];
            for (col = 0; col < 8; col++) {
                if (left & (0x80 >> col)) draw_scaled_pixel(x + col * kcg_scale, y + row * kcg_scale, fg);
            }
            for (col = 0; col < 8; col++) {
                if (right & (0x80 >> col)) draw_scaled_pixel(x + (8 + col) * kcg_scale, y + row * kcg_scale, fg);
            }
        }
    }
}

/* utf8.h replacements -> using lib/utf8.c if linked, or we disable sjis/utf8 for libos32gfx for now */
extern u8 unicode_to_ank(u32 cp);
extern u16 unicode_to_jis(u32 cp);
typedef struct { u32 codepoint; int bytes_used; } utf8_decode_t;
extern utf8_decode_t utf8_decode(const u8 *str);

int kcg_draw_utf8(int x, int y, const char *utf8_str, u8 fg, u8 bg) {
    int cx = x;
    const u8 *p = (const u8 *)utf8_str;
    int scale = kcg_scale;
    int ank_w = 8 * scale;
    int kanji_w = 16 * scale;
    int line_h = 18 * scale;

    while (*p) {
        utf8_decode_t dec;
        u32 cp;
        u8 ank;
        u16 jis;
        if (*p == '\n') { cx = x; y += line_h; p++; continue; }
        dec = utf8_decode(p);
        cp = dec.codepoint;
        p += dec.bytes_used;
        if (cp == 0xFEFF || cp < 0x20) {
            if (cp == '\t') cx += ank_w * 4;
            continue;
        }
        ank = unicode_to_ank(cp);
        if (ank) { kcg_draw_ank(cx, y, ank, fg, bg); cx += ank_w; }
        else {
            jis = unicode_to_jis(cp);
            if (!jis) jis = 0x2222;
            kcg_draw_kanji(cx, y, jis, fg, bg); cx += kanji_w;
        }
    }
    return cx - x;
}
