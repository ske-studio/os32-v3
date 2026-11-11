#include "gfx/v2/gfx2_sw.h"
static u32 pal[256];
static int probe(void)
{
    struct gfx2_fmt_info f;
    u32 fmt, x, v, y;
    u8 p[5];
    if (gfx2_fmt_info(0, &f) != OS32_ERR_INVAL || gfx2_fmt_info(11, &f) != OS32_ERR_INVAL ||
        gfx2_fmt_info(0x40, &f) != OS32_ERR_INVAL) return 1;
    for (fmt = 1; fmt <= 10; fmt++) {
        if (gfx2_fmt_info(fmt, &f) || !f.cpp || f.bpp != f.cpp * 8) return 2;
        p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 0xa5;
        gfx2_px_write(p, fmt, 0x78563412u);
        v = f.cpp == 1 ? 0x12u : f.cpp == 2 ? 0x3412u : 0x78563412u;
        if (gfx2_px_read(p, fmt) != v || p[f.cpp] != (f.cpp == 4 ? 0xa5 : 0)) return 3;
    }
    for (fmt = GFX2_FMT_RGB565; fmt <= GFX2_FMT_ARGB1555; fmt++) {
        if (fmt == GFX2_FMT_XRGB8888 || fmt == GFX2_FMT_ARGB8888) continue;
        for (x = 0; x < 65536; x++) {
            y = gfx2_to_argb(fmt, x, pal);
            if (gfx2_from_argb(fmt, y, &v)) return 4;
            if (v != (fmt == GFX2_FMT_XRGB1555 ? x | 0x8000u : x)) return 4;
        }
    }
    for (x = 0; x < 256; x++) {
        u32 argb = (x << 24) | (x << 16) | (x << 8) | x;
        if (gfx2_from_argb(GFX2_FMT_ARGB8888, argb, &v) || v != argb ||
            gfx2_to_argb(GFX2_FMT_ARGB8888, v, pal) != argb) return 5;
        if (gfx2_from_argb(GFX2_FMT_XRGB8888, argb, &v) || v != (argb | 0xff000000u) ||
            gfx2_to_argb(GFX2_FMT_XRGB8888, v, pal) != v) return 5;
    }
    if (gfx2_to_argb(GFX2_FMT_RGB565, 0xffff, pal) != 0xffffffffu ||
        gfx2_to_argb(GFX2_FMT_RGB565, 0x07e0, pal) != 0xff00ff00u) return 10;
    pal[42] = 0x123456;
    if (gfx2_to_argb(GFX2_FMT_INDEX8, 42, pal) != 0xff123456u ||
        gfx2_from_argb(GFX2_FMT_INDEX8, 0, &v) != OS32_ERR_NOSYS) return 6;
    if (gfx2_yuv_argb(16, 128, 128) != 0xff000000u ||
        gfx2_yuv_argb(235, 128, 128) != 0xffffffffu ||
        gfx2_yuv_argb(81, 90, 240) != 0xffff0000u ||
        gfx2_yuv_argb(17, 128, 128) != 0xff010101u) return 7;
    for (x = 0; x <= 65025; x++) if (gfx2_div255(x) != (x + 127) / 255) return 8;
    if (gfx2_to_argb(GFX2_FMT_A8, 0x80, pal) != 0x80000000u ||
        gfx2_to_argb(GFX2_FMT_L8, 0x80, pal) != 0xff808080u ||
        gfx2_from_argb(GFX2_FMT_A8, 0xabcdef12u, &v) || v != 0xab ||
        gfx2_from_argb(GFX2_FMT_L8, 0xff808080u, &v) || v != 0x80) return 9;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
